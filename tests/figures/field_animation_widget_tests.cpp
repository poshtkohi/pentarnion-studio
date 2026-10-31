#include "../gui/offscreen_test_bootstrap.hpp"
#include "ide/figure_manager.hpp"
#include "plotting/field_animation.hpp"
#include <QComboBox>
#include <QSignalSpy>
#include <QSlider>
#include <QTimer>
#include <QtTest>

namespace {
std::shared_ptr<const penta::plotting::FieldAnimationData> frames(bool loop=false){auto data=std::make_shared<penta::plotting::FieldAnimationData>();data->nx=3;data->x={0,.5,1};data->t={0,.1,.2};data->components=5;data->fps=60;data->loop=loop;data->selectedFrames={0,1,2};for(int t=0;t<3;++t)for(int x=0;x<3;++x)for(int c=0;c<5;++c)data->values.push_back(100*c+10*t+x);penta::plotting::validateFieldAnimation(*data);return data;}
penta::runtime::FigureAction animation(bool loop=false){penta::runtime::FigureAction a;a.kind=penta::runtime::FigureActionKind::SetAnimation;a.algebraId="A_gamma_v1";a.gamma=1;a.animation=frames(loop);return a;}
}
class FieldAnimationWidgetTests:public QObject {
    Q_OBJECT
private slots:
    void sliderAndCoefficientInspectImmutableSamples(){
        penta::FigureManager manager(nullptr,false);const auto action=animation();const auto original=action.animation->values;manager.apply({action});auto state=manager.figureState(1);QVERIFY(state&&state->animation);QCOMPARE(state->curves[0].points[1][1],1.);auto* slider=manager.findChild<QSlider*>("fieldAnimationFrameSlider");QVERIFY(slider);slider->setValue(1);state=manager.figureState(1);QCOMPARE(state->animationFrame,std::size_t(1));QCOMPARE(state->curves[0].points[1][1],11.);
        auto* component=manager.findChild<QComboBox*>("fieldAnimationCoefficient");QVERIFY(component);QCOMPARE(component->count(),5);component->setCurrentIndex(4);state=manager.figureState(1);QCOMPARE(state->animationCoefficient,std::size_t(4));QCOMPARE(state->curves[0].points[1][1],411.);QVERIFY(state->animation->values==original);QVERIFY(state->style.colorLimits==action.animation->coefficientRanges[4]);
    }
    void wideAlgebraFieldsExposeEveryCoefficient(){
        auto data=std::make_shared<penta::plotting::FieldAnimationData>();data->nx=2;data->x={0,1};data->t={0,1};data->components=16;data->algebraId="sedenion_coordinates";data->selectedFrames={0,1};for(int t=0;t<2;++t)for(int node=0;node<2;++node)for(int c=0;c<16;++c)data->values.push_back(100*c+10*t+node);penta::plotting::validateFieldAnimation(*data);penta::runtime::FigureAction action;action.kind=penta::runtime::FigureActionKind::SetAnimation;action.animation=data;penta::FigureManager manager(nullptr,false);manager.apply({action});auto* component=manager.findChild<QComboBox*>("fieldAnimationCoefficient");QVERIFY(component);QCOMPARE(component->count(),16);component->setCurrentIndex(15);auto* slider=manager.findChild<QSlider*>("fieldAnimationFrameSlider");slider->setValue(1);const auto state=manager.figureState(1);QCOMPARE(state->curves[0].points[1][1],1511.);QCOMPARE(state->basisLabels.size(),std::size_t(16));QCOMPARE(state->animation->values,data->values);
    }
    void playbackUsesSavedFramesAndStopsAtEnd(){
        penta::FigureManager manager(nullptr,false);manager.apply({animation()});penta::runtime::FigureAction play;play.kind=penta::runtime::FigureActionKind::SetAnimationPlaying;play.animationPlaying=true;manager.apply({play});QTRY_COMPARE_WITH_TIMEOUT(manager.figureState(1)->animationFrame,std::size_t(2),500);QTRY_VERIFY_WITH_TIMEOUT(!manager.figureState(1)->animationPlaying,500);auto* timer=manager.findChild<QTimer*>("fieldAnimationTimer");QVERIFY(timer&&!timer->isActive());
    }
    void loopPauseAndClosingReleaseTimer(){
        penta::FigureManager manager(nullptr,false);manager.apply({animation(true)});penta::runtime::FigureAction play;play.kind=penta::runtime::FigureActionKind::SetAnimationPlaying;play.animationPlaying=true;manager.apply({play});QTest::qWait(90);QVERIFY(manager.figureState(1)->animationPlaying);play.animationPlaying=false;manager.apply({play});QVERIFY(!manager.findChild<QTimer*>("fieldAnimationTimer")->isActive());play.animationPlaying=true;manager.apply({play});penta::runtime::FigureAction close;close.kind=penta::runtime::FigureActionKind::CloseAll;manager.apply({close});QTest::qWait(50);QCOMPARE(manager.figureCount(),0);
    }
    void invalidFramePreservesDisplayedSample(){
        penta::FigureManager manager(nullptr,false);manager.apply({animation()});QSignalSpy errors(&manager,&penta::FigureManager::error);penta::runtime::FigureAction select;select.kind=penta::runtime::FigureActionKind::SelectAnimationFrame;select.animationFrame=12;manager.apply({select});QCOMPARE(errors.count(),1);QCOMPARE(manager.figureState(1)->animationFrame,std::size_t(0));
    }
    void retainedRunGammaOverridesCurrentWorkspaceContext(){
        penta::FigureManager manager(nullptr,false);auto data=std::make_shared<penta::plotting::FieldAnimationData>(*frames());data->gamma=.25;auto action=animation();action.gamma=1;action.animation=data;manager.apply({action});const auto state=manager.figureState(1);QVERIFY(state);QCOMPARE(state->gamma,.25);QCOMPARE(state->animation->gamma,.25);
    }
    void replacingPlotDisarmsPlaybackAndKeepsCustomTitle(){
        penta::FigureManager manager(nullptr,false);manager.apply({animation()});penta::runtime::FigureAction title;title.kind=penta::runtime::FigureActionKind::SetTitle;title.text="A custom mathematical field";manager.apply({title});penta::runtime::FigureAction select;select.kind=penta::runtime::FigureActionKind::SelectAnimationFrame;select.animationFrame=1;manager.apply({select});QCOMPARE(manager.figureState(1)->title,QString("A custom mathematical field"));
        penta::runtime::FigureAction plot;plot.kind=penta::runtime::FigureActionKind::Plot;plot.algebraId="A_gamma_v1";plot.gamma=1;plot.dimensions=2;plot.axes={-1,-1,-1};plot.curves.push_back({{{0,0,0},{1,1,0}},"replacement"});manager.apply({plot});QVERIFY(!manager.figureState(1)->animation);QVERIFY(!manager.findChild<QTimer*>("fieldAnimationTimer")->isActive());QCOMPARE(manager.figureState(1)->curves[0].label,std::string("replacement"));
    }
};
PENTA_OFFSCREEN_TEST_MAIN(FieldAnimationWidgetTests)
#include "field_animation_widget_tests.moc"
