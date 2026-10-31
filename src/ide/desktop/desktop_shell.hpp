#pragma once

#include <QObject>
#include <QPointer>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

class QAction;
class QDockWidget;
class QHBoxLayout;
class QMainWindow;
class QMenu;
class QMenuBar;
class QSettings;
class QToolBar;
class QToolButton;
class QWidget;

namespace penta::desktop {
class CollapsibleDockController;
class Toolstrip;

// GTG-style presentation around the application's original actions and widgets.
// This class never executes scientific commands or owns their model state.
class DesktopShell final : public QObject {
public:
    explicit DesktopShell(QMainWindow* window, QSettings* settings = nullptr);
    ~DesktopShell() override;

    int addPage(const QString& title);
    QToolButton* addCommand(int page, const QString& group, QAction* action,
                           bool prominent = false);
    QToolButton* addCommand(const QString& page, const QString& group, QAction* action,
                           bool prominent = false);
    void attachViewMenu(QMenu* view);
    void registerClassicToolbar(QToolBar* toolbar);
    CollapsibleDockController* registerDock(QDockWidget* dock, const QString& key,
                                          bool autoHide = false, bool reflowDrawer = false,
                                          QMainWindow* host = nullptr);

    void setRibbonMode(bool enabled, bool persist = true);
    bool ribbonMode() const { return ribbonMode_; }
    void restorePreferences(bool restoreDockState = true);
    void savePreferences();
    void captureDefaultLayout();
    bool restoreDefaultLayout();

    void setClassicHeaderAccessory(QWidget* widget);
    void setRibbonHeaderAccessory(QWidget* widget);
    QMenuBar* classicMenuBar() const { return classicMenuBar_; }
    QWidget* interfaceHeader() const { return header_; }
    Toolstrip* toolstrip() const { return toolstrip_; }
    QAction* classicAction() const { return classicAction_; }
    QAction* ribbonAction() const { return ribbonAction_; }
    QAction* defaultLayoutAction() const { return defaultLayoutAction_; }

private:
    struct Toolbar { QPointer<QToolBar> widget; bool classicVisible = true; };
    struct Dock { QPointer<CollapsibleDockController> controller; QByteArray defaultState; };
    QPointer<QMainWindow> window_;
    QPointer<QSettings> settings_;
    QWidget* header_ = nullptr;
    QWidget* classicHeader_ = nullptr;
    QMenuBar* classicMenuBar_ = nullptr;
    QHBoxLayout* classicRow_ = nullptr;
    QPointer<QWidget> classicAccessory_;
    Toolstrip* toolstrip_ = nullptr;
    QPointer<QObject> toolstripLifetime_;
    QAction* classicAction_ = nullptr;
    QAction* ribbonAction_ = nullptr;
    QAction* defaultLayoutAction_ = nullptr;
    QHash<QString, int> pages_;
    QVector<Toolbar> toolbars_;
    QVector<Dock> docks_;
    QByteArray defaultLayout_;
    bool ribbonMode_ = false;
    bool restoring_ = true;
};
}
