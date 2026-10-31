// Adapted from GTG Cell Studio desktop components; scientific engine remains separate.
#include "ide/assistant/copilot_settings.hpp"
#include "ide/desktop/studio_icons.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include "ide/distribution_paths.hpp"
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif

namespace penta::assistant {
using penta::desktop::studioCommandIcon;
using penta::desktop::applyStudioButtonIcons;
namespace {
constexpr auto group="copilot/";
void setError(QString* target,const QString& value){if(target)*target=value;}
QString providerId(CloudProvider provider){
    switch(provider){case CloudProvider::OpenAI:return "openai";case CloudProvider::AzureOpenAI:return "azure-openai";case CloudProvider::Gemini:return "gemini";}
    return {};
}
CloudProvider parseProvider(const QString& id){if(id=="azure-openai")return CloudProvider::AzureOpenAI;if(id=="gemini")return CloudProvider::Gemini;return CloudProvider::OpenAI;}
QUrl cleanEndpoint(const CloudCopilotConfig& config){
    if(config.provider!=CloudProvider::AzureOpenAI)return {};
    auto endpoint=config.endpoint;endpoint.setPath({});if(endpoint.port()==443)endpoint.setPort(-1);return endpoint;
}
class SystemCredentials final : public CopilotCredentialStore {
public:
    bool available()const override {
#ifdef Q_OS_WIN
        return true;
#else
        return false;
#endif
    }
    bool read(const QString& id,QByteArray* key,QString* error) override {
        key->clear();
#ifdef Q_OS_WIN
        PCREDENTIALW credential=nullptr;
        const auto target=targetName(id);
        if(!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()),CRED_TYPE_GENERIC,0,&credential)){
            if(GetLastError()==ERROR_NOT_FOUND)return true;
            setError(error,"The saved API key could not be read from Windows Credential Manager. Enter it for this session.");return false;
        }
        *key=QByteArray(reinterpret_cast<const char*>(credential->CredentialBlob),static_cast<qsizetype>(credential->CredentialBlobSize));
        if(credential->CredentialBlob&&credential->CredentialBlobSize)SecureZeroMemory(credential->CredentialBlob,credential->CredentialBlobSize);
        CredFree(credential);return true;
#else
        Q_UNUSED(id);setError(error,"Secure credential storage is unavailable in this build. Enter the API key for this session.");return false;
#endif
    }
    bool write(const QString& id,const QByteArray& key,QString* error) override {
#ifdef Q_OS_WIN
        if(key.size()>CRED_MAX_CREDENTIAL_BLOB_SIZE){setError(error,"This key is too long for Windows Credential Manager. Use it for this session instead.");return false;}
        const auto target=targetName(id),user=QStringLiteral("Pentarnion Studio Copilot");
        CREDENTIALW credential{};credential.Type=CRED_TYPE_GENERIC;credential.TargetName=const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16()));
        credential.UserName=const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(user.utf16()));credential.CredentialBlobSize=static_cast<DWORD>(key.size());
        credential.CredentialBlob=reinterpret_cast<LPBYTE>(const_cast<char*>(key.constData()));credential.Persist=CRED_PERSIST_LOCAL_MACHINE;
        if(CredWriteW(&credential,0))return true;
        setError(error,"The API key could not be saved in Windows Credential Manager. Use it for this session instead.");return false;
#else
        Q_UNUSED(id);Q_UNUSED(key);setError(error,"Secure credential storage is unavailable in this build. Use the API key for this session.");return false;
#endif
    }
    bool remove(const QString& id,QString* error) override {
#ifdef Q_OS_WIN
        const auto target=targetName(id);
        if(CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()),CRED_TYPE_GENERIC,0)||GetLastError()==ERROR_NOT_FOUND)return true;
        setError(error,"The saved API key could not be removed from Windows Credential Manager.");return false;
#else
        Q_UNUSED(id);Q_UNUSED(error);return true;
#endif
    }
