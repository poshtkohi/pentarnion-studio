#include "../gui/offscreen_test_bootstrap.hpp"
#include "ide/figure_manager.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>

using namespace penta;

namespace {
void addFigures(FigureManager& manager,int count) {
    for(int number=1;number<=count;++number){
        runtime::FigureAction selected;selected.kind=runtime::FigureActionKind::Select;selected.figure=number;
        selected.name="Preserved figure "+std::to_string(number);
        runtime::FigureAction plotted;plotted.kind=runtime::FigureActionKind::Plot;plotted.dimensions=2;
        plotted.axes={1,2,3};plotted.algebraId="A_gamma_v1";plotted.gamma=.75;
        plotted.vectors={{{1,2,3,4,5},"p"}};
        manager.apply({selected,plotted});
    }
}
QGridLayout* figureGrid(FigureManager& manager) {
    auto* host=manager.findChild<QWidget*>(QStringLiteral("figureTileHost"));
    return host?qobject_cast<QGridLayout*>(host->layout()):nullptr;
}
}

class TileLayoutTests final:public QObject {
    Q_OBJECT
private slots:
    void menusOfferLayoutsAndRespectCapacity();
    void presetRowsAndColumnsPreserveFigures();
    void customLayoutValidatesCapacityAndCellLimit();
    void customCancelPreservesLayoutAndDocking();
    void floatingMenuCanChooseLayoutAndDockSafely();
    void desktopMenuTilesAndCustomCancelIsSafe();
    void liveClosingFiguresDoesNotDestroyCustomDialog();
    void languageTileLayoutStillRetainsEmptyCells();
    void lastTiledFigureCloseShowsBlankPaneAndPreservesLayout_data();
    void lastTiledFigureCloseShowsBlankPaneAndPreservesLayout();
};

void TileLayoutTests::menusOfferLayoutsAndRespectCapacity() {
    FigureManager manager(nullptr,false);
    auto* studio=manager.findChild<QToolButton*>(QStringLiteral("tileFiguresStudioButton"));
    auto* desktop=manager.findChild<QToolButton*>(QStringLiteral("tileFiguresDesktopButton"));
    QVERIFY(studio);QVERIFY(desktop);QVERIFY(studio->menu());QVERIFY(desktop->menu());
    QVERIFY(!studio->isEnabled());QVERIFY(!desktop->isEnabled());
    addFigures(manager,3);
    QVERIFY(studio->isEnabled());QVERIFY(desktop->isEnabled());
    for(auto* menu:{studio->menu(),desktop->menu()}){
        const QString prefix=menu==studio->menu()?QStringLiteral("tileStudio"):QStringLiteral("tileDesktop");
        QVERIFY(menu->findChild<QAction*>(prefix+QStringLiteral("Automatic")));
        QVERIFY(menu->findChild<QAction*>(prefix+QStringLiteral("Custom")));
        auto* tooSmall=menu->findChild<QAction*>(prefix+QStringLiteral("Layout1x2"));
        auto* enough=menu->findChild<QAction*>(prefix+QStringLiteral("Layout2x2"));
        QVERIFY(tooSmall);QVERIFY(enough);QVERIFY(!tooSmall->isEnabled());QVERIFY(enough->isEnabled());
    }
    runtime::FigureAction close;close.kind=runtime::FigureActionKind::Close;close.figure=3;manager.apply({close});
    QVERIFY(studio->menu()->findChild<QAction*>(QStringLiteral("tileStudioLayout1x2"))->isEnabled());
}

