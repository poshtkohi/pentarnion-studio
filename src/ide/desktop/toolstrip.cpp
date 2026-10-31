// Adapted from GTG Cell Studio toolstrip.cpp. See GTG_COMPONENT_PROVENANCE.txt.
#include "ide/desktop/toolstrip.hpp"
#include "ide/desktop/studio_icons.hpp"
#include <QAction>
#include <QContextMenuEvent>
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMenu>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace penta::desktop {
namespace {
constexpr int bodyHeight=88;
QIcon commandIcon(QWidget* owner,const QAction* action){
    return studioActionIcon(action,owner);
}
}

Toolstrip::Toolstrip(QWidget* parent):QWidget(parent){
    setObjectName("studioToolstrip");setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);setMinimumWidth(0);
    layout_=new QVBoxLayout(this);layout_->setContentsMargins(0,0,0,0);layout_->setSpacing(0);
    auto* header=new QWidget(this);header_=header;header->setObjectName("toolstripHeader");header->setFixedHeight(28);
    auto* row=new QHBoxLayout(header);row->setContentsMargins(5,0,5,0);row->setSpacing(4);
    tabs_=new QTabBar(header);tabs_->setObjectName("toolstripTabs");tabs_->setDrawBase(false);tabs_->setExpanding(false);tabs_->setUsesScrollButtons(true);tabs_->setFocusPolicy(Qt::TabFocus);tabs_->setAccessibleName("Toolstrip tabs");tabs_->installEventFilter(this);row->addWidget(tabs_,1);
    pin_=new QToolButton(header);pin_->setObjectName("toolstripPin");pin_->setCheckable(true);pin_->setChecked(true);pin_->setFocusPolicy(Qt::TabFocus);pin_->setFixedSize(24,22);row->addWidget(pin_);layout_->addWidget(header);
    displayMenu_=new QMenu("Ribbon",this);displayMenu_->setObjectName("ribbonDisplayMenu");
    toggleRibbon_=new QAction("Minimise Ribbon",this);toggleRibbon_->setObjectName("ribbonCollapse");toggleRibbon_->setCheckable(true);toggleRibbon_->setShortcut(QKeySequence("Ctrl+F1"));toggleRibbon_->setShortcutContext(Qt::WindowShortcut);toggleRibbon_->setIcon(studioCommandIcon("Minimise Ribbon","ribbonCollapse"));QWidget::addAction(toggleRibbon_);connect(toggleRibbon_,&QAction::triggered,this,[this](bool collapsed){setDisplayMode(collapsed?RibbonDisplay::TabsOnly:RibbonDisplay::Expanded);});
    displayActions_.push_back(toggleRibbon_);displayMenu_->addAction(toggleRibbon_);
    body_=new QStackedWidget(this);body_->setObjectName("toolstripBody");body_->setFixedHeight(bodyHeight);body_->setMinimumWidth(0);layout_->addWidget(body_);
    popup_=new QFrame(this,Qt::Popup);popup_->setObjectName("toolstripPopup");popup_->setFrameShape(QFrame::StyledPanel);popup_->installEventFilter(this);popupLayout_=new QVBoxLayout(popup_);popupLayout_->setContentsMargins(1,0,1,1);popupLayout_->setSpacing(0);
    installEventFilter(this);
    setStyleSheet(QStringLiteral(
        "QWidget#studioToolstrip {background:#f4f5f7;}"
        "QWidget#toolstripHeader {background:#185b89;}"
        "QTabBar#toolstripTabs {background:transparent;}"
        "QTabBar#toolstripTabs::tab {background:transparent;color:white;border:0;padding:5px 15px;margin:0;}"
        "QTabBar#toolstripTabs::tab:selected {background:#f4f5f7;color:#194e73;}"
        "QTabBar#toolstripTabs::tab:hover:!selected {background:#34739d;}"
        "QToolButton#toolstripPin {color:white;background:transparent;border:0;}"
        "QToolButton#toolstripPin:hover {background:#34739d;}"
        "QFrame#toolstripPopup {background:#f4f5f7;border:1px solid #aebac5;}"
        "QWidget#toolstripPage {background:#f4f5f7;}"
        "QWidget#toolstripGroup {background:transparent;border:0;border-right:1px solid #d0d6dd;}"
        "QLabel#toolstripGroupLabel {color:#526473;background:transparent;font-size:9px;border:0;}"
        "QToolButton#toolstripCommand {background:transparent;border:1px solid transparent;border-radius:2px;padding:2px 5px;color:#182d3b;}"
        "QToolButton#toolstripCommand:hover {background:#e4edf5;border-color:#b6cddd;}"
        "QToolButton#toolstripCommand:pressed,QToolButton#toolstripCommand:checked {background:#d1e2ef;border-color:#7fabc8;}"
        "QToolButton#toolstripCommand:disabled {color:#8d969d;}"
        "QScrollArea#toolstripScroll {background:#f4f5f7;border:0;}"
    ));
    connect(pin_,&QToolButton::clicked,this,[this](bool checked){setPinned(checked);});
    connect(tabs_,&QTabBar::currentChanged,this,[this](int page){body_->setCurrentIndex(page);if(currentPageChanged)currentPageChanged(page);});
    connect(tabs_,&QTabBar::tabBarClicked,this,[this](int page){if(page<0)return;setCurrentPage(page);if(!pinned_)setPopupVisible(true);});
    connect(tabs_,&QTabBar::tabBarDoubleClicked,this,[this](int page){if(page>=0)setPinned(!pinned_);});
    pin_->setText(QString::fromUtf8("⌃"));pin_->setToolTip("Minimise Ribbon (Ctrl+F1 or double-click a tab)");pin_->setAccessibleName("Minimise Ribbon");
    watchContextMenus(header_);watchContextMenus(body_);watchContextMenus(popup_);
}

