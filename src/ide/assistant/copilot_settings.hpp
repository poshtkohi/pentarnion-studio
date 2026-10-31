// Adapted from GTG Cell Studio desktop components; scientific engine remains separate.
#pragma once
#include "ide/assistant/cloud_copilot_client.hpp"
#include <QDialog>
#include <memory>
class QSettings;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QLabel;
class QDoubleSpinBox;

namespace penta::assistant {
// Injectable so tests never read or write the user's OS credential vault.
class CopilotCredentialStore {
public:
    virtual ~CopilotCredentialStore() = default;
    virtual bool available() const = 0;
    virtual bool read(const QString& id,QByteArray* key,QString* error) = 0;
    virtual bool write(const QString& id,const QByteArray& key,QString* error) = 0;
    virtual bool remove(const QString& id,QString* error) = 0;
};
std::shared_ptr<CopilotCredentialStore> systemCopilotCredentialStore();
bool validateCopilotSettings(const CloudCopilotConfig&,QString* error=nullptr);
QString copilotCredentialIdentity(const CloudCopilotConfig&);

class CopilotSettingsStore final {
public:
    explicit CopilotSettingsStore(QSettings&,std::shared_ptr<CopilotCredentialStore> = systemCopilotCredentialStore());
    CloudCopilotConfig load(QString* error=nullptr) const;
    bool save(const CloudCopilotConfig&,bool rememberKey,QString* error=nullptr);
    bool remembered() const;
    bool secureStorageAvailable() const;
private:
    QSettings& settings_;
    std::shared_ptr<CopilotCredentialStore> credentials_;
};

class CopilotSettingsDialog final : public QDialog {
public:
    explicit CopilotSettingsDialog(QWidget* parent=nullptr);
    void setConfiguration(const CloudCopilotConfig&);
    CloudCopilotConfig configuration() const;
    bool rememberKey() const;
    void setRememberKey(bool);
    static bool secureStorageAvailable();
    static bool rememberedKey();
    static CloudCopilotConfig loadConfiguration(QString* error=nullptr);
    static bool saveConfiguration(const CloudCopilotConfig&,bool rememberKey,QString* error=nullptr);
private:
    void updateProvider(bool clearCredentials);
    QComboBox* provider_;
    QLineEdit* model_;
    QLineEdit* endpoint_;
    QLabel* endpointLabel_;
    QLabel* modelLabel_;
    QLineEdit* key_;
    QSpinBox* outputTokens_;
    QCheckBox* remember_;
    QCheckBox* rememberConversation_;
    QCheckBox* estimatePrices_;
    QWidget* pricesPanel_;
    QDoubleSpinBox* inputPrice_;
    QDoubleSpinBox* cachedPrice_;
    QDoubleSpinBox* writePrice_;
    QDoubleSpinBox* outputPrice_;
    QLabel* error_;
};
}