void TileLayoutTests::presetRowsAndColumnsPreserveFigures() {
    FigureManager manager(nullptr,false);manager.resize(1000,800);manager.show();addFigures(manager,2);
    QSignalSpy errors(&manager,&FigureManager::error);
    const auto before=manager.figureState(1);const int selected=manager.currentFigureNumber();
    auto* preset=manager.findChild<QAction*>(QStringLiteral("tileStudioLayout2x1"));QVERIFY(preset);preset->trigger();
    auto* grid=figureGrid(manager);QVERIFY(grid);QCOMPARE(grid->count(),2);
    int row=0,column=0,rowSpan=0,columnSpan=0;grid->getItemPosition(1,&row,&column,&rowSpan,&columnSpan);
    QCOMPARE(row,1);QCOMPARE(column,0);QCOMPARE(grid->rowStretch(0),1);QCOMPARE(grid->rowStretch(1),1);
    QCOMPARE(manager.currentFigureNumber(),selected);QCOMPARE(errors.size(),0);
    QVERIFY(manager.figureState(1)->vectors[0].coefficients==before->vectors[0].coefficients);
    QCOMPARE(manager.figureState(1)->name,before->name);QCOMPARE(manager.figureState(1)->gamma,before->gamma);
    manager.findChild<QAction*>(QStringLiteral("tileStudioLayout1x2"))->trigger();
    grid->getItemPosition(1,&row,&column,&rowSpan,&columnSpan);QCOMPARE(row,0);QCOMPARE(column,1);
    QCOMPARE(grid->rowStretch(1),0);QCOMPARE(grid->columnStretch(1),1);
    manager.showTabs();QCOMPARE(manager.findChild<QTabWidget*>(QStringLiteral("figureTabs"))->count(),2);
    QCOMPARE(grid->rowStretch(0),0);QCOMPARE(grid->columnStretch(1),0);
}

void TileLayoutTests::customLayoutValidatesCapacityAndCellLimit() {
    FigureManager manager(nullptr,false);manager.resize(1000,800);manager.show();addFigures(manager,3);
    auto* custom=manager.findChild<QAction*>(QStringLiteral("tileStudioCustom"));QVERIFY(custom);
    bool visited=false,capacityRejected=false,productRejected=false,validAccepted=false;
    QTimer::singleShot(0,&manager,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
        visited=true;
        auto* rows=dialog->findChild<QSpinBox*>(QStringLiteral("tileLayoutRows"));
        auto* columns=dialog->findChild<QSpinBox*>(QStringLiteral("tileLayoutColumns"));
        auto* buttons=dialog->findChild<QDialogButtonBox*>(QStringLiteral("tileLayoutButtons"));
        auto* message=dialog->findChild<QLabel*>(QStringLiteral("tileLayoutValidation"));
        if(!rows||!columns||!buttons||!message){dialog->reject();return;}
        rows->setValue(1);columns->setValue(1);
        capacityRejected=!buttons->button(QDialogButtonBox::Ok)->isEnabled()&&message->text().contains(QStringLiteral("fewer cells"));
        rows->setValue(8);columns->setValue(9);
        productRejected=!buttons->button(QDialogButtonBox::Ok)->isEnabled()&&message->text().contains(QStringLiteral("64"));
        rows->setValue(2);columns->setValue(3);
        validAccepted=buttons->button(QDialogButtonBox::Ok)->isEnabled();
        if(validAccepted)buttons->button(QDialogButtonBox::Ok)->click();else dialog->reject();
    });
    custom->trigger();QVERIFY(visited);QVERIFY(capacityRejected);QVERIFY(productRejected);QVERIFY(validAccepted);
    auto* grid=figureGrid(manager);QVERIFY(grid);QCOMPARE(grid->count(),3);
    QCOMPARE(grid->rowStretch(0),1);QCOMPARE(grid->rowStretch(1),1);QCOMPARE(grid->columnStretch(2),1);
    QCoreApplication::processEvents();grid->activate();
    // Qt's cellRect may describe an ancestor height-for-width measurement.
    // Test the real page geometry: the explicit empty second row must retain
    // half the available grid height, and every column its third of the width.
    auto* host=grid->parentWidget();QVERIFY(host);
    for(int i=0;i<grid->count();++i){
        const auto actual=grid->itemAt(i)->geometry();
        QVERIFY(actual.height()>host->height()/3);QVERIFY(actual.height()<2*host->height()/3);
        QVERIFY(actual.width()>host->width()/4);QVERIFY(actual.width()<host->width()/2);
        QVERIFY(actual.bottom()<2*host->height()/3);
    }
    QCOMPARE(manager.figureCount(),3);QVERIFY(manager.figureState(1)->docked);
}

