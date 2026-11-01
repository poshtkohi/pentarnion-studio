#include "offscreen_test_bootstrap.hpp"
#include "ide/assistant/offline_help.hpp"
#include "ide/assistant/help_article_browser.hpp"
#include "ide/desktop/about_dialog.hpp"
#include "ide/editor_widget.hpp"
#include "ide/document.hpp"
#include "ide/syntax_highlighter.hpp"
#include "ide/find_replace_bar.hpp"
#include "ide/find_replace_dialog.hpp"
#include "language/frontend.hpp"
#include <QAction>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QScrollBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextCursor>
#include <QVariant>
#include <array>
#include <utility>

using namespace penta;
using namespace penta::assistant;
namespace {
void write(const QString& path,const QByteArray& bytes){QFile file(path);if(file.open(QIODevice::WriteOnly))file.write(bytes);}
}
class HelpDesktopTests final:public QObject {
    Q_OBJECT
private slots:
    void commandReferenceHasCurrentSyntaxAndExamples();
    void languageContentsAndLinkedArticlesAreFormatted();
    void examplesRetainCheckedLanguageSyntax();
    void arbitraryHtmlAndExternalLinksRemainData();
    void findWindowRemainsModelessAndTracksTheEditor();
    void thinScrollbarsKeepLongLinesReachable();
    void editorPresentationPreservesSourceModificationAndUndo();
    void aboutStartsWithAuthorsAndKeepsLicencesTogether();
    void aboutContactsAreReadableBlueMailLinks();
    void aboutArtworkFitsNormalAndNarrowBannerAreas();
};
void HelpDesktopTests::commandReferenceHasCurrentSyntaxAndExamples(){
    installDocumentationReferences({});const auto matches=HelpDialog::searchReferences(QStringLiteral("sin"));QVERIFY(!matches.isEmpty());QCOMPARE(matches.front().reference.title,QStringLiteral("sin"));
    QVERIFY(matches.front().reference.text.contains(QStringLiteral("Syntax\n```p")));QVERIFY(matches.front().reference.text.contains(QStringLiteral("Example\n```p")));
    HelpDialog dialog;dialog.search(QStringLiteral("sin"));QCOMPARE(dialog.currentTopicId(),QStringLiteral("PENTA-command-sin"));
    auto* article=dialog.findChild<QTextBrowser*>(QStringLiteral("helpArticle"));QVERIFY(article);QVERIFY(article->property("helpFormatted").toBool());
    QVERIFY(article->toPlainText().contains(QStringLiteral("one-element calculus")));QVERIFY(article->toPlainText().contains(QStringLiteral("exp(p)")));QVERIFY(article->toHtml().contains(QStringLiteral("<table")));
}
void HelpDesktopTests::languageContentsAndLinkedArticlesAreFormatted(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());write(temporary.filePath(QStringLiteral("language.txt")),"L01. SOURCE STRUCTURE\nStatements terminate with semicolons.\nL02. VALUES\nTyped values retain their algebra context.\n");
    installDocumentationReferences(loadDocumentationReferences(temporary.path()));HelpDialog dialog;dialog.openLanguageReference();
    auto* article=dialog.findChild<QTextBrowser*>(QStringLiteral("helpArticle"));QVERIFY(article);QCOMPARE(dialog.currentTopicId(),QStringLiteral("PENTA-language"));
    QVERIFY(article->toPlainText().contains(QStringLiteral("SOURCE STRUCTURE")));QVERIFY(article->toPlainText().contains(QStringLiteral("Command reference")));QVERIFY(article->toHtml().contains(QStringLiteral("penta-help://topic/")));
    const QUrl target(QStringLiteral("penta-help://topic/PENTA-command-display"));QVERIFY(QMetaObject::invokeMethod(article,"anchorClicked",Qt::DirectConnection,Q_ARG(QUrl,target)));
    QCOMPARE(dialog.currentTopicId(),QStringLiteral("PENTA-command-display"));QVERIFY(article->toPlainText().contains(QStringLiteral("[1,100]")));
    auto* back=dialog.findChild<QAction*>(QStringLiteral("helpBack"));QVERIFY(back&&back->isEnabled());back->trigger();QCOMPARE(dialog.currentTopicId(),QStringLiteral("PENTA-language"));
    dialog.search(QStringLiteral("language-reference"));QCOMPARE(dialog.currentTopicId(),QStringLiteral("PENTA-language"));installDocumentationReferences({});
}
void HelpDesktopTests::examplesRetainCheckedLanguageSyntax(){
    int checked=0;for(const auto& reference:HelpDialog::allReferences()){
        if(!reference.id.startsWith(QStringLiteral("PENTA-command-")))continue;
        const auto marker=QStringLiteral("Example\n```p\n");const int first=reference.text.indexOf(marker);if(first<0)continue;
        const int start=first+marker.size(),end=reference.text.indexOf(QStringLiteral("```"),start);QVERIFY(end>=start);
        const auto source=reference.text.mid(start,end-start).toUtf8();language::AnalysisOptions options;options.languageVersion="0.2";
        const auto analysis=language::analyse(std::string_view(source.constData(),static_cast<std::size_t>(source.size())),options);
        QVERIFY2(analysis.ok(),qPrintable(reference.id+QStringLiteral(" has an invalid displayed example")));++checked;
    }QVERIFY(checked>=10);
}
void HelpDesktopTests::arbitraryHtmlAndExternalLinksRemainData(){
    HelpArticleBrowser article;article.resize(480,300);const CopilotReference input{QStringLiteral("fixture"),QStringLiteral("Safe local article"),QStringLiteral("fixture.txt"),QStringLiteral("<img src='file:///private.png'><script>unsafe()</script>\n\nSyntax\n```p\ndisplay(1);\n```\n")};
    article.showReference(input,{input});QVERIFY(article.toPlainText().contains(QStringLiteral("<script>unsafe()</script>")));QVERIFY(!article.openExternalLinks());QVERIFY(!article.openLinks());
    const auto before=article.toPlainText();article.document()->resource(QTextDocument::ImageResource,QUrl(QStringLiteral("https://invalid.example/resource.png")));QCOMPARE(article.toPlainText(),before);
    HelpDialog dialog;dialog.openTopic(QStringLiteral("PENTA-order"));auto* browser=dialog.findChild<QTextBrowser*>(QStringLiteral("helpArticle"));QVERIFY(browser);const auto current=dialog.currentTopicId();
    const QUrl target(QStringLiteral("https://invalid.example/"));QVERIFY(QMetaObject::invokeMethod(browser,"anchorClicked",Qt::DirectConnection,Q_ARG(QUrl,target)));QCOMPARE(dialog.currentTopicId(),current);
}
void HelpDesktopTests::findWindowRemainsModelessAndTracksTheEditor(){
    QMainWindow host;auto* editor=new EditorWidget(&host);host.setCentralWidget(editor);editor->setPlainText(QStringLiteral("alpha alpha beta"));host.resize(650,420);host.show();QApplication::processEvents();const auto height=editor->height();
    FindReplaceDialog dialog(&host);dialog.showFind(editor,QStringLiteral("main.p"),true);QApplication::processEvents();
    QVERIFY(dialog.isWindow());QVERIFY(!dialog.isModal());QCOMPARE(dialog.windowModality(),Qt::NonModal);QVERIFY(QApplication::activeModalWidget()==nullptr);QCOMPARE(editor->height(),height);QVERIFY(!editor->isReadOnly());
    auto* query=dialog.bar()->findChild<QLineEdit*>(QStringLiteral("findText"));QVERIFY(query);query->setText(QStringLiteral("alpha"));QCOMPARE(dialog.bar()->matchCount(),2);
    editor->moveCursor(QTextCursor::End);editor->insertPlainText(QStringLiteral(" alpha"));QTRY_COMPARE(dialog.bar()->matchCount(),3);
    EditorWidget next;next.setPlainText(QStringLiteral("alpha"));dialog.followEditor(&next,QStringLiteral("next.p"));QCOMPARE(dialog.bar()->matchCount(),1);QCOMPARE(editor->extraSelections().size(),1);QVERIFY(dialog.bar()->findChild<QLabel*>(QStringLiteral("findSourceName"))->text().contains(QStringLiteral("next.p")));
    dialog.reject();QVERIFY(!dialog.isVisible());QCOMPARE(next.extraSelections().size(),1);
}
void HelpDesktopTests::thinScrollbarsKeepLongLinesReachable(){
    EditorWidget editor;const auto source=QString(240,'x')+QStringLiteral("\n")+QStringLiteral("short line\n").repeated(70);editor.setPlainText(source);editor.resize(440,260);editor.show();QApplication::processEvents();
    QVERIFY(editor.verticalScrollBar()->width()<=10);QVERIFY(editor.horizontalScrollBar()->height()<=10);QTRY_VERIFY(editor.horizontalScrollBar()->maximum()>0);QVERIFY(editor.verticalScrollBar()->maximum()>0);
    editor.resize(300,240);QApplication::processEvents();editor.resize(520,280);QApplication::processEvents();QTRY_VERIFY(editor.horizontalScrollBar()->maximum()>0);editor.horizontalScrollBar()->setValue(editor.horizontalScrollBar()->maximum());QCOMPARE(editor.toPlainText(),source);
}
void HelpDesktopTests::editorPresentationPreservesSourceModificationAndUndo(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const auto path=temporary.filePath(QStringLiteral("presentation.p"));
    const QString source=QStringLiteral("real alpha = 1;\n% an unchanged source\ndisplay(alpha);\n");write(path,source.toUtf8());
    Document model;QString error;QVERIFY2(model.load(path,&error),qPrintable(error));const auto revision=model.revision();
    QVERIFY(!model.isModified());QVERIFY(!model.textDocument()->isUndoAvailable());
    EditorWidget editor;editor.setDocument(model.textDocument());
    // Attaching a source and deferred style/layout work are presentation only.
    QVERIFY(!model.isModified());editor.resize(440,260);editor.ensurePolished();editor.show();QApplication::processEvents();
    editor.resize(460,280);editor.setFontPointSize(13);editor.syntaxHighlighter()->rehighlight();QApplication::processEvents();
    QCOMPARE(model.text(),source);QCOMPARE(model.revision(),revision);QVERIFY(!model.isModified());QVERIFY(!model.textDocument()->isUndoAvailable());
    // A genuine edit must survive presentation work, and its undo must return
    // directly to the saved source without a hidden margin-format undo step.
    editor.moveCursor(QTextCursor::End);editor.insertPlainText(QStringLiteral("% edited\n"));QVERIFY(model.isModified());
    const auto editedRevision=model.revision();editor.hide();editor.setFontPointSize(12);editor.show();editor.syntaxHighlighter()->rehighlight();QApplication::processEvents();
    QVERIFY(model.isModified());QCOMPARE(model.revision(),editedRevision);QVERIFY(model.text().endsWith(QStringLiteral("% edited\n")));
    editor.undo();QCOMPARE(model.text(),source);QVERIFY(!model.isModified());QVERIFY(!model.textDocument()->isUndoAvailable());
}
void HelpDesktopTests::aboutStartsWithAuthorsAndKeepsLicencesTogether(){
    desktop::AboutDialog dialog;auto* tabs=dialog.findChild<QTabWidget*>(QStringLiteral("aboutTabs"));QVERIFY(tabs);QCOMPARE(tabs->tabText(0),QStringLiteral("Authors"));
    QCOMPARE(dialog.findChild<QLabel*>(QStringLiteral("aboutCopyright"))->text(),desktop::studioCopyrightText());QCOMPARE(desktop::studioCopyrightText(2026),QString::fromUtf8("© 2026 Alireza Poshtkohi and Susanne Pumplün"));
    auto* notice=dialog.findChild<QLabel*>(QStringLiteral("aboutQtNotice"));QVERIFY(notice);bool contained=false;for(int i=0;i<tabs->count();++i)if(tabs->tabText(i)==QStringLiteral("Licences"))contained=tabs->widget(i)->isAncestorOf(notice);QVERIFY(contained);
    auto* banner=dialog.findChild<QWidget*>(QStringLiteral("aboutBanner"));QVERIFY(banner);QVERIFY(banner->height()>=160);
}
void HelpDesktopTests::aboutContactsAreReadableBlueMailLinks(){
    desktop::AboutDialog dialog;
    QCOMPARE(dialog.findChild<QLabel*>(QStringLiteral("aboutAuthor1Name"))->text(),QStringLiteral("Dr Alireza Poshtkohi"));
    QCOMPARE(dialog.findChild<QLabel*>(QStringLiteral("aboutAuthor2Name"))->text(),QString::fromUtf8("Dr Susanne Pumplün"));
    const std::array<std::pair<const char*,QString>,2> contacts{{
        {"aboutAuthor1Email",QStringLiteral("a.poshtkohi@herts.ac.uk")},
        {"aboutAuthor2Email",QStringLiteral("Susanne.Pumpluen@nottingham.ac.uk")}
    }};
    for(const auto& [name,address]:contacts){
        auto* label=dialog.findChild<QLabel*>(QString::fromLatin1(name));QVERIFY(label);QCOMPARE(label->textFormat(),Qt::RichText);
        QVERIFY(label->openExternalLinks());QVERIFY(label->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));QVERIFY(label->textInteractionFlags().testFlag(Qt::LinksAccessibleByKeyboard));
        QTextDocument text;text.setHtml(label->text());QCOMPARE(text.toPlainText(),address);QTextCursor cursor(&text);cursor.setPosition(1);
        const auto format=cursor.charFormat();QVERIFY(format.isAnchor());QCOMPARE(format.anchorHref(),QStringLiteral("mailto:")+address);
        QCOMPARE(format.foreground().color(),QColor(QStringLiteral("#0067b8")));QVERIFY(format.fontUnderline());
    }
}
void HelpDesktopTests::aboutArtworkFitsNormalAndNarrowBannerAreas(){
    desktop::AboutDialog dialog;dialog.resize(720,555);dialog.show();QApplication::processEvents();
    auto* banner=dialog.findChild<QWidget*>(QStringLiteral("aboutBanner"));auto* artwork=dialog.findChild<QWidget*>(QStringLiteral("aboutAlgebraArtwork"));QVERIFY(banner&&artwork);QCOMPARE(banner->height(),170);
    for(const int width:{180,96,48}){
        // Exercise the real view's fitting transform at a normal artwork width
        // and two narrow allocations; the document/scientific engine is idle.
        artwork->setFixedWidth(width);QApplication::processEvents();QCOMPARE(artwork->width(),width);
        const auto available=artwork->property("artworkAvailableRect").toRectF();const auto bounds=artwork->property("artworkPaintBounds").toRectF();
        QVERIFY(!available.isEmpty());QVERIFY(!bounds.isEmpty());QVERIFY(QRectF(artwork->rect()).contains(available));QVERIFY(available.adjusted(-.01,-.01,.01,.01).contains(bounds));
        QVERIFY(banner->rect().contains(artwork->geometry()));const auto labels=artwork->property("artworkLabelBounds").toList();QCOMPARE(labels.size(),5);
        for(const auto& label:labels){QVERIFY(!label.toRectF().isEmpty());QVERIFY(available.adjusted(-.01,-.01,.01,.01).contains(label.toRectF()));}
    }
}
PENTA_OFFSCREEN_TEST_MAIN(HelpDesktopTests)
#include "help_desktop_tests.moc"
