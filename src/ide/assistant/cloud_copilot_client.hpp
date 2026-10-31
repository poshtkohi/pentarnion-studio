// Adapted from GTG Cell Studio desktop components; scientific engine remains separate.
#pragma once
#include <QObject>
#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QUrl>
#include <memory>
class QJsonObject;
#ifdef PENTA_CLOUD_COPILOT_TESTING
#include <functional>
class QNetworkAccessManager;
#endif

namespace penta::assistant {
enum class CloudProvider { OpenAI, AzureOpenAI, Gemini };
// User-supplied USD prices, per million tokens. No provider tariff is assumed.
struct CloudTokenPrices {
    bool enabled=false;
    double input=0,cachedInput=0,cacheWrite=0,output=0;
};
struct CloudCopilotConfig {
    CloudProvider provider=CloudProvider::OpenAI;
    QString model;
    QUrl endpoint;
    QByteArray apiKey;
    int maxOutputTokens=4096;
    bool rememberConversation=false; // Explicit provider-side storage opt-in.
    CloudTokenPrices prices;
};
struct CloudChatMessage { QString role,text; };
struct CloudRequestOptions {
    QString previousResponseId;
    // On a saved conversation's first request this is input context, so it is
    // retained across previous_response_id calls. Stable policy remains in
    // top-level instructions on EVERY turn. Caller resets if this changes.
    QString initialReference;
    QString cacheKey;
};
struct CloudTokenUsage {
    // -1 means unreported/unknown, never a measured zero.
    qint64 inputTokens=-1,outputTokens=-1,cachedInputTokens=-1,cacheWriteTokens=-1;
    qint64 totalTokens=-1,reasoningTokens=-1;
    bool available()const{return inputTokens>=0||outputTokens>=0;}
};
struct CloudResponseMetadata {
    QString responseId; // Safe bounded Responses ID; only on successful completion.
    CloudTokenUsage usage;
};
QString cloudUsageSummary(const CloudTokenUsage&,const CloudTokenPrices& = {});
// Returns -1 when prices or required usage details are unavailable/inconsistent.
double cloudEstimatedCostUsd(const CloudTokenUsage&,const CloudTokenPrices&);

// Pure request/framing helpers are also used by offline provider-fixture tests.
// They do not open connections, read files, execute tools or persist credentials.
namespace cloud_copilot_detail {
struct HttpRequest { QUrl url; QMap<QByteArray,QByteArray> headers; QByteArray body; };
bool buildRequest(const CloudCopilotConfig&,const QString& system,
                  const QList<CloudChatMessage>&,HttpRequest*,QString* error,
                  const CloudRequestOptions& options={});
// Only known machine fields select fixed messages. Arbitrary provider message,
// code, parameter and request-id strings are never displayed or logged.
QString safeProviderError(CloudProvider,const QJsonObject&,int httpStatus=0);
class BoundedErrorBody {
public:
    static constexpr qsizetype MaximumBytes=64*1024;
    ~BoundedErrorBody(){clear();}
    void append(const QByteArray&);
    void clear();
    QString message(CloudProvider,int httpStatus)const;
    qsizetype size()const{return bytes_.size();}
    bool truncated()const{return truncated_;}
private:
    QByteArray bytes_;
    bool truncated_=false;
};
class StreamParser {
public:
    explicit StreamParser(CloudProvider provider);
    QString feed(const QByteArray&);
    bool finish(QString* error=nullptr);
    QString text()const{return text_;}
    QString error()const{return error_;}
    bool terminal()const{return terminal_;}
    CloudResponseMetadata metadata()const{return metadata_;}
private:
    void line(QByteArray);
    void dispatch();
    void append(const QString&);
    void fail(const QString&);
    CloudProvider provider_;
    QByteArray pending_,data_;
    QString text_,error_;
    qint64 received_=0;
    bool terminal_=false,hasData_=false;
    CloudResponseMetadata metadata_;
};
}

// The facade lives with its GUI owner; all HTTP work runs on a private thread.
// Signals contain public answer text and bounded generic errors, never keys or
// raw provider error bodies. There is no automatic retry or remote fallback.
class CloudCopilotClient final:public QObject {
    Q_OBJECT
public:
    explicit CloudCopilotClient(QObject* parent=nullptr);
#ifdef PENTA_CLOUD_COPILOT_TESTING
    // Compiled only into the standalone offline test target, not Studio.
    explicit CloudCopilotClient(std::function<QNetworkAccessManager*(QObject*)> testNetworkFactory,QObject* parent=nullptr);
#endif
    ~CloudCopilotClient()override;
    void send(quint64 requestId,const CloudCopilotConfig&,const QString& system,
              const QList<CloudChatMessage>&,const CloudRequestOptions& options={});
    void cancel();
    bool active()const{return active_;}
signals:
    void textDelta(quint64 requestId,const QString& text);
    void completed(quint64 requestId,const QString& fullText);
    void responseMetadata(quint64 requestId,const penta::assistant::CloudResponseMetadata& metadata);
    void failed(quint64 requestId,const QString& userSafeError);
    void cancelled(quint64 requestId);
private:
    struct State;
    std::unique_ptr<State> state_;
    quint64 current_=0;
    quint64 generation_=0;
    bool active_=false;
};
}
