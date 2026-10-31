// Adapted from GTG Cell Studio desktop components; scientific engine remains separate.
#include "ide/assistant/cloud_copilot_client.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <functional>
#include <cmath>

namespace penta::assistant {
namespace {
constexpr qsizetype MaxRequest=256*1024,MaxEvent=256*1024,MaxText=1024*1024,MaxWire=4*1024*1024;
bool cleanUrl(const QUrl& u){return u.isValid()&&u.scheme()=="https"&&u.userInfo().isEmpty()&&u.query().isEmpty()&&u.fragment().isEmpty()&&(u.port(-1)==-1||u.port()==443);}
bool azureHost(const QString& host){
    static const QRegularExpression resource("^[a-z0-9][a-z0-9-]{0,62}$");
    for(const auto& suffix:{QString(".openai.azure.com"),QString(".services.ai.azure.com")})
        if(host.endsWith(suffix)&&resource.match(host.left(host.size()-suffix.size())).hasMatch())return true;
    return false;
}
QString textFromResponse(const QJsonObject& response){
    QString text;
    for(const auto& item:response.value("output").toArray())for(const auto& content:item.toObject().value("content").toArray()){
        const auto part=content.toObject();if(part.value("type").toString()=="output_text")text+=part.value("text").toString();
    }
    return text;
}
QString httpError(int status){
    const auto code=status>=100&&status<=599?QString(" (HTTP %1)").arg(status):QString();
    if(status==401||status==403)return "The provider rejected the credentials or access"+code+". Check the API key, model and permissions.";
    if(status==429)return "The provider's quota or rate limit was reached"+code+". Check your API usage and billing before retrying.";
    if(status>=300&&status<400)return "The provider redirected the request"+code+". Redirects are disabled to protect credentials.";
    if(status==400||status==404)return "The provider rejected the request"+code+". Check the selected model or Azure deployment and endpoint.";
    if(status>=500)return "The provider is temporarily unavailable"+code+". No automatic retry was made.";
    return "The cloud request failed"+code+". Check your connection and provider configuration.";
}
bool hasProviderError(const QJsonValue& value){
    if(value.isUndefined()||value.isNull())return false;
    if(value.isObject())return !value.toObject().isEmpty();
    if(value.isArray())return !value.toArray().isEmpty();
    if(value.isString())return !value.toString().trimmed().isEmpty();
    if(value.isBool())return value.toBool();
    return value.toDouble()!=0;
}
qint64 tokenCount(const QJsonObject& object,const char* field){
    const auto value=object.value(QLatin1String(field));if(!value.isDouble())return -1;
    const auto number=value.toDouble();return std::isfinite(number)&&number>=0&&number<=1.0e12&&std::floor(number)==number?static_cast<qint64>(number):-1;
}
bool validResponseId(const QString& id){
    static const QRegularExpression pattern("\\Aresp_[A-Za-z0-9_-]{1,200}\\z");return pattern.match(id).hasMatch();
}
CloudTokenUsage responseUsage(const QJsonObject& object){
    CloudTokenUsage result;result.inputTokens=tokenCount(object,"input_tokens");result.outputTokens=tokenCount(object,"output_tokens");result.totalTokens=tokenCount(object,"total_tokens");
    const auto input=object.value("input_tokens_details").toObject();result.cachedInputTokens=tokenCount(input,"cached_tokens");result.cacheWriteTokens=tokenCount(input,"cache_write_tokens");
    result.reasoningTokens=tokenCount(object.value("output_tokens_details").toObject(),"reasoning_tokens");return result;
}
CloudTokenUsage geminiUsage(const QJsonObject& object){
    CloudTokenUsage result;result.inputTokens=tokenCount(object,"promptTokenCount");result.totalTokens=tokenCount(object,"totalTokenCount");
    const auto candidates=tokenCount(object,"candidatesTokenCount"),thoughts=tokenCount(object,"thoughtsTokenCount");
    result.reasoningTokens=thoughts;
    if(candidates>=0)result.outputTokens=candidates+(thoughts>=0?thoughts:0);
    // Google omits zero-valued optional protobuf counters in JSON. This client
    // does not create explicit cache resources or enable any tool calls.
    result.cachedInputTokens=object.contains("cachedContentTokenCount")?tokenCount(object,"cachedContentTokenCount"):0;
    result.cacheWriteTokens=0;return result;
}
}
double cloudEstimatedCostUsd(const CloudTokenUsage& usage,const CloudTokenPrices& prices){
    if(!prices.enabled)return -1;
    for(const auto rate:{prices.input,prices.cachedInput,prices.cacheWrite,prices.output})if(!std::isfinite(rate)||rate<0||rate>1.0e6)return -1;
    if(usage.inputTokens<0||usage.outputTokens<0)return -1;
    // A missing split can only be priced if all possible input categories have
    // the same user-supplied rate. Otherwise report unknown, not a false zero.
    if(usage.cachedInputTokens<0||usage.cacheWriteTokens<0){
        if(prices.input!=prices.cachedInput||prices.input!=prices.cacheWrite)return -1;
        return (usage.inputTokens*prices.input+usage.outputTokens*prices.output)/1.0e6;
    }
    if(usage.cachedInputTokens>usage.inputTokens||usage.cacheWriteTokens>usage.inputTokens-usage.cachedInputTokens)return -1;
    const auto ordinary=usage.inputTokens-usage.cachedInputTokens-usage.cacheWriteTokens;
    return (ordinary*prices.input+usage.cachedInputTokens*prices.cachedInput+usage.cacheWriteTokens*prices.cacheWrite+usage.outputTokens*prices.output)/1.0e6;
}
QString cloudUsageSummary(const CloudTokenUsage& usage,const CloudTokenPrices& prices){
    const auto number=[](qint64 n){return n>=0?QString::number(n):QString("unknown");};
    if(!usage.available())return "Token usage not reported by the provider.";
    auto result=QString("Input %1 · cached %2 · cache writes %3 · output %4").arg(number(usage.inputTokens),number(usage.cachedInputTokens),number(usage.cacheWriteTokens),number(usage.outputTokens));
    const auto cost=cloudEstimatedCostUsd(usage,prices);
    if(prices.enabled)result+=cost>=0?QString(" · estimated $%1 (your rates)").arg(cost,0,'f',6):QString(" · cost unknown (incomplete usage/rates)");
    return result;
}
namespace cloud_copilot_detail {
QString safeProviderError(CloudProvider provider,const QJsonObject& envelope,int httpStatus){
    // Only structured machine fields are consulted. Free-form messages may
    // contain credentials, user source, provider internals or rich text.
    QJsonObject detail=envelope;
    if(envelope.value("error").isObject()&&hasProviderError(envelope.value("error")))detail=envelope.value("error").toObject();
    else if(envelope.value("response").isObject()){
        const auto response=envelope.value("response").toObject();
        if(response.value("error").isObject())detail=response.value("error").toObject();
    }
    QStringList identifiers;
    if(detail.value("code").isString())identifiers.append(detail.value("code").toString().toLower());
    if(provider==CloudProvider::Gemini){
        int count=0;for(const auto& raw:detail.value("details").toArray()){
            if(++count>16)break;
            const auto item=raw.toObject();
            if(item.value("reason").isString())identifiers.append(item.value("reason").toString().toLower());
        }
    }
    for(const auto* field:{"type","status","reason"})if(detail.value(field).isString())identifiers.append(detail.value(field).toString().toLower());
    const QStringList recognized={"invalid_api_key","api_key_invalid","authentication_error","invalid_authentication","unauthenticated","invalidsubscriptionkey","model_not_found","deploymentnotfound","deployment_not_found","previous_response_not_found","response_not_found","insufficient_quota","credit_balance_exhausted","billing_hard_limit_reached","billing_not_active","spend_limit_exceeded","usage_limit_reached","organization_spend_limit_exceeded","project_spend_limit_exceeded","organization_usage_limit_exceeded","rate_limit_exceeded","rate_limit_error","ratelimitreached","too_many_requests","slow_down","permission_denied","permission_error","access_denied","authorizationfailed","forbidden","resource_exhausted","context_length_exceeded","input_too_long","request_too_large","request_too_large_error","unsupported_parameter","unsupported_value","invalid_request_error","invalid_request","invalid_argument","badrequest","server_error","internal_error","internal","unavailable","service_unavailable","overloaded_error","server_is_overloaded"};
    QString classified;for(const auto& identifier:identifiers)if(recognized.contains(identifier)){classified=identifier;break;}
    const auto known=[&](std::initializer_list<const char*> codes)->QString{for(const auto* code:codes)if(classified==QLatin1String(code))return QString::fromLatin1(code);return {};};
    const auto param=detail.value("param").toString().toLower();
    const auto auth=known({"invalid_api_key","api_key_invalid","authentication_error","invalid_authentication","unauthenticated","invalidsubscriptionkey"});
    if(!auth.isEmpty())return QString("Authentication failed (%1). Check the API key for the selected provider in Copilot settings.").arg(auth);
    const auto model=known({"model_not_found","deploymentnotfound","deployment_not_found"});
    if(!model.isEmpty())return QString("Model or deployment unavailable (%1). Check the model ID or Azure deployment name and that your API project has access.").arg(model);
    const auto conversation=known({"previous_response_not_found","response_not_found"});
    if(!conversation.isEmpty())return QString("The saved provider conversation is unavailable (%1). Start a new chat or turn off provider conversation storage. No automatic retry was made.").arg(conversation);
    const auto organizationSpend=known({"organization_spend_limit_exceeded"});
    if(!organizationSpend.isEmpty())return "Organization spending limit reached (organization_spend_limit_exceeded). Ask the API organization owner to review its budget and spending limit.";
    const auto projectSpend=known({"project_spend_limit_exceeded"});
    if(!projectSpend.isEmpty())return "API project spending limit reached (project_spend_limit_exceeded). Review this API project's budget and spending limit with its owner.";
    const auto organizationUsage=known({"organization_usage_limit_exceeded"});
    if(!organizationUsage.isEmpty())return "Organization usage limit reached (organization_usage_limit_exceeded). Review the API organization's usage allowance and limits before retrying.";
    const auto quota=known({"insufficient_quota","credit_balance_exhausted","billing_hard_limit_reached","billing_not_active","spend_limit_exceeded","usage_limit_reached"});
    if(!quota.isEmpty())return QString("API credits or spending allowance exhausted (%1). Check this provider's API billing, credits and spending limits; a chat subscription alone may not supply API credits.").arg(quota);
    const auto slow=known({"slow_down"});
    if(!slow.isEmpty())return "The provider requested slower traffic (slow_down). Wait before sending another request; no automatic retry was made.";
    const auto rate=known({"rate_limit_exceeded","rate_limit_error","ratelimitreached","too_many_requests"});
    if(!rate.isEmpty())return QString("API rate limit reached (%1). Wait and check your API project's rate limits before retrying; no automatic retry was made.").arg(rate);
    const auto permission=known({"permission_denied","permission_error","access_denied","authorizationfailed","forbidden"});
    if(!permission.isEmpty())return QString("API access denied (%1). Check the key's permissions, API project and model access, and any provider restrictions.").arg(permission);
    const auto resource=known({"resource_exhausted"});
    if(!resource.isEmpty())return "API quota or capacity exhausted (RESOURCE_EXHAUSTED). Check the Google API project's quota and billing, then retry when capacity is available.";
    const auto context=known({"context_length_exceeded","input_too_long","request_too_large","request_too_large_error"});
    if(!context.isEmpty())return QString("The model's input limit was reached (%1). Start a new chat or include less source context; the request was not silently truncated.").arg(context);
    const auto unsupported=known({"unsupported_parameter","unsupported_value"});
    if(!unsupported.isEmpty()){
        if(param=="max_output_tokens")return QString("Unsupported output setting (%1: max_output_tokens). Select a model compatible with the Responses API and its output-token limit.").arg(unsupported);
        if(param=="store")return QString("Unsupported response-storage setting (%1: store). Select a model that supports the Responses API's store=false option.").arg(unsupported);
        if(param=="stream")return QString("Streaming is unsupported by this model (%1: stream). Select a streaming-capable model or deployment.").arg(unsupported);
        return QString("Unsupported request setting (%1). Check that the selected model or deployment supports this provider's streaming text API.").arg(unsupported);
    }
    const auto invalid=known({"invalid_request_error","invalid_request","invalid_argument","badrequest"});
    if(!invalid.isEmpty()){
        if(param=="model")return QString("Invalid model setting (%1: model). Check the model ID or Azure deployment name and API access.").arg(invalid);
        if(param=="max_output_tokens")return QString("Invalid output limit (%1: max_output_tokens). Review the output-token limit for the selected model in Copilot settings.").arg(invalid);
        if(param=="input"||param=="contents")return QString("The provider rejected the conversation input (%1: input). Try a new chat or less source context and check this model's text-input support.").arg(invalid);
        return QString("The provider rejected the request (%1). Check the model or Azure deployment, endpoint and supported request settings.").arg(invalid);
    }
    const auto overloaded=known({"server_is_overloaded"});
    if(!overloaded.isEmpty())return "The provider is overloaded (server_is_overloaded). Try again later; no automatic retry was made.";
    const auto server=known({"server_error","internal_error","internal","unavailable","service_unavailable","overloaded_error"});
    if(!server.isEmpty())return QString("The provider is temporarily unavailable (%1). Try again later; no automatic retry was made.").arg(server);
    if(httpStatus)return httpError(httpStatus);
    return "The provider returned an unclassified error. Check the model or deployment, API credentials, API billing and provider status. No private error text was displayed.";
}
void BoundedErrorBody::append(const QByteArray& bytes){
    const auto room=MaximumBytes-bytes_.size();
    if(bytes.size()>room)truncated_=true;
    if(room>0)bytes_.append(bytes.constData(),qMin(room,bytes.size()));
}
void BoundedErrorBody::clear(){bytes_.fill('\0');bytes_.clear();truncated_=false;}
QString BoundedErrorBody::message(CloudProvider provider,int httpStatus)const{
    if(!truncated_){
        QStringDecoder decoder(QStringDecoder::Utf8);decoder.decode(bytes_);QJsonParseError parse;
        if(!decoder.hasError()){
            const auto document=QJsonDocument::fromJson(bytes_,&parse);
            if(parse.error==QJsonParseError::NoError&&document.isObject()){
                const auto safe=safeProviderError(provider,document.object(),httpStatus);
                if(httpStatus>=100&&httpStatus<=599&&!safe.contains("HTTP "))return QString("HTTP %1: ").arg(httpStatus)+safe;
                return safe;
            }
        }
    }
    // An incomplete or excessive body cannot choose a classification based on
    // a partial prefix. The HTTP status still yields a bounded fixed message.
    return httpError(httpStatus);
}
bool buildRequest(const CloudCopilotConfig& config,const QString& system,const QList<CloudChatMessage>& messages,HttpRequest* result,QString* error,const CloudRequestOptions& options){
    const auto reject=[&](const QString& message){if(error)*error=message;return false;};
    if(!result)return reject("No cloud request destination was supplied.");
    if(config.provider!=CloudProvider::OpenAI&&config.provider!=CloudProvider::AzureOpenAI&&config.provider!=CloudProvider::Gemini)return reject("Choose a supported cloud provider.");
    QString model=config.model.trimmed();if(config.provider==CloudProvider::Gemini&&model.startsWith("models/"))model.remove(0,7);
    static const QRegularExpression identifier("^[A-Za-z0-9][A-Za-z0-9._-]{0,159}$");
    if(!identifier.match(model).hasMatch())return reject("Enter a valid model ID or Azure deployment name.");
    if(config.apiKey.isEmpty()||config.apiKey.size()>8192)return reject("Enter your API key for this provider.");
    for(auto c:config.apiKey)if(static_cast<unsigned char>(c)<33||static_cast<unsigned char>(c)>126)return reject("The API key contains unsupported whitespace or control characters.");
    if(config.maxOutputTokens<128||config.maxOutputTokens>32768)return reject("The output limit must be between 128 and 32768 tokens.");
    if(system.trimmed().isEmpty()||messages.isEmpty()||messages.size()>64)return reject("The conversation is empty or too long. Start a new chat.");
    if(config.rememberConversation&&config.provider==CloudProvider::Gemini)return reject("Saved provider conversations are available for OpenAI and Azure OpenAI. Google uses Studio-managed conversation history.");
    if(!options.previousResponseId.isEmpty()&&(!config.rememberConversation||!validResponseId(options.previousResponseId)))return reject("The saved conversation reference is unavailable or invalid. Start a new chat; no automatic retry was made.");
    if(!options.previousResponseId.isEmpty()&&(messages.size()!=1||messages.front().role!="user"))return reject("A saved conversation continuation must contain only the new user turn.");
    if(!options.cacheKey.isEmpty()){
        static const QRegularExpression cachePattern("\\A[A-Za-z0-9_-]{1,64}\\z");
        if(!cachePattern.match(options.cacheKey).hasMatch())return reject("The conversation cache identity is invalid. Start a new chat.");
    }
    QJsonArray input;QJsonArray contents;
    if(!options.initialReference.isEmpty()&&options.previousResponseId.isEmpty()){
        if(!config.rememberConversation)return reject("Initial saved context requires provider conversation storage to be enabled explicitly.");
        input.append(QJsonObject{{"role","developer"},{"content",options.initialReference}});
    }
    for(const auto& message:messages){
        if((message.role!="user"&&message.role!="assistant")||message.text.trimmed().isEmpty())return reject("Only nonempty user and assistant text messages can be sent.");
        input.append(QJsonObject{{"role",message.role},{"content",message.text}});
        contents.append(QJsonObject{{"role",message.role=="assistant"?"model":"user"},{"parts",QJsonArray{QJsonObject{{"text",message.text}}}}});
    }
    HttpRequest request;request.headers.insert("Content-Type","application/json");request.headers.insert("Accept","text/event-stream");
    if(config.provider==CloudProvider::Gemini){
        if(!config.endpoint.isEmpty()&&(!cleanUrl(config.endpoint)||config.endpoint.host()!="generativelanguage.googleapis.com"||(config.endpoint.path()!=""&&config.endpoint.path()!="/")))return reject("Google Gemini uses its official HTTPS API endpoint.");
        request.url=QUrl("https://generativelanguage.googleapis.com/v1beta/models/"+model+":streamGenerateContent?alt=sse");
        request.headers.insert("x-goog-api-key",config.apiKey);
        request.body=QJsonDocument(QJsonObject{{"systemInstruction",QJsonObject{{"parts",QJsonArray{QJsonObject{{"text",system}}}}}},{"contents",contents},{"store",false},{"generationConfig",QJsonObject{{"maxOutputTokens",config.maxOutputTokens},{"candidateCount",1}}}}).toJson(QJsonDocument::Compact);
    }else{
        if(config.provider==CloudProvider::OpenAI){
            if(!config.endpoint.isEmpty()&&(!cleanUrl(config.endpoint)||config.endpoint.host()!="api.openai.com"||(config.endpoint.path()!=""&&config.endpoint.path()!="/"&&config.endpoint.path()!="/v1"&&config.endpoint.path()!="/v1/")))return reject("OpenAI uses its official HTTPS API endpoint.");
            request.url=QUrl("https://api.openai.com/v1/responses");request.headers.insert("Authorization","Bearer "+config.apiKey);
        }else{
            const auto endpoint=config.endpoint;const auto path=endpoint.path();
            if(!cleanUrl(endpoint)||!azureHost(endpoint.host())||(path!=""&&path!="/"&&path!="/openai/v1"&&path!="/openai/v1/"))return reject("Enter an Azure OpenAI HTTPS resource endpoint, such as https://your-resource.openai.azure.com.");
            request.url=endpoint;request.url.setPath("/openai/v1/responses");request.headers.insert("api-key",config.apiKey);
        }
        QJsonObject body{{"model",model},{"instructions",system},{"input",input},{"stream",true},{"store",config.rememberConversation},{"max_output_tokens",config.maxOutputTokens}};
        if(!options.previousResponseId.isEmpty())body.insert("previous_response_id",options.previousResponseId);
        if(!options.cacheKey.isEmpty())body.insert("prompt_cache_key",options.cacheKey);
        // Provider defaults already enable eligible prefix caching. Do not send
        // model-specific cache TTL or breakpoint options to unknown deployments.
        request.body=QJsonDocument(body).toJson(QJsonDocument::Compact);
    }
    if(request.body.size()>MaxRequest)return reject("This conversation exceeds the 256 KB request limit. Start a new chat or select less context; nothing was truncated.");
    *result=std::move(request);if(error)error->clear();return true;
}

StreamParser::StreamParser(CloudProvider provider):provider_(provider){}
void StreamParser::fail(const QString& message){if(error_.isEmpty())error_=message;metadata_.responseId.clear();}
void StreamParser::append(const QString& value){
    if(value.isEmpty())return;
    if(terminal_){fail("The provider sent answer text after its completion event.");return;}
    if(text_.toUtf8().size()+value.toUtf8().size()>MaxText){fail("The answer exceeded the local output limit and was stopped.");return;}
    text_+=value;
}
void StreamParser::dispatch(){
    if(!hasData_||!error_.isEmpty()){data_.clear();hasData_=false;return;}
    auto payload=data_;data_.clear();hasData_=false;
    if(payload=="[DONE]")return;
    QStringDecoder decoder(QStringDecoder::Utf8);decoder.decode(payload);if(decoder.hasError()){fail("The provider returned invalid UTF-8 text.");return;}
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(payload,&parse);
    if(parse.error!=QJsonParseError::NoError||!document.isObject()){fail("The provider returned an invalid stream event.");return;}
    const auto object=document.object();
    if(provider_!=CloudProvider::Gemini){const auto response=object.value("response").toObject();if(response.value("usage").isObject())metadata_.usage=responseUsage(response.value("usage").toObject());}
    if(hasProviderError(object.value("error"))){fail(safeProviderError(provider_,object));return;}
    if(provider_==CloudProvider::Gemini){
        if(object.value("usageMetadata").isObject())metadata_.usage=geminiUsage(object.value("usageMetadata").toObject());
        if(object.value("promptFeedback").toObject().contains("blockReason")){fail("The provider declined this prompt.");return;}
        const auto candidates=object.value("candidates").toArray();
        if(candidates.size()>1){fail("The provider returned multiple candidates unexpectedly.");return;}
        if(candidates.isEmpty())return;
        const auto candidate=candidates.first().toObject();
        for(const auto& raw:candidate.value("content").toObject().value("parts").toArray()){
            const auto part=raw.toObject();if(part.value("thought").toBool())continue;
            if(part.contains("functionCall")){fail("The provider attempted a tool call; cloud tools are disabled.");return;}
            append(part.value("text").toString());
        }
        const auto finish=candidate.value("finishReason").toString();
        if(!finish.isEmpty()){
            if(finish=="STOP")terminal_=true;
            else if(finish=="MAX_TOKENS")fail("The answer reached the provider's output limit and is incomplete.");
            else fail("The provider stopped the answer before normal completion.");
        }
        return;
    }
    const auto type=object.value("type").toString();
    if(type=="response.output_text.delta")append(object.value("delta").toString());
    else if(type=="response.refusal.delta"||type=="response.refusal.done")fail("The provider declined this request.");
    else if(type=="response.completed"){
        const auto response=object.value("response").toObject();
        if(response.value("usage").isObject())metadata_.usage=responseUsage(response.value("usage").toObject());
        if(hasProviderError(response.value("error"))){fail(safeProviderError(provider_,object));return;}
        if(response.value("status").toString()!="completed"){fail("The provider ended the response without successful completion.");return;}
        if(text_.isEmpty())append(textFromResponse(response));
        const auto id=response.value("id").toString();if(validResponseId(id))metadata_.responseId=id;
        terminal_=true;
    }else if(type=="response.incomplete"){
        const auto response=object.value("response").toObject();if(response.value("usage").isObject())metadata_.usage=responseUsage(response.value("usage").toObject());
        fail("The provider returned an incomplete answer. Try a shorter request or a larger output limit.");
    }
    else if(type=="response.failed"||type=="error")fail(safeProviderError(provider_,object));
}
void StreamParser::line(QByteArray value){
    if(value.size()>MaxEvent){fail("A provider stream line exceeded the local size limit.");return;}
    if(value.endsWith('\r'))value.chop(1);
    if(value.isEmpty()){dispatch();return;}
    if(value.startsWith("data:")){
        auto part=value.mid(5);if(part.startsWith(' '))part.remove(0,1);
        if(hasData_)data_+='\n';
        data_+=part;
        hasData_=true;
        if(data_.size()>MaxEvent)fail("A provider stream event exceeded the local size limit.");
    }
}
QString StreamParser::feed(const QByteArray& bytes){
    if(!error_.isEmpty())return {};
    const auto previous=text_.size();received_+=bytes.size();
    if(received_>MaxWire){fail("The provider response exceeded the local transfer limit.");return {};}
    pending_+=bytes;
    while(error_.isEmpty()){
        const auto newline=pending_.indexOf('\n');if(newline<0)break;
        const auto value=pending_.left(newline);pending_.remove(0,newline+1);line(value);
    }
    if(pending_.size()>MaxEvent)fail("A provider stream line exceeded the local size limit.");
    return text_.mid(previous);
}
bool StreamParser::finish(QString* error){
    if(error_.isEmpty()&&(!pending_.isEmpty()||hasData_))fail("The connection ended in the middle of a provider event.");
    if(error_.isEmpty()&&!terminal_)fail("The connection ended before the provider confirmed completion.");
    if(error_.isEmpty()&&text_.trimmed().isEmpty())fail("The provider completed without a text answer.");
    if(error)*error=error_;
    return error_.isEmpty();
}
}

namespace {
class CloudWorker final:public QObject {
public:
    std::function<QNetworkAccessManager*(QObject*)> networkFactory;
    std::function<void(quint64,const QString&)> delta,complete,error;
    std::function<void(quint64,const CloudResponseMetadata&)> metadata;
    std::function<void(quint64)> cancelled;
    void send(quint64 id,CloudCopilotConfig config,QString system,QList<CloudChatMessage> messages,CloudRequestOptions options){
        if(reply_){if(error)error(id,"Another cloud request is already running.");return;}
        cloud_copilot_detail::HttpRequest prepared;QString reason;
        if(!cloud_copilot_detail::buildRequest(config,system,messages,&prepared,&reason,options)){config.apiKey.fill('\0');if(error)error(id,reason);return;}
        current_=id;provider_=config.provider;errorBody_.clear();parser_=std::make_unique<cloud_copilot_detail::StreamParser>(config.provider);
        if(!network_)network_=networkFactory?networkFactory(this):new QNetworkAccessManager(this);
        QNetworkRequest request(prepared.url);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);request.setTransferTimeout(45000);
        for(auto it=prepared.headers.cbegin();it!=prepared.headers.cend();++it)request.setRawHeader(it.key(),it.value());
        config.apiKey.fill('\0');prepared.headers.clear();reply_=network_->post(request,prepared.body);reply_->setReadBufferSize(MaxEvent);
        connect(reply_,&QNetworkReply::readyRead,this,[this]{read();});
        connect(reply_,&QNetworkReply::finished,this,[this]{finished();});
        connect(reply_,&QNetworkReply::sslErrors,this,[this](const auto&){end(Kind::Error,"TLS verification failed. The connection was stopped without ignoring certificate errors.");});
        deadline_=new QTimer(this);deadline_->setSingleShot(true);connect(deadline_,&QTimer::timeout,this,[this]{end(Kind::Error,"The cloud request timed out. No automatic retry was made.");});deadline_->start(240000);
    }
    void cancel(){if(reply_)end(Kind::Cancel,{});}
    void shutdown(){cancel();}
private:
    enum class Kind{Complete,Error,Cancel};
    void read(){
        if(!reply_)return;
        const int status=reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto bytes=reply_->readAll();
        if(status<200||status>=300){errorBody_.append(bytes);return;} // Fixed-code classification only; never echo raw body text.
        if(!reply_->header(QNetworkRequest::ContentTypeHeader).toByteArray().toLower().startsWith("text/event-stream")){
            end(Kind::Error,"The provider did not return the requested event stream. Check the endpoint and model support.");return;
        }
        auto text=parser_->feed(bytes);if(!text.isEmpty()&&delta)delta(current_,text);
        if(!parser_->error().isEmpty())end(Kind::Error,parser_->error());
    }
    void finished(){
        if(!reply_)return;
        read();if(!reply_)return;
        const int status=reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(reply_->error()!=QNetworkReply::NoError||status<200||status>=300){end(Kind::Error,errorBody_.message(provider_,status));return;}
        QString reason;if(!parser_->finish(&reason)){end(Kind::Error,reason);return;}
        end(Kind::Complete,parser_->text());
    }
    void end(Kind kind,QString message){
        if(!reply_)return;
        auto result=parser_?parser_->metadata():CloudResponseMetadata{};
        if(kind!=Kind::Complete)result.responseId.clear();
        auto* reply=reply_;reply_=nullptr;reply->disconnect(this);if(deadline_){deadline_->stop();deadline_->deleteLater();deadline_=nullptr;}
        reply->abort();reply->deleteLater();parser_.reset();errorBody_.clear();
        const auto id=current_;current_=0;
        if((result.usage.available()||!result.responseId.isEmpty())&&metadata)metadata(id,result);
        if(kind==Kind::Complete){if(complete)complete(id,message);}
        else if(kind==Kind::Cancel){if(cancelled)cancelled(id);}
        else if(error)error(id,message);
    }
    QNetworkAccessManager* network_=nullptr;
    QNetworkReply* reply_=nullptr;
    QTimer* deadline_=nullptr;
    quint64 current_=0;
    CloudProvider provider_=CloudProvider::OpenAI;
    cloud_copilot_detail::BoundedErrorBody errorBody_;
    std::unique_ptr<cloud_copilot_detail::StreamParser> parser_;
};
}
struct CloudCopilotClient::State {QThread thread;CloudWorker* worker=nullptr;};
CloudCopilotClient::CloudCopilotClient(QObject* parent):QObject(parent),state_(std::make_unique<State>()){
    auto* worker=new CloudWorker;state_->worker=worker;worker->moveToThread(&state_->thread);connect(&state_->thread,&QThread::finished,worker,&QObject::deleteLater);
    const QPointer<CloudCopilotClient> guard(this);
    // Worker identities are private generations, not caller-visible request IDs.
    // Even a reused public ID cannot admit a reply queued before cancellation.
    worker->delta=[guard](quint64 generation,const QString& text){if(guard)QMetaObject::invokeMethod(guard,[guard,generation,text]{if(guard&&guard->active_&&guard->generation_==generation)emit guard->textDelta(guard->current_,text);},Qt::QueuedConnection);};
    worker->metadata=[guard](quint64 generation,const CloudResponseMetadata& result){if(guard)QMetaObject::invokeMethod(guard,[guard,generation,result]{if(guard&&guard->active_&&guard->generation_==generation)emit guard->responseMetadata(guard->current_,result);},Qt::QueuedConnection);};
    worker->complete=[guard](quint64 generation,const QString& text){if(guard)QMetaObject::invokeMethod(guard,[guard,generation,text]{if(guard&&guard->active_&&guard->generation_==generation){guard->active_=false;emit guard->completed(guard->current_,text);}},Qt::QueuedConnection);};
    worker->error=[guard](quint64 generation,const QString& text){if(guard)QMetaObject::invokeMethod(guard,[guard,generation,text]{if(guard&&guard->active_&&guard->generation_==generation){guard->active_=false;emit guard->failed(guard->current_,text);}},Qt::QueuedConnection);};
    worker->cancelled=[guard](quint64 generation){if(guard)QMetaObject::invokeMethod(guard,[guard,generation]{if(guard&&guard->active_&&guard->generation_==generation){guard->active_=false;emit guard->cancelled(guard->current_);}},Qt::QueuedConnection);};
    state_->thread.setObjectName("cloudCopilotNetwork");state_->thread.start();
}
#ifdef PENTA_CLOUD_COPILOT_TESTING
CloudCopilotClient::CloudCopilotClient(std::function<QNetworkAccessManager*(QObject*)> factory,QObject* parent):CloudCopilotClient(parent){
    auto* worker=state_->worker;QMetaObject::invokeMethod(worker,[worker,factory=std::move(factory)]{worker->networkFactory=factory;},Qt::BlockingQueuedConnection);
}
#endif
CloudCopilotClient::~CloudCopilotClient(){
    if(state_->thread.isRunning()){auto* worker=state_->worker;QMetaObject::invokeMethod(worker,[worker]{worker->shutdown();},Qt::BlockingQueuedConnection);state_->thread.quit();state_->thread.wait();}
}
void CloudCopilotClient::send(quint64 requestId,const CloudCopilotConfig& config,const QString& system,const QList<CloudChatMessage>& messages,const CloudRequestOptions& options){
    if(!requestId){emit failed(requestId,"A nonzero request identity is required.");return;}
    if(active_){emit failed(requestId,"Another cloud request is already running.");return;}
    current_=requestId;active_=true;const auto generation=++generation_;auto* worker=state_->worker;QMetaObject::invokeMethod(worker,[worker,generation,config,system,messages,options]{worker->send(generation,config,system,messages,options);},Qt::QueuedConnection);
}
void CloudCopilotClient::cancel(){if(active_){
    const auto id=current_;active_=false;++generation_;
    auto* worker=state_->worker;QMetaObject::invokeMethod(worker,[worker]{worker->cancel();},Qt::QueuedConnection);
    // Abort and the next send are queued to the same receiver in FIFO order.
    // Invalidate old GUI events now, so the caller can start a new request now.
    emit cancelled(id);
}}
}
