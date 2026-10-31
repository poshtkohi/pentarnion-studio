#include "../gui/offscreen_test_bootstrap.hpp"
#include "ide/figure_manager.hpp"
#include "ide/tile_layout_widgets.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QRegion>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

using namespace penta;
namespace {
runtime::FigureAction plotted(double coefficient=2){
    runtime::FigureAction p;p.kind=runtime::FigureActionKind::Plot;p.dimensions=2;p.axes={1,2,3};p.algebraId="A_gamma_v1";p.gamma=.5;p.vectors={{{1,coefficient,3,4,5},"p"}};return p;
}
void figures(FigureManager& manager,int count){
    for(int i=1;i<=count;++i){runtime::FigureAction s;s.kind=runtime::FigureActionKind::Select;s.figure=i;s.name="Retained "+std::to_string(i);manager.apply({s,plotted(i)});}
}
TileLayoutSpec spans(){return {2,3,{{1,0,0,2,2},{2,0,2,1,1},{3,1,2,1,1}},{1,2},{2,3,1}};}
void drag(QWidget* widget,QPoint start,QPoint end){QTest::mousePress(widget,Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(widget,end);QTest::mouseRelease(widget,Qt::LeftButton,Qt::NoModifier,end);}
QWidget* fixturePanel(TileLayoutHost& host){
    auto* panel=new QWidget(&host);auto* layout=new QVBoxLayout(panel);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
    auto* toolbar=new QLabel(QStringLiteral("Tools"),panel);toolbar->setFixedHeight(22);toolbar->setStyleSheet(QStringLiteral("background:#f0f0f0;"));layout->addWidget(toolbar);
    auto* canvas=new QLabel(QStringLiteral("Data"),panel);canvas->setStyleSheet(QStringLiteral("background:white;"));layout->addWidget(canvas,1);return panel;
}
bool dividersStayInGutters(TileLayoutHost& host){
    auto* grid=host.tileGrid();if(!grid)return false;
    for(auto* handle:host.findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly)){
        if(!handle->property("tileDividerIndex").isValid()||!handle->isVisible())continue;
        auto visible=handle->mask().isEmpty()?QRegion(handle->rect()):handle->mask();visible.translate(handle->pos());
        if(visible.isEmpty()||!visible.subtracted(QRegion(host.rect())).isEmpty())return false;
        for(int i=0;i<grid->count();++i)if(auto* item=grid->itemAt(i);item&&(!item->widget()||!item->widget()->isHidden()))
            if(visible.intersects(item->geometry()))return false;
    }
    return true;
}
}

class DrawnTileTests final:public QObject {
    Q_OBJECT
private slots:
    void visualGridPreviewDragAndKeyboard();
    void invalidGridAndOutsideReleaseDoNotApply();
    void spanEditorDrawsOnlyNonoverlappingRectangles();
    void layoutValidationRejectsInvalidMetadata();
    void studioSpansPreserveIdentitiesAndPlots();
    void subplotSpansPreserveSparsePanelIds();
    void dividersResizeAndSaveWeights();
    void dividersFollowPostedLayoutsWithoutCrossingPanels();
    void spanningTilesKeepDividersOutsideOccupiedCells();
    void dividerRefreshSettlesWhileIdleAndAfterSelection();
    void versionedLayoutsRoundTripAndRejectTransactionally();
    void visualMenuTargetsFiguresAndSubplots();
    void spanDialogSurvivesClosingFloatingFigures();
};

