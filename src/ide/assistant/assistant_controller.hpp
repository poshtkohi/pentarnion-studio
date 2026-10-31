#pragma once
#include "ide/assistant/source_review.hpp"
#include "ide/assistant/cloud_copilot_client.hpp"
#include "ide/assistant/copilot_chat_context.hpp"
#include "ide/assistant/copilot_knowledge.hpp"
#include <QObject>
#include <QPointer>
#include <functional>
#include <thread>
class QAction;
class QDialog;
class QDockWidget;
class QMainWindow;
class QWidget;
namespace penta::assistant {
class CopilotPanel;
class HelpDialog;
class HelpSearchBox;
namespace assistant_detail {
struct TextTurn {QString instructions;QList<CloudChatMessage> messages;CloudRequestOptions options;};
// Pure framing shared by the controller and offline provider fixtures.
TextTurn prepareTextTurn(const CloudCopilotConfig&,const QList<CloudChatMessage>& history,
                         const QString& newUserTurn,const QString& previousResponse);
}
class AssistantController final:public QObject {
    Q_OBJECT
public:
    using SourceProvider=std::function<SourceSnapshot()>;
    using ApplyCallback=std::function<bool(const SourceSnapshot&,const QString&,QString*)>;
    explicit AssistantController(QMainWindow*,SourceProvider,ApplyCallback,
        QString documentationRoot={},bool persistenceEnabled=true);
    ~AssistantController()override;
    QAction* toggleAction()const;
    QDockWidget* dock()const{return dock_;}
    CopilotPanel* panel()const{return panel_;}
    HelpSearchBox* createHelpSearch(QWidget* parent=nullptr);
    void showHelp(const QString& query={});
    void contextHelp(const QString& term);
    void setScienceBusy(bool);
    void invalidateSource();
    void shutdown();
    // Explicit host-triggered review also works offline; opening this dialog
    // neither uploads source nor executes/apply changes automatically.
    void reviewSuggestion(const SourceSnapshot&,const QString&,bool insert=false);
    // Programmatic configuration does not send a request. Primarily useful for
    // hosts that offer one settings page and for offline fixtures.
    bool setConfiguration(const CloudCopilotConfig&,QString* error=nullptr);
private:
    void configure();
    void ensureDocumentation();
    void ensureClient();
    void send(const QString&);
    void stop();
    void newChat(bool preserveDraft=false);
    void previewContext();
    void attachFiles();
    void manageAttachments();
    QList<CopilotSharedContext> selectedContexts(const QString&,const SourceSnapshot&)const;
    void reviewCode(const QString&,bool insert);
    QString connectionName()const;
    QMainWindow* window_;
    QDockWidget* dock_;
    CopilotPanel* panel_;
    CloudCopilotClient* client_=nullptr;
    QPointer<HelpDialog> help_;
    SourceProvider source_;
    ApplyCallback apply_;
    QString documentationRoot_;
    bool persistence_=true,scienceBusy_=false,stopped_=false,documentationLoaded_=false;
    quint64 request_=0;
    CloudCopilotConfig config_;
    QList<CloudChatMessage> history_;
    CopilotContextLedger ledger_;
    CopilotPreparedTurn pendingTurn_;
    QList<CopilotSharedContext> attachments_;
    QList<CopilotReference> references_;
    SourceSnapshot responseSnapshot_;
    QString pendingUser_,response_,previousResponse_,pendingResponse_;
    CloudTokenUsage usage_;
    std::shared_ptr<std::atomic_bool> documentationCancel_,validationCancel_;
    std::jthread documentationWorker_,validationWorker_;
    bool validationRunning_=false;
    bool sourceProposalLive_=false,applyingProposal_=false;
    QPointer<QDialog> reviewDialog_;
};
}
