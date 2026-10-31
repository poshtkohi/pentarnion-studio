// Search/presentation pattern adapted from GTG Cell Studio's offline Help.
#include "ide/assistant/offline_help.hpp"
#include "ide/assistant/help_article_browser.hpp"
#include "runtime/help_catalog.hpp"
#include <QAction>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextCursor>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <mutex>
namespace penta::assistant {
namespace {
std::mutex referenceMutex;
QList<CopilotReference> documentation;
QString normalized(QString text){text=text.toCaseFolded();text.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")),QStringLiteral(" "));return text.simplified();}
QString commandExample(const QString& name){
    if(name==QStringLiteral("display"))return QStringLiteral("display([1,100]);\npentarnion p=1+i+j;\ndisplay(\"p =\",p);\n");
    if(QStringList{QStringLiteral("sin"),QStringLiteral("cos"),QStringLiteral("exp"),QStringLiteral("sinh"),QStringLiteral("cosh")}.contains(name))return QStringLiteral("use_algebra(\"A_gamma\",gamma=1);\nreal angle=0.5;\ndisplay(sin(angle),cos(angle));\npentarnion p=0.1+0.2*i;\ndisplay(exp(p));\n");
    if(QStringList{QStringLiteral("figure"),QStringLiteral("plot2"),QStringLiteral("plot3"),QStringLiteral("title"),QStringLiteral("xlabel"),QStringLiteral("ylabel"),QStringLiteral("zlabel"),QStringLiteral("caption")}.contains(name))return QStringLiteral("use_algebra(\"A_gamma\",gamma=1);\npentarnion p=1+i+j+ell;\npentarnion q=1-i+j-ell;\nfigure(1,\"Name\",\"Pentarnion vectors\",\"NumberTitle\",\"off\");\nplot3(\"i\",\"j\",\"k\",p,q,p+q,p*q);\ntitle(\"Addition and ordered multiplication\");\nxlabel(\"$i$\");ylabel(\"$j$\");zlabel(\"$k$\");\n");
    if(QStringList{QStringLiteral("matrix"),QStringLiteral("vector"),QStringLiteral("zeros"),QStringLiteral("ones"),QStringLiteral("eye")}.contains(name))return QStringLiteral("matrix A=[1,2;3,4];\nmatrix x=[2;1];\nmatrix b=A*x;\ndisplay(\"A*x =\",b);\n");
    if(name==QStringLiteral("odesolve"))return QStringLiteral("use_algebra(\"A_gamma\",gamma=1);\node decay(t,pentarnion p;real rate){\n    p'=-rate*p;\n}\nsolution s=odesolve(decay,p=1+i,rate=0.5,\n    t0=0,t1=2,sample_step=0.02,\n    method=\"bdf\",jacobian=\"analytic\");\ndisplay(s);\nfigure(1);plot(s.t,coeff(s.p,0));\n");
    if(name==QStringLiteral("function")||name==QStringLiteral("return"))return QStringLiteral("function real square(real x){return x*x;}\ndisplay(square(3));\n");
    if(name==QStringLiteral("if")||name==QStringLiteral("else"))return QStringLiteral("real x=3;\nif(x>0){display(\"positive\");}\nelse{display(\"non-positive\");}\n");
    return {};
}
QList<CopilotReference> basics(){return {
 {"PENTA-home","Pentarnion Studio","Built-in overview",QStringLiteral("Pentarnion Studio combines .p source editing, a MATLAB-style Command Window, scientific solvers and figures. Source executes only through explicit Run or Command Window submission. F1 and Help search work offline. Copilot is optional and only sends explicitly selected context when Send is pressed.")},
 {"PENTA-language","Language Reference","language.txt",QStringLiteral("Getting started\nThe typed .p language provides explicitly ordered algebra, arrays, functions and scientific commands. Choose a command below for its current syntax and description, or read the full language sections indexed from the installed specification.\n\nExample\n```p\nuse_algebra(\"A_gamma\", gamma=1);\npentarnion p = 1 + i + j;\npentarnion q = 1 - i + ell;\ndisplay(\"p + q =\", p + q);\ndisplay(\"p*q =\", p*q);\n```\n\nNotes\nStatements in scripts terminate with semicolons. Source executes only through explicit Run or a submitted Command Window command. Parentheses preserve the multiplication grouping. The displayed examples are text; viewing Help never executes them.")},
 {"PENTA-order","Algebra and multiplication order","language.txt",QStringLiteral("Pentarnion multiplication can be nonassociative. Preserve operand order and parentheses: `(p*q)*r` and `p*(q*r)` can differ. The associator records that difference:\n\n\\[[p,q,r]=(pq)r-p(qr)\\]\n\nA reciprocal is distinct from solving an ordered left or right equation.\n\nSyntax\n```p\nuse_algebra(\"A_gamma\",gamma=1);\n```\nConsult specs/native_algebras_v0_2.txt and the equation-solve specifications for other families and capabilities.")},
 {"PENTA-command","Command Window and scripts","language.txt",QStringLiteral("Use display(value); to show values. clc; clears the transcript; clear; clears workspace variables. Run a saved .p script using run(\"file.p\") or the Run action. Source diagnostics retain line/column locations. Ctrl+C requests cancellation during a running calculation. Opening, editing and saving source do not execute it.")},
 {"PENTA-figures","Figures and subplots","language.txt",QStringLiteral("Named/numbered figures and subplots retain their data when docked, floated or tiled. Example:\nfigure(1, \"Name\", \"Vectors\", \"NumberTitle\", \"off\");\nplot3(\"i\", \"j\", \"k\", 1+i+j+k);\ntitle(\"A vector\");\nUse the indexed figure, export and layout specifications for supported argument forms.")},
 {"PENTA-ode","ODE solving and native callbacks","ODE_GUIDE.txt",QStringLiteral("ODE equations and supported numerical callbacks compile into cached native DLL/SO code. Unchanged callback structure reuses compiled code while runtime parameters may change. Jacobians are compiler-generated or selected as finite differences through documented solver options. Parentheses and algebra context remain part of the mathematical contract. Consult the indexed ODE guide for the complete declaration/odesolve syntax.")},
 {"PENTA-copilot","Copilot context and reviewed changes","Studio GUI",QStringLiteral("Copilot uses your selected provider and account. No provider request happens on startup or when attaching context. Source sharing defaults to None. Preview the outgoing context before Send. Generated .p code is a suggestion: Review, statically Validate and explicitly Apply. Validation does not run calculations or prove numerical/scientific correctness. Source changes invalidate old proposals.")}
};}
class LocalBrowser final:public QTextBrowser {
public:using QTextBrowser::QTextBrowser;
protected:QVariant loadResource(int,const QUrl&)override{return {};}
};
}
QList<CopilotReference> loadDocumentationReferences(const QString& root,const std::atomic_bool* cancel){
    QList<CopilotReference> result;const auto canonical=QFileInfo(root).canonicalFilePath();if(canonical.isEmpty())return result;
    const QDir directory(canonical);QStringList files;
    for(const auto& name:directory.entryList({QStringLiteral("*.txt")},QDir::Files|QDir::NoSymLinks,QDir::Name))
        if(name==QStringLiteral("language.txt")||name.endsWith(QStringLiteral("_GUIDE.txt")))files.append(directory.filePath(name));
    for(const auto& sub:{QStringLiteral("specs"),QStringLiteral("examples"),QStringLiteral("docs")}){
        QDirIterator iterator(directory.filePath(sub),sub==QStringLiteral("examples")?QStringList{QStringLiteral("*.p"),QStringLiteral("README.txt")}:QStringList{QStringLiteral("*.txt")},QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories);
        while(iterator.hasNext()&&files.size()<768){if(cancel&&cancel->load())return {};files.append(iterator.next());}
    }
    files.sort();qsizetype bytes=0;const QRegularExpression section(QStringLiteral("(?m)^L([0-9]{2,3})[.]\\s+([^\\r\\n]+)"));
    for(const auto& path:files){
        if(cancel&&cancel->load())return {};
        const QFileInfo info(path);if(info.size()<1||info.size()>1024*1024||bytes+info.size()>16*1024*1024)continue;
        const auto real=info.canonicalFilePath();if(!real.startsWith(canonical+QLatin1Char('/'))&&!real.startsWith(canonical+QLatin1Char('\\')))continue;
        QFile file(path);if(!file.open(QIODevice::ReadOnly))continue;const auto data=file.read(1024*1024+1);if(data.size()>1024*1024||data.contains('\0'))continue;bytes+=data.size();
        const auto text=QString::fromUtf8(data);const auto relative=directory.relativeFilePath(path);QList<QRegularExpressionMatch> matches;
        if(info.fileName()==QStringLiteral("language.txt")){auto scan=section.globalMatch(text);while(scan.hasNext())matches.append(scan.next());}
        const auto add=[&](QString title,QString body,int line,QString suffix){
            if(result.size()>=1024)return;
            const bool excerpt=body.size()>48*1024;if(excerpt)body=body.left(48*1024)+QStringLiteral("\n[Documentation excerpt; open the installed source text for the complete section.]");
            const auto key=relative+suffix;const auto id=QStringLiteral("PENTA-doc-")+QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(),QCryptographicHash::Sha256).toHex().left(24));
            result.append({id,std::move(title),relative+QLatin1Char(':')+QString::number(line),std::move(body)});
        };
        if(matches.isEmpty()){
            auto title=text.section(QLatin1Char('\n'),0,0).trimmed();if(title.isEmpty()||title.size()>160)title=relative;
            if(info.suffix()==QStringLiteral("p"))title=QStringLiteral("Example: ")+relative;
            add(title,text,1,{});
        }else for(int index=0;index<matches.size();++index){
            const auto start=matches[index].capturedStart();const auto end=index+1<matches.size()?matches[index+1].capturedStart():text.size();
            add(QStringLiteral("L")+matches[index].captured(1)+QStringLiteral(" · ")+matches[index].captured(2),text.mid(start,end-start),text.left(start).count(QLatin1Char('\n'))+1,QString::number(index));
        }
    }
    return result;
}
void installDocumentationReferences(QList<CopilotReference> references){std::lock_guard lock(referenceMutex);documentation=std::move(references);}
QList<CopilotReference> HelpDialog::allReferences(){
    static const QList<CopilotReference> builtin=[](){
        auto result=basics();QSet<QString> aliases;
        for(const auto& entry:runtime::help::entries()){
            const auto names=QString::fromUtf8(entry.names.data(),static_cast<qsizetype>(entry.names.size())).split('|',Qt::SkipEmptyParts);
            const auto summary=QString::fromUtf8(entry.summary.data(),static_cast<qsizetype>(entry.summary.size()));
            const auto usage=QString::fromUtf8(entry.usage.data(),static_cast<qsizetype>(entry.usage.size()));
            const auto details=QString::fromUtf8(entry.details.data(),static_cast<qsizetype>(entry.details.size()));
            for(const auto& name:names){
                if(aliases.contains(name))continue;
                aliases.insert(name);
                auto text=summary+QStringLiteral("\n\nSyntax\n```p\n")+usage+QStringLiteral("\n```\n\nDescription\n")+details;
                const auto example=commandExample(name);if(!example.isEmpty())text+=QStringLiteral("\n\nExample\n```p\n")+example+QStringLiteral("```\n");
                text+=QStringLiteral("\n\nSee also\nRelated command names: ")+names.join(QStringLiteral(", "))+QStringLiteral(". Search the Language Reference for the complete grammar and typed argument rules.");
                result.append({QStringLiteral("PENTA-command-")+name,name,QStringLiteral("Current command catalogue · ")+names.join(QStringLiteral(", ")),text});
            }
        }return result;
    }();
    auto result=builtin;std::lock_guard lock(referenceMutex);result.append(documentation);return result;
}
QList<HelpSearchResult> HelpDialog::searchReferences(const QString& raw,int maximum){
    const auto query=normalized(raw.left(256));auto tokens=query.split(QLatin1Char(' '),Qt::SkipEmptyParts);
    const QSet<QString> stop={QStringLiteral("a"),QStringLiteral("an"),QStringLiteral("the"),QStringLiteral("and"),QStringLiteral("or"),QStringLiteral("of"),QStringLiteral("for"),QStringLiteral("to"),QStringLiteral("in"),QStringLiteral("is"),QStringLiteral("are"),QStringLiteral("can"),QStringLiteral("how"),QStringLiteral("what"),QStringLiteral("does"),QStringLiteral("do"),QStringLiteral("with"),QStringLiteral("please"),QStringLiteral("me"),QStringLiteral("about")};
    for(int index=tokens.size()-1;index>=0;--index)if(stop.contains(tokens[index]))tokens.removeAt(index);
    QList<HelpSearchResult> result;
    for(const auto& ref:allReferences()){
        const auto title=normalized(ref.title),body=normalized(ref.text),location=normalized(ref.location);int score=0;bool matched=true;
        for(const auto& token:tokens){if(title.contains(token))score+=40;else if(location.contains(token))score+=15;else if(body.contains(token))score+=4;else {matched=false;break;}}
        if(!matched)continue;
        if(!query.isEmpty()&&title==query)score+=10000;
        else if(!query.isEmpty()&&title.contains(query))score+=50;
        int position=0;if(!tokens.isEmpty()){const int at=ref.text.indexOf(tokens.front(),0,Qt::CaseInsensitive);if(at>40)position=at-40;}
        auto snippet=ref.text.mid(position,220).simplified();if(position)snippet.prepend(QStringLiteral("…"));result.append({ref,snippet,score});
    }
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.score!=b.score?a.score>b.score:a.reference.title.localeAwareCompare(b.reference.title)<0;});
    if(result.size()>std::clamp(maximum,1,1024))result=result.mid(0,std::clamp(maximum,1,1024));
    return result;
}
HelpDialog::HelpDialog(QWidget* parent):QDialog(parent){
    auto reading=QFontDatabase::systemFont(QFontDatabase::GeneralFont);const auto families=QFontDatabase::families();
    for(const auto* family:{"Segoe UI","Noto Sans","DejaVu Sans","Arial"})if(families.contains(QString::fromLatin1(family),Qt::CaseInsensitive)){reading.setFamily(QString::fromLatin1(family));break;}
    reading.setStyleHint(QFont::SansSerif);reading.setStretch(QFont::Unstretched);reading.setWeight(QFont::Normal);reading.setPointSizeF(10);setFont(reading);
    setObjectName(QStringLiteral("pentarnionHelpDialog"));setWindowTitle(QStringLiteral("Pentarnion Help"));setAttribute(Qt::WA_DeleteOnClose,false);setWindowFlag(Qt::WindowContextHelpButtonHint,false);setModal(false);resize(1080,710);setMinimumSize(660,420);
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(8,8,8,8);layout->setSpacing(6);
    auto* toolbar=new QToolBar(this);toolbar->setObjectName(QStringLiteral("helpNavigation"));toolbar->setIconSize({16,16});toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);toolbar->setMovable(false);toolbar->setFloatable(false);
    back_=toolbar->addAction(style()->standardIcon(QStyle::SP_ArrowBack),tr("Back"),this,[this]{navigate(-1);});back_->setObjectName(QStringLiteral("helpBack"));
    forward_=toolbar->addAction(style()->standardIcon(QStyle::SP_ArrowForward),tr("Forward"),this,[this]{navigate(1);});forward_->setObjectName(QStringLiteral("helpForward"));
    toolbar->addAction(style()->standardIcon(QStyle::SP_DirHomeIcon),tr("Help home"),this,[this]{openTopic(QStringLiteral("PENTA-home"));});
    toolbar->addSeparator();query_=new QLineEdit(this);query_->setObjectName(QStringLiteral("helpTopicSearch"));query_->setPlaceholderText(tr("Search commands, language, examples and guides"));query_->setAccessibleName(tr("Search local documentation"));query_->setMaxLength(256);query_->setClearButtonEnabled(true);query_->setMinimumWidth(250);query_->setStyleSheet(QStringLiteral("QLineEdit {background:white;color:#203b4e;border:1px solid #b5c7d5;padding:4px;}QLineEdit:focus {border-color:#168a9f;}"));toolbar->addWidget(query_);
    category_=new QComboBox(toolbar);category_->setObjectName(QStringLiteral("helpCategory"));category_->addItems({tr("All topics"),tr("Command reference"),tr("Language sections"),tr("Examples"),tr("Guides and contracts")});toolbar->addWidget(category_);
    auto* language=toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView),tr("Language Reference"),this,[this]{openLanguageReference();});language->setObjectName(QStringLiteral("helpLanguageContents"));
    auto* details=toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogInfoView),tr("Reference details"));details->setCheckable(true);details->setObjectName(QStringLiteral("helpReferenceDetails"));layout->addWidget(toolbar);
    summary_=new QLabel(this);summary_->setObjectName(QStringLiteral("helpSummary"));summary_->setStyleSheet(QStringLiteral("color:#60778a;"));layout->addWidget(summary_);
    auto* split=new QSplitter(Qt::Horizontal,this);split->setObjectName(QStringLiteral("helpSplitter"));split->setChildrenCollapsible(false);
    topics_=new QListWidget(split);topics_->setObjectName(QStringLiteral("helpTopicList"));topics_->setWordWrap(false);topics_->setMinimumWidth(215);topics_->setSpacing(0);topics_->setUniformItemSizes(true);topics_->setTextElideMode(Qt::ElideRight);topics_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);topics_->setStyleSheet(QStringLiteral("QListWidget {border:1px solid #d5dee6;background:white;}QListWidget::item {padding:5px 7px;border-bottom:1px solid #edf1f4;}QListWidget::item:selected {background:#e5f1fb;color:#1d547b;}"));
    article_=new HelpArticleBrowser(split);article_->setObjectName(QStringLiteral("helpArticle"));article_->setFont(reading);article_->document()->setDefaultFont(reading);article_->setStyleSheet(QStringLiteral("QTextBrowser {background:white;border:1px solid #d5dee6;}"));split->setSizes({315,745});layout->addWidget(split,1);
    provenance_=new QLabel(this);provenance_->setObjectName(QStringLiteral("helpProvenance"));provenance_->setTextFormat(Qt::PlainText);provenance_->setWordWrap(true);provenance_->setTextInteractionFlags(Qt::TextSelectableByMouse);provenance_->setStyleSheet(QStringLiteral("color:#60778a;font-size:11px;"));provenance_->hide();layout->addWidget(provenance_);
    searchTimer_=new QTimer(this);searchTimer_->setSingleShot(true);searchTimer_->setInterval(140);
    const auto searchNow=[this](bool remember){populate(query_->text());if(topics_->count())display(topics_->item(0)->data(Qt::UserRole).toString(),remember);};
    connect(query_,&QLineEdit::textEdited,searchTimer_,qOverload<>(&QTimer::start));connect(searchTimer_,&QTimer::timeout,this,[searchNow]{searchNow(false);});connect(query_,&QLineEdit::returnPressed,this,[this,searchNow]{searchTimer_->stop();searchNow(true);});connect(category_,&QComboBox::currentIndexChanged,this,[searchNow]{searchNow(false);});
    connect(topics_,&QListWidget::currentItemChanged,this,[this](QListWidgetItem* current){if(current)display(current->data(Qt::UserRole).toString());});
    connect(details,&QAction::toggled,provenance_,&QWidget::setVisible);
    connect(article_,&QTextBrowser::anchorClicked,this,[this](const QUrl& url){if(url.scheme()==QStringLiteral("penta-help")&&url.host()==QStringLiteral("topic"))display(url.path().mid(1));});
    populate({});display(QStringLiteral("PENTA-home"));
}
void HelpDialog::populate(const QString& query){
    const QSignalBlocker guard(topics_);topics_->clear();const int category=category_->currentIndex();
    for(const auto& match:searchReferences(query,1024)){
        const auto& ref=match.reference;
        if(category==1&&!ref.id.startsWith(QStringLiteral("PENTA-command-")))continue;
        if(category==2&&!ref.location.startsWith(QStringLiteral("language.txt:"))&&ref.id!=QStringLiteral("PENTA-language"))continue;
        if(category==3&&!ref.title.startsWith(QStringLiteral("Example: ")))continue;
        if(category==4&&(ref.id.startsWith(QStringLiteral("PENTA-command-"))||ref.title.startsWith(QStringLiteral("Example: "))||ref.location.startsWith(QStringLiteral("language.txt:"))))continue;
        const auto title=ref.title.simplified().left(240),snippet=match.snippet.simplified().left(180);
        auto* item=new QListWidgetItem(title+QLatin1Char('\n')+snippet,topics_);item->setData(Qt::UserRole,ref.id);item->setToolTip(title+QLatin1Char('\n')+snippet);
        // The standard delegate elides each plain-text line. Fixed two-line
        // rows keep long specification/example titles from stretching the list.
        item->setSizeHint(QSize(120,topics_->fontMetrics().lineSpacing()*2+14));
        if(topics_->count()>=512)break;
    }
    summary_->setText(query.trimmed().isEmpty()?tr("%1 local topics · Available offline").arg(topics_->count()):tr("%1 topics for “%2” · Available offline").arg(topics_->count()).arg(query));
    if(!topics_->count()){
        article_->showReference({QStringLiteral("PENTA-no-matches"),tr("No matching help topics"),tr("Local Help"),tr("Try a command such as `odesolve`, `plot3`, `sin` or `matrix`. Select All topics to search the complete catalogue. Local Help does not send a Copilot request.")},allReferences());
        currentTopic_.clear();
    }
}
void HelpDialog::search(const QString& query){searchTimer_->stop();{QSignalBlocker guard(query_);query_->setText(query.left(256));}if(category_->currentIndex()!=0){QSignalBlocker guard(category_);category_->setCurrentIndex(0);}populate(query_->text());if(query.trimmed().isEmpty())display(QStringLiteral("PENTA-home"));else if(topics_->count())display(topics_->item(0)->data(Qt::UserRole).toString());show();raise();activateWindow();}
void HelpDialog::openTopic(const QString& id){display(id);show();raise();activateWindow();}
void HelpDialog::refreshIndex(){const auto current=currentTopic_.isEmpty()?QStringLiteral("PENTA-home"):currentTopic_;populate(query_->text());display(current,false);}
void HelpDialog::display(const QString& id,bool remember){
    const auto catalogue=allReferences();CopilotReference found;for(const auto& ref:catalogue)if(ref.id==id){found=ref;break;}if(found.id.isEmpty())return;
    currentTopic_=id;article_->showReference(found,catalogue);provenance_->setText(tr("Local Pentarnion reference · %1\nSource: %2").arg(id,found.location));
    {QSignalBlocker guard(topics_);for(int i=0;i<topics_->count();++i)if(topics_->item(i)->data(Qt::UserRole).toString()==id){topics_->setCurrentRow(i);break;}}
    if(remember&&(historyIndex_<0||history_.value(historyIndex_)!=id)){while(history_.size()>historyIndex_+1)history_.removeLast();history_.append(id);if(history_.size()>128)history_.removeFirst();historyIndex_=history_.size()-1;}
    back_->setEnabled(historyIndex_>0);forward_->setEnabled(historyIndex_>=0&&historyIndex_+1<history_.size());
}
void HelpDialog::navigate(int delta){const int next=historyIndex_+delta;if(next<0||next>=history_.size())return;historyIndex_=next;display(history_[next],false);}
}
