// Adapted from GTG Cell Studio toolstrip.hpp. See GTG_COMPONENT_PROVENANCE.txt.
#pragma once
#include <QWidget>
#include <QPointer>
#include <QString>
#include <functional>
#include <vector>
class QAction;class QTabBar;class QToolButton;class QStackedWidget;class QFrame;class QMenu;
class QVBoxLayout;class QHBoxLayout;class QGridLayout;class QResizeEvent;
class QContextMenuEvent;
namespace penta::desktop {
// Presentation of the application's existing actions. Actions, shortcuts and
// command handlers retain their original owner and are never duplicated.
class Toolstrip final:public QWidget {
public:
    // Value 2 remains readable for older workspace preferences. It is migrated
    // to TabsOnly: collapsing commands must never hide the navigation tabs.
    enum class RibbonDisplay { Expanded, TabsOnly, AutoHide };
    explicit Toolstrip(QWidget* parent=nullptr);
    ~Toolstrip()override;
    int addPage(const QString& title);
    QToolButton* addAction(int page,const QString& group,QAction*,bool prominent=false);
    void setPinned(bool);
    bool isPinned()const{return pinned_;}
    void setDisplayMode(RibbonDisplay);
    RibbonDisplay displayMode()const{return displayMode_;}
    // One shared action names the next step: Minimise Ribbon / Restore Ribbon.
    QList<QAction*> displayActions()const{return displayActions_;}
    QAction* toggleRibbonAction()const{return toggleRibbon_;}
    QMenu* displayMenu()const{return displayMenu_;}
    void setCurrentPage(int);
    int currentPage()const;
    void setPopupVisible(bool);
    bool popupVisible()const;
    void closePopup(){setPopupVisible(false);}
    QTabBar* tabBar()const{return tabs_;}
    void setHeaderAccessory(QWidget*);
    std::function<void(bool)> pinnedChanged;
    std::function<void(int)> currentPageChanged;
    std::function<void(RibbonDisplay)> displayModeChanged;
protected:
    bool eventFilter(QObject*,QEvent*)override;
    void resizeEvent(QResizeEvent*)override;
    void contextMenuEvent(QContextMenuEvent*)override;
private:
    struct Group {QString title;QWidget* widget;QHBoxLayout* columns;QGridLayout* compact=nullptr;int compactCount=0;};
    struct Page {QWidget* contents;QHBoxLayout* layout;std::vector<Group> groups;};
    Group& ensureGroup(int,const QString&);
    void placeBody(bool popup);
    void positionPopup();
    void restoreFocus();
    void updateDisplay();
    void watchContextMenus(QWidget*);
    bool handlesRibbonContext(QWidget*)const;
    QVBoxLayout* layout_;QTabBar* tabs_;QToolButton* pin_;
    QStackedWidget* body_=nullptr;QFrame* popup_=nullptr;QVBoxLayout* popupLayout_;
    QWidget* header_;
    QPointer<QWidget> headerAccessory_;
    QMenu* displayMenu_;QList<QAction*> displayActions_;QAction* toggleRibbon_;
    std::vector<Page> pages_;
    QPointer<QWidget> previousFocus_;
    bool pinned_=true,reparenting_=false,applicationFilter_=false;
    RibbonDisplay displayMode_=RibbonDisplay::Expanded;
};
}