Toolstrip::~Toolstrip(){if(applicationFilter_&&qApp)qApp->removeEventFilter(this);}

void Toolstrip::watchContextMenus(QWidget* root){
    root->installEventFilter(this);for(auto* child:root->findChildren<QWidget*>())child->installEventFilter(this);
}
bool Toolstrip::handlesRibbonContext(QWidget* target)const{
    if(!target||(target!=this&&!isAncestorOf(target)&&target!=popup_&&!popup_->isAncestorOf(target)))return false;
    // Text editing and open command menus keep their own context behavior.
    if(headerAccessory_&&(target==headerAccessory_||headerAccessory_->isAncestorOf(target)))return false;
    for(auto* ancestor=target;ancestor;ancestor=ancestor->parentWidget()){
        if(qobject_cast<QMenu*>(ancestor)||qobject_cast<QLineEdit*>(ancestor))return false;
        if(ancestor==this||ancestor==popup_)break;
    }
    return true;
}

void Toolstrip::setHeaderAccessory(QWidget* widget){
    if(headerAccessory_==widget)return;
    auto* row=static_cast<QHBoxLayout*>(header_->layout());
    if(headerAccessory_){row->removeWidget(headerAccessory_);headerAccessory_->hide();}
    headerAccessory_=widget;if(widget){widget->setParent(header_);row->insertWidget(row->indexOf(pin_),widget,0,Qt::AlignVCenter);}
}

int Toolstrip::addPage(const QString& title){
    auto* contents=new QWidget;contents->setObjectName("toolstripPage");auto* row=new QHBoxLayout(contents);row->setContentsMargins(5,3,5,2);row->setSpacing(4);row->addStretch(1);
    auto* scroll=new QScrollArea(body_);scroll->setObjectName("toolstripScroll");scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);scroll->setWidget(contents);scroll->setFocusPolicy(Qt::NoFocus);
    pages_.push_back({contents,row,{}});body_->addWidget(scroll);watchContextMenus(scroll);return tabs_->addTab(title);
}

Toolstrip::Group& Toolstrip::ensureGroup(int page,const QString& title){
    auto& target=pages_.at(static_cast<std::size_t>(page));
    for(auto& group:target.groups)if(group.title==title)return group;
    auto* widget=new QWidget(target.contents);widget->setObjectName("toolstripGroup");auto* column=new QVBoxLayout(widget);column->setContentsMargins(4,0,8,0);column->setSpacing(1);
    auto* commands=new QHBoxLayout;commands->setContentsMargins(0,0,0,0);commands->setSpacing(2);column->addLayout(commands,1);
    auto* label=new QLabel(title,widget);label->setObjectName("toolstripGroupLabel");label->setAlignment(Qt::AlignHCenter);label->setFixedHeight(13);column->addWidget(label);
    target.layout->insertWidget(target.layout->count()-1,widget);target.groups.push_back({title,widget,commands});watchContextMenus(widget);return target.groups.back();
}

