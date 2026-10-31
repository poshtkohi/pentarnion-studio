#pragma once

#include <QMainWindow>
#include <QHash>
#include <QPointer>
#include <QRect>
#include <functional>
#include <vector>

class QDockWidget;

namespace penta::desktop {
class FigureDockController;

// Renderer-independent GTG-style figure groups. Existing view widgets move;
// their scenes, data, camera and background-render recipients are not rebuilt.
class FigureGroupHost final:public QMainWindow {
public:
    explicit FigureGroupHost(QWidget* parent=nullptr);
    ~FigureGroupHost()override;
    bool addFigure(int id,const QString& title,QWidget* view);
    QWidget* takeFigure(int id); // Hidden, detached, owned by the caller.
    bool containsFigure(int id)const{return figures_.contains(id);}
    int count()const{return figures_.size();}
    int currentFigure()const{return current_;}
    QWidget* figureWidget(int id)const;
    QDockWidget* dockWidget(int id)const;
    FigureDockController* dockingController(int id)const;
    bool setFigureTitle(int id,const QString& title);
    bool setCurrentFigure(int id);
    bool splitFigure(int first,int second,Qt::Orientation orientation,bool before=false);
    // Renderer-independent migration of an existing tiled arrangement. Rects
    // are logical host coordinates and never contain plot/result data.
    bool arrangeRectangles(const QHash<int,QRect>& rectangles);
    QRect figureCellRect(int id)const;
    bool tabifyFigures(int first,int second);
    bool floatFigure(int id,bool floating=true);
    void showTabs();
    QByteArray saveState(int version=1)const;
    bool restoreState(const QByteArray& state,int version=1);

    std::function<void(int)> selected;
    std::function<void(int)> closeRequested;
    std::function<void(int,bool)> dockedChanged;
protected:
    bool eventFilter(QObject* object,QEvent* event)override;
private:
    struct Figure {
        QPointer<QDockWidget> dock;
        QPointer<QWidget> view;
        QPointer<FigureDockController> controller;
    };
    void watch(QWidget* widget);
    void removeDestroyedFigure(int id);
    QHash<int,bool> dockStates()const;
    void notifyDockChanges(const QHash<int,bool>& before);
    struct Cell {QPointer<QDockWidget> dock;QRect rect;QList<QPointer<QDockWidget>> peers;};
    bool arrangeCells(std::vector<Cell> cells,int keepFirstPair=0);
    QHash<int,Figure> figures_;
    int current_=0;
    bool changing_=false,destroying_=false,watchPending_=false;
    unsigned splitEpoch_=0;
};
}
