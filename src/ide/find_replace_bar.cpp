#include "find_replace_bar.hpp"
#include "editor_widget.hpp"
#include <QCheckBox>
#include <QCompleter>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QShowEvent>
#include <QStyle>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStringListModel>
#include <algorithm>
namespace penta {
namespace {constexpr qsizetype maximumMatches=10000;constexpr qsizetype maximumCharacters=4*1024*1024;}
FindReplaceBar::FindReplaceBar(QWidget* parent):QWidget(parent) {
    setObjectName(QStringLiteral("findReplaceBar"));setAccessibleName(tr("Find and replace in the current source"));
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(10,10,10,9);layout->setSpacing(7);
    m_sourceName=new QLabel(this);m_sourceName->setTextFormat(Qt::PlainText);m_sourceName->setObjectName(QStringLiteral("findSourceName"));m_sourceName->setStyleSheet(QStringLiteral("color:#527087;"));layout->addWidget(m_sourceName);
    auto* findRow=new QHBoxLayout;findRow->setSpacing(4);
    auto* findLabel=new QLabel(tr("Find:"),this);m_find=new QLineEdit(this);m_find->setObjectName(QStringLiteral("findText"));m_find->setAccessibleName(tr("Find text"));m_find->setClearButtonEnabled(true);findLabel->setBuddy(m_find);
    m_previous=new QToolButton(this);m_previous->setObjectName(QStringLiteral("findPrevious"));m_previous->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));m_previous->setToolTip(tr("Previous match (Shift+F3)"));m_previous->setAccessibleName(tr("Previous match"));
    m_next=new QToolButton(this);m_next->setObjectName(QStringLiteral("findNext"));m_next->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));m_next->setToolTip(tr("Next match (Enter or F3)"));m_next->setAccessibleName(tr("Next match"));
    m_count=new QLabel(this);m_count->setObjectName(QStringLiteral("findMatchCount"));m_count->setMinimumWidth(85);
    m_case=new QCheckBox(tr("Case"),this);m_case->setObjectName(QStringLiteral("findCaseSensitive"));m_case->setToolTip(tr("Match case"));
    m_word=new QCheckBox(tr("Word"),this);m_word->setObjectName(QStringLiteral("findWholeWord"));m_word->setToolTip(tr("Match whole words"));
    m_regex=new QCheckBox(tr("Regex"),this);m_regex->setObjectName(QStringLiteral("findRegularExpression"));m_regex->setToolTip(tr("Use a regular expression; replacement supports \\1..\\99 captured groups"));
    auto* close=new QToolButton(this);close->setObjectName(QStringLiteral("closeFindReplace"));close->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));close->setToolTip(tr("Close find bar (Esc)"));close->setAccessibleName(tr("Close find bar"));
    findRow->addWidget(findLabel);findRow->addWidget(m_find,1);findRow->addWidget(m_previous);findRow->addWidget(m_next);findRow->addWidget(close);
    m_replacementRow=new QWidget(this);auto* replaceRow=new QHBoxLayout(m_replacementRow);replaceRow->setContentsMargins(0,0,0,0);replaceRow->setSpacing(4);auto* replaceLabel=new QLabel(tr("Replace:"),this);m_replacement=new QLineEdit(this);m_replacement->setObjectName(QStringLiteral("replaceText"));m_replacement->setAccessibleName(tr("Replacement text"));replaceLabel->setBuddy(m_replacement);
    m_replace=new QPushButton(tr("Replace"),this);m_replace->setObjectName(QStringLiteral("replaceOne"));m_replaceAll=new QPushButton(tr("Replace All"),this);m_replaceAll->setObjectName(QStringLiteral("replaceAll"));
    m_status=new QLabel(this);m_status->setObjectName(QStringLiteral("findStatus"));m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);m_status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);m_status->setMinimumWidth(0);
    replaceRow->addWidget(replaceLabel);replaceRow->addWidget(m_replacement,1);replaceRow->addWidget(m_replace);replaceRow->addWidget(m_replaceAll);
    auto* options=new QHBoxLayout;options->addWidget(m_case);options->addWidget(m_word);options->addWidget(m_regex);options->addStretch();options->addWidget(m_count);
    layout->addLayout(findRow);layout->addWidget(m_replacementRow);layout->addLayout(options);layout->addWidget(m_status);
    setStyleSheet(QStringLiteral("QLineEdit {background:white;color:#203b4e;border:1px solid #b5c7d5;padding:4px;}QLineEdit:focus {border-color:#168a9f;}QToolButton {padding:4px;}QPushButton {padding:4px 10px;}"));
    m_refresh=new QTimer(this);m_refresh->setSingleShot(true);m_refresh->setInterval(75);connect(m_refresh,&QTimer::timeout,this,&FindReplaceBar::rebuild);
    connect(m_find,&QLineEdit::textChanged,this,[this]{m_lastZeroStart=-1;rebuild();});
    for(auto* toggle:{m_case,m_word,m_regex})connect(toggle,&QCheckBox::toggled,this,[this]{m_lastZeroStart=-1;rebuild();});
    connect(m_find,&QLineEdit::returnPressed,this,[this]{findMatch(false);});connect(m_previous,&QToolButton::clicked,this,[this]{findMatch(true);});connect(m_next,&QToolButton::clicked,this,[this]{findMatch(false);});
    connect(m_replace,&QPushButton::clicked,this,&FindReplaceBar::replaceOne);connect(m_replaceAll,&QPushButton::clicked,this,&FindReplaceBar::replaceEvery);connect(close,&QToolButton::clicked,this,&FindReplaceBar::closePanel);
    auto* escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);escape->setContext(Qt::WidgetWithChildrenShortcut);connect(escape,&QShortcut::activated,this,&FindReplaceBar::closePanel);
    m_find->installEventFilter(this);m_replacement->installEventFilter(this);hide();
}
FindReplaceBar::~FindReplaceBar(){if(m_editor)m_editor->setSearchSelections({});}
void FindReplaceBar::setEditor(EditorWidget* editor) {
    if(m_editor==editor)return;
    disconnect(m_contentsConnection);disconnect(m_cursorConnection);m_refresh->stop();
    if(m_editor){m_editor->removeEventFilter(this);m_editor->setSearchSelections({});}
    m_editor=editor;m_matches.clear();m_lastZeroStart=-1;
    if(editor){editor->installEventFilter(this);m_contentsConnection=connect(editor->document(),&QTextDocument::contentsChanged,this,[this]{m_lastZeroStart=-1;if(isVisible())m_refresh->start();});m_cursorConnection=connect(editor,&QPlainTextEdit::cursorPositionChanged,this,[this]{if(isVisible()&&!m_selecting)updateHighlights();});}
    setEnabled(editor!=nullptr);if(!editor){hide();if(closeRequested)closeRequested();}else if(isVisible())rebuild();
}
void FindReplaceBar::openFor(EditorWidget* editor,const QString& selectedText) {
    setEditor(editor);if(!editor)return;
    if(!selectedText.isEmpty()&&!selectedText.contains(QChar::ParagraphSeparator)&&!selectedText.contains(QLatin1Char('\n')))m_find->setText(selectedText);
    show();rebuild();focusSearch();
}
void FindReplaceBar::focusSearch(){m_find->setFocus(Qt::ShortcutFocusReason);m_find->selectAll();}
void FindReplaceBar::closePanel(){hide();if(m_editor)m_editor->setFocus(Qt::ShortcutFocusReason);if(closeRequested)closeRequested();}
void FindReplaceBar::setSourceName(const QString& name){m_sourceName->setText(name.isEmpty()?tr("Current source document"):tr("Current source: %1").arg(name));}
void FindReplaceBar::setReplaceVisible(bool visible){m_replacementRow->setVisible(visible);}
void FindReplaceBar::setHistories(const QStringList& search,const QStringList& replacement){
    const auto bounded=[](const QStringList& source){QStringList result;for(const auto& value:source)if(!value.isEmpty()&&value.size()<=2048&&!result.contains(value)){result.append(value);if(result.size()==32)break;}return result;};
    m_searchHistory=bounded(search);m_replacementHistory=bounded(replacement);
    for(const auto& pair:{qMakePair(m_find,m_searchHistory),qMakePair(m_replacement,m_replacementHistory)}){
        if(auto* existing=pair.first->completer()){
            if(auto* model=qobject_cast<QStringListModel*>(existing->model()))model->setStringList(pair.second);
        }else{
            auto* completer=new QCompleter(pair.second,pair.first);completer->setCaseSensitivity(Qt::CaseSensitive);pair.first->setCompleter(completer);
        }
    }
}
void FindReplaceBar::rememberSearch(bool replacement){
    auto search=m_searchHistory;auto replacements=m_replacementHistory;
    if(!m_find->text().isEmpty()){search.removeAll(m_find->text());search.prepend(m_find->text());}
    if(replacement&&!m_replacement->text().isEmpty()){replacements.removeAll(m_replacement->text());replacements.prepend(m_replacement->text());}
    setHistories(search,replacements);
}
void FindReplaceBar::hideEvent(QHideEvent* event){m_refresh->stop();if(m_editor)m_editor->setSearchSelections({});QWidget::hideEvent(event);}
void FindReplaceBar::showEvent(QShowEvent* event){QWidget::showEvent(event);rebuild();}
bool FindReplaceBar::eventFilter(QObject* watched,QEvent* event) {
    if(isVisible()&&event->type()==QEvent::KeyPress){auto* key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Escape&&key->modifiers()==Qt::NoModifier){closePanel();return true;}
        if(key->key()==Qt::Key_F3&&(key->modifiers()==Qt::NoModifier||key->modifiers()==Qt::ShiftModifier)){findMatch(key->modifiers()==Qt::ShiftModifier);return true;}
        if(watched==m_find&&(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&key->modifiers()==Qt::ShiftModifier){findMatch(true);return true;}
    }
    return QWidget::eventFilter(watched,event);
}
void FindReplaceBar::rebuild() {
    m_refresh->stop();m_matches.clear();m_overflow=false;m_valid=true;m_status->clear();
    if(m_editor&&!m_find->text().isEmpty()) {
        QString pattern=m_regex->isChecked()?m_find->text():QRegularExpression::escape(m_find->text());
        if(m_word->isChecked())pattern=QStringLiteral("(?<!\\w)(?:%1)(?!\\w)").arg(pattern);
        auto flags=QRegularExpression::UseUnicodePropertiesOption|QRegularExpression::MultilineOption;if(!m_case->isChecked())flags|=QRegularExpression::CaseInsensitiveOption;
        const QRegularExpression expression(pattern,flags);m_valid=expression.isValid();
        if(!m_valid)m_status->setText(tr("Invalid expression: %1").arg(expression.errorString()));
        else {auto matches=expression.globalMatch(m_editor->toPlainText());while(matches.hasNext()&&m_matches.size()<maximumMatches)m_matches.push_back(matches.next());m_overflow=matches.hasNext();if(m_overflow)m_status->setText(tr("More than10,000 matches; refine the search to replace all."));}
    }
    const bool active=m_editor&&m_valid&&!m_matches.isEmpty();m_previous->setEnabled(active);m_next->setEnabled(active);m_replace->setEnabled(active&&!m_editor->isReadOnly());m_replaceAll->setEnabled(active&&!m_overflow&&!m_editor->isReadOnly());updateHighlights();
}
void FindReplaceBar::updateHighlights() {
    if(!m_editor)return;
    if(!isVisible()){m_editor->setSearchSelections({});return;}
    const auto cursor=m_editor->textCursor();int current=-1;
    for(qsizetype n=0;n<m_matches.size();++n){const auto& match=m_matches[n];if((cursor.hasSelection()&&cursor.selectionStart()==match.capturedStart()&&cursor.selectionEnd()==match.capturedEnd())||(!cursor.hasSelection()&&cursor.position()>=match.capturedStart()&&cursor.position()<match.capturedEnd())){current=static_cast<int>(n);break;}if(!cursor.hasSelection()&&match.capturedLength()==0&&cursor.position()==match.capturedStart()&&m_lastZeroStart==cursor.position())current=static_cast<int>(n);}
    QList<QTextEdit::ExtraSelection> highlights;highlights.reserve(m_matches.size());
    for(qsizetype n=0;n<m_matches.size();++n){const auto& match=m_matches[n];if(!match.capturedLength())continue;QTextEdit::ExtraSelection selection;selection.cursor=QTextCursor(m_editor->document());selection.cursor.setPosition(static_cast<int>(match.capturedStart()));selection.cursor.setPosition(static_cast<int>(match.capturedEnd()),QTextCursor::KeepAnchor);selection.format.setBackground(n==current?QColor(255,174,91):QColor(255,240,145));highlights.push_back(selection);}
    m_editor->setSearchSelections(highlights);
    m_count->setText(m_matches.isEmpty()?tr("No matches"):current>=0?tr("%1 of %2%3").arg(current+1).arg(m_matches.size()).arg(m_overflow?QStringLiteral("+"):QString()):tr("%1%2 matches").arg(m_matches.size()).arg(m_overflow?QStringLiteral("+"):QString()));
}
void FindReplaceBar::selectMatch(int index) {
    if(!m_editor||index<0||index>=m_matches.size())return;
    const auto& match=m_matches[index];QTextCursor cursor(m_editor->document());cursor.setPosition(static_cast<int>(match.capturedStart()));cursor.setPosition(static_cast<int>(match.capturedEnd()),QTextCursor::KeepAnchor);m_selecting=true;m_editor->setTextCursor(cursor);m_editor->ensureCursorVisible();m_selecting=false;m_lastZeroStart=match.capturedLength()==0?static_cast<int>(match.capturedStart()):-1;updateHighlights();
}
void FindReplaceBar::findMatch(bool backwards) {
    rememberSearch();
    rebuild();if(!m_editor||m_matches.isEmpty())return;const auto cursor=m_editor->textCursor();const auto position=backwards?cursor.selectionStart():cursor.selectionEnd();int selected=-1;
    if(backwards){for(int n=static_cast<int>(m_matches.size())-1;n>=0;--n){const auto& match=m_matches[n];if(match.capturedEnd()<=position&&!(match.capturedLength()==0&&match.capturedStart()==m_lastZeroStart)){selected=n;break;}}if(selected<0)selected=static_cast<int>(m_matches.size())-1;}
    else {for(int n=0;n<m_matches.size();++n){const auto& match=m_matches[n];if(match.capturedStart()>=position&&!(match.capturedLength()==0&&match.capturedStart()==m_lastZeroStart)){selected=n;break;}}if(selected<0)selected=0;}
    selectMatch(selected);
}
QString FindReplaceBar::replacementFor(const QRegularExpressionMatch& match)const {
    const auto text=m_replacement->text();if(!m_regex->isChecked())return text;QString result;const int groups=match.regularExpression().captureCount();
    for(qsizetype n=0;n<text.size();++n){if(text[n]==QLatin1Char('\\')&&n+1<text.size()){
        if(text[n+1]==QLatin1Char('\\')){result+=QLatin1Char('\\');++n;continue;}
        if(text[n+1]>=QLatin1Char('1')&&text[n+1]<=QLatin1Char('9')){int group=text[++n].digitValue();if(n+1<text.size()&&text[n+1].isDigit()&&group*10+text[n+1].digitValue()<=groups)group=group*10+text[++n].digitValue();if(group<=groups)result+=match.captured(group);else result+=QLatin1Char('\\')+QString::number(group);continue;}
    }result+=text[n];}return result;
}
void FindReplaceBar::replaceOne() {
    rememberSearch(true);
    rebuild();if(!m_editor||m_matches.isEmpty()||m_editor->isReadOnly())return;auto cursor=m_editor->textCursor();int selected=-1;
    for(int n=0;n<m_matches.size();++n)if(cursor.selectionStart()==m_matches[n].capturedStart()&&cursor.selectionEnd()==m_matches[n].capturedEnd()){selected=n;break;}
    if(selected<0){findMatch(false);cursor=m_editor->textCursor();for(int n=0;n<m_matches.size();++n)if(cursor.selectionStart()==m_matches[n].capturedStart()&&cursor.selectionEnd()==m_matches[n].capturedEnd()){selected=n;break;}}
    if(selected<0)return;
    const QString replacement=replacementFor(m_matches[selected]);if(m_editor->document()->characterCount()-1-cursor.selectedText().size()+replacement.size()>maximumCharacters){m_status->setText(tr("Replacement exceeds the source-size limit."));return;}
    cursor.beginEditBlock();cursor.insertText(replacement);cursor.endEditBlock();m_editor->setTextCursor(cursor);m_lastZeroStart=-1;rebuild();m_status->setText(tr("Replaced one occurrence."));
}
void FindReplaceBar::replaceEvery() {
    rememberSearch(true);
    rebuild();if(!m_editor||m_matches.isEmpty()||m_overflow||m_editor->isReadOnly())return;
    QVector<QString> replacements;replacements.reserve(m_matches.size());qsizetype size=m_editor->document()->characterCount()-1;for(const auto& match:m_matches){auto replacement=replacementFor(match);size+=replacement.size()-match.capturedLength();if(size>maximumCharacters){m_status->setText(tr("Replacement exceeds the source-size limit."));return;}replacements.push_back(std::move(replacement));}
    const auto matches=m_matches;const auto original=m_editor->textCursor();QTextCursor edit(m_editor->document());edit.beginEditBlock();for(qsizetype n=matches.size();n-->0;){edit.setPosition(static_cast<int>(matches[n].capturedStart()));edit.setPosition(static_cast<int>(matches[n].capturedEnd()),QTextCursor::KeepAnchor);edit.insertText(replacements[n]);}edit.endEditBlock();m_editor->setTextCursor(original);m_lastZeroStart=-1;rebuild();m_status->setText(tr("Replaced %1 occurrence(s).").arg(matches.size()));
}
}
