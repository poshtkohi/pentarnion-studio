// Adapted from GTG Cell Studio collapsible_dock.cpp. See GTG_COMPONENT_PROVENANCE.txt.
#include "ide/desktop/collapsible_dock.hpp"
#include "ide/desktop/studio_icons.hpp"
#include <QDockWidget>
#include <QMainWindow>
#include <QToolBar>
#include <QToolButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QMouseEvent>
#include <QStyle>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEvent>
#include <QApplication>
#include <QFrame>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QPixmap>
#include <QIcon>
#include <QAction>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QLayout>
#include <QVariant>
#include <algorithm>
#include <cmath>
namespace penta::desktop {
namespace {
class DockTitle final:public QWidget {
public:
    using QWidget::QWidget;
protected:
    void mousePressEvent(QMouseEvent* event)override{event->ignore();}
    void mouseMoveEvent(QMouseEvent* event)override{event->ignore();}
    void mouseReleaseEvent(QMouseEvent* event)override{event->ignore();}
    void mouseDoubleClickEvent(QMouseEvent* event)override{event->ignore();}
};
class RailButton final:public QToolButton {
public:
    using QToolButton::QToolButton;
    void setVertical(bool vertical){vertical_=vertical;updateGeometry();update();}
    void setCompact(bool compact){compact_=compact;updateGeometry();update();}
    QSize sizeHint()const override{return vertical_?(compact_?QSize(std::max(16,fontMetrics().height()+2),std::max(108,fontMetrics().horizontalAdvance(text())+8)):QSize(26,108)):QSize(std::max(108,fontMetrics().horizontalAdvance(text())+20),26);}
    QSize minimumSizeHint()const override{return compact_&&vertical_?sizeHint():QToolButton::minimumSizeHint();}
protected:
    void paintEvent(QPaintEvent*)override{
        bool active=false;for(const QWidget* node=this;node;node=node->parentWidget())if(node->property("studioPaneHoverActive").toBool()){active=true;break;}
        QPainter p(this);p.fillRect(rect(),active?QColor("#0a5664"):underMouse()?QColor(219,234,245):QColor(235,239,243));
        p.setPen(active?QColor(Qt::white):QColor(45,62,77));p.save();if(vertical_){p.translate(0,height());p.rotate(-90);p.drawText(QRect(0,0,height(),width()),Qt::AlignCenter,text());}else p.drawText(rect(),Qt::AlignCenter,text());p.restore();
    }
private:
    bool vertical_=true,compact_=false;
};
}
CollapsibleDockController::CollapsibleDockController(QMainWindow* window,QDockWidget* dock,const QString& key):QObject(window),window_(window),dock_(dock){
    const auto prefix=key.isEmpty()?QStringLiteral("project"):key;prefix_=prefix;project_=prefix=="project";sideDrawer_=project_||prefix=="copilot";defaultArea_=window->dockWidgetArea(dock);if(defaultArea_==Qt::NoDockWidgetArea)defaultArea_=prefix=="copilot"?Qt::RightDockWidgetArea:Qt::LeftDockWidgetArea;if(prefix=="copilot")expandedWidth_=340;const auto labelText=dock->windowTitle();
    dock->setProperty("studioDockRole",prefix);
    setObjectName(prefix+"DockController");
    rail_=new QToolBar(labelText,window);rail_->setObjectName(prefix+"MinimizedRail");rail_->setMovable(false);rail_->setFloatable(false);rail_->setAllowedAreas(Qt::AllToolBarAreas);rail_->setIconSize({16,16});rail_->setStyleSheet("QToolBar {padding:0;spacing:0;border:0;background:#ebeff3;} QToolButton {padding:3px;border:0;}");
    auto* restoreButton=new QToolButton(rail_);railRestore_=restoreButton;restoreButton->setObjectName(prefix+"DockRestore");restoreButton->setIcon(studioCommandIcon(project_?QStringLiteral("Project"):prefix=="copilot"?QStringLiteral("Copilot"):QStringLiteral("Command Window")));restoreButton->setToolTip("Restore "+labelText);restoreButton->setAccessibleName(restoreButton->toolTip());rail_->addWidget(restoreButton);connect(restoreButton,&QToolButton::clicked,this,[this]{if(autoHide_)peek();else restore();});
    auto* label=new RailButton(rail_);railLabel_=label;label->setCompact(sideDrawer_);label->setText(labelText);label->setObjectName(prefix+"DockRailLabel");label->setAccessibleName("Restore "+labelText+" panel");label->setToolTip(label->accessibleName());rail_->addWidget(label);connect(label,&QToolButton::clicked,this,[this]{if(autoHide_)peek();else restore();});connect(rail_,&QToolBar::orientationChanged,label,[label](Qt::Orientation orientation){label->setVertical(orientation==Qt::Vertical);});alignRail();label->setVertical(rail_->orientation()==Qt::Vertical);rail_->hide();rail_->installEventFilter(this);if(sideDrawer_)label->installEventFilter(this);
    auto* title=new DockTitle(dock);titleBar_=title;title->setObjectName(prefix+"DockTitleBar");title->setFixedHeight(24);auto* row=new QHBoxLayout(title);titleLayout_=row;row->setContentsMargins(4,0,1,0);row->setSpacing(1);auto* text=new QLabel(labelText,title);titleLabel_=text;text->setObjectName(prefix+"DockTitleText");text->setAttribute(Qt::WA_TransparentForMouseEvents);row->addWidget(text,1);
    const auto button=[&](const QString& name,const QString& tip,QStyle::StandardPixmap icon){auto* tool=new QToolButton(title);tool->setObjectName(prefix+name);tool->setIcon(window->style()->standardIcon(icon));tool->setIconSize({12,12});tool->setFixedSize(20,20);tool->setToolTip(tip);tool->setAccessibleName(tip);tool->setAutoRaise(true);row->addWidget(tool);return tool;};
    minimize_=button("DockMinimize","Minimize "+labelText,QStyle::SP_TitleBarMinButton);float_=button("DockFloat","Undock "+labelText,QStyle::SP_TitleBarNormalButton);auto* close=button("DockClose","Close "+labelText,QStyle::SP_TitleBarCloseButton);dock->setTitleBarWidget(title);dock->installEventFilter(this);
    connect(minimize_,&QToolButton::clicked,this,[this]{collapse();});connect(float_,&QToolButton::clicked,this,[this]{if(dock_&&undockingEnabled_)dock_->setFloating(!dock_->isFloating());});connect(close,&QToolButton::clicked,this,[this]{if(prefix_=="copilot")setPanelVisible(false);else if(dock_)dock_->close();});
    connect(dock,&QDockWidget::topLevelChanged,this,[this](bool floating){
        updateControls();
        if(!floating)scheduleReflowColumn();
        if(floating&&!undockingEnabled_)QTimer::singleShot(0,this,[this]{if(dock_&&!undockingEnabled_&&dock_->isFloating()){dock_->setFloating(false);if(autoHide_)setCollapsed(true);else applyExtent();}});
    });
    connect(dock,&QDockWidget::dockLocationChanged,this,[this](Qt::DockWidgetArea){scheduleReflowColumn();});
    connect(dock,&QDockWidget::visibilityChanged,this,[this](bool visible){
        if(prefix_=="copilot")QTimer::singleShot(0,this,[this]{notifyPanelVisibility();});
        if(!visible||changing_)return;
        if(autoHide_){changing_=true;dock_->hide();collapsed_=true;alignRail();rail_->show();changing_=false;if(window_&&window_->isVisible())peek();}
        else if(collapsed_){collapsed_=false;rail_->hide();QTimer::singleShot(0,this,[this]{applyExtent();});if(collapsedChanged)collapsedChanged(false);}
    });
    drawer_=new QFrame(window);drawer_->setObjectName(prefix+"Drawer");drawer_->setFrameShape(QFrame::StyledPanel);drawer_->setStyleSheet("QFrame#"+prefix+"Drawer {background:white;border:1px solid #aebcc8;}");
    drawerLayout_=new QVBoxLayout(drawer_);drawerLayout_->setContentsMargins(1,1,1,1);drawerLayout_->setSpacing(0);
    auto* drawerTitle=new QWidget(drawer_);drawerTitle->setObjectName(prefix+"DrawerTitle");drawerTitle->setFixedHeight(24);drawerTitle->setStyleSheet("background:#e8edf2;");auto* drawerRow=new QHBoxLayout(drawerTitle);drawerRow->setContentsMargins(4,0,1,0);drawerRow->setSpacing(1);drawerRow->addWidget(new QLabel(labelText,drawerTitle),1);
    auto* pin=new QToolButton(drawerTitle);pin->setObjectName(prefix+"DrawerPin");pin->setToolTip("Pin "+labelText);pin->setAccessibleName(pin->toolTip());pin->setAutoRaise(true);pin->setFixedSize(20,20);
    QPixmap thumbtack(16,16);thumbtack.fill(Qt::transparent);{QPainter p(&thumbtack);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor(53,94,126),1.2));p.setBrush(QColor(111,157,188));p.drawRect(5,2,6,2);p.drawRect(6,4,4,5);p.drawLine(4,10,12,10);p.drawLine(8,10,8,15);}pin->setIcon(QIcon(thumbtack));drawerRow->addWidget(pin);connect(pin,&QToolButton::clicked,this,[this]{setAutoHide(false);});
    if(prefix_=="copilot"){
        pin_=new QToolButton(title);pin_->setObjectName(prefix+"DockPin");pin_->setIcon(QIcon(thumbtack));pin_->setIconSize({12,12});pin_->setFixedSize(20,20);pin_->setAutoRaise(true);pin_->setCheckable(true);row->insertWidget(1,pin_);
        connect(pin_,&QToolButton::clicked,this,[this](bool pinned){setAutoHide(!pinned);});
    }
    drawerFloat_=new QToolButton(drawerTitle);drawerFloat_->setObjectName(prefix+"DrawerFloat");drawerFloat_->setIcon(window->style()->standardIcon(QStyle::SP_TitleBarNormalButton));drawerFloat_->setToolTip("Undock "+labelText);drawerFloat_->setAccessibleName(drawerFloat_->toolTip());drawerFloat_->setAutoRaise(true);drawerFloat_->setFixedSize(20,20);drawerRow->addWidget(drawerFloat_);connect(drawerFloat_,&QToolButton::clicked,this,[this]{if(!undockingEnabled_||!dock_)return;setAutoHide(false);dock_->setFloating(true);});
    auto* dismiss=new QToolButton(drawerTitle);dismiss->setObjectName(prefix+"DrawerClose");dismiss->setIcon(window->style()->standardIcon(QStyle::SP_TitleBarCloseButton));dismiss->setToolTip("Close "+labelText+" drawer");dismiss->setAccessibleName(dismiss->toolTip());dismiss->setAutoRaise(true);dismiss->setFixedSize(20,20);drawerRow->addWidget(dismiss);connect(dismiss,&QToolButton::clicked,this,[this]{closeDrawer();});drawerLayout_->addWidget(drawerTitle);
    placeholder_=new QWidget(drawer_);placeholder_->hide();drawer_->hide();window->installEventFilter(this);
    setUndockingEnabled(false);if(sideDrawer_)setAutoHide(true);lastPanelVisible_=panelVisible();

}
void CollapsibleDockController::applyExtent(){if(!window_||!dock_||collapsed_||dock_->isHidden()||dock_->isFloating())return;ensureReflowColumn();const auto area=window_->dockWidgetArea(dock_);const bool horizontal=area==Qt::LeftDockWidgetArea||area==Qt::RightDockWidgetArea;window_->resizeDocks({dock_},{horizontal?expandedWidth_:expandedHeight_},horizontal?Qt::Horizontal:Qt::Vertical);}
void CollapsibleDockController::rememberExtent(){if(!window_||!window_->isVisible()||!dock_||dock_->isHidden()||dock_->isFloating())return;const auto area=window_->dockWidgetArea(dock_);if(area==Qt::LeftDockWidgetArea||area==Qt::RightDockWidgetArea){if(dock_->width()>=64)expandedWidth_=dock_->width();}else if(dock_->height()>=64)expandedHeight_=dock_->height();}
void CollapsibleDockController::alignRail(){
    if(railHost_){
        if(window_&&window_->toolBarArea(rail_)!=Qt::NoToolBarArea)window_->removeToolBar(rail_);
        if(rail_->parentWidget()!=railHost_)rail_->setParent(railHost_);
        if(auto* box=dynamic_cast<QBoxLayout*>(railHost_->layout());box&&box->indexOf(rail_)<0)box->addWidget(rail_);
        rail_->setOrientation(Qt::Horizontal);updateRailMetrics(false);return;
    }
    if(!window_||!dock_)return;
    Qt::ToolBarArea area=prefix_=="copilot"&&defaultArea_==Qt::RightDockWidgetArea?Qt::RightToolBarArea:Qt::LeftToolBarArea;switch(window_->dockWidgetArea(dock_)){case Qt::RightDockWidgetArea:area=Qt::RightToolBarArea;break;case Qt::TopDockWidgetArea:area=Qt::TopToolBarArea;break;case Qt::BottomDockWidgetArea:area=Qt::BottomToolBarArea;break;default:break;}
    const bool vertical=area==Qt::LeftToolBarArea||area==Qt::RightToolBarArea;if(window_->toolBarArea(rail_)!=area)window_->addToolBar(area,rail_);rail_->setOrientation(vertical?Qt::Vertical:Qt::Horizontal);updateRailMetrics(vertical);
}
void CollapsibleDockController::setRailHost(QWidget* host){
    if(!host||!dynamic_cast<QBoxLayout*>(host->layout()))return;
    const bool shown=!rail_->isHidden();railHost_=host;alignRail();rail_->setVisible(shown);
}
void CollapsibleDockController::updateRailMetrics(bool vertical){
    const bool compact=sideDrawer_&&vertical;const int thickness=compact?std::max(16,railLabel_->fontMetrics().height()+2):28;
    rail_->setMinimumSize(0,0);rail_->setMaximumSize(QWIDGETSIZE_MAX,QWIDGETSIZE_MAX);if(vertical)rail_->setFixedWidth(thickness);else rail_->setFixedHeight(thickness);
    const QString style=compact?QStringLiteral("QToolBar {padding:0;spacing:0;border:0;background:#ebeff3;} QToolButton {padding:0;border:0;}"):QStringLiteral("QToolBar {padding:0;spacing:0;border:0;background:#ebeff3;} QToolButton {padding:3px;border:0;}");
    if(rail_->styleSheet()!=style)rail_->setStyleSheet(style);
    rail_->setIconSize(compact?QSize(12,12):QSize(16,16));railRestore_->setIconSize(rail_->iconSize());
    railRestore_->setMinimumSize(0,0);railRestore_->setMaximumSize(QWIDGETSIZE_MAX,QWIDGETSIZE_MAX);railLabel_->setMinimumSize(0,0);railLabel_->setMaximumSize(QWIDGETSIZE_MAX,QWIDGETSIZE_MAX);
    if(compact){rail_->setContentsMargins(0,0,0,0);if(rail_->layout())rail_->layout()->setContentsMargins(0,0,0,0);railRestore_->setFixedSize(thickness,18);railLabel_->setFixedWidth(thickness);}
}
CollapsibleDockController::~CollapsibleDockController(){if(applicationFilter_&&qApp)qApp->removeEventFilter(this);if(drawerOpen_&&dock_)restoreDrawerContent();}
void CollapsibleDockController::setHeaderWidget(QWidget* widget){
    if(!titleBar_||!titleLayout_||headerWidget_==widget)return;
    if(widget&&(widget==titleBar_||widget->isAncestorOf(titleBar_)))return;
    if(headerWidget_){titleLayout_->removeWidget(headerWidget_);headerWidget_->hide();headerWidget_->deleteLater();}
    headerWidget_=widget;
    if(titleLabel_)titleLabel_->setVisible(!widget);
    if(widget){
        widget->setParent(titleBar_);titleLayout_->insertWidget(0,widget,1);widget->show();
        connect(widget,&QObject::destroyed,this,[this]{if(!headerWidget_&&titleLabel_)titleLabel_->show();});
    }
    titleLayout_->invalidate();titleBar_->updateGeometry();if(dock_)dock_->updateGeometry();
}
void CollapsibleDockController::updateControls(){
    if(pin_){QSignalBlocker blocked(pin_);pin_->setChecked(!autoHide_);pin_->setToolTip((autoHide_?QStringLiteral("Pin "):QStringLiteral("Auto-hide "))+(dock_?dock_->windowTitle():QString{}));pin_->setAccessibleName(pin_->toolTip());}
    if(!dock_)return;
    const bool floating=dock_->isFloating();minimize_->setEnabled(!floating);float_->setVisible(undockingEnabled_);float_->setEnabled(undockingEnabled_);float_->setToolTip((floating?"Dock ":"Undock ")+dock_->windowTitle());float_->setAccessibleName(float_->toolTip());if(drawerFloat_)drawerFloat_->setVisible(undockingEnabled_);
}
void CollapsibleDockController::setUndockingEnabled(bool enabled){
    undockingEnabled_=enabled;if(!dock_)return;auto features=dock_->features();features.setFlag(QDockWidget::DockWidgetFloatable,enabled);dock_->setFeatures(features);if(!enabled&&dock_->isFloating()){closeDrawer();dock_->setFloating(false);if(autoHide_)setCollapsed(true);else applyExtent();}updateControls();
}
void CollapsibleDockController::setAutoHide(bool enabled){
    if(!dock_||!window_)return;
    if(autoHide_==enabled){if(enabled&&!drawerOpen_)setCollapsed(true);return;}
    closeDrawer();if(enabled){rememberExtent();if(dock_->isFloating())dock_->setFloating(false);autoHide_=true;setCollapsed(true);}else{autoHide_=false;setCollapsed(false);}updateControls();if(autoHideChanged)autoHideChanged(autoHide_);
}
void CollapsibleDockController::setReflowDrawer(bool enabled){if(reflowDrawer_==enabled)return;closeDrawer();reflowDrawer_=enabled;if(enabled)scheduleReflowColumn();}
void CollapsibleDockController::scheduleReflowColumn(){
    if(!reflowDrawer_||prefix_!="copilot"||columnRepairPending_||columnRepairing_)return;
    columnRepairPending_=true;QTimer::singleShot(0,this,[this]{columnRepairPending_=false;ensureReflowColumn();});
}
void CollapsibleDockController::ensureReflowColumn(){
    if(!reflowDrawer_||prefix_!="copilot"||columnRepairing_||!window_||!dock_||dock_->isHidden()||dock_->isFloating())return;
    const int chatWidth=expandedWidth_;
    QScopedValueRollback<bool> repairing(columnRepairing_,true);
    QDockWidget* command=nullptr;
    for(auto* candidate:window_->findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly))
        if(candidate->property("studioDockRole").toString()==QStringLiteral("command")){command=candidate;break;}
    auto* commandOwner=dynamic_cast<CollapsibleDockController*>(window_->findChild<QObject*>("commandDockController"));
    const int previousCommandHeight=command?command->height():245,storedCommandHeight=commandOwner?commandOwner->expandedHeight():245;
    {
        QScopedValueRollback<bool> changing(changing_,true);
        // Legacy Qt toolbar state can reattach the minimized Command strip
        // globally. Keep it inside the left working area even without a
        // usable separately saved Command Window preference.
        if(commandOwner&&commandOwner->railHost_){
            auto* host=commandOwner->railHost_.data();auto* rail=commandOwner->rail_;
            if(window_->toolBarArea(rail)!=Qt::NoToolBarArea||rail->parentWidget()!=host)commandOwner->setRailHost(host);
        }
        if(window_->layout())window_->layout()->activate();
    }
    const auto area=window_->dockWidgetArea(dock_);
    const bool wrongCorner=window_->corner(Qt::BottomRightCorner)!=Qt::RightDockWidgetArea||window_->corner(Qt::TopRightCorner)!=Qt::RightDockWidgetArea;
    const bool wrongArea=area!=Qt::RightDockWidgetArea;
    const bool moveCommand=command&&!command->isFloating()&&window_->dockWidgetArea(command)!=Qt::NoDockWidgetArea&&window_->dockWidgetArea(command)!=Qt::BottomDockWidgetArea;
    // Qt native state restores can retain a split hierarchy even when public
    // area/corner values are already correct. Check the visible geometry too.
    bool overlap=false;
    if(command&&command->isVisible()&&!command->isFloating()&&dock_->isVisible()){
        const QRect chat(dock_->mapTo(window_,QPoint()),dock_->size()),output(command->mapTo(window_,QPoint()),command->size());
        // A bottom-spanning command dock may sit entirely BELOW Copilot,
        // rather than intersecting its rectangle. Horizontal intrusion into
        // the right column is still a violation in that arrangement.
        overlap=output.right()>=chat.left()&&output.bottom()>=chat.top();
    }
    if(!wrongCorner&&!wrongArea&&!moveCommand&&!overlap)return;
    QScopedValueRollback<bool> changing(changing_,true);
    const int commandHeight=moveCommand?storedCommandHeight:previousCommandHeight;
    const bool commandShown=command&&!command->isHidden();
    if(moveCommand){window_->removeDockWidget(command);window_->addDockWidget(Qt::BottomDockWidgetArea,command);command->setVisible(commandShown);}
    if(wrongArea||overlap){window_->removeDockWidget(dock_);window_->addDockWidget(Qt::RightDockWidgetArea,dock_);dock_->show();}
    window_->setCorner(Qt::TopRightCorner,Qt::RightDockWidgetArea);window_->setCorner(Qt::BottomRightCorner,Qt::RightDockWidgetArea);
    if(window_->layout()){window_->layout()->invalidate();window_->layout()->activate();}
    window_->resizeDocks({dock_},{chatWidth},Qt::Horizontal);
    if(command&&commandShown&&!command->isFloating())window_->resizeDocks({command},{std::max(64,commandHeight)},Qt::Vertical);
    expandedWidth_=chatWidth;
}
void CollapsibleDockController::positionDrawer(){
    if(reflowDrawer_)return;
    if(!drawerOpen_||!window_||!dock_)return;
    QRect space=window_->centralWidget()?QRect(window_->centralWidget()->mapTo(window_,QPoint()),window_->centralWidget()->size()):window_->contentsRect();if(space.width()<80||space.height()<80)space=window_->contentsRect();
    const auto area=window_->dockWidgetArea(dock_);QRect box=space;const int width=std::min(std::max(140,expandedWidth_),space.width()),height=std::min(std::max(100,expandedHeight_),space.height());
    if(area==Qt::RightDockWidgetArea){box.setLeft(space.right()-width+1);}else if(area==Qt::TopDockWidgetArea){box.setHeight(height);}else if(area==Qt::BottomDockWidgetArea){box.setTop(space.bottom()-height+1);}else box.setWidth(width);drawer_->setGeometry(box);drawer_->raise();
}
void CollapsibleDockController::peek(){
    if(autoHide_&&reflowDrawer_){
        if(!dock_||!window_||drawerOpen_)return;
        previousFocus_=QApplication::focusWidget();drawerOpen_=true;changing_=true;collapsed_=false;alignRail();rail_->hide();dock_->show();changing_=false;applyExtent();updateControls();
        if(auto* content=dock_->widget())content->setFocus(Qt::OtherFocusReason);
        if(!applicationFilter_&&qApp){qApp->installEventFilter(this);applicationFilter_=true;}notifyPanelVisibility();return;
    }
    if(!autoHide_){restore();return;}if(!dock_||!window_||drawerOpen_)return;changing_=true;if(!dock_->isHidden())rememberExtent();dock_->hide();collapsed_=true;alignRail();rail_->show();changing_=false;
    drawerContent_=dock_->widget();if(!drawerContent_)return;previousFocus_=QApplication::focusWidget();drawerOpen_=true;hideExpandedPanelRail();if(window_->layout())window_->layout()->activate();drawerContent_->setParent(drawer_);placeholder_->setParent(dock_);dock_->setWidget(placeholder_);placeholder_->hide();drawerLayout_->addWidget(drawerContent_);drawerContent_->show();positionDrawer();drawer_->show();drawer_->raise();drawerContent_->setFocus(Qt::OtherFocusReason);if(!applicationFilter_&&qApp){qApp->installEventFilter(this);applicationFilter_=true;}
}
void CollapsibleDockController::restoreDrawerContent(){
    if(!drawerContent_||!dock_)return;
    drawerLayout_->removeWidget(drawerContent_);placeholder_->hide();placeholder_->setParent(drawer_);drawerContent_->setParent(dock_);dock_->setWidget(drawerContent_);drawerContent_.clear();
}
void CollapsibleDockController::closeDrawer(){
    if(!drawerOpen_)return;
    const auto focus=QApplication::focusWidget();auto* container=reflowDrawer_?static_cast<QWidget*>(dock_.data()):static_cast<QWidget*>(drawer_);const bool restoreFocus=!focus||focus==container||(container&&container->isAncestorOf(focus));drawerOpen_=false;if(applicationFilter_&&qApp){qApp->removeEventFilter(this);applicationFilter_=false;}
    if(reflowDrawer_){rememberExtent();changing_=true;collapsed_=true;dock_->hide();alignRail();rail_->show();changing_=false;}
    else {drawer_->hide();restoreDrawerContent();if(prefix_=="copilot"){alignRail();rail_->show();}}
    if(restoreFocus&&previousFocus_&&previousFocus_->isVisible())previousFocus_->setFocus(Qt::OtherFocusReason);
    previousFocus_.clear();
}
bool CollapsibleDockController::panelVisible()const{return drawerOpen_||(rail_&&!rail_->isHidden())||(dock_&&!dock_->isHidden());}
void CollapsibleDockController::hideExpandedPanelRail(){
    // QMainWindow restores toolbar visibility independently of dock visibility.
    // A stale native layout must not expose a minimized rail beside its pinned
    // or floating dock. Project's overlay keeps its rail, while Copilot's open
    // drawer replaces it.
    if(rail_&&((dock_&&!dock_->isHidden())||(prefix_=="copilot"&&drawerOpen_)))rail_->hide();
}
void CollapsibleDockController::notifyPanelVisibility(){const bool visible=panelVisible();if(lastPanelVisible_==visible)return;lastPanelVisible_=visible;if(panelVisibilityChanged)panelVisibilityChanged(visible);}
void CollapsibleDockController::setPanelVisible(bool visible){
    if(!dock_||!window_)return;
    if(visible&&drawerOpen_){hideExpandedPanelRail();notifyPanelVisibility();return;}
    if(!visible){closeDrawer();changing_=true;dock_->hide();rail_->hide();changing_=false;}
    else if(autoHide_||collapsed_){changing_=true;collapsed_=true;alignRail();dock_->hide();rail_->show();changing_=false;}
    else{changing_=true;rail_->hide();dock_->show();changing_=false;applyExtent();}
    notifyPanelVisibility();
}
void CollapsibleDockController::setCollapsed(bool collapse){
    if(!dock_||!window_||changing_||(collapse&&dock_->isFloating()))return;
    if(autoHide_&&!collapse){peek();return;}if(collapse)closeDrawer();
    if(collapsed_==collapse){if(collapse){alignRail();dock_->hide();rail_->show();}return;}
    changing_=true;if(collapse){rememberExtent();collapsed_=true;alignRail();dock_->hide();rail_->show();}else{collapsed_=false;rail_->hide();dock_->show();applyExtent();QTimer::singleShot(0,this,[this]{applyExtent();});}changing_=false;if(collapsedChanged)collapsedChanged(collapsed_);
}
void CollapsibleDockController::reset(){
    closeDrawer();setUndockingEnabled(false);const bool prior=collapsed_,wasAuto=autoHide_;changing_=true;autoHide_=sideDrawer_;collapsed_=sideDrawer_;expandedWidth_=prefix_=="copilot"?340:245;expandedHeight_=245;if(dock_){if(!undockingEnabled_&&dock_->isFloating())dock_->setFloating(false);if(prefix_=="copilot"&&window_)window_->addDockWidget(defaultArea_,dock_);dock_->setVisible(!collapsed_);}if(collapsed_){alignRail();rail_->show();}else rail_->hide();changing_=false;if(prior!=collapsed_&&collapsedChanged)collapsedChanged(collapsed_);if(wasAuto!=autoHide_&&autoHideChanged)autoHideChanged(autoHide_);updateControls();notifyPanelVisibility();
}
QByteArray CollapsibleDockController::saveState()const{return QJsonDocument(QJsonObject{{"schema",3},{"autoHide",autoHide_},{"collapsed",collapsed_},{"visible",panelVisible()},{"expandedWidth",expandedWidth_},{"expandedHeight",expandedHeight_}}).toJson(QJsonDocument::Compact);}
bool CollapsibleDockController::restoreState(const QByteArray& bytes){
    if(bytes.size()>4096)return false;
    QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);const auto object=document.object();const auto schema=object.value("schema").toInt();if(error.error!=QJsonParseError::NoError||!document.isObject()||(schema!=1&&schema!=2&&schema!=3)||!object.value("collapsed").isBool()||!object.value("expandedWidth").isDouble()||(schema>=2&&!object.value("expandedHeight").isDouble())||(schema==3&&!object.value("autoHide").isBool()))return false;const auto width=object.value("expandedWidth").toDouble(),height=schema==1?245:object.value("expandedHeight").toDouble();for(const auto extent:{width,height})if(!std::isfinite(extent)||extent<64||extent>16384||extent!=static_cast<int>(extent))return false;
    const bool desiredAuto=schema==3?object.value("autoHide").toBool():sideDrawer_,desired=desiredAuto||object.value("collapsed").toBool();if(desired&&dock_&&dock_->isFloating()&&undockingEnabled_)return false;
    closeDrawer();const bool priorAuto=autoHide_;expandedWidth_=static_cast<int>(width);expandedHeight_=static_cast<int>(height);changing_=true;autoHide_=desiredAuto;collapsed_=desired;if(dock_&&!undockingEnabled_&&dock_->isFloating())dock_->setFloating(false);if(desired)alignRail();
    const bool visible=object.value("visible").toBool(true);const bool panelVisible=schema<3?(object.value("collapsed").toBool()||visible):visible;if(dock_)dock_->setVisible(!desired&&visible);rail_->setVisible(desired&&panelVisible);changing_=false;if(!desired)applyExtent();if(collapsedChanged)collapsedChanged(desired);if(priorAuto!=autoHide_&&autoHideChanged)autoHideChanged(autoHide_);updateControls();notifyPanelVisibility();return true;
}
bool CollapsibleDockController::eventFilter(QObject* watched,QEvent* event){
    if((watched==rail_||watched==dock_)&&(event->type()==QEvent::Show||event->type()==QEvent::ShowToParent))hideExpandedPanelRail();
    if(reflowDrawer_&&((watched==window_&&(event->type()==QEvent::LayoutRequest||event->type()==QEvent::Show||event->type()==QEvent::WindowStateChange))||(watched==dock_&&event->type()==QEvent::Show)))scheduleReflowColumn();
    if(sideDrawer_&&(watched==rail_||watched==railLabel_)&&event->type()==QEvent::FontChange)updateRailMetrics(rail_->orientation()==Qt::Vertical);
    if(prefix_=="copilot"&&(watched==rail_||watched==dock_)&&(event->type()==QEvent::Show||event->type()==QEvent::Hide||event->type()==QEvent::ShowToParent||event->type()==QEvent::HideToParent))QTimer::singleShot(0,this,[this]{notifyPanelVisibility();});
    if(watched==dock_&&event->type()==QEvent::Resize&&!changing_&&!collapsed_)rememberExtent();
    if(drawerOpen_){
        if(watched==window_&&event->type()==QEvent::Resize)positionDrawer();
        if(watched==window_&&event->type()==QEvent::Hide){closeDrawer();return false;}
        if(watched==window_&&event->type()==QEvent::WindowDeactivate)QTimer::singleShot(0,this,[this]{if(drawerOpen_&&window_&&!window_->isActiveWindow()&&!QApplication::activePopupWidget()&&!QApplication::activeModalWidget())closeDrawer();});
        if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape&&!QApplication::activePopupWidget()&&!QApplication::activeModalWidget()){closeDrawer();return true;}
        if(event->type()==QEvent::MouseButtonPress){auto* target=qobject_cast<QWidget*>(watched);auto* container=reflowDrawer_?static_cast<QWidget*>(dock_.data()):static_cast<QWidget*>(drawer_);bool popup=false;for(auto* ancestor=target;ancestor;ancestor=ancestor->parentWidget())if(qobject_cast<QMenu*>(ancestor)){popup=true;break;}
            bool separator=false;if(reflowDrawer_&&dock_&&window_){const auto point=window_->mapFromGlobal(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());const QRect bounds(dock_->mapTo(window_,QPoint()),dock_->size());const int extent=std::max(4,window_->style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent));separator=bounds.adjusted(-extent,-extent,extent,extent).contains(point);}
            if(target&&!separator&&!popup&&!QApplication::activePopupWidget()&&!QApplication::activeModalWidget()&&target!=container&&!(container&&container->isAncestorOf(target))&&target!=rail_&&!rail_->isAncestorOf(target))closeDrawer();}
    }
    return QObject::eventFilter(watched,event);
}
}
