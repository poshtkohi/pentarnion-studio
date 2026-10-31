#include "help_article_browser.hpp"
#include "ide/syntax_highlighter.hpp"
#include "ide/math_text_renderer.hpp"
#include <QCryptographicHash>
#include <QMimeData>
#include <QPointer>
#include <QSet>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTextLayout>
#include <QTimer>
#include <algorithm>

namespace penta::assistant {
namespace {
QString topicLink(const CopilotReference& ref){return QStringLiteral("<a href=\"penta-help://topic/")+ref.id.toHtmlEscaped()+QStringLiteral("\">")+ref.title.toHtmlEscaped()+QStringLiteral("</a>");}
QString codeHtml(const QString& source){
    const auto bounded=source.left(48*1024);
    QTextDocument document;document.setPlainText(bounded);SyntaxHighlighter highlighter(&document);highlighter.setDocument(&document);highlighter.rehighlight();
    QString html=QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"9\" bgcolor=\"#f3f7fa\"><tr><td><pre style=\"white-space:pre-wrap;font-family:Consolas,'DejaVu Sans Mono',monospace;margin:0;\">");
    for(auto block=document.begin();block.isValid();block=block.next()){
        const auto text=block.text();int at=0;
        if(block.layout())for(const auto& run:block.layout()->formats()){
            const int first=std::max(at,run.start),end=std::min<int>(text.size(),run.start+run.length);if(first>=end)continue;
            html+=text.mid(at,first-at).toHtmlEscaped();
            const auto color=run.format.foreground().color();
            html+=QStringLiteral("<span style=\"color:%1;\">%2</span>").arg(color.isValid()?color.name():QStringLiteral("#203746"),text.mid(first,end-first).toHtmlEscaped());at=end;
        }
        html+=text.mid(at).toHtmlEscaped();if(block.next().isValid())html+=QLatin1Char('\n');
    }
    return html+QStringLiteral("</pre></td></tr></table>");
}
bool heading(const QString& line){
    static const QRegularExpression named(QStringLiteral("^(Syntax|Description|Examples?|Notes|Arguments|Returns|See also|Related topics|Language reference|Getting started|Scientific computing|Desktop controls)$"),QRegularExpression::CaseInsensitiveOption);
    return named.match(line).hasMatch()||((line.size()<110)&&line.contains(QRegularExpression(QStringLiteral("[A-Z]")))&&!line.contains(QRegularExpression(QStringLiteral("[a-z]")))&&!line.endsWith(';'));
}
QString pageStyle(){return QStringLiteral("<style>body{color:#243746;font-family:'Segoe UI','Noto Sans',sans-serif;font-size:10pt;}h1{font-size:20pt;color:#145579;margin:0 0 14px 0;}h2{font-size:13pt;color:#145579;margin:18px 0 8px 0;}p{margin:0 0 11px 0;line-height:1.35;}a{color:#0879b8;text-decoration:none;}code{font-family:Consolas,'DejaVu Sans Mono',monospace;color:#155474;}li{margin-bottom:7px;}.meta{color:#6a7d8c;font-size:9pt;}</style>");}
}
HelpArticleBrowser::HelpArticleBrowser(QWidget* parent):QTextBrowser(parent){
    setOpenLinks(false);setOpenExternalLinks(false);setLineWrapMode(QTextEdit::WidgetWidth);
    document()->setDocumentMargin(16);m_resize=new QTimer(this);m_resize->setSingleShot(true);m_resize->setInterval(90);
    connect(m_resize,&QTimer::timeout,this,[this]{if(!m_reference.id.isEmpty()&&!m_images.isEmpty())render(true);});
}
void HelpArticleBrowser::showReference(const CopilotReference& reference,const QList<CopilotReference>& catalogue){m_resize->stop();m_reference=reference;m_catalogue=catalogue;render(false);}
QString HelpArticleBrowser::mathHtml(const QString& source,bool display){
    const auto fallback=QStringLiteral("<code>")+source.toHtmlEscaped()+QStringLiteral("</code>");
    if(source.isEmpty()||source.size()>1024||m_images.size()>=128)return fallback;
    QString error;auto image=renderMathText(QStringLiteral("$")+source+QStringLiteral("$"),font(),QColor(QStringLiteral("#243746")),true,&error);
    if(image.isNull()||image.sizeInBytes()>16*1024*1024-m_imageBytes)return fallback;
    const auto key=QStringLiteral("penta-help:/math/")+QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(),QCryptographicHash::Sha256).toHex());
    if(!m_images.contains(key)){m_imageBytes+=image.sizeInBytes();m_images.insert(key,image);m_sources.insert(key,source);}
    const auto natural=image.deviceIndependentSize();const auto available=std::max(80,viewport()->width()-44);const qreal scale=std::min<qreal>(1,available/std::max<qreal>(1,natural.width()));
    const auto html=QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%3\" alt=\"%4\" />").arg(key).arg(std::max(1,qRound(natural.width()*scale))).arg(std::max(1,qRound(natural.height()*scale))).arg(source.toHtmlEscaped());
    return display?QStringLiteral("<p align=\"center\">")+html+QStringLiteral("</p>"):html;
}
QString HelpArticleBrowser::inlineHtml(const QString& text){
    QString html;for(qsizetype at=0;at<text.size();){
        if(text[at]=='`'){
            const auto end=text.indexOf('`',at+1);if(end>at){html+=QStringLiteral("<code>")+text.mid(at+1,end-at-1).toHtmlEscaped()+QStringLiteral("</code>");at=end+1;continue;}
        }
        if(text.mid(at,2)==QStringLiteral("\\(")){
            const auto end=text.indexOf(QStringLiteral("\\)"),at+2);if(end>=at+2){html+=mathHtml(text.mid(at+2,end-at-2));at=end+2;continue;}
        }
        html+=QString(text[at++]).toHtmlEscaped();
    }
    return html;
}
QString HelpArticleBrowser::bodyHtml(const QString& body){
    const auto lines=body.split('\n');QString html,paragraph,code;bool fenced=false,list=false;
    const auto flush=[&]{if(!paragraph.isEmpty()){html+=QStringLiteral("<p>")+inlineHtml(paragraph)+QStringLiteral("</p>");paragraph.clear();}if(list){html+=QStringLiteral("</ul>");list=false;}};
    for(const auto& original:lines){
        const auto line=original.trimmed();
        if(line.startsWith(QStringLiteral("```"))){flush();if(fenced){html+=codeHtml(code);code.clear();}fenced=!fenced;continue;}
        if(fenced){if(!code.isEmpty())code+='\n';code+=original;continue;}
        if(line.isEmpty()){flush();continue;}
        if(line.startsWith(QStringLiteral("\\["))&&line.endsWith(QStringLiteral("\\]"))){flush();html+=mathHtml(line.mid(2,line.size()-4),true);continue;}
        if(line.startsWith('#')||heading(line)){flush();auto title=line;while(title.startsWith('#'))title.remove(0,1);html+=QStringLiteral("<h2>")+title.trimmed().toHtmlEscaped()+QStringLiteral("</h2>");continue;}
        if(line.startsWith(QStringLiteral("- "))||line.startsWith(QStringLiteral("* "))){if(!paragraph.isEmpty()){html+=QStringLiteral("<p>")+inlineHtml(paragraph)+QStringLiteral("</p>");paragraph.clear();}if(!list){html+=QStringLiteral("<ul>");list=true;}html+=QStringLiteral("<li>")+inlineHtml(line.mid(2))+QStringLiteral("</li>");continue;}
        if(list){html+=QStringLiteral("</ul>");list=false;}
        if(!paragraph.isEmpty())paragraph+=' ';
        paragraph+=line;
    }
    flush();if(!code.isEmpty())html+=codeHtml(code);return html;
}
void HelpArticleBrowser::render(bool preserveView){
    const auto cursor=textCursor();const int position=cursor.position(),anchor=cursor.anchor(),scroll=verticalScrollBar()->value();const auto oldPlain=toPlainText();
    m_images.clear();m_sources.clear();m_imageBytes=0;
    auto* fresh=new QTextDocument(this);fresh->setDefaultFont(font());fresh->setDocumentMargin(16);QPointer<QTextDocument> previous=document();setDocument(fresh);if(previous&&previous->parent()==this)delete previous;
    QString html=pageStyle()+QStringLiteral("<h1>")+m_reference.title.toHtmlEscaped()+QStringLiteral("</h1>");
    if(m_reference.id==QStringLiteral("PENTA-home")||m_reference.id==QStringLiteral("PENTA-language")){
        html+=bodyHtml(m_reference.text);
        html+=QStringLiteral("<h2>")+tr("Browse documentation")+QStringLiteral("</h2><ul>");
        int count=0;for(const auto& ref:m_catalogue){
            const bool language=m_reference.id==QStringLiteral("PENTA-language");
            if(ref.id==m_reference.id)continue;
            if(language?ref.location.startsWith(QStringLiteral("language.txt:")):(ref.id.startsWith(QStringLiteral("PENTA-"))&&!ref.id.startsWith(QStringLiteral("PENTA-doc-"))&&!ref.id.startsWith(QStringLiteral("PENTA-command-")))){html+=QStringLiteral("<li>")+topicLink(ref)+QStringLiteral("</li>");if(++count>=180)break;}
        }
        html+=QStringLiteral("</ul>");
        if(m_reference.id==QStringLiteral("PENTA-language")){
            html+=QStringLiteral("<h2>Command reference</h2><p>Every command page includes its current syntax, description and linked specifications/examples.</p><p>");
            int commands=0;for(const auto& ref:m_catalogue)if(ref.id.startsWith(QStringLiteral("PENTA-command-"))){if(commands++)html+=QStringLiteral(" &nbsp; · &nbsp; ");html+=topicLink(ref);if(commands>=600)break;}html+=QStringLiteral("</p>");
        }
    }else if(m_reference.title.startsWith(QStringLiteral("Example: ")))html+=QStringLiteral("<p class=\"meta\">Runnable .p source. Viewing this example never executes it.</p>")+codeHtml(m_reference.text);
    else html+=bodyHtml(m_reference.text);
    QString links;int related=0;
    const QRegularExpression path(QStringLiteral("(?:specs|examples)/[A-Za-z0-9_./-]+"));auto paths=path.globalMatch(m_reference.text);QSet<QString> included;
    while(paths.hasNext()&&related<10){auto prefix=paths.next().captured();while(prefix.endsWith('.')||prefix.endsWith('/'))prefix.chop(1);for(const auto& ref:m_catalogue){
        const auto location=ref.location.section(':',0,0);if(ref.id!=m_reference.id&&!included.contains(ref.id)&&(location==prefix||location.startsWith(prefix+'/'))){included.insert(ref.id);links+=QStringLiteral("<li>")+topicLink(ref)+QStringLiteral("</li>");if(++related==10)break;}
    }}
    if(m_reference.id.startsWith(QStringLiteral("PENTA-command-"))){
        const auto command=m_reference.id.mid(QStringLiteral("PENTA-command-").size());
        const QRegularExpression called(QStringLiteral("(?<![A-Za-z0-9_])")+QRegularExpression::escape(command)+QStringLiteral("\\s*[\\(;]"));
        QList<CopilotReference> examples;for(const auto& ref:m_catalogue)if(ref.title.startsWith(QStringLiteral("Example: "))&&!included.contains(ref.id)&&called.match(ref.text).hasMatch())examples.append(ref);
        std::stable_sort(examples.begin(),examples.end(),[](const auto& a,const auto& b){return a.text.size()<b.text.size();});
        for(int index=0;index<std::min<int>(3,examples.size())&&related<10;++index){links+=QStringLiteral("<li>")+topicLink(examples[index])+QStringLiteral("</li>");++related;}
    }
    if(!links.isEmpty())html+=QStringLiteral("<h2>Examples and detailed contracts</h2><ul>")+links+QStringLiteral("</ul>");
    html+=QStringLiteral("<p class=\"meta\">")+m_reference.location.toHtmlEscaped()+QStringLiteral(" · Local documentation · ")+m_reference.id.toHtmlEscaped()+QStringLiteral("</p>");
    setHtml(html);setProperty("helpMathImageCount",m_images.size());setProperty("helpMathRasterBytes",m_imageBytes);setProperty("helpFormatted",true);
    if(preserveView&&oldPlain==toPlainText()){QTextCursor restored(document());restored.setPosition(std::clamp(anchor,0,document()->characterCount()-1));restored.setPosition(std::clamp(position,0,document()->characterCount()-1),QTextCursor::KeepAnchor);setTextCursor(restored);verticalScrollBar()->setValue(scroll);}else moveCursor(QTextCursor::Start);
}
QVariant HelpArticleBrowser::loadResource(int type,const QUrl& url){if(type==QTextDocument::ImageResource){const auto found=m_images.constFind(url.toString());if(found!=m_images.cend())return *found;}return {};}
QMimeData* HelpArticleBrowser::createMimeDataFromSelection()const{
    auto* mime=QTextBrowser::createMimeDataFromSelection();const auto selection=textCursor();if(!selection.hasSelection())return mime;
    QString text;const int begin=selection.selectionStart(),end=selection.selectionEnd();for(auto block=document()->findBlock(begin);block.isValid()&&block.position()<end;block=block.next()){
        for(auto it=block.begin();!it.atEnd();++it){const auto fragment=it.fragment();if(!fragment.isValid())continue;const int from=std::max(begin,fragment.position()),to=std::min(end,fragment.position()+fragment.length());if(to<=from)continue;const auto format=fragment.charFormat();text+=format.isImageFormat()?m_sources.value(format.toImageFormat().name()):fragment.text().mid(from-fragment.position(),to-from);}
        if(block.position()+block.length()-1<end&&block.next().isValid())text+='\n';
    }mime->setText(text);return mime;
}
void HelpArticleBrowser::resizeEvent(QResizeEvent* event){QTextBrowser::resizeEvent(event);if(!m_images.isEmpty())m_resize->start();}
}
