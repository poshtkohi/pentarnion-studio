// Adapted from GTG Cell Studio figure_docking.cpp. See GTG_COMPONENT_PROVENANCE.txt.
#include "ide/desktop/figure_docking.hpp"
#include <QCloseEvent>
#include <QDockWidget>
#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QMouseEvent>
#include <QMenuBar>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVariant>
#include <QWindow>
#include <QGuiApplication>
#include <QScopedValueRollback>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace penta::desktop {
namespace {
QIcon dockControlIcon(bool dock){
    QPixmap image(20,20);image.fill(Qt::transparent);QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(QColor("#4d6172"),1.4));painter.drawRect(QRectF(2.5,6.5,11,11));painter.drawLine(QPointF(2.5,9.5),QPointF(13.5,9.5));painter.setPen(QPen(QColor("#287bb5"),1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    if(dock){painter.drawLine(QPointF(17,2),QPointF(9,10));painter.drawLine(QPointF(9,5),QPointF(9,10));painter.drawLine(QPointF(9,10),QPointF(14,10));}
    else{painter.drawLine(QPointF(9,10),QPointF(17,2));painter.drawLine(QPointF(12,2),QPointF(17,2));painter.drawLine(QPointF(17,2),QPointF(17,7));}return QIcon(image);
}
class FloatingFigure final:public QMainWindow {
public:
    FloatingFigure(QWidget* owner,FigureDockController* controller):QMainWindow(owner,Qt::Window),controller_(controller){
        setObjectName("studioUndockedFigureWindow");
        setAttribute(Qt::WA_DeleteOnClose,false);
        setAttribute(Qt::WA_QuitOnClose,false);
        setMinimumSize(360,240);
    }
protected:
    void closeEvent(QCloseEvent* event) override {
        // Closing a figure returns it to Studio; no data or live recipient is
        // deleted by the native window's close button.
        if(controller_&&controller_->isUndocked()&&controller_->dock())event->ignore();
        else QMainWindow::closeEvent(event);
    }
private:
    QPointer<FigureDockController> controller_;
};
void reserveTab(QTabWidget* tabs,int index){
    if(index<0)return;
    tabs->tabBar()->setTabButton(index,QTabBar::LeftSide,nullptr);
    tabs->tabBar()->setTabButton(index,QTabBar::RightSide,nullptr);
}
void revealHostPages(QWidget* content){
    for(auto* ancestor=content->parentWidget();ancestor;ancestor=ancestor->parentWidget()){
        if(auto* tabs=qobject_cast<QTabWidget*>(ancestor)){
            for(int index=0;index<tabs->count();++index){
                auto* page=tabs->widget(index);
                if(page==content||page->isAncestorOf(content)){
                    tabs->setCurrentIndex(index);
                    break;
                }
            }
        }
    }
}
void releaseTransientOwner(QWidget* window){
    // QObject ownership still follows Studio, but an independent figure must
    // not be a tool window permanently stacked above the editor/other figures.
    // Create the Windows handle while hidden so its Shell role is correct on
    // the first show, rather than changing an owned/tool window afterward.
#ifdef Q_OS_WIN
    if(QGuiApplication::platformName()=="windows"&&!window->windowHandle())
        (void)window->winId();
#endif
    if(auto* handle=window->windowHandle()){
        handle->setTransientParent(nullptr);
#ifdef Q_OS_WIN
        // The Windows QWidget backend also derives GW_OWNER from the widget
        // parent, independently of QWindow::transientParent. Clear only that
        // native stacking owner; QObject ownership and the dock's saved slot
        // must remain intact for redocking and Studio shutdown.
        if(QGuiApplication::platformName()=="windows"){
            const auto hwnd=reinterpret_cast<HWND>(handle->winId());
            if(hwnd&&GetWindow(hwnd,GW_OWNER)){
                SetLastError(ERROR_SUCCESS);
                const auto previous=SetWindowLongPtrW(hwnd,GWLP_HWNDPARENT,0);
                const auto error=GetLastError();
                if(!previous&&error!=ERROR_SUCCESS)
                    qWarning("Studio figure could not release its native stacking owner (Windows error %lu)",static_cast<unsigned long>(error));
            }
            if(hwnd){
                // Floating plots are ordinary application windows in the
                // existing Studio process, so the Shell can group their
                // thumbnails with its main window. Keep all unrelated styles.
                const auto extended=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
                const auto desired=(extended|WS_EX_APPWINDOW)&~LONG_PTR(WS_EX_TOOLWINDOW);
                if(extended!=desired){
                    SetLastError(ERROR_SUCCESS);
                    const auto previous=SetWindowLongPtrW(hwnd,GWL_EXSTYLE,desired);
                    const auto error=GetLastError();
                    if(!previous&&error!=ERROR_SUCCESS)
                        qWarning("Studio figure could not set its taskbar role (Windows error %lu)",static_cast<unsigned long>(error));
                    else if(!SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED))
                        qWarning("Studio figure could not refresh its native frame (Windows error %lu)",static_cast<unsigned long>(GetLastError()));
                }
            }
        }
#endif
    }
}
bool hasNativeWindowState(QWidget* window){
    if(window->isMinimized()||window->isMaximized()||window->isFullScreen())return true;
#ifdef Q_OS_WIN
    // WM_SIZE/WM_MOVE can precede Qt's WindowStateChange notification. Do not
    // resize or recreate the native window inside that transition.
    if(QGuiApplication::platformName()=="windows")if(auto* handle=window->windowHandle()){
        const auto hwnd=reinterpret_cast<HWND>(handle->winId());
        if(hwnd&&(IsIconic(hwnd)||IsZoomed(hwnd)))return true;
    }
#endif
    return false;
}
bool intersectsScreen(const QRect& rectangle){
    for(auto* screen:QGuiApplication::screens())if(screen->availableGeometry().intersects(rectangle))return true;
    return false;
}
void retainVisibleGeometry(QWidget* window,QRect& lastNormal){
    if(!window||!window->isVisible()||hasNativeWindowState(window))return;
    if(intersectsScreen(window->frameGeometry())){lastNormal=window->geometry();return;}
    // A native restore must not strand a still-live figure outside every
    // display. Retain legitimate partially visible/multi-screen placement.
    QRect next=lastNormal;
    if(!next.isValid()||!intersectsScreen(next)){
        auto* screen=window->screen();if(!screen)screen=QGuiApplication::primaryScreen();if(!screen)return;
        const auto available=screen->availableGeometry();
        const auto size=window->size().expandedTo(QSize(360,240)).boundedTo(available.size());
        next=QRect(QPoint(),size);next.moveCenter(available.center());
    }
    window->setGeometry(next);lastNormal=next;
}
}
void normalizeIndependentFigureWindow(QWidget* window){
    if(!window)return;
    auto flags=window->windowFlags();
    flags&=~(Qt::WindowType_Mask|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowStaysOnBottomHint|Qt::X11BypassWindowManagerHint);
    flags|=Qt::Window|Qt::WindowTitleHint|Qt::WindowSystemMenuHint|Qt::WindowMinMaxButtonsHint|Qt::WindowCloseButtonHint;
    if(flags!=window->windowFlags()&&!hasNativeWindowState(window)){
        const bool visible=window->isVisible();const auto geometry=window->geometry();
        window->setWindowFlags(flags);window->setGeometry(geometry);releaseTransientOwner(window);if(visible)window->show();
    }
    window->setAttribute(Qt::WA_QuitOnClose,false);releaseTransientOwner(window);
}
bool isFloatingCaptionControl(QWidget* window,const QMouseEvent* event){
    if(!window||!event)return false;
#ifdef Q_OS_WIN
    if(QGuiApplication::platformName()=="windows")if(auto* handle=window->windowHandle()){
        const auto hwnd=reinterpret_cast<HWND>(handle->winId());POINT origin{};
        if(!hwnd||!ClientToScreen(hwnd,&origin))return false;
        const auto local=window->mapFromGlobal(event->globalPosition());const auto ratio=window->devicePixelRatioF();
        const auto hit=SendMessageW(hwnd,WM_NCHITTEST,0,MAKELPARAM(origin.x+qRound(local.x()*ratio),origin.y+qRound(local.y()*ratio)));
        return hit==HTMINBUTTON||hit==HTMAXBUTTON||hit==HTCLOSE||hit==HTSYSMENU;
    }
#else
    Q_UNUSED(window);Q_UNUSED(event);
#endif
    return false;
}
FigureDockController::FigureDockController(QWidget* content,const QString& title,const QString& action_name)
    :QObject(content),content_(content),toggle_(new QAction(this)),title_(title){
    dock_icon_=dockControlIcon(true);undock_icon_=dockControlIcon(false);
    toggle_->setObjectName(action_name);
    connect(toggle_,&QAction::triggered,this,[this]{if(isUndocked())dock();else undock();});
    content->installEventFilter(this);
    updateAction();
}
FigureDockController::~FigureDockController(){
    // deleteLater is safe both for explicit content deletion and while its
    // owning native window is itself destroying the central widget.
    if(floating_)floating_->deleteLater();
    if(placeholder_)placeholder_->deleteLater();
}
bool FigureDockController::isUndocked() const {return dock_container_?dock_container_->isFloating():!floating_.isNull();}
QWidget* FigureDockController::floatingWindow() const {if(dock_container_)return dock_container_->isFloating()?dock_container_.data():nullptr;return floating_.data();}
void FigureDockController::setFigureActions(const QList<QAction*>& actions){figure_actions_=actions;}
void FigureDockController::setDockContainer(QDockWidget* dock){
    if(floating_)this->dock();
    if(dock_container_){restoreDockTitle();dock_container_->removeEventFilter(this);disconnect(dock_container_,nullptr,this,nullptr);}
    dock_container_=dock;
    dock_home_=dock?qobject_cast<QMainWindow*>(dock->parentWidget()):nullptr;
    if(dock){
        dock->installEventFilter(this);
        connect(dock,&QDockWidget::topLevelChanged,this,[this](bool){
            // Qt can emit this while QMainWindow::restoreState is traversing
            // its dock layout. Defer frame/title mutations until that traversal
            // returns; changing window flags here can invalidate its items.
            scheduleFloatingNormalization();
            updateAction();
        });
    }
    updateAction();
}
void FigureDockController::releaseDockTitle(){
    if(!dock_container_)return;
    auto* title=dock_container_->titleBarWidget();if(!title)return;
    // Preserve the compact/custom header for redocking. Leaving any custom
    // title installed makes QDockWidget choose a frameless floating window.
    dock_title_=title;title->hide();dock_container_->setTitleBarWidget(nullptr);
}
void FigureDockController::restoreDockTitle(){
    if(dock_container_&&dock_title_&&!dock_container_->isFloating()){
        auto* title=dock_title_.data();dock_title_.clear();dock_container_->setTitleBarWidget(title);title->show();
    }
}
void FigureDockController::normalizeFloatingDock(){
    if(!dock_container_||!dock_container_->isFloating()||normalizingDock_)return;
    QScopedValueRollback<bool> guard(normalizingDock_,true);
    auto* dock=dock_container_.data();
    dock->setAttribute(Qt::WA_QuitOnClose,false);
    if(hasNativeWindowState(dock)){
        // setWindowFlags hides a QWidget and can reset a pending native
        // minimize/maximize operation. Normalize its frame after restoration.
        releaseTransientOwner(dock);return;
    }
    // A late-created figure can inherit a 120-pixel placeholder geometry.
    // Its native system buttons then consume the entire caption. Adjust only
    // floating geometry: minimum constraints on a QDockWidget/content can be
    // retained by Qt's dock layout and enlarge compact tiles after redocking.
    if(!dock->isMinimized()&&!dock->isMaximized()&&!dock->isFullScreen())
        dock->resize(dock->size().expandedTo(QSize(360,240)));
    const bool visible=dock->isVisible();
    const auto geometry=dock->geometry();
    releaseDockTitle();
    auto flags=dock->windowFlags();
    flags&=~(Qt::WindowType_Mask|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowStaysOnBottomHint|Qt::X11BypassWindowManagerHint);
    flags|=Qt::Window|Qt::WindowTitleHint|Qt::WindowSystemMenuHint|Qt::WindowMinMaxButtonsHint|Qt::WindowCloseButtonHint;
    if(flags!=dock->windowFlags()){
        dock->setWindowFlags(flags);
        dock->setGeometry(geometry);
        releaseTransientOwner(dock);
        if(visible)dock->show();
    }
    releaseTransientOwner(dock);
    retainVisibleGeometry(dock,floating_normal_geometry_);
}
void FigureDockController::scheduleFloatingNormalization(){
    if(floatingNormalizationQueued_)return;
    floatingNormalizationQueued_=true;
    QTimer::singleShot(0,this,[this]{
        floatingNormalizationQueued_=false;
        // Direct native drags and restored layouts finish their platform show
        // after topLevelChanged/Show. Recheck once, without activation or data
        // publication, to remove any owner Qt reinstalled during that finish.
        if(dock_container_){
            if(dock_container_->isFloating())normalizeFloatingDock();
            else{
                restoreDockTitle();
                if(auto* host=qobject_cast<QMainWindow*>(dock_container_->parentWidget()))dock_home_=host;
            }
        }
        if(floating_){releaseTransientOwner(floating_);retainVisibleGeometry(floating_,floating_normal_geometry_);}
    });
}
void FigureDockController::updateAction(){
    const bool palette=content_&&content_->property("studioPaletteKey").isValid();
    const bool floating=isUndocked();const QString label=palette?(floating?"Return palette":"Float palette"):(floating?"Dock figure":"Undock figure");
    const QString description=palette?(floating?"Return this palette and its figures to Studio":"Float this palette with all of its figures"):(floating?"Return this figure to its host":"Move this figure to an independent window");
    // Cached icons and unchanged action values must not generate ActionChanged
    // repaint chains while Qt updates native window geometry or hover state.
    if(toggle_->text()!=label)toggle_->setText(label);
    if(toggle_->toolTip()!=label)toggle_->setToolTip(label);
    if(toggle_->statusTip()!=description)toggle_->setStatusTip(description);
    const auto& icon=floating?dock_icon_:undock_icon_;if(toggle_->icon().cacheKey()!=icon.cacheKey())toggle_->setIcon(icon);
    const bool enabled=content_&&(floating||content_->parentWidget());if(toggle_->isEnabled()!=enabled)toggle_->setEnabled(enabled);
}
bool FigureDockController::undock(bool activateWindow){
    if(!content_||changing_)return false;
    if(dock_container_){
        const bool prior=dock_container_->testAttribute(Qt::WA_ShowWithoutActivating);
        if(!activateWindow)dock_container_->setAttribute(Qt::WA_ShowWithoutActivating);
        releaseDockTitle();
        dock_container_->setFloating(true);
        normalizeFloatingDock();
        dock_container_->show();
        releaseTransientOwner(dock_container_);
        if(activateWindow){dock_container_->raise();dock_container_->activateWindow();}
        dock_container_->setAttribute(Qt::WA_ShowWithoutActivating,prior);
        updateAction();
        return dock_container_->isFloating();
    }
    if(isUndocked()){if(activateWindow)present();return true;}
    home_parent_=content_->parentWidget();if(!home_parent_)return false;
    changing_=true;home_geometry_=content_->geometry();home_tabs_.clear();home_layout_.clear();home_index_=-1;
    for(auto* ancestor=home_parent_.data();ancestor;ancestor=ancestor->parentWidget())if(auto* tabs=qobject_cast<QTabWidget*>(ancestor);tabs&&tabs->indexOf(content_)>=0){home_tabs_=tabs;break;}
    if(home_tabs_){home_index_=home_tabs_->indexOf(content_);tab_title_=home_tabs_->tabText(home_index_);tab_icon_=home_tabs_->tabIcon(home_index_);tab_tooltip_=home_tabs_->tabToolTip(home_index_);}
    else home_layout_=home_parent_->layout();
    const auto figureTitle=content_->property("studioFigureTitle").toString().isEmpty()?title_:content_->property("studioFigureTitle").toString();
    auto* placeholder=new QWidget;placeholder->setObjectName("figureDockPlaceholder");placeholder->setProperty("studioFigureKey",content_->property("studioFigureKey"));
    placeholder->setProperty("studioPaletteKey",content_->property("studioPaletteKey"));
    auto* layout=new QVBoxLayout(placeholder);layout->addStretch();
    auto* message=new QLabel(figureTitle+" is in a separate window.",placeholder);message->setAlignment(Qt::AlignCenter);layout->addWidget(message);
    auto* show=new QPushButton("Show figure window",placeholder);show->setObjectName("showUndockedFigure");layout->addWidget(show,0,Qt::AlignHCenter);
    layout->addStretch();
    connect(show,&QPushButton::clicked,this,[this]{present();});placeholder_=placeholder;
    QWidget* owner=home_parent_->window();
    auto* window=new FloatingFigure(owner,this);window->setWindowTitle(figureTitle+" — Pentarnion Studio");window->setWindowIcon(content_->windowIcon().isNull()?owner->windowIcon():content_->windowIcon());
    window->resize(std::max(900,content_->width()),std::max(620,content_->height()));
    auto* figureMenu=window->menuBar()->addMenu("&Figure");for(auto* action:figure_actions_)if(action)figureMenu->addAction(action);
    auto* windowMenu=window->menuBar()->addMenu("&Window");windowMenu->addAction(toggle_);windowMenu->addAction("Close figure window",window,&QWidget::close);
    auto* desktopMenu=window->menuBar()->addMenu("&Desktop");desktopMenu->addAction(toggle_);
    auto* corner=new QToolButton(window->menuBar());corner->setObjectName("figureDockCornerButton");corner->setDefaultAction(toggle_);corner->setToolButtonStyle(Qt::ToolButtonIconOnly);corner->setIconSize({16,16});corner->setAutoRaise(true);corner->setAccessibleName(content_->property("studioPaletteKey").isValid()?"Return palette":"Dock figure");window->menuBar()->setCornerWidget(corner,Qt::TopRightCorner);window->setProperty("studioDockCornerProvided",true);
    if(home_tabs_){home_tabs_->removeTab(home_index_);home_tabs_->insertTab(home_index_,placeholder,tab_icon_,tab_title_);home_tabs_->setTabToolTip(home_index_,tab_tooltip_);reserveTab(home_tabs_,home_index_);home_tabs_->setCurrentIndex(home_index_);}
    else if(home_layout_){auto* item=home_layout_->replaceWidget(content_,placeholder);delete item;}
    else{placeholder->setParent(home_parent_);placeholder->setGeometry(home_geometry_);placeholder->show();}
    floating_=window;window->installEventFilter(this);window->setCentralWidget(content_);content_->show();if(!activateWindow)window->setAttribute(Qt::WA_ShowWithoutActivating);releaseTransientOwner(window);window->show();releaseTransientOwner(window);retainVisibleGeometry(window,floating_normal_geometry_);if(activateWindow){window->raise();window->activateWindow();}
    changing_=false;updateAction();return true;
}
bool FigureDockController::dock(){
    if(dock_container_){
        if(changing_)return false;
        QScopedValueRollback<bool> guard(changing_,true);
        auto* dock=dock_container_.data();
        // A native tab-group drag may temporarily reparent the dock. Keep the
        // owning palette as the fallback, without rebuilding a normal slot.
        if(dock_home_&&dock->parentWidget()!=dock_home_){
            dock->setParent(dock_home_);
            dock_home_->addDockWidget(Qt::LeftDockWidgetArea,dock);
        }
        dock->setFloating(false);
        if(dock->isFloating())return false;
        restoreDockTitle();
        revealHostPages(dock);
        dock->show();content_->show();dock->raise();
        auto* window=dock->window();
        if(window->isMinimized())window->showNormal();
        window->show();window->raise();window->activateWindow();
        updateAction();
        return true;
    }
    if(!content_||!floating_||changing_)return false;
    if(!home_tabs_&&!home_parent_)return false;
    changing_=true;auto* window=floating_.data();window->hide();window->takeCentralWidget();
    if(home_tabs_){
        int index=home_tabs_->indexOf(placeholder_);if(index<0)index=std::clamp(home_index_,0,home_tabs_->count());else home_tabs_->removeTab(index);
        home_tabs_->insertTab(index,content_,tab_icon_,tab_title_);home_tabs_->setTabToolTip(index,tab_tooltip_);reserveTab(home_tabs_,index);home_tabs_->setCurrentIndex(index);
    }else if(home_layout_){content_->setParent(home_parent_);auto* item=home_layout_->replaceWidget(placeholder_,content_);if(item)delete item;else home_layout_->addWidget(content_);}
    else{content_->setParent(home_parent_);content_->setGeometry(home_geometry_);}
    if(placeholder_){placeholder_->hide();placeholder_->deleteLater();placeholder_.clear();}
    content_->show();floating_.clear();changing_=false;updateAction();window->deleteLater();revealHostPages(content_);return true;
}
void FigureDockController::present(){
    if(dock_container_){
        if(dock_container_->isFloating()){
            normalizeFloatingDock();
            if(dock_container_->isMinimized())dock_container_->showNormal();
            dock_container_->show();releaseTransientOwner(dock_container_);
            dock_container_->raise();dock_container_->activateWindow();
        }else{
            revealHostPages(dock_container_);
            dock_container_->show();content_->show();dock_container_->raise();
        }
        return;
    }
    if(floating_){if(floating_->isMinimized())floating_->showNormal();else floating_->show();releaseTransientOwner(floating_);retainVisibleGeometry(floating_,floating_normal_geometry_);floating_->raise();floating_->activateWindow();return;}
    if(!content_)return;
    for(auto* ancestor=content_->parentWidget();ancestor;ancestor=ancestor->parentWidget())if(auto* tabs=qobject_cast<QTabWidget*>(ancestor)){for(int k=0;k<tabs->count();++k)if(tabs->widget(k)==content_||tabs->widget(k)->isAncestorOf(content_)){tabs->setCurrentIndex(k);break;}}
    content_->show();
}
bool FigureDockController::eventFilter(QObject* watched,QEvent* event){
    if(watched==content_&&event->type()==QEvent::ParentChange&&!changing_)updateAction();
    if(watched==dock_container_){
        if(dock_container_->isFloating()&&event->type()==QEvent::NonClientAreaMouseButtonDblClick&&static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton){
            // Windows owns caption double-click maximize/restore. Qt's dock
            // handler would otherwise redock this same figure as well.
            return true;
        }
        if(dock_container_->isFloating()&&(event->type()==QEvent::NonClientAreaMouseButtonPress||event->type()==QEvent::NonClientAreaMouseButtonDblClick)&&isFloatingCaptionControl(dock_container_,static_cast<QMouseEvent*>(event))){
            // Native caption buttons belong to Windows. QDockWidget otherwise
            // interprets their presses as the beginning of a dock drag.
            return true;
        }
        if(event->type()==QEvent::Show)scheduleFloatingNormalization();
        else if((event->type()==QEvent::Resize||event->type()==QEvent::Move||event->type()==QEvent::WindowStateChange||event->type()==QEvent::WinIdChange)&&dock_container_->isFloating()&&!normalizingDock_)scheduleFloatingNormalization();
        else if(event->type()==QEvent::ParentChange&&!dock_container_->isFloating()){
            if(auto* host=qobject_cast<QMainWindow*>(dock_container_->parentWidget()))dock_home_=host;
        }
    }
    if(watched==floating_&&(event->type()==QEvent::Show||event->type()==QEvent::Move||event->type()==QEvent::Resize||event->type()==QEvent::WindowStateChange||event->type()==QEvent::WinIdChange))scheduleFloatingNormalization();
    return QObject::eventFilter(watched,event);
}
}
