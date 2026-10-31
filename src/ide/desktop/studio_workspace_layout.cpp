#include "ide/desktop/studio_workspace_layout.hpp"
#include <QApplication>
#include <QDockWidget>
#include <QSet>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QToolBar>
#include <memory>
#include <vector>

namespace penta::desktop {
namespace {
constexpr qsizetype MaximumStateBytes=1024*1024;
Qt::DockWidgetArea defaultArea(QDockWidget* dock){
    if(dock->objectName()==QStringLiteral("resultsDock")||dock->objectName()==QStringLiteral("problemsDock"))return Qt::BottomDockWidgetArea;
    if(dock->objectName()==QStringLiteral("figuresDock"))return Qt::RightDockWidgetArea;
    return Qt::LeftDockWidgetArea;
}
QMainWindow* dockOwner(QDockWidget* dock){
    for(auto* parent=dock?dock->parentWidget():nullptr;parent;parent=parent->parentWidget())
        if(auto* host=qobject_cast<QMainWindow*>(parent))return host;
    return nullptr;
}
}
StudioWorkspaceLayout::StudioWorkspaceLayout(QMainWindow* outer,QWidget* source,
        const QList<QDockWidget*>& workingDocks,QDockWidget* copilot)
    :QObject(outer),outer_(outer),source_(source),copilot_(copilot){
    setObjectName(QStringLiteral("studioWorkspaceLayout"));
    // Refuse a mismatched caller before taking ownership of any live widget.
    if(!outer||!source||outer->centralWidget()!=source)return;
    QSet<QDockWidget*> seen;
    QList<QPair<QDockWidget*,QDockWidget*>> tabPairs;
    for(auto* dock:workingDocks){
        if(!dock||dock==copilot||seen.contains(dock))continue;
        seen.insert(dock);
        const auto area=outer->dockWidgetArea(dock);
        panes_.append({dock,area==Qt::NoDockWidgetArea?defaultArea(dock):area});
    }
    // Migrate each tab group once. Replaying reciprocal pairs can remove and
    // reinsert the anchor of a group that has already been rebuilt.
    QSet<QDockWidget*> grouped;
    for(const auto& pane:panes_){
        if(grouped.contains(pane.dock))continue;
        grouped.insert(pane.dock);
        for(auto* sibling:outer->tabifiedDockWidgets(pane.dock))
            if(seen.contains(sibling)&&!grouped.contains(sibling)){
                grouped.insert(sibling);tabPairs.append({pane.dock,sibling});
            }
    }
    QPointer<QWidget> focus=QApplication::focusWidget();
    const bool showSource=!source->isHidden()||!source->testAttribute(Qt::WA_WState_ExplicitShowHide);
    working_=new QMainWindow(outer,Qt::Widget);
    // QMainWindow gives itself Window semantics during construction. Reset the
    // type afterwards: this host must participate in the outer central layout
    // and become visible with its parent, never own a second top-level surface.
    working_->setWindowFlags(Qt::Widget);
    working_->setObjectName(QStringLiteral("studioWorkingHost"));
    working_->setProperty("pentaWorkspaceHost",true);working_->setMinimumSize(0,0);
    working_->setDockOptions(QMainWindow::AllowNestedDocks|QMainWindow::AllowTabbedDocks);
    working_->setDockNestingEnabled(true);working_->setDocumentMode(true);
    working_->setTabPosition(Qt::AllDockWidgetAreas,QTabWidget::North);
    working_->setStyleSheet(QStringLiteral("QMainWindow#studioWorkingHost::separator {width:4px;height:4px;background:#e8edf2;}"));
    // takeCentralWidget is essential: setCentralWidget alone schedules the old
    // central widget for deletion, which would destroy the source registry.
    outer->takeCentralWidget();working_->setCentralWidget(source);
    outer->setCentralWidget(working_);
    source->setVisible(showSource);working_->show();
    for(const auto& pane:panes_)placeWorkingPane(pane.dock,pane.defaultArea);
    // Rebuild each group with its members present, then restore their original
    // visibility once. Hidden tabs must retain their placement when revealed.
    QList<QPair<QDockWidget*,bool>> tabVisibility;
    QSet<QDockWidget*> tabParticipants;
    std::vector<std::unique_ptr<QSignalBlocker>> tabSignals;
    for(const auto& pair:tabPairs)for(auto* dock:{pair.first,pair.second})
        if(dock&&!tabParticipants.contains(dock)){
            tabParticipants.insert(dock);tabVisibility.append({dock,!dock->isHidden()});
            tabSignals.push_back(std::make_unique<QSignalBlocker>(dock));dock->show();
        }
    for(const auto& pair:tabPairs)if(pair.first&&pair.second)
        working_->tabifyDockWidget(pair.first,pair.second);
    for(const auto& visibility:tabVisibility)visibility.first->setVisible(visibility.second);
    ensureSeparatedLayout();captureDefaultLayout();
    if(focus&&focus->isVisible())focus->setFocus(Qt::OtherFocusReason);
}
QMainWindow* StudioWorkspaceLayout::hostForDock(const QDockWidget* dock)const{
    for(const auto& pane:panes_)if(pane.dock==dock)return working_;
    if(dock&&dock!=copilot_&&dockOwner(const_cast<QDockWidget*>(dock))==working_)return working_;
    return outer_;
}
QList<QDockWidget*> StudioWorkspaceLayout::workingDocks()const{
    QList<QDockWidget*> result;for(const auto& pane:panes_)if(pane.dock)result.append(pane.dock);
    return result;
}
bool StudioWorkspaceLayout::registerWorkingDock(QDockWidget* dock,Qt::DockWidgetArea area){
    if(!working_||!dock||dock==copilot_)return false;
    for(const auto& pane:panes_)if(pane.dock==dock)return true;
    if(area!=Qt::LeftDockWidgetArea&&area!=Qt::RightDockWidgetArea&&area!=Qt::TopDockWidgetArea&&area!=Qt::BottomDockWidgetArea)return false;
    panes_.append({dock,area});
    if(dockOwner(dock)==working_&&working_->dockWidgetArea(dock)!=Qt::NoDockWidgetArea)return true;
    const bool shown=!dock->isHidden(),floating=dock->isFloating();const auto geometry=dock->saveGeometry();
    const QSignalBlocker blocked(dock);
    if(auto* previous=dockOwner(dock))previous->removeDockWidget(dock);
    if(floating)dock->setFloating(false);
    dock->setParent(working_);
    const bool restored=working_->restoreDockWidget(dock);
    if(!restored){working_->addDockWidget(area,dock);if(floating){dock->setFloating(true);dock->restoreGeometry(geometry);}}
    dock->setVisible(shown);return true;
}
void StudioWorkspaceLayout::placeWorkingPane(QDockWidget* dock,Qt::DockWidgetArea area){
    if(!working_||!dock)return;
    const bool shown=!dock->isHidden(),floating=dock->isFloating();const auto geometry=dock->saveGeometry();
    const QSignalBlocker blocked(dock);
    if(auto* previous=dockOwner(dock))previous->removeDockWidget(dock);
    if(floating)dock->setFloating(false);
    dock->setParent(working_);working_->addDockWidget(area,dock);
    if(floating){dock->setFloating(true);dock->restoreGeometry(geometry);}
    dock->setVisible(shown);
}
void StudioWorkspaceLayout::ensureSeparatedLayout(){
    if(!outer_||!working_)return;
    for(const auto& pane:panes_)if(pane.dock&&dockOwner(pane.dock)!=working_)
        placeWorkingPane(pane.dock,pane.defaultArea);
    // GTG gives the right column both corners so the Command Window cannot span
    // underneath it. The inner working host makes this invariant structural.
    if(outer_->corner(Qt::TopRightCorner)!=Qt::RightDockWidgetArea)outer_->setCorner(Qt::TopRightCorner,Qt::RightDockWidgetArea);
    if(outer_->corner(Qt::BottomRightCorner)!=Qt::RightDockWidgetArea)outer_->setCorner(Qt::BottomRightCorner,Qt::RightDockWidgetArea);
    if(copilot_){
        copilot_->setAllowedAreas(Qt::RightDockWidgetArea);
        if(dockOwner(copilot_)!=outer_||(!copilot_->isFloating()&&outer_->dockWidgetArea(copilot_)!=Qt::RightDockWidgetArea)){
            const bool shown=!copilot_->isHidden(),floating=copilot_->isFloating();const auto geometry=copilot_->saveGeometry();
            const QSignalBlocker blocked(copilot_);
            if(auto* previous=dockOwner(copilot_))previous->removeDockWidget(copilot_);
            if(floating)copilot_->setFloating(false);
            copilot_->setParent(outer_);outer_->addDockWidget(Qt::RightDockWidgetArea,copilot_);
            if(floating){copilot_->setFloating(true);copilot_->restoreGeometry(geometry);}
            copilot_->setVisible(shown);
        }
    }
}
bool StudioWorkspaceLayout::restoreOuterState(const QByteArray& state,int version){
    if(!outer_||!working_||state.isEmpty()||state.size()>MaximumStateBytes)return false;
    // QMainWindow searches named descendants recursively, including objects
    // owned by a nested QMainWindow. Outer placeholders must not resolve to
    // those live inner widgets and retain their subsequently freed items in
    // Qt's deferred restored-state snapshot.
    std::vector<std::unique_ptr<QSignalBlocker>> dockSignals;
    for(auto* dock:outer_->findChildren<QDockWidget*>())dockSignals.push_back(std::make_unique<QSignalBlocker>(dock));
    struct MaskedName {QPointer<QObject> object;QString name;std::unique_ptr<QSignalBlocker> blocker;};
    std::vector<MaskedName> masked;
    const auto mask=[&](QObject* object){
        const auto name=object->objectName();if(name.isEmpty())return;
        masked.push_back({object,name,std::make_unique<QSignalBlocker>(object)});
        object->setObjectName(QString());
    };
    for(auto* dock:working_->findChildren<QDockWidget*>())mask(dock);
    for(auto* toolbar:working_->findChildren<QToolBar*>())mask(toolbar);
    const bool restored=outer_->restoreState(state,version);
    // Restore names on success and format/version failure before unblocking.
    // Widget ownership, visibility and native window identities never change.
    for(auto& saved:masked)if(saved.object)saved.object->setObjectName(saved.name);
    return restored;
}
QByteArray StudioWorkspaceLayout::saveWorkingState()const{
    if(!working_)return {};
    const auto state=working_->saveState(StateVersion);
    return state.size()<=MaximumStateBytes?state:QByteArray{};
}
bool StudioWorkspaceLayout::restoreWorkingState(const QByteArray& state){
    if(!working_||state.isEmpty()||state.size()>MaximumStateBytes)return false;
    const auto previous=working_->saveState(StateVersion);
    // Dock-controller state is restored separately by the desktop shell. Avoid
    // invoking intermediate auto-hide callbacks while Qt restores placement.
    std::vector<std::unique_ptr<QSignalBlocker>> blocked;
    for(const auto& pane:panes_)if(pane.dock)blocked.push_back(std::make_unique<QSignalBlocker>(pane.dock));
    const bool restored=working_->restoreState(state,StateVersion);
    if(!restored)working_->restoreState(previous,StateVersion);
    ensureSeparatedLayout();return restored;
}
void StudioWorkspaceLayout::captureDefaultLayout(){defaultState_=saveWorkingState();}
bool StudioWorkspaceLayout::resetWorkingLayout(){return restoreWorkingState(defaultState_);}
}
