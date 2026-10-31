#pragma once
#include <QByteArray>
#include <QList>
#include <QMainWindow>
#include <QObject>
#include <QPointer>

class QDockWidget;

namespace penta::desktop {
// GTG-style ownership boundary: scientific/editor panes live in the working
// host, and the optional Copilot is the outer window's independent right column.
// This is ordinary raster QWidget layout; it never renders or executes source.
class StudioWorkspaceLayout final:public QObject {
    Q_OBJECT
public:
    StudioWorkspaceLayout(QMainWindow* outer,QWidget* sourceWorkspace,
                          const QList<QDockWidget*>& workingDocks,
                          QDockWidget* copilot=nullptr);
    bool isInstalled()const{return working_!=nullptr;}
    QMainWindow* workingHost()const{return working_;}
    QMainWindow* hostForDock(const QDockWidget*)const;
    QList<QDockWidget*> workingDocks()const;
    // Lazy inspectors/help panes are registered after the initial shell build.
    // Restore a saved Qt placeholder before falling back to the requested area.
    bool registerWorkingDock(QDockWidget*,Qt::DockWidgetArea area=Qt::RightDockWidgetArea);
    // Qt resolves dock/toolbar names recursively. Keep nested workspace
    // objects outside the independent outer layout's restoration scope.
    bool restoreOuterState(const QByteArray&,int version=2);
    QByteArray saveWorkingState()const;
    bool restoreWorkingState(const QByteArray&);
    void captureDefaultLayout();
    bool resetWorkingLayout();
    // Explicit migration/restore boundary, not a LayoutRequest/resize repair.
    // Call once after restoring the new outer and working layout settings.
    void ensureSeparatedLayout();
private:
    struct Pane {QPointer<QDockWidget> dock;Qt::DockWidgetArea defaultArea;};
    void placeWorkingPane(QDockWidget*,Qt::DockWidgetArea);
    QPointer<QMainWindow> outer_,working_;
    QPointer<QWidget> source_;
    QPointer<QDockWidget> copilot_;
    QList<Pane> panes_;
    QByteArray defaultState_;
    static constexpr int StateVersion=2;
};
}