void TileLayoutTests::customCancelPreservesLayoutAndDocking() {
    FigureManager manager(nullptr,false);addFigures(manager,3);QString message;QVERIFY(manager.tileStudio(1,3,&message));
    const int selected=manager.currentFigureNumber();QSignalSpy changed(&manager,&FigureManager::figuresChanged);
    bool visited=false;
    QTimer::singleShot(0,&manager,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
        visited=true;auto* rows=dialog->findChild<QSpinBox*>(QStringLiteral("tileLayoutRows"));
        if(rows)rows->setValue(5);
        dialog->reject();
    });
    manager.findChild<QAction*>(QStringLiteral("tileStudioCustom"))->trigger();
    QVERIFY(visited);QCOMPARE(changed.size(),0);QCOMPARE(manager.currentFigureNumber(),selected);
    auto* grid=figureGrid(manager);QCOMPARE(grid->rowCount(),1);QCOMPARE(grid->rowStretch(0),1);QCOMPARE(grid->columnStretch(2),1);
    for(int number=1;number<=3;++number)QVERIFY(manager.figureState(number)->docked);
}

void TileLayoutTests::floatingMenuCanChooseLayoutAndDockSafely() {
    FigureManager manager(nullptr,false);manager.showTabs();addFigures(manager,2);QString message;QVERIFY(manager.undockFigure(1,&message));
    QPointer<QMainWindow> floating=manager.findChild<QMainWindow*>(QStringLiteral("floatingFigure1"));QVERIFY(floating);
    auto* tile=floating->findChild<QToolButton*>(QStringLiteral("floatingFigureTileButton"));QVERIFY(tile);QVERIFY(tile->menu());
    auto* custom=floating->findChild<QAction*>(QStringLiteral("tileStudioCustom"));QVERIFY(custom);
    bool visited=false;
    QTimer::singleShot(0,&manager,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
        visited=true;auto* rows=dialog->findChild<QSpinBox*>(QStringLiteral("tileLayoutRows"));auto* columns=dialog->findChild<QSpinBox*>(QStringLiteral("tileLayoutColumns"));
        auto* buttons=dialog->findChild<QDialogButtonBox*>(QStringLiteral("tileLayoutButtons"));
        if(!rows||!columns||!buttons){dialog->reject();return;}
        rows->setValue(2);columns->setValue(2);buttons->button(QDialogButtonBox::Ok)->click();
    });
    custom->trigger();QVERIFY(visited);QVERIFY(floating.isNull());
    QVERIFY(manager.figureState(1)->docked);QVERIFY(manager.figureState(2)->docked);
    QCOMPARE(figureGrid(manager)->rowStretch(1),1);QCOMPARE(figureGrid(manager)->columnStretch(1),1);
    QCOMPARE(manager.figureState(1)->vectors[0].coefficients[4],5.0);
}

