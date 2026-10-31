#include "offscreen_test_bootstrap.hpp"
#include "ide/assistant/assistant_controller.hpp"
#include "ide/assistant/copilot_panel.hpp"
#include "ide/assistant/copilot_settings.hpp"
#include "ide/assistant/offline_help.hpp"
#include "ide/assistant/help_search.hpp"
#include <QtTest>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMainWindow>
#include <QMap>
#include <QNetworkAccessManager>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
using namespace penta::assistant;
namespace {
SourceSnapshot snapshot(){SourceSnapshot result;result.identity=QByteArrayLiteral("document:1:revision:1:project:1");result.text=QStringLiteral("use_algebra(\"A_gamma\", gamma=1);\npentarnion p = 1+i+j;\n");result.selectionStart=result.selectionEnd=result.text.size();return result;}
void write(const QString& path,const QByteArray& bytes){QFile file(path);if(file.open(QIODevice::WriteOnly))file.write(bytes);}
void saveCopilotArtifact(CopilotPanel& panel,const QString& filename){
    const auto directory=qEnvironmentVariable("PENTA_GUI_REBUILD_ARTIFACTS");
    if(directory.isEmpty()||QApplication::platformName()!=QStringLiteral("offscreen"))return;
    if(!QDir().mkpath(directory)){qWarning("Cannot create the optional Copilot review-image directory.");return;}
    panel.resize(360,650);panel.show();QApplication::processEvents();
    if(!panel.grab().save(QDir(directory).filePath(filename)))qWarning("Cannot save the optional Copilot review image.");
    panel.hide();
}
class MemoryCredentials final:public CopilotCredentialStore {
public:
    bool available()const override{return true;}
    bool read(const QString& id,QByteArray* key,QString*)override{*key=keys.value(id);return true;}
    bool write(const QString& id,const QByteArray& key,QString*)override{keys[id]=key;return true;}
    bool remove(const QString& id,QString*)override{keys.remove(id);return true;}
    QMap<QString,QByteArray> keys;
};
}
class AssistantUiTests final:public QObject {
    Q_OBJECT
private slots:
    void dormantConstructionDoesNotShareOrConnect();
    void offlineHelpIndexesActualSectionsAndExamples();
    void validationPreservesSourceAndRejectsNewImports();
    void originalImportChangesInvalidateValidation();
    void sourceAndContextRevisionsAreExplicit();
    void providerRequestsAreBoundedOfflineFixtures();
    void defaultAndStoredConversationFramingStayDistinct();
    void credentialPreferencesNeverStorePlaintextKeys();
    void panelRecognizesPentarnionCodeAndInvalidatesOldProposals();
    void explicitReviewValidatesBeforeApplyAndRejectsStaleSource();
    void unchangedReviewAppliesOnlyAfterExplicitAction();
    void routineSourceInvalidationsStaySilent();
    void anActiveReviewIsInvalidatedOnlyOnce();
    void unchangedScienceBusyRetainsTranscriptAndReadingPosition();
    void welcomeExamplesStayLocalBeforeAndAfterConfiguration();
    void assistantProseHidesInternalReferencesAndKeepsFriendlySources();
    void streamingReferencesNeverExposePartialIdentifiers();
    void referencePresentationPreservesUserMessagesCodeAndMath();
    void configuringAndNewChatRestoreWelcomeWithoutSharing();
};
void AssistantUiTests::dormantConstructionDoesNotShareOrConnect(){
    QMainWindow host;int reads=0,applies=0;
    AssistantController controller(&host,[&]{++reads;return snapshot();},[&](const auto&,const auto&,QString*){++applies;return true;},{},false);
    QCOMPARE(reads,0);QCOMPARE(applies,0);QVERIFY(controller.dock()->isHidden());QCOMPARE(controller.panel()->contextMode(),CopilotPanel::ContextMode::None);QVERIFY(controller.panel()->includeReference());
    QVERIFY(!host.findChild<CloudCopilotClient*>());QVERIFY(!host.findChild<QNetworkAccessManager*>());QVERIFY(!host.findChild<QDialog*>(QStringLiteral("pentarnionHelpDialog")));
    auto* search=controller.createHelpSearch(&host);QVERIFY(search->placeholderText().contains(QStringLiteral("Pentarnion")));QCOMPARE(reads,0);controller.shutdown();
}
void AssistantUiTests::offlineHelpIndexesActualSectionsAndExamples(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());QDir dir(temporary.path());QVERIFY(dir.mkpath(QStringLiteral("examples/demo")));QVERIFY(dir.mkpath(QStringLiteral("specs")));
    write(dir.filePath(QStringLiteral("language.txt")),"L01. SOURCE STRUCTURE\nSemicolons terminate statements.\nL02. SPECIAL_SENTINEL_FUNCTION\nSentinel syntax is documented here.\n");
    write(dir.filePath(QStringLiteral("examples/demo/demo.p")),"use_algebra(\"A_gamma\",gamma=1);\ndisplay(1+i);\n");
    write(dir.filePath(QStringLiteral("specs/test.txt")),"REAL LOCAL CONTRACT\nSearchable fixture body.\n");
    const auto references=loadDocumentationReferences(temporary.path());QVERIFY(references.size()>=4);installDocumentationReferences(references);
    const auto matches=HelpDialog::searchReferences(QStringLiteral("SPECIAL_SENTINEL_FUNCTION"));QVERIFY(!matches.isEmpty());QVERIFY(matches.first().reference.text.contains(QStringLiteral("Sentinel syntax")));QVERIFY(matches.first().reference.location.startsWith(QStringLiteral("language.txt:")));
    bool example=false;for(const auto& ref:references)if(ref.title==QStringLiteral("Example: examples/demo/demo.p"))example=true;QVERIFY(example);
    std::atomic_bool cancelled{true};QVERIFY(loadDocumentationReferences(temporary.path(),&cancelled).isEmpty());installDocumentationReferences({});
}
void AssistantUiTests::validationPreservesSourceAndRejectsNewImports(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());auto original=snapshot();original.path=temporary.filePath(QStringLiteral("main.p"));original.projectRoot=temporary.path();write(original.path,original.text.toUtf8());
    const auto checked=validateSourceSuggestion(original,original.text+QStringLiteral("display(p);\n"));QVERIFY2(checked.valid,qPrintable(checked.message));QFile retained(original.path);QVERIFY(retained.open(QIODevice::ReadOnly));QCOMPARE(retained.readAll(),original.text.toUtf8());
    write(temporary.filePath(QStringLiteral("private.p")),"use_algebra(\"A_gamma\",gamma=1); real secret=42;\n");
    const auto rejected=validateSourceSuggestion(original,original.text+QStringLiteral("import \"private.p\" as extra;\n"));QVERIFY(!rejected.valid);QVERIFY(rejected.dependencies.isEmpty());QVERIFY(rejected.message.contains(QStringLiteral("resolver"),Qt::CaseInsensitive));
    const auto malformed=validateSourceSuggestion(original,QStringLiteral("real x = ;"));QVERIFY(!malformed.valid);QVERIFY(!malformed.message.isEmpty());
}
void AssistantUiTests::originalImportChangesInvalidateValidation(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());auto original=snapshot();original.path=temporary.filePath(QStringLiteral("main.p"));original.projectRoot=temporary.path();
    const auto dependency=temporary.filePath(QStringLiteral("helpers.p"));write(dependency,"use_algebra(\"A_gamma\",gamma=1); real answer=2;\n");original.text=QStringLiteral("use_algebra(\"A_gamma\", gamma=1); import \"helpers.p\" as helper; real value=helper.answer;\n");
    const auto checked=validateSourceSuggestion(original,original.text+QStringLiteral("display(value);\n"));QVERIFY2(checked.valid,qPrintable(checked.message));QVERIFY(!checked.dependencies.isEmpty());QVERIFY(sourceDependenciesMatch(checked,true));
    write(dependency,"use_algebra(\"A_gamma\",gamma=1); real changed=300;\n");QVERIFY(!sourceDependenciesMatch(checked,true));
}
void AssistantUiTests::sourceAndContextRevisionsAreExplicit(){
    const auto first=snapshot();auto revised=first;QVERIFY(sourceSnapshotMatches(first,revised));revised.identity=QByteArrayLiteral("revision:2");QVERIFY(!sourceSnapshotMatches(first,revised));revised=first;revised.gamma=.5;QVERIFY(!sourceSnapshotMatches(first,revised));
    CopilotContextLedger ledger;auto turn=ledger.prepare(QStringLiteral("question"),{{QStringLiteral("source"),QStringLiteral("main.p"),QStringLiteral("one")}});QCOMPARE(turn.added,1);ledger.commit(turn);
    turn=ledger.prepare(QStringLiteral("question"),{{QStringLiteral("source"),QStringLiteral("main.p"),QStringLiteral("one")}});QCOMPARE(turn.reused,1);QCOMPARE(turn.added,0);
    turn=ledger.prepare(QStringLiteral("question"),{{QStringLiteral("source"),QStringLiteral("main.p"),QStringLiteral("two")}});QCOMPARE(turn.added,1);QVERIFY(turn.text.contains(QStringLiteral("two")));
    turn=ledger.prepare(QStringLiteral("question"),{});QCOMPARE(turn.removed,1);QVERIFY(turn.text.contains(QStringLiteral("no longer selected")));
}
void AssistantUiTests::providerRequestsAreBoundedOfflineFixtures(){
    CloudCopilotConfig config;config.model=QStringLiteral("test-model");config.apiKey=QByteArrayLiteral("not-a-real-key");
    for(const auto provider:{CloudProvider::OpenAI,CloudProvider::AzureOpenAI,CloudProvider::Gemini}){
        config.provider=provider;config.endpoint=provider==CloudProvider::AzureOpenAI?QUrl(QStringLiteral("https://fixture.openai.azure.com")):QUrl();
        cloud_copilot_detail::HttpRequest request;QString error;QVERIFY2(cloud_copilot_detail::buildRequest(config,QStringLiteral("test policy"),{{QStringLiteral("user"),QStringLiteral("hello")}},&request,&error),qPrintable(error));QCOMPARE(request.url.scheme(),QStringLiteral("https"));QVERIFY(!request.body.contains(config.apiKey));QVERIFY(!request.url.toString().contains(QString::fromUtf8(config.apiKey)));
    }
    config.provider=CloudProvider::AzureOpenAI;config.endpoint=QUrl(QStringLiteral("https://attacker.invalid"));QString error;QVERIFY(!validateCopilotSettings(config,&error));
    const auto safe=cloud_copilot_detail::safeProviderError(CloudProvider::OpenAI,QJsonObject{{QStringLiteral("error"),QJsonObject{{QStringLiteral("message"),QStringLiteral("secret-not-for-ui")},{QStringLiteral("code"),QStringLiteral("invalid_api_key")}}}},401);QVERIFY(!safe.contains(QStringLiteral("secret-not-for-ui")));
}
void AssistantUiTests::defaultAndStoredConversationFramingStayDistinct(){
    CloudCopilotConfig config;config.model=QStringLiteral("fixture-model");config.apiKey=QByteArrayLiteral("synthetic-key");
    const QList<CloudChatMessage> history={{QStringLiteral("user"),QStringLiteral("old question")},{QStringLiteral("assistant"),QStringLiteral("old answer")}};
    for(const auto provider:{CloudProvider::OpenAI,CloudProvider::AzureOpenAI,CloudProvider::Gemini}){
        config.provider=provider;config.rememberConversation=false;config.endpoint=provider==CloudProvider::AzureOpenAI?QUrl(QStringLiteral("https://fixture.openai.azure.com")):QUrl();
        const auto turn=assistant_detail::prepareTextTurn(config,history,QStringLiteral("new question"),QStringLiteral("resp_old"));QCOMPARE(turn.messages.size(),3);QVERIFY(turn.options.previousResponseId.isEmpty());QVERIFY(turn.options.initialReference.isEmpty());QVERIFY(!turn.instructions.isEmpty());
        cloud_copilot_detail::HttpRequest request;QString error;QVERIFY2(cloud_copilot_detail::buildRequest(config,turn.instructions,turn.messages,&request,&error,turn.options),qPrintable(error));const auto body=QJsonDocument::fromJson(request.body).object();QCOMPARE(body.value(QStringLiteral("store")).toBool(true),false);QVERIFY(!body.contains(QStringLiteral("previous_response_id")));
        if(provider==CloudProvider::Gemini){QVERIFY(body.contains(QStringLiteral("systemInstruction")));QCOMPARE(body.value(QStringLiteral("contents")).toArray().size(),3);}
        else {QVERIFY(body.value(QStringLiteral("stream")).toBool());QCOMPARE(body.value(QStringLiteral("input")).toArray().size(),3);}
    }
    for(const auto provider:{CloudProvider::OpenAI,CloudProvider::AzureOpenAI}){
        config.provider=provider;config.rememberConversation=true;config.endpoint=provider==CloudProvider::AzureOpenAI?QUrl(QStringLiteral("https://fixture.openai.azure.com")):QUrl();
        auto turn=assistant_detail::prepareTextTurn(config,history,QStringLiteral("first stored turn"),{});QVERIFY(!turn.options.initialReference.isEmpty());cloud_copilot_detail::HttpRequest request;QString error;QVERIFY2(cloud_copilot_detail::buildRequest(config,turn.instructions,turn.messages,&request,&error,turn.options),qPrintable(error));QVERIFY(QJsonDocument::fromJson(request.body).object().value(QStringLiteral("store")).toBool());
        turn=assistant_detail::prepareTextTurn(config,history,QStringLiteral("next turn"),QStringLiteral("resp_fixture"));QCOMPARE(turn.messages.size(),1);QVERIFY(turn.options.initialReference.isEmpty());QCOMPARE(turn.options.previousResponseId,QStringLiteral("resp_fixture"));QVERIFY2(cloud_copilot_detail::buildRequest(config,turn.instructions,turn.messages,&request,&error,turn.options),qPrintable(error));const auto body=QJsonDocument::fromJson(request.body).object();QCOMPARE(body.value(QStringLiteral("input")).toArray().size(),1);QCOMPARE(body.value(QStringLiteral("previous_response_id")).toString(),QStringLiteral("resp_fixture"));QVERIFY(!body.value(QStringLiteral("instructions")).toString().isEmpty());
    }
}
void AssistantUiTests::credentialPreferencesNeverStorePlaintextKeys(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());QSettings settings(temporary.filePath(QStringLiteral("test.ini")),QSettings::IniFormat);const auto credentials=std::make_shared<MemoryCredentials>();CopilotSettingsStore store(settings,credentials);
    CloudCopilotConfig config;config.model=QStringLiteral("fixture-model");config.apiKey=QByteArrayLiteral("fixture-secret-key");QString error;QVERIFY2(store.save(config,false,&error),qPrintable(error));QVERIFY(credentials->keys.isEmpty());QVERIFY(store.load().apiKey.isEmpty());
    for(const auto& key:settings.allKeys())QVERIFY(!settings.value(key).toString().contains(QString::fromUtf8(config.apiKey)));
    QVERIFY2(store.save(config,true,&error),qPrintable(error));QCOMPARE(store.load().apiKey,config.apiKey);QVERIFY(!credentials->keys.isEmpty());
    QVERIFY2(store.save(config,false,&error),qPrintable(error));QVERIFY(credentials->keys.isEmpty());
}
void AssistantUiTests::panelRecognizesPentarnionCodeAndInvalidatesOldProposals(){
    const auto code=QStringLiteral("use_algebra(\"A_gamma\", gamma=1);\ndisplay(1+i);\n");const auto response=QStringLiteral("```p\n")+code+QStringLiteral("```\n");QCOMPARE(CopilotPanel::extractModelSource(response),code);
    QVERIFY(CopilotPanel::extractModelSource(QStringLiteral("```gtg\n:: Model ::\n```\n")).isEmpty());CopilotPanel panel;panel.setConfigured(true,QStringLiteral("offline fixture"));panel.beginAssistant();panel.appendAssistantDelta(response);panel.finishAssistant();
    auto* review=panel.findChild<QPushButton*>(QStringLiteral("copilotReviewCode"));QVERIFY(review);QVERIFY(review->isEnabled());panel.invalidateProposals(QStringLiteral("changed"));QVERIFY(!review->isEnabled());
}
void AssistantUiTests::explicitReviewValidatesBeforeApplyAndRejectsStaleSource(){
    QMainWindow host;auto current=snapshot();int applied=0;AssistantController controller(&host,[&]{return current;},[&](const auto&,const auto&,QString*){++applied;return true;},{},false);
    controller.reviewSuggestion(current,current.text+QStringLiteral("display(p);\n"));auto* dialog=host.findChild<QDialog*>(QStringLiteral("copilotSourceReview"));QVERIFY(dialog);auto* apply=dialog->findChild<QPushButton*>(QStringLiteral("copilotApplyProposal"));auto* validate=dialog->findChild<QPushButton*>(QStringLiteral("copilotValidateProposal"));QVERIFY(apply&&validate);QVERIFY(!apply->isEnabled());QCOMPARE(applied,0);validate->click();QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(),10000);QCOMPARE(applied,0);
    current.identity=QByteArrayLiteral("revision:2");apply->click();QCOMPARE(applied,0);QVERIFY(!apply->isEnabled());dialog->reject();controller.shutdown();
}
void AssistantUiTests::unchangedReviewAppliesOnlyAfterExplicitAction(){
    QMainWindow host;auto current=snapshot();const auto candidate=current.text+QStringLiteral("display(p);\n");int applied=0;AssistantController controller(&host,[&]{return current;},[&](const auto& expected,const auto& source,QString*){if(!sourceSnapshotMatches(expected,current))return false;++applied;current.text=source;current.identity=QByteArrayLiteral("revision:2");return true;},{},false);
    controller.reviewSuggestion(current,candidate);auto* dialog=host.findChild<QDialog*>(QStringLiteral("copilotSourceReview"));QVERIFY(dialog);auto* apply=dialog->findChild<QPushButton*>(QStringLiteral("copilotApplyProposal"));auto* validate=dialog->findChild<QPushButton*>(QStringLiteral("copilotValidateProposal"));QVERIFY(apply&&validate);validate->click();QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(),10000);QCOMPARE(applied,0);apply->click();QTRY_COMPARE_WITH_TIMEOUT(applied,1,10000);QCOMPARE(current.text,candidate);controller.shutdown();
}
void AssistantUiTests::routineSourceInvalidationsStaySilent(){
    QMainWindow host;int reads=0;AssistantController controller(&host,[&]{++reads;return snapshot();},[](const auto&,const auto&,QString*){return true;},{},false);
    auto* transcript=controller.panel()->findChild<QTextBrowser*>();QVERIFY(transcript);const auto original=transcript->toPlainText();
    controller.invalidateSource();controller.invalidateSource();controller.invalidateSource();
    QCOMPARE(transcript->toPlainText(),original);QCOMPARE(reads,0);controller.shutdown();
}
void AssistantUiTests::anActiveReviewIsInvalidatedOnlyOnce(){
    QMainWindow host;auto current=snapshot();AssistantController controller(&host,[&]{return current;},[](const auto&,const auto&,QString*){return true;},{},false);
    controller.reviewSuggestion(current,current.text+QStringLiteral("display(p);\n"));auto* dialog=host.findChild<QDialog*>(QStringLiteral("copilotSourceReview"));QVERIFY(dialog);
    auto* transcript=controller.panel()->findChild<QTextBrowser*>();QVERIFY(transcript);current.identity=QByteArrayLiteral("document:1:revision:2");
    controller.invalidateSource();controller.invalidateSource();controller.invalidateSource();
    QCOMPARE(transcript->toPlainText().count(QStringLiteral("Source changed; review and validate a new proposal.")),1);
    auto* apply=dialog->findChild<QPushButton*>(QStringLiteral("copilotApplyProposal"));QVERIFY(apply);QVERIFY(!apply->isEnabled());controller.shutdown();
}
void AssistantUiTests::unchangedScienceBusyRetainsTranscriptAndReadingPosition(){
    CopilotPanel panel;auto* transcript=panel.findChild<QTextBrowser*>(QStringLiteral("copilotTranscript"));
    auto* prompt=panel.findChild<QPlainTextEdit*>(QStringLiteral("copilotPrompt"));
    auto* send=panel.findChild<QPushButton*>(QStringLiteral("copilotSend"));
    auto* review=panel.findChild<QPushButton*>(QStringLiteral("copilotReviewCode"));
    auto* status=panel.findChild<QLabel*>(QStringLiteral("copilotStatus"));
    QVERIFY(transcript&&prompt&&send&&review&&status);
    const auto initialRevision=transcript->document()->revision();panel.setScienceBusy(false);
    QCOMPARE(transcript->document()->revision(),initialRevision);QVERIFY(!send->isEnabled());QVERIFY(!prompt->isEnabled());QVERIFY(!review->isEnabled());

    panel.resize(480,480);panel.setConfigured(true,QStringLiteral("offline fixture"));panel.show();QApplication::processEvents();
    const QString code=QStringLiteral("use_algebra(\"A_gamma\", gamma=1);\ndisplay(1+i);\n");
    panel.appendUser(QStringLiteral("Explain this model."));panel.beginAssistant();
    panel.appendAssistantDelta(QStringLiteral("A bounded local fixture paragraph for the transcript.\n\n").repeated(60)+QStringLiteral("```p\n")+code+QStringLiteral("```\n"));panel.finishAssistant();
    prompt->setPlainText(QStringLiteral("Next question"));
    QTRY_VERIFY_WITH_TIMEOUT(transcript->verticalScrollBar()->maximum()>0,1000);
    QVERIFY(send->isEnabled());QVERIFY(review->isEnabled());QVERIFY(status->isHidden());QVERIFY(transcript->toHtml().contains(QStringLiteral("penta-copilot:/review/")));
    const auto checkRepeatedState=[&](bool busy){
        auto selected=transcript->textCursor();selected.setPosition(12);selected.setPosition(37,QTextCursor::KeepAnchor);transcript->setTextCursor(selected);
        transcript->verticalScrollBar()->setValue(transcript->verticalScrollBar()->maximum()/2);
        auto* document=transcript->document();const int revision=document->revision();QSignalSpy changed(document,&QTextDocument::contentsChanged);
        const int scroll=transcript->verticalScrollBar()->value(),horizontal=transcript->horizontalScrollBar()->value();
        const auto cursor=transcript->textCursor();
        for(int i=0;i<100;++i)panel.setScienceBusy(busy);
        QCOMPARE(transcript->document(),document);QCOMPARE(document->revision(),revision);QCOMPARE(changed.count(),0);
        QCOMPARE(transcript->textCursor().position(),cursor.position());QCOMPARE(transcript->textCursor().anchor(),cursor.anchor());QCOMPARE(transcript->textCursor().selectedText(),cursor.selectedText());
        QCOMPARE(transcript->verticalScrollBar()->value(),scroll);QCOMPARE(transcript->horizontalScrollBar()->value(),horizontal);
    };
    checkRepeatedState(false);
    const int idleRevision=transcript->document()->revision();panel.setScienceBusy(true);
    QVERIFY(transcript->document()->revision()>idleRevision);QVERIFY(!send->isEnabled());QVERIFY(!review->isEnabled());QVERIFY(!status->isHidden());QVERIFY(status->text().contains(QStringLiteral("paused")));
    QVERIFY(!transcript->toHtml().contains(QStringLiteral("penta-copilot:/review/")));checkRepeatedState(true);
    const int busyRevision=transcript->document()->revision();panel.setScienceBusy(false);
    QVERIFY(transcript->document()->revision()>busyRevision);QVERIFY(send->isEnabled());QVERIFY(review->isEnabled());QVERIFY(status->isHidden());QVERIFY(transcript->toHtml().contains(QStringLiteral("penta-copilot:/review/")));
    checkRepeatedState(false);
}
void AssistantUiTests::welcomeExamplesStayLocalBeforeAndAfterConfiguration(){
    CopilotPanel panel;
    auto* welcome=panel.findChild<QWidget*>(QStringLiteral("copilotWelcome"));
    auto* greeting=panel.findChild<QLabel*>(QStringLiteral("copilotWelcomeGreeting"));
    auto* examples=panel.findChild<QLabel*>(QStringLiteral("copilotWelcomeExamples"));
    auto* shuffle=panel.findChild<QPushButton*>(QStringLiteral("copilotShuffleExamples"));
    auto* transcript=panel.findChild<QTextBrowser*>(QStringLiteral("copilotTranscript"));
    auto* prompt=panel.findChild<QPlainTextEdit*>(QStringLiteral("copilotPrompt"));
    auto* send=panel.findChild<QPushButton*>(QStringLiteral("copilotSend"));
    QVERIFY(welcome&&greeting&&examples&&shuffle&&transcript&&prompt&&send);
    QSignalSpy submitted(&panel,&CopilotPanel::sendRequested);
    QVERIFY(!welcome->isHidden());QVERIFY(transcript->isHidden());QVERIFY(greeting->text().startsWith(QStringLiteral("Hello!")));
    QVERIFY(examples->isEnabled());QVERIFY(shuffle->isEnabled());QVERIFY(!prompt->isEnabled());QVERIFY(!send->isEnabled());
    saveCopilotArtifact(panel,QStringLiteral("copilot-welcome-offscreen.png"));
    const auto originalExamples=examples->text();shuffle->click();QVERIFY(examples->text()!=originalExamples);
    QVERIFY(QMetaObject::invokeMethod(examples,"linkActivated",Qt::DirectConnection,Q_ARG(QString,QStringLiteral("penta-copilot:/example/0"))));
    const auto draft=prompt->toPlainText();QVERIFY(!draft.isEmpty());QCOMPARE(submitted.count(),0);QVERIFY(!send->isEnabled());
    panel.setConfigured(true,QStringLiteral("offline fixture"));QVERIFY(!welcome->isHidden());QVERIFY(prompt->isEnabled());QVERIFY(send->isEnabled());QCOMPARE(prompt->toPlainText(),draft);QCOMPARE(submitted.count(),0);
    QVERIFY(QMetaObject::invokeMethod(examples,"linkActivated",Qt::DirectConnection,Q_ARG(QString,QStringLiteral("penta-copilot:/example/1"))));
    QVERIFY(prompt->toPlainText()!=draft);QCOMPARE(submitted.count(),0);
    panel.appendUser(QStringLiteral("A local test question"));QVERIFY(welcome->isHidden());QVERIFY(!transcript->isHidden());
    panel.clearConversation();QVERIFY(!welcome->isHidden());QVERIFY(transcript->isHidden());QVERIFY(prompt->toPlainText().isEmpty());
    QVERIFY(!panel.findChild<CloudCopilotClient*>());QVERIFY(!panel.findChild<QNetworkAccessManager*>());
}
void AssistantUiTests::assistantProseHidesInternalReferencesAndKeepsFriendlySources(){
    CopilotPanel panel;panel.setConfigured(true,QStringLiteral("offline fixture"));
    const QString id=QStringLiteral("PENTA-doc-ffd4a247463b403215a211d6");
    panel.beginAssistant();
    panel.appendAssistantDelta(QStringLiteral("A five-dimensional real algebra. [")+id+QStringLiteral("]\n\nPreserve grouping. [PENTA-command-display] [PENTA-not-a-provided-source]\n\nGrouped citations disappear. [PENTA-doc-one, PENTA-doc-two] [PENTA-doc-three; PENTA-command-display] [PENTA-doc-four\nPENTA-doc-five]\n\n[Read the manual](https://example.invalid/manual) and [1, 100].\n\n[PENTA-doc-another](https://example.invalid/hidden)"));
    panel.setReferences({{id,QStringLiteral("Algebra and multiplication order"),QStringLiteral("language.txt:42"),QStringLiteral("Local fixture.")}});
    panel.finishAssistant();
    auto* transcript=panel.findChild<QTextBrowser*>(QStringLiteral("copilotTranscript"));QVERIFY(transcript);
    const auto plain=transcript->toPlainText();QVERIFY(!plain.contains(QStringLiteral("PENTA-")));QVERIFY(plain.contains(QStringLiteral("A five-dimensional real algebra.")));QVERIFY(plain.contains(QStringLiteral("Preserve grouping.")));QVERIFY(plain.contains(QStringLiteral("Read the manual")));QVERIFY(plain.contains(QStringLiteral("[1, 100]")));
    QVERIFY(plain.contains(QStringLiteral("Sources provided")));QVERIFY(plain.contains(QStringLiteral("Algebra and multiplication order")));QVERIFY(!plain.contains(QStringLiteral("https://example.invalid/hidden")));
    QVERIFY(plain.contains(QStringLiteral("Grouped citations disappear.")));saveCopilotArtifact(panel,QStringLiteral("copilot-sources-offscreen.png"));
    QSignalSpy requested(&panel,&CopilotPanel::referenceRequested);
    QVERIFY(QMetaObject::invokeMethod(transcript,"anchorClicked",Qt::DirectConnection,Q_ARG(QUrl,QUrl(QStringLiteral("penta-copilot:/reference/")+id))));
    QCOMPARE(requested.count(),1);QCOMPARE(requested.first().first().toString(),id);
}
void AssistantUiTests::streamingReferencesNeverExposePartialIdentifiers(){
    CopilotPanel panel;panel.setConfigured(true,QStringLiteral("offline fixture"));panel.beginAssistant();
    auto* transcript=panel.findChild<QTextBrowser*>(QStringLiteral("copilotTranscript"));
    auto* presentation=panel.findChild<QTimer*>(QStringLiteral("copilotPresentation"));QVERIFY(transcript&&presentation);
    panel.appendAssistantDelta(QStringLiteral("A useful explanation. "));
    const QString marker=QStringLiteral("[PENTA-doc-ffd4a247463b403215a211d6, PENTA-doc-two; PENTA-command-display]");
    for(const auto ch:marker){
        panel.appendAssistantDelta(QString(ch));QVERIFY(QMetaObject::invokeMethod(presentation,"timeout",Qt::DirectConnection));
        const auto plain=transcript->toPlainText();QVERIFY(plain.contains(QStringLiteral("A useful explanation.")));QVERIFY(!plain.contains(QStringLiteral("[P")));QVERIFY(!plain.contains(QStringLiteral("ffd4")));
    }
    panel.appendAssistantDelta(QStringLiteral(" More explanation. [PENTA-command-"));QVERIFY(QMetaObject::invokeMethod(presentation,"timeout",Qt::DirectConnection));
    QVERIFY(!transcript->toPlainText().contains(QStringLiteral("PENTA-")));
    panel.finishAssistant(QStringLiteral("Offline fixture stopped."));QVERIFY(!transcript->toPlainText().contains(QStringLiteral("PENTA-")));QVERIFY(transcript->toPlainText().contains(QStringLiteral("More explanation.")));
    panel.clearConversation();panel.beginAssistant();panel.appendAssistantDelta(QStringLiteral("Ordinary brackets [P"));QVERIFY(QMetaObject::invokeMethod(presentation,"timeout",Qt::DirectConnection));QVERIFY(!transcript->toPlainText().contains(QStringLiteral("[P")));
    panel.appendAssistantDelta(QStringLiteral("lot] survive."));panel.finishAssistant();QVERIFY(transcript->toPlainText().contains(QStringLiteral("[Plot] survive.")));
    panel.clearConversation();panel.beginAssistant();panel.appendAssistantDelta(QStringLiteral("Keep bracketed prose [PENTA-doc-one, ordinary explanatory text]."));panel.finishAssistant();QVERIFY(transcript->toPlainText().contains(QStringLiteral("[PENTA-doc-one, ordinary explanatory text]")));
}
void AssistantUiTests::referencePresentationPreservesUserMessagesCodeAndMath(){
    CopilotPanel panel;panel.setConfigured(true,QStringLiteral("offline fixture"));
    const QString marker=QStringLiteral("[PENTA-doc-literal-code]");
    const QString user=QStringLiteral("Keep this literal input ")+marker+QStringLiteral(" and <b>my text</b>.");panel.appendUser(user);
    const QString code=QStringLiteral("use_algebra(\"A_gamma\", gamma=1);\ndisplay(\"")+marker+QStringLiteral("\");\n");
    const QString response=QStringLiteral("Inline `")+marker+QStringLiteral("` stays literal. Math $p=(p_0,p_1)$ is rendered.\n\n<script>alert('no')</script><img src=\"https://example.invalid/tracker\">\n\n```p\n")+code+QStringLiteral("```\n");
    QCOMPARE(CopilotPanel::extractModelSource(response),code);panel.beginAssistant();panel.appendAssistantDelta(response);panel.finishAssistant();
    auto* transcript=panel.findChild<QTextBrowser*>(QStringLiteral("copilotTranscript"));QVERIFY(transcript);
    const auto plain=transcript->toPlainText();QVERIFY(plain.contains(user));QCOMPARE(plain.count(marker),3);QVERIFY(plain.contains(QStringLiteral("<script>alert('no')</script>")));QVERIFY(plain.contains(QStringLiteral("<img src=\"https://example.invalid/tracker\">")));
    const auto html=transcript->toHtml();QVERIFY(html.contains(QStringLiteral("penta-copilot:/math/")));QVERIFY(!html.contains(QStringLiteral("<script>")));QVERIFY(!html.contains(QStringLiteral("src=\"https://example.invalid/tracker\"")));
    QSignalSpy reviewed(&panel,&CopilotPanel::codeReviewRequested);
    QVERIFY(QMetaObject::invokeMethod(transcript,"anchorClicked",Qt::DirectConnection,Q_ARG(QUrl,QUrl(QStringLiteral("penta-copilot:/review/0")))));QCOMPARE(reviewed.count(),1);QCOMPARE(reviewed.first().first().toString(),code);
    QVERIFY(QMetaObject::invokeMethod(transcript,"anchorClicked",Qt::DirectConnection,Q_ARG(QUrl,QUrl(QStringLiteral("penta-copilot:/copy/0")))));QCOMPARE(QApplication::clipboard()->text(),code);
    QVERIFY(!panel.findChild<QNetworkAccessManager*>());
}
void AssistantUiTests::configuringAndNewChatRestoreWelcomeWithoutSharing(){
    QMainWindow host;int reads=0,applies=0;AssistantController controller(&host,[&]{++reads;return snapshot();},[&](const auto&,const auto&,QString*){++applies;return true;},{},false);
    auto* panel=controller.panel();auto* welcome=panel->findChild<QWidget*>(QStringLiteral("copilotWelcome"));auto* transcript=panel->findChild<QTextBrowser*>(QStringLiteral("copilotTranscript"));auto* prompt=panel->findChild<QPlainTextEdit*>(QStringLiteral("copilotPrompt"));auto* newChat=panel->findChild<QToolButton*>(QStringLiteral("copilotNewChat"));auto* review=panel->findChild<QPushButton*>(QStringLiteral("copilotReviewCode"));QVERIFY(welcome&&transcript&&prompt&&newChat&&review);
    QVERIFY(!welcome->isHidden());prompt->setPlainText(QStringLiteral("A draft not sent"));
    CloudCopilotConfig config;config.model=QStringLiteral("offline-fixture-model");config.apiKey=QByteArrayLiteral("synthetic-test-key");QString error;QVERIFY2(controller.setConfiguration(config,&error),qPrintable(error));
    QVERIFY(!welcome->isHidden());QVERIFY(transcript->isHidden());QCOMPARE(prompt->toPlainText(),QStringLiteral("A draft not sent"));QVERIFY(transcript->toPlainText().isEmpty());QCOMPARE(reads,0);QCOMPARE(applies,0);QVERIFY(!host.findChild<CloudCopilotClient*>());QVERIFY(!host.findChild<QNetworkAccessManager*>());
    panel->appendUser(QStringLiteral("An offline recorded question"));panel->beginAssistant();panel->appendAssistantDelta(QStringLiteral("```p\nuse_algebra(\"A_gamma\", gamma=1);\ndisplay(1+i);\n```\n"));panel->finishAssistant();QVERIFY(review->isEnabled());QVERIFY(welcome->isHidden());
    prompt->setPlainText(QStringLiteral("Keep this next draft"));config.model=QStringLiteral("another-offline-fixture");QVERIFY2(controller.setConfiguration(config,&error),qPrintable(error));QVERIFY(!welcome->isHidden());QVERIFY(transcript->toPlainText().isEmpty());QVERIFY(!review->isEnabled());QCOMPARE(prompt->toPlainText(),QStringLiteral("Keep this next draft"));
    panel->appendUser(QStringLiteral("A second recorded question"));newChat->click();QVERIFY(!welcome->isHidden());QVERIFY(transcript->isHidden());QVERIFY(transcript->toPlainText().isEmpty());QVERIFY(prompt->toPlainText().isEmpty());QCOMPARE(reads,0);QCOMPARE(applies,0);QVERIFY(!host.findChild<CloudCopilotClient*>());QVERIFY(!host.findChild<QNetworkAccessManager*>());
    const auto original=snapshot();controller.reviewSuggestion(original,original.text+QStringLiteral("display(p);\n"));QPointer<QDialog> dialog=host.findChild<QDialog*>(QStringLiteral("copilotSourceReview"));QVERIFY(dialog);QVERIFY(!dialog->isHidden());
    const int readsBeforeConfiguration=reads;config.model=QStringLiteral("offline-fixture-after-review");QVERIFY2(controller.setConfiguration(config,&error),qPrintable(error));QVERIFY(!dialog||dialog->isHidden());QVERIFY(!welcome->isHidden());QCOMPARE(reads,readsBeforeConfiguration);QCOMPARE(applies,0);QVERIFY(!host.findChild<CloudCopilotClient*>());QVERIFY(!host.findChild<QNetworkAccessManager*>());controller.shutdown();
}
PENTA_OFFSCREEN_TEST_MAIN(AssistantUiTests)
#include "assistant_ui_tests.moc"
