#include "ide/desktop/figure_group_host.hpp"
#include "ide/desktop/figure_docking.hpp"
#include "ide/desktop/pane_hover.hpp"

#include <QApplication>
#include <QChildEvent>
#include <QCloseEvent>
#include <QDockWidget>
#include <QEvent>
#include <QScopedValueRollback>
#include <QSet>
#include <QLayout>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVariant>
#include <algorithm>

namespace penta::desktop {
namespace {
class FigureDock final:public QDockWidget {
public:
    using QDockWidget::QDockWidget;
    std::function<void()> requestClose;
protected:
    void closeEvent(QCloseEvent* event)override {
        event->ignore();
        if(requestClose)requestClose();
    }
};
constexpr int maximumFigures=64;
}

FigureGroupHost::FigureGroupHost(QWidget* parent):QMainWindow(parent,Qt::Widget) {
    setObjectName(QStringLiteral("figureGroupHost"));setMinimumSize(0,0);
    setDockOptions(AllowNestedDocks|AllowTabbedDocks|GroupedDragging);
    setDockNestingEnabled(true);setDocumentMode(true);
    setTabPosition(Qt::AllDockWidgetAreas,QTabWidget::North);
    connect(this,&QMainWindow::tabifiedDockWidgetActivated,this,[this](QDockWidget* dock){
        if(destroying_||changing_||!dock)return;
        const int id=dock->property("studioFigureId").toInt();
        if(id&&figures_.value(id).dock==dock&&current_!=id){current_=id;if(selected)selected(id);}
    });
    installStudioPaneHover(this);
}
FigureGroupHost::~FigureGroupHost() {
    destroying_=true;selected={};closeRequested={};dockedChanged={};
    for(auto it=figures_.begin();it!=figures_.end();++it){
        if(it->view)QObject::disconnect(it->view,nullptr,this,nullptr);
        if(it->dock){QObject::disconnect(it->dock,nullptr,this,nullptr);static_cast<FigureDock*>(it->dock.data())->requestClose={};}
    }
}
bool FigureGroupHost::addFigure(int id,const QString& title,QWidget* view) {
    if(destroying_||!view||id<1||figures_.contains(id)||figures_.size()>=maximumFigures)return false;
    for(const auto& item:figures_)if(item.view==view)return false;
    ++splitEpoch_;
    QScopedValueRollback<bool> changing(changing_,true);
    auto* dock=new FigureDock(title,this);dock->setObjectName(QStringLiteral("studioFigureDock_%1").arg(id));
    dock->setFeatures(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);dock->setMinimumSize(1,1);
    dock->setProperty("studioFigureId",id);view->setProperty("studioFigureId",id);
    view->setProperty("studioFigureTitle",title);view->setProperty("studioFigureKey",QString::number(id));
    view->hide();dock->setWidget(view);
    auto* controller=new FigureDockController(view,title,QStringLiteral("figureDockToggle_%1").arg(id));
    controller->setDockContainer(dock);dock->addAction(controller->toggleAction());
    const QPointer<QDockWidget> originalDock=dock;
    const QPointer<QWidget> originalView=view;
    const auto close=[this,id,originalDock,originalView]{
        // Finish the native close event before scientific ownership can erase
        // its view/dock. Closing a frame never deletes a scene behind the host.
        QTimer::singleShot(0,this,[this,id,originalDock,originalView]{
            const auto current=figures_.constFind(id);
            if(!destroying_&&originalDock&&originalView&&current!=figures_.cend()
                &&current->dock==originalDock&&current->view==originalView&&closeRequested)closeRequested(id);
        });
    };
    dock->requestClose=close;figures_.insert(id,{dock,view,controller});
    if(!restoreDockWidget(dock)){
        addDockWidget(Qt::LeftDockWidgetArea,dock);
        if(const auto previous=figures_.value(current_).dock)tabifyDockWidget(previous,dock);
    }
    connect(dock,&QDockWidget::topLevelChanged,this,[this,id](bool floating){
        if(!destroying_&&!changing_&&figures_.contains(id)&&dockedChanged)dockedChanged(id,!floating);
    });
    connect(view,&QObject::destroyed,this,[this,id]{
        if(!destroying_&&!changing_)QTimer::singleShot(0,this,[this,id]{removeDestroyedFigure(id);});
    });
    watch(dock);view->show();dock->show();dock->raise();current_=id;
    return true;
}
QWidget* FigureGroupHost::takeFigure(int id) {
    auto it=figures_.find(id);if(it==figures_.end())return nullptr;
    ++splitEpoch_;
    QScopedValueRollback<bool> changing(changing_,true);
    const auto record=*it;figures_.erase(it);
    auto* view=record.view.data();auto* dock=record.dock.data();
    if(view){QObject::disconnect(view,nullptr,this,nullptr);view->hide();}
    if(record.controller)delete record.controller.data();
    if(view)view->setParent(nullptr);
    if(dock){
        QObject::disconnect(dock,nullptr,this,nullptr);static_cast<FigureDock*>(dock)->requestClose={};
        dock->hide();dock->setWidget(nullptr);removeDockWidget(dock);delete dock;
    }
    if(current_==id){
        current_=0;
        if(!figures_.isEmpty()){auto keys=figures_.keys();std::sort(keys.begin(),keys.end());current_=keys.front();}
    }
    update();return view;
}
QWidget* FigureGroupHost::figureWidget(int id)const{return figures_.value(id).view;}
QDockWidget* FigureGroupHost::dockWidget(int id)const{return figures_.value(id).dock;}
FigureDockController* FigureGroupHost::dockingController(int id)const{return figures_.value(id).controller;}
bool FigureGroupHost::setFigureTitle(int id,const QString& title) {
    auto it=figures_.find(id);if(it==figures_.end()||!it->dock||!it->view)return false;
    it->dock->setWindowTitle(title);it->view->setProperty("studioFigureTitle",title);return true;
}
bool FigureGroupHost::setCurrentFigure(int id) {
    const auto dock=figures_.value(id).dock;if(!dock)return false;
    const bool different=current_!=id;current_=id;dock->show();dock->raise();
    if(different&&!changing_&&!destroying_&&selected)selected(id);
    return true;
}
QRect FigureGroupHost::figureCellRect(int id)const {
    auto* dock=dockWidget(id);if(!dock)return {};
    auto* exposed=dock;
    if(dock->geometry().left()<0||dock->geometry().top()<0)
        for(auto* peer:tabifiedDockWidgets(dock))if(peer&&!peer->isFloating()&&peer->geometry().left()>=0&&peer->geometry().top()>=0){exposed=peer;break;}
    QRect cell(exposed->mapTo(const_cast<FigureGroupHost*>(this),QPoint()),exposed->size());
    for(auto* bar:findChildren<QTabBar*>())if(bar->parentWidget()==this&&bar->isVisible()){
        bool matching=false;for(int index=0;index<bar->count();++index)matching|=bar->tabData(index).toULongLong()==reinterpret_cast<quintptr>(dock);
        const QRect heading(bar->mapTo(const_cast<FigureGroupHost*>(this),QPoint()),bar->size());
        if(matching&&rect().contains(heading))cell=cell.united(heading);
    }
    return cell;
}
bool FigureGroupHost::arrangeCells(std::vector<Cell> cells,int keepFirstPair) {
    if(cells.empty())return false;
    for(const auto& cell:cells)if(!cell.dock||!cell.rect.isValid())return false;
    // GTG's cell reconstruction clears Qt's unplug/tab placeholders before
    // splitting. Removing two docks alone can silently create another tab.
    struct Parked {QPointer<QDockWidget> dock;bool shown;};std::vector<Parked> parked;
    QWidget parking;parking.hide();
    for(const auto& figure:figures_)if(figure.dock&&!figure.dock->isFloating()){
        parked.push_back({figure.dock,!figure.dock->isHidden()});removeDockWidget(figure.dock);figure.dock->setParent(&parking);
    }
    struct Node {int cell=-1,left=-1,right=-1,anchor=-1;Qt::Orientation axis=Qt::Horizontal;};std::vector<Node> nodes;
    std::function<int(const std::vector<int>&)> plan=[&](const std::vector<int>& indices){
        if(indices.size()==1){const int index=int(nodes.size());nodes.push_back({indices.front(),-1,-1,indices.front(),Qt::Horizontal});return index;}
        const bool keepPair=keepFirstPair&&indices.size()>2&&std::find(indices.begin(),indices.end(),0)!=indices.end()&&std::find(indices.begin(),indices.end(),1)!=indices.end();
        for(const auto axis:{Qt::Horizontal,Qt::Vertical})for(const int candidate:indices){
            const auto box=cells[size_t(candidate)].rect;const int edge=axis==Qt::Horizontal?box.right():box.bottom();std::vector<int> left,right;bool crosses=false;
            for(const int index:indices){const auto rectangle=cells[size_t(index)].rect;const int low=axis==Qt::Horizontal?rectangle.left():rectangle.top(),high=axis==Qt::Horizontal?rectangle.right():rectangle.bottom();if(high<=edge)left.push_back(index);else if(low>edge)right.push_back(index);else{crosses=true;break;}}
            if(keepPair&&((std::find(left.begin(),left.end(),0)!=left.end())!=(std::find(left.begin(),left.end(),1)!=left.end())))continue;
            if(!crosses&&!left.empty()&&!right.empty()){const int a=plan(left),b=plan(right),index=int(nodes.size());nodes.push_back({-1,a,b,nodes[size_t(a)].anchor,axis});return index;}
        }
        QRect bounds;for(int index:indices)bounds=bounds.united(cells[size_t(index)].rect);
        const auto axis=bounds.width()>=bounds.height()?Qt::Horizontal:Qt::Vertical;
        std::vector<std::vector<int>> units;if(keepPair)units.push_back({0,1});for(int index:indices)if(!keepPair||index>1)units.push_back({index});
        const auto center=[&](const std::vector<int>& unit){QRect box;for(int index:unit)box=box.united(cells[size_t(index)].rect);return box.center();};
        std::stable_sort(units.begin(),units.end(),[&](const auto& a,const auto& b){return axis==Qt::Horizontal?center(a).x()<center(b).x():center(a).y()<center(b).y();});
        std::vector<int> left,right;for(size_t index=0;index<units.size();++index){auto& side=index<units.size()/2?left:right;side.insert(side.end(),units[index].begin(),units[index].end());}
        const int a=plan(left),b=plan(right),index=int(nodes.size());nodes.push_back({-1,a,b,nodes[size_t(a)].anchor,axis});return index;
    };
    std::vector<int> indices;for(int index=0;index<int(cells.size());++index)indices.push_back(index);
    const int root=plan(indices);
    addDockWidget(Qt::LeftDockWidgetArea,cells[size_t(nodes[size_t(root)].anchor)].dock);
    std::function<void(int)> build=[&](int index){const auto node=nodes[size_t(index)];if(node.cell>=0)return;auto* a=cells[size_t(nodes[size_t(node.left)].anchor)].dock.data();auto* b=cells[size_t(nodes[size_t(node.right)].anchor)].dock.data();splitDockWidget(a,b,node.axis);if(node.axis==Qt::Horizontal&&layoutDirection()==Qt::RightToLeft)splitDockWidget(b,a,node.axis);build(node.left);build(node.right);};
    build(root);
    QList<QDockWidget*> docks;QList<int> widths,heights;
    for(const auto& cell:cells){
        for(auto peer:cell.peers)if(peer){addDockWidget(Qt::LeftDockWidgetArea,peer);tabifyDockWidget(cell.dock,peer);peer->show();}
        cell.dock->show();if(cell.dock->widget())cell.dock->widget()->show();cell.dock->raise();
        docks.push_back(cell.dock);widths.push_back(cell.rect.width());heights.push_back(cell.rect.height());
    }
    for(const auto& item:parked)if(item.dock&&item.dock->parentWidget()==&parking){addDockWidget(Qt::LeftDockWidgetArea,item.dock);item.dock->setVisible(item.shown);}
    if(layout())layout()->activate();
    resizeDocks(docks,widths,Qt::Horizontal);resizeDocks(docks,heights,Qt::Vertical);
    return true;
}
bool FigureGroupHost::arrangeRectangles(const QHash<int,QRect>& rectangles){
    if(destroying_||rectangles.isEmpty()||rectangles.size()>maximumFigures)return false;
    std::vector<Cell> cells;auto keys=rectangles.keys();std::sort(keys.begin(),keys.end());
    for(int id:keys){
        auto* dock=dockWidget(id);if(!dock||dock->isFloating()||!rectangles.value(id).isValid())return false;
        const auto existing=std::find_if(cells.begin(),cells.end(),[&](const auto& cell){return cell.rect==rectangles.value(id);});
        if(existing==cells.end())cells.push_back({dock,rectangles.value(id),{}});else existing->peers.push_back(dock);
    }
    QScopedValueRollback<bool> changing(changing_,true);++splitEpoch_;return arrangeCells(std::move(cells));
}
bool FigureGroupHost::splitFigure(int first,int second,Qt::Orientation orientation,bool before) {
    if(first==second||(orientation!=Qt::Horizontal&&orientation!=Qt::Vertical))return false;
    const auto firstDock=figures_.value(first).dock,secondDock=figures_.value(second).dock;
    if(!firstDock||!secondDock)return false;
    const auto previousStates=dockStates();const bool selectionChanged=current_!=second;
    const auto epoch=++splitEpoch_;
    QRect target;
    std::vector<Cell> expectedCells;
    {
        QScopedValueRollback<bool> changing(changing_,true);
        if(firstDock->isFloating())firstDock->setFloating(false);
        firstDock->show();firstDock->raise();
        if(layout())layout()->activate();
        target=figureCellRect(first);
        const int separator=std::max(0,style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent,nullptr,this));
        const int extent=std::max(2,(orientation==Qt::Horizontal?target.width():target.height())-separator),half=extent/2;
        QRect leading=target,trailing=target;if(orientation==Qt::Horizontal){leading.setWidth(half);trailing.setLeft(target.left()+half+separator);}else{leading.setHeight(half);trailing.setTop(target.top()+half+separator);}
        std::vector<Cell> cells{{firstDock,before?trailing:leading,{}},{secondDock,before?leading:trailing,{}}};
        QSet<QDockWidget*> recorded{firstDock,secondDock};
        for(auto* peer:tabifiedDockWidgets(firstDock))if(peer&&peer!=secondDock&&!peer->isFloating()){cells[0].peers.push_back(peer);recorded.insert(peer);}
        auto keys=figures_.keys();std::sort(keys.begin(),keys.end());
        for(int id:keys){auto* dock=dockWidget(id);if(!dock||recorded.contains(dock)||dock->isFloating())continue;
            Cell cell{dock,figureCellRect(id),{}};recorded.insert(dock);
            for(auto* peer:tabifiedDockWidgets(dock))if(peer&&peer!=secondDock&&!peer->isFloating()&&!recorded.contains(peer)){cell.peers.push_back(peer);recorded.insert(peer);}
            cells.push_back(std::move(cell));
        }
        if(secondDock->isFloating())secondDock->setFloating(false);
        expectedCells=cells;
        if(!arrangeCells(std::move(cells),1))return false;
        current_=second;
    }
    const QPointer<QDockWidget> a=firstDock,b=secondDock;
    QTimer::singleShot(0,this,[this,a,b,epoch,expectedCells]{
        if(destroying_||changing_||epoch!=splitEpoch_||!a||!b||a->isFloating()||b->isFloating()||tabifiedDockWidgets(a).contains(b))return;
        QList<QDockWidget*> docks;QList<int> widths,heights;
        for(const auto& cell:expectedCells)if(cell.dock&&!cell.dock->isFloating()&&cell.dock->parentWidget()==this){docks.push_back(cell.dock);widths.push_back(cell.rect.width());heights.push_back(cell.rect.height());}
        resizeDocks(docks,widths,Qt::Horizontal);resizeDocks(docks,heights,Qt::Vertical);
    });
    const QPointer<FigureGroupHost> owner=this;notifyDockChanges(previousStates);
    if(owner&&selectionChanged&&!changing_&&selected)selected(second);
    return true;
}
bool FigureGroupHost::tabifyFigures(int first,int second) {
    if(first==second)return false;
    const auto firstDock=figures_.value(first).dock,secondDock=figures_.value(second).dock;
    if(!firstDock||!secondDock)return false;
    ++splitEpoch_;
    const auto before=dockStates();const bool selectionChanged=current_!=second;
    {
        QScopedValueRollback<bool> changing(changing_,true);
        if(firstDock->isFloating())firstDock->setFloating(false);
        if(secondDock->isFloating())secondDock->setFloating(false);
        tabifyDockWidget(firstDock,secondDock);firstDock->show();secondDock->show();secondDock->raise();current_=second;
    }
    const QPointer<FigureGroupHost> owner=this;notifyDockChanges(before);
    if(owner&&selectionChanged&&!changing_&&selected)selected(second);
    return true;
}
bool FigureGroupHost::floatFigure(int id,bool floating) {
    const auto record=figures_.value(id);if(!record.dock||!record.view||!record.controller)return false;
    if(record.dock->isFloating()==floating)return true;
    ++splitEpoch_;
    return floating?record.controller->undock():record.controller->dock();
}
void FigureGroupHost::showTabs() {
    auto keys=figures_.keys();std::sort(keys.begin(),keys.end());if(keys.isEmpty())return;
    const auto before=dockStates();const int previous=current_;
    {
        QScopedValueRollback<bool> changing(changing_,true);
        for(int i=1;i<keys.size();++i)tabifyFigures(keys.front(),keys[i]);
        setCurrentFigure(keys.back());
    }
    const QPointer<FigureGroupHost> owner=this;notifyDockChanges(before);
    if(owner&&previous!=current_&&selected)selected(current_);
}
QByteArray FigureGroupHost::saveState(int version)const{return QMainWindow::saveState(version);}
bool FigureGroupHost::restoreState(const QByteArray& state,int version) {
    if(state.isEmpty()||state.size()>1024*1024)return false;
    ++splitEpoch_;
    const auto before=dockStates();const auto previous=QMainWindow::saveState(version);bool restored=false;
    {
        QScopedValueRollback<bool> changing(changing_,true);
        restored=QMainWindow::restoreState(state,version);
        if(!restored)QMainWindow::restoreState(previous,version);
    }
    notifyDockChanges(before);return restored;
}
QHash<int,bool> FigureGroupHost::dockStates()const {
    QHash<int,bool> result;for(auto it=figures_.cbegin();it!=figures_.cend();++it)if(it->dock)result.insert(it.key(),!it->dock->isFloating());return result;
}
void FigureGroupHost::notifyDockChanges(const QHash<int,bool>& before) {
    if(changing_||destroying_||!dockedChanged)return;
    const QPointer<FigureGroupHost> owner=this;
    auto keys=before.keys();std::sort(keys.begin(),keys.end());
    for(int id:keys){
        const auto dock=figures_.value(id).dock;
        if(dock&&before.value(id)!=!dock->isFloating())dockedChanged(id,!dock->isFloating());
        if(!owner)return;
    }
}
void FigureGroupHost::watch(QWidget* widget) {
    if(!widget)return;
    widget->installEventFilter(this);
    for(auto* child:widget->findChildren<QWidget*>())child->installEventFilter(this);
}
void FigureGroupHost::removeDestroyedFigure(int id) {
    auto it=figures_.find(id);if(destroying_||it==figures_.end()||it->view)return;
    const auto dock=it->dock;figures_.erase(it);
    if(dock){static_cast<FigureDock*>(dock.data())->requestClose={};removeDockWidget(dock);dock->deleteLater();}
    if(current_==id)current_=figures_.isEmpty()?0:figures_.constBegin().key();
}
bool FigureGroupHost::eventFilter(QObject* object,QEvent* event) {
    auto* widget=qobject_cast<QWidget*>(object);if(!widget)return QMainWindow::eventFilter(object,event);
    if(event->type()==QEvent::ChildAdded){
        if(!watchPending_){
            watchPending_=true;
            QTimer::singleShot(0,this,[this]{watchPending_=false;if(!destroying_)watch(this);});
        }
    }
    if(!changing_&&!destroying_&&(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::FocusIn)){
        for(auto* ancestor=widget;ancestor&&ancestor!=this;ancestor=ancestor->parentWidget()){
            const int id=ancestor->property("studioFigureId").toInt();
            if(id&&figures_.contains(id)){if(id!=current_)setCurrentFigure(id);break;}
        }
    }
    return QMainWindow::eventFilter(object,event);
}
}
