#pragma once

#include <QGridLayout>
#include <QJsonObject>
#include <QWidget>
#include <QString>
#include <functional>
#include <optional>
#include <vector>

namespace penta {

struct TilePlacement {
    int id=0,row=0,column=0,rowSpan=1,columnSpan=1;
    bool operator==(const TilePlacement&) const = default;
};
struct TileLayoutSpec {
    int rows=1,columns=1;
    std::vector<TilePlacement> placements;
    std::vector<int> rowWeights,columnWeights;
    bool operator==(const TileLayoutSpec&) const = default;
};
bool validateTileLayout(const TileLayoutSpec&,const std::vector<int>& required,QString* error=nullptr);
TileLayoutSpec regularTileLayout(int rows,int columns,const std::vector<int>& ids,bool indexed=false);
QJsonObject tileLayoutJson(const TileLayoutSpec&);
std::optional<TileLayoutSpec> parseTileLayoutJson(const QJsonObject&,QString* error=nullptr);

// Small native grid selector. The highlighted rectangle is a live preview;
// release or Enter chooses the dimensions. No layout changes occur on hover.
class TileGridPicker final:public QWidget {
    Q_OBJECT
public:
    explicit TileGridPicker(QWidget* parent=nullptr);
    void setRequiredCount(int count);
    int previewRows()const{return m_rows;}
    int previewColumns()const{return m_columns;}
    QRect cellRect(int row,int column)const;
    QSize sizeHint()const override;
signals:
    void layoutChosen(int rows,int columns);
    void previewChanged(int rows,int columns,bool valid);
protected:
    void paintEvent(QPaintEvent*)override;
    void mouseMoveEvent(QMouseEvent*)override;
    void mousePressEvent(QMouseEvent*)override;
    void mouseReleaseEvent(QMouseEvent*)override;
    void keyPressEvent(QKeyEvent*)override;
private:
    void previewAt(QPoint);
    void updatePreview(int rows,int columns);
    bool valid()const;
    int m_rows=1,m_columns=1,m_required=0;
    bool m_pressed=false;
};

// A host for the existing QGridLayout. Its narrow transparent handles resize
// row/column stretches without replacing plot widgets or their native cameras.
class TileLayoutHost final:public QWidget {
    Q_OBJECT
public:
    explicit TileLayoutHost(QWidget* parent=nullptr);
    void setTileGrid(QGridLayout* grid);
    void setTileWeights(int rows,int columns,const std::vector<int>& rowWeights,
                        const std::vector<int>& columnWeights);
    std::function<void(const std::vector<int>&,const std::vector<int>&)> weightsChanged;
    QGridLayout* tileGrid()const{return m_grid;}
protected:
    bool event(QEvent*)override;
    void resizeEvent(QResizeEvent*)override;
    void showEvent(QShowEvent*)override;
    bool eventFilter(QObject*,QEvent*)override;
private:
    void queueHandleRefresh();
    void refreshHandles();
    void dragHandle(bool vertical,int index,int coordinate);
    QGridLayout* m_grid=nullptr;
    std::vector<QWidget*> m_verticalHandles,m_horizontalHandles;
    std::vector<int> m_rowWeights,m_columnWeights;
    int m_rows=0,m_columns=0;
    bool m_handleRefreshQueued=false;
    // Qt can reuse cellRect data for a height-for-width size-hint probe. Keep
    // the last geometry-validated track snapshot for overlay and drag use.
    std::vector<QRect> m_columnCells,m_rowCells,m_itemGeometries;
    QRect m_gridGeometry;
    std::vector<int> m_gridParameters;
    bool m_cellsValid=false;
};

// Visual span editor. Select an item, then drag a rectangle in free cells.
// Overlap/out-of-grid edits reject without modifying the proposed layout.
class TileSpanEditor final:public QWidget {
    Q_OBJECT
public:
    explicit TileSpanEditor(QWidget* parent=nullptr);
    void setSpec(const TileLayoutSpec& spec);
    const TileLayoutSpec& spec()const{return m_spec;}
    void setSelectedId(int id);
    QRect cellRect(int row,int column)const;
    QSize sizeHint()const override;
signals:
    void layoutEdited();
    void editRejected(const QString& reason);
protected:
    void paintEvent(QPaintEvent*)override;
    void mousePressEvent(QMouseEvent*)override;
    void mouseMoveEvent(QMouseEvent*)override;
    void mouseReleaseEvent(QMouseEvent*)override;
private:
    std::optional<QPoint> cellAt(QPoint)const;
    TileLayoutSpec m_spec;
    int m_selected=0;
    std::optional<QPoint> m_start,m_end;
};
}
