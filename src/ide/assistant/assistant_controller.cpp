// Host adapter around GTG's Copilot presentation/provider components. All
// Pentarnion edits cross the host's explicit source-snapshot apply boundary.
#include "ide/assistant/assistant_controller.hpp"
#include "ide/assistant/copilot_panel.hpp"
#include "ide/assistant/copilot_settings.hpp"
#include "ide/assistant/help_search.hpp"
#include "ide/assistant/offline_help.hpp"
#include "ide/syntax_highlighter.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QScopedValueRollback>
#include <QStringDecoder>
#include <QSplitter>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
namespace penta::assistant {
namespace {
constexpr qsizetype MaximumContextCharacters=64*1024;
QString providerName(CloudProvider provider){switch(provider){case CloudProvider::OpenAI:return QStringLiteral("OpenAI");case CloudProvider::AzureOpenAI:return QStringLiteral("Azure OpenAI");case CloudProvider::Gemini:return QStringLiteral("Google Gemini");}return {};}
SourceSnapshot safeSnapshot(const AssistantController::SourceProvider& source){return source?source():SourceSnapshot{};}
QString contextTitle(CopilotPanel::ContextMode mode){switch(mode){case CopilotPanel::ContextMode::None:return QStringLiteral("No source shared");case CopilotPanel::ContextMode::Selection:return QStringLiteral("Selected source text");case CopilotPanel::ContextMode::CurrentSource:return QStringLiteral("Current source document");}return {};}
}
assistant_detail::TextTurn assistant_detail::prepareTextTurn(const CloudCopilotConfig& config,const QList<CloudChatMessage>& history,const QString& newUserTurn,const QString& previousResponse){
    TextTurn result;const bool stored=config.rememberConversation&&config.provider!=CloudProvider::Gemini;
    result.options.previousResponseId=stored?previousResponse:QString();
    result.messages=result.options.previousResponseId.isEmpty()?history:QList<CloudChatMessage>{};result.messages.append({QStringLiteral("user"),newUserTurn});
    if(stored&&result.options.previousResponseId.isEmpty())result.options.initialReference=copilot_knowledge::coreReference();
    result.options.cacheKey=QStringLiteral("pentarnion_studio_0_2");result.instructions=copilotResponsePolicy()+QStringLiteral("\n\n")+copilot_knowledge::coreReference();return result;
}
AssistantController::AssistantController(QMainWindow* window,SourceProvider source,ApplyCallback apply,QString root,bool persistence)
    :QObject(window),window_(window),source_(std::move(source)),apply_(std::move(apply)),documentationRoot_(std::move(root)),persistence_(persistence){
    dock_=new QDockWidget(QStringLiteral("Copilot"),window);dock_->setObjectName(QStringLiteral("copilotDock"));dock_->setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea);panel_=new CopilotPanel(dock_);dock_->setWidget(panel_);window->addDockWidget(Qt::RightDockWidgetArea,dock_);dock_->hide();
    dock_->toggleViewAction()->setObjectName(QStringLiteral("viewCopilot"));
    if(persistence_){QString ignored;config_=CopilotSettingsDialog::loadConfiguration(&ignored);}
    panel_->setConfigured(validateCopilotSettings(config_),connectionName());
    connect(dock_,&QDockWidget::visibilityChanged,this,[this](bool visible){if(visible)ensureDocumentation();});
    connect(panel_,&CopilotPanel::sendRequested,this,&AssistantController::send);
    connect(panel_,&CopilotPanel::stopRequested,this,&AssistantController::stop);
    connect(panel_,&CopilotPanel::settingsRequested,this,&AssistantController::configure);
    connect(panel_,&CopilotPanel::newChatRequested,this,[this]{newChat();});
    connect(panel_,&CopilotPanel::contextPreviewRequested,this,&AssistantController::previewContext);
    connect(panel_,&CopilotPanel::codeReviewRequested,this,[this](const QString& text){reviewCode(text,false);});
    connect(panel_,&CopilotPanel::codeInsertRequested,this,[this](const QString& text){reviewCode(text,true);});
    connect(panel_,&CopilotPanel::referenceRequested,this,[this](const QString& id){ensureDocumentation();if(!help_)help_=new HelpDialog(window_);help_->openTopic(id);});
    connect(panel_,&CopilotPanel::fileAttachmentsRequested,this,&AssistantController::attachFiles);
    connect(panel_,&CopilotPanel::contextAttachmentsRequested,this,&AssistantController::manageAttachments);
    connect(panel_,&CopilotPanel::contextChanged,this,[this]{panel_->setContextDescription(contextTitle(panel_->contextMode()));});
    panel_->setContextDescription(contextTitle(panel_->contextMode()));
}
AssistantController::~AssistantController(){shutdown();}
QAction* AssistantController::toggleAction()const{return dock_->toggleViewAction();}
QString AssistantController::connectionName()const{return config_.model.isEmpty()?QString():providerName(config_.provider)+QStringLiteral(" · ")+config_.model;}
bool AssistantController::setConfiguration(const CloudCopilotConfig& configuration,QString* error){
    if(!validateCopilotSettings(configuration,error))return false;
    // As in GTG, a new connection starts at the local welcome. Keep an unsent
    // question, while retiring prior responses and queued provider callbacks.
    newChat(true);config_.apiKey.fill('\0');config_=configuration;
    panel_->setConfigured(true,connectionName());return true;
}
void AssistantController::configure(){
    CopilotSettingsDialog dialog(window_);dialog.setConfiguration(config_);if(persistence_)dialog.setRememberKey(CopilotSettingsDialog::rememberedKey());
    if(!persistence_){if(auto* remember=dialog.findChild<QCheckBox*>(QStringLiteral("copilotRememberKey"))){remember->setChecked(false);remember->setEnabled(false);}}
    if(dialog.exec()!=QDialog::Accepted)return;
    const auto candidate=dialog.configuration();
    QString error;
    if(!validateCopilotSettings(candidate,&error)){panel_->appendNotice(error);return;}
    if(persistence_&&!CopilotSettingsDialog::saveConfiguration(candidate,dialog.rememberKey(),&error)){panel_->appendNotice(error);return;}
    setConfiguration(candidate,&error);
}
void AssistantController::ensureDocumentation(){
    if(stopped_||documentationLoaded_||documentationWorker_.joinable())return;
    documentationCancel_=std::make_shared<std::atomic_bool>(false);const auto cancelled=documentationCancel_;const auto root=documentationRoot_;
    documentationWorker_=std::jthread([this,cancelled,root]{
        auto references=loadDocumentationReferences(root,cancelled.get());if(cancelled->load())return;
        // shutdown joins this member before QObject destruction; the context
        // discards delivery if the owner is subsequently destroyed.
        QMetaObject::invokeMethod(this,[this,references=std::move(references)]()mutable{if(stopped_)return;installDocumentationReferences(std::move(references));documentationLoaded_=true;if(help_)help_->refreshIndex();},Qt::QueuedConnection);
    });
}
HelpSearchBox* AssistantController::createHelpSearch(QWidget* parent){
    auto* search=new HelpSearchBox(parent);search->searchRequested=[this](const QString& text){showHelp(text);};search->topicRequested=[this](const QString& id){ensureDocumentation();if(!help_)help_=new HelpDialog(window_);help_->openTopic(id);};return search;
}
void AssistantController::showHelp(const QString& query){
    if(stopped_)return;
    ensureDocumentation();if(!help_)help_=new HelpDialog(window_);
    if(query==QStringLiteral("language-reference")||query==QStringLiteral("PENTA-language"))help_->openLanguageReference();
    else if(query.trimmed().isEmpty())help_->openTopic(QStringLiteral("PENTA-home"));
    else help_->search(query);
}
void AssistantController::contextHelp(const QString& term){showHelp(term.trimmed());}
void AssistantController::setScienceBusy(bool busy){scienceBusy_=busy;panel_->setScienceBusy(busy);if(busy&&reviewDialog_)if(auto* apply=reviewDialog_->findChild<QPushButton*>(QStringLiteral("copilotApplyProposal")))apply->setEnabled(false);}
void AssistantController::invalidateSource(){
    if(stopped_)return;
    // Routine typing, cursor changes and autosaves are not conversation events.
    // Retire a live answer/review once, rather than append a notice per edit.
    if(sourceProposalLive_){
        sourceProposalLive_=false;
        panel_->invalidateProposals(applyingProposal_?QString():QStringLiteral("Source changed; review and validate a new proposal."));
    }
    if(reviewDialog_)if(auto* apply=reviewDialog_->findChild<QPushButton*>(QStringLiteral("copilotApplyProposal")))apply->setEnabled(false);
}
QList<CopilotSharedContext> AssistantController::selectedContexts(const QString& question,const SourceSnapshot& snapshot)const{
    QList<CopilotSharedContext> contexts;const auto mode=panel_->contextMode();
    if(mode!=CopilotPanel::ContextMode::None&&snapshot.valid()){
        const auto text=mode==CopilotPanel::ContextMode::Selection?snapshot.selection:snapshot.text;
        if(!text.isEmpty())contexts.append({QStringLiteral("source"),snapshot.path.isEmpty()?QStringLiteral("Unsaved .p source"):QFileInfo(snapshot.path).fileName(),text});
    }
    contexts.append(attachments_);
    if(panel_->includeReference()){
        const auto references=copilot_knowledge::retrieve(question,{},5,18000);
        for(const auto& ref:references)contexts.append({ref.id,ref.title+QStringLiteral(" — ")+ref.location,ref.text});
    }
    return contexts;
}
void AssistantController::ensureClient(){
    if(client_)return;
    client_=new CloudCopilotClient(this);
    connect(client_,&CloudCopilotClient::textDelta,this,[this](quint64 request,const QString& text){if(stopped_||request!=request_)return;response_+=text;panel_->appendAssistantDelta(text);});
    connect(client_,&CloudCopilotClient::responseMetadata,this,[this](quint64 request,const CloudResponseMetadata& metadata){if(stopped_||request!=request_)return;usage_=metadata.usage;pendingResponse_=metadata.responseId;});
    connect(client_,&CloudCopilotClient::completed,this,[this](quint64 request,const QString& text){
        if(stopped_||request!=request_)return;
        response_=text;
        ledger_.commit(pendingTurn_);
        history_.append({QStringLiteral("user"),pendingUser_});
        history_.append({QStringLiteral("assistant"),text});
        previousResponse_=config_.rememberConversation?pendingResponse_:QString();
        panel_->setReferences(references_);panel_->finishAssistant();panel_->setBusy(false);panel_->setFollowupPrompts(copilot_knowledge::suggestedFollowups(pendingUser_,references_));panel_->setUsageSummary(cloudUsageSummary(usage_,config_.prices));
        if(!sourceSnapshotMatches(responseSnapshot_,safeSnapshot(source_))){
            panel_->invalidateProposals(sourceProposalLive_?QStringLiteral("Source changed during the response; this proposal cannot be applied."):QString());
            sourceProposalLive_=false;
        }
    });
    connect(client_,&CloudCopilotClient::failed,this,[this](quint64 request,const QString& error){if(stopped_||request!=request_)return;sourceProposalLive_=false;panel_->finishAssistant(error);panel_->setBusy(false);panel_->setUsageSummary(QStringLiteral("Request failed; no usage estimate available."));});
    connect(client_,&CloudCopilotClient::cancelled,this,[this](quint64 request){if(stopped_||request!=request_)return;sourceProposalLive_=false;panel_->finishAssistant(QStringLiteral("Response cancelled."));panel_->setBusy(false);});
}
void AssistantController::send(const QString& prompt){
    if(stopped_||(client_&&client_->active()))return;
    QString error;
    if(!validateCopilotSettings(config_,&error)){
        panel_->appendNotice(error);
        return;
    }
    if(prompt.trimmed().isEmpty()||prompt.size()>CopilotPanel::MaximumPromptCharacters)return;
    ensureDocumentation();const auto snapshot=safeSnapshot(source_);const auto mode=panel_->contextMode();const auto context=mode==CopilotPanel::ContextMode::Selection?snapshot.selection:snapshot.text;
    if(mode!=CopilotPanel::ContextMode::None&&context.size()>MaximumContextCharacters){panel_->appendNotice(QStringLiteral("Selected source exceeds 64 KiB. Select a smaller relevant section or choose Context: None."));return;}
    qsizetype retained=0;for(const auto& message:history_)retained+=message.text.size();
    if(history_.size()>30||retained>192*1024){history_.clear();ledger_.clear();previousResponse_.clear();panel_->appendNotice(QStringLiteral("Cloud context limit reached; a new bounded provider conversation starts with the currently selected context."));}
    const auto contexts=selectedContexts(prompt,snapshot);pendingTurn_=ledger_.prepare(prompt,contexts);if(pendingTurn_.text.size()>256*1024){panel_->appendNotice(QStringLiteral("Message and context exceed 256 KiB. Remove attachments or select less source."));return;}
    responseSnapshot_=snapshot;sourceProposalLive_=true;pendingUser_=pendingTurn_.text;response_.clear();pendingResponse_.clear();usage_={};references_=panel_->includeReference()?copilot_knowledge::retrieve(prompt,{},5,18000):QList<CopilotReference>{};
    panel_->appendUser(prompt);panel_->beginAssistant();panel_->setBusy(true);panel_->setUsageSummary(QStringLiteral("Waiting for provider usage…"));
    const auto turn=assistant_detail::prepareTextTurn(config_,history_,pendingTurn_.text,previousResponse_);
    ensureClient();client_->send(++request_,config_,turn.instructions,turn.messages,turn.options);
}
void AssistantController::stop(){if(client_&&client_->active())client_->cancel();}
void AssistantController::newChat(bool preserveDraft){stop();++request_;if(reviewDialog_)reviewDialog_->reject();sourceProposalLive_=false;history_.clear();ledger_.clear();previousResponse_.clear();response_.clear();responseSnapshot_={};panel_->clearConversation(preserveDraft);panel_->setBusy(false);}
void AssistantController::previewContext(){
    const auto snapshot=safeSnapshot(source_);QString text=QStringLiteral("Source context: ")+contextTitle(panel_->contextMode())+QStringLiteral("\nProvider: ")+connectionName()+QStringLiteral("\nNo request has been sent by opening this preview.\n");
    const auto contexts=selectedContexts(QStringLiteral("Pentarnion language source plotting ODE"),snapshot);for(const auto& context:contexts)text+=QStringLiteral("\n\n[")+context.id+QStringLiteral("] ")+context.title+QLatin1Char('\n')+context.text;
    auto* dialog=new QDialog(window_);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(QStringLiteral("Outgoing context preview"));dialog->resize(800,600);auto* layout=new QVBoxLayout(dialog);auto* preview=new QPlainTextEdit(dialog);preview->setReadOnly(true);preview->setPlainText(text);layout->addWidget(preview);auto* close=new QDialogButtonBox(QDialogButtonBox::Close,dialog);connect(close,&QDialogButtonBox::rejected,dialog,&QDialog::reject);layout->addWidget(close);dialog->show();
}
void AssistantController::attachFiles(){
    const auto paths=QFileDialog::getOpenFileNames(window_,QStringLiteral("Attach local context"),{},QStringLiteral("Text context (*.p *.txt *.md *.log *.json *.csv *.tsv)"));
    const QSet<QString> extensions={QStringLiteral("p"),QStringLiteral("txt"),QStringLiteral("md"),QStringLiteral("log"),QStringLiteral("json"),QStringLiteral("csv"),QStringLiteral("tsv")};
    for(const auto& path:paths){
        if(attachments_.size()>=4){panel_->appendNotice(QStringLiteral("At most four context attachments can be selected."));break;}
        const QFileInfo info(path);if(!info.isFile()||info.isSymLink()||!extensions.contains(info.suffix().toLower())||info.size()>64*1024){panel_->appendNotice(QStringLiteral("Choose a text context file of at most 64 KiB."));continue;}
        QFile file(path);if(!file.open(QIODevice::ReadOnly))continue;const auto bytes=file.read(64*1024+1);QStringDecoder decoder(QStringDecoder::Utf8);const auto text=decoder.decode(bytes);if(bytes.size()>64*1024||bytes.contains('\0')||decoder.hasError()){panel_->appendNotice(QStringLiteral("Attachment is not bounded valid UTF-8 text."));continue;}
        const auto id=QStringLiteral("attachment:")+info.canonicalFilePath();bool duplicate=false;for(auto& attachment:attachments_)if(attachment.id==id){attachment.text=text;duplicate=true;break;}if(!duplicate)attachments_.append({id,info.fileName(),text});
    }
    panel_->setAttachmentSummary(QStringLiteral("%1 file(s)").arg(attachments_.size()));
}
void AssistantController::manageAttachments(){
    auto* dialog=new QDialog(window_);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(QStringLiteral("Selected context attachments"));dialog->resize(650,420);auto* layout=new QVBoxLayout(dialog);auto* list=new QListWidget(dialog);for(const auto& attachment:attachments_){auto* item=new QListWidgetItem(attachment.title,list);item->setData(Qt::UserRole,attachment.id);item->setToolTip(attachment.text.left(4096));}layout->addWidget(list);auto* remove=new QPushButton(QStringLiteral("Remove selected"),dialog);layout->addWidget(remove);connect(remove,&QPushButton::clicked,dialog,[this,list]{const auto selected=list->selectedItems();for(auto* item:selected){const auto id=item->data(Qt::UserRole).toString();for(int index=attachments_.size()-1;index>=0;--index)if(attachments_[index].id==id)attachments_.removeAt(index);delete item;}panel_->setAttachmentSummary(QStringLiteral("%1 file(s)").arg(attachments_.size()));});auto* close=new QDialogButtonBox(QDialogButtonBox::Close,dialog);connect(close,&QDialogButtonBox::rejected,dialog,&QDialog::reject);layout->addWidget(close);dialog->show();
}
void AssistantController::reviewCode(const QString& source,bool insert){
    reviewSuggestion(responseSnapshot_,source,insert);
}
void AssistantController::reviewSuggestion(const SourceSnapshot& snapshot,const QString& source,bool insert){
    if(stopped_||scienceBusy_||validationRunning_||!apply_)return;
    if(!sourceSnapshotMatches(snapshot,safeSnapshot(source_))){panel_->appendNotice(QStringLiteral("This suggestion does not match the current source snapshot. Ask again with the current source selected."));return;}
    if(reviewDialog_){reviewDialog_->raise();return;}
    sourceProposalLive_=true;
    auto* dialog=new QDialog(window_);reviewDialog_=dialog;dialog->setObjectName(QStringLiteral("copilotSourceReview"));dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(QStringLiteral("Review Pentarnion source changes"));dialog->resize(1100,720);
    auto* layout=new QVBoxLayout(dialog);auto* mode=new QComboBox(dialog);mode->setObjectName(QStringLiteral("copilotProposalTarget"));mode->addItem(QStringLiteral("Replace entire source document"));mode->addItem(snapshot.selectionStart!=snapshot.selectionEnd?QStringLiteral("Replace captured selection"):QStringLiteral("Insert at captured cursor"));mode->setCurrentIndex(insert?1:0);layout->addWidget(mode);
    auto* split=new QSplitter(dialog);auto* before=new QPlainTextEdit(split);before->setReadOnly(true);before->setPlainText(snapshot.text);auto* after=new QPlainTextEdit(split);after->setObjectName(QStringLiteral("copilotCandidateSource"));auto* beforeSyntax=new penta::SyntaxHighlighter(before->document());auto* afterSyntax=new penta::SyntaxHighlighter(after->document());Q_UNUSED(beforeSyntax);Q_UNUSED(afterSyntax);layout->addWidget(split,1);
    auto* status=new QLabel(QStringLiteral("Review the complete proposed source. Validation never executes it."),dialog);status->setObjectName(QStringLiteral("copilotValidationStatus"));status->setTextFormat(Qt::PlainText);status->setWordWrap(true);layout->addWidget(status);
    auto* controls=new QDialogButtonBox(QDialogButtonBox::Cancel,dialog);auto* validate=controls->addButton(QStringLiteral("Validate"),QDialogButtonBox::ActionRole);validate->setObjectName(QStringLiteral("copilotValidateProposal"));auto* apply=controls->addButton(QStringLiteral("Apply"),QDialogButtonBox::AcceptRole);apply->setObjectName(QStringLiteral("copilotApplyProposal"));apply->setEnabled(false);layout->addWidget(controls);
    const auto state=std::make_shared<SourceValidation>();const auto makeCandidate=[snapshot,source,mode]{if(mode->currentIndex()==0)return source;auto text=snapshot.text;const int begin=std::clamp(snapshot.selectionStart,0,int(text.size())),end=std::clamp(snapshot.selectionEnd,begin,int(text.size()));text.replace(begin,end-begin,source);return text;};after->setPlainText(makeCandidate());
    connect(mode,qOverload<int>(&QComboBox::currentIndexChanged),dialog,[after,makeCandidate]{after->setPlainText(makeCandidate());});connect(after,&QPlainTextEdit::textChanged,dialog,[apply,status,state]{state->valid=false;apply->setEnabled(false);status->setText(QStringLiteral("Proposal edited; validate again before Apply."));});
    connect(controls,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    connect(dialog,&QDialog::finished,this,[this]{if(validationCancel_)validationCancel_->store(true);});
    connect(validate,&QPushButton::clicked,dialog,[this,dialog=QPointer<QDialog>(dialog),snapshot,after,status,validate,apply,state]{
        if(validationRunning_||!dialog)return;
        if(!sourceSnapshotMatches(snapshot,safeSnapshot(source_))){
            status->setText(QStringLiteral("Source changed; close this review and request a new proposal."));
            return;
        }
        validationRunning_=true;validationCancel_=std::make_shared<std::atomic_bool>(false);const auto cancelled=validationCancel_;const auto candidate=after->toPlainText();validate->setEnabled(false);apply->setEnabled(false);after->setReadOnly(true);status->setText(QStringLiteral("Checking syntax, types, grouping and original import snapshots…"));
        validationWorker_=std::jthread([this,dialog,snapshot,candidate,cancelled,status,validate,apply,after,state]{auto result=validateSourceSuggestion(snapshot,candidate,cancelled);QMetaObject::invokeMethod(this,[this,dialog,snapshot,result=std::move(result),status,validate,apply,after,state]()mutable{validationRunning_=false;if(stopped_||!dialog)return;*state=std::move(result);after->setReadOnly(false);validate->setEnabled(true);const bool current=sourceSnapshotMatches(snapshot,safeSnapshot(source_)),sameCandidate=after->toPlainText()==state->candidate;status->setText(!current?QStringLiteral("Source changed while checking. This proposal cannot be applied."):!sameCandidate?QStringLiteral("Proposal changed while checking. Validate the current proposal again."):state->message);apply->setEnabled(current&&sameCandidate&&state->valid&&!scienceBusy_);},Qt::QueuedConnection);});
    });
    connect(apply,&QPushButton::clicked,dialog,[this,dialog=QPointer<QDialog>(dialog),snapshot,after,status,apply,validate,state]{
        QString error;if(!state->valid||scienceBusy_||after->toPlainText()!=state->candidate||!sourceSnapshotMatches(snapshot,safeSnapshot(source_))||!sourceDependenciesMatch(*state,false,&error)){apply->setEnabled(false);status->setText(error.isEmpty()?QStringLiteral("The source or proposal changed; validate again."):error);return;}
        // Rehash imported bytes away from the GUI thread immediately before
        // application. The GUI then rechecks source identity and metadata.
        validationRunning_=true;apply->setEnabled(false);validate->setEnabled(false);after->setReadOnly(true);status->setText(QStringLiteral("Rechecking imported source identities before Apply…"));
        validationCancel_=std::make_shared<std::atomic_bool>(false);const auto cancelled=validationCancel_;const auto accepted=*state;
        validationWorker_=std::jthread([this,dialog,snapshot,after,status,apply,validate,accepted,cancelled]{
            QString dependencyError;const bool matches=sourceDependenciesMatch(accepted,true,&dependencyError);
            QMetaObject::invokeMethod(this,[this,dialog,snapshot,after,status,apply,validate,accepted,cancelled,matches,dependencyError]{
                validationRunning_=false;if(stopped_||!dialog)return;after->setReadOnly(false);validate->setEnabled(true);
                QString error=dependencyError;
                if(cancelled->load()||!matches||scienceBusy_||after->toPlainText()!=accepted.candidate||!sourceSnapshotMatches(snapshot,safeSnapshot(source_))||!sourceDependenciesMatch(accepted,false,&error)){status->setText(error.isEmpty()?QStringLiteral("The source changed; validate again before Apply."):error);return;}
                QScopedValueRollback<bool> applying(applyingProposal_,true);
                if(!apply_(snapshot,accepted.candidate,&error)){status->setText(error.isEmpty()?QStringLiteral("The host rejected this stale source edit."):error);return;}
                sourceProposalLive_=false;
                panel_->invalidateProposals(QStringLiteral("Applied as an undoable source edit. Run remains explicit."));dialog->accept();
            },Qt::QueuedConnection);
        });
    });
    dialog->show();
}
void AssistantController::shutdown(){
    if(stopped_)return;
    stopped_=true;
    ++request_;
    if(client_)client_->cancel();
    if(documentationCancel_)documentationCancel_->store(true);
    if(validationCancel_)validationCancel_->store(true);
    if(documentationWorker_.joinable())documentationWorker_.join();
    if(validationWorker_.joinable())validationWorker_.join();
    if(reviewDialog_)reviewDialog_->close();
    if(help_)help_->close();
    config_.apiKey.fill('\0');
    config_.apiKey.clear();
}
}
