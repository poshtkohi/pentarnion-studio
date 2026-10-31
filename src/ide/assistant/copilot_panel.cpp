// Adapted from GTG Cell Studio desktop components; scientific engine remains separate.
#include "ide/assistant/copilot_panel.hpp"
#include "ide/math_text_renderer.hpp"
#include "ide/syntax_highlighter.hpp"
#include "ide/desktop/studio_icons.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QScrollArea>
#include <QStyle>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTextLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace penta::assistant {
using penta::desktop::studioCommandIcon;
using penta::desktop::applyStudioButtonIcons;
namespace {
QString bounded(const QString& text,int maximum){
    if(text.size()<=maximum)return text;
    int end=qMax(0,maximum);if(end>0&&text.at(end-1).isHighSurrogate())--end;
    return text.left(end);
}
QFont copilotMathFont(QFont font){
    // Match the actual logical pixel size used by the chat document, including
    // the current screen's DPI. Math serif fonts often have a smaller x-height
    // than the UI font, so compensate locally for equally readable variables.
    // Returning the math family makes this normalization idempotent when a
    // display row is passed to mathImage. Figure fonts are never changed.
    const int base=qMax(1,QFontInfo(font).pixelSize());const auto readingX=QFontMetricsF(font).xHeight();
    font.setPixelSize(base);font.setFamily(QStringLiteral("Times New Roman"));font.setStyleHint(QFont::Serif);font.setItalic(false);
    const auto mathX=QFontMetricsF(font).xHeight();const double adjustment=mathX>0?qBound(1.,readingX/mathX,1.3):1.;
    font.setPixelSize(qBound(base,int(std::ceil(base*adjustment)),qMax(base,int(std::floor(base*1.3)))));return font;
}
QToolButton* smallButton(QWidget* parent,const char* name,const QString& text,const QString& icon){
    auto* button=new QToolButton(parent);button->setObjectName(name);button->setText(text);
    button->setToolTip(text);button->setAccessibleName(text);button->setIcon(studioCommandIcon(icon));
    button->setIconSize(QSize(16,16));button->setAutoRaise(true);button->setToolButtonStyle(Qt::ToolButtonIconOnly);return button;
}
QString compactUsage(const QString& summary){
    // Only the application's fixed usage-summary grammar is interpreted.
    // Provider messages must never become token or currency measurements.
    const QRegularExpression pattern(QStringLiteral("^Input ([0-9]+|unknown) · cached ([0-9]+|unknown) · cache writes ([0-9]+|unknown) · output ([0-9]+|unknown)(?: · estimated \\$([0-9]+\\.[0-9]{6}) \\(your rates\\)| · cost unknown \\(incomplete usage/rates\\))?$"));
    const auto match=pattern.match(summary);
    if(!match.hasMatch())return summary=="Waiting for provider usage…"?QString("Waiting for usage…"):QString("Usage unavailable");
    const auto tokens=[](const QString& text){
        if(text=="unknown")return text;
        bool ok=false;
        const qint64 value=text.toLongLong(&ok);
        if(!ok)return QString("unknown");
        if(value<1000)return QString::number(value);
        const double divisor=value>=1000000000?1.0e9:value>=1000000?1.0e6:1.0e3;const QString suffix=value>=1000000000?"B":value>=1000000?"M":"k";
        auto number=QString::number(double(value)/divisor,'f',value/divisor<100?1:0);if(number.endsWith(".0"))number.chop(2);return number+suffix;
    };
    auto result=QString("In %1 · Out %2 · Cached %3").arg(tokens(match.captured(1)),tokens(match.captured(4)),tokens(match.captured(2)));
    if(!match.captured(5).isEmpty()){auto cost=match.captured(5);while(cost.endsWith('0'))cost.chop(1);if(cost.endsWith('.'))cost.chop(1);result+=" · ≈$"+cost;}
    else if(summary.contains("cost unknown"))result+=" · cost unknown";
    return result;
}
QImage roleImage(bool assistant){
    QImage image(36,36,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);image.setDevicePixelRatio(2);
    QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);
    painter.setBrush(assistant?QColor("#168ba3"):QColor("#64778a"));painter.drawEllipse(QRectF(0,0,18,18));
    painter.setBrush(Qt::white);
    if(assistant){
        QPainterPath spark;spark.moveTo(9,3);spark.lineTo(10.6,7.4);spark.lineTo(15,9);spark.lineTo(10.6,10.6);spark.lineTo(9,15);spark.lineTo(7.4,10.6);spark.lineTo(3,9);spark.lineTo(7.4,7.4);spark.closeSubpath();painter.drawPath(spark);
    }else{painter.drawEllipse(QRectF(6.3,3.3,5.4,5.4));painter.drawRoundedRect(QRectF(4.5,9.5,9,5.5),3,3);}
    return image;
}
const QStringList& welcomePrompts(){
    static const QStringList prompts{
        "Explain this Pentarnion expression and its multiplication grouping.",
        "Check the selected .p source and explain its diagnostics.",
        "How can I plot a Pentarnion vector in two or three dimensions?",
        "Explain analytic Jacobians and native equation caching.",
        "Help me choose an ODE solver for this supplied equation.",
        "Explain how left and right multiplication operators differ.",
        "How can I reproduce a scientific calculation with a .p script?",
        "Which documented language commands apply to my question?"
    };return prompts;
}
// QTextBrowser must never resolve model-supplied URLs, local files or remote
// pictures. Its only resources are bounded images made by this application.
class SafeTranscript final:public QTextBrowser {
public:
    static constexpr qint64 MaximumRasterBytes=16*1024*1024;
    static constexpr qint64 MaximumImagePixels=256*1024;
    explicit SafeTranscript(QWidget* parent):QTextBrowser(parent){
        setOpenLinks(false);setOpenExternalLinks(false);setReadOnly(true);setUndoRedoEnabled(false);
        setFrameShape(QFrame::NoFrame);setTextInteractionFlags(Qt::TextBrowserInteraction);
        images.insert("penta-copilot:/role/user",roleImage(false));images.insert("penta-copilot:/role/assistant",roleImage(true));
        updateRasterProperties();
    }
    QHash<QString,QImage> images;
    QHash<QString,QString> mathSources;
    int blockedResources=0;
    bool resetDocumentResources=false;
    qint64 rasterBytes=0;
    void updateRasterProperties(){
        rasterBytes=0;qint64 largest=0;for(const auto& image:images){rasterBytes+=image.sizeInBytes();largest=qMax(largest,qint64(image.width())*image.height());}
        setProperty("copilotRasterBytes",rasterBytes);setProperty("copilotRasterLimit",MaximumRasterBytes);setProperty("copilotLargestRasterPixels",largest);setProperty("copilotRasterImages",images.size());
    }
    void clearMathResources(){
        for(auto it=images.begin();it!=images.end();)if(it.key().startsWith("penta-copilot:/math/"))it=images.erase(it);else ++it;
        mathSources.clear();
        updateRasterProperties();resetDocumentResources=true;
    }
    void dropDocumentResourceCache(){
        if(!resetDocumentResources)return;
        // QTextDocument can retain implicitly shared resource copies. Replace
        // that cache as well when old transcript turns are discarded.
        auto* fresh=new QTextDocument(this);fresh->setDefaultFont(font());fresh->setDocumentMargin(7);setDocument(fresh);resetDocumentResources=false;
    }
    QVariant loadResource(int type,const QUrl& name)override{
        if(type==QTextDocument::ImageResource){auto found=images.constFind(name.toString());if(found!=images.cend())return *found;}
        ++blockedResources;return {};
    }
    int availableMathWidth()const{
        // Reserve scrollbar space even while it is hidden. Otherwise changing
        // formula height can repeatedly toggle the scrollbar and math cache.
        return qBound(80,width()-int(2*document()->documentMargin())-style()->pixelMetric(QStyle::PM_ScrollBarExtent)-16,1600);
    }
    QMimeData* createMimeDataFromSelection()const override{
        auto* original=QTextBrowser::createMimeDataFromSelection();const auto selection=textCursor();if(!selection.hasSelection())return original;
        const int begin=selection.selectionStart(),end=selection.selectionEnd();QString plain;
        for(auto block=document()->findBlock(begin);block.isValid()&&block.position()<end;block=block.next()){
            for(auto it=block.begin();!it.atEnd();++it){const auto fragment=it.fragment();if(!fragment.isValid())continue;const int from=qMax(begin,fragment.position()),to=qMin(end,fragment.position()+fragment.length());if(to<=from)continue;
                if(fragment.charFormat().isImageFormat()){const auto name=fragment.charFormat().toImageFormat().name();if(mathSources.contains(name))plain+=mathSources.value(name);}
                else plain+=fragment.text().mid(from-fragment.position(),to-from);
            }
            if(block.position()+block.length()-1<end&&block.next().isValid())plain+='\n';
        }
        // QTextBrowser supplies a lazy QTextDocumentFragment MIME subclass;
        // overriding its stored text does not replace its retrieveData result.
        // Materialize the rich formats into an ordinary MIME object first.
        auto* mime=new QMimeData;
        for(const auto& format:original->formats())if(format!="text/plain")mime->setData(format,original->data(format));
        if(original->hasImage())mime->setImageData(original->imageData());
        delete original;mime->setText(plain);return mime;
    }
    QString mathImage(const QString& source,const QFont& requestedFont){
        if(source.isEmpty()||source.size()>4096)return "<code>"+source.toHtmlEscaped()+"</code>";
        const auto font=copilotMathFont(requestedFont);
        const QString key="penta-copilot:/math/"+QString::fromLatin1(QCryptographicHash::hash(source.toUtf8()+font.toString().toUtf8()+QByteArray::number(availableMathWidth()),QCryptographicHash::Sha256).toHex());
        auto found=images.constFind(key);
        if(found==images.cend()){
            if(images.size()>258)return "<code>"+source.toHtmlEscaped()+"</code>";
            auto rasterFont=font;rasterFont.setPixelSize(qMax(1,font.pixelSize()*2));
            QString error;QImage image=penta::renderMathText(source,rasterFont,QColor("#243746"),true,&error);
            if(image.isNull())return "<code>"+source.toHtmlEscaped()+"</code>";
            if(image.width()>availableMathWidth()*2||image.height()>360)
                image=image.scaled(QSize(availableMathWidth()*2,360),Qt::KeepAspectRatio,Qt::SmoothTransformation);
            image.setDevicePixelRatio(2);
            if(qint64(image.width())*image.height()>MaximumImagePixels||image.sizeInBytes()>MaximumRasterBytes-rasterBytes)
                return "<code>"+source.toHtmlEscaped()+"</code>";
            images.insert(key,image);mathSources.insert(key,source);updateRasterProperties();found=images.constFind(key);
        }
        return QString("<img src=\"%1\" width=\"%2\" height=\"%3\" alt=\"%4\" />").arg(key).arg(found->width()/2).arg(found->height()/2).arg(source.toHtmlEscaped());
    }
    QString displayMath(const QString& source,const QFont& font){return mathImage(source,font);}

};
QString formatCode(const QString& source,const QString& language){
    QTextDocument highlighted;highlighted.setPlainText(source);highlighted.setProperty("pentaSourceDialect",language.toLower());
    if(language.compare("p",Qt::CaseInsensitive)==0||language.compare("pentarnion",Qt::CaseInsensitive)==0){auto* syntax=new penta::SyntaxHighlighter(&highlighted);syntax->rehighlight();}
    QString html;
    for(auto block=highlighted.begin();block.isValid();block=block.next()){
        const auto text=block.text();auto runs=block.layout()->formats();
        std::sort(runs.begin(),runs.end(),[](const auto& a,const auto& b){return a.start<b.start;});int cursor=0;
        for(const auto& run:runs){
            const int begin=qBound(cursor,run.start,int(text.size())),end=qBound(begin,run.start+run.length,int(text.size()));
            html+=text.mid(cursor,begin-cursor).toHtmlEscaped();QString style;
            if(run.format.foreground().style()!=Qt::NoBrush)style+="color:"+run.format.foreground().color().name()+";";
            if(run.format.fontWeight()>=QFont::DemiBold)style+="font-weight:600;";
            if(run.format.fontItalic())style+="font-style:italic;";
            html+="<span style=\""+style+"\">"+text.mid(begin,end-begin).toHtmlEscaped()+"</span>";cursor=end;
        }
        html+=text.mid(cursor).toHtmlEscaped();if(block.next().isValid())html+='\n';
    }
    return html;
}
bool internalReferenceId(const QString& text){
    if(!text.startsWith("PENTA-")||text.size()<=6)return false;
    return std::all_of(text.cbegin()+6,text.cend(),[](QChar ch){return (ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='.';});
}
bool internalReferenceLabel(const QString& text,bool pending=false){
    // A citation may group several IDs, but prose in brackets is not a citation.
    // Bound the group independently of the overall reply's display limit.
    if(text.size()>4096)return false;
    static const QRegularExpression separators(QStringLiteral("[,;\\s]+"));
    const auto ids=text.split(separators,Qt::SkipEmptyParts);
    if(ids.isEmpty())return false;
    for(qsizetype index=0;index<ids.size();++index){
        if(internalReferenceId(ids[index]))continue;
        if(pending&&index==ids.size()-1&&QStringLiteral("PENTA-").startsWith(ids[index]))continue;
        return false;
    }
    return true;
}
QString inlineMarkup(const QString& text,SafeTranscript* browser,const QFont& font,const QList<CopilotReference>& references,bool streaming=false,int depth=0){
    QString html;
    int at=0;
    const auto referenceKnown=[&](const QString& id){return std::any_of(references.cbegin(),references.cend(),[&](const auto& item){return item.id==id;});};
    while(at<text.size()){
        if(text.at(at)=='`'){
            const int end=text.indexOf('`',at+1);if(end>at){html+="<code>"+text.mid(at+1,end-at-1).toHtmlEscaped()+"</code>";at=end+1;continue;}
        }
        const bool slashMath=text.mid(at,2)=="\\(",slashDisplay=text.mid(at,2)=="\\[";
        const bool dollarMath=text.at(at)=='$'&&(at==0||text.at(at-1)!='\\'),dollarDisplay=dollarMath&&text.mid(at,2)=="$$";
        if(slashMath||slashDisplay||dollarMath){
            const QString close=slashMath?"\\)":slashDisplay?"\\]":dollarDisplay?"$$":"$";const int start=at+(slashMath||slashDisplay||dollarDisplay?2:1),end=text.indexOf(close,start);
            if(end>=start){html+=(slashDisplay||dollarDisplay)?browser->displayMath(text.mid(start,end-start),font):browser->mathImage(text.mid(start,end-start),font);at=end+close.size();continue;}
        }
        if(depth<4&&(text.mid(at,2)=="**"||text.mid(at,2)=="__")){
            const auto token=text.mid(at,2);const int end=text.indexOf(token,at+2);
            if(end>at+2){html+="<b>"+inlineMarkup(text.mid(at+2,end-at-2),browser,font,references,streaming,depth+1)+"</b>";at=end+2;continue;}
        }
        if(depth<4&&(text.at(at)=='*'||text.at(at)=='_')&&(at==0||!text.at(at-1).isLetterOrNumber())){
            const auto token=text.at(at);const int end=text.indexOf(token,at+1);
            if(end>at+1){html+="<i>"+inlineMarkup(text.mid(at+1,end-at-1),browser,font,references,streaming,depth+1)+"</i>";at=end+1;continue;}
        }
        if(text.at(at)=='['){
            const int end=text.indexOf(']',at+1);
            if(end>at){
                const auto label=text.mid(at+1,end-at-1);
                // Reference IDs belong to local provenance and Help routing,
                // not the reply's prose. Keep the stored answer and code intact;
                // human-readable source titles are linked below the response.
                const bool internal=internalReferenceLabel(label)||referenceKnown(label);
                if((depth<4||internal)&&end+1<text.size()&&text.at(end+1)=='('){
                    const int urlEnd=text.indexOf(')',end+2);
                    if(urlEnd>=0){if(!internal)html+=inlineMarkup(label,browser,font,references,streaming,depth+1);at=urlEnd+1;continue;}
                }
                if(internal){at=end+1;continue;}
            }else if(end<0){
                const auto tail=text.mid(at+1);
                // Deltas can stop anywhere inside a marker. Withhold its tail
                // until it becomes ordinary text or completes, avoiding a flash
                // of hashes while streaming. Finished ordinary brackets survive.
                if(internalReferenceLabel(tail,streaming||tail.startsWith("PENTA-")))break;
            }
        }
        html+=QString(text.at(at)).toHtmlEscaped();++at;
    }
    return html;
}
QString markdown(const QString& source,SafeTranscript* browser,const QFont& font,const QList<CopilotReference>& references,
                 const std::function<QString(const QString&,const QString&,bool)>& codeCard,bool streaming){
    const auto lines=source.split('\n');QString html,paragraph,list;int at=0;
    const auto inlineText=[&](const QString& text){return inlineMarkup(text,browser,font,references,streaming);};
    const auto flush=[&]{if(!paragraph.isEmpty()){html+="<p>"+inlineText(paragraph)+"</p>";paragraph.clear();}if(!list.isEmpty()){html+="</"+list+">";list.clear();}};
    const auto tableCells=[](QString line){line=line.trimmed();if(line.startsWith('|'))line.remove(0,1);if(line.endsWith('|'))line.chop(1);return line.split('|');};
    const QRegularExpression tableRule("^\\s*\\|?\\s*:?-{3,}:?\\s*(?:\\|\\s*:?-{3,}:?\\s*)+\\|?\\s*$");
    while(at<lines.size()){
        const auto line=lines.at(at),trimmed=line.trimmed();
        if(trimmed.startsWith("```")){
            flush();const auto language=trimmed.mid(3).trimmed().toLower();QString code;bool complete=false,firstLine=true;++at;
            while(at<lines.size()){if(lines.at(at).trimmed()=="```"){complete=true;++at;break;}if(!firstLine)code+='\n';firstLine=false;code+=lines.at(at++);}
            html+=codeCard(code,language,complete);continue;
        }
        if(trimmed=="\\["||trimmed=="$$"){
            flush();const auto close=trimmed=="$$"?"$$":"\\]";QString formula;++at;
            while(at<lines.size()&&lines.at(at).trimmed()!=close){formula+=lines.at(at++)+' ';}
            if(at<lines.size()){++at;html+="<p align=\"center\">"+browser->displayMath(formula.trimmed(),font)+"</p>";}
            else html+="<p><code>"+(trimmed+'\n'+formula).toHtmlEscaped()+"</code></p>";
            continue;
        }
        if(trimmed.startsWith("\\[")&&trimmed.endsWith("\\]")){flush();html+="<p align=\"center\">"+browser->displayMath(trimmed.mid(2,trimmed.size()-4),font)+"</p>";++at;continue;}
        if(trimmed.startsWith("$$")&&trimmed.endsWith("$$")&&trimmed.size()>4){flush();html+="<p align=\"center\">"+browser->displayMath(trimmed.mid(2,trimmed.size()-4),font)+"</p>";++at;continue;}
        if(at+1<lines.size()&&line.contains('|')&&tableRule.match(lines.at(at+1)).hasMatch()){
            flush();html+="<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\" style=\"border-color:#d9e2e8;\">";
            const auto headers=tableCells(line);html+="<tr>";for(const auto& cell:headers)html+="<th bgcolor=\"#f0f5f8\">"+inlineText(cell.trimmed())+"</th>";html+="</tr>";at+=2;
            while(at<lines.size()&&lines.at(at).contains('|')&&!lines.at(at).trimmed().isEmpty()){
                html+="<tr>";const auto cells=tableCells(lines.at(at++));for(const auto& cell:cells)html+="<td>"+inlineText(cell.trimmed())+"</td>";html+="</tr>";
            }html+="</table>";continue;
        }
        if(trimmed.isEmpty()){flush();++at;continue;}
        const auto heading=QRegularExpression("^(#{1,6})\\s+(.+)$").match(trimmed);
        if(heading.hasMatch()){flush();const int level=qMin(4,int(heading.captured(1).size())+1);html+=QString("<h%1>%2</h%1>").arg(level).arg(inlineText(heading.captured(2)));++at;continue;}
        const auto bullet=QRegularExpression("^(?:[-*+]\\s+|[0-9]+[.)]\\s+)(.+)$").match(trimmed);
        if(bullet.hasMatch()){
            if(!paragraph.isEmpty()){html+="<p>"+inlineText(paragraph)+"</p>";paragraph.clear();}
            const auto kind=trimmed.at(0).isDigit()?QString("ol"):QString("ul");if(list!=kind){if(!list.isEmpty())html+="</"+list+">";list=kind;html+="<"+list+">";}
            html+="<li>"+inlineText(bullet.captured(1))+"</li>";++at;continue;
        }
        if(trimmed.startsWith('>')){flush();html+="<blockquote>"+inlineText(trimmed.mid(1).trimmed())+"</blockquote>";++at;continue;}
        if(trimmed=="---"||trimmed=="***"){flush();html+="<hr />";++at;continue;}
        if(!list.isEmpty()){html+="</"+list+">";list.clear();}if(!paragraph.isEmpty())paragraph+=' ';paragraph+=line;++at;
    }
    flush();return html;
}
}