private:
    static QString targetName(const QString& id){return "PentarnionStudio/Copilot/v1/"+id;}
};
}
std::shared_ptr<CopilotCredentialStore> systemCopilotCredentialStore(){return std::make_shared<SystemCredentials>();}
bool validateCopilotSettings(const CloudCopilotConfig& config,QString* error){
    setError(error,{});
    if(providerId(config.provider).isEmpty()){setError(error,"Choose a supported provider.");return false;}
    auto model=config.model;if(config.provider==CloudProvider::Gemini&&model.startsWith("models/"))model.remove(0,7);
    static const QRegularExpression modelPattern("\\A[A-Za-z0-9][A-Za-z0-9._-]{0,159}\\z");
    if(!modelPattern.match(model).hasMatch()){setError(error,config.provider==CloudProvider::AzureOpenAI?"Enter a valid Azure model deployment name (up to 160 characters).":"Enter a valid model ID (up to 160 characters).");return false;}
    if(config.apiKey.isEmpty()||config.apiKey.size()>8192){setError(error,"Enter an API key containing 1 to 8192 characters.");return false;}
    for(const auto value:config.apiKey){const auto byte=static_cast<unsigned char>(value);if(byte<0x21||byte>0x7e){setError(error,"The API key contains whitespace or unsupported characters. Copy the key without surrounding text.");return false;}}
    if(config.model.toUtf8()==config.apiKey||(config.apiKey.size()>=8&&config.model.toUtf8().contains(config.apiKey))){setError(error,"The model field appears to contain your API key. Enter the model ID separately; credentials belong only in the API key field.");return false;}
    if(config.maxOutputTokens<128||config.maxOutputTokens>32768){setError(error,"Choose an output limit between 128 and 32768 tokens.");return false;}
    if(config.rememberConversation&&config.provider==CloudProvider::Gemini){setError(error,"Google Gemini uses Studio-managed conversation history. Turn off saved provider conversation.");return false;}
    if(config.prices.enabled)for(const auto value:{config.prices.input,config.prices.cachedInput,config.prices.cacheWrite,config.prices.output})if(!std::isfinite(value)||value<0||value>1.0e6){setError(error,"Enter finite token prices between 0 and 1000000 USD per million tokens.");return false;}
    if(config.provider==CloudProvider::AzureOpenAI){
        const auto& url=config.endpoint;const auto path=url.path();
        static const QRegularExpression azureHost("\\A[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\\.(?:openai\\.azure\\.com|services\\.ai\\.azure\\.com)\\z");
        if(!url.isValid()||url.scheme()!="https"||!azureHost.match(url.host().toLower()).hasMatch()||!url.userInfo().isEmpty()||url.hasQuery()||url.hasFragment()||(url.port(-1)!=-1&&url.port()!=443)||(path!=""&&path!="/"&&path!="/openai/v1"&&path!="/openai/v1/")){
            setError(error,"Enter your HTTPS Azure resource endpoint, for example https://your-resource.openai.azure.com. Credentials, query strings and custom paths are not accepted.");return false;
        }
    }
    return true;
}
QString copilotCredentialIdentity(const CloudCopilotConfig& config){
    const auto endpoint=cleanEndpoint(config).toString(QUrl::FullyEncoded).toLower();
    return QString::fromLatin1(QCryptographicHash::hash((providerId(config.provider)+"\n"+endpoint).toUtf8(),QCryptographicHash::Sha256).toHex());
}
CopilotSettingsStore::CopilotSettingsStore(QSettings& settings,std::shared_ptr<CopilotCredentialStore> credentials):settings_(settings),credentials_(std::move(credentials)){}
bool CopilotSettingsStore::secureStorageAvailable()const{return credentials_&&credentials_->available();}
bool CopilotSettingsStore::remembered()const{return settings_.value(QString(group)+"rememberKey",false).toBool();}
CloudCopilotConfig CopilotSettingsStore::load(QString* error)const{
    setError(error,{});CloudCopilotConfig result;result.provider=parseProvider(settings_.value(QString(group)+"provider","openai").toString());
    result.model=settings_.value(QString(group)+"model").toString();
    if(result.provider==CloudProvider::AzureOpenAI)result.endpoint=QUrl(settings_.value(QString(group)+"endpoint").toString());
    result.maxOutputTokens=std::clamp(settings_.value(QString(group)+"maxOutputTokens",4096).toInt(),128,32768);
    result.rememberConversation=result.provider!=CloudProvider::Gemini&&settings_.value(QString(group)+"rememberConversation",false).toBool();
    result.prices.enabled=settings_.value(QString(group)+"prices/enabled",false).toBool();
    result.prices.input=settings_.value(QString(group)+"prices/input",0.0).toDouble();result.prices.cachedInput=settings_.value(QString(group)+"prices/cachedInput",0.0).toDouble();
    result.prices.cacheWrite=settings_.value(QString(group)+"prices/cacheWrite",0.0).toDouble();result.prices.output=settings_.value(QString(group)+"prices/output",0.0).toDouble();
    for(const auto value:{result.prices.input,result.prices.cachedInput,result.prices.cacheWrite,result.prices.output})if(!std::isfinite(value)||value<0||value>1.0e6)result.prices.enabled=false;
    if(remembered()){
        if(secureStorageAvailable())credentials_->read(copilotCredentialIdentity(result),&result.apiKey,error);
        else setError(error,"This build keeps API keys for the session only. Enter your key to connect.");
    }
    return result;
}
bool CopilotSettingsStore::save(const CloudCopilotConfig& config,bool rememberKey,QString* error){
    if(!validateCopilotSettings(config,error))return false;
    const auto id=copilotCredentialIdentity(config),old=settings_.value(QString(group)+"credentialId").toString();
    if(rememberKey){
        if(!secureStorageAvailable()){setError(error,"Secure credential storage is unavailable. Uncheck Remember and use the key for this session.");return false;}
        if(!credentials_->write(id,config.apiKey,error))return false;
    }
    if(!old.isEmpty()&&(!rememberKey||old!=id)&&credentials_&&!credentials_->remove(old,error))return false;
    settings_.setValue(QString(group)+"provider",providerId(config.provider));settings_.setValue(QString(group)+"model",config.model);
    settings_.setValue(QString(group)+"endpoint",cleanEndpoint(config).toString(QUrl::FullyEncoded));settings_.setValue(QString(group)+"maxOutputTokens",config.maxOutputTokens);
    settings_.setValue(QString(group)+"rememberKey",rememberKey);
    settings_.setValue(QString(group)+"rememberConversation",config.rememberConversation);
    settings_.setValue(QString(group)+"prices/enabled",config.prices.enabled);settings_.setValue(QString(group)+"prices/input",config.prices.input);settings_.setValue(QString(group)+"prices/cachedInput",config.prices.cachedInput);
    settings_.setValue(QString(group)+"prices/cacheWrite",config.prices.cacheWrite);settings_.setValue(QString(group)+"prices/output",config.prices.output);
    if(rememberKey)settings_.setValue(QString(group)+"credentialId",id);else settings_.remove(QString(group)+"credentialId");
    // Erase obsolete accidental plaintext fields rather than retaining them.
    for(const auto& field:{"apiKey","key","token"})settings_.remove(QString(group)+field);
    settings_.sync();if(settings_.status()!=QSettings::NoError){setError(error,"Copilot preferences could not be saved. Your project has not been changed.");return false;}
    return true;
}
bool CopilotSettingsDialog::secureStorageAvailable(){return systemCopilotCredentialStore()->available();}
bool CopilotSettingsDialog::rememberedKey(){const auto file=penta::distributionSettingsPath(QStringLiteral("StudioCopilot.ini"));if(!file.isEmpty()){QSettings settings(file,QSettings::IniFormat);return settings.value(QString(group)+"rememberKey",false).toBool();}QSettings settings(QSettings::IniFormat,QSettings::UserScope,QStringLiteral("Pentarnion"),QStringLiteral("StudioCopilot"));return settings.value(QString(group)+"rememberKey",false).toBool();}
CloudCopilotConfig CopilotSettingsDialog::loadConfiguration(QString* error){const auto file=penta::distributionSettingsPath(QStringLiteral("StudioCopilot.ini"));if(!file.isEmpty()){QSettings settings(file,QSettings::IniFormat);return CopilotSettingsStore(settings).load(error);}QSettings settings(QSettings::IniFormat,QSettings::UserScope,QStringLiteral("Pentarnion"),QStringLiteral("StudioCopilot"));return CopilotSettingsStore(settings).load(error);}
bool CopilotSettingsDialog::saveConfiguration(const CloudCopilotConfig& config,bool rememberKey,QString* error){const auto file=penta::distributionSettingsPath(QStringLiteral("StudioCopilot.ini"));if(!file.isEmpty()){QSettings settings(file,QSettings::IniFormat);return CopilotSettingsStore(settings).save(config,rememberKey,error);}QSettings settings(QSettings::IniFormat,QSettings::UserScope,QStringLiteral("Pentarnion"),QStringLiteral("StudioCopilot"));return CopilotSettingsStore(settings).save(config,rememberKey,error);}
CopilotSettingsDialog::CopilotSettingsDialog(QWidget* parent):QDialog(parent){
    setObjectName("copilotSettingsDialog");setWindowTitle("Copilot connection");setMinimumWidth(490);
    auto* layout=new QVBoxLayout(this);auto* intro=new QLabel("Connect your own cloud account. Messages and the context you select are sent directly to the chosen provider. API usage may incur charges.");intro->setWordWrap(true);layout->addWidget(intro);
    auto* form=new QFormLayout;provider_=new QComboBox;provider_->setObjectName("copilotProvider");provider_->addItem("OpenAI",static_cast<int>(CloudProvider::OpenAI));provider_->addItem("Microsoft Azure OpenAI",static_cast<int>(CloudProvider::AzureOpenAI));provider_->addItem("Google Gemini",static_cast<int>(CloudProvider::Gemini));form->addRow("Provider",provider_);
    model_=new QLineEdit;model_->setObjectName("copilotModel");model_->setMaxLength(167);modelLabel_=new QLabel("Model ID");form->addRow(modelLabel_,model_);
    endpoint_=new QLineEdit;endpoint_->setObjectName("copilotEndpoint");endpoint_->setMaxLength(256);endpoint_->setPlaceholderText("https://your-resource.openai.azure.com");endpointLabel_=new QLabel("Resource endpoint");form->addRow(endpointLabel_,endpoint_);
    key_=new QLineEdit;key_->setObjectName("copilotApiKey");key_->setEchoMode(QLineEdit::Password);key_->setMaxLength(8192);key_->setPlaceholderText("API key — kept for this session by default");form->addRow("API key",key_);
    outputTokens_=new QSpinBox;outputTokens_->setObjectName("copilotOutputTokens");outputTokens_->setRange(128,32768);outputTokens_->setSingleStep(256);outputTokens_->setValue(4096);form->addRow("Maximum output tokens",outputTokens_);layout->addLayout(form);
    remember_=new QCheckBox("Remember key on this computer");remember_->setObjectName("copilotRememberKey");remember_->setChecked(false);remember_->setEnabled(secureStorageAvailable());layout->addWidget(remember_);
    auto* storage=new QLabel(secureStorageAvailable()?"Remembered keys are held in Windows Credential Manager, separately from projects. Otherwise the key lasts only for this Studio session.":"Keys last only for this Studio session. Secure key persistence is unavailable in this build; keys are never saved in project files or plain-text preferences.");storage->setWordWrap(true);layout->addWidget(storage);
    auto* billing=new QLabel("Use an API key from the provider's developer platform. A chat subscription and API access can have different usage limits and billing.");billing->setWordWrap(true);layout->addWidget(billing);
    rememberConversation_=new QCheckBox("Keep this chat with the provider (send new turns only)");rememberConversation_->setObjectName("copilotRememberConversation");rememberConversation_->setChecked(false);layout->addWidget(rememberConversation_);
    rememberConversation_->setToolTip("Optional OpenAI/Azure response storage. The provider retains chat and selected source according to its data-retention policy. Earlier context remains billable. Starting a new chat drops Studio's continuation ID; it does not delete stored provider records.");
    auto* retention=new QLabel("Off by default: Studio sends conversation history with store=false. Eligible prompt caching may still reduce cost. Saved provider chats retain selected content remotely and do not make earlier context free.");retention->setWordWrap(true);layout->addWidget(retention);
    estimatePrices_=new QCheckBox("Estimate cost using my token prices (USD per million)");estimatePrices_->setObjectName("copilotEstimatePrices");layout->addWidget(estimatePrices_);
    pricesPanel_=new QWidget;auto* priceForm=new QFormLayout(pricesPanel_);priceForm->setContentsMargins(12,0,0,0);
    const auto price=[&](const char* object,const QString& title){auto* result=new QDoubleSpinBox;result->setObjectName(object);result->setDecimals(6);result->setRange(0,1000000);result->setSingleStep(0.1);priceForm->addRow(title,result);return result;};
    inputPrice_=price("copilotInputPrice","Ordinary input");cachedPrice_=price("copilotCachedPrice","Cached input");writePrice_=price("copilotWritePrice","Cache writes (total rate)");outputPrice_=price("copilotOutputPrice","Output, including reasoning");
    pricesPanel_->setToolTip("Enter prices for the exact provider, deployment, model, service tier and cache policy. Estimates cover token charges only, before taxes or credits; no tariff is assumed. Cache-write price is the total rate, not an additional surcharge.");layout->addWidget(pricesPanel_);pricesPanel_->hide();connect(estimatePrices_,&QCheckBox::toggled,pricesPanel_,&QWidget::setVisible);
    error_=new QLabel;error_->setObjectName("copilotSettingsError");error_->setWordWrap(true);error_->setStyleSheet("color:#b42318");layout->addWidget(error_);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText("Connect");layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,this,[this]{QString error;if(!validateCopilotSettings(configuration(),&error)){error_->setText(error);return;}accept();});connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(provider_,&QComboBox::currentIndexChanged,this,[this]{updateProvider(true);});connect(endpoint_,&QLineEdit::textEdited,this,[this]{key_->clear();remember_->setChecked(false);});
    connect(model_,&QLineEdit::textEdited,this,[this]{estimatePrices_->setChecked(false);});
    updateProvider(false);applyStudioButtonIcons(this);
}
void CopilotSettingsDialog::updateProvider(bool clearCredentials){
    const bool azure=static_cast<CloudProvider>(provider_->currentData().toInt())==CloudProvider::AzureOpenAI;endpoint_->setVisible(azure);endpointLabel_->setVisible(azure);modelLabel_->setText(azure?"Model deployment":"Model ID");model_->setPlaceholderText(azure?"Your deployment name":"Model ID from your provider");
    const bool gemini=static_cast<CloudProvider>(provider_->currentData().toInt())==CloudProvider::Gemini;rememberConversation_->setEnabled(!gemini);if(gemini)rememberConversation_->setChecked(false);
    if(clearCredentials){key_->clear();model_->clear();endpoint_->clear();remember_->setChecked(false);rememberConversation_->setChecked(false);estimatePrices_->setChecked(false);error_->clear();}
}
void CopilotSettingsDialog::setConfiguration(const CloudCopilotConfig& config){
    const QSignalBlocker providerBlock(provider_),endpointBlock(endpoint_);provider_->setCurrentIndex(provider_->findData(static_cast<int>(config.provider)));model_->setText(config.model);endpoint_->setText(config.provider==CloudProvider::AzureOpenAI?config.endpoint.toString():QString{});key_->setText(QString::fromUtf8(config.apiKey));outputTokens_->setValue(config.maxOutputTokens);remember_->setChecked(false);
    rememberConversation_->setChecked(config.rememberConversation&&config.provider!=CloudProvider::Gemini);estimatePrices_->setChecked(config.prices.enabled);inputPrice_->setValue(config.prices.input);cachedPrice_->setValue(config.prices.cachedInput);writePrice_->setValue(config.prices.cacheWrite);outputPrice_->setValue(config.prices.output);updateProvider(false);
}
CloudCopilotConfig CopilotSettingsDialog::configuration()const{
    CloudCopilotConfig config;config.provider=static_cast<CloudProvider>(provider_->currentData().toInt());config.model=model_->text().trimmed();if(config.provider==CloudProvider::AzureOpenAI)config.endpoint=QUrl(endpoint_->text().trimmed());config.apiKey=key_->text().toUtf8();config.maxOutputTokens=outputTokens_->value();config.rememberConversation=rememberConversation_->isEnabled()&&rememberConversation_->isChecked();config.prices={estimatePrices_->isChecked(),inputPrice_->value(),cachedPrice_->value(),writePrice_->value(),outputPrice_->value()};return config;
}
bool CopilotSettingsDialog::rememberKey()const{return remember_->isEnabled()&&remember_->isChecked();}
void CopilotSettingsDialog::setRememberKey(bool enabled){remember_->setChecked(remember_->isEnabled()&&enabled);}
}
