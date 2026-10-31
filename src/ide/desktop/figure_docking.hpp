// Adapted from GTG Cell Studio figure_docking.hpp. See GTG_COMPONENT_PROVENANCE.txt.
#pragma once
#include <QAction>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
class QLayout;
class QMainWindow;
class QMouseEvent;
class QDockWidget;
class QTabWidget;
class QWidget;

namespace penta::desktop {
// Native frame controls must not be mistaken for the start of a dock drag.
bool isFloatingCaptionControl(QWidget*,const QMouseEvent*);
// Apply GTG's ordinary-window caption and taskbar role without changing data or
// QObject ownership. Call before showing an independent figure/palette frame.
void normalizeIndependentFigureWindow(QWidget*);
// Moves the existing figure widget between its host and an independent native
// window. Plot cells, buffers, navigation and live-update recipients stay intact.
class FigureDockController final:public QObject {
public:
    FigureDockController(QWidget* content,const QString& title,const QString& action_name);
    ~FigureDockController() override;
    bool undock(bool activateWindow=true);
    bool dock();
    bool isUndocked() const;
    QWidget* floatingWindow() const;
    QAction* toggleAction() const {return toggle_;}
    void setFigureActions(const QList<QAction*>& actions);
    void setDockContainer(QDockWidget*);
    void present();
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    void updateAction();
    void normalizeFloatingDock();
    void releaseDockTitle();
    void restoreDockTitle();
    void scheduleFloatingNormalization();
    QPointer<QWidget> content_,home_parent_,placeholder_;
    QPointer<QTabWidget> home_tabs_;
    QPointer<QLayout> home_layout_;
    QPointer<QMainWindow> floating_;
    QPointer<QDockWidget> dock_container_;
    QPointer<QMainWindow> dock_home_;
    QPointer<QWidget> dock_title_;
    QAction* toggle_;
    QList<QAction*> figure_actions_;
    QString title_,tab_title_,tab_tooltip_;
    QIcon tab_icon_,dock_icon_,undock_icon_;
    QRect home_geometry_;
    QRect floating_normal_geometry_;
    int home_index_=-1;
    bool changing_=false;
    bool normalizingDock_=false;
    bool floatingNormalizationQueued_=false;
};
}