void DrawnTileTests::visualGridPreviewDragAndKeyboard(){
    TileGridPicker picker;picker.setRequiredCount(6);picker.show();QSignalSpy chosen(&picker,&TileGridPicker::layoutChosen),preview(&picker,&TileGridPicker::previewChanged);
    QTest::mouseMove(&picker,picker.cellRect(1,2).center());QCOMPARE(picker.previewRows(),2);QCOMPARE(picker.previewColumns(),3);QVERIFY(!preview.empty());QCOMPARE(chosen.size(),0);
    drag(&picker,picker.cellRect(0,0).center(),picker.cellRect(1,2).center());QCOMPARE(chosen.size(),1);QCOMPARE(chosen[0][0].toInt(),2);QCOMPARE(chosen[0][1].toInt(),3);
    picker.setFocus();QTest::keyClick(&picker,Qt::Key_Right);QTest::keyClick(&picker,Qt::Key_Return);QCOMPARE(chosen.size(),2);QCOMPARE(chosen[1][1].toInt(),4);
    QTest::keyClick(&picker,Qt::Key_Down);QCOMPARE(picker.previewRows(),3);
}
void DrawnTileTests::invalidGridAndOutsideReleaseDoNotApply(){
    TileGridPicker picker;picker.setRequiredCount(10);picker.show();QSignalSpy chosen(&picker,&TileGridPicker::layoutChosen);
    drag(&picker,picker.cellRect(0,0).center(),picker.cellRect(1,2).center());QCOMPARE(chosen.size(),0);
    drag(&picker,picker.cellRect(0,0).center(),QPoint(-10,-10));QCOMPARE(chosen.size(),0);
    QTest::keyClick(&picker,Qt::Key_Escape);QCOMPARE(chosen.size(),0);
    drag(&picker,picker.cellRect(0,0).center(),picker.cellRect(7,7).center());QCOMPARE(chosen.size(),1);QCOMPARE(chosen[0][0].toInt()*chosen[0][1].toInt(),64);
}
void DrawnTileTests::spanEditorDrawsOnlyNonoverlappingRectangles(){
    TileSpanEditor editor;editor.resize(500,300);editor.setSpec(regularTileLayout(2,3,{1,2}));editor.setSelectedId(1);editor.show();
    QSignalSpy edited(&editor,&TileSpanEditor::layoutEdited),rejected(&editor,&TileSpanEditor::editRejected);
    drag(&editor,editor.cellRect(1,0).center(),editor.cellRect(1,2).center());QCOMPARE(edited.size(),1);QCOMPARE(editor.spec().placements[0].columnSpan,3);QCOMPARE(editor.spec().placements[0].row,1);
    const auto before=editor.spec();editor.setSelectedId(2);drag(&editor,editor.cellRect(0,0).center(),editor.cellRect(1,1).center());QCOMPARE(rejected.size(),1);QVERIFY(editor.spec()==before);
    drag(&editor,editor.cellRect(0,0).center(),QPoint(-10,-10));QVERIFY(editor.spec()==before);
}
void DrawnTileTests::layoutValidationRejectsInvalidMetadata(){
    QString error;auto spec=spans();QVERIFY(validateTileLayout(spec,{1,2,3},&error));
    auto bad=spec;bad.placements[1].column=1;QVERIFY(!validateTileLayout(bad,{1,2,3},&error));QVERIFY(error.contains("overlap"));
    bad=spec;bad.rows=65;QVERIFY(!validateTileLayout(bad,{1,2,3},&error));
    bad=spec;bad.placements[1].id=1;QVERIFY(!validateTileLayout(bad,{1,2,3},&error));
    bad=spec;bad.columnWeights={1,0,1};QVERIFY(!validateTileLayout(bad,{1,2,3},&error));
    bad=spec;bad.rowWeights={1};QVERIFY(!validateTileLayout(bad,{1,2,3},&error));
    auto json=tileLayoutJson(spec);QVERIFY(parseTileLayoutJson(json,&error));json["rows"]=QStringLiteral("2");QVERIFY(!parseTileLayoutJson(json,&error));
    json=tileLayoutJson(spec);json["unknown"]=1;QVERIFY(!parseTileLayoutJson(json,&error));
}
void DrawnTileTests::studioSpansPreserveIdentitiesAndPlots(){
    FigureManager manager(nullptr,false);manager.resize(1000,700);manager.show();figures(manager,3);const auto before=manager.figureState(1);const int selected=manager.currentFigureNumber();QString error;
    const auto spec=spans();QVERIFY2(manager.applyTileLayout(TileLayoutTarget::Studio,spec,0,&error),qPrintable(error));QVERIFY(manager.tileLayout(TileLayoutTarget::Studio)==spec);
    auto* host=manager.findChild<QWidget*>(QStringLiteral("figureTileHost"));auto* grid=qobject_cast<QGridLayout*>(host->layout());QVERIFY(grid);QCOMPARE(grid->count(),3);
    QCOMPARE(grid->itemAtPosition(1,1)->widget()->objectName(),QStringLiteral("figurePage1"));QCOMPARE(grid->columnStretch(1),3);QCOMPARE(grid->rowStretch(1),2);
    QCOMPARE(manager.currentFigureNumber(),selected);QCOMPARE(manager.figureState(1)->name,before->name);QCOMPARE(manager.figureState(1)->gamma,before->gamma);QVERIFY(manager.figureState(1)->vectors[0].coefficients==before->vectors[0].coefficients);
    auto bad=spec;bad.placements[2].row=0;QVERIFY(!manager.applyTileLayout(TileLayoutTarget::Studio,bad,0,&error));QVERIFY(manager.tileLayout(TileLayoutTarget::Studio)==spec);
    auto* reset=manager.findChild<QAction*>(QStringLiteral("tileStudioLayout2x3"));QVERIFY(reset);reset->trigger();QCOMPARE(manager.tileLayout(TileLayoutTarget::Studio).placements[0].columnSpan,1);
}
void DrawnTileTests::subplotSpansPreserveSparsePanelIds(){
    FigureManager manager(nullptr,false);runtime::FigureAction selected;selected.kind=runtime::FigureActionKind::Select;selected.figure=1;manager.apply({selected});
    for(const int id:{1,2,4}){runtime::FigureAction panel;panel.kind=runtime::FigureActionKind::Subplot;panel.rows=panel.columns=2;panel.subplotIndex=id;manager.apply({panel,plotted(id)});}
    QString error;QVERIFY(manager.tileSubplots(1,2,2,&error));
    TileLayoutSpec spec{2,2,{{1,0,0,1,2},{2,1,0,1,1},{4,1,1,1,1}},{1,2},{1,3}};
    QVERIFY2(manager.applyTileLayout(TileLayoutTarget::Subplots,spec,1,&error),qPrintable(error));QVERIFY(manager.tileLayout(TileLayoutTarget::Subplots,1)==spec);
    const auto state=manager.figureState(1);QCOMPARE(state->activeSubplot,4);QCOMPARE(state->subplots.size(),std::size_t(3));QCOMPARE(state->subplots[2].index,4);QCOMPARE(state->subplots[2].vectors[0].coefficients[1],4.0);
    auto invalid=spec;invalid.placements.pop_back();QVERIFY(!manager.applyTileLayout(TileLayoutTarget::Subplots,invalid,1,&error));QVERIFY(manager.tileLayout(TileLayoutTarget::Subplots,1)==spec);
}
void DrawnTileTests::dividersResizeAndSaveWeights(){
    FigureManager manager(nullptr,false);manager.resize(1000,700);manager.show();figures(manager,2);QString error;QVERIFY(manager.tileStudio(1,2,&error));
    auto* host=manager.findChild<TileLayoutHost*>(QStringLiteral("figureTileHost"));QVERIFY(host);QCoreApplication::processEvents();QVERIFY(host->width()>100);
    auto* divider=host->findChild<QWidget*>(QStringLiteral("tileColumnDivider"));QVERIFY(divider);QTRY_VERIFY(divider->isVisible());
    const QPoint local=divider->rect().center(),global=divider->mapToGlobal(local)+QPoint(100,0);
    QTest::mousePress(divider,Qt::LeftButton,Qt::NoModifier,local);
    QMouseEvent moved(QEvent::MouseMove,QPointF(divider->mapFromGlobal(global)),QPointF(global),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(divider,&moved);
    QTest::mouseRelease(divider,Qt::LeftButton,Qt::NoModifier,local);
    const auto layout=manager.tileLayout(TileLayoutTarget::Studio);QVERIFY(layout.columnWeights[0]>layout.columnWeights[1]);
    QVERIFY(std::all_of(layout.columnWeights.begin(),layout.columnWeights.end(),[](int w){return w>0&&w<=1000000;}));
    QTemporaryDir temp;QVERIFY(temp.isValid());QVERIFY(manager.saveTileLayout(TileLayoutTarget::Studio,temp.filePath("resize.json"),0,&error));QVERIFY(manager.tileStudio(1,2,&error));QVERIFY(manager.loadTileLayout(temp.filePath("resize.json"),&error));QVERIFY(manager.tileLayout(TileLayoutTarget::Studio)==layout);
}
void DrawnTileTests::dividersFollowPostedLayoutsWithoutCrossingPanels(){
    TileLayoutHost host;auto* grid=new QGridLayout(&host);grid->setContentsMargins(0,0,0,0);grid->setSpacing(4);host.setTileGrid(grid);
    QList<QWidget*> panels;for(int row=0;row<2;++row)for(int column=0;column<2;++column){auto* panel=fixturePanel(host);panels.append(panel);grid->addWidget(panel,row,column);}
    host.setTileWeights(2,2,{1,1},{1,1});host.resize(420,280);host.show();
    QTRY_VERIFY_WITH_TIMEOUT(host.findChild<QWidget*>(QStringLiteral("tileColumnDivider")),1000);
    auto* column=host.findChild<QWidget*>(QStringLiteral("tileColumnDivider"));auto* row=host.findChild<QWidget*>(QStringLiteral("tileRowDivider"));QVERIFY(row);
    QTRY_VERIFY_WITH_TIMEOUT(column->isVisible()&&row->isVisible()&&dividersStayInGutters(host),1000);
    const QSize originalSize=host.size();const int originalPanelWidth=panels.first()->width(),originalDivider=column->geometry().center().x();
    // These changes post a LayoutRequest without resizing/showing the host.
    // The old implementation leaves a full-height strip in the widened panel.
    grid->setContentsMargins(17,9,5,13);grid->setColumnStretch(0,4);grid->setRowStretch(1,3);grid->invalidate();
    QTRY_VERIFY_WITH_TIMEOUT(panels.first()->width()>originalPanelWidth,1000);QCOMPARE(host.size(),originalSize);
    QTRY_VERIFY_WITH_TIMEOUT(column->isVisible()&&row->isVisible()&&dividersStayInGutters(host),1000);QVERIFY(column->geometry().center().x()!=originalDivider);
    host.resize(700,390);QApplication::processEvents();QTRY_VERIFY_WITH_TIMEOUT(column->isVisible()&&row->isVisible()&&dividersStayInGutters(host),1000);
    host.hide();host.resize(510,340);host.show();QApplication::processEvents();QTRY_VERIFY_WITH_TIMEOUT(column->isVisible()&&row->isVisible()&&dividersStayInGutters(host),1000);
}
void DrawnTileTests::spanningTilesKeepDividersOutsideOccupiedCells(){
    TileLayoutHost host;auto* grid=new QGridLayout(&host);grid->setContentsMargins(0,0,0,0);grid->setSpacing(4);host.setTileGrid(grid);
    auto* spanning=fixturePanel(host);grid->addWidget(spanning,0,0,2,2);grid->addWidget(fixturePanel(host),0,2);grid->addWidget(fixturePanel(host),1,2);
    host.setTileWeights(2,3,{1,2},{2,3,1});host.resize(600,320);host.show();
    QTRY_COMPARE_WITH_TIMEOUT(host.findChildren<QWidget*>(QStringLiteral("tileColumnDivider"),Qt::FindDirectChildrenOnly).size(),2,1000);
    const auto columns=host.findChildren<QWidget*>(QStringLiteral("tileColumnDivider"),Qt::FindDirectChildrenOnly);
    QWidget* insideSpan=nullptr;QWidget* realGap=nullptr;
    for(auto* column:columns)(column->property("tileDividerIndex").toInt()==0?insideSpan:realGap)=column;
    QVERIFY(insideSpan&&realGap);auto* row=host.findChild<QWidget*>(QStringLiteral("tileRowDivider"));QVERIFY(row);
    QTRY_VERIFY_WITH_TIMEOUT(insideSpan->isHidden()&&realGap->isVisible()&&row->isVisible()&&dividersStayInGutters(host),1000);
    auto visible=row->mask().isEmpty()?QRegion(row->rect()):row->mask();visible.translate(row->pos());QVERIFY(!visible.intersects(spanning->geometry()));
    host.resize(740,380);QApplication::processEvents();QTRY_VERIFY_WITH_TIMEOUT(insideSpan->isHidden()&&realGap->isVisible()&&row->isVisible()&&dividersStayInGutters(host),1000);
}
void DrawnTileTests::dividerRefreshSettlesWhileIdleAndAfterSelection(){
    class LayoutCounter final:public QObject {
    public:int requests=0;
    protected:bool eventFilter(QObject*,QEvent* event)override{if(event->type()==QEvent::LayoutRequest)++requests;return false;}
    } counter;
    FigureManager manager(nullptr,false);manager.resize(800,500);manager.show();figures(manager,3);QString error;
    QVERIFY(manager.tileStudio(2,3,&error));
    auto* host=manager.findChild<TileLayoutHost*>(QStringLiteral("figureTileHost"));QVERIFY(host);host->installEventFilter(&counter);
    QTest::qWait(60);QVERIFY2(counter.requests<20,"Divider refresh must settle, without an idle LayoutRequest loop.");
    for(int i=0;i<host->tileGrid()->count();++i){const auto actual=host->tileGrid()->itemAt(i)->geometry();QVERIFY(actual.height()>host->height()/3);QVERIFY(actual.height()<2*host->height()/3);}
    QVERIFY(dividersStayInGutters(*host));
    const int settled=counter.requests;QTest::qWait(40);QCOMPARE(counter.requests,settled);
    // Selecting another figure shows/hides GTG's persistent perimeter overlay.
    // Its size-hint query must not move or restart the divider refreshes.
    auto* page=manager.findChild<QWidget*>(QStringLiteral("figurePage1"));QVERIFY(page);
    QTest::mouseClick(page,Qt::LeftButton,Qt::NoModifier,QPoint(3,3));QTest::qWait(60);
    for(int i=0;i<host->tileGrid()->count();++i){const auto actual=host->tileGrid()->itemAt(i)->geometry();QVERIFY(actual.height()>host->height()/3);QVERIFY(actual.height()<2*host->height()/3);}
    QVERIFY(dividersStayInGutters(*host));
    const int selected=counter.requests;QTest::qWait(40);QCOMPARE(counter.requests,selected);
}
void DrawnTileTests::versionedLayoutsRoundTripAndRejectTransactionally(){
    FigureManager manager(nullptr,false);figures(manager,3);QString error;QVERIFY(manager.applyTileLayout(TileLayoutTarget::Studio,spans(),0,&error));QTemporaryDir temp;QVERIFY(temp.isValid());const auto path=temp.filePath("layout.pentalayout.json");
    QVERIFY(manager.saveTileLayout(TileLayoutTarget::Studio,path,0,&error));QVERIFY(manager.tileStudio(1,3,&error));QVERIFY2(manager.loadTileLayout(path,&error),qPrintable(error));QVERIFY(manager.tileLayout(TileLayoutTarget::Studio)==spans());
    QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));auto document=QJsonDocument::fromJson(file.readAll()).object();file.close();document["schema_version"]=2;QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(document).toJson());file.close();QVERIFY(!manager.loadTileLayout(path,&error));QVERIFY(manager.tileLayout(TileLayoutTarget::Studio)==spans());
    document["schema_version"]=1;document["layout"]=tileLayoutJson(regularTileLayout(1,3,{1,2,9}));QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(document).toJson());file.close();QVERIFY(!manager.loadTileLayout(path,&error));QVERIFY(manager.tileLayout(TileLayoutTarget::Studio)==spans());
}
void DrawnTileTests::visualMenuTargetsFiguresAndSubplots(){
    FigureManager manager(nullptr,false);figures(manager,2);auto* button=manager.findChild<QToolButton*>(QStringLiteral("tileFiguresStudioButton"));QVERIFY(button);auto* picker=button->menu()->findChild<TileGridPicker*>();QVERIFY(picker);picker->setRequiredCount(2);
    drag(picker,picker->cellRect(0,0).center(),picker->cellRect(1,1).center());QTRY_COMPARE(manager.tileLayout(TileLayoutTarget::Studio).rows,2);QCOMPARE(manager.tileLayout(TileLayoutTarget::Studio).columns,2);
    auto* plots=manager.findChild<QToolButton*>(QStringLiteral("tileSubplotsButton"));QVERIFY(plots);auto* plotPicker=plots->menu()->findChild<TileGridPicker*>();QVERIFY(plotPicker);plotPicker->setRequiredCount(1);
    drag(plotPicker,plotPicker->cellRect(0,0).center(),plotPicker->cellRect(0,1).center());QTRY_COMPARE(manager.figureState(2)->columns,2);QCOMPARE(manager.figureState(1)->columns,1);
}
void DrawnTileTests::spanDialogSurvivesClosingFloatingFigures(){
    FigureManager manager(nullptr,false);manager.showTabs();figures(manager,2);QString error;QVERIFY(manager.undockFigure(1,&error));auto* floating=manager.findChild<QMainWindow*>(QStringLiteral("floatingFigure1"));QVERIFY(floating);auto* action=floating->findChild<QAction*>(QStringLiteral("tileStudioSpans"));QVERIFY(action);bool survived=false,disabled=false;
    QTimer::singleShot(0,&manager,[&]{QPointer<QDialog> dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;runtime::FigureAction close;close.kind=runtime::FigureActionKind::CloseAll;manager.apply({close});survived=!dialog.isNull();if(dialog){auto* buttons=dialog->findChild<QDialogButtonBox*>();disabled=buttons&&!buttons->button(QDialogButtonBox::Ok)->isEnabled();dialog->reject();}});
    action->trigger();QVERIFY(survived);QVERIFY(disabled);QCOMPARE(manager.figureCount(),0);
}

PENTA_OFFSCREEN_TEST_MAIN(DrawnTileTests)

#include "drawn_tile_tests.moc"