QToolButton* Toolstrip::addAction(int page,const QString& title,QAction* action,bool prominent){
    if(!action||page<0||static_cast<std::size_t>(page)>=pages_.size())return nullptr;
    auto& group=ensureGroup(page,title);auto* button=new QToolButton(group.widget);button->setObjectName("toolstripCommand");button->setFocusPolicy(Qt::TabFocus);button->setDefaultAction(action);button->setAccessibleName(action->text().remove('&'));
    if(action->icon().isNull())button->setIcon(commandIcon(button,action));
    button->setToolButtonStyle(prominent?Qt::ToolButtonTextUnderIcon:Qt::ToolButtonTextBesideIcon);button->setIconSize(prominent?QSize(24,24):QSize(16,16));
    if(prominent){button->setMinimumWidth(58);group.columns->addWidget(button,0,Qt::AlignTop);}
    else {
        if(!group.compact){group.compact=new QGridLayout;group.compact->setContentsMargins(0,0,0,0);group.compact->setHorizontalSpacing(3);group.compact->setVerticalSpacing(0);group.columns->addLayout(group.compact);}
        const auto index=group.compactCount++;group.compact->addWidget(button,index%2,index/2,Qt::AlignLeft|Qt::AlignVCenter);
    }
    // QToolButton already uses its default action's menu. Calling setMenu with
    // that same menu removes the shared default action in some Qt versions.
    if(action->menu()){
        button->setPopupMode(QToolButton::InstantPopup);
        connect(action->menu(),&QMenu::triggered,this,[this]{if(!pinned_)closePopup();});
    }
    connect(action,&QAction::triggered,this,[this]{if(!pinned_)closePopup();});
    connect(action,&QAction::changed,button,[button,action]{button->setAccessibleName(action->text().remove('&'));if(action->icon().isNull())button->setIcon(commandIcon(button,action));});
    watchContextMenus(button);return button;
}

void Toolstrip::setPinned(bool pinned){setDisplayMode(pinned?RibbonDisplay::Expanded:RibbonDisplay::TabsOnly);}
void Toolstrip::setDisplayMode(RibbonDisplay mode){
    if(mode!=RibbonDisplay::Expanded&&mode!=RibbonDisplay::TabsOnly&&mode!=RibbonDisplay::AutoHide)return;
    if(mode==RibbonDisplay::AutoHide)mode=RibbonDisplay::TabsOnly;
    if(displayMode_==mode){updateDisplay();return;}
    auto* focus=QApplication::focusWidget();if(focus&&!isAncestorOf(focus)&&!popup_->isAncestorOf(focus))previousFocus_=focus;
    closePopup();const bool oldPinned=pinned_;displayMode_=mode;pinned_=mode==RibbonDisplay::Expanded;updateDisplay();
    if(oldPinned!=pinned_&&pinnedChanged)pinnedChanged(pinned_);
    if(displayModeChanged)displayModeChanged(mode);
    restoreFocus();
}
void Toolstrip::updateDisplay(){
    pin_->setChecked(pinned_);pin_->setText(pinned_?QString::fromUtf8("⌃"):QString::fromUtf8("⌄"));
    pin_->setToolTip(pinned_?"Minimise Ribbon (Ctrl+F1 or double-click a tab)":"Restore Ribbon (Ctrl+F1 or double-click a tab)");pin_->setAccessibleName(pinned_?"Minimise Ribbon":"Restore Ribbon");
    toggleRibbon_->setText(pinned_?"Minimise Ribbon":"Restore Ribbon");toggleRibbon_->setIcon(studioCommandIcon(toggleRibbon_->text(),pinned_?"ribbonCollapse":"ribbonRestore"));
    toggleRibbon_->setChecked(!pinned_);
    header_->show();body_->setVisible(pinned_||popupVisible());updateGeometry();
}
void Toolstrip::setCurrentPage(int page){if(page>=0&&page<tabs_->count()){tabs_->setCurrentIndex(page);body_->setCurrentIndex(page);}}
int Toolstrip::currentPage()const{return tabs_->currentIndex();}
bool Toolstrip::popupVisible()const{return popup_->isVisible();}

void Toolstrip::placeBody(bool popup){
    reparenting_=true;
    if(popup){layout_->removeWidget(body_);body_->setParent(popup_);popupLayout_->addWidget(body_);}
    else {popupLayout_->removeWidget(body_);body_->setParent(this);layout_->addWidget(body_);}
    reparenting_=false;
}

