#include "find_replace_dialog.hpp"
#include "find_replace_bar.hpp"
#include "editor_widget.hpp"
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QHideEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace penta {
FindReplaceDialog::FindReplaceDialog(QWidget* parent,QSettings* settings):QDialog(parent,Qt::Tool),m_settings(settings){
    setObjectName(QStringLiteral("findReplaceDialog"));setWindowTitle(tr("Find and Replace"));
    setModal(false);setWindowModality(Qt::NonModal);setAttribute(Qt::WA_DeleteOnClose,false);
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(2,2,2,2);
    m_bar=new FindReplaceBar(this);layout->addWidget(m_bar);resize(570,210);
    if(m_settings){
        m_bar->setHistories(m_settings->value(QStringLiteral("editor/findHistory")).toStringList(),m_settings->value(QStringLiteral("editor/replaceHistory")).toStringList());
        m_geometry=m_settings->value(QStringLiteral("editor/findDialogGeometry")).toByteArray();
        if(m_geometry.size()<=4096&&!m_geometry.isEmpty())restoreGeometry(m_geometry);
    }
    m_bar->closeRequested=[this]{savePreferences();m_closing=true;QDialog::reject();};
}
FindReplaceDialog::~FindReplaceDialog(){savePreferences();m_bar->closeRequested={};}
void FindReplaceDialog::showFind(EditorWidget* editor,const QString& sourceName,bool replace){
    if(!editor)return;
    const bool first=!m_shown;m_shown=true;m_closing=false;m_focusSearchPending=true;
    m_bar->setSourceName(sourceName);m_bar->setReplaceVisible(replace);show();
    m_bar->openFor(editor,editor->textCursor().selectedText());
    if(first)QTimer::singleShot(0,this,[this]{
        if(!isVisible())return;
        auto* display=screen();if(!display)display=QGuiApplication::primaryScreen();
        if(display){const auto available=display->availableGeometry();const auto size=frameGeometry().size();const auto current=pos();move(std::clamp(current.x(),available.left(),std::max(available.left(),available.right()-size.width()+1)),std::clamp(current.y(),available.top(),std::max(available.top(),available.bottom()-size.height()+1)));}
        rememberGeometry();
    });
    raise();activateWindow();m_bar->focusSearch();
    if(isActiveWindow())m_focusSearchPending=false;
}
bool FindReplaceDialog::event(QEvent* event){
    const bool result=QDialog::event(event);
    // A tool window's activation is asynchronous on native and offscreen Qt.
    // Restore the requested query focus after activation, once per Ctrl+F;
    // ordinary activation must not pull focus from the replacement controls.
    if(event->type()==QEvent::WindowActivate&&m_focusSearchPending&&isVisible()){
        m_focusSearchPending=false;m_bar->focusSearch();
    }
    return result;
}
void FindReplaceDialog::followEditor(EditorWidget* editor,const QString& sourceName){m_bar->setSourceName(sourceName);m_bar->setEditor(editor);}
void FindReplaceDialog::reject(){m_bar->closePanel();}
void FindReplaceDialog::rememberGeometry(){if(m_shown&&isVisible()&&!m_closing)m_geometry=saveGeometry();}
void FindReplaceDialog::moveEvent(QMoveEvent* event){QDialog::moveEvent(event);rememberGeometry();}
void FindReplaceDialog::resizeEvent(QResizeEvent* event){QDialog::resizeEvent(event);rememberGeometry();}
void FindReplaceDialog::hideEvent(QHideEvent* event){m_closing=true;m_focusSearchPending=false;QDialog::hideEvent(event);}
void FindReplaceDialog::savePreferences(){
    if(!m_settings||!m_shown)return;
    rememberGeometry();if(!m_geometry.isEmpty())m_settings->setValue(QStringLiteral("editor/findDialogGeometry"),m_geometry);
    m_settings->setValue(QStringLiteral("editor/findHistory"),m_bar->searchHistory());m_settings->setValue(QStringLiteral("editor/replaceHistory"),m_bar->replacementHistory());
}
}
