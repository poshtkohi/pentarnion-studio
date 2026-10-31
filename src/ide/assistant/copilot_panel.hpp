// Adapted from GTG Cell Studio desktop components; scientific engine remains separate.
#pragma once
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QList>
#include <QUrl>
#include "ide/assistant/copilot_knowledge.hpp"
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QToolButton;
class QTextBrowser;

namespace penta::assistant {
// Presentation only. The owner captures context, owns cloud requests and
// validates reviewed suggestions. This widget never opens or writes a project.
class CopilotPanel final : public QWidget {
    Q_OBJECT
public:
    enum class ContextMode { None, Selection, CurrentSource };
    static constexpr int MaximumPromptCharacters = 32768;
    static constexpr int MaximumReplyCharacters = 131072;
    static constexpr int MaximumTranscriptCharacters = 262144;
    explicit CopilotPanel(QWidget* parent = nullptr);
    QSize sizeHint() const override;
    ContextMode contextMode() const;
    bool includeReference() const;
    bool hasConversation() const;
    void setConfigured(bool configured, const QString& providerAndModel = {});
    void setBusy(bool busy);
    void setScienceBusy(bool busy);
    void setContextDescription(const QString& description);
    void appendUser(const QString& text);
    void beginAssistant();
    void appendAssistantDelta(const QString& text);
    void finishAssistant(const QString& error = {});
    void clearConversation(bool preserveDraft=false);
    void setReferences(const QList<CopilotReference>&);
    void setFollowupPrompts(const QStringList&);
    void setUsageSummary(const QString& text,const QString& tooltip={});
    void setAttachmentSummary(const QString&);
    void appendNotice(const QString&);
    void invalidateProposals(const QString& reason={});
    static QString extractModelSource(const QString& response);
signals:
    void sendRequested(QString prompt);
    void stopRequested();
    void settingsRequested();
    void contextPreviewRequested();
    void newChatRequested();
    void codeReviewRequested(QString source);
    void contextChanged();
    void codeInsertRequested(QString source);
    void referenceRequested(QString id);
    void contextAttachmentsRequested();
    void fileAttachmentsRequested();
protected:
    bool eventFilter(QObject*, QEvent*) override;
private:
    void updateControls();
    void sendPrompt();
    void flushAssistant();
    void trimTranscript();
    void renderTranscript();
    void handleAction(const QUrl&);
    void resizeComposer();
    void refreshWelcomeExamples();
    void updateWelcome();
    struct CodeAction {QString source;bool review=false,enabled=false;};
    struct Message {QString role,text,error,html;bool complete=false,valid=false;QList<CopilotReference> references;QStringList followups;QList<CodeAction> actions;};
    QTextBrowser* transcript_;
    QWidget* welcome_;
    QLabel* welcomeExamples_;
    QPushButton* shuffleExamples_;
    QPlainTextEdit* prompt_;
    QComboBox* context_;
    QCheckBox* reference_;
    QLabel* connection_;
    QLabel* contextDescription_;
    QLabel* privacy_;
    QLabel* status_;
    QToolButton* newChat_;
    QToolButton* settings_;
    QToolButton* preview_;
    QPushButton* send_;
    QPushButton* stop_;
    QPushButton* review_;
    QTimer* presentation_;
    QWidget* advanced_;
    QToolButton* contextToggle_;
    QToolButton* attachments_;
    QToolButton* addFile_;
    QLabel* usage_;
    QList<Message> messages_;
    QList<CodeAction> codeActions_;
    QList<CopilotReference> references_;
    QStringList followups_;
    QStringList examplePrompts_;
    int exampleOffset_=0;
    QString attachmentSummary_;
    QString providerAndModel_;
    QString reply_;
    QString pendingText_;
    QString reviewSource_;
    bool configured_ = false;
    bool busy_ = false;
    bool scienceBusy_ = false;
    bool assistantOpen_ = false;
    bool replyTruncated_ = false;
    bool trimmingPrompt_ = false;
    bool currentProposalInvalidated_ = false;
};
}