void TileLayoutTests::desktopMenuTilesAndCustomCancelIsSafe() {
    FigureManager manager(nullptr,false);addFigures(manager,2);QSignalSpy errors(&manager,&FigureManager::error);
    auto* desktop=manager.findChild<QAction*>(QStringLiteral("tileDesktopLayout1x2"));QVERIFY(desktop);desktop->trigger();
    QCOMPARE(errors.size(),0);QVERIFY(!manager.figureState(1)->docked);QVERIFY(!manager.figureState(2)->docked);
    auto* first=manager.findChild<QMainWindow*>(QStringLiteral("floatingFigure1"));auto* second=manager.findChild<QMainWindow*>(QStringLiteral("floatingFigure2"));
    QVERIFY(first);QVERIFY(second);QVERIFY(first->screen());
    // Native setFramePosition requests settle through the window event queue.
    // Capture the cancellation baseline only after both tile moves complete.
    const QRect available=first->screen()->availableGeometry();
    QTRY_COMPARE_WITH_TIMEOUT(first->frameGeometry().topLeft(),available.topLeft(),5000);
    QTRY_COMPARE_WITH_TIMEOUT(second->frameGeometry().topLeft(),QPoint(available.left()+available.width()/2,available.top()),5000);
    const QRect before=first->geometry();const int selected=manager.currentFigureNumber();
    bool visited=false,remembered=false;
    QTimer::singleShot(0,&manager,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
        visited=true;auto* rows=dialog->findChild<QSpinBox*>(QStringLiteral("tileLayoutRows"));auto* columns=dialog->findChild<QSpinBox*>(QStringLiteral("tileLayoutColumns"));
        remembered=rows&&columns&&rows->value()==1&&columns->value()==2;dialog->reject();
    });
    first->findChild<QAction*>(QStringLiteral("tileDesktopCustom"))->trigger();
    QVERIFY(visited);QVERIFY(remembered);QCOMPARE(first->geometry(),before);QCOMPARE(manager.currentFigureNumber(),selected);
    QVERIFY(!manager.figureState(1)->docked);QCOMPARE(manager.figureCount(),2);
}

void TileLayoutTests::languageTileLayoutStillRetainsEmptyCells() {
    FigureManager manager(nullptr,false);addFigures(manager,2);
    runtime::FigureAction tile;tile.kind=runtime::FigureActionKind::TileStudio;tile.rows=2;tile.columns=2;
    QSignalSpy errors(&manager,&FigureManager::error);manager.apply({tile});QCOMPARE(errors.size(),0);
    auto* grid=figureGrid(manager);QCOMPARE(grid->rowStretch(1),1);QCOMPARE(grid->columnStretch(1),1);
    tile.rows=tile.columns=1;manager.apply({tile});QCOMPARE(errors.size(),1);QCOMPARE(grid->rowStretch(1),1);QCOMPARE(manager.figureCount(),2);
}

void TileLayoutTests::lastTiledFigureCloseShowsBlankPaneAndPreservesLayout_data() {
    QTest::addColumn<bool>("closeAll");
    QTest::newRow("individual-last-close")<<false;
    QTest::newRow("close-all")<<true;
}

