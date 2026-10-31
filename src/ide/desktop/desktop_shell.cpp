#include "ide/desktop/desktop_shell.hpp"
#include "ide/desktop/collapsible_dock.hpp"
#include "ide/desktop/pane_hover.hpp"
#include "ide/desktop/studio_workspace_layout.hpp"
#include "ide/desktop/studio_icons.hpp"
#include "ide/desktop/toolstrip.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QScopedValueRollback>
#include <QSettings>
#include <QTabBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>

namespace penta::desktop {
DesktopShell::DesktopShell(QMainWindow* window,QSettings* settings)
    : QObject(window),window_(window),settings_(settings) {
    Q_ASSERT(window);
    setObjectName(QStringLiteral("desktopShell"));
    classicMenuBar_=window->menuBar();
    header_=new QWidget(window);header_->setObjectName(QStringLiteral("studioInterfaceHeader"));
    auto* outer=new QVBoxLayout(header_);outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
    classicHeader_=new QWidget(header_);classicHeader_->setObjectName(QStringLiteral("classicInterfaceHeader"));
    classicRow_=new QHBoxLayout(classicHeader_);classicRow_->setContentsMargins(0,0,5,0);classicRow_->setSpacing(6);
    // Reparent before changing QMainWindow's menu slot, retaining every action.
    classicMenuBar_->setParent(classicHeader_);classicRow_->addWidget(classicMenuBar_,1);
    outer->addWidget(classicHeader_);
    toolstrip_=new Toolstrip(header_);toolstripLifetime_=toolstrip_;outer->addWidget(toolstrip_);toolstrip_->hide();
    window->setMenuWidget(header_);
    auto* modes=new QActionGroup(this);modes->setExclusive(true);
    classicAction_=new QAction(tr("Classic"),this);classicAction_->setObjectName(QStringLiteral("interfaceClassic"));
    ribbonAction_=new QAction(tr("Ribbon"),this);ribbonAction_->setObjectName(QStringLiteral("interfaceRibbon"));
    for(auto* action:{classicAction_,ribbonAction_}){action->setCheckable(true);modes->addAction(action);}
    classicAction_->setChecked(true);
    classicAction_->setIcon(studioCommandIcon(classicAction_->text(),classicAction_->objectName()));
    ribbonAction_->setIcon(studioCommandIcon(ribbonAction_->text(),ribbonAction_->objectName()));
    connect(classicAction_,&QAction::triggered,this,[this]{setRibbonMode(false);});
    connect(ribbonAction_,&QAction::triggered,this,[this]{setRibbonMode(true);});
    defaultLayoutAction_=new QAction(tr("Default View"),this);
    defaultLayoutAction_->setObjectName(QStringLiteral("viewDefaultLayout"));
    defaultLayoutAction_->setIcon(studioCommandIcon(defaultLayoutAction_->text(),defaultLayoutAction_->objectName()));
    connect(defaultLayoutAction_,&QAction::triggered,this,[this]{restoreDefaultLayout();});
    toolstrip_->displayModeChanged=[this](Toolstrip::RibbonDisplay){savePreferences();};
    toolstrip_->currentPageChanged=[this](int){savePreferences();};
    toolstrip_->toggleRibbonAction()->setEnabled(false);
    installStudioPaneHover(window);
}
DesktopShell::~DesktopShell() {
    // The menu header is owned by QMainWindow and can outlive this controller.
    // Its plain callbacks must not retain a destroyed shell during teardown.
    if(toolstripLifetime_){toolstrip_->displayModeChanged={};toolstrip_->currentPageChanged={};toolstrip_->pinnedChanged={};}
}

int DesktopShell::addPage(const QString& title) {
    if(auto it=pages_.constFind(title);it!=pages_.cend())return *it;
    const int page=toolstrip_->addPage(title);pages_.insert(title,page);return page;
}
QToolButton* DesktopShell::addCommand(int page,const QString& group,QAction* action,bool prominent) {
    if(!action||!window_)return nullptr;
    // Register the same action with the window so shortcuts survive a hidden
    // classic menu. QAction ownership and its single command callback stay put.
    if(!window_->actions().contains(action))window_->addAction(action);
    return toolstrip_->addAction(page,group,action,prominent);
}
QToolButton* DesktopShell::addCommand(const QString& page,const QString& group,QAction* action,bool prominent) {
    return addCommand(addPage(page),group,action,prominent);
}
void DesktopShell::attachViewMenu(QMenu* view) {
    if(!view)return;
    auto* modes=view->addMenu(tr("Interface"));modes->setObjectName(QStringLiteral("interfaceMenu"));
    modes->addAction(classicAction_);modes->addAction(ribbonAction_);modes->addSeparator();
    modes->addAction(toolstrip_->toggleRibbonAction());view->addAction(defaultLayoutAction_);
    if(window_)for(auto* action:{classicAction_,ribbonAction_,defaultLayoutAction_,toolstrip_->toggleRibbonAction()})
        if(!window_->actions().contains(action))window_->addAction(action);
}
void DesktopShell::registerClassicToolbar(QToolBar* toolbar) {
    if(!toolbar)return;
    for(const auto& item:toolbars_)if(item.widget==toolbar)return;
    toolbars_.push_back({toolbar,!toolbar->isHidden()});
    connect(toolbar,&QToolBar::visibilityChanged,this,[this,toolbar](bool){
        if(restoring_||ribbonMode_)return;
        for(auto& item:toolbars_)if(item.widget==toolbar)item.classicVisible=!toolbar->isHidden();
        savePreferences();
    });
}
CollapsibleDockController* DesktopShell::registerDock(QDockWidget* dock,const QString& key,bool autoHide,bool reflow,QMainWindow* host) {
    if(!window_||!dock)return nullptr;
    for(const auto& item:docks_)if(item.controller&&item.controller->dock()==dock)return item.controller;
    auto* controller=new CollapsibleDockController(host?host:window_.data(),dock,key);
    controller->setReflowDrawer(reflow);controller->setAutoHide(autoHide);
    docks_.push_back({controller,{}});return controller;
}
void DesktopShell::setRibbonMode(bool enabled,bool persist) {
    QPointer<QWidget> focus=QApplication::focusWidget();
    {
        QScopedValueRollback<bool> guard(restoring_,true);
        toolstrip_->closePopup();ribbonMode_=enabled;
        classicHeader_->setVisible(!enabled);classicMenuBar_->setVisible(!enabled);toolstrip_->setVisible(enabled);
        for(auto& item:toolbars_)if(item.widget){
            item.widget->setVisible(!enabled&&item.classicVisible);
            item.widget->toggleViewAction()->setEnabled(!enabled);
        }
        classicAction_->setChecked(!enabled);ribbonAction_->setChecked(enabled);
        toolstrip_->toggleRibbonAction()->setEnabled(enabled);header_->updateGeometry();
    }
    if(focus&&focus->isVisible())focus->setFocus(Qt::OtherFocusReason);
    if(persist)savePreferences();
}
void DesktopShell::restorePreferences(bool restoreDockState) {
    restoring_=true;
    if(settings_){
        for(auto& item:toolbars_)if(item.widget&&!item.widget->objectName().isEmpty())
            item.classicVisible=settings_->value(QStringLiteral("interface/toolbar/")+item.widget->objectName(),!item.widget->isHidden()).toBool();
        const int display=settings_->value(QStringLiteral("interface/ribbonDisplay"),0).toInt();
        toolstrip_->setDisplayMode(display==0?Toolstrip::RibbonDisplay::Expanded:Toolstrip::RibbonDisplay::TabsOnly);
        const QString name=settings_->value(QStringLiteral("interface/pageName")).toString();
        int page=settings_->value(QStringLiteral("interface/page"),0).toInt();
        if(!name.isEmpty())for(int i=0;i<toolstrip_->tabBar()->count();++i)if(toolstrip_->tabBar()->tabText(i)==name){page=i;break;}
        toolstrip_->setCurrentPage(std::clamp(page,0,std::max(0,toolstrip_->tabBar()->count()-1)));
        const auto mode=settings_->value(QStringLiteral("interface/mode"),QStringLiteral("classic")).toString();
        setRibbonMode(mode==QStringLiteral("ribbon")||mode==QStringLiteral("toolstrip"),false);
        // QMainWindow restores rail widgets and native dock placement first.
        // Its opaque state does not restore our controller's collapsed/drawer
        // flags, which must match the visible rail before the user clicks it.
        if(restoreDockState)for(auto& item:docks_)if(item.controller&&!item.controller->objectName().isEmpty()){
            const auto key=QStringLiteral("interface/docks/")+item.controller->objectName();
            if(settings_->contains(key))item.controller->restoreState(settings_->value(key).toByteArray());
        }
    } else setRibbonMode(ribbonMode_,false);
    // Preferences are now loaded; subsequent explicit interactions can persist.
    restoring_=false;
}
void DesktopShell::savePreferences() {
    if(restoring_||!settings_)return;
    settings_->setValue(QStringLiteral("interface/mode"),ribbonMode_?QStringLiteral("ribbon"):QStringLiteral("classic"));
    settings_->setValue(QStringLiteral("interface/ribbonDisplay"),static_cast<int>(toolstrip_->displayMode()));
    settings_->setValue(QStringLiteral("interface/page"),toolstrip_->currentPage());
    settings_->setValue(QStringLiteral("interface/pageName"),toolstrip_->tabBar()->tabText(toolstrip_->currentPage()));
    for(const auto& item:toolbars_)if(item.widget&&!item.widget->objectName().isEmpty())
        settings_->setValue(QStringLiteral("interface/toolbar/")+item.widget->objectName(),item.classicVisible);
    for(const auto& item:docks_)if(item.controller&&!item.controller->objectName().isEmpty())
        settings_->setValue(QStringLiteral("interface/docks/")+item.controller->objectName(),item.controller->saveState());
}
void DesktopShell::captureDefaultLayout() {
    if(!window_)return;
    defaultLayout_=window_->saveState();
    for(auto& item:docks_)if(item.controller)item.defaultState=item.controller->saveState();
}
bool DesktopShell::restoreDefaultLayout() {
    if(!window_||defaultLayout_.isEmpty())return false;
    QScopedValueRollback<bool> guard(restoring_,true);
    toolstrip_->closePopup();
    auto* workspace=window_->findChild<StudioWorkspaceLayout*>(QStringLiteral("studioWorkspaceLayout"),Qt::FindDirectChildrenOnly);
    const bool result=workspace?workspace->restoreOuterState(defaultLayout_,0):window_->restoreState(defaultLayout_);
    for(auto& item:docks_)if(item.controller&&!item.defaultState.isEmpty())item.controller->restoreState(item.defaultState);
    // Interface presentation, current documents and data remain the user's.
    setRibbonMode(ribbonMode_,false);return result;
}
void DesktopShell::setClassicHeaderAccessory(QWidget* widget) {
    if(classicAccessory_==widget)return;
    if(classicAccessory_){classicRow_->removeWidget(classicAccessory_);classicAccessory_->hide();}
    classicAccessory_=widget;
    if(widget){widget->setParent(classicHeader_);classicRow_->addWidget(widget);widget->show();}
}
void DesktopShell::setRibbonHeaderAccessory(QWidget* widget){toolstrip_->setHeaderAccessory(widget);}
}