void Toolstrip::setPopupVisible(bool visible){
    if(visible){
        if(pinned_||!isVisible()||pages_.empty())return;
        if(!popup_->isVisible()){auto* focus=QApplication::focusWidget();if(focus&&focus!=this&&!isAncestorOf(focus)&&!popup_->isAncestorOf(focus))previousFocus_=focus;placeBody(true);body_->show();positionPopup();popup_->show();popup_->raise();if(!applicationFilter_&&qApp){qApp->installEventFilter(this);applicationFilter_=true;}}
        else positionPopup();
    }else {
        if(popup_->isVisible())popup_->hide();
        if(applicationFilter_&&qApp){qApp->removeEventFilter(this);applicationFilter_=false;}
        if(body_->parentWidget()!=this)placeBody(false);
        updateDisplay();restoreFocus();
    }
}

void Toolstrip::positionPopup(){
    const auto top=mapToGlobal(QPoint(0,height()));auto* screen=QGuiApplication::screenAt(top);if(!screen)screen=QGuiApplication::primaryScreen();
    const auto available=screen?screen->availableGeometry():QRect(top,QSize(std::max(1,width()),bodyHeight+2));
    const int popupHeight=bodyHeight+2;
    const auto popupWidth=std::min(std::max(1,width()),available.width());const auto x=std::clamp(top.x(),available.left(),available.right()-popupWidth+1);
    auto y=top.y();if(y+popupHeight>available.bottom()+1)y=std::max(available.top(),mapToGlobal(QPoint(0,0)).y()-popupHeight);
    popup_->setGeometry(x,y,popupWidth,popupHeight);
}

void Toolstrip::restoreFocus(){
    auto* focus=QApplication::focusWidget();
    // Closing a command popup must not undo an explicit move into the header
    // search field, including the deferred close after its Hide event.
    if(headerAccessory_&&focus&&(focus==headerAccessory_||headerAccessory_->isAncestorOf(focus)))return;
    if(previousFocus_&&previousFocus_->isVisible()&&(!focus||focus==this||isAncestorOf(focus)||popup_->isAncestorOf(focus))&&!QApplication::activeModalWidget())previousFocus_->setFocus(Qt::OtherFocusReason);
}

bool Toolstrip::eventFilter(QObject* watched,QEvent* event){
    if(!popup_)return QWidget::eventFilter(watched,event);
    // The collapsed commands live in a separate Qt::Popup window. Their
    // context events do not reliably propagate to Toolstrip::contextMenuEvent.
    if(event->type()==QEvent::ContextMenu&&handlesRibbonContext(qobject_cast<QWidget*>(watched))){contextMenuEvent(static_cast<QContextMenuEvent*>(event));return true;}
    if(watched==popup_&&event->type()==QEvent::Hide&&!reparenting_){QTimer::singleShot(0,this,[this]{if(!popup_->isVisible())setPopupVisible(false);});}
    if(watched==this&&event->type()==QEvent::Hide&&!reparenting_)closePopup();
    if(watched==tabs_&&event->type()==QEvent::KeyPress){auto* key=static_cast<QKeyEvent*>(event);if(!pinned_&&(key->key()==Qt::Key_Down||key->key()==Qt::Key_Return||key->key()==Qt::Key_Space)){setPopupVisible(true);for(auto* button:body_->currentWidget()->findChildren<QToolButton*>())if(button->isEnabled()&&button->isVisible()){button->setFocus(Qt::TabFocusReason);break;}return true;}}
    if(popup_->isVisible()){
        if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){closePopup();return true;}
        if(event->type()==QEvent::MouseButtonPress){auto* target=qobject_cast<QWidget*>(watched);bool inMenu=false;for(auto* ancestor=target;ancestor;ancestor=ancestor->parentWidget())if(qobject_cast<QMenu*>(ancestor)){inMenu=true;break;}if(target&&!inMenu&&target!=popup_&&!popup_->isAncestorOf(target)&&target!=this&&!isAncestorOf(target))closePopup();}
    }
    return QWidget::eventFilter(watched,event);
}

void Toolstrip::contextMenuEvent(QContextMenuEvent* event){displayMenu_->exec(event->globalPos());event->accept();}
void Toolstrip::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);if(popup_->isVisible())positionPopup();}
}
