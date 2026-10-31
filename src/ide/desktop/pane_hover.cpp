// Adapted from GTG Cell Studio pane_hover.cpp. See GTG_COMPONENT_PROVENANCE.txt.
#include "ide/desktop/pane_hover.hpp"
#include <QAbstractButton>
#include <QApplication>
#include <QColor>
#include <QCursor>
#include <QDialog>
#include <QDockWidget>
#include <QEvent>
#include <QIcon>
#include <QImage>
#include <QMainWindow>
#include <QPair>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVariant>
#include <QVector>
#include <algorithm>

namespace penta::desktop {
namespace {
constexpr auto activeProperty="studioPaneHoverActive";
constexpr auto selectedTabOnlyProperty="studioPaneHoverSelectedTabOnly";
constexpr auto persistentSelectedTabProperty="studioPanePersistentSelectedTab";
constexpr auto managerName="studioPaneHoverManager";
const QString styleBegin=QStringLiteral("\n/* Studio pane hover begin */\n");
const QString styleEnd=QStringLiteral("\n/* Studio pane hover end */\n");
QString withoutAccent(QString style){
    const auto begin=style.indexOf(styleBegin);
    if(begin>=0){const auto end=style.indexOf(styleEnd,begin);if(end>=0)style.remove(begin,end+styleEnd.size()-begin);}
    return style;
}
QString accentStyle(bool nativeDock,bool selectedTabOnly){
    if(nativeDock)return QStringLiteral("QDockWidget[studioPaneHoverActive=\"true\"]::title {background:#0b6573;color:#f5fcfd;}");
    if(selectedTabOnly)return QStringLiteral(
        "QTabBar[studioPaneHoverActive=\"true\"]::tab:selected {background:#147b88;color:white;}"
        "QTabBar[studioPaneHoverActive=\"true\"]::tab:selected:hover {background:#238996;color:white;}"
    );
    return QStringLiteral(
        "*[studioPaneHoverActive=\"true\"] {background:#0b6573;color:#f5fcfd;}"
        "*[studioPaneHoverActive=\"true\"] QLabel {background:transparent;color:#f5fcfd;}"
        "*[studioPaneHoverActive=\"true\"] QAbstractButton {background:transparent;color:#f5fcfd;}"
        "*[studioPaneHoverActive=\"true\"] QAbstractButton:hover {background:#197b88;}"
        "*[studioPaneHoverActive=\"true\"] QAbstractButton:pressed {background:#084a57;}"
        "*[studioPaneHoverActive=\"true\"] QAbstractButton:disabled {color:#a4c4c9;}"
        "*[studioPaneHoverActive=\"true\"] QTabBar {background:transparent;}"
        "*[studioPaneHoverActive=\"true\"] QTabBar::tab,QTabBar[studioPaneHoverActive=\"true\"]::tab {background:#0b6573;color:#e2f2f4;}"
        "*[studioPaneHoverActive=\"true\"] QTabBar::tab:selected,QTabBar[studioPaneHoverActive=\"true\"]::tab:selected {background:#147b88;color:white;}"
        "*[studioPaneHoverActive=\"true\"] QTabBar::tab:hover,QTabBar[studioPaneHoverActive=\"true\"]::tab:hover {background:#238996;color:white;}"
        "*[studioPaneHoverActive=\"true\"] QTabBar::tab:disabled,QTabBar[studioPaneHoverActive=\"true\"]::tab:disabled {color:#a4c4c9;}"
    );
}
// Retain semantic glyphs, removing pale backing instead of making opaque tiles.
QIcon lightIcon(const QIcon& source){
    if(source.isNull())return {};
    QIcon result;
    for(const auto mode:{QIcon::Normal,QIcon::Active,QIcon::Selected,QIcon::Disabled}){
        auto image=source.pixmap(QSize(32,32),mode).toImage().convertToFormat(QImage::Format_ARGB32);
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){
            const auto pixel=image.pixel(x,y);
            const auto alpha=qAlpha(pixel)*std::min(1.,(255.-qGray(pixel))/100.);
            const QColor ink=mode==QIcon::Disabled?QColor("#a4c4c9"):QColor("#effcfd");
            image.setPixel(x,y,qRgba(ink.red(),ink.green(),ink.blue(),qRound(alpha)));
        }
        auto pixmap=QPixmap::fromImage(image);pixmap.setDevicePixelRatio(2);result.addPixmap(pixmap,mode);
    }
    return result;
}
struct ButtonIcon {QPointer<QAbstractButton> button;QIcon original,light;};
struct TabIcon {QIcon original,light;};
struct TabIcons {QPointer<QTabBar> tabs;QVector<TabIcon> icons;};
struct Entry {
    QPointer<QWidget> header;
    QVector<QPointer<QWidget>> panes;
    bool nativeDock=false,active=false;
    QVariant originalProperty;
    bool originalStyledBackground=false;
    QVector<ButtonIcon> buttons;
    QVector<TabIcons> tabs;
};
class PaneHoverManager final:public QObject {
public:
    explicit PaneHoverManager(QWidget* window):QObject(window),window_(window){
        setObjectName(managerName);scanTimer_.setSingleShot(true);scanTimer_.setInterval(70);
        connect(&scanTimer_,&QTimer::timeout,this,[this]{scan();});
        refreshTimer_.setSingleShot(true);refreshTimer_.setInterval(0);
        connect(&refreshTimer_,&QTimer::timeout,this,[this]{refresh();});
    }
    void add(QWidget* pane,QWidget* header,bool nativeDock=false){
        if(!pane||!header)return;
        for(auto& entry:entries_)if(entry.header==header){if(!entry.panes.contains(pane))entry.panes.push_back(pane);return;}
        Entry entry;entry.header=header;entry.panes.push_back(pane);entry.nativeDock=nativeDock;entries_.push_back(std::move(entry));
    }
    void registerPane(QWidget* pane,QWidget* header){
        if(!pane||!header)return;
        const auto duplicate=std::any_of(manual_.cbegin(),manual_.cend(),[&](const auto& binding){return binding.first==pane&&binding.second==header;});
        if(!duplicate)manual_.push_back({pane,header});
        scan();
    }
    void scan(){
        if(!window_)return;
        for(auto& entry:entries_)entry.panes.clear();
        manual_.erase(std::remove_if(manual_.begin(),manual_.end(),[](const auto& binding){return !binding.first||!binding.second;}),manual_.end());
        for(const auto& binding:manual_)add(binding.first,binding.second);
        watch(window_);
        const auto widgets=window_->findChildren<QWidget*>();for(auto* widget:widgets)watch(widget);
        const auto docks=window_->findChildren<QDockWidget*>();
        for(auto* dock:docks){
            if(inDialog(dock))continue;
            auto* title=dock->titleBarWidget();
            if(!title)add(dock,dock,true);else if(title->maximumHeight()!=0)add(dock,title);
        }
        for(auto* widget:widgets){
            if(inDialog(widget))continue;
            const auto name=widget->objectName();
            if(name.endsWith("DrawerTitle"))for(auto* parent=widget->parentWidget();parent;parent=parent->parentWidget())if(parent->objectName().endsWith("Drawer")){add(parent,widget);break;}
            if(auto* rail=qobject_cast<QToolBar*>(widget);rail&&(name.endsWith("MinimizedRail")||name.endsWith("EmbeddedRail")))add(rail,rail);
            if(auto* tabWidget=qobject_cast<QTabWidget*>(widget))add(tabWidget,tabWidget->tabBar());
            auto* tabs=qobject_cast<QTabBar*>(widget);if(!tabs)continue;
            auto* host=qobject_cast<QMainWindow*>(tabs->parentWidget());if(!host)continue;
            for(auto* dock:docks){
                if(dock->parentWidget()!=host)continue;
                for(int i=0;i<tabs->count();++i){
                    const auto identity=tabs->tabData(i).toULongLong();
                    if(identity==reinterpret_cast<quintptr>(dock)||(!identity&&tabs->tabText(i)==dock->windowTitle())){add(dock,tabs);break;}
                }
            }
        }
        entries_.erase(std::remove_if(entries_.begin(),entries_.end(),[](Entry& entry){
            if(!entry.header)return true;
            if(!entry.panes.isEmpty())return false;
            if(entry.active)setActive(entry,false);
            return true;
        }),entries_.end());
        if(!hover_){auto* current=QApplication::widgetAt(QCursor::pos());if(current&&(current==window_||window_->isAncestorOf(current)))hover_=current;}
        queueRefresh();
    }
protected:
    bool eventFilter(QObject* object,QEvent* event)override{
        auto* widget=qobject_cast<QWidget*>(object);if(!widget)return false;
        switch(event->type()){
        case QEvent::Enter:hover_=widget;queueRefresh();break;
        case QEvent::Leave:
            // Qt need not send Enter to an already-hovered parent when the
            // pointer leaves a child for blank space within that same pane.
            if(hover_&&(hover_==widget||widget->isAncestorOf(hover_)))hover_=widget==window_?nullptr:widget->parentWidget();
            queueRefresh();break;
        case QEvent::ChildAdded:case QEvent::ParentChange:case QEvent::Show:case QEvent::Hide:
            if(!scanTimer_.isActive())scanTimer_.start();
            queueRefresh();break;
        case QEvent::LayoutRequest:
            // Dock/tab rearrangement changes bindings. Ordinary content layout
            // (including a live log or plot) never triggers a tree rescan.
            if((qobject_cast<QMainWindow*>(widget)||qobject_cast<QTabBar*>(widget))&&!scanTimer_.isActive())scanTimer_.start();
            break;
        default:break;
        }
        return false;
    }
private:
    static bool inDialog(QWidget* widget){for(auto* current=widget;current;current=current->parentWidget())if(qobject_cast<QDialog*>(current))return true;return false;}
    void watch(QWidget* widget){
        if(watched_.contains(widget))return;
        watched_.insert(widget);widget->installEventFilter(this);
        if(auto* tabs=qobject_cast<QTabBar*>(widget))connect(tabs,&QTabBar::currentChanged,this,[this](int){queueRefresh();});
        connect(widget,&QObject::destroyed,this,[this,widget]{watched_.remove(widget);});
    }
    void queueRefresh(){if(!refreshTimer_.isActive())refreshTimer_.start();}
    void refresh(){
        QDockWidget* hoveredDock=nullptr;
        for(auto* widget=hover_.data();widget;widget=widget->parentWidget())if((hoveredDock=qobject_cast<QDockWidget*>(widget)))break;
        for(auto& entry:entries_){
            if(!entry.header)continue;
            // About keeps its selected page identifiable even when the mouse
            // leaves the dialog. This is explicitly opt-in; workspace panels
            // continue to accent only while the pointer is inside them.
            bool active=entry.header->isVisible()&&entry.header->property(selectedTabOnlyProperty).toBool()&&entry.header->property(persistentSelectedTabProperty).toBool();
            if(!active&&hover_&&entry.header->isVisible()){
                active=entry.header==hover_||(!entry.nativeDock&&entry.header->isAncestorOf(hover_));
                for(auto& pane:entry.panes)if(pane&&pane->isVisible()&&(pane==hover_||pane->isAncestorOf(hover_))){
                    // Model outline remains inside Project, including its
                    // drawer. A nested dock owns its hover independently of
                    // the enclosing dock/drawer; workspace tabs keep theirs.
                    if(hoveredDock&&pane!=hoveredDock&&pane->isAncestorOf(hoveredDock)&&
                       (qobject_cast<QDockWidget*>(pane.data())||pane->objectName().endsWith("Drawer")))continue;
                    active=true;break;
                }
            }
            if(active!=entry.active)setActive(entry,active);else if(active)syncIcons(entry);
        }
    }
    static void syncTabs(TabIcons& record){
        if(!record.tabs)return;
        QVector<TabIcon> current;current.reserve(record.tabs->count());
        for(int i=0;i<record.tabs->count();++i){
            const auto icon=record.tabs->tabIcon(i);
            const auto previous=std::find_if(record.icons.cbegin(),record.icons.cend(),[&](const TabIcon& value){return value.light.cacheKey()==icon.cacheKey()||value.original.cacheKey()==icon.cacheKey();});
            if(previous!=record.icons.cend())current.push_back(*previous);else current.push_back({icon,lightIcon(icon)});
            const bool tinted=!record.tabs->property(selectedTabOnlyProperty).toBool()||i==record.tabs->currentIndex();
            const auto desired=tinted?current.last().light:current.last().original;
            if(record.tabs->tabIcon(i).cacheKey()!=desired.cacheKey())record.tabs->setTabIcon(i,desired);
        }
        record.icons=std::move(current);
    }
    static void syncIcons(Entry& entry){
        for(auto& button:entry.buttons){if(!button.button)continue;const auto icon=button.button->icon();if(icon.cacheKey()!=button.light.cacheKey()){button.original=icon;button.light=lightIcon(icon);button.button->setIcon(button.light);}}
        for(auto& tabs:entry.tabs)syncTabs(tabs);
    }
    static void setActive(Entry& entry,bool active){
        auto* header=entry.header.data();if(!header)return;entry.active=active;
        // Inner figure tabs have their own identity colours. Let their painter
        // strengthen the selected colour on pane entry; the generic teal CSS
        // and white icon tint would erase those distinctions.
        if(header->property("studioFigureTabs").toBool()){
            if(active)entry.originalProperty=header->property(activeProperty);
            header->setProperty(activeProperty,active?QVariant(true):entry.originalProperty);header->update();return;
        }
        if(active){
            entry.originalProperty=header->property(activeProperty);entry.originalStyledBackground=header->testAttribute(Qt::WA_StyledBackground);
            header->setProperty(activeProperty,true);
            header->setStyleSheet(withoutAccent(header->styleSheet())+styleBegin+accentStyle(entry.nativeDock,header->property(selectedTabOnlyProperty).toBool())+styleEnd);
            if(!entry.nativeDock)header->setAttribute(Qt::WA_StyledBackground,true);
            const auto buttons=header->findChildren<QAbstractButton*>();
            for(auto* button:buttons){if(entry.nativeDock&&button->parentWidget()!=header)continue;const auto original=button->icon();if(original.isNull())continue;entry.buttons.push_back({button,original,lightIcon(original)});button->setIcon(entry.buttons.last().light);}
            auto tabs=header->findChildren<QTabBar*>();if(auto* self=qobject_cast<QTabBar*>(header))tabs.prepend(self);
            for(auto* bar:tabs){entry.tabs.push_back({bar,{}});syncTabs(entry.tabs.last());}
        }else{
            header->setProperty(activeProperty,entry.originalProperty);header->setStyleSheet(withoutAccent(header->styleSheet()));
            if(!entry.nativeDock)header->setAttribute(Qt::WA_StyledBackground,entry.originalStyledBackground);
            for(auto& button:entry.buttons)if(button.button&&button.button->icon().cacheKey()==button.light.cacheKey())button.button->setIcon(button.original);
            for(auto& record:entry.tabs){if(!record.tabs)continue;for(int i=0;i<record.tabs->count();++i){const auto current=record.tabs->tabIcon(i).cacheKey();const auto icon=std::find_if(record.icons.cbegin(),record.icons.cend(),[&](const TabIcon& value){return value.light.cacheKey()==current;});if(icon!=record.icons.cend())record.tabs->setTabIcon(i,icon->original);}}
            entry.buttons.clear();entry.tabs.clear();
        }
        header->update();
    }
    QPointer<QWidget> window_;
    QPointer<QWidget> hover_;
    QSet<QWidget*> watched_;
    QVector<Entry> entries_;
    QVector<QPair<QPointer<QWidget>,QPointer<QWidget>>> manual_;
    QTimer scanTimer_,refreshTimer_;
};
PaneHoverManager* managerFor(QWidget* widget){
    // Explicit registrations in a dialog belong to that dialog, rather than
    // the main workspace. Ordinary dialogs remain excluded from discovery.
    QWidget* window=nullptr;for(auto* current=widget;current;current=current->parentWidget()){
        if(auto* dialog=qobject_cast<QDialog*>(current)){window=dialog;break;}
        if(auto* main=qobject_cast<QMainWindow*>(current))window=main;
    }
    if(!window)return nullptr;
    for(auto* child:window->children())if(child->objectName()==managerName)if(auto* manager=dynamic_cast<PaneHoverManager*>(child))return manager;
    return new PaneHoverManager(window);
}
}
void installStudioPaneHover(QMainWindow* window){if(window)if(auto* manager=managerFor(window))manager->scan();}
void registerStudioPaneHover(QWidget* pane,QWidget* header){if(auto* manager=managerFor(pane))manager->registerPane(pane,header);}
bool studioPaneHoverActive(const QWidget* widget){for(auto* current=widget;current;current=current->parentWidget())if(current->property(activeProperty).toBool())return true;return false;}
}
