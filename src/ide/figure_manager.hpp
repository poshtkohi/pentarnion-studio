#pragma once

#include "runtime/interpreter.hpp"
#include "tile_layout_widgets.hpp"

#include <QWidget>
#include <QString>
#include <QPointF>
#include <QPointer>
#include <QHash>
#include <QRect>
#include <QAbstractNativeEventFilter>

#include <array>
#include <memory>
#include <optional>
#include <vector>

class QTabWidget;
class QToolBar;
class QStackedWidget;
class QGridLayout;
class QMenu;
class QTabBar;
class QDockWidget;

namespace penta {
namespace desktop {class FigureGroupHost;}

enum class TileLayoutTarget { Studio, Desktop, Subplots };

enum class PlotTool { Navigate, ZoomIn, ZoomOut, Pan, DataCursor, Rotate, GraphSelect, GraphDrag };

struct PlotDataPoint {
    std::size_t seriesIndex = 1, index = 1; // One-based stored series/sample indices.
    QString label;
    std::array<double,3> coordinates{}; // Original values, before render scaling.
    int dimensions = 0;
    std::optional<double> time; // Only set when explicit time metadata is available.
    std::optional<double> scalarValue; // Original field value at a mesh vertex.
    std::vector<double> originalCoefficients{}; // Complete arrow value when selected.
    std::vector<std::string> basisLabels{};
};

struct SubplotState {
    int index = 1; // One-based, row-major within the figure grid.
    int dimensions = 0; // Zero denotes a newly selected/cleared empty figure.
    std::array<int, 3> axes{{1, 2, 3}};
    std::vector<runtime::PlotVector> vectors; // Full original algebra coefficients.
    std::vector<runtime::PlotCurve> curves; // Sampled connected lines, without arrow glyphs.
    std::optional<runtime::PlotMesh> mesh;
    std::optional<runtime::PlotGraph> graph;
    QString algebraId = QStringLiteral("A_gamma_v1");
    double gamma = 1.0;
    QString title, xLabel, yLabel, zLabel, caption;
    bool titleLatex = true, xLabelLatex = true, yLabelLatex = true,
         zLabelLatex = true, captionLatex = true;
    runtime::PlotStyle style;
    std::shared_ptr<const plotting::FieldAnimationData> animation;
    std::size_t animationFrame=0,animationCoefficient=0;
    bool animationPlaying=false;
    std::vector<std::string> basisLabels{}; // Empty retains legacy (1,i,j,k,ell).
};

// The inherited fields mirror the active panel for existing clients.
struct FigureState : SubplotState {
    int number = 0;
    QString name;
    bool numberTitle = true;
    bool docked = true;
    int rows = 1, columns = 1, activeSubplot = 1;
    std::vector<SubplotState> subplots; // Allocated panels, ordered by index.
    std::vector<int> linkedPanels;
    unsigned linkedAxesMask=0;
    std::vector<int> linkedCameras;
};

// Figures can use Studio tabs/tiles or an owned floating figure-only window.
// renderingEnabled=false supports headless model tests, never fake image export.
class FigureManager final : public QWidget,public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit FigureManager(QWidget* parent = nullptr, bool renderingEnabled = true);
    ~FigureManager() override;
    void apply(const std::vector<runtime::FigureAction>& actions);
    bool exportFigure(int number, const QString& path, QString* error = nullptr);
    bool exportSubplot(int figure, int panel, const QString& path, QString* error = nullptr);
    bool exportFigure(int number,const QString& path,const runtime::FigureExportOptions& options,QString* error=nullptr);
    int figureCount() const;
    int currentFigureNumber() const;
    bool containsFigure(int number) const;
    std::optional<FigureState> figureState(int number) const;
    bool savePlotTemplate(int figure,int panel,const QString& path,bool overwrite=false,QString* error=nullptr)const;
    bool renderingEnabled() const { return m_renderingEnabled; }
    bool dockFigure(int number, QString* error = nullptr);
    bool undockFigure(int number, QString* error = nullptr);
    bool tileStudio(int rows = 0, int columns = 0, QString* error = nullptr);
    bool tileDesktop(int rows = 0, int columns = 0, QString* error = nullptr);
    bool tileSubplots(int figure,int rows=0,int columns=0,QString* error=nullptr);
    TileLayoutSpec tileLayout(TileLayoutTarget target,int figure=0) const;
    bool applyTileLayout(TileLayoutTarget target,const TileLayoutSpec& layout,
                         int figure=0,QString* error=nullptr);
    bool saveTileLayout(TileLayoutTarget target,const QString& path,int figure=0,
                        QString* error=nullptr) const;
    bool loadTileLayout(const QString& path,QString* error=nullptr);
    void showTabs();
    void closeAllFigures();
    bool splitFigures(int first,int second,Qt::Orientation orientation,QString* error=nullptr);
    bool tabifyFigures(int first,int second,QString* error=nullptr);
    bool saveFigureArrangement(const QString& path,QString* error=nullptr)const;
    bool loadFigureArrangement(const QString& path,QString* error=nullptr);
    bool groupedFigures()const{return m_grouped;}
    bool setPlotTool(int figure, int panel, PlotTool tool, QString* error = nullptr);
    bool zoomPlot(int figure, int panel, double factor, QString* error = nullptr);
    bool resetPlotView(int figure, int panel, QString* error = nullptr);
    bool setDataPointsVisible(int figure, int panel, bool visible, QString* error = nullptr);
    // Canvas position/tolerance use Qt logical pixels; picking snaps to a stored vertex.
    std::optional<PlotDataPoint> pickPlotPoint(int figure, int panel, QPointF localCanvasPosition,
                                             double tolerancePixels = 12.0);
    std::optional<PlotDataPoint> selectedDataPoint(int figure, int panel) const;
    std::vector<PlotDataPoint> dataTips(int figure, int panel) const;
    bool removeSelectedDataTip(int figure, int panel, QString* error = nullptr);
    bool clearDataTips(int figure, int panel, QString* error = nullptr);
    std::optional<PlotTool> plotTool(int figure, int panel) const;
    bool dataPointsVisible(int figure, int panel) const;
    bool moveGraphNode(int figure,int panel,std::uint32_t node,const std::array<double,3>& position,QString* error=nullptr);
    bool selectGraphItems(int figure,int panel,const std::vector<std::uint32_t>& nodes,
                          const std::vector<std::uint32_t>& edges,QString* error=nullptr);
    bool setGraphLayout(int figure,int panel,const QString& method,std::uint64_t seed=0,QString* error=nullptr);
    bool saveGraphLayout(int figure,int panel,const QString& path,QString* error=nullptr)const;
    bool loadGraphLayout(int figure,int panel,const QString& path,QString* error=nullptr);

signals:
    void error(const QString& message);
    void figuresChanged();

protected:
    bool eventFilter(QObject*,QEvent*)override;
    bool nativeEventFilter(const QByteArray&,void*,qintptr*)override;

private:
    friend class FigureDockingWorkflowTestAccess;
    struct Entry;
    Entry* entry(int number) const;
    Entry* selectFigure(int number);
    int nextAvailableFigure() const;
    bool applyOne(const runtime::FigureAction& action, QString* error);
    void closeFigure(int number);
    void updateToolbar();
    void updateSelectionBorders();
    void activateFigureWidget(QWidget* widget);
    void requestExport();
    void requestSubplotExport(int figure, int panel);
    bool exportSelection(int figure, std::optional<int> panel, const QString& path, QString* error,
                         const runtime::FigureExportOptions& options={});
    void synchronizeLinkedAxes(int figure,int source,bool interactive);
    void refreshNames();
    void rebuildInternalLayout();
    bool gridDimensions(int count, int& rows, int& columns, QString* error) const;
    QMenu* createTileMenu(bool desktop, QWidget* parent,int subplotFigure=0);
    void requestTileLayout(bool desktop);
    void requestSpanLayout(TileLayoutTarget target,int figure=0);
    void requestSavedTileLayout(TileLayoutTarget target,int figure,bool load);
    std::vector<int> tileItemIds(TileLayoutTarget target,int figure=0) const;
    QString displayName(const Entry& figure) const;
    void setGroupedFigures(bool grouped);
    void populateArrangementMenu(QMenu* menu);
    QRect figureGlobalCell(int number)const;
    QWidget* figureFloatingWindow(int number)const;
    void beginFigureDrag(int number,const QPoint& global);
    void updateFigureDropPreview(const QPoint& global);
    void finishFigureDrag(const QPoint& global);
    void cancelFigureDrag();
    QTabWidget* m_tabs = nullptr;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_tileHost = nullptr;
    QGridLayout* m_grid = nullptr;
    QToolBar* m_toolbar = nullptr;
    desktop::FigureGroupHost* m_groups = nullptr;
    bool m_renderingEnabled = true;
    bool m_tiled = false;
    bool m_grouped = true;
    bool m_rearranging = false;
    bool m_destroying = false;
    bool m_linkingAxes = false;
    int m_rows = 0, m_columns = 0;
    int m_desktopRows = 0, m_desktopColumns = 0;
    TileLayoutSpec m_studioLayout,m_desktopLayout;
    int m_selectedNumber = 0;
    int m_dragFigure=0,m_dropFigure=0;
    QPointer<QWidget> m_dragPage,m_dropPage,m_dropPreview;
    QPointer<QTabBar> m_dragTabBar;
    QPointer<QDockWidget> m_dragDock;
    QPoint m_dragStart;
    QRect m_dropBounds;
    QHash<int,QRect> m_dropCells;
    QString m_dropZone;
    Qt::DockWidgetAreas m_dragAllowedAreas=Qt::AllDockWidgetAreas;
    bool m_dragMoved=false,m_dragAreasRestricted=false,m_sourceTabDrop=false;
    bool m_nativeMoveActive=false,m_nativeInputDrag=false;
    quintptr m_nativeMoveWindow=0;
    unsigned m_dragEpoch=0;
    std::vector<std::unique_ptr<Entry>> m_figures;
};

} // namespace penta