void TileLayoutTests::lastTiledFigureCloseShowsBlankPaneAndPreservesLayout() {
    QFETCH(bool,closeAll);
    // This fixture deliberately disables rendering: it checks widget/layout
    // lifetime without creating a native graphics context or desktop capture.
    FigureManager manager(nullptr,false);manager.resize(1000,800);manager.show();
    addFigures(manager,2);
    auto layout=regularTileLayout(2,3,{1,2});
    layout.rowWeights={2,5};layout.columnWeights={3,1,4};
    QString message;
    QVERIFY2(manager.applyTileLayout(TileLayoutTarget::Studio,layout,0,&message),qPrintable(message));
    auto* tabs=manager.findChild<QTabWidget*>(QStringLiteral("figureTabs"));
    auto* host=manager.findChild<QWidget*>(QStringLiteral("figureTileHost"));
    QVERIFY(tabs);QVERIFY(host);
    auto* stack=qobject_cast<QStackedWidget*>(tabs->parentWidget());
    auto* grid=figureGrid(manager);QVERIFY(stack);QVERIFY(grid);
    QCoreApplication::processEvents();
    QCOMPARE(stack->currentWidget(),host);QVERIFY(host->isVisible());
    QPointer<QWidget> first=manager.findChild<QWidget*>(QStringLiteral("figurePage1"));
    QPointer<QWidget> second=manager.findChild<QWidget*>(QStringLiteral("figurePage2"));
    QVERIFY(first);QVERIFY(second);QVERIFY(first->isVisible());QVERIFY(second->isVisible());
    const auto before=manager.figureState(1);
    QSignalSpy errors(&manager,&FigureManager::error);
    runtime::FigureAction close;
    if(closeAll){close.kind=runtime::FigureActionKind::CloseAll;manager.apply({close});}
    else{
        close.kind=runtime::FigureActionKind::Close;close.figure=2;manager.apply({close});
        QVERIFY(second.isNull());QVERIFY(first);QVERIFY(first->isVisible());
        QCOMPARE(manager.figureCount(),1);
        close.figure=1;manager.apply({close});
    }
    QCoreApplication::processEvents();
    QCOMPARE(errors.size(),0);QCOMPARE(manager.figureCount(),0);QCOMPARE(manager.currentFigureNumber(),0);
    QVERIFY(first.isNull());QVERIFY(second.isNull());QCOMPARE(grid->count(),0);QCOMPARE(tabs->count(),0);
    // A retained tiled host can display dividers or old graphics pixels after
    // its last page disappears. The empty tab surface must cover that host.
    QCOMPARE(stack->currentWidget(),static_cast<QWidget*>(tabs));
    QVERIFY(tabs->isVisible());QVERIFY(!host->isVisible());
    const auto empty=manager.tileLayout(TileLayoutTarget::Studio);
    QCOMPARE(empty.rows,layout.rows);QCOMPARE(empty.columns,layout.columns);
    QVERIFY(empty.rowWeights==layout.rowWeights);QVERIFY(empty.columnWeights==layout.columnWeights);
    QVERIFY(empty.placements.empty());

    addFigures(manager,2);QCoreApplication::processEvents();
    QCOMPARE(errors.size(),0);QCOMPARE(manager.figureCount(),2);QCOMPARE(grid->count(),2);
    QCOMPARE(stack->currentWidget(),host);QVERIFY(host->isVisible());QVERIFY(!tabs->isVisible());
    QVERIFY(manager.tileLayout(TileLayoutTarget::Studio)==layout);
    for(int number=1;number<=2;++number){
        auto* page=manager.findChild<QWidget*>(QStringLiteral("figurePage%1").arg(number));
        QVERIFY(page);QVERIFY(page->isVisible());QVERIFY(page->width()>0);QVERIFY(page->height()>0);
        const auto state=manager.figureState(number);QVERIFY(state);QVERIFY(state->docked);
        QCOMPARE(state->name,QStringLiteral("Preserved figure %1").arg(number));
        QCOMPARE(state->gamma,before->gamma);QVERIFY(state->vectors[0].coefficients==before->vectors[0].coefficients);
    }
}

void TileLayoutTests::liveClosingFiguresDoesNotDestroyCustomDialog() {
    FigureManager manager(nullptr,false);manager.showTabs();addFigures(manager,2);QString message;QVERIFY(manager.undockFigure(1,&message));
    auto* floating=manager.findChild<QMainWindow*>(QStringLiteral("floatingFigure1"));QVERIFY(floating);
    auto* custom=floating->findChild<QAction*>(QStringLiteral("tileStudioCustom"));QVERIFY(custom);
    bool survived=false,disabled=false;
    QTimer::singleShot(0,&manager,[&]{
        QPointer<QDialog> dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
        runtime::FigureAction close;close.kind=runtime::FigureActionKind::CloseAll;manager.apply({close});
        survived=!dialog.isNull();
        if(dialog){
            auto* buttons=dialog->findChild<QDialogButtonBox*>(QStringLiteral("tileLayoutButtons"));
            disabled=buttons&&!buttons->button(QDialogButtonBox::Ok)->isEnabled();dialog->reject();
        }
    });
    custom->trigger();QVERIFY(survived);QVERIFY(disabled);QCOMPARE(manager.figureCount(),0);
}

PENTA_OFFSCREEN_TEST_MAIN(TileLayoutTests)

#include "tile_layout_tests.moc"