CopilotPanel::CopilotPanel(QWidget* parent):QWidget(parent){
    setObjectName("copilotPanel");setMinimumWidth(270);
    auto reading=QFontDatabase::systemFont(QFontDatabase::GeneralFont);const auto families=QFontDatabase::families();
    for(const auto* family:{"Segoe UI","Noto Sans","DejaVu Sans","Arial"})if(families.contains(QString::fromLatin1(family),Qt::CaseInsensitive)){reading.setFamily(QString::fromLatin1(family));break;}
    reading.setStyleHint(QFont::SansSerif);reading.setStretch(QFont::Unstretched);reading.setPointSizeF(qMax(9.5,reading.pointSizeF()));setFont(reading);
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(7,5,7,5);layout->setSpacing(4);
    auto* header=new QHBoxLayout;header->setSpacing(3);
    connection_=new QLabel("Not connected",this);connection_->setObjectName("copilotConnection");connection_->setTextFormat(Qt::PlainText);connection_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);connection_->setStyleSheet("color:#536a7a;");header->addWidget(connection_,1);
    newChat_=smallButton(this,"copilotNewChat","New chat","New model");header->addWidget(newChat_);
    settings_=smallButton(this,"copilotSettings","Copilot settings","Options");header->addWidget(settings_);layout->addLayout(header);
    transcript_=new SafeTranscript(this);transcript_->setObjectName("copilotTranscript");
    transcript_->setFont(reading);
    transcript_->setStyleSheet("QTextBrowser { background:white; color:#243746; border:0; selection-background-color:#cfe6fa; }");
    transcript_->document()->setDocumentMargin(7);transcript_->setPlaceholderText("Ask about Pentarnion, explain your model, or review a proposed change.");layout->addWidget(transcript_,1);
    auto* welcomeScroll=new QScrollArea(this);welcome_=welcomeScroll;welcome_->setObjectName("copilotWelcome");welcomeScroll->setWidgetResizable(true);welcomeScroll->setFrameShape(QFrame::NoFrame);welcomeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);welcome_->setStyleSheet("QScrollArea#copilotWelcome{background:white;border:0;}QWidget#copilotWelcomeContent{background:white;}");
    auto* welcomeContent=new QWidget(welcome_);welcomeContent->setObjectName("copilotWelcomeContent");welcomeContent->setAttribute(Qt::WA_StyledBackground);welcomeScroll->setWidget(welcomeContent);
    auto* welcomeLayout=new QVBoxLayout(welcomeContent);welcomeLayout->setContentsMargins(7,10,7,7);welcomeLayout->setSpacing(11);
    auto* welcomeHeading=new QHBoxLayout;welcomeHeading->setSpacing(6);auto* avatar=new QLabel(welcome_);avatar->setObjectName("copilotWelcomeAvatar");avatar->setPixmap(QPixmap::fromImage(roleImage(true)));avatar->setFixedSize(18,18);welcomeHeading->addWidget(avatar);
    auto* welcomeTitle=new QLabel("Copilot",welcome_);welcomeTitle->setObjectName("copilotWelcomeTitle");welcomeTitle->setStyleSheet("font-weight:600;color:#42647c;");welcomeHeading->addWidget(welcomeTitle);welcomeHeading->addStretch();welcomeLayout->addLayout(welcomeHeading);
    auto* greeting=new QLabel("Hello! I can help you understand Pentarnion, explain models and review proposed changes.",welcome_);greeting->setObjectName("copilotWelcomeGreeting");greeting->setTextFormat(Qt::PlainText);greeting->setWordWrap(true);welcomeLayout->addWidget(greeting);
    auto* validation=new QLabel("Generated answers can be incorrect. Check the science and validate model changes before use.",welcome_);validation->setObjectName("copilotWelcomeValidation");validation->setTextFormat(Qt::PlainText);validation->setWordWrap(true);validation->setStyleSheet("QLabel#copilotWelcomeValidation{color:#466477;background:#f5f9fc;border:1px solid #c6dbe8;border-radius:3px;padding:7px;}");welcomeLayout->addWidget(validation);
    shuffleExamples_=new QPushButton("Shuffle Example Prompts",welcome_);shuffleExamples_->setObjectName("copilotShuffleExamples");shuffleExamples_->setIcon(studioCommandIcon("Shuffle Example Prompts"));shuffleExamples_->setIconSize({14,14});shuffleExamples_->setToolTip("Show other local Pentarnion example questions. This does not contact a provider.");welcomeLayout->addWidget(shuffleExamples_,0,Qt::AlignHCenter);
    welcomeExamples_=new QLabel(welcome_);welcomeExamples_->setObjectName("copilotWelcomeExamples");welcomeExamples_->setWordWrap(true);welcomeExamples_->setTextFormat(Qt::RichText);welcomeExamples_->setTextInteractionFlags(Qt::LinksAccessibleByMouse|Qt::LinksAccessibleByKeyboard);welcomeExamples_->setOpenExternalLinks(false);welcomeLayout->addWidget(welcomeExamples_);
    auto* sharing=new QLabel("Examples fill the question box without sending. To discuss your model, select it as context before you send.",welcome_);sharing->setObjectName("copilotWelcomeSharing");sharing->setTextFormat(Qt::PlainText);sharing->setWordWrap(true);sharing->setStyleSheet("color:#617482;");welcomeLayout->addWidget(sharing);welcomeLayout->addStretch();layout->addWidget(welcome_,1);welcome_->hide();
    connect(welcomeExamples_,&QLabel::linkActivated,this,[this](const QString& link){handleAction(QUrl(link));});
    connect(shuffleExamples_,&QPushButton::clicked,this,[this]{if(busy_||scienceBusy_||!messages_.isEmpty())return;exampleOffset_=(exampleOffset_+3)%welcomePrompts().size();refreshWelcomeExamples();});refreshWelcomeExamples();
    auto* mathReflow=new QTimer(this);mathReflow->setObjectName("copilotMathReflow");mathReflow->setSingleShot(true);mathReflow->setInterval(90);
    connect(mathReflow,&QTimer::timeout,this,[this]{auto* browser=static_cast<SafeTranscript*>(transcript_);if(browser->property("copilotMathWidth").toInt()!=browser->availableMathWidth())renderTranscript();});
    transcript_->viewport()->installEventFilter(this);
    connect(transcript_,&QTextBrowser::anchorClicked,this,&CopilotPanel::handleAction);
    auto* contextRow=new QHBoxLayout;contextRow->setSpacing(3);
    addFile_=smallButton(this,"copilotAddFile","Add File","Add File");addFile_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);addFile_->setAutoRaise(false);addFile_->setToolTip("Attach a UTF-8 Pentarnion model or text/data file (.p, .txt, .md, .log, .json, .csv, .tsv), up to 64 KiB each. Files stay local until you send a question; review or remove them with the context controls.");contextRow->addWidget(addFile_);
    contextToggle_=smallButton(this,"copilotContextToggle","Context: None · Pentarnion reference","Project");contextToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);contextToggle_->setCheckable(true);contextToggle_->setArrowType(Qt::DownArrow);contextToggle_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);contextRow->addWidget(contextToggle_,1);
    attachments_=smallButton(this,"copilotAddContext","Add context","Open");contextRow->addWidget(attachments_);
    preview_=smallButton(this,"copilotPreviewContext","Review outgoing context","Print preview");contextRow->addWidget(preview_);layout->addLayout(contextRow);
    advanced_=new QWidget(this);advanced_->setObjectName("copilotAdvancedContext");auto* advancedLayout=new QVBoxLayout(advanced_);advancedLayout->setContentsMargins(0,0,0,2);advancedLayout->setSpacing(3);
    context_=new QComboBox(advanced_);context_->setObjectName("copilotContext");context_->setAccessibleName("Source context sent to provider");
    context_->addItem("None",int(ContextMode::None));context_->addItem("Selection",int(ContextMode::Selection));context_->addItem("Current source",int(ContextMode::CurrentSource));advancedLayout->addWidget(context_);
    reference_=new QCheckBox("Include Pentarnion reference",advanced_);reference_->setObjectName("copilotIncludeReference");reference_->setChecked(true);reference_->setToolTip("Include the relevant bundled Pentarnion documentation and examples.");advancedLayout->addWidget(reference_);
    contextDescription_=new QLabel("No model text included",advanced_);contextDescription_->setObjectName("copilotContextDescription");contextDescription_->setWordWrap(true);contextDescription_->setTextFormat(Qt::PlainText);advancedLayout->addWidget(contextDescription_);advanced_->hide();layout->addWidget(advanced_);
    connect(contextToggle_,&QToolButton::toggled,this,[this](bool open){advanced_->setVisible(open);contextToggle_->setArrowType(open?Qt::UpArrow:Qt::DownArrow);});
    connect(attachments_,&QToolButton::clicked,this,&CopilotPanel::contextAttachmentsRequested);
    connect(addFile_,&QToolButton::clicked,this,&CopilotPanel::fileAttachmentsRequested);
    prompt_=new QPlainTextEdit(this);prompt_->setObjectName("copilotPrompt");prompt_->setAccessibleName("Message to Copilot");prompt_->setPlaceholderText("Ask Pentarnion Copilot  ·  Ctrl+Enter");prompt_->setTabChangesFocus(true);prompt_->installEventFilter(this);prompt_->setStyleSheet("QPlainTextEdit#copilotPrompt { background:white; border:1px solid #bdcbd5; border-radius:4px; padding:3px; }QPlainTextEdit#copilotPrompt:enabled:hover {border-color:#159b9b;}");layout->addWidget(prompt_);
    auto* footer=new QHBoxLayout;footer->setSpacing(4);
    usage_=new QLabel(this);usage_->setObjectName("copilotUsage");usage_->setTextFormat(Qt::PlainText);usage_->setStyleSheet("color:#617482;");usage_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);usage_->installEventFilter(this);usage_->hide();footer->addWidget(usage_,1);footer->addStretch(0);
    review_=new QPushButton("Review code",this);review_->setObjectName("copilotReviewCode");review_->setIcon(studioCommandIcon("Generate model"));review_->setIconSize(QSize(14,14));review_->setFlat(true);review_->setToolTip("Review the completed Pentarnion suggestion before applying it.");footer->addWidget(review_);
    stop_=new QPushButton(this);stop_->setObjectName("copilotStop");stop_->setIcon(studioCommandIcon("Stop"));stop_->setIconSize(QSize(16,16));stop_->setToolTip("Stop response");stop_->setAccessibleName("Stop response");stop_->setFixedSize(28,24);footer->addWidget(stop_);
    send_=new QPushButton(this);send_->setObjectName("copilotSend");send_->setIcon(studioCommandIcon("Next"));send_->setIconSize(QSize(16,16));send_->setToolTip("Send (Ctrl+Enter)");send_->setAccessibleName("Send message");send_->setFixedSize(28,24);send_->setDefault(false);footer->addWidget(send_);layout->addLayout(footer);
    status_=new QLabel(this);status_->setObjectName("copilotStatus");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);status_->hide();layout->addWidget(status_);
    privacy_=new QLabel(this);privacy_->setObjectName("copilotPrivacy");privacy_->setTextFormat(Qt::PlainText);privacy_->setWordWrap(true);privacy_->setStyleSheet("color:#617482;");layout->addWidget(privacy_);
    presentation_=new QTimer(this);presentation_->setObjectName("copilotPresentation");presentation_->setSingleShot(true);presentation_->setInterval(65);connect(presentation_,&QTimer::timeout,this,&CopilotPanel::flushAssistant);
    connect(send_,&QPushButton::clicked,this,&CopilotPanel::sendPrompt);connect(stop_,&QPushButton::clicked,this,&CopilotPanel::stopRequested);connect(settings_,&QToolButton::clicked,this,&CopilotPanel::settingsRequested);connect(preview_,&QToolButton::clicked,this,&CopilotPanel::contextPreviewRequested);connect(newChat_,&QToolButton::clicked,this,&CopilotPanel::newChatRequested);
    connect(review_,&QPushButton::clicked,this,[this]{if(!reviewSource_.isEmpty()&&!busy_&&!scienceBusy_)emit codeReviewRequested(reviewSource_);});
    connect(context_,&QComboBox::currentIndexChanged,this,[this]{updateControls();emit contextChanged();});connect(reference_,&QCheckBox::toggled,this,[this]{updateControls();emit contextChanged();});
    connect(prompt_,&QPlainTextEdit::textChanged,this,[this]{
        if(!trimmingPrompt_&&prompt_->toPlainText().size()>MaximumPromptCharacters){
            trimmingPrompt_=true;auto cursor=prompt_->textCursor();int cutoff=MaximumPromptCharacters;if(prompt_->toPlainText().at(cutoff-1).isHighSurrogate())--cutoff;const int position=qMin(cursor.position(),cutoff);
            cursor=QTextCursor(prompt_->document());cursor.setPosition(cutoff);cursor.movePosition(QTextCursor::End,QTextCursor::KeepAnchor);cursor.removeSelectedText();cursor.setPosition(qMin(position,int(prompt_->toPlainText().size())));prompt_->setTextCursor(cursor);trimmingPrompt_=false;
            status_->setText("Message limit reached (32,768 characters). Shorten or split your question.");status_->show();
        }resizeComposer();updateControls();
    });
    setConfigured(false);resizeComposer();
}
QSize CopilotPanel::sizeHint()const{return QSize(350,650);}
CopilotPanel::ContextMode CopilotPanel::contextMode()const{return static_cast<ContextMode>(context_->currentData().toInt());}
bool CopilotPanel::includeReference()const{return reference_->isChecked();}
void CopilotPanel::setConfigured(bool configured,const QString& providerAndModel){
    configured_=configured;providerAndModel_=providerAndModel;connection_->setText(configured?providerAndModel:"Not connected");connection_->setToolTip(connection_->text());
    privacy_->setText(configured?"":"Connect your provider in settings to start.");privacy_->setVisible(!configured);
    privacy_->setToolTip("Only your message and selected context are sent. API usage may incur charges.");contextToggle_->setToolTip("Select source context and Pentarnion reference. API usage may incur charges.");
    if(!busy_){status_->setText(scienceBusy_?"Copilot is paused while a model operation runs.":configured?"Ready":"Configure a provider to start.");status_->setVisible(scienceBusy_);}
    updateControls();
}
void CopilotPanel::setBusy(bool busy){if(busy_!=busy)for(auto& message:messages_)message.html.clear();busy_=busy;if(busy_)reviewSource_.clear();renderTranscript();updateControls();}
void CopilotPanel::setScienceBusy(bool busy){
    // Editor cursor/status updates can repeat the same execution state. Reusing
    // it must not rebuild rich text, discard cached math/code or disturb reading.
    // Initial controls are already established by setConfigured in the ctor.
    if(scienceBusy_==busy)return;
    scienceBusy_=busy;for(auto& message:messages_)message.html.clear();
    if(busy)status_->setText("Copilot is paused while a model operation runs.");
    else if(!busy_)status_->setText(configured_?"Ready":"Configure a provider to start.");
    status_->setVisible(scienceBusy_);renderTranscript();updateControls();
}
void CopilotPanel::setContextDescription(const QString& text){contextDescription_->setText(bounded(text,1024));contextToggle_->setToolTip(contextDescription_->text()+"\nOnly selected context is sent; API usage may incur charges.");}
void CopilotPanel::setAttachmentSummary(const QString& text){attachmentSummary_=bounded(text,512);attachments_->setToolTip(attachmentSummary_.isEmpty()?"Add context":attachmentSummary_);updateControls();}
void CopilotPanel::setUsageSummary(const QString& text,const QString& tooltip){
    const auto compact=text.isEmpty()?QString():compactUsage(text);usage_->setProperty("copilotCompactUsage",compact);
    usage_->setText(usage_->fontMetrics().elidedText(compact,Qt::ElideMiddle,qMax(1,usage_->width())));
    usage_->setToolTip(text+(tooltip.isEmpty()?QString():"\n\n"+bounded(tooltip,4096)));usage_->setVisible(!text.isEmpty());
}
void CopilotPanel::setReferences(const QList<CopilotReference>& references){
    references_=references.mid(0,24);if(assistantOpen_&&!messages_.isEmpty()){messages_.last().references=references_;messages_.last().html.clear();renderTranscript();}
}
void CopilotPanel::setFollowupPrompts(const QStringList& prompts){
    followups_.clear();for(const auto& prompt:prompts){if(followups_.size()==3)break;if(!prompt.trimmed().isEmpty())followups_.append(bounded(prompt.trimmed(),180));}
    if(!messages_.isEmpty()&&messages_.last().role=="assistant"){messages_.last().followups=followups_;renderTranscript();}
}
void CopilotPanel::invalidateProposals(const QString& reason){
    reviewSource_.clear();if(assistantOpen_)currentProposalInvalidated_=true;for(auto& message:messages_){message.valid=false;message.html.clear();}renderTranscript();updateControls();if(!reason.isEmpty())appendNotice(reason);
}
void CopilotPanel::appendNotice(const QString& text){
    if(text.trimmed().isEmpty())return;
    flushAssistant();
    const Message notice{"notice",bounded(text,2048),{},{},true,false,{},{},{}};
    // Keep the streaming assistant as the last message: later deltas belong to
    // that response, never to the intervening source-revision notice.
    if(assistantOpen_&&!messages_.isEmpty()&&messages_.last().role=="assistant")messages_.insert(messages_.size()-1,notice);else messages_.append(notice);
    trimTranscript();renderTranscript();
}
bool CopilotPanel::hasConversation() const{
    for(const auto& message:messages_)if(message.role=="user"||message.role=="assistant")return true;
    return false;
}
void CopilotPanel::updateControls(){
    send_->setEnabled(configured_&&!busy_&&!scienceBusy_&&!prompt_->toPlainText().trimmed().isEmpty());stop_->setEnabled(busy_);stop_->setVisible(busy_);settings_->setEnabled(!busy_);newChat_->setEnabled(!busy_);
    context_->setEnabled(!busy_);reference_->setEnabled(!busy_);preview_->setEnabled(!busy_);attachments_->setEnabled(!busy_&&!scienceBusy_);addFile_->setEnabled(!busy_&&!scienceBusy_);contextToggle_->setEnabled(!busy_);prompt_->setEnabled(configured_&&!busy_);
    review_->setEnabled(!busy_&&!scienceBusy_&&!reviewSource_.isEmpty());review_->setVisible(!reviewSource_.isEmpty());
    QString context="Context: "+context_->currentText();if(reference_->isChecked())context+=" · Pentarnion";if(!attachmentSummary_.isEmpty())context+=" · attached";contextToggle_->setText(context);
    updateWelcome();
}
void CopilotPanel::refreshWelcomeExamples(){
    examplePrompts_.clear();QString html;const auto& prompts=welcomePrompts();
    for(int i=0;i<3;++i){examplePrompts_.append(prompts[(exampleOffset_+i)%prompts.size()]);html+="<p style=\"margin-top:3px;margin-bottom:10px;\"><a style=\"color:#176ca3;text-decoration:none;\" href=\"penta-copilot:/example/"+QString::number(i)+"\">✧ &nbsp;"+examplePrompts_.last().toHtmlEscaped()+"</a></p>";}
    welcomeExamples_->setText(html);
}
void CopilotPanel::updateWelcome(){
    const bool show=messages_.isEmpty()&&!assistantOpen_;welcome_->setVisible(show);transcript_->setVisible(!show);shuffleExamples_->setEnabled(show&&!busy_&&!scienceBusy_);welcomeExamples_->setEnabled(show&&!busy_&&!scienceBusy_);
}
void CopilotPanel::sendPrompt(){if(!send_->isEnabled())return;emit sendRequested(prompt_->toPlainText().trimmed());}
void CopilotPanel::resizeComposer(){
    const int columns=qMax(20,prompt_->viewport()->width()/qMax(1,prompt_->fontMetrics().averageCharWidth()));int lines=0;
    for(const auto& line:prompt_->toPlainText().split('\n'))lines+=qMax(1,(int(line.size())+columns-1)/columns);
    prompt_->setFixedHeight(qBound(39,prompt_->fontMetrics().lineSpacing()*qBound(1,lines,5)+15,112));
}
bool CopilotPanel::eventFilter(QObject* object,QEvent* event){
    if(object==transcript_->viewport()&&event->type()==QEvent::Resize)if(auto* reflow=findChild<QTimer*>("copilotMathReflow"))reflow->start();
    if(object==usage_&&event->type()==QEvent::Resize){usage_->setText(usage_->fontMetrics().elidedText(usage_->property("copilotCompactUsage").toString(),Qt::ElideMiddle,qMax(1,usage_->width())));}
    if(object==prompt_){if(event->type()==QEvent::KeyPress){auto* key=static_cast<QKeyEvent*>(event);if((key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&key->modifiers()==Qt::ControlModifier){sendPrompt();return true;}}
        else if(event->type()==QEvent::Resize)QTimer::singleShot(0,this,[this]{resizeComposer();});}
    return QWidget::eventFilter(object,event);
}
void CopilotPanel::trimTranscript(){
    int size=0;for(const auto& message:messages_)size+=int(message.text.size()+message.error.size()+80);
    bool trimmed=false;while(size>MaximumTranscriptCharacters&&messages_.size()>1){const auto old=messages_.takeFirst();size-=int(old.text.size()+old.error.size()+80);trimmed=true;}if(trimmed){for(auto& message:messages_)message.html.clear();static_cast<SafeTranscript*>(transcript_)->clearMathResources();}
}
void CopilotPanel::appendUser(const QString& text){
    flushAssistant();reviewSource_.clear();for(auto& message:messages_){message.valid=false;message.html.clear();}prompt_->clear();followups_.clear();messages_.append({"user",bounded(text,MaximumPromptCharacters),{},{},true,false,{},{},{}});trimTranscript();renderTranscript();updateControls();
}
void CopilotPanel::beginAssistant(){
    flushAssistant();for(auto& message:messages_){message.valid=false;message.html.clear();}reply_.clear();pendingText_.clear();reviewSource_.clear();replyTruncated_=false;currentProposalInvalidated_=false;assistantOpen_=true;busy_=true;
    messages_.append({"assistant",{},{},{},false,false,references_,{},{}});status_->setText("Receiving response…");status_->show();renderTranscript();updateControls();
}
void CopilotPanel::appendAssistantDelta(const QString& text){
    if(!assistantOpen_||replyTruncated_||text.isEmpty())return;
    const auto portion=bounded(text,MaximumReplyCharacters-int(reply_.size()));
    reply_+=portion;
    pendingText_+=portion;
    if(portion.size()!=text.size()){replyTruncated_=true;pendingText_+="\n[Response display limit reached; this answer is incomplete.]";}
    qsizetype transcriptSize=reply_.size();for(qsizetype i=0;i+1<messages_.size();++i)transcriptSize+=messages_[i].text.size()+messages_[i].error.size();
    const int interval=reply_.size()>65536||transcriptSize>131072?200:reply_.size()>16384?125:65;
    // Changing the interval only at size thresholds avoids continually
    // restarting the timer while a steady stream of small deltas arrives.
    if(presentation_->interval()!=interval)presentation_->setInterval(interval);
    if(!presentation_->isActive())presentation_->start();
}
void CopilotPanel::flushAssistant(){
    presentation_->stop();if(pendingText_.isEmpty())return;if(!messages_.isEmpty()&&messages_.last().role=="assistant"){messages_.last().text+=pendingText_;messages_.last().html.clear();}pendingText_.clear();trimTranscript();renderTranscript();
}
void CopilotPanel::finishAssistant(const QString& error){
    const bool wasOpen=assistantOpen_;flushAssistant();assistantOpen_=false;busy_=false;
    if(!wasOpen){if(!error.isEmpty())appendNotice(error);updateControls();return;}
    if(!messages_.isEmpty()&&messages_.last().role=="assistant"){
        auto& message=messages_.last();message.error=bounded(error,2048);message.complete=error.isEmpty()&&!replyTruncated_;message.valid=message.complete&&!currentProposalInvalidated_;message.followups=message.complete?followups_:QStringList();message.html.clear();
    }
    if(!error.isEmpty()){status_->setText("Request did not complete.");reviewSource_.clear();status_->show();}
    else if(replyTruncated_){status_->setText("Incomplete response; code review is unavailable.");reviewSource_.clear();status_->show();}
    else{reviewSource_=currentProposalInvalidated_?QString():extractModelSource(reply_);status_->setText(scienceBusy_?"Copilot is paused while a model operation runs.":"Ready");status_->setVisible(scienceBusy_);}
    renderTranscript();updateControls();
}
void CopilotPanel::clearConversation(bool preserveDraft){
    presentation_->stop();pendingText_.clear();reply_.clear();reviewSource_.clear();messages_.clear();codeActions_.clear();references_.clear();followups_.clear();assistantOpen_=false;replyTruncated_=false;currentProposalInvalidated_=false;busy_=false;
    auto* browser=static_cast<SafeTranscript*>(transcript_);browser->clearMathResources();browser->dropDocumentResourceCache();transcript_->clear();setUsageSummary({});if(!preserveDraft)prompt_->clear();
    status_->setText(scienceBusy_?"Copilot is paused while a model operation runs.":configured_?"Ready":"Configure a provider to start.");status_->setVisible(scienceBusy_);updateControls();
}
void CopilotPanel::renderTranscript(){
    auto* browser=static_cast<SafeTranscript*>(transcript_);auto* scrollbar=browser->verticalScrollBar();const bool follows=scrollbar->value()>=scrollbar->maximum()-4;const int scroll=scrollbar->value();
    const auto selected=browser->textCursor();const int position=selected.position(),anchor=selected.anchor();
    if(browser->property("copilotMathWidth").toInt()!=browser->availableMathWidth()){
        browser->setProperty("copilotMathWidth",browser->availableMathWidth());browser->clearMathResources();for(auto& message:messages_)message.html.clear();
    }
    browser->dropDocumentResourceCache();
    QString html="<html><head><style>body{color:#243746;}p{margin-top:5px;margin-bottom:8px;}h2,h3,h4{font-size:105%;margin-top:12px;margin-bottom:5px;}li{margin-bottom:3px;}ul,ol{margin-top:3px;margin-bottom:7px;margin-left:17px;}code,pre{font-family:'Consolas','DejaVu Sans Mono',monospace;font-size:94%;}code{color:#194f70;}pre{white-space:pre-wrap;margin:5px;}a{color:#176ca3;text-decoration:none;}blockquote{margin-left:10px;color:#526878;}th{text-align:left;}td{vertical-align:top;}</style></head><body>";codeActions_.clear();
    for(int messageIndex=0;messageIndex<messages_.size();++messageIndex){
        auto& message=messages_[messageIndex];
        if(message.role=="notice"){html+="<p style=\"color:#6b7782;font-size:90%;\">"+message.text.toHtmlEscaped()+"</p>";continue;}
        const bool assistant=message.role=="assistant";const QString role=assistant?"assistant":"user";
        html+="<p style=\"margin-top:14px;margin-bottom:5px;color:#42647c;\"><img src=\"penta-copilot:/role/"+role+"\" width=\"18\" height=\"18\" />&nbsp; <b>"+(assistant?QString("Copilot"):QString("You"))+"</b></p>";
        if(!assistant){html+="<table width=\"100%\" cellspacing=\"0\" cellpadding=\"8\"><tr><td bgcolor=\"#edf5fb\">"+message.text.toHtmlEscaped().replace('\n',"<br />")+"</td></tr></table>";continue;}
        if(message.html.isEmpty()){
        message.actions.clear();
        const auto codeCard=[&](const QString& source,const QString& language,bool fencedComplete){
            const bool penta=language=="p"||language=="pentarnion";const bool completeModel=penta&&source.trimmed().startsWith("use_algebra");const bool enabled=fencedComplete&&message.complete&&message.valid&&message.error.isEmpty();
            const int action=codeActions_.size();const CodeAction item{source+"\n",completeModel,enabled};codeActions_.append(item);message.actions.append(item);
            QString actions="<a href=\"penta-copilot:/copy/"+QString::number(action)+"\">⧉ Copy</a>";
            if(penta&&enabled&&!busy_&&!scienceBusy_)actions+=" &nbsp; <a href=\"penta-copilot:/"+QString(completeModel?"review/":"insert/")+QString::number(action)+"\">"+(completeModel?QString("◇ Review changes"):QString("↳ Insert"))+"</a>";
            return "<table width=\"100%\" border=\"0\" cellspacing=\"0\" cellpadding=\"6\" style=\"margin-top:5px;margin-bottom:8px;\"><tr><td bgcolor=\"#eaf0f5\"><span style=\"font-size:85%;color:#536c7d;\">"+(language.isEmpty()?QString("Code"):language.toHtmlEscaped())+"</span></td><td bgcolor=\"#eaf0f5\" align=\"right\"><span style=\"font-size:85%;\">"+actions+"</span></td></tr><tr><td colspan=\"2\" bgcolor=\"#f6f8fa\"><pre>"+formatCode(source,language)+"</pre></td></tr></table>";
        };
        message.html=markdown(message.text,browser,browser->font(),message.references,codeCard,assistantOpen_&&messageIndex==messages_.size()-1);
        }else codeActions_.append(message.actions);
        html+=message.html;
        if(!message.error.isEmpty())html+="<p style=\"color:#b52d2d;\">"+message.error.toHtmlEscaped()+"</p>";
        if(message.complete&&!message.references.isEmpty()){
            html+="<p style=\"font-size:90%;color:#647785;margin-top:10px;\">Sources provided</p>";
            for(const auto& reference:message.references){html+="<p style=\"margin-top:2px;margin-bottom:3px;font-size:90%;\"><a href=\"penta-copilot:/reference/"+QString::fromUtf8(QUrl::toPercentEncoding(reference.id))+"\">▤ "+reference.title.toHtmlEscaped()+"</a></p>";}
        }
        if(message.complete&&messageIndex==messages_.size()-1&&!message.followups.isEmpty()&&!busy_&&!scienceBusy_){
            html+="<p style=\"font-size:90%;color:#647785;margin-top:11px;\">Continue exploring</p>";for(int i=0;i<message.followups.size();++i)html+="<p style=\"margin-top:2px;margin-bottom:4px;font-size:90%;\"><a href=\"penta-copilot:/followup/"+QString::number(messageIndex)+"/"+QString::number(i)+"\">✧ "+message.followups[i].toHtmlEscaped()+"</a></p>";
        }
    }
    html+="</body></html>";browser->setHtml(html);updateWelcome();
    QTextCursor restored(browser->document());const int last=qMax(0,browser->document()->characterCount()-1);restored.setPosition(qBound(0,anchor,last));restored.setPosition(qBound(0,position,last),QTextCursor::KeepAnchor);browser->setTextCursor(restored);scrollbar->setValue(follows?scrollbar->maximum():scroll);
}
void CopilotPanel::handleAction(const QUrl& url){
    if(url.scheme()!="penta-copilot"||!url.host().isEmpty()||!url.query().isEmpty()||!url.fragment().isEmpty())return;
    const auto parts=url.path().split('/',Qt::SkipEmptyParts);
    if(parts.isEmpty())return;
    if(parts[0]=="example"&&parts.size()==2){
        if(busy_||scienceBusy_||!messages_.isEmpty())return;
        bool ok=false;
        const int index=parts[1].toInt(&ok);
        if(!ok||index<0||index>=examplePrompts_.size())return;
        prompt_->setPlainText(examplePrompts_[index]);auto cursor=prompt_->textCursor();cursor.movePosition(QTextCursor::End);prompt_->setTextCursor(cursor);prompt_->setFocus(Qt::OtherFocusReason);return;
    }
    if(parts[0]=="reference"&&parts.size()==2){const auto id=parts[1];for(const auto& message:messages_)for(const auto& reference:message.references)if(reference.id==id){emit referenceRequested(id);return;}return;}
    if(parts[0]=="followup"&&parts.size()==3&&!busy_&&!scienceBusy_&&configured_){bool ok1=false,ok2=false;const int message=parts[1].toInt(&ok1),index=parts[2].toInt(&ok2);if(ok1&&ok2&&message==messages_.size()-1&&message>=0&&index>=0&&index<messages_[message].followups.size())emit sendRequested(messages_[message].followups[index]);return;}
    if(parts.size()!=2)return;
    bool ok=false;
    const int index=parts[1].toInt(&ok);
    if(!ok||index<0||index>=codeActions_.size())return;
    const auto& action=codeActions_[index];
    if(parts[0]=="copy"){auto* mime=new QMimeData;mime->setText(action.source);QApplication::clipboard()->setMimeData(mime);return;}
    if(!action.enabled||busy_||scienceBusy_)return;
    if(parts[0]=="review"&&action.review)emit codeReviewRequested(action.source);
    else if(parts[0]=="insert"&&!action.review)emit codeInsertRequested(action.source);
}
QString CopilotPanel::extractModelSource(const QString& response){
    const QRegularExpression block(QStringLiteral("(?:^|\\n)```(?:p|pentarnion)[ \\t]*\\r?\\n([\\s\\S]*?)\\r?\\n```[ \\t]*(?=\\r?\\n|$)"),QRegularExpression::CaseInsensitiveOption);
    auto matches=block.globalMatch(response);if(!matches.hasNext())return {};auto source=matches.next().captured(1).trimmed();if(matches.hasNext())return {};if(!source.startsWith("use_algebra"))return {};return source+"\n";
}
}
