// Adapted from GTG Cell Studio collapsible_dock.hpp. See GTG_COMPONENT_PROVENANCE.txt.
#pragma once
#include <QObject>
#include <QPointer>
#include <QByteArray>
#include <QDockWidget>
#include <QString>
#include <functional>
class QMainWindow;class QDockWidget;class QToolBar;class QToolButton;class QFrame;class QVBoxLayout;class QHBoxLayout;class QLabel;
namespace penta::desktop {
// Hiding the original dock preserves Qt's native placement and its contents.
// A separate slim rail frees its row/column without recreating dock contents.
class CollapsibleDockController final:public QObject {
public:
    CollapsibleDockController(QMainWindow*,QDockWidget*,const QString& key={});
    ~CollapsibleDockController()override;
    bool isCollapsed()const{return collapsed_;}
    void setCollapsed(bool);
    void collapse(){setCollapsed(true);}
    void restore(){setCollapsed(false);}
    void reset();
    void setAutoHide(bool);
    bool autoHide()const{return autoHide_;}
    void peek();
    void closeDrawer();
    // Copilot uses an auto-hidden dock that reflows the workspace when opened;
    // Project can retain its lightweight overlay drawer.
    void setReflowDrawer(bool enabled);
    void ensureReflowColumn();
    void setRailHost(QWidget*);
    // Logical panel visibility includes a minimized rail or an overlay drawer.
    // Showing an auto-hidden panel exposes its rail without opening the drawer.
    void setPanelVisible(bool);
    bool panelVisible()const;
    bool drawerOpen()const{return drawerOpen_;}
    QFrame* drawer()const{return drawer_;}
    void setUndockingEnabled(bool);
    bool undockingEnabled()const{return undockingEnabled_;}
    // Own an optional compact replacement for the title text in the existing
    // 24px header row. The panel controls keep their position. Replaced widgets
    // are deleted; nullptr restores the ordinary title.
    void setHeaderWidget(QWidget*);
    QWidget* headerWidget()const{return headerWidget_;}
    QByteArray saveState()const;
    bool restoreState(const QByteArray&);
    QToolBar* rail()const{return rail_;}
    QDockWidget* dock()const{return dock_;}
    int expandedWidth()const{return expandedWidth_;}
    int expandedHeight()const{return expandedHeight_;}
    std::function<void(bool)> collapsedChanged;
    std::function<void(bool)> autoHideChanged;
    std::function<void(bool)> panelVisibilityChanged;
protected:
    bool eventFilter(QObject*,QEvent*)override;
private:
    void applyExtent();
    void rememberExtent();
    void alignRail();
    void updateRailMetrics(bool vertical);
    void positionDrawer();
    void restoreDrawerContent();
    void updateControls();
    void hideExpandedPanelRail();
    void notifyPanelVisibility();
    void scheduleReflowColumn();
    QPointer<QMainWindow> window_;QPointer<QDockWidget> dock_;
    QToolBar* rail_;QToolButton* minimize_;QToolButton* float_;QToolButton* drawerFloat_=nullptr;QToolButton* pin_=nullptr;
    QToolButton* railRestore_=nullptr;QToolButton* railLabel_=nullptr;
    QFrame* drawer_;QVBoxLayout* drawerLayout_;QWidget* placeholder_;
    QPointer<QWidget> titleBar_,headerWidget_;
    QPointer<QLabel> titleLabel_;
    QHBoxLayout* titleLayout_=nullptr;
    QPointer<QWidget> drawerContent_,previousFocus_;
    QPointer<QWidget> railHost_;
    QString prefix_;
    Qt::DockWidgetArea defaultArea_=Qt::LeftDockWidgetArea;
    bool project_=false,sideDrawer_=false,autoHide_=false,drawerOpen_=false,applicationFilter_=false,undockingEnabled_=false,lastPanelVisible_=false;
    bool collapsed_=false,changing_=false,reflowDrawer_=false;int expandedWidth_=245,expandedHeight_=245;
    bool columnRepairPending_=false,columnRepairing_=false;
};
}
