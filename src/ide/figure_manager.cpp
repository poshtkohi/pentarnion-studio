#include "figure_manager.hpp"
#include "math_text_renderer.hpp"
#include "plotting/field_animation.hpp"
#include "plotting/graph_view.hpp"
#include "plotting/native_export.hpp"
#include "plotting/plot_template.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QDomDocument>
#include <QCoreApplication>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QComboBox>
#include <QHelpEvent>
#include <QImage>
#include <QLabel>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenu>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QSpinBox>
#include <QSlider>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QPointer>
#include <QPainter>
#include <QPalette>
#include <QRegion>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QFontMetricsF>
#include <QPolygonF>
#include <QSvgGenerator>
#include <QSvgRenderer>
#include <QSignalBlocker>
#include <QStyle>
#include <QWheelEvent>
#include <QWidgetAction>
#include <QWindow>
#include <QSaveFile>
#include <QTabWidget>
#include <QToolBar>
#include <QToolTip>
#include <QVBoxLayout>

#include "raster_plot_canvas.hpp"
#include "raster_depth_painter.hpp"
#include "desktop/figure_group_host.hpp"
#include "desktop/figure_docking.hpp"
#include <QAbstractButton>
#include <QTabBar>
#include <QCursor>
#include <QDockWidget>
#include <QJsonArray>
#include <QSet>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <vtkGenericRenderWindowInteractor.h>
#include <vtkActor.h>
#include <vtkActorCollection.h>
#include <vtkArrowSource.h>
#include <vtkCamera.h>
#include <vtkCellPicker.h>
#include <vtkCallbackCommand.h>
#include <vtkCellArray.h>
#include <vtkCommand.h>
#include <vtkCoordinate.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkInteractorStyleImage.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkJPEGWriter.h>
#include <vtkLegendBoxActor.h>
#include <vtkLineSource.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkPNGWriter.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkPlaneSource.h>
#include <vtkPlane.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkPointData.h>
#include <vtkFloatArray.h>
#include <vtkDoubleArray.h>
#include <vtkScalarBarActor.h>
#include <vtkLookupTable.h>
#include <vtkCleanPolyData.h>
#include <vtkClipPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkProperty2D.h>
#include <vtkPropCollection.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkSmartPointer.h>
#include <vtkSphereSource.h>
#include <vtkTextActor.h>
#include <vtkTriangleFilter.h>
#include <vtkTextProperty.h>
#include <vtkTexturedActor2D.h>
#include <vtkTexture.h>
#include <vtkUnsignedCharArray.h>

#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <functional>

namespace penta {
namespace {
constexpr int maximumFigures = 64;
constexpr std::size_t maximumVectors = 256;
constexpr std::size_t maximumCurvePoints = 1000000;
constexpr std::size_t maximumRetainedPoints = 2000000;
constexpr int maximumPanels = 64;
constexpr std::size_t maximumRetainedPanels = 128;
constexpr const char* basisNames[] = {"1", "i", "j", "k", "ell"};

// GTG uses a two-pixel figure perimeter and a one-pixel active subplot
// perimeter, both MATLAB blue. Keep this chrome outside the scientific scene:
// selecting a view must never invalidate its CPU image or change its camera.
class FigureSelectionBorder final:public QWidget {
public:
    FigureSelectionBorder(QWidget* parent,int width,const QString& name):QWidget(parent),width_(width){
        setObjectName(name);setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);setFocusPolicy(Qt::NoFocus);
        setProperty("studioSelectionColor",QColor(0,114,189));setProperty("studioSelectionWidth",width);
        parent->installEventFilter(this);syncGeometry();hide();
    }
    void setSelected(bool selected){
        if(selected_==selected)return;
        selected_=selected;setProperty("studioSelectionActive",selected);
        setVisible(selected);if(selected)raise();
    }
protected:
    bool eventFilter(QObject* watched,QEvent* event)override{
        if(watched==parentWidget()&&(event->type()==QEvent::Resize||event->type()==QEvent::Show))syncGeometry();
        return QWidget::eventFilter(watched,event);
    }
    void paintEvent(QPaintEvent*)override{
        QPainter painter(this);painter.setPen(QPen(QColor(0,114,189),width_));painter.setBrush(Qt::NoBrush);
        const qreal inset=width_==2?1:0.5;
        painter.drawRect(QRectF(rect()).adjusted(inset,inset,-inset,-inset));
    }
private:
    void syncGeometry(){
        if(!parentWidget())return;
        setGeometry(parentWidget()->rect());
        // Only this narrow perimeter is dirty when selection changes. No
        // layout margin, standalone rail or extra divider is introduced.
        const int edge=width_+1;
        setMask(QRegion(rect()).subtracted(QRegion(rect().adjusted(edge,edge,-edge,-edge))));
        if(selected_)raise();
    }
    int width_;bool selected_=false;
};
QString basisName(const SubplotState& state,int coordinate) {
    if(coordinate<0)return {};
    if(!state.basisLabels.empty())return static_cast<std::size_t>(coordinate)<state.basisLabels.size()?QString::fromStdString(state.basisLabels[static_cast<std::size_t>(coordinate)]):QString();
    return coordinate<5?QString::fromLatin1(basisNames[coordinate]):QString();
}
QString algebraDisplayName(const SubplotState& state) {
    if(state.basisLabels.empty())return state.algebraId;
    if(state.algebraId.startsWith(QStringLiteral("builtin/")))return state.algebraId.section('/',1,1);
    return QStringLiteral("table %1").arg(state.algebraId.section('/',-1).left(12));
}


bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}
QFont publicationFont(const runtime::PlotStyle& style,int kind=-1){
    QFont font(style.fontFamily=="serif"?QStringLiteral("Times New Roman"):style.fontFamily=="mono"?QStringLiteral("Courier New"):QStringLiteral("Arial"));
    font.setPointSizeF(style.fontSize+(kind==0?5:kind>0?2:0));return font;
}
vtkSmartPointer<vtkPolyData> dashedCurve(vtkPoints* input,const std::string& style){
    std::vector<double> pattern=style=="dash"?std::vector<double>{.06,.035}:style=="dot"?std::vector<double>{.01,.025}:std::vector<double>{.06,.02,.01,.02};
    long double total=0;for(vtkIdType k=1;k<input->GetNumberOfPoints();++k){double a[3],b[3];input->GetPoint(k-1,a);input->GetPoint(k,b);total+=std::hypot(b[0]-a[0],b[1]-a[1],b[2]-a[2]);}
    const double stretch=static_cast<double>(std::max(1.L,total/(std::accumulate(pattern.begin(),pattern.end(),0.)*250000)));
    for(auto& value:pattern)value*=stretch;
    vtkNew<vtkPoints> points;points->SetDataTypeToDouble();vtkNew<vtkCellArray> lines;std::size_t phase=0;double used=0;
    for(vtkIdType k=1;k<input->GetNumberOfPoints();++k){double a[3],b[3];input->GetPoint(k-1,a);input->GetPoint(k,b);const double length=std::hypot(b[0]-a[0],b[1]-a[1],b[2]-a[2]);if(!std::isfinite(length)||length==0)continue;
        double offset=0;while(offset<length){const double step=std::min(length-offset,pattern[phase]-used);if(step<=0){phase=(phase+1)%pattern.size();used=0;continue;}
            if(phase%2==0){vtkIdType ids[2];for(int end=0;end<2;++end){double point[3];const double t=(offset+end*step)/length;for(int d=0;d<3;++d)point[d]=a[d]+t*(b[d]-a[d]);ids[end]=points->InsertNextPoint(point);}lines->InsertNextCell(2,ids);}
            offset+=step;used+=step;if(used>=pattern[phase]*(1-1e-12)){phase=(phase+1)%pattern.size();used=0;}
        }
    }
    auto data=vtkSmartPointer<vtkPolyData>::New();data->SetPoints(points);data->SetLines(lines);return data;
}

QString vectorLabel(const runtime::PlotVector& vector, std::size_t index) {
    return vector.label.empty() ? QStringLiteral("Vector %1").arg(index + 1)
                                : QString::fromStdString(vector.label);
}

QString coordinateNames(const SubplotState& state) {
    QStringList names;
    for (int index = 0; index < state.dimensions; ++index)
        names.append(state.axes[static_cast<std::size_t>(index)] < 0
            ? QString::fromLatin1(index == 0 ? "x" : index == 1 ? "y" : "z")
            : basisName(state,state.axes[static_cast<std::size_t>(index)]));
    return names.join(QStringLiteral(", "));
}

QString contextText(const SubplotState& state) {
    auto text=QStringLiteral("Coordinates (%1) | %2").arg(coordinateNames(state),algebraDisplayName(state));
    if(state.basisLabels.empty())text+=QStringLiteral(" | gamma=%1").arg(state.gamma,0,'g',15);
    if(state.animation&&state.animationFrame<state.animation->selectedFrames.size()){
        const auto source=state.animation->selectedFrames[state.animationFrame];
        text+=QStringLiteral(" | t=%1 | coefficient %2").arg(state.animation->t[source],0,'g',10).arg(state.animationCoefficient);
    }
    return text;
}

QString vectorDescription(const SubplotState& state, const runtime::PlotVector& vector,
                          std::size_t index) {
    QStringList values;
    QStringList names;for(std::size_t n=0;n<vector.allCoefficients().size();++n)names.append(basisName(state,static_cast<int>(n)));
    for (const double coefficient : vector.allCoefficients()) values.append(QString::number(coefficient, 'g', 17));
    return QStringLiteral("%1\n(%2) = [%3]\n%4\nDefinition: %5")
        .arg(vectorLabel(vector,index),names.join(QStringLiteral(", ")),values.join(QStringLiteral(", ")),contextText(state),state.algebraId);
}

class VectorCanvas final : public RasterPlotCanvas {
public:
    explicit VectorCanvas(QWidget* parent) : RasterPlotCanvas(parent) {
        setObjectName(QStringLiteral("figureCanvas"));
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(320, 240);
        setToolTip(QStringLiteral("Hover a vector to inspect every original algebra coefficient."));
    }
    vtkRenderer* renderer = nullptr;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow_;
    void setRenderWindow(vtkGenericOpenGLRenderWindow* value){renderWindow_=value;}
    vtkGenericOpenGLRenderWindow* renderWindow()const{return renderWindow_;}
    std::map<vtkProp*, QString> descriptions;
    std::function<void()> activated;
    std::function<bool(QEvent*)> interaction;
protected:
    bool event(QEvent* event) override {
        if ((event->type()==QEvent::MouseButtonPress||event->type()==QEvent::FocusIn) && activated) activated();
        if(interaction&&interaction(event))return true;
        if (event->type() == QEvent::ToolTip && renderer && isValid()) {
            const auto* help = static_cast<QHelpEvent*>(event);
            vtkNew<vtkCellPicker> picker;
            picker->SetTolerance(0.025);
            const double ratio = devicePixelRatioF();
            if (picker->Pick(help->pos().x() * ratio, (height() - 1 - help->pos().y()) * ratio,
                             0.0, renderer)) {
                const auto found = descriptions.find(picker->GetViewProp());
                if (found != descriptions.end()) {
                    QString html = found->second.toHtmlEscaped();
                    html.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
                    QToolTip::showText(help->globalPos(), QStringLiteral("<p>%1</p>").arg(html), this);
                    return true;
                }
            }
        }
        return RasterPlotCanvas::event(event);
    }
};

class FloatingFigureWindow final : public QMainWindow {
public:
    explicit FloatingFigureWindow(QWidget* owner) : QMainWindow(owner,Qt::Window) {
        setObjectName(QStringLiteral("floatingFigureWindow"));
    }
    std::function<void()> closeRequested;
    std::function<void()> activated;
protected:
    void closeEvent(QCloseEvent* event) override {
        event->ignore();
        if(closeRequested)closeRequested();
    }
    bool event(QEvent* event) override {
        const bool result=QMainWindow::event(event);
        if(event->type()==QEvent::WindowActivate&&activated)activated();
        return result;
    }
};

vtkSmartPointer<vtkTexturedActor2D> imageOverlay(const QImage& input, vtkSmartPointer<vtkPlaneSource>& geometry) {
    const QImage image=input.convertToFormat(QImage::Format_RGBA8888);
    auto pixels=vtkSmartPointer<vtkImageData>::New();
    pixels->SetDimensions(image.width(),image.height(),1);
    pixels->AllocateScalars(VTK_UNSIGNED_CHAR,4);
    for(int y=0;y<image.height();++y)
        std::memcpy(pixels->GetScalarPointer(0,y,0),image.constScanLine(image.height()-1-y),
                    static_cast<std::size_t>(image.width())*4);
    auto texture=vtkSmartPointer<vtkTexture>::New();
    texture->SetInputData(pixels);texture->InterpolateOn();texture->RepeatOff();
    auto plane=vtkSmartPointer<vtkPlaneSource>::New();
    plane->SetOrigin(0,0,0);plane->SetPoint1(image.width(),0,0);plane->SetPoint2(0,image.height(),0);
    auto mapper=vtkSmartPointer<vtkPolyDataMapper2D>::New();mapper->SetInputConnection(plane->GetOutputPort());
    auto actor=vtkSmartPointer<vtkTexturedActor2D>::New();actor->SetMapper(mapper);actor->SetTexture(texture);
    actor->GetPositionCoordinate()->SetCoordinateSystemToDisplay();
    geometry=plane;
    return actor;
}

vtkSmartPointer<vtkActor> sourceActor(vtkAlgorithmOutput* output, const double* color) {
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(output);
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(color[0],color[1],color[2]);
    actor->GetProperty()->SetAmbient(0.3);
    actor->GetProperty()->SetDiffuse(0.7);
    return actor;
}

QIcon plotIcon(const QString& drawing,QStyle::StandardPixmap fallback) {
    // Render through the linked SVG module; standalone builds do not need an
    // image-format plugin just to display their plot controls.
    const QByteArray svg=(QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='18' height='18' viewBox='0 0 18 18'><g fill='none' stroke='#334155' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round'>")
        +drawing+QStringLiteral("</g></svg>")).toUtf8();
    QSvgRenderer renderer(svg);
    if(renderer.isValid()){
        QPixmap pixmap(36,36);pixmap.setDevicePixelRatio(2);pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);renderer.render(&painter,QRectF(0,0,18,18));painter.end();return QIcon(pixmap);
    }
    return QApplication::style()->standardIcon(fallback);
}

constexpr qint64 maximumSvgBytes=128*1024*1024;
class SvgBuffer final:public QBuffer {
public:
    bool limitReached=false;
protected:
    qint64 writeData(const char* bytes,qint64 count) override {
        if(count<0||pos()>maximumSvgBytes-count){limitReached=true;setErrorString(QStringLiteral("SVG export exceeds 128 MiB."));return -1;}
        return QBuffer::writeData(bytes,count);
    }
};

QString svgPath(const QPainterPath& path) {
    QString result;
    auto number=[](double x){return QString::number(x,'g',12);};
    for(int index=0;index<path.elementCount();++index){const auto element=path.elementAt(index);
        if(element.isMoveTo())result+=QStringLiteral("M%1 %2").arg(number(element.x),number(element.y));
        else if(element.isLineTo())result+=QStringLiteral("L%1 %2").arg(number(element.x),number(element.y));
        else if(element.type==QPainterPath::CurveToElement&&index+2<path.elementCount()){
            const auto second=path.elementAt(++index),last=path.elementAt(++index);
            result+=QStringLiteral("C%1 %2 %3 %4 %5 %6").arg(number(element.x),number(element.y),number(second.x),number(second.y),number(last.x),number(last.y));
        }
    }
    return result;
}

bool outlineSvgText(QByteArray& bytes,QString* error) {
    QDomDocument document;
#if QT_VERSION >= QT_VERSION_CHECK(6,5,0)
    if(!document.setContent(bytes))return fail(error,QStringLiteral("The generated SVG document could not be validated."));
#else
    if(!document.setContent(bytes,false))return fail(error,QStringLiteral("The generated SVG document could not be validated."));
#endif
    qint64 outlinedBytes=bytes.size();
    std::function<bool(QDomElement,QFont)> visit=[&](QDomElement element,QFont font){
        if(element.hasAttribute(QStringLiteral("font-family")))font.setFamily(element.attribute(QStringLiteral("font-family")));
        if(element.hasAttribute(QStringLiteral("font-size")))font.setPointSizeF(element.attribute(QStringLiteral("font-size")).toDouble()*72.0/96.0);
        if(element.hasAttribute(QStringLiteral("font-weight"))){const QString weight=element.attribute(QStringLiteral("font-weight"));
            font.setWeight(static_cast<QFont::Weight>(weight==QStringLiteral("normal")?400:weight==QStringLiteral("bold")?700:std::clamp(weight.toInt(),1,1000)));}
        if(element.hasAttribute(QStringLiteral("font-style")))font.setItalic(element.attribute(QStringLiteral("font-style"))==QStringLiteral("italic"));
        if(element.tagName()==QStringLiteral("text")){
            const QString text=element.text();
            double x=element.attribute(QStringLiteral("x")).toDouble(),y=element.attribute(QStringLiteral("y")).toDouble();
            const QString anchor=element.attribute(QStringLiteral("text-anchor"));
            if(anchor==QStringLiteral("middle"))x-=QFontMetricsF(font).horizontalAdvance(text)*0.5;
            else if(anchor==QStringLiteral("end"))x-=QFontMetricsF(font).horizontalAdvance(text);
            QPainterPath path;path.addText(QPointF(x,y),font,text);
            const QString data=svgPath(path);outlinedBytes+=data.size();
            if(outlinedBytes>maximumSvgBytes)return false;
            auto replacement=document.createElement(path.isEmpty()?QStringLiteral("g"):QStringLiteral("path"));
            const auto attributes=element.attributes();
            for(int index=0;index<attributes.count();++index){const auto attr=attributes.item(index).toAttr();
                if(attr.name()!=QStringLiteral("x")&&attr.name()!=QStringLiteral("y"))replacement.setAttribute(attr.name(),attr.value());}
            if(!path.isEmpty())replacement.setAttribute(QStringLiteral("d"),data);
            replacement.setAttribute(QStringLiteral("data-text"),text);
            auto title=document.createElement(QStringLiteral("title"));title.appendChild(document.createTextNode(text));replacement.appendChild(title);
            element.parentNode().replaceChild(replacement,element);return true;
        }
        for(auto child=element.firstChild();!child.isNull();){const auto next=child.nextSibling();
            if(child.isElement()&&!visit(child.toElement(),font))return false;
            child=next;}
        return true;
    };
    if(!visit(document.documentElement(),QApplication::font()))return fail(error,QStringLiteral("Outlined SVG text exceeds the 128 MiB export limit."));
    // Use explicit SVG pixels rather than the generator's physical-mm size;
    // readers otherwise disagree about the legacy 90-versus-96 DPI mapping.
    auto root=document.documentElement();const auto bounds=root.attribute(QStringLiteral("viewBox")).split(QLatin1Char(' '),Qt::SkipEmptyParts);
    if(bounds.size()==4){root.setAttribute(QStringLiteral("width"),bounds[2]+QStringLiteral("px"));root.setAttribute(QStringLiteral("height"),bounds[3]+QStringLiteral("px"));}
    bytes=document.toByteArray(0);
    if(bytes.size()>maximumSvgBytes)return fail(error,QStringLiteral("SVG export exceeds 128 MiB."));
    return true;
}
}

namespace {
struct FigurePanel {
    SubplotState state;
    QWidget* page = nullptr;
    VectorCanvas* canvas = nullptr;
    QToolBar* toolbar = nullptr;
    FigureSelectionBorder* selectionBorder = nullptr;
    std::map<PlotTool,QAction*> toolActions;
    QAction* pointsAction = nullptr;
    QAction* clearTipsAction = nullptr;
    QAction* saveAction = nullptr;
    std::function<void()> saveRequested;
    PlotTool tool = PlotTool::Navigate;
    bool pointsVisible = false, dragging = false, picking = false;
    bool detachedPaintScene = false;
    QFont capturedDefaultFont;
    std::shared_ptr<std::atomic_bool> paintCancelled;
    std::function<void()> activate;
    std::function<void()> viewChanged;
    std::function<void()> stateChanged;
    QTimer* animationTimer=nullptr;
    QAction *animationPlay=nullptr,*animationPrevious=nullptr,*animationNext=nullptr;
    QSlider* animationSlider=nullptr;
    QComboBox* animationComponent=nullptr;
    QLabel* animationTime=nullptr;
    QString animationDefaultTitle;
    PlotTool dragTool = PlotTool::Pan;
    std::optional<std::uint32_t> draggedGraphNode;
    std::array<double,3> graphDragStart{},graphDragWorld{};
    struct GraphText {std::array<double,3> point;vtkSmartPointer<vtkTextActor> actor;};
    std::vector<GraphText> graphText;
    std::vector<plotting::GraphSegment> graphLines;
    QAction* graphLayoutAction=nullptr;
    bool graphContains(const std::vector<std::uint32_t>& ids,std::uint32_t id)const{return std::find(ids.begin(),ids.end(),id)!=ids.end();}
    std::optional<PlotDataPoint> graphPick(QPointF position,double tolerance)const {
        if(!state.graph||!renderer||!canvas||!window)return {};
        const auto& graph=*state.graph;double nearest=tolerance*tolerance;std::optional<PlotDataPoint> found;
        auto projected=[&](const std::array<double,3>& point){const auto world=worldPoint(point);renderer->SetWorldPoint(world[0],world[1],world[2],1);renderer->WorldToDisplay();const double* v=renderer->GetDisplayPoint();const int* size=window->GetSize();return QPointF(v[0]*canvas->width()/std::max(1,size[0]),(size[1]-1-v[1])*canvas->height()/std::max(1,size[1]));};
        for(std::size_t n=0;n<graph.positions.size();++n){const auto delta=projected(graph.positions[n])-position;const double distance=delta.x()*delta.x()+delta.y()*delta.y();if(distance<=nearest){nearest=distance;found=PlotDataPoint{1,n+1,QStringLiteral("Node %1").arg(QString::fromStdString(plotting::graphNodeLabel(graph,static_cast<std::uint32_t>(n+1)))),graph.positions[n],graph.dimensions,{},{}};}}
        // Nodes take priority over incident edges, even at an exact endpoint.
        if(found)return found;
        for(const auto& line:graphLines){const auto a=projected(line.first),b=projected(line.last),delta=b-a;const double square=delta.x()*delta.x()+delta.y()*delta.y();const auto from=position-a;const double t=square>0?std::clamp((from.x()*delta.x()+from.y()*delta.y())/square,0.,1.):0;const auto offset=a+t*delta-position;const double distance=offset.x()*offset.x()+offset.y()*offset.y();if(distance<=nearest){nearest=distance;std::array<double,3> raw{};for(std::size_t d=0;d<3;++d)raw[d]=(1-t)*line.first[d]+t*line.last[d];const auto endpoints=graph.edges[line.edgeId-1];found=PlotDataPoint{2,line.edgeId,QStringLiteral("Edge %1: %2 %3 %4").arg(line.edgeId).arg(endpoints[0]).arg(graph.directed?QStringLiteral("→"):QStringLiteral("—")).arg(endpoints[1]),raw,graph.dimensions,{},graph.weights[line.edgeId-1]};}}
        return found;
    }
    void graphSelect(const std::optional<PlotDataPoint>& point,bool additive){
        auto& graph=*state.graph;if(!additive){graph.selectedNodes.clear();graph.selectedEdges.clear();}
        if(point){auto& ids=point->seriesIndex==1?graph.selectedNodes:graph.selectedEdges;const auto id=static_cast<std::uint32_t>(point->index);const auto found=std::find(ids.begin(),ids.end(),id);if(found==ids.end())ids.push_back(id);else if(additive)ids.erase(found);}
        rebuild(false,true);if(stateChanged)stateChanged();
    }
    bool changeGraphLayout(const std::string& method,std::uint64_t seed,QString* error){
        if(!state.graph)return fail(error,QStringLiteral("This subplot is not a graph."));
        try{auto graph=*state.graph;graph.positions=plotting::graphPositions(graph.positions.size(),graph.dimensions,method,graph.radius,seed);graph.layout=method;graph.seed=seed;plotting::validateGraphView(graph);state.graph=std::move(graph);clearAllTips();rebuild();if(stateChanged)stateChanged();return true;}catch(const plotting::ExportError& e){return fail(error,QString::fromUtf8(e.what()));}
    }
    bool saveGraph(const QString& path,QString* error)const{
        if(!state.graph)return fail(error,QStringLiteral("This subplot is not a graph."));
        try{const auto json=plotting::graphLayoutJson(*state.graph);QSaveFile file(path);file.setDirectWriteFallback(false);if(!file.open(QIODevice::WriteOnly)||file.write(json.data(),static_cast<qint64>(json.size()))!=static_cast<qint64>(json.size())||!file.commit())return fail(error,QStringLiteral("Cannot save graph layout: ")+file.errorString());return true;}catch(const plotting::ExportError& e){return fail(error,QString::fromUtf8(e.what()));}
    }
    bool loadGraph(const QString& path,QString* error){
        if(!state.graph)return fail(error,QStringLiteral("This subplot is not a graph."));
        QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>16*1024*1024)return fail(error,QStringLiteral("Cannot read graph layout or file exceeds16 MiB."));
        try{auto graph=plotting::graphLayoutFromJson(file.readAll().toStdString(),*state.graph);state.graph=std::move(graph);clearAllTips();rebuild();if(stateChanged)stateChanged();return true;}catch(const plotting::ExportError& e){return fail(error,QString::fromUtf8(e.what()));}
    }
    QPointF lastDragPosition;
    std::array<double,3> renderCenter{}, renderUnits{{1,1,1}};
    std::vector<vtkSmartPointer<vtkActor>> pointActors;
    std::optional<PlotDataPoint> selectedPoint;
    struct DataTip {
        PlotDataPoint point;
        vtkSmartPointer<vtkTextActor> text,marker;
        QRectF hitRectangle;
        QPointF markerPosition;
    };
    std::vector<DataTip> dataTips;
    std::optional<std::size_t> selectedTip;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> window;
    vtkSmartPointer<vtkRenderer> renderer;
    vtkSmartPointer<vtkRenderer> annotationRenderer;
    std::array<QImage,5> labelImages;
    std::array<QImage,5> rasterLabelImages;
    struct Overlay {int kind;QSize size;QSize displayed;vtkSmartPointer<vtkTexturedActor2D> actor;vtkSmartPointer<vtkPlaneSource> plane;};
    std::vector<Overlay> overlays;
    struct TickLabel {std::array<double,3> point;int axis;vtkSmartPointer<vtkTextActor> actor;};
    std::vector<TickLabel> tickLabels;
    vtkSmartPointer<vtkTextActor> contextCaption;
    vtkSmartPointer<vtkLegendBoxActor> legend;
    unsigned long renderObserver = 0;

    ~FigurePanel() {
        if(window&&renderObserver)window->RemoveObserver(renderObserver);
        if(canvas){canvas->activated={};canvas->interaction={};}
        delete page;
    }

    void setTool(PlotTool value) {
        tool=value;dragging=false;draggedGraphNode.reset();
        for(const auto& [mode,action]:toolActions){const QSignalBlocker blocked(action);action->setChecked(mode==tool);}
        if(canvas)canvas->setCursor(tool==PlotTool::Pan?Qt::SizeAllCursor:
            tool==PlotTool::DataCursor||tool==PlotTool::ZoomIn||tool==PlotTool::ZoomOut?Qt::CrossCursor:
            tool==PlotTool::Rotate||tool==PlotTool::GraphDrag?Qt::OpenHandCursor:
            tool==PlotTool::GraphSelect?Qt::PointingHandCursor:Qt::ArrowCursor);
    }

    std::array<double,3> worldPoint(const std::array<double,3>& point) const {
        std::array<double,3> result{};
        for(int axis=0;axis<state.dimensions;++axis){const auto at=static_cast<std::size_t>(axis);
            result[at]=static_cast<double>((static_cast<long double>(point[at])-renderCenter[at])/renderUnits[at]);}
        return result;
    }

    QPointF displayPosition(QPointF point) const {
        const int* size=window->GetSize();
        return {point.x()*size[0]/std::max(1,canvas->width()),
            size[1]-1-point.y()*size[1]/std::max(1,canvas->height())};
    }

    std::array<double,3> worldAt(QPointF point) const {
        const auto display=displayPosition(point);
        const double* focus=renderer->GetActiveCamera()->GetFocalPoint();
        renderer->SetWorldPoint(focus[0],focus[1],focus[2],1);renderer->WorldToDisplay();
        const double depth=renderer->GetDisplayPoint()[2];
        renderer->SetDisplayPoint(display.x(),display.y(),depth);renderer->DisplayToWorld();
        const double* world=renderer->GetWorldPoint();
        return {world[0]/world[3],world[1]/world[3],world[2]/world[3]};
    }

    void constrainCamera() {
        if(!renderer)return;
        auto* camera=renderer->GetActiveCamera();
        const double fit=state.dimensions==2?(state.axes[0]<0?1.25:1.45):2.0;
        const double current=camera->GetParallelScale();
        camera->SetParallelScale(std::isfinite(current)?std::clamp(current,fit*0.001,fit*100):fit);
        double position[3],focus[3];camera->GetPosition(position);camera->GetFocalPoint(focus);
        for(int axis=0;axis<3;++axis)if(!std::isfinite(position[axis])||!std::isfinite(focus[axis])){resetView();return;}
        for(int axis=0;axis<3;++axis){
            const double next=std::isfinite(focus[axis])?std::clamp(focus[axis],-100.0,100.0):0;
            position[axis]+=next-focus[axis];focus[axis]=next;
        }
        camera->SetPosition(position);camera->SetFocalPoint(focus);camera->OrthogonalizeViewUp();
        renderer->ResetCameraClippingRange();
    }

    void translateCamera(const std::array<double,3>& delta) {
        auto* camera=renderer->GetActiveCamera();
        double position[3],focus[3];camera->GetPosition(position);camera->GetFocalPoint(focus);
        for(int axis=0;axis<3;++axis){position[axis]+=delta[static_cast<std::size_t>(axis)];focus[axis]+=delta[static_cast<std::size_t>(axis)];}
        camera->SetPosition(position);camera->SetFocalPoint(focus);
    }

    void zoom(double factor,std::optional<QPointF> anchor=std::nullopt) {
        const auto before=anchor?worldAt(*anchor):std::array<double,3>{};
        renderer->GetActiveCamera()->SetParallelScale(renderer->GetActiveCamera()->GetParallelScale()/std::clamp(factor,1e-12,1e12));
        constrainCamera();
        if(anchor){const auto after=worldAt(*anchor);std::array<double,3> delta{};
            for(std::size_t axis=0;axis<3;++axis)delta[axis]=before[axis]-after[axis];
            translateCamera(delta);constrainCamera();}
        render();if(viewChanged)viewChanged();
    }

    void refreshDataTip() {
        if(clearTipsAction)clearTipsAction->setEnabled(!dataTips.empty());
        if(!annotationRenderer)return;
        for(std::size_t index=0;index<dataTips.size();++index) {
            auto& tip=dataTips[index];const bool selected=selectedTip&&*selectedTip==index;
            if(!tip.text) {
                tip.text=vtkSmartPointer<vtkTextActor>::New();tip.marker=vtkSmartPointer<vtkTextActor>::New();
                tip.text->GetTextProperty()->SetColor(0.1,0.1,0.1);
                tip.text->GetTextProperty()->SetBackgroundOpacity(0.95);tip.text->GetTextProperty()->SetFrame(true);
                tip.marker->SetInput("+");tip.marker->GetTextProperty()->SetBold(true);
                tip.marker->GetTextProperty()->SetJustificationToCentered();tip.marker->GetTextProperty()->SetVerticalJustificationToCentered();
                annotationRenderer->AddViewProp(tip.text);annotationRenderer->AddViewProp(tip.marker);
            }
            tip.text->GetTextProperty()->SetBackgroundColor(selected?0.89:1,selected?0.95:1,selected?1:0.94);
            tip.text->GetTextProperty()->SetFrameColor(selected?0.1:0.45,selected?0.4:0.45,selected?0.85:0.40);
            tip.marker->GetTextProperty()->SetColor(selected?0.1:0.85,selected?0.4:0.15,selected?0.85:0.1);
            const auto& sample=tip.point;
            QString label=sample.label.left(160);label.replace(QLatin1Char('\n'),QLatin1Char(' '));label.replace(QLatin1Char('\r'),QLatin1Char(' '));
            QString text=QStringLiteral("%1 — sample %2\nx: %3\ny: %4").arg(label).arg(sample.index)
                .arg(sample.coordinates[0],0,'g',17).arg(sample.coordinates[1],0,'g',17);
            if(sample.dimensions==3)text+=QStringLiteral("\nz: %1").arg(sample.coordinates[2],0,'g',17);
            if(sample.scalarValue)text+=QStringLiteral("\nValue: %1").arg(*sample.scalarValue,0,'g',17);
            tip.text->SetInput(text.toUtf8().constData());
        }
    }
    void selectDataTip(std::size_t index) {
        if(index>=dataTips.size())return;
        selectedTip=index;selectedPoint=dataTips[index].point;refreshDataTip();
        if(annotationRenderer) {
            auto& tip=dataTips[index];
            annotationRenderer->RemoveViewProp(tip.text);annotationRenderer->RemoveViewProp(tip.marker);
            annotationRenderer->AddViewProp(tip.text);annotationRenderer->AddViewProp(tip.marker);
        }
    }
    std::optional<std::size_t> dataTipAt(QPointF logicalPosition) const {
        const auto hit=[&](std::size_t index) {
            const auto& tip=dataTips[index];if(!tip.text||!tip.text->GetVisibility())return false;
            const auto delta=tip.markerPosition-logicalPosition;
            return tip.hitRectangle.adjusted(-3,-3,3,3).contains(logicalPosition)||delta.x()*delta.x()+delta.y()*delta.y()<=64;
        };
        if(selectedTip&&hit(*selectedTip))return selectedTip;
        for(std::size_t index=dataTips.size();index>0;--index)if(hit(index-1))return index-1;
        return {};
    }
    bool removeSelectedTip() {
        if(!selectedTip||*selectedTip>=dataTips.size())return false;
        const auto index=*selectedTip;
        if(annotationRenderer){annotationRenderer->RemoveViewProp(dataTips[index].text);annotationRenderer->RemoveViewProp(dataTips[index].marker);}
        dataTips.erase(dataTips.begin()+static_cast<std::ptrdiff_t>(index));selectedTip.reset();selectedPoint.reset();
        refreshDataTip();render();return true;
    }
    void clearAllTips() {
        if(annotationRenderer)for(const auto& tip:dataTips){annotationRenderer->RemoveViewProp(tip.text);annotationRenderer->RemoveViewProp(tip.marker);}
        dataTips.clear();selectedTip.reset();selectedPoint.reset();refreshDataTip();render();
    }

    std::optional<PlotDataPoint> pick(QPointF logicalPosition,double tolerance) {
        if(!canvas||!canvas->isValid()||!renderer||state.dimensions==0||!std::isfinite(logicalPosition.x())
            ||!std::isfinite(logicalPosition.y())||!std::isfinite(tolerance)||tolerance<=0)return std::nullopt;
        const int* size=window->GetSize();if(size[0]<=0||size[1]<=0)return std::nullopt;
        const auto target=displayPosition(logicalPosition);
        const double xRatio=static_cast<double>(size[0])/std::max(1,canvas->width());
        const double yRatio=static_cast<double>(size[1])/std::max(1,canvas->height());
        const double* viewport=renderer->GetViewport();
        if(target.x()<viewport[0]*size[0]||target.x()>viewport[2]*size[0]
            ||target.y()<viewport[1]*size[1]||target.y()>viewport[3]*size[1])return std::nullopt;
        if(static_cast<qint64>(size[0])*size[1]>64000000)return std::nullopt;
        for(auto& tip:dataTips){if(tip.text)tip.text->SetVisibility(false);if(tip.marker)tip.marker->SetVisibility(false);}
        // CPU geometry picking is independent of the desktop compositor. Depth
        // occlusion is sampled only in the small cursor neighbourhood.
        const auto depthAt=cursorDepths(target,tolerance*std::max(xRatio,yRatio)+2);
        std::optional<PlotDataPoint> result;
        double nearest=tolerance*tolerance,nearestDepth=1;
        auto consider=[&](const std::array<double,3>& raw,std::size_t series,std::size_t index,const QString& label){
            const auto world=worldPoint(raw);renderer->SetWorldPoint(world[0],world[1],world[2],1);renderer->WorldToDisplay();
            const double* display=renderer->GetDisplayPoint();
            if(display[2]<0||display[2]>1||display[0]<viewport[0]*size[0]||display[0]>viewport[2]*size[0]
                ||display[1]<viewport[1]*size[1]||display[1]>viewport[3]*size[1])return;
            const double dx=(display[0]-target.x())/xRatio,dy=(display[1]-target.y())/yRatio;
            const double distance=dx*dx+dy*dy;
            if(distance>nearest||(distance==nearest&&display[2]>=nearestDepth))return;
            const int x=std::clamp(static_cast<int>(std::lround(display[0])),0,size[0]-1);
            const int y=std::clamp(static_cast<int>(std::lround(display[1])),0,size[1]-1);
            if(display[2]>depthAt(x,y)+0.0001)return;
            nearest=distance;nearestDepth=display[2];result=PlotDataPoint{series,index,label,raw,state.dimensions,std::nullopt,std::nullopt};
        };
        for(std::size_t series=0;series<state.curves.size();++series){const auto& curve=state.curves[series];
            const QString label=curve.label.empty()?QStringLiteral("Curve %1").arg(series+1):QString::fromStdString(curve.label);
            for(std::size_t index=0;index<curve.points.size();++index)consider(curve.points[index],series+1,index+1,label);}
        for(std::size_t index=0;index<state.vectors.size();++index){std::array<double,3> raw{};
            for(int axis=0;axis<state.dimensions;++axis)raw[static_cast<std::size_t>(axis)]=state.vectors[index].coefficient(static_cast<std::size_t>(state.axes[static_cast<std::size_t>(axis)]));
            const auto label=vectorLabel(state.vectors[index],index);consider(raw,index+1,1,label);
            if(result&&result->label==label&&result->index==1&&result->coordinates==raw){const auto full=state.vectors[index].allCoefficients();result->originalCoefficients.assign(full.begin(),full.end());result->basisLabels=state.basisLabels;}}
        if(state.mesh){const auto& mesh=*state.mesh;const auto series=state.curves.size()+state.vectors.size()+1;for(std::size_t index=0;index<mesh.points.size();++index)consider(mesh.points[index],series,index+1,QString::fromStdString(mesh.label));if(result&&result->seriesIndex==series&&!mesh.scalars.empty())result->scalarValue=mesh.scalars[result->index-1];}
        if(state.graph)result=graphPick(logicalPosition,tolerance);
        if(result) {
            const auto existing=std::find_if(dataTips.begin(),dataTips.end(),[&](const auto& tip){
                return tip.point.seriesIndex==result->seriesIndex&&tip.point.index==result->index;});
            if(existing!=dataTips.end())selectDataTip(static_cast<std::size_t>(existing-dataTips.begin()));
            else if(dataTips.size()<128) {
                dataTips.push_back(DataTip{*result,{},{},{},{}});selectDataTip(dataTips.size()-1);
            }else{
                QToolTip::showText(canvas->mapToGlobal(logicalPosition.toPoint()),QStringLiteral("This subplot already has 128 data tips. Remove a tip or use Clear data tips before adding another."),canvas);
                result.reset();
            }
        }
        render();return result;
    }

    bool handleInteraction(QEvent* event) {
        if(!renderer||state.dimensions==0)return false;
        if(event->type()==QEvent::KeyPress) {
            const auto key=static_cast<QKeyEvent*>(event)->key();
            if(key==Qt::Key_Escape&&state.graph&&(!state.graph->selectedNodes.empty()||!state.graph->selectedEdges.empty())){graphSelect({},false);return true;}
            if(key==Qt::Key_Delete||key==Qt::Key_Backspace||key==Qt::Key_Escape) {
                if(removeSelectedTip())return true;
                if(key==Qt::Key_Escape){setTool(PlotTool::Navigate);return true;}
            }
        }
        if(event->type()==QEvent::ContextMenu) {
            const auto* context=static_cast<QContextMenuEvent*>(event);
            if(const auto selected=dataTipAt(context->pos()))selectDataTip(*selected);
            auto* menu=new QMenu(canvas);menu->setObjectName(QStringLiteral("plotDataTipContextMenu"));menu->setAttribute(Qt::WA_DeleteOnClose);
            auto* remove=menu->addAction(QStringLiteral("Remove selected data tip"));remove->setObjectName(QStringLiteral("removeSelectedDataTip"));remove->setEnabled(selectedTip.has_value());
            auto* clear=menu->addAction(QStringLiteral("Clear all data tips"));clear->setObjectName(QStringLiteral("clearAllDataTips"));clear->setEnabled(!dataTips.empty());
            QObject::connect(remove,&QAction::triggered,canvas,[this]{removeSelectedTip();});
            QObject::connect(clear,&QAction::triggered,canvas,[this]{clearAllTips();});
            if(state.graph){auto* selection=menu->addAction(QStringLiteral("Clear graph selection"));QObject::connect(selection,&QAction::triggered,canvas,[this]{graphSelect({},false);});}
            render();menu->popup(context->globalPos());return true;
        }
        if(event->type()==QEvent::Wheel){
            auto* wheel=static_cast<QWheelEvent*>(event);
            zoom(std::pow(1.18,std::clamp(wheel->angleDelta().y()/120.0,-10.0,10.0)),wheel->position());return true;
        }
        if(event->type()==QEvent::MouseButtonDblClick){resetView();return true;}
        if(event->type()==QEvent::MouseButtonPress){
            const auto* mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()==Qt::LeftButton)if(const auto selected=dataTipAt(mouse->position())) {
                selectDataTip(*selected);render();return true;
            }
            if(mouse->button()==Qt::LeftButton&&(tool==PlotTool::ZoomIn||tool==PlotTool::ZoomOut)){
                zoom(tool==PlotTool::ZoomIn?1.35:1.0/1.35,mouse->position());return true;}
            if(mouse->button()==Qt::LeftButton&&tool==PlotTool::DataCursor){pick(mouse->position(),12);return true;}
            if(mouse->button()==Qt::LeftButton&&state.graph&&state.graph->interactive&&(tool==PlotTool::GraphSelect||tool==PlotTool::GraphDrag)){
                const auto point=graphPick(mouse->position(),12);graphSelect(point,mouse->modifiers().testFlag(Qt::ControlModifier));
                if(tool==PlotTool::GraphDrag&&point&&point->seriesIndex==1){draggedGraphNode=static_cast<std::uint32_t>(point->index);graphDragStart=state.graph->positions[point->index-1];graphDragWorld=worldAt(mouse->position());dragging=true;dragTool=PlotTool::GraphDrag;}
                return true;
            }
            if(mouse->button()==Qt::LeftButton||mouse->button()==Qt::MiddleButton){
                dragging=true;lastDragPosition=mouse->position();
                dragTool=mouse->button()==Qt::MiddleButton?PlotTool::Pan:
                    ((tool==PlotTool::Rotate||(tool==PlotTool::Navigate&&state.dimensions==3))?PlotTool::Rotate:PlotTool::Pan);
            }
            return true;
        }
        if(event->type()==QEvent::MouseMove&&dragging){
            const auto current=static_cast<QMouseEvent*>(event)->position();
            if(dragTool==PlotTool::GraphDrag&&draggedGraphNode&&state.graph){
                const auto world=worldAt(current);auto position=graphDragStart;
                for(int d=0;d<state.dimensions;++d)position[static_cast<std::size_t>(d)]+=(world[static_cast<std::size_t>(d)]-graphDragWorld[static_cast<std::size_t>(d)])*renderUnits[static_cast<std::size_t>(d)];
                auto graph=*state.graph;graph.positions[*draggedGraphNode-1]=position;graph.layout="manual";
                try{plotting::validateGraphView(graph);state.graph=std::move(graph);rebuild(false,true);if(stateChanged)stateChanged();}catch(const plotting::ExportError&){}
                return true;
            }
            if(dragTool==PlotTool::Pan){const auto before=worldAt(lastDragPosition),after=worldAt(current);std::array<double,3> delta{};
                for(std::size_t axis=0;axis<3;++axis)delta[axis]=before[axis]-after[axis];
                translateCamera(delta);
            }else{auto* camera=renderer->GetActiveCamera();camera->Azimuth((lastDragPosition.x()-current.x())*0.6);
                camera->Elevation((current.y()-lastDragPosition.y())*0.6);}
            lastDragPosition=current;constrainCamera();render();if(viewChanged)viewChanged();return true;
        }
        if(event->type()==QEvent::MouseButtonRelease){dragging=false;draggedGraphNode.reset();return true;}
        if(event->type()==QEvent::FocusOut)dragging=false;
        return false;
    }

    void positionOverlays() {
        if(!window||!renderer)return;
        const int* dimensions=window->GetSize();
        const double width=dimensions[0],height=dimensions[1];
        // An initially hidden dock can expose a one-pixel target before its
        // layout settles. FreeType cannot build a caption texture there. Draw
        // annotations only once the target can hold a text line; the next
        // normal render restores them at the final size.
        if(width<32||height<32){annotationRenderer->DrawOff();return;}
        annotationRenderer->DrawOn();
        const double ratio=canvas?canvas->devicePixelRatioF():1.0;
        const bool genericXY=state.dimensions==2&&state.axes[0]<0;
        const double tickFontSize=std::clamp(height/28.0,6.0*ratio,state.style.fontSize*ratio);
        // Keep a stable data-view aspect while leaving full-frame margins for
        // mathematical titles/captions. Reparenting into a narrow tile then
        // changes only this viewport, never the user's camera or vector data.
        const double availableWidth=width*0.84,availableHeight=height*0.76;
        const double dataAspect=genericXY&&availableHeight>0?availableWidth/availableHeight:1.0;
        const double dataWidth=std::min(availableWidth,availableHeight*dataAspect);
        const double dataHeight=dataWidth/dataAspect;
        if(genericXY&&width>0&&height>0){
            qsizetype widest=1;
            for(const auto& label:tickLabels)if(label.axis==1)
                widest=std::max(widest,QString::fromUtf8(label.actor->GetInput()).size());
            // Reserve outward label space rather than drawing axes/ticks
            // through a time series. Generic x/y units remain independent.
            const double left=std::min(width*0.42,std::max(50*ratio,widest*tickFontSize*0.62+44*ratio));
            const double bottom=std::min(height*0.30,65*ratio);
            const double right=std::min(width*0.08,18*ratio),top=std::min(height*0.22,34*ratio);
            renderer->SetViewport(left/width,bottom/height,(width-right)/width,(height-top)/height);
        }else if(width>0&&height>0){
            const double x=(width-dataWidth)/2.0,y=height*0.51-dataHeight/2.0;
            renderer->SetViewport(x/width,y/height,(x+dataWidth)/width,(y+dataHeight)/height);
        }
        for(auto& label:graphText){const auto world=worldPoint(label.point);renderer->SetWorldPoint(world[0],world[1],world[2],1);renderer->WorldToDisplay();const auto* at=renderer->GetDisplayPoint();label.actor->SetDisplayPosition(static_cast<int>(at[0]+6*ratio),static_cast<int>(at[1]+6*ratio));label.actor->SetVisibility(at[2]>=0&&at[2]<=1);label.actor->GetTextProperty()->SetFontSize(static_cast<int>(tickFontSize));}
        for(auto& label:tickLabels){
            renderer->SetWorldPoint(label.point[0],label.point[1],label.point[2],1);
            renderer->WorldToDisplay();
            const double* projected=renderer->GetDisplayPoint();
            const double x=projected[0]+(genericXY?(label.axis==0?0:-7):(label.axis==0?-10:6))*ratio;
            const double y=projected[1]+(genericXY?(label.axis==0?-7:0):(label.axis==0?-18:-6))*ratio;
            label.actor->SetDisplayPosition(static_cast<int>(x),static_cast<int>(y));
            label.actor->GetTextProperty()->SetFontSize(static_cast<int>(tickFontSize));
            label.actor->SetVisibility(x>=0&&y>=0&&x<width-18&&y<height-14);
        }
        for(auto& overlay:overlays){
            const double heightFraction=overlay.kind==2&&state.dimensions==2?0.48:0.13;
            const double scale=std::min({1.0,std::max(1.0,width-12*ratio)/overlay.size.width(),
                std::max(1.0,height*heightFraction)/overlay.size.height()});
            overlay.displayed=overlay.size*scale;
            overlay.plane->SetPoint1(overlay.displayed.width(),0,0);
            overlay.plane->SetPoint2(0,overlay.displayed.height(),0);
        }
        int captionHeight=0;
        for(const auto& overlay:overlays)if(overlay.kind==4)captionHeight=overlay.displayed.height();
        if(contextCaption){
            contextCaption->SetDisplayPosition(static_cast<int>(8*ratio),static_cast<int>(6*ratio+captionHeight));
            contextCaption->GetTextProperty()->SetFontSize(static_cast<int>(std::clamp(height/28.0,8.0*ratio,13.0*ratio)));
            const QString text=width<500*ratio&&state.basisLabels.empty()?QStringLiteral("(%1) | gamma=%2").arg(coordinateNames(state)).arg(state.gamma,0,'g',6)
                :contextText(state);
            contextCaption->SetInput(text.toUtf8().constData());
        }
        for(auto& overlay:overlays){
            double x=(width-overlay.displayed.width())/2.0,y=0;
            if(overlay.kind==0)y=height-overlay.displayed.height()-6*ratio;
            else if(overlay.kind==4)y=4*ratio;
            else if(state.dimensions==2){
                if(genericXY){
                    renderer->SetWorldPoint(-1.15,-1.15,0,1);renderer->WorldToDisplay();
                    const double* corner=renderer->GetDisplayPoint();
                    if(overlay.kind==1){
                        const double* viewport=renderer->GetViewport();
                        x=(viewport[0]+viewport[2])*width*0.5-overlay.displayed.width()*0.5;
                        y=corner[1]-tickFontSize-13*ratio-overlay.displayed.height();
                    }else{x=5*ratio;y=(height-overlay.displayed.height())/2.0;}
                }else if(overlay.kind==1)y=30*ratio+captionHeight;
                else{x=6*ratio;y=(height-overlay.displayed.height())/2.0;}
            }else{
                double anchor[]{0,0,0};anchor[overlay.kind-1]=1.32;
                renderer->SetWorldPoint(anchor[0],anchor[1],anchor[2],1);
                renderer->WorldToDisplay();
                const double* projected=renderer->GetDisplayPoint();
                x=projected[0]-overlay.displayed.width()/2.0;
                y=projected[1]-overlay.displayed.height()/2.0;
            }
            x=std::clamp(x,0.0,std::max(0.0,width-overlay.displayed.width()));
            y=std::clamp(y,0.0,std::max(0.0,height-overlay.displayed.height()));
            overlay.actor->SetPosition(x,y);
        }
        std::vector<QRectF> occupiedTips;
        for(auto& tip:dataTips) {
            if(!tip.text||!tip.marker)continue;
            const auto point=worldPoint(tip.point.coordinates);
            renderer->SetWorldPoint(point[0],point[1],point[2],1);renderer->WorldToDisplay();
            const double* display=renderer->GetDisplayPoint();const double* viewport=renderer->GetViewport();
            const bool visible=!picking&&display[2]>=0&&display[2]<=1&&display[0]>=viewport[0]*width&&display[0]<=viewport[2]*width
                &&display[1]>=viewport[1]*height&&display[1]<=viewport[3]*height;
            tip.text->SetVisibility(visible);tip.marker->SetVisibility(visible);
            tip.hitRectangle={};if(!visible)continue;
            tip.marker->SetDisplayPosition(static_cast<int>(display[0]),static_cast<int>(display[1]));
            tip.marker->GetTextProperty()->SetFontSize(static_cast<int>(20*ratio));
            tip.text->GetTextProperty()->SetFontSize(static_cast<int>(11*ratio));
            double textSize[2]{};tip.text->GetSize(annotationRenderer,textSize);
            const double tipWidth=std::min(width,textSize[0]+8*ratio),tipHeight=std::min(height,textSize[1]+8*ratio);
            QRectF box;
            for(int attempt=0;attempt<16;++attempt) {
                const double x=display[0]+(attempt%2==0?12*ratio:-tipWidth-12*ratio);
                const double y=display[1]+12*ratio+(attempt/2)*(tipHeight+6*ratio)*(attempt/2%2==0?1:-1);
                box=QRectF(std::clamp(x,0.0,std::max(0.0,width-tipWidth)),std::clamp(y,0.0,std::max(0.0,height-tipHeight)),tipWidth,tipHeight);
                if(std::none_of(occupiedTips.begin(),occupiedTips.end(),[&](const auto& other){return box.adjusted(-3*ratio,-3*ratio,3*ratio,3*ratio).intersects(other);}))break;
            }
            occupiedTips.push_back(box);
            tip.text->SetDisplayPosition(static_cast<int>(box.x()),static_cast<int>(box.y()));
            const double xRatio=width/std::max(1,canvas->width()),yRatio=height/std::max(1,canvas->height());
            tip.hitRectangle=QRectF(box.x()/xRatio,(height-box.y()-box.height())/yRatio,box.width()/xRatio,box.height()/yRatio);
            tip.markerPosition=QPointF(display[0]/xRatio,(height-1-display[1])/yRatio);
        }
    }

    void addLabels() {
        if(!renderer)return;
        for(std::size_t index=0;index<labelImages.size();++index){
            if(index==3&&state.dimensions!=3)continue;
            QImage image=labelImages[index];
            if(image.isNull()&&index>=1&&index<=3&&state.dimensions>=2){
                QFont font=publicationFont(state.style,1);
                const int axis=state.axes[index-1];
                image=renderMathText(axis<0?QString::fromLatin1(index==1?"x":index==2?"y":"z")
                    :basisName(state,axis),font,Qt::black,false);
            }
            if(image.isNull())continue;
            if(index==2&&state.dimensions==2)image=image.transformed(QTransform().rotate(-90));
            const double ratio=canvas?canvas->devicePixelRatioF():1.0;
            if(ratio!=1.0)image=image.scaled(image.size()*ratio,Qt::KeepAspectRatio,Qt::SmoothTransformation);
            rasterLabelImages[index]=image;
            vtkSmartPointer<vtkPlaneSource> plane;
            auto actor=imageOverlay(image,plane);
            overlays.push_back({static_cast<int>(index),image.size(),image.size(),actor,plane});
            annotationRenderer->AddViewProp(actor);
        }
        positionOverlays();
    }

    void render() {
        if (canvas) canvas->requestScene();
    }

    #include "raster_scene.inc.hpp"

    bool paintVector(QPainter& painter,std::size_t& primitiveCount,QString* error) {
        if(!renderer||!window||(!detachedPaintScene&&(!canvas||!canvas->isValid())))return fail(error,QStringLiteral("The plot scene is unavailable."));
        const int* size=window->GetSize();const double height=size[1];
        if(size[0]<=0||size[1]<=0)return fail(error,QStringLiteral("The SVG canvas has no usable size."));
        using Primitive=RasterPrimitive;
        std::vector<Primitive> geometry;
        auto append=[&](QPolygonF points,const QColor& color,double width,double depth,int kind,std::vector<double> vertexDepths={}){
            if(paintCancelled&&paintCancelled->load())return false;
            if(points.isEmpty())return true;
            if(++primitiveCount>2000000)return false;
            geometry.push_back({std::move(points),color,width,depth,kind,std::move(vertexDepths)});return true;
        };
        std::size_t visitedCells=0;
        const auto cancelled=[&]{return paintCancelled&&paintCancelled->load();};
        auto* actors=renderer->GetActors();actors->InitTraversal();
        while(auto* actor=actors->GetNextActor()){
            if(paintCancelled&&paintCancelled->load())return false;
            if(!actor->GetVisibility())continue;
            auto* mapper=vtkPolyDataMapper::SafeDownCast(actor->GetMapper());if(!mapper)continue;
            mapper->Update();auto* data=mapper->GetInput();if(!data)continue;
            const double* rgb=actor->GetProperty()->GetColor();
            const QColor color=QColor::fromRgbF(rgb[0],rgb[1],rgb[2],actor->GetProperty()->GetOpacity());
            auto* scalarArray=data->GetPointData()->GetScalars();
            auto* lookup=mapper->GetLookupTable();
            const bool scalarColor=mapper->GetScalarVisibility()&&scalarArray&&lookup;
            auto mappedColor=[&](const std::vector<vtkIdType>& ids){
                const bool plainMesh=state.mesh&&state.mesh->scalars.empty();
                if(ids.empty()||(!scalarColor&&!plainMesh))return color;
                double rgb[]{color.redF(),color.greenF(),color.blueF()};
                if(scalarColor){double value=0;for(const auto id:ids)value+=scalarArray->GetComponent(id,0)/ids.size();lookup->GetColor(value,rgb);}
                if(actor->GetProperty()->GetLighting()&&ids.size()>=3){auto* normals=data->GetPointData()->GetNormals();if(normals){double normal[3]{},center[3]{};for(const auto id:ids){double n[3],p[3];normals->GetTuple(id,n);data->GetPoint(id,p);for(int d=0;d<3;++d){normal[d]+=n[d];center[d]+=p[d]/ids.size();}}double camera[3];renderer->GetActiveCamera()->GetPosition(camera);double dot=0,norm=0,distance=0;for(int d=0;d<3;++d){const double v=camera[d]-center[d];dot+=normal[d]*v;norm+=normal[d]*normal[d];distance+=v*v;}const double brightness=norm>0&&distance>0?.25+.75*std::abs(dot)/std::sqrt(norm*distance):1;for(auto& c:rgb)c*=std::clamp(brightness,0.0,1.0);}}
                return QColor::fromRgbF(rgb[0],rgb[1],rgb[2],actor->GetProperty()->GetOpacity());
            };
            auto project=[&](vtkIdType index){
                double point[3],input[4],world[4];data->GetPoint(index,point);
                input[0]=point[0];input[1]=point[1];input[2]=point[2];input[3]=1;
                actor->GetMatrix()->MultiplyPoint(input,world);
                renderer->SetWorldPoint(world);renderer->WorldToDisplay();const double* display=renderer->GetDisplayPoint();
                return std::array<double,3>{{display[0],height-display[1],display[2]}};
            };
            auto line=[&](std::array<double,3> first,std::array<double,3> last,QPolygonF& path,std::vector<double>& depths){
                if((first[2]<0&&last[2]<0)||(first[2]>1&&last[2]>1))return false;
                for(double plane:{0.0,1.0}){
                    const bool outsideFirst=plane==0?first[2]<plane:first[2]>plane;
                    const bool outsideLast=plane==0?last[2]<plane:last[2]>plane;
                    if(outsideFirst!=outsideLast){const double t=(plane-first[2])/(last[2]-first[2]);
                        std::array<double,3> clipped{};for(std::size_t axis=0;axis<3;++axis)clipped[axis]=first[axis]+t*(last[axis]-first[axis]);
                        if(outsideFirst)first=clipped;else last=clipped;
                    }
                }
                path.append(QPointF(first[0],first[1]));path.append(QPointF(last[0],last[1]));depths={first[2],last[2]};return true;
            };
            vtkIdType count=0;const vtkIdType* ids=nullptr;
            auto* lines=data->GetLines();lines->InitTraversal();
            while(lines->GetNextCell(count,ids)){
                const QColor lineColor=mappedColor(std::vector<vtkIdType>(ids,ids+count));
                QPolygonF path;double depth=0;
                for(vtkIdType index=1;index<count;++index){const auto first=project(ids[index-1]),last=project(ids[index]);
                    if((index&1023)==0&&paintCancelled&&paintCancelled->load())return false;
                    QPolygonF segment;std::vector<double> depths;if(!line(first,last,segment,depths)){if(!path.isEmpty()){
                        if(!append(path,lineColor,actor->GetProperty()->GetLineWidth(),depth/std::max<qsizetype>(1,path.size()),0))return fail(error,QStringLiteral("SVG geometry exceeds two million primitives."));
                        path.clear();depth=0;}continue;}
                    if(state.dimensions==3){const double depth=(depths[0]+depths[1])*0.5;if(!append(segment,lineColor,actor->GetProperty()->GetLineWidth(),depth,0,std::move(depths)))return fail(error,QStringLiteral("Plot geometry exceeds two million primitives."));}
                    else{if(path.isEmpty()){path.append(segment[0]);depth+=first[2];}path.append(segment[1]);depth+=last[2];}
                }
                if(!path.isEmpty()&&!append(path,lineColor,actor->GetProperty()->GetLineWidth(),depth/std::max<qsizetype>(1,path.size()),0))
                    return fail(error,QStringLiteral("SVG geometry exceeds two million primitives."));
            }
            auto polygon=[&](const std::vector<vtkIdType>& vertices){
                if((++visitedCells&255)==0&&cancelled())return false;
                std::vector<std::array<double,3>> points;for(auto index:vertices)points.push_back(project(index));
                for(double plane:{0.0,1.0}){
                    std::vector<std::array<double,3>> clipped;
                    for(std::size_t index=0;index<points.size();++index){const auto& a=points[index];const auto& b=points[(index+1)%points.size()];
                        const bool inA=plane==0?a[2]>=plane:a[2]<=plane,inB=plane==0?b[2]>=plane:b[2]<=plane;
                        if(inA)clipped.push_back(a);
                        if(inA!=inB){const double t=(plane-a[2])/(b[2]-a[2]);std::array<double,3> p{};
                            for(std::size_t axis=0;axis<3;++axis)p[axis]=a[axis]+t*(b[axis]-a[axis]);
                            clipped.push_back(p);}
                    }
                    points=std::move(clipped);
                }
                if(detachedPaintScene){
                    // The snapshot already contains world-space triangles.
                    // Near/far clipping retains convexity; triangulate the
                    // clipped face and drop edge-on faces with zero coverage.
                    const auto color=mappedColor(vertices);
                    for(std::size_t index=2;index<points.size();++index){
                        const auto& a=points[0];const auto& b=points[index-1];const auto& c=points[index];
                        if((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])==0)continue;
                        if(!append(QPolygonF{{a[0],a[1]},{b[0],b[1]},{c[0],c[1]}},color,0,
                            (a[2]+b[2]+c[2])/3,1,{a[2],b[2],c[2]}))return false;
                    }
                    return true;
                }
                QPolygonF shape;std::vector<double> depths;double depth=0;for(const auto& point:points){shape.append({point[0],point[1]});depths.push_back(point[2]);depth+=point[2];}
                return append(shape,mappedColor(vertices),0,depth/std::max<std::size_t>(1,points.size()),1,std::move(depths));
            };
            auto* polygons=data->GetPolys();polygons->InitTraversal();
            while(polygons->GetNextCell(count,ids))if(!polygon(std::vector<vtkIdType>(ids,ids+count)))return fail(error,QStringLiteral("SVG geometry exceeds two million primitives."));
            auto* strips=data->GetStrips();strips->InitTraversal();
            while(strips->GetNextCell(count,ids))for(vtkIdType index=2;index<count;++index)
                if(!polygon({ids[index-2],ids[index-1],ids[index]}))return fail(error,QStringLiteral("SVG geometry exceeds two million primitives."));
            auto* vertices=data->GetVerts();vertices->InitTraversal();
            while(vertices->GetNextCell(count,ids))for(vtkIdType index=0;index<count;++index){
                if((++visitedCells&255)==0&&cancelled())return false;
                const auto point=project(ids[index]);
                if(point[2]>=0&&point[2]<=1&&!append(QPolygonF{QPointF(point[0],point[1])},mappedColor({ids[index]}),actor->GetProperty()->GetPointSize(),point[2],2))
                    return fail(error,QStringLiteral("SVG geometry exceeds two million primitives."));}
        }
        painter.save();painter.setRenderHint(QPainter::Antialiasing);painter.fillRect(QRectF(0,0,size[0],size[1]),Qt::white);
        const double* viewport=renderer->GetViewport();
        const QRectF clipRect(viewport[0]*size[0],height*(1-viewport[3]),(viewport[2]-viewport[0])*size[0],height*(viewport[3]-viewport[1]));
        QPainterPath clip;clip.addRect(clipRect);
        painter.save();painter.setClipRect(clipRect);
        if(paintCancelled&&paintCancelled->load()){painter.restore();painter.restore();return false;}
        if(detachedPaintScene){
            QString diagnostic;
            const auto image=rasterizePlotGeometry(QSize(size[0],size[1]),geometry,clipRect,paintCancelled,&diagnostic);
            if(image.isNull()){painter.restore();painter.restore();return fail(error,diagnostic);}
            painter.drawImage(QPointF(0,0),image);
        }else{
        std::stable_sort(geometry.begin(),geometry.end(),[](const Primitive& a,const Primitive& b){return a.depth>b.depth;});
        for(const auto& item:geometry){
            if(paintCancelled&&paintCancelled->load()){painter.restore();painter.restore();return false;}
            if(item.kind==0){
                QPainterPath path;path.moveTo(item.points.front());for(qsizetype index=1;index<item.points.size();++index)path.lineTo(item.points[index]);
                if(clipRect.contains(path.controlPointRect().adjusted(-item.width,-item.width,item.width,item.width))){
                    painter.setPen(QPen(item.color,item.width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));painter.setBrush(Qt::NoBrush);painter.drawPath(path);
                }else{
                    QPainterPathStroker stroke;stroke.setWidth(item.width);stroke.setCapStyle(Qt::RoundCap);stroke.setJoinStyle(Qt::RoundJoin);
                    const auto visible=stroke.createStroke(path).intersected(clip);
                    if(!visible.isEmpty()){painter.setPen(Qt::NoPen);painter.setBrush(item.color);painter.drawPath(visible);}
                }
            }else{
                QPainterPath path;if(item.kind==1)path.addPolygon(item.points);else path.addEllipse(item.points.front(),item.width*0.5,item.width*0.5);
                const auto visible=clipRect.contains(path.controlPointRect())?path:path.intersected(clip);
                if(!visible.isEmpty()){painter.setPen(Qt::NoPen);painter.setBrush(item.color);painter.drawPath(visible);}
            }
        }
        }
        painter.restore();
        auto textActor=[&](vtkTextActor* actor){
            if(!actor||!actor->GetVisibility())return;
            auto* property=actor->GetTextProperty();QFont font(QString::fromUtf8(property->GetFontFamilyAsString()));
            font.setPixelSize(std::max(1,property->GetFontSize()));font.setBold(property->GetBold());font.setItalic(property->GetItalic());
            painter.setFont(font);const QFontMetricsF metrics(font);const QString text=QString::fromUtf8(actor->GetInput());
            const auto lines=text.split(QLatin1Char('\n'));double width=0;
            for(const auto& value:lines)width=std::max(width,metrics.horizontalAdvance(value));
            const double textHeight=metrics.lineSpacing()*lines.size();const double* position=actor->GetPosition();
            double x=position[0],y=height-position[1];
            if(property->GetJustification()==1)x-=width*0.5;else if(property->GetJustification()==2)x-=width;
            if(property->GetVerticalJustification()==0)y-=textHeight;else if(property->GetVerticalJustification()==1)y-=textHeight*0.5;
            if(property->GetBackgroundOpacity()>0){const double* background=property->GetBackgroundColor();
                painter.fillRect(QRectF(x-2,y-2,width+4,textHeight+4),QColor::fromRgbF(background[0],background[1],background[2],property->GetBackgroundOpacity()));}
            const double* color=property->GetColor();painter.setPen(QColor::fromRgbF(color[0],color[1],color[2],property->GetOpacity()));
            for(qsizetype index=0;index<lines.size();++index)painter.drawText(QPointF(x,y+metrics.ascent()+index*metrics.lineSpacing()),lines[index]);
        };
        for(const auto& tick:tickLabels)textActor(tick.actor);
        for(const auto& label:graphText)textActor(label.actor);
        textActor(contextCaption);
        const std::array<QString,5> texts{{state.title,state.xLabel,state.yLabel,state.zLabel,state.caption}};
        const std::array<bool,5> modes{{state.titleLatex,state.xLabelLatex,state.yLabelLatex,state.zLabelLatex,state.captionLatex}};
        for(const auto& overlay:overlays){const auto kind=static_cast<std::size_t>(overlay.kind);QString text=texts[kind];bool latex=modes[kind];
            if(text.isEmpty()&&kind>=1&&kind<=3){const int axis=state.axes[kind-1];text=axis<0?QString::fromLatin1(kind==1?"x":kind==2?"y":"z"):basisName(state,axis);latex=false;}
            QFont font=publicationFont(state.style,static_cast<int>(kind));
            const double* position=overlay.actor->GetPosition();const QRectF target(position[0],height-position[1]-overlay.displayed.height(),overlay.displayed.width(),overlay.displayed.height());
            if(detachedPaintScene){
                if(!labelImages[kind].isNull())painter.drawImage(target,labelImages[kind]);
                continue;
            }
            if(kind==2&&state.dimensions==2){painter.save();painter.translate(target.center());painter.rotate(-90);
                if(!paintMathText(painter,QRectF(-target.height()*0.5,-target.width()*0.5,target.height(),target.width()),text,font,Qt::black,latex,error)){painter.restore();painter.restore();return false;}painter.restore();
            }else if(!paintMathText(painter,target,text,font,Qt::black,latex,error)){painter.restore();return false;}
        }
        if(legend&&legend->GetVisibility()&&(!state.curves.empty()||!state.vectors.empty())){
            auto* host=state.dimensions==2&&state.axes[0]<0?annotationRenderer.GetPointer():renderer.GetPointer();
            const double* lower=legend->GetPositionCoordinate()->GetComputedDoubleDisplayValue(host);
            const std::array<double,2> first{{lower[0],lower[1]}};
            const double* upper=legend->GetPosition2Coordinate()->GetComputedDoubleDisplayValue(host);
            const QRectF box(first[0],height-upper[1],std::max(1.0,upper[0]-first[0]),std::max(1.0,upper[1]-first[1]));
            const std::size_t count=state.curves.size()+state.vectors.size();const double row=box.height()/std::max<std::size_t>(1,count);
            QFont font=publicationFont(state.style);font.setPixelSize(std::clamp(static_cast<int>(row*0.7),5,static_cast<int>(state.style.fontSize)));painter.setFont(font);
            if(state.style.legendBox){painter.fillRect(box,Qt::white);painter.setPen(QPen(QColor(80,80,80),.5));painter.drawRect(box);}
            for(std::size_t index=0;index<count;++index){const auto rgb=runtime::plotColor(state.style,index);const QColor color=QColor::fromRgbF(rgb[0],rgb[1],rgb[2]);
                const double y=box.top()+(index+0.5)*row;painter.setPen(QPen(color,1));painter.drawLine(QPointF(box.left(),y),QPointF(box.left()+12,y));
                const auto fallback=index<state.vectors.size()?vectorLabel(state.vectors[index],index).toStdString():(state.curves[index-state.vectors.size()].label.empty()?"Curve "+std::to_string(index-state.vectors.size()+1):state.curves[index-state.vectors.size()].label);
                const auto label=QString::fromStdString(runtime::plotLegendLabel(state.style,index,fallback));
                painter.drawText(QPointF(box.left()+16,y+QFontMetricsF(font).ascent()*0.35),label);}
        }
        if(state.mesh&&!state.mesh->scalars.empty()){
            const auto [a,b]=std::minmax_element(state.mesh->scalars.begin(),state.mesh->scalars.end());const bool constant=!state.style.colorLimits&&*a==*b;const double padding=std::max(1.0,std::abs(*a)*.05);const double low=state.style.colorLimits?state.style.colorLimits->lower:constant?(std::isfinite(*a-padding)?*a-padding:*a):*a,high=state.style.colorLimits?state.style.colorLimits->upper:constant?(std::isfinite(*a+padding)?*a+padding:*a):*b;
            vtkNew<vtkLookupTable> lut;lut->SetNumberOfTableValues(256);lut->SetRange(low,high);lut->SetHueRange(.667,constant?.667:0);lut->Build();
            const QRectF bar(size[0]*.88,height*(1-.25-.45),size[0]*.08,height*.45);
            painter.setPen(Qt::NoPen);for(int i=0;i<256;++i){double rgb[3];lut->GetColor(low+(high-low)*(i+.5)/256,rgb);painter.setBrush(QColor::fromRgbF(rgb[0],rgb[1],rgb[2]));painter.drawRect(QRectF(bar.left(),bar.bottom()-(i+1)*bar.height()/256,bar.width(),bar.height()/256+0.1));}
            painter.setPen(QPen(QColor(40,40,40),.6));painter.setBrush(Qt::NoBrush);painter.drawRect(bar);QFont font=detachedPaintScene?capturedDefaultFont:QApplication::font();font.setPixelSize(11);painter.setFont(font);
            for(int i=0;i<(constant?1:5);++i){const double fraction=constant?.5:i/4.0,y=bar.bottom()-fraction*bar.height();painter.drawText(QPointF(bar.right()+3,y+4),QString::number(constant?*a:low+(high-low)*fraction,'g',4));}
        }
        for(const auto& tip:dataTips){textActor(tip.marker);textActor(tip.text);}
        painter.restore();return true;
    }

    void resetView() {
        if (!renderer || !canvas) return;
        auto* camera = renderer->GetActiveCamera();
        camera->SetFocalPoint(0,0,0);
        camera->ParallelProjectionOn();
        if (state.dimensions == 2) {
            camera->SetPosition(0,0,5);
            camera->SetViewUp(0,1,0);
        } else {
            camera->SetPosition(3,-3,2.5);
            camera->SetViewUp(0,0,1);
        }
        camera->SetParallelScale(state.dimensions == 2 ? (state.axes[0]<0?1.25:1.45) : 2.0);
        renderer->ResetCameraClippingRange();
        render();
    }

    void setAnimationPlaying(bool playing){
        if(!state.animation)playing=false;
        state.animationPlaying=playing;
        if(animationPlay){QSignalBlocker block(animationPlay);animationPlay->setChecked(playing);animationPlay->setText(playing?QStringLiteral("Pause"):QStringLiteral("Play"));}
        if(animationTimer){if(playing)animationTimer->start(std::max(1,qRound(1000/state.animation->fps)));else animationTimer->stop();}
        if(stateChanged)stateChanged();
    }
    bool showAnimationFrame(std::size_t index,bool resetCamera=false){
        if(!state.animation||index>=state.animation->selectedFrames.size())return false;
        auto frame=plotting::fieldAnimationFrame(*state.animation,index,state.animationCoefficient);state.animationFrame=index;
        state.dimensions=frame.dimensions;state.axes=frame.axes;state.graph.reset();state.vectors.clear();state.curves=std::move(frame.curves);state.mesh=std::move(frame.mesh);state.style.colorLimits=frame.style.colorLimits;
        if(state.title.isEmpty()||state.title==animationDefaultTitle){state.title=QString::fromStdString(frame.title);animationDefaultTitle=state.title;labelImages[0]=renderMathText(state.title,publicationFont(state.style,0),Qt::black,false);state.titleLatex=false;}
        if(state.xLabel.isEmpty())state.xLabel=QString::fromStdString(frame.xLabel);
        if(state.yLabel.isEmpty())state.yLabel=QString::fromStdString(frame.yLabel);
        if(state.zLabel.isEmpty())state.zLabel=QString::fromStdString(frame.zLabel);
        for(std::size_t k=1;k<4;++k){const auto text=k==1?state.xLabel:k==2?state.yLabel:state.zLabel;labelImages[k]=renderMathText(text,publicationFont(state.style,static_cast<int>(k)),Qt::black,k==1?state.xLabelLatex:k==2?state.yLabelLatex:state.zLabelLatex);}
        clearAllTips();if(animationSlider){QSignalBlocker block(animationSlider);animationSlider->setValue(static_cast<int>(index));}
        if(animationTime)animationTime->setText(QStringLiteral("%1/%2  t=%3").arg(index+1).arg(state.animation->selectedFrames.size()).arg(state.animation->t[state.animation->selectedFrames[index]],0,'g',7));
        rebuild(resetCamera);if(stateChanged)stateChanged();return true;
    }
    void animationControls(){
        if(animationTimer)return;
        animationTimer=new QTimer(page);animationTimer->setObjectName(QStringLiteral("fieldAnimationTimer"));
        animationPrevious=toolbar->addAction(QStringLiteral("Previous"));animationPrevious->setObjectName(QStringLiteral("previousAnimationFrame"));
        animationPlay=toolbar->addAction(QStringLiteral("Play"));animationPlay->setObjectName(QStringLiteral("playFieldAnimation"));animationPlay->setCheckable(true);
        animationNext=toolbar->addAction(QStringLiteral("Next"));animationNext->setObjectName(QStringLiteral("nextAnimationFrame"));
        animationSlider=new QSlider(Qt::Horizontal,toolbar);animationSlider->setObjectName(QStringLiteral("fieldAnimationFrameSlider"));animationSlider->setMaximumWidth(180);animationSlider->setMinimumWidth(60);toolbar->addWidget(animationSlider);
        animationComponent=new QComboBox(toolbar);animationComponent->setObjectName(QStringLiteral("fieldAnimationCoefficient"));toolbar->addWidget(animationComponent);
        animationTime=new QLabel(toolbar);animationTime->setObjectName(QStringLiteral("fieldAnimationTime"));toolbar->addWidget(animationTime);
        QObject::connect(animationPlay,&QAction::toggled,page,[this](bool on){if(activate)activate();if(on&&state.animation&&state.animationFrame+1==state.animation->selectedFrames.size())showAnimationFrame(0);setAnimationPlaying(on);});
        QObject::connect(animationPrevious,&QAction::triggered,page,[this]{if(activate)activate();setAnimationPlaying(false);if(state.animationFrame)showAnimationFrame(state.animationFrame-1);});
        QObject::connect(animationNext,&QAction::triggered,page,[this]{if(activate)activate();setAnimationPlaying(false);showAnimationFrame(state.animationFrame+1);});
        QObject::connect(animationSlider,&QSlider::valueChanged,page,[this](int index){if(activate)activate();setAnimationPlaying(false);showAnimationFrame(static_cast<std::size_t>(index));});
        QObject::connect(animationComponent,&QComboBox::currentIndexChanged,page,[this](int component){if(!state.animation||component<0)return;if(activate)activate();setAnimationPlaying(false);state.animationCoefficient=static_cast<std::size_t>(component);const auto range=state.animation->coefficientRanges[state.animationCoefficient];state.style.colorLimits=range;state.style.limits[state.animation->dimension==1?1:2]=range;showAnimationFrame(state.animationFrame,true);});
        QObject::connect(animationTimer,&QTimer::timeout,page,[this]{if(!state.animation){setAnimationPlaying(false);return;}if(state.animationFrame+1<state.animation->selectedFrames.size())showAnimationFrame(state.animationFrame+1);else if(state.animation->loop)showAnimationFrame(0);else setAnimationPlaying(false);});
    }
    void assignAnimation(std::shared_ptr<const plotting::FieldAnimationData> data){
        setAnimationPlaying(false);state.animation=std::move(data);state.basisLabels=state.animation->basisLabels;state.animationFrame=0;state.animationCoefficient=state.animation->coefficient;if(state.title==animationDefaultTitle)state.title.clear();animationDefaultTitle.clear();
        animationControls();animationPrevious->setVisible(true);animationNext->setVisible(true);animationPlay->setVisible(true);animationSlider->show();animationTime->show();animationComponent->show();
        {QSignalBlocker block(animationSlider);animationSlider->setRange(0,static_cast<int>(state.animation->selectedFrames.size()-1));}
        {QSignalBlocker block(animationComponent);animationComponent->clear();for(std::size_t k=0;k<state.animation->components;++k)animationComponent->addItem(basisName(state,static_cast<int>(k)));animationComponent->setCurrentIndex(static_cast<int>(state.animationCoefficient));}
        const auto first=plotting::fieldAnimationFrame(*state.animation,0);state.style.limits=first.style.limits;state.style.colorLimits=first.style.colorLimits;state.style.hold=false;state.style.legendVisible=state.animation->dimension==1;
        showAnimationFrame(0,true);
    }
    void clearAnimation(){
        setAnimationPlaying(false);if(state.animation){state.style.limits={};state.style.colorLimits.reset();}state.animation.reset();if(animationPlay){animationPrevious->setVisible(false);animationNext->setVisible(false);animationPlay->setVisible(false);animationSlider->hide();animationTime->hide();animationComponent->hide();}
    }

    std::array<runtime::AxisLimits,2> visibleLimits() const {
        std::array<runtime::AxisLimits,2> result{};
        if(!renderer||!canvas||!window)return result;
        auto* camera=renderer->GetActiveCamera();const double* focus=camera->GetFocalPoint();const double span=camera->GetParallelScale();
        for(std::size_t d=0;d<2;++d){result[d].lower=renderCenter[d]+(focus[d]-span)*renderUnits[d];result[d].upper=renderCenter[d]+(focus[d]+span)*renderUnits[d];}
        return result;
    }

    void rebuild(bool resetCamera=true,bool preserveGraphBounds=false) {
        if(state.dimensions!=3&&tool==PlotTool::Rotate)setTool(PlotTool::Navigate);
        for(const auto& [mode,action]:toolActions){action->setEnabled(state.dimensions!=0&&(mode!=PlotTool::Rotate||state.dimensions==3));
            if(mode==PlotTool::Rotate)action->setVisible(state.dimensions==3);
            if(mode==PlotTool::GraphSelect||mode==PlotTool::GraphDrag){const bool usable=state.graph&&state.graph->interactive;action->setVisible(state.graph.has_value());action->setEnabled(usable);}}
        if(graphLayoutAction)graphLayoutAction->setVisible(state.graph.has_value());
        if(pointsAction)pointsAction->setEnabled(state.dimensions!=0);
        if(saveAction)saveAction->setEnabled(state.dimensions!=0);
        if (!renderer) return;
        canvas->descriptions.clear();
        overlays.clear();tickLabels.clear();graphText.clear();graphLines.clear();contextCaption=nullptr;
        pointActors.clear();
        for(auto& tip:dataTips){tip.text=nullptr;tip.marker=nullptr;tip.hitRectangle={};}
        renderer->RemoveAllViewProps();
        annotationRenderer->RemoveAllViewProps();
        if (state.dimensions == 0) { addLabels();render(); return; }
        // Generic x/y charts deliberately use independent axis units. Fixing
        // the projection aspect lets a time series fill a rectangular panel;
        // algebra projections keep the camera's physical equal-unit aspect.
        renderer->GetActiveCamera()->SetUseExplicitAspectRatio(state.dimensions==2&&state.axes[0]<0&&!state.style.equalUnits);
        renderer->GetActiveCamera()->SetExplicitAspectRatio(1.0);

        double scale = 0.0;
        for (const auto& vector : state.vectors)
            for (int dimension = 0; dimension < state.dimensions; ++dimension)
                scale = std::max(scale, std::abs(vector.coefficient(
                    static_cast<std::size_t>(state.axes[static_cast<std::size_t>(dimension)]))));
        if (scale == 0.0) scale = 1.0;
        std::array<double,3> center{}, units{{scale,scale,scale}};
        if(state.graph)graphLines=plotting::graphSegments(*state.graph);
        if(!state.curves.empty()||(state.mesh&&!state.mesh->points.empty())||state.graph){
            std::array<double,3> low{{INFINITY,INFINITY,INFINITY}},high{{-INFINITY,-INFINITY,-INFINITY}};
            for(const auto& curve:state.curves)for(const auto& point:curve.points)
                for(int axis=0;axis<state.dimensions;++axis){
                    const auto at=static_cast<std::size_t>(axis);
                    low[at]=std::min(low[at],point[at]);high[at]=std::max(high[at],point[at]);
                }
            if(state.graph){auto include=[&](const auto& point){for(int d=0;d<state.dimensions;++d){low[static_cast<std::size_t>(d)]=std::min(low[static_cast<std::size_t>(d)],point[static_cast<std::size_t>(d)]);high[static_cast<std::size_t>(d)]=std::max(high[static_cast<std::size_t>(d)],point[static_cast<std::size_t>(d)]);}};for(const auto& point:state.graph->positions)include(point);for(const auto& line:graphLines){include(line.first);include(line.last);}}
            if(state.mesh)for(const auto& point:state.mesh->points)for(int axis=0;axis<state.dimensions;++axis){const auto at=static_cast<std::size_t>(axis);low[at]=std::min(low[at],point[at]);high[at]=std::max(high[at],point[at]);}
            for(const auto& vector:state.vectors)for(int axis=0;axis<state.dimensions;++axis){const auto at=static_cast<std::size_t>(axis);const auto value=vector.coefficient(static_cast<std::size_t>(state.axes[at]));low[at]=std::min({low[at],0.0,value});high[at]=std::max({high[at],0.0,value});}
            scale=0;
            for(int axis=0;axis<state.dimensions;++axis){
                const auto at=static_cast<std::size_t>(axis);
                // Half first to avoid overflowing a finite double range.
                center[at]=low[at]*0.5+high[at]*0.5;
                units[at]=high[at]*0.5-low[at]*0.5;
                scale=std::max(scale,units[at]);
            }
            if(scale==0)scale=1;
            for(int axis=0;axis<state.dimensions;++axis){
                auto& unit=units[static_cast<std::size_t>(axis)];
                if(state.axes[0]>=0||state.style.equalUnits)unit=scale;
                else if(unit==0)unit=std::max(1.0,std::abs(center[static_cast<std::size_t>(axis)])*0.1);
            }
        }
        for(int d=0;d<state.dimensions;++d)if(state.style.limits[static_cast<std::size_t>(d)]){const auto bounds=*state.style.limits[static_cast<std::size_t>(d)];center[static_cast<std::size_t>(d)]=bounds.lower*.5+bounds.upper*.5;units[static_cast<std::size_t>(d)]=std::max(std::numeric_limits<double>::denorm_min(),bounds.upper*.5-bounds.lower*.5);}
        if(state.style.equalUnits){const double common=*std::max_element(units.begin(),units.begin()+state.dimensions);for(int d=0;d<state.dimensions;++d)units[static_cast<std::size_t>(d)]=common;}
        if(preserveGraphBounds&&state.graph){center=renderCenter;units=renderUnits;}
        renderCenter=center;renderUnits=units;
        auto clipMapper=[&](vtkPolyDataMapper* mapper){for(int d=0;d<state.dimensions;++d)if(state.style.limits[static_cast<std::size_t>(d)])for(int side:{-1,1}){vtkNew<vtkPlane> plane;double origin[3]{},normal[3]{};const auto limit=*state.style.limits[static_cast<std::size_t>(d)];origin[d]=static_cast<double>((static_cast<long double>(side<0?limit.lower:limit.upper)-center[static_cast<std::size_t>(d)])/units[static_cast<std::size_t>(d)]);normal[d]=side<0?1:-1;plane->SetOrigin(origin);plane->SetNormal(normal);auto clip=vtkSmartPointer<vtkClipPolyData>::New();clip->SetInputConnection(mapper->GetInputConnection(0,0));clip->SetClipFunction(plane);mapper->SetInputConnection(clip->GetOutputPort());}};
        auto addPoints=[&](vtkPoints* points,const double* color){
            vtkNew<vtkCellArray> vertices;vertices->InsertNextCell(points->GetNumberOfPoints());
            for(vtkIdType index=0;index<points->GetNumberOfPoints();++index)vertices->InsertCellPoint(index);
            vtkNew<vtkPolyData> data;data->SetPoints(points);data->SetVerts(vertices);
            vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(data);
            auto actor=vtkSmartPointer<vtkActor>::New();actor->SetMapper(mapper);
            actor->GetProperty()->SetColor(color[0],color[1],color[2]);actor->GetProperty()->SetPointSize(5);
            actor->GetProperty()->SetRepresentationToPoints();actor->GetProperty()->LightingOff();
            actor->SetVisibility(pointsVisible);renderer->AddActor(actor);pointActors.push_back(actor);
        };
        // Common render-space scaling preserves relative lengths and equal axis
        // units while avoiding GPU overflow for finite binary64 coordinates.
        const bool scaledLabels = scale > 1e100 || scale < 1e-100;
        const bool genericXY=state.dimensions==2&&state.axes[0]<0;
        for (int dimension = 0; dimension < state.dimensions; ++dimension) {
            double beginning[]{0,0,0}, end[]{0,0,0};
            beginning[dimension]=-1.15; end[dimension]=1.15;
            if(genericXY)beginning[1-dimension]=end[1-dimension]=-1.15;
            vtkNew<vtkLineSource> line;
            line->SetPoint1(beginning); line->SetPoint2(end);
            const double shade=genericXY?0.42:0.75;
            const double gray[]{shade,shade,shade};
            auto actor = sourceActor(line->GetOutputPort(), gray);
            actor->GetProperty()->SetLineWidth(1);
            renderer->AddActor(actor);
            // Sparse, horizontal numeric labels remain readable in small tiles
            // and under 3D rotation. Ticks use the same common unit on all axes.
            std::vector<std::pair<double,double>> ticks;
            const auto at=static_cast<std::size_t>(dimension);
            if(genericXY){
                const double rawStep=std::max(units[at]*0.5,std::numeric_limits<double>::denorm_min());
                double magnitude=std::pow(10.0,std::floor(std::log10(rawStep)));
                if(magnitude==0)magnitude=rawStep;
                const double fraction=rawStep/magnitude;
                const double step=magnitude*(fraction<=1?1:fraction<=2?2:fraction<=2.5?2.5:fraction<=5?5:10);
                const long double first=std::ceil((static_cast<long double>(center[at])-1.15L*units[at])/step);
                const long double last=std::floor((static_cast<long double>(center[at])+1.15L*units[at])/step);
                for(int tick=0;tick<10&&first+tick<=last;++tick){
                    const long double value=(first+tick)*step;
                    if(std::abs(value)>std::numeric_limits<double>::max())continue;
                    ticks.emplace_back(static_cast<double>((value-center[at])/units[at]),static_cast<double>(value));
                }
            }else for(int tick=-2;tick<=2;++tick){
                if(state.dimensions==3&&std::abs(tick)!=2)continue;
                const double coordinate=tick*0.5;
                if(tick==0&&dimension!=0)continue;
                ticks.emplace_back(coordinate,(!state.curves.empty()||state.mesh)?center[at]+coordinate*units[at]
                    :coordinate*(scaledLabels?1.0:scale));
            }
            for(const auto& [coordinate,coordinateValue]:ticks){
                double start[]{0,0,0},finish[]{0,0,0};
                start[dimension]=finish[dimension]=coordinate;
                const int cross=dimension==0?1:0;
                start[cross]=genericXY?-1.15:-0.022;finish[cross]=genericXY?-1.19:0.022;
                vtkNew<vtkLineSource> mark;mark->SetPoint1(start);mark->SetPoint2(finish);
                auto tickActor=sourceActor(mark->GetOutputPort(),gray);
                renderer->AddActor(tickActor);
                vtkNew<vtkTextActor> value;
                const QByteArray text=QString::number(coordinateValue,'g',4).toUtf8();
                value->SetInput(text.constData());
                value->GetTextProperty()->SetColor(0.28,0.28,0.28);value->GetTextProperty()->SetFontFamily(state.style.fontFamily=="serif"?VTK_TIMES:state.style.fontFamily=="mono"?VTK_COURIER:VTK_ARIAL);
                if(genericXY){
                    if(dimension==0){value->GetTextProperty()->SetJustificationToCentered();value->GetTextProperty()->SetVerticalJustificationToTop();}
                    else{value->GetTextProperty()->SetJustificationToRight();value->GetTextProperty()->SetVerticalJustificationToCentered();}
                }
                std::array<double,3> point{};point[static_cast<std::size_t>(dimension)]=coordinate;
                if(genericXY)point[static_cast<std::size_t>(cross)]=-1.15;
                tickLabels.push_back({point,dimension,value});
                annotationRenderer->AddViewProp(value);
            }
        }

        legend=vtkSmartPointer<vtkLegendBoxActor>::New();
        const std::size_t seriesCount=state.vectors.size()+state.curves.size();
        legend->SetNumberOfEntries(static_cast<int>(seriesCount));
        const double legendHeight=std::min(0.50,0.026*seriesCount+0.020);
        legend->SetPosition(0.73,0.94-legendHeight);
        legend->SetPosition2(0.25,legendHeight);
        if(genericXY){
            // Keep the automatic series key outside the curve area, including
            // peaks near the top edge of a tightly fitted time-series panel.
            legend->SetPosition(0.72,0.02);legend->SetPosition2(0.26,std::min(0.18,legendHeight));
        }
        legend->BorderOn(); legend->BoxOn();
        legend->GetBoxProperty()->SetColor(1,1,1);
        legend->GetEntryTextProperty()->SetColor(0.1,0.1,0.1);
        legend->GetEntryTextProperty()->SetFontSize(static_cast<int>(state.style.fontSize));
        legend->GetEntryTextProperty()->SetFontFamily(state.style.fontFamily=="serif"?VTK_TIMES:state.style.fontFamily=="mono"?VTK_COURIER:VTK_ARIAL);
        legend->SetVisibility(state.style.legendVisible);legend->SetBox(state.style.legendBox);legend->SetBorder(state.style.legendBox);
        if(state.style.legendLocation!="auto"){const bool west=state.style.legendLocation=="northwest"||state.style.legendLocation=="southwest",south=state.style.legendLocation=="southwest"||state.style.legendLocation=="southeast"||state.style.legendLocation=="outside";legend->SetPosition(west?.04:.70,south?.03:.94-legendHeight);legend->SetPosition2(.26,legendHeight);}

        for (std::size_t index=0; index<state.vectors.size(); ++index) {
            const auto& vector=state.vectors[index];
            double endpoint[]{0,0,0};
            for (int dimension=0; dimension<state.dimensions; ++dimension)
                endpoint[dimension]=static_cast<double>(static_cast<long double>(vector.coefficient(
                    static_cast<std::size_t>(state.axes[static_cast<std::size_t>(dimension)]))) / scale);
            double origin[3]{};for(int d=0;d<state.dimensions;++d){origin[d]=-center[static_cast<std::size_t>(d)]/units[static_cast<std::size_t>(d)];endpoint[d]=static_cast<double>((static_cast<long double>(vector.coefficient(static_cast<std::size_t>(state.axes[static_cast<std::size_t>(d)])))-center[static_cast<std::size_t>(d)])/units[static_cast<std::size_t>(d)]);}
            const double direction[]{endpoint[0]-origin[0],endpoint[1]-origin[1],endpoint[2]-origin[2]};
            const double length=std::hypot(direction[0],direction[1],direction[2]);
            const auto selectedColor=runtime::plotColor(state.style,index);const double* color=selectedColor.data();
            vtkSmartPointer<vtkActor> actor;
            vtkNew<vtkArrowSource> arrow;
            arrow->SetTipLength(0.25); arrow->SetTipRadius(0.075); arrow->SetShaftRadius(0.022);
            arrow->SetTipResolution(24); arrow->SetShaftResolution(20);
            arrow->Update();
            if (length == 0.0) {
                vtkNew<vtkSphereSource> point;
                point->SetRadius(0.035); point->SetThetaResolution(20); point->SetPhiResolution(16);
                actor=sourceActor(point->GetOutputPort(),color);actor->SetPosition(origin);
            } else {
                actor=sourceActor(arrow->GetOutputPort(),color);
                double x[]{direction[0]/length,direction[1]/length,direction[2]/length};
                const double helper[]{std::abs(x[0])<0.9 ? 1.0 : 0.0,
                                      std::abs(x[0])<0.9 ? 0.0 : 1.0,0.0};
                double y[]{helper[1]*x[2]-helper[2]*x[1],helper[2]*x[0]-helper[0]*x[2],
                           helper[0]*x[1]-helper[1]*x[0]};
                const double yLength=std::hypot(y[0],y[1],y[2]);
                for(double& component:y) component/=yLength;
                const double z[]{x[1]*y[2]-x[2]*y[1],x[2]*y[0]-x[0]*y[2],x[0]*y[1]-x[1]*y[0]};
                vtkNew<vtkMatrix4x4> transform;
                transform->Identity();
                for(int row=0;row<3;++row) {
                    transform->SetElement(row,0,x[row]*length);
                    transform->SetElement(row,1,y[row]*length);
                    transform->SetElement(row,2,z[row]*length);
                    transform->SetElement(row,3,origin[row]);
                }
                actor->SetUserMatrix(transform);
            }
            if(std::any_of(state.style.limits.begin(),state.style.limits.end(),[](const auto& value){return value.has_value();})){
                auto* mapper=vtkPolyDataMapper::SafeDownCast(actor->GetMapper());mapper->Update();vtkNew<vtkPolyData> transformed;transformed->ShallowCopy(mapper->GetInput());vtkNew<vtkPoints> transformedPoints;transformedPoints->SetDataTypeToDouble();
                for(vtkIdType k=0;k<mapper->GetInput()->GetNumberOfPoints();++k){double point[3],in[4],out[4];mapper->GetInput()->GetPoint(k,point);for(int d=0;d<3;++d)in[d]=point[d];in[3]=1;actor->GetMatrix()->MultiplyPoint(in,out);transformedPoints->InsertNextPoint(out);}
                transformed->SetPoints(transformedPoints);mapper->SetInputData(transformed);actor->SetUserMatrix(nullptr);actor->SetPosition(0,0,0);clipMapper(mapper);
            }
            renderer->AddActor(actor);
            vtkNew<vtkPoints> vertex;vertex->SetDataTypeToDouble();vertex->InsertNextPoint(endpoint);addPoints(vertex,color);
            canvas->descriptions.emplace(actor.GetPointer(),vectorDescription(state,vector,index));
            const auto label=QString::fromStdString(runtime::plotLegendLabel(state.style,index,vectorLabel(vector,index).toStdString())).toUtf8();
            double legendColor[]{color[0],color[1],color[2]};
            legend->SetEntry(static_cast<int>(index),arrow->GetOutput(),label.constData(),legendColor);
        }
        for(std::size_t index=0;index<state.curves.size();++index){
            const auto& curve=state.curves[index];
            vtkNew<vtkPoints> points;points->SetDataTypeToDouble();
            vtkNew<vtkCellArray> lines;lines->InsertNextCell(static_cast<vtkIdType>(curve.points.size()));
            for(const auto& point:curve.points){
                double normalized[]{0,0,0};
                for(int axis=0;axis<state.dimensions;++axis){
                    const auto at=static_cast<std::size_t>(axis);
                    normalized[axis]=static_cast<double>(std::clamp((static_cast<long double>(point[at])-center[at])/units[at],-1e10L,1e10L));
                }
                lines->InsertCellPoint(points->InsertNextPoint(normalized));
            }
            vtkNew<vtkPolyData> data;data->SetPoints(points);data->SetLines(lines);
            vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(state.style.lineStyle=="solid"?data.GetPointer():dashedCurve(points,state.style.lineStyle).GetPointer());
            vtkNew<vtkActor> actor;actor->SetMapper(mapper);
            const auto selectedColor=runtime::plotColor(state.style,index+state.vectors.size());const double* color=selectedColor.data();
            actor->GetProperty()->SetColor(color[0],color[1],color[2]);actor->GetProperty()->SetLineWidth(static_cast<float>(state.style.lineWidth));clipMapper(mapper);
            actor->GetProperty()->LightingOff();renderer->AddActor(actor);
            addPoints(points,color);
            const QString text=curve.label.empty()?QStringLiteral("Curve %1").arg(index+1):QString::fromStdString(curve.label);
            canvas->descriptions.emplace(actor.GetPointer(),QStringLiteral("%1\n%2 sampled points\n%3")
                .arg(text).arg(curve.points.size()).arg(contextText(state)));
            vtkNew<vtkLineSource> symbol;symbol->SetPoint1(0,0,0);symbol->SetPoint2(1,0,0);symbol->Update();
            double legendColor[]{color[0],color[1],color[2]};
            const auto legendText=runtime::plotLegendLabel(state.style,index+state.vectors.size(),text.toStdString());
            legend->SetEntry(static_cast<int>(index+state.vectors.size()),symbol->GetOutput(),legendText.c_str(),legendColor);
        }
        if(seriesCount){if(genericXY)annotationRenderer->AddViewProp(legend);else renderer->AddViewProp(legend);}
        if(state.mesh&&!state.mesh->points.empty()){
            const auto& mesh=*state.mesh;
            vtkNew<vtkPoints> points;points->SetDataTypeToDouble();
            for(const auto& point:mesh.points){double p[3];for(int d=0;d<3;++d)p[d]=d<state.dimensions?static_cast<double>((static_cast<long double>(point[d])-center[d])/units[d]):0;points->InsertNextPoint(p);}
            vtkNew<vtkCellArray> faces;for(const auto& f:mesh.triangles){vtkIdType ids[]{static_cast<vtkIdType>(f[0]),static_cast<vtkIdType>(f[1]),static_cast<vtkIdType>(f[2])};faces->InsertNextCell(3,ids);}
            vtkNew<vtkCellArray> edges;for(const auto& e:mesh.segments){vtkIdType ids[]{static_cast<vtkIdType>(e[0]),static_cast<vtkIdType>(e[1])};edges->InsertNextCell(2,ids);}
            vtkNew<vtkPolyData> data;data->SetPoints(points);data->SetPolys(faces);data->SetLines(edges);
            vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(data);
            vtkNew<vtkActor> actor;actor->SetMapper(mapper);
            if(mesh.scalars.empty()){
                mapper->ScalarVisibilityOff();
                const auto color=runtime::plotColor(state.style,seriesCount);actor->GetProperty()->SetColor(color[0],color[1],color[2]);
            }else{
                vtkNew<vtkDoubleArray> values;values->SetName("Field value");for(const double v:mesh.scalars)values->InsertNextValue(v);data->GetPointData()->SetScalars(values);
                const auto [lo,hi]=std::minmax_element(mesh.scalars.begin(),mesh.scalars.end());const bool constant=!state.style.colorLimits&&*hi==*lo;const double padding=std::max(1.0,std::abs(*lo)*.05);const double low=state.style.colorLimits?state.style.colorLimits->lower:constant?(std::isfinite(*lo-padding)?*lo-padding:*lo):*lo,high=state.style.colorLimits?state.style.colorLimits->upper:constant?(std::isfinite(*lo+padding)?*lo+padding:*lo):*hi;
                vtkNew<vtkLookupTable> colors;colors->SetNumberOfTableValues(256);colors->SetRange(low,high);colors->SetHueRange(.667,constant?.667:0);colors->Build();mapper->SetLookupTable(colors);mapper->SetScalarRange(low,high);
                vtkNew<vtkScalarBarActor> key;key->SetLookupTable(colors);key->SetNumberOfLabels(constant?1:5);if(constant){vtkNew<vtkDoubleArray> labels;labels->InsertNextValue(*lo);key->SetCustomLabels(labels);key->SetUseCustomLabels(true);}key->SetPosition(.88,.25);key->SetWidth(.08);key->SetHeight(.45);key->GetLabelTextProperty()->SetColor(.15,.15,.15);key->GetLabelTextProperty()->SetFontSize(11);annotationRenderer->AddViewProp(key);
            }
            if(state.dimensions==3&&!mesh.triangles.empty()){vtkNew<vtkCleanPolyData> clean;clean->SetInputData(data);clean->SetTolerance(1e-10);vtkNew<vtkPolyDataNormals> normals;normals->SetInputConnection(clean->GetOutputPort());normals->SplittingOff();normals->ConsistencyOn();normals->AutoOrientNormalsOn();mapper->SetInputConnection(normals->GetOutputPort());actor->GetProperty()->LightingOn();actor->GetProperty()->SetInterpolationToPhong();actor->GetProperty()->SetAmbient(.25);actor->GetProperty()->SetDiffuse(.75);actor->GetProperty()->SetSpecular(.1);}else actor->GetProperty()->LightingOff();
            actor->GetProperty()->SetLineWidth(static_cast<float>(state.style.lineWidth));clipMapper(mapper);renderer->AddActor(actor);canvas->descriptions.emplace(actor.GetPointer(),QString::fromStdString(mesh.label));
            const double marker[]{.15,.15,.15};addPoints(points,marker);
        }
        if(state.graph){const auto& graph=*state.graph;const std::set<std::uint32_t> selectedNodes(graph.selectedNodes.begin(),graph.selectedNodes.end()),highlightedNodes(graph.highlightedNodes.begin(),graph.highlightedNodes.end());
            const std::array<std::array<double,3>,3> colors{{{{.38,.40,.43}},{{.88,.12,.20}},{{.96,.56,.10}}}};
            auto actor=[&](vtkPoints* points,vtkCellArray* cells,bool nodes,const std::array<double,3>& color,double width){vtkNew<vtkPolyData> data;data->SetPoints(points);if(nodes)data->SetVerts(cells);else data->SetLines(cells);vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(data);clipMapper(mapper);vtkNew<vtkActor> item;item->SetMapper(mapper);item->GetProperty()->SetColor(color[0],color[1],color[2]);item->GetProperty()->LightingOff();if(nodes){item->GetProperty()->SetPointSize(static_cast<float>(width));item->GetProperty()->RenderPointsAsSpheresOn();}else item->GetProperty()->SetLineWidth(static_cast<float>(width));renderer->AddActor(item);};
            for(int kind=0;kind<3;++kind){vtkNew<vtkPoints> points;points->SetDataTypeToDouble();vtkNew<vtkCellArray> cells;for(const auto& line:graphLines)if((line.selected?2:line.highlighted?1:0)==kind){const auto a=worldPoint(line.first),b=worldPoint(line.last);vtkIdType ids[]{points->InsertNextPoint(a.data()),points->InsertNextPoint(b.data())};cells->InsertNextCell(2,ids);}actor(points,cells,false,colors[static_cast<std::size_t>(kind)],graph.edgeWidth+(kind?1.5:0));}
            auto label=[&](const auto& point,const std::string& text){vtkNew<vtkTextActor> item;item->SetInput(text.c_str());item->GetTextProperty()->SetColor(.12,.12,.12);item->GetTextProperty()->SetBackgroundColor(1,1,1);item->GetTextProperty()->SetBackgroundOpacity(.7);annotationRenderer->AddViewProp(item);graphText.push_back({point,item});};
            for(int kind=0;kind<3;++kind){vtkNew<vtkPoints> points;points->SetDataTypeToDouble();vtkNew<vtkCellArray> cells;for(std::size_t n=0;n<graph.positions.size();++n){const auto id=static_cast<std::uint32_t>(n+1);if((selectedNodes.contains(id)?2:highlightedNodes.contains(id)?1:0)!=kind)continue;const auto p=worldPoint(graph.positions[n]);const auto vertex=points->InsertNextPoint(p.data());cells->InsertNextCell(1,&vertex);}actor(points,cells,true,kind?colors[static_cast<std::size_t>(kind)]:runtime::plotColor(state.style,0),graph.nodeSize+(kind?2:0));}
            if(graph.nodeLabels)for(std::size_t n=0;n<graph.positions.size();++n)label(graph.positions[n],plotting::graphNodeLabel(graph,static_cast<std::uint32_t>(n+1)));
            if(graph.edgeLabels){const auto anchors=plotting::graphEdgeAnchors(graph,graphLines);for(std::size_t e=0;e<graph.edges.size();++e)label(anchors[e],std::to_string(e+1)+": "+QString::number(graph.weights[e],'g',6).toStdString());}
        }
        vtkNew<vtkTextActor> caption;
        QString text=contextText(state);
        if(state.graph)text+=QStringLiteral(" | %1 nodes, %2 edges%3").arg(state.graph->positions.size()).arg(state.graph->edges.size()).arg(state.graph->interactive?QString():QStringLiteral(" | static view"));
        if(scaledLabels) text+=QStringLiteral(" | all axes x %1").arg(scale,0,'g',15);
        const QByteArray captionUtf8=text.toUtf8();
        caption->SetInput(captionUtf8.constData());
        caption->SetDisplayPosition(12,10);
        caption->GetTextProperty()->SetFontSize(13);
        caption->GetTextProperty()->SetColor(0.12,0.12,0.12);
        annotationRenderer->AddViewProp(caption);
        contextCaption=caption;
        addLabels();
        refreshDataTip();
        if(state.dimensions==2) {
            vtkNew<vtkInteractorStyleImage> style;
            style->SetInteractionModeToImage2D();
            style->SetDefaultRenderer(renderer);style->SetCurrentRenderer(renderer);
            window->GetInteractor()->SetInteractorStyle(style);
        } else {
            vtkNew<vtkInteractorStyleTrackballCamera> style;
            style->SetDefaultRenderer(renderer);style->SetCurrentRenderer(renderer);
            window->GetInteractor()->SetInteractorStyle(style);
        }
        if(resetCamera)resetView();else render();
    }
};
} // namespace

namespace {
std::unique_ptr<FigurePanel> createPanel(QWidget* parent, bool renderingEnabled, int index) {
    auto panel=std::make_unique<FigurePanel>();
    panel->state.index=index;
    panel->page=new QWidget(parent);
    panel->page->setObjectName(QStringLiteral("subplot%1").arg(index));
    auto* layout=new QVBoxLayout(panel->page);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(0);
    auto* raw=panel.get();
    panel->toolbar=new QToolBar(panel->page);panel->toolbar->setObjectName(QStringLiteral("subplotToolbar"));
    panel->toolbar->setAccessibleName(QStringLiteral("Plot tools"));panel->toolbar->setIconSize(QSize(16,16));
    panel->toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);panel->toolbar->setMovable(false);panel->toolbar->setFloatable(false);
    panel->toolbar->setStyleSheet(QStringLiteral("QToolBar { border: 0; spacing: 1px; padding: 0; } QToolButton { padding: 2px; }"));
    auto* modes=new QActionGroup(panel->page);modes->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    auto add=[&](const char* name,const QString& text,const QString& drawing,QStyle::StandardPixmap fallback){
        auto* action=panel->toolbar->addAction(plotIcon(drawing,fallback),text);action->setObjectName(QString::fromLatin1(name));action->setToolTip(text);
        if(auto* button=qobject_cast<QToolButton*>(panel->toolbar->widgetForAction(action))){
            button->setObjectName(QString::fromLatin1(name)+QStringLiteral("Button"));button->setAccessibleName(text);button->setToolTip(text);}
        return action;
    };
    auto mode=[&](PlotTool tool,const char* name,const QString& text,const QString& drawing,QStyle::StandardPixmap fallback){
        auto* action=add(name,text,drawing,fallback);action->setCheckable(true);modes->addAction(action);panel->toolActions.emplace(tool,action);
        QObject::connect(action,&QAction::triggered,panel->page,[raw,tool](bool checked){if(raw->activate)raw->activate();raw->setTool(checked?tool:PlotTool::Navigate);});
    };
    mode(PlotTool::ZoomIn,"plotZoomIn",QStringLiteral("Zoom in — click the plot"),QStringLiteral("<circle cx='7' cy='7' r='4.5'/><path d='M10.5 10.5L15.5 15.5M4.5 7h5M7 4.5v5'/>"),QStyle::SP_ArrowUp);
    mode(PlotTool::ZoomOut,"plotZoomOut",QStringLiteral("Zoom out — click the plot"),QStringLiteral("<circle cx='7' cy='7' r='4.5'/><path d='M10.5 10.5L15.5 15.5M4.5 7h5'/>"),QStyle::SP_ArrowDown);
    mode(PlotTool::Pan,"plotPan",QStringLiteral("Pan — drag the plot"),QStringLiteral("<path d='M9 2v14M2 9h14M6 5l3-3 3 3M6 13l3 3 3-3M5 6L2 9l3 3M13 6l3 3-3 3'/>"),QStyle::SP_ArrowForward);
    auto* fit=add("plotResetView",QStringLiteral("Reset / fit all data"),QStringLiteral("<path d='M2 7V2h5M11 2h5v5M16 11v5h-5M7 16H2v-5M6 6h6v6H6z'/>"),QStyle::SP_DialogResetButton);
    QObject::connect(fit,&QAction::triggered,panel->page,[raw]{if(raw->activate)raw->activate();raw->resetView();});
    mode(PlotTool::Rotate,"plotRotate",QStringLiteral("Rotate 3D — drag the plot"),QStringLiteral("<path d='M14 5a6 6 0 1 0 1 7M14 2v4h-4'/><ellipse cx='9' cy='9' rx='3' ry='6'/>"),QStyle::SP_BrowserReload);
    mode(PlotTool::DataCursor,"plotDataCursor",QStringLiteral("Data cursor — add tips; click a tip to select; Delete / Backspace / Escape removes the selected tip"),QStringLiteral("<circle cx='9' cy='9' r='4'/><path d='M9 1v5M9 12v5M1 9h5M12 9h5'/><circle cx='9' cy='9' r='1'/>"),QStyle::SP_DialogHelpButton);
    mode(PlotTool::GraphSelect,"plotGraphSelect",QStringLiteral("Select graph nodes or edges — Ctrl adds/removes selection"),QStringLiteral("<circle cx='4' cy='4' r='2'/><circle cx='14' cy='4' r='2'/><circle cx='9' cy='14' r='2'/><path d='M6 4h6M5 6l3 6M13 6l-3 6'/>"),QStyle::SP_ArrowForward);
    mode(PlotTool::GraphDrag,"plotGraphDrag",QStringLiteral("Move graph node — drag in the current view plane"),QStringLiteral("<circle cx='9' cy='9' r='3'/><path d='M9 1v4M9 13v4M1 9h4M13 9h4'/>"),QStyle::SP_FileDialogDetailedView);
    panel->graphLayoutAction=add("plotGraphLayout",QStringLiteral("Graph layouts and saved positions"),QStringLiteral("<circle cx='3' cy='3' r='1'/><circle cx='9' cy='3' r='1'/><circle cx='15' cy='3' r='1'/><circle cx='3' cy='9' r='1'/><circle cx='9' cy='9' r='1'/><circle cx='15' cy='9' r='1'/><circle cx='3' cy='15' r='1'/><circle cx='9' cy='15' r='1'/><circle cx='15' cy='15' r='1'/>"),QStyle::SP_FileDialogListView);
    QObject::connect(panel->graphLayoutAction,&QAction::triggered,panel->page,[raw]{if(!raw->state.graph)return;auto* menu=new QMenu(raw->page);menu->setAttribute(Qt::WA_DeleteOnClose);for(const auto& method:{"circle","grid","random","sphere"}){if(std::string(method)=="sphere"&&raw->state.dimensions!=3)continue;auto* action=menu->addAction(QString::fromLatin1(method));QObject::connect(action,&QAction::triggered,raw->page,[raw,method]{QString error;if(!raw->changeGraphLayout(method,raw->state.graph->seed,&error))QToolTip::showText(QCursor::pos(),error,raw->page);});}menu->addSeparator();auto* save=menu->addAction(QStringLiteral("Save graph layout…"));QObject::connect(save,&QAction::triggered,raw->page,[raw]{const auto path=QFileDialog::getSaveFileName(raw->page,QStringLiteral("Save graph layout"),QString(),QStringLiteral("Graph view JSON (*.json)"));if(path.isEmpty())return;QString error;if(!raw->saveGraph(path,&error))QToolTip::showText(QCursor::pos(),error,raw->page);});auto* load=menu->addAction(QStringLiteral("Load graph layout…"));QObject::connect(load,&QAction::triggered,raw->page,[raw]{const auto path=QFileDialog::getOpenFileName(raw->page,QStringLiteral("Load graph layout"),QString(),QStringLiteral("Graph view JSON (*.json)"));if(path.isEmpty())return;QString error;if(!raw->loadGraph(path,&error))QToolTip::showText(QCursor::pos(),error,raw->page);});menu->popup(QCursor::pos());});
    panel->clearTipsAction=add("plotClearDataTips",QStringLiteral("Clear all data tips in this subplot"),QStringLiteral("<path d='M4 5h10M7 2h4l1 3M5 5l1 11h6l1-11M8 8v5M10 8v5'/>"),QStyle::SP_TrashIcon);
    panel->clearTipsAction->setEnabled(false);
    QObject::connect(panel->clearTipsAction,&QAction::triggered,panel->page,[raw]{if(raw->activate)raw->activate();raw->clearAllTips();});
    panel->pointsAction=add("plotShowPoints",QStringLiteral("Show stored data points"),QStringLiteral("<path d='M3 13l5-8 6 5'/><circle cx='3' cy='13' r='1.6'/><circle cx='8' cy='5' r='1.6'/><circle cx='14' cy='10' r='1.6'/>"),QStyle::SP_FileDialogDetailedView);
    panel->pointsAction->setCheckable(true);
    QObject::connect(panel->pointsAction,&QAction::toggled,panel->page,[raw](bool visible){
        if(raw->activate)raw->activate();
        raw->pointsVisible=visible;for(const auto& actor:raw->pointActors)actor->SetVisibility(visible);raw->render();});
    panel->saveAction=add("plotSave",QStringLiteral("Save subplot…"),QStringLiteral("<path d='M3 2h10l3 3v11H2V2zM5 2v5h7V2M5 16v-6h8v6M10 3v3'/>"),QStyle::SP_DialogSaveButton);
    QObject::connect(panel->saveAction,&QAction::triggered,panel->page,[raw]{
        const QPointer<QWidget> page=raw->page;
        if(raw->activate)raw->activate();
        if(raw->canvas)raw->canvas->setFocus(Qt::OtherFocusReason);
        if(raw->saveRequested)raw->saveRequested();
        if(page&&raw->activate)raw->activate();
    });
    layout->addWidget(panel->toolbar);
    if(renderingEnabled){
        panel->canvas=new VectorCanvas(panel->page);
        panel->canvas->setMinimumSize(1,1);
        panel->canvas->setProperty("subplotIndex",index);
        panel->window=vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
        panel->window->SetReadyForRendering(false);
        vtkNew<vtkGenericRenderWindowInteractor> interactor;
        interactor->SetRenderWindow(panel->window);interactor->EnableRenderOff();
        panel->window->SetMultiSamples(0);panel->window->SetNumberOfLayers(3);
        panel->renderer=vtkSmartPointer<vtkRenderer>::New();
        panel->renderer->GetActiveCamera();panel->renderer->SetLayer(1);
        panel->renderer->SetBackground(1,1,1);panel->window->AddRenderer(panel->renderer);
        panel->renderer->UseFXAAOn();
        vtkNew<vtkRenderer> background;background->SetBackground(1,1,1);background->SetInteractive(false);
        panel->window->AddRenderer(background);
        panel->annotationRenderer=vtkSmartPointer<vtkRenderer>::New();
        panel->annotationRenderer->SetLayer(2);panel->annotationRenderer->SetInteractive(false);
        panel->annotationRenderer->SetPreserveColorBuffer(true);panel->annotationRenderer->SetPreserveDepthBuffer(true);
        panel->window->AddRenderer(panel->annotationRenderer);
        panel->canvas->renderer=panel->renderer;panel->canvas->setRenderWindow(panel->window);
        panel->canvas->setSceneProvider([raw](QSize pixels,double dpr){return raw->rasterSnapshot(pixels,dpr);});
        panel->canvas->interaction=[raw](QEvent* event){return raw->handleInteraction(event);};
        layout->addWidget(panel->canvas,1);
        panel->selectionBorder=new FigureSelectionBorder(panel->canvas,1,QStringLiteral("subplotSelectionBorder"));
    }else{
        auto* unavailable=new QLabel(QCoreApplication::translate("FigureManager",
            "Figure data is available. The plot renderer is disabled for this session."),panel->page);
        unavailable->setObjectName(QStringLiteral("figureCanvasPlaceholder"));
        unavailable->setAlignment(Qt::AlignCenter);unavailable->setWordWrap(true);layout->addWidget(unavailable,1);
        panel->selectionBorder=new FigureSelectionBorder(unavailable,1,QStringLiteral("subplotSelectionBorder"));
    }
    return panel;
}
} // namespace

struct FigureManager::Entry {
    FigureManager* owner=nullptr;
    FigureState state;
    QWidget* page=nullptr;
    FigureSelectionBorder* selectionBorder=nullptr;
    QLabel* heading=nullptr;
    QGridLayout* grid=nullptr;
    TileLayoutHost* tileHost=nullptr;
    TileLayoutSpec layoutSpec;
    QPointer<FloatingFigureWindow> floating;
    std::map<int,std::unique_ptr<FigurePanel>> panels;

    ~Entry() {
        if(floating){floating->closeRequested={};floating->activated={};
            if(floating->centralWidget()==page)floating->takeCentralWidget();}
        panels.clear();delete page;delete floating.data();
    }
    FigurePanel* active() const {
        const auto found=panels.find(state.activeSubplot);
        return found==panels.end()?nullptr:found->second.get();
    }
    void syncState() {
        if(const auto* panel=active())static_cast<SubplotState&>(state)=panel->state;
        updateHeading();
    }
    void updateHeading() {
        if(!owner||!heading)return;
        if(owner->m_tiled||state.rows*state.columns>1)heading->setText(owner->displayName(*this));
        else heading->setText(state.dimensions==0?owner->displayName(*this)+QStringLiteral(" — no plot")
            :owner->displayName(*this)+QStringLiteral(" — %1\n%2 vector(s), %3 curve(s).")
                .arg(contextText(state)).arg(state.vectors.size()).arg(state.curves.size()));
        heading->setVisible(state.docked&&!owner->m_grouped);
    }
    void relayout() {
        while(auto* item=grid->takeAt(0))delete item;
        std::vector<int> ids;for(const auto& [id,panel]:panels){(void)panel;ids.push_back(id);}
        if(layoutSpec.rows!=state.rows||layoutSpec.columns!=state.columns||!validateTileLayout(layoutSpec,ids))
            layoutSpec=regularTileLayout(state.rows,state.columns,ids,true);
        for(auto& [index,panel]:panels){
            const auto placement=std::find_if(layoutSpec.placements.begin(),layoutSpec.placements.end(),[index](const auto& p){return p.id==index;});
            if(placement==layoutSpec.placements.end())continue;
            grid->addWidget(panel->page,placement->row,placement->column,placement->rowSpan,placement->columnSpan);
            panel->page->show();
        }
        tileHost->setTileWeights(layoutSpec.rows,layoutSpec.columns,layoutSpec.rowWeights,layoutSpec.columnWeights);
        updateHeading();
    }
    FigurePanel* selectPanel(int index) {
        if(!panels.contains(index)){
            auto panel=createPanel(page,owner->m_renderingEnabled,index);
            panel->activate=[this,index]{
                state.activeSubplot=index;owner->m_selectedNumber=state.number;syncState();owner->updateToolbar();
            };
            panel->saveRequested=[this,index]{owner->requestSubplotExport(state.number,index);};
            panel->viewChanged=[this,index]{owner->synchronizeLinkedAxes(state.number,index,true);};
            panel->stateChanged=[this]{syncState();};
            panel->toolbar->setProperty("figureNumber",state.number);panel->toolbar->setProperty("subplotIndex",index);
            panel->page->setProperty("figureNumber",state.number);panel->page->setProperty("subplotIndex",index);
            if(panel->canvas){panel->canvas->activated=panel->activate;panel->canvas->setProperty("figureNumber",state.number);}
            panels.emplace(index,std::move(panel));
            relayout();
        }
        state.activeSubplot=index;syncState();return active();
    }
    void clear() {
        panels.clear();state.rows=state.columns=state.activeSubplot=1;state.linkedPanels.clear();state.linkedAxesMask=0;state.linkedCameras.clear();
        layoutSpec={};
        selectPanel(1);relayout();rebuild();
    }
    bool hasPlot() const {
        return std::any_of(panels.begin(),panels.end(),[](const auto& p){return p.second->state.dimensions!=0;});
    }
    void setMinimumCanvasSize(int width,int height) {
        for(const auto& [index,panel]:panels)if(panel->canvas)
            panel->canvas->setMinimumSize(state.columns==1?width:1,state.rows==1?height:1);
    }
    void resetView() {if(auto* panel=active())panel->resetView();}
    void rebuild(bool resetCamera=true) {if(auto* panel=active())panel->rebuild(resetCamera);syncState();}
};

FigureManager::FigureManager(QWidget* parent, bool renderingEnabled) : QWidget(parent) {
    qApp->installEventFilter(this);qApp->installNativeEventFilter(this);
    setObjectName(QStringLiteral("figureManager"));
    setBackgroundRole(QPalette::Window);
    setAutoFillBackground(true);
    m_renderingEnabled=renderingEnabled;
    auto* layout=new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    m_toolbar=new QToolBar(this);
    m_toolbar->setObjectName(QStringLiteral("figureToolbar"));
    auto* exportAction=m_toolbar->addAction(tr("Export Image…"));
    exportAction->setObjectName(QStringLiteral("exportFigureImage"));
    connect(exportAction,&QAction::triggered,this,&FigureManager::requestExport);
    auto* reset=m_toolbar->addAction(tr("Reset View"));
    reset->setObjectName(QStringLiteral("resetFigureView"));
    connect(reset,&QAction::triggered,this,[this]{if(auto* selected=entry(currentFigureNumber())) selected->resetView();});
    auto* closeAll=m_toolbar->addAction(tr("Close all"));closeAll->setObjectName(QStringLiteral("figuresCloseAll"));
    closeAll->setToolTip(tr("Close all figures, including floating figures"));connect(closeAll,&QAction::triggered,this,&FigureManager::closeAllFigures);
    auto* saveTemplate=m_toolbar->addAction(tr("Save Style…"));saveTemplate->setObjectName("savePlotTemplate");connect(saveTemplate,&QAction::triggered,this,[this]{const auto path=QFileDialog::getSaveFileName(this,tr("Save selected subplot style and labels"),{},tr("Plot template (*.json)"));if(path.isEmpty())return;QString message;if(!savePlotTemplate(currentFigureNumber(),0,path,QFileInfo::exists(path),&message))emit error(message);});
    auto* loadTemplate=m_toolbar->addAction(tr("Load Style…"));loadTemplate->setObjectName("loadPlotTemplate");connect(loadTemplate,&QAction::triggered,this,[this]{const auto path=QFileDialog::getOpenFileName(this,tr("Load subplot style and labels"),{},tr("Plot template (*.json)"));if(path.isEmpty())return;try{runtime::FigureAction action;action.kind=runtime::FigureActionKind::ApplyPlotTemplate;action.plotTemplate=std::make_shared<const plotting::PlotTemplate>(plotting::readPlotTemplate(std::filesystem::path(path.toStdWString())));QString message;if(!applyOne(action,&message))emit error(message);}catch(const std::exception& problem){emit error(QString::fromUtf8(problem.what()));}});
    auto* linkViews=m_toolbar->addAction(tr("Link3D Views"));linkViews->setObjectName("link3dCameras");connect(linkViews,&QAction::triggered,this,[this]{auto* selected=entry(currentFigureNumber());if(!selected)return;runtime::FigureAction action;action.kind=runtime::FigureActionKind::LinkCameras;action.figure=selected->state.number;if(selected->state.linkedCameras.empty())for(const auto& [index,panel]:selected->panels)if(panel->state.dimensions==3)action.linkedPanels.push_back(index);QString message;if(!applyOne(action,&message))emit error(message);});
    auto* undock=m_toolbar->addAction(tr("Undock"));undock->setObjectName(QStringLiteral("undockFigure"));
    connect(undock,&QAction::triggered,this,[this]{QString message;if(!undockFigure(currentFigureNumber(),&message))emit error(message);});
    auto* tabs=m_toolbar->addAction(tr("Tabs"));tabs->setObjectName(QStringLiteral("figureTabsLayout"));
    connect(tabs,&QAction::triggered,this,&FigureManager::showTabs);
    for(const bool desktop:{false,true}){
        auto* tile=new QToolButton(m_toolbar);
        tile->setObjectName(desktop?QStringLiteral("tileFiguresDesktopButton"):QStringLiteral("tileFiguresStudioButton"));
        auto* automatic=new QAction(desktop?tr("Tile on Desktop"):tr("Tile in Studio"),tile);
        automatic->setObjectName(desktop?QStringLiteral("tileFiguresDesktop"):QStringLiteral("tileFiguresStudio"));
        connect(automatic,&QAction::triggered,this,[this,desktop]{QString message;if(!(desktop?tileDesktop(0,0,&message):tileStudio(0,0,&message)))emit error(message);});
        tile->setDefaultAction(automatic);
        tile->setPopupMode(QToolButton::MenuButtonPopup);
        tile->setToolTip(tr("Tile figures automatically, or use the arrow to choose rows and columns."));
        tile->setMenu(createTileMenu(desktop,tile));
        m_toolbar->addWidget(tile);
    }
    auto* plotTiles=new QToolButton(m_toolbar);plotTiles->setText(tr("Tile plots"));
    plotTiles->setObjectName(QStringLiteral("tileSubplotsButton"));plotTiles->setPopupMode(QToolButton::InstantPopup);
    plotTiles->setToolTip(tr("Draw a layout for plots inside the selected figure"));
    plotTiles->setMenu(createTileMenu(false,plotTiles,-1));m_toolbar->addWidget(plotTiles);
    m_stack=new QStackedWidget(this);
    m_tabs=new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("figureTabs"));
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    connect(m_tabs,&QTabWidget::currentChanged,this,[this]{
        if(m_rearranging||m_destroying)return;
        for(const auto& figure:m_figures)if(figure->page==m_tabs->currentWidget())m_selectedNumber=figure->state.number;
        updateToolbar();
    });
    connect(m_tabs,&QTabWidget::tabCloseRequested,this,[this](int index){
        QWidget* page=m_tabs->widget(index);
        for(const auto& figure:m_figures) if(figure->page==page){closeFigure(figure->state.number);return;}
    });
    auto* tileHost=new TileLayoutHost(this);m_tileHost=tileHost;m_tileHost->setObjectName(QStringLiteral("figureTileHost"));
    m_grid=new QGridLayout(m_tileHost);m_grid->setContentsMargins(0,0,0,0);
    m_grid->setSizeConstraint(QLayout::SetNoConstraint);
    tileHost->setTileGrid(m_grid);
    tileHost->weightsChanged=[this](const auto& rows,const auto& columns){m_studioLayout.rowWeights=rows;m_studioLayout.columnWeights=columns;};
    m_stack->addWidget(m_tabs);m_stack->addWidget(m_tileHost);
    m_groups=new desktop::FigureGroupHost(this);m_groups->setObjectName(QStringLiteral("figureGroups"));m_stack->addWidget(m_groups);
    m_groups->selected=[this](int number){if(!m_rearranging&&!m_destroying&&entry(number)){m_selectedNumber=number;updateToolbar();}};
    m_groups->closeRequested=[this](int number){if(!m_destroying)closeFigure(number);};
    m_groups->dockedChanged=[this](int number,bool docked){if(m_destroying)return;if(auto* figure=entry(number)){figure->state.docked=docked;figure->updateHeading();updateToolbar();emit figuresChanged();}};
    auto* arrange=new QToolButton(m_toolbar);arrange->setObjectName(QStringLiteral("figureArrangeButton"));arrange->setText(tr("Arrange"));arrange->setPopupMode(QToolButton::InstantPopup);
    auto* arrangeMenu=new QMenu(arrange);connect(arrangeMenu,&QMenu::aboutToShow,this,[this,arrangeMenu]{populateArrangementMenu(arrangeMenu);});arrange->setMenu(arrangeMenu);m_toolbar->addWidget(arrange);
    layout->addWidget(m_toolbar);
    layout->addWidget(m_stack,1);
    updateToolbar();
}

FigureManager::~FigureManager() {
    qApp->removeEventFilter(this);qApp->removeNativeEventFilter(this);cancelFigureDrag();
    m_destroying=true;
    if(m_groups){m_groups->selected={};m_groups->closeRequested={};m_groups->dockedChanged={};}
    // Deleting tab pages emits currentChanged. Disconnect before the owned
    // entries begin destruction so those signals cannot inspect a dying vector.
    m_tabs->blockSignals(true);
    disconnect(m_tabs,nullptr,this,nullptr);
    m_figures.clear();
}

FigureManager::Entry* FigureManager::entry(int number) const {
    for(const auto& figure:m_figures) if(figure->state.number==number) return figure.get();
    return nullptr;
}

FigureManager::Entry* FigureManager::selectFigure(int number) {
    if(auto* existing=entry(number)){
        m_selectedNumber=number;
        if(m_grouped)m_groups->setCurrentFigure(number);
        else if(existing->floating){existing->floating->show();existing->floating->raise();existing->floating->activateWindow();}
        else if(!m_tiled)m_tabs->setCurrentWidget(existing->page);
        if(existing->active()&&existing->active()->canvas)existing->active()->canvas->setFocus();
        updateToolbar();return existing;
    }
    auto figure=std::make_unique<Entry>();
    figure->owner=this;
    figure->state.number=number;
    figure->page=new QWidget(m_tabs);
    figure->page->setObjectName(QStringLiteral("figurePage%1").arg(number));
    auto* layout=new QVBoxLayout(figure->page);
    figure->heading=new QLabel(figure->page);
    figure->heading->setProperty("studioFigureDragTitle",number);
    figure->heading->setTextFormat(Qt::PlainText);
    figure->heading->setWordWrap(true);
    layout->addWidget(figure->heading);
    auto* panelHost=new TileLayoutHost(figure->page);figure->tileHost=panelHost;
    figure->grid=new QGridLayout(panelHost);figure->grid->setContentsMargins(0,0,0,0);
    figure->grid->setSpacing(4);figure->grid->setSizeConstraint(QLayout::SetNoConstraint);
    panelHost->setTileGrid(figure->grid);panelHost->setObjectName(QStringLiteral("subplotTileHost"));
    panelHost->weightsChanged=[raw=figure.get()](const auto& rows,const auto& columns){raw->layoutSpec.rowWeights=rows;raw->layoutSpec.columnWeights=columns;};
    layout->addWidget(panelHost,1);
    figure->selectionBorder=new FigureSelectionBorder(figure->page,2,QStringLiteral("figureSelectionBorder"));
    figure->selectPanel(1);
    auto* selected=figure.get();
    m_figures.push_back(std::move(figure));
    m_selectedNumber=number;
    if(m_grouped||m_tiled)rebuildInternalLayout();
    else{m_tabs->addTab(selected->page,displayName(*selected));m_tabs->setCurrentWidget(selected->page);}
    selected->rebuild();
    return selected;
}

int FigureManager::nextAvailableFigure() const {
    for(int number=1;number<=maximumFigures;++number) if(!entry(number)) return number;
    return 0;
}

int FigureManager::figureCount() const {return static_cast<int>(m_figures.size());}
int FigureManager::currentFigureNumber() const {
    if(entry(m_selectedNumber))return m_selectedNumber;
    if(!m_tabs)return 0;
    QWidget* selected=m_tabs->currentWidget();
    for(const auto& figure:m_figures) if(figure->page==selected) return figure->state.number;
    return 0;
}
bool FigureManager::containsFigure(int number) const {return entry(number)!=nullptr;}
std::optional<FigureState> FigureManager::figureState(int number) const {
    if(const auto* figure=entry(number)) {
        FigureState snapshot=figure->state;
        for(const auto& [index,panel]:figure->panels)snapshot.subplots.push_back(panel->state);
        return snapshot;
    }
    return std::nullopt;
}

bool FigureManager::setPlotTool(int figure,int panel,PlotTool tool,QString* error) {
    if(error)error->clear();
    const auto* item=entry(figure);
    if(!item||!item->panels.contains(panel))return fail(error,tr("The requested figure/subplot is not open."));
    if(tool<PlotTool::Navigate||tool>PlotTool::GraphDrag)return fail(error,tr("Unknown plot tool."));
    auto* target=item->panels.at(panel).get();
    if((tool==PlotTool::GraphSelect||tool==PlotTool::GraphDrag)&&(!target->state.graph||!target->state.graph->interactive))return fail(error,tr("Graph selection and dragging require a bounded interactive graph view."));
    if(tool==PlotTool::Rotate&&target->state.dimensions!=3)return fail(error,tr("Rotation requires a three-dimensional plot."));
    target->setTool(tool);return true;
}

bool FigureManager::zoomPlot(int figure,int panel,double factor,QString* error) {
    if(error)error->clear();
    if(!std::isfinite(factor)||factor<=0)return fail(error,tr("The zoom factor must be finite and positive."));
    const auto* item=entry(figure);
    if(!item||!item->panels.contains(panel))return fail(error,tr("The requested figure/subplot is not open."));
    auto* target=item->panels.at(panel).get();
    if(!target->renderer||target->state.dimensions==0)return fail(error,tr("Plot navigation requires a native rendered plot."));
    target->zoom(factor);return true;
}

bool FigureManager::resetPlotView(int figure,int panel,QString* error) {
    if(error)error->clear();
    const auto* item=entry(figure);
    if(!item||!item->panels.contains(panel))return fail(error,tr("The requested figure/subplot is not open."));
    auto* target=item->panels.at(panel).get();
    if(!target->renderer||target->state.dimensions==0)return fail(error,tr("Plot navigation requires a native rendered plot."));
    target->resetView();return true;
}

bool FigureManager::setDataPointsVisible(int figure,int panel,bool visible,QString* error) {
    if(error)error->clear();
    const auto* item=entry(figure);
    if(!item||!item->panels.contains(panel))return fail(error,tr("The requested figure/subplot is not open."));
    auto* target=item->panels.at(panel).get();target->pointsVisible=visible;
    if(target->pointsAction){const QSignalBlocker blocked(target->pointsAction);target->pointsAction->setChecked(visible);}
    for(const auto& actor:target->pointActors)actor->SetVisibility(visible);
    target->render();return true;
}

std::optional<PlotDataPoint> FigureManager::pickPlotPoint(int figure,int panel,QPointF point,double tolerance) {
    const auto* item=entry(figure);
    if(!item||!item->panels.contains(panel))return std::nullopt;
    return item->panels.at(panel)->pick(point,tolerance);
}

std::optional<PlotDataPoint> FigureManager::selectedDataPoint(int figure,int panel) const {
    const auto* item=entry(figure);
    if(!item||!item->panels.contains(panel))return std::nullopt;
    return item->panels.at(panel)->selectedPoint;
}

std::vector<PlotDataPoint> FigureManager::dataTips(int figure,int panel) const {
    std::vector<PlotDataPoint> result;const auto* item=entry(figure);
    if(!item||!item->panels.contains(panel))return result;
    for(const auto& tip:item->panels.at(panel)->dataTips)result.push_back(tip.point);
    return result;
}

bool FigureManager::removeSelectedDataTip(int figure,int panel,QString* error) {
    if(error)error->clear();
    const auto* item=entry(figure);
    if(!item||!item->panels.contains(panel))return fail(error,tr("The requested figure/subplot is not open."));
    if(!item->panels.at(panel)->removeSelectedTip())return fail(error,tr("Select a data tip before removing it."));
    return true;
}

bool FigureManager::clearDataTips(int figure,int panel,QString* error) {
    if(error)error->clear();
    const auto* item=entry(figure);
    if(!item||!item->panels.contains(panel))return fail(error,tr("The requested figure/subplot is not open."));
    item->panels.at(panel)->clearAllTips();return true;
}

std::optional<PlotTool> FigureManager::plotTool(int figure,int panel) const {
    const auto* item=entry(figure);
    if(!item||!item->panels.contains(panel))return std::nullopt;
    return item->panels.at(panel)->tool;
}

bool FigureManager::dataPointsVisible(int figure,int panel) const {
    const auto* item=entry(figure);
    return item&&item->panels.contains(panel)&&item->panels.at(panel)->pointsVisible;
}

bool FigureManager::moveGraphNode(int figure,int panel,std::uint32_t node,const std::array<double,3>& position,QString* error){
    auto* item=entry(figure);if(!item||!item->panels.contains(panel))return fail(error,tr("The requested figure/subplot is not open."));
    auto* target=item->panels.at(panel).get();if(!target->state.graph||!target->state.graph->interactive)return fail(error,tr("This subplot is not an interactive graph."));
    if(node<1||node>target->state.graph->positions.size())return fail(error,tr("Unknown stable graph node ID."));
    try{auto graph=*target->state.graph;graph.positions[node-1]=position;graph.layout="manual";plotting::validateGraphView(graph);target->state.graph=std::move(graph);target->clearAllTips();target->rebuild(false,true);item->syncState();emit figuresChanged();return true;}catch(const plotting::ExportError& e){return fail(error,QString::fromUtf8(e.what()));}
}
bool FigureManager::selectGraphItems(int figure,int panel,const std::vector<std::uint32_t>& nodes,const std::vector<std::uint32_t>& edges,QString* error){
    auto* item=entry(figure);if(!item||!item->panels.contains(panel))return fail(error,tr("The requested figure/subplot is not open."));
    auto* target=item->panels.at(panel).get();if(!target->state.graph||!target->state.graph->interactive)return fail(error,tr("This subplot is not an interactive graph."));
    try{auto graph=*target->state.graph;graph.selectedNodes=nodes;graph.selectedEdges=edges;plotting::validateGraphView(graph);target->state.graph=std::move(graph);target->rebuild(false,true);item->syncState();emit figuresChanged();return true;}catch(const plotting::ExportError& e){return fail(error,QString::fromUtf8(e.what()));}
}
bool FigureManager::setGraphLayout(int figure,int panel,const QString& method,std::uint64_t seed,QString* error){
    auto* item=entry(figure);if(!item||!item->panels.contains(panel))return fail(error,tr("The requested figure/subplot is not open."));
    const bool okay=item->panels.at(panel)->changeGraphLayout(method.toStdString(),seed,error);if(okay){item->syncState();emit figuresChanged();}return okay;
}
bool FigureManager::saveGraphLayout(int figure,int panel,const QString& path,QString* error)const{
    auto* item=entry(figure);if(!item||!item->panels.contains(panel))return fail(error,tr("The requested figure/subplot is not open."));
    return item->panels.at(panel)->saveGraph(path,error);
}
bool FigureManager::loadGraphLayout(int figure,int panel,const QString& path,QString* error){
    auto* item=entry(figure);if(!item||!item->panels.contains(panel))return fail(error,tr("The requested figure/subplot is not open."));
    const bool okay=item->panels.at(panel)->loadGraph(path,error);if(okay){item->syncState();emit figuresChanged();}return okay;
}

QString FigureManager::displayName(const Entry& figure) const {
    if(!figure.state.numberTitle)return figure.state.name.isEmpty()?tr("Figure"):figure.state.name;
    const QString base=tr("Figure %1").arg(figure.state.number);
    return figure.state.name.isEmpty()?base:base+QStringLiteral(" — ")+figure.state.name;
}

void FigureManager::refreshNames() {
    for(const auto& figure:m_figures){
        if(m_grouped)m_groups->setFigureTitle(figure->state.number,displayName(*figure));
        figure->updateHeading();
        const int index=m_tabs->indexOf(figure->page);
        if(index>=0)m_tabs->setTabText(index,displayName(*figure));
        if(figure->floating)figure->floating->setWindowTitle(displayName(*figure));
    }
}

bool FigureManager::gridDimensions(int count,int& rows,int& columns,QString* error) const {
    if(rows==0&&columns==0){
        if(count==0)return true;
        columns=static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
        rows=(count+columns-1)/columns;return true;
    }
    if(rows<1||columns<1||rows>64||columns>64||rows*columns>64)
        return fail(error,tr("A tile layout requires positive rows/columns and at most 64 cells."));
    if(rows*columns<count)return fail(error,tr("The tile layout has fewer cells than open figures."));
    return true;
}

std::vector<int> FigureManager::tileItemIds(TileLayoutTarget target,int figure) const {
    std::vector<int> ids;
    if(target==TileLayoutTarget::Subplots){
        if(const auto* item=entry(figure?figure:currentFigureNumber()))
            for(const auto& [id,panel]:item->panels){(void)panel;ids.push_back(id);}
    }else for(const auto& item:m_figures)ids.push_back(item->state.number);
    std::sort(ids.begin(),ids.end());return ids;
}

TileLayoutSpec FigureManager::tileLayout(TileLayoutTarget target,int figure) const {
    if(target==TileLayoutTarget::Subplots){if(const auto* item=entry(figure?figure:currentFigureNumber()))return item->layoutSpec;return {};}
    const auto ids=tileItemIds(target);const auto& stored=target==TileLayoutTarget::Desktop?m_desktopLayout:m_studioLayout;
    if(validateTileLayout(stored,ids))return stored;
    int rows=0,columns=0;gridDimensions(static_cast<int>(ids.size()),rows,columns,nullptr);
    return regularTileLayout(std::max(1,rows),std::max(1,columns),ids);
}

bool FigureManager::tileSubplots(int figure,int rows,int columns,QString* error) {
    const int number=figure?figure:currentFigureNumber();const auto ids=tileItemIds(TileLayoutTarget::Subplots,number);
    if(ids.empty())return fail(error,tr("Select an open figure before arranging its plots."));
    if(!gridDimensions(static_cast<int>(ids.size()),rows,columns,error))return false;
    return applyTileLayout(TileLayoutTarget::Subplots,regularTileLayout(rows,columns,ids),number,error);
}

namespace {
std::map<int,QRect> desktopTileRectangles(const TileLayoutSpec& spec,const QRect& available){
    const auto rw=spec.rowWeights.empty()?std::vector<int>(static_cast<std::size_t>(spec.rows),1):spec.rowWeights;
    const auto cw=spec.columnWeights.empty()?std::vector<int>(static_cast<std::size_t>(spec.columns),1):spec.columnWeights;
    auto positions=[](const auto& weights,int origin,int extent){std::vector<int> p{origin};const auto total=std::accumulate(weights.begin(),weights.end(),qint64{0});qint64 sum=0;for(const auto w:weights){sum+=w;p.push_back(origin+static_cast<int>(sum*extent/total));}return p;};
    const auto x=positions(cw,available.left(),available.width()),y=positions(rw,available.top(),available.height());
    std::map<int,QRect> result;
    for(const auto& p:spec.placements)result[p.id]=QRect(x[static_cast<std::size_t>(p.column)],y[static_cast<std::size_t>(p.row)],x[static_cast<std::size_t>(p.column+p.columnSpan)]-x[static_cast<std::size_t>(p.column)],y[static_cast<std::size_t>(p.row+p.rowSpan)]-y[static_cast<std::size_t>(p.row)]);
    return result;
}
}

bool FigureManager::applyTileLayout(TileLayoutTarget target,const TileLayoutSpec& proposed,int figure,QString* error) {
    if(error)error->clear();
    const int number=figure?figure:currentFigureNumber();const auto ids=tileItemIds(target,number);
    if(ids.empty())return fail(error,tr("Open the requested figures/plots before applying a layout."));
    if(!validateTileLayout(proposed,ids,error))return false;
    if(target==TileLayoutTarget::Subplots){
        auto* item=entry(number);item->layoutSpec=proposed;item->state.rows=proposed.rows;item->state.columns=proposed.columns;
        item->relayout();item->syncState();emit figuresChanged();return true;
    }
    if(target==TileLayoutTarget::Studio){
        if(!tileStudio(proposed.rows,proposed.columns,error))return false;
        m_studioLayout=proposed;rebuildInternalLayout();emit figuresChanged();return true;
    }
    const auto* selected=entry(currentFigureNumber());
    QScreen* display=selected&&selected->floating?selected->floating->screen():screen();
    if(!display)display=QGuiApplication::primaryScreen();
    if(!display)return fail(error,tr("No desktop display is available for tiling."));
    const auto rectangles=desktopTileRectangles(proposed,display->availableGeometry());
    for(const auto& [id,rectangle]:rectangles){(void)id;if(rectangle.width()<120||rectangle.height()<100)return fail(error,tr("A saved/spanned desktop tile is too small for its figure window."));}
    if(!tileDesktop(proposed.rows,proposed.columns,error))return false;
    for(const auto& [id,rectangle]:rectangles){
        auto* item=entry(id);const auto margins=item->floating->windowHandle()?item->floating->windowHandle()->frameMargins():QMargins();
        item->floating->resize(std::max(1,rectangle.width()-margins.left()-margins.right()),std::max(1,rectangle.height()-margins.top()-margins.bottom()));
        if(item->floating->windowHandle())item->floating->windowHandle()->setFramePosition(rectangle.topLeft());else item->floating->move(rectangle.topLeft());
    }
    m_desktopLayout=proposed;emit figuresChanged();return true;
}

bool FigureManager::saveTileLayout(TileLayoutTarget target,const QString& path,int figure,QString* error) const {
    if(error)error->clear();
    const int number=figure?figure:currentFigureNumber();const auto ids=tileItemIds(target,number);
    if(ids.empty())return fail(error,tr("There is no open layout to save."));
    const auto spec=tileLayout(target,number);if(!validateTileLayout(spec,ids,error))return false;
    const QString kind=target==TileLayoutTarget::Studio?QStringLiteral("studio"):target==TileLayoutTarget::Desktop?QStringLiteral("desktop"):QStringLiteral("subplots");
    const QJsonObject document{{"format",QStringLiteral("pentarnion.tile-layout")},{"schema_version",1},{"target",kind},{"figure",target==TileLayoutTarget::Subplots?number:0},{"layout",tileLayoutJson(spec)}};
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return fail(error,tr("Cannot save layout: %1").arg(file.errorString()));
    const auto bytes=QJsonDocument(document).toJson();if(file.write(bytes)!=bytes.size()||!file.commit())return fail(error,tr("Cannot atomically write the selected layout file."));return true;
}

bool FigureManager::loadTileLayout(const QString& path,QString* error) {
    if(error)error->clear();
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)||file.size()>65536)return fail(error,tr("Cannot read layout, or layout exceeds 64 KiB."));
    QJsonParseError parseError;const auto doc=QJsonDocument::fromJson(file.readAll(),&parseError);
    if(parseError.error!=QJsonParseError::NoError||!doc.isObject())return fail(error,tr("Malformed tile layout JSON."));
    const auto object=doc.object();const auto version=object.value("schema_version"),figure=object.value("figure");
    if(object.size()!=5||object.value("format").toString()!=QStringLiteral("pentarnion.tile-layout")||!version.isDouble()||version.toDouble()!=1||!figure.isDouble()||figure.toDouble()!=std::floor(figure.toDouble())||figure.toDouble()<0||figure.toDouble()>64||!object.value("layout").isObject())return fail(error,tr("Unsupported tile layout schema or target metadata."));
    const auto kind=object.value("target").toString();TileLayoutTarget target;
    if(kind==QStringLiteral("studio"))target=TileLayoutTarget::Studio;else if(kind==QStringLiteral("desktop"))target=TileLayoutTarget::Desktop;else if(kind==QStringLiteral("subplots"))target=TileLayoutTarget::Subplots;else return fail(error,tr("Unknown saved layout target."));
    if((target==TileLayoutTarget::Subplots)==(figure.toInt()==0))return fail(error,tr("Saved subplot layouts require a figure ID; figure layouts use ID zero."));
    auto spec=parseTileLayoutJson(object.value("layout").toObject(),error);if(!spec)return false;
    return applyTileLayout(target,*spec,figure.toInt(),error);
}

void FigureManager::requestSavedTileLayout(TileLayoutTarget target,int figure,bool load) {
    const QString path=load?QFileDialog::getOpenFileName(window(),tr("Restore tile layout"),{},tr("Pentarnion layout (*.pentalayout.json);;JSON (*.json)"))
        :QFileDialog::getSaveFileName(window(),tr("Save tile layout"),QStringLiteral("layout.pentalayout.json"),tr("Pentarnion layout (*.pentalayout.json);;JSON (*.json)"));
    if(path.isEmpty())return;
    QString message;
    if(!(load?loadTileLayout(path,&message):saveTileLayout(target,path,figure,&message)))emit error(message);
}

void FigureManager::requestSpanLayout(TileLayoutTarget target,int figure) {
    const int number=figure?figure:currentFigureNumber();const auto ids=tileItemIds(target,number);if(ids.empty())return;
    auto spec=tileLayout(target,number);
    {
        QDialog dialog(window());dialog.setObjectName(QStringLiteral("drawTileLayoutDialog"));
        dialog.setWindowTitle(target==TileLayoutTarget::Subplots?tr("Draw subplot layout"):target==TileLayoutTarget::Desktop?tr("Draw Desktop figure layout"):tr("Draw Studio figure layout"));
        auto* layout=new QVBoxLayout(&dialog);
        auto* instructions=new QLabel(tr("Choose an item, then drag its rectangle into free grid cells. Drag dividers after applying to resize tiles."),&dialog);instructions->setWordWrap(true);layout->addWidget(instructions);
        auto* controls=new QHBoxLayout;
        auto* rows=new QSpinBox(&dialog);rows->setObjectName(QStringLiteral("spanLayoutRows"));rows->setRange(1,64);rows->setValue(spec.rows);
        auto* columns=new QSpinBox(&dialog);columns->setObjectName(QStringLiteral("spanLayoutColumns"));columns->setRange(1,64);columns->setValue(spec.columns);
        auto* selected=new QComboBox(&dialog);selected->setObjectName(QStringLiteral("spanLayoutItem"));
        for(const int id:ids)selected->addItem(target==TileLayoutTarget::Subplots?tr("Plot %1").arg(id):tr("Figure %1").arg(id),id);
        auto* reset=new QPushButton(tr("Reset positions"),&dialog);reset->setObjectName(QStringLiteral("resetTilePositions"));
        controls->addWidget(new QLabel(tr("Rows:"),&dialog));controls->addWidget(rows);controls->addWidget(new QLabel(tr("Columns:"),&dialog));controls->addWidget(columns);controls->addWidget(selected);controls->addWidget(reset);layout->addLayout(controls);
        auto* editor=new TileSpanEditor(&dialog);editor->setSpec(spec);editor->setSelectedId(ids.front());layout->addWidget(editor,1);
        auto* feedback=new QLabel(&dialog);feedback->setObjectName(QStringLiteral("tileSpanFeedback"));feedback->setWordWrap(true);layout->addWidget(feedback);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply layout"));layout->addWidget(buttons);
        auto validate=[&,target,number]{QString reason;const bool valid=validateTileLayout(editor->spec(),tileItemIds(target,number),&reason);feedback->setText(valid?tr("Layout valid. Existing plots and camera settings are preserved."):reason);buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);};
        auto dimensions=[&]{auto updated=editor->spec();updated.rows=rows->value();updated.columns=columns->value();updated.rowWeights.assign(static_cast<std::size_t>(updated.rows),1);updated.columnWeights.assign(static_cast<std::size_t>(updated.columns),1);editor->setSpec(updated);validate();};
        connect(rows,&QSpinBox::valueChanged,&dialog,dimensions);connect(columns,&QSpinBox::valueChanged,&dialog,dimensions);
        connect(selected,&QComboBox::currentIndexChanged,&dialog,[=](int){editor->setSelectedId(selected->currentData().toInt());});
        connect(reset,&QPushButton::clicked,&dialog,[&]{if(rows->value()*columns->value()<=64&&rows->value()*columns->value()>=static_cast<int>(ids.size()))editor->setSpec(regularTileLayout(rows->value(),columns->value(),ids));validate();});
        connect(editor,&TileSpanEditor::layoutEdited,&dialog,validate);connect(editor,&TileSpanEditor::editRejected,feedback,&QLabel::setText);
        connect(this,&FigureManager::figuresChanged,&dialog,validate);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{if(buttons->button(QDialogButtonBox::Ok)->isEnabled())dialog.accept();});connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        validate();dialog.resize(620,500);if(dialog.exec()!=QDialog::Accepted)return;spec=editor->spec();
    }
    QString message;if(!applyTileLayout(target,spec,number,&message))emit error(message);
}

QMenu* FigureManager::createTileMenu(bool desktop,QWidget* parent,int subplotFigure) {
    auto* menu=new QMenu(parent);
    const auto target=subplotFigure?TileLayoutTarget::Subplots:(desktop?TileLayoutTarget::Desktop:TileLayoutTarget::Studio);
    const QString prefix=subplotFigure?QStringLiteral("tileSubplots"):(desktop?QStringLiteral("tileDesktop"):QStringLiteral("tileStudio"));
    menu->setObjectName(prefix+QStringLiteral("Menu"));
    auto figureId=[this,subplotFigure]{return subplotFigure<0?currentFigureNumber():subplotFigure;};
    auto applyGrid=[this,target,figureId](int rows,int columns){
        QString message;const int figure=figureId();
        const bool ok=target==TileLayoutTarget::Subplots?tileSubplots(figure,rows,columns,&message)
            :target==TileLayoutTarget::Desktop?tileDesktop(rows,columns,&message):tileStudio(rows,columns,&message);
        if(!ok)emit error(message);
    };
    auto* drawn=new QWidgetAction(menu);drawn->setObjectName(prefix+QStringLiteral("DrawGrid"));
    auto* picker=new TileGridPicker(menu);drawn->setDefaultWidget(picker);menu->addAction(drawn);
    connect(picker,&TileGridPicker::layoutChosen,this,[this,menu,target,figureId](int rows,int columns){
        const int figure=figureId();menu->close();
        QTimer::singleShot(0,this,[this,target,figure,rows,columns]{
            QString message;
            const bool ok=target==TileLayoutTarget::Subplots?tileSubplots(figure,rows,columns,&message)
                :target==TileLayoutTarget::Desktop?tileDesktop(rows,columns,&message):tileStudio(rows,columns,&message);
            if(!ok)emit error(message);
        });
    });
    menu->addSeparator();
    auto* automatic=menu->addAction(tr("Automatic"));
    automatic->setObjectName(prefix+QStringLiteral("Automatic"));
    connect(automatic,&QAction::triggered,this,[applyGrid]{applyGrid(0,0);});
    menu->addSeparator();
    for(const auto& dimensions:std::array<std::array<int,2>,8>{{{1,1},{1,2},{2,1},{2,2},{2,3},{3,2},{3,3},{4,4}}}){
        const int rows=dimensions[0],columns=dimensions[1];
        auto* action=menu->addAction(tr("%1 × %2").arg(rows).arg(columns));
        action->setObjectName(prefix+QStringLiteral("Layout%1x%2").arg(rows).arg(columns));
        action->setProperty("tileRows",rows);action->setProperty("tileColumns",columns);
        connect(action,&QAction::triggered,this,[applyGrid,rows,columns]{applyGrid(rows,columns);});
    }
    menu->addSeparator();
    auto* custom=menu->addAction(tr("Custom rows and columns…"));
    custom->setObjectName(prefix+QStringLiteral("Custom"));
    connect(custom,&QAction::triggered,this,[this,desktop,target,figureId]{
        if(target==TileLayoutTarget::Subplots)requestSpanLayout(target,figureId());else requestTileLayout(desktop);
    });
    auto* spans=menu->addAction(tr("Draw spans / arrange items…"));
    spans->setObjectName(prefix+QStringLiteral("Spans"));
    connect(spans,&QAction::triggered,this,[this,target,figureId]{requestSpanLayout(target,figureId());});
    auto* save=menu->addAction(tr("Save layout…"));save->setObjectName(prefix+QStringLiteral("Save"));
    connect(save,&QAction::triggered,this,[this,target,figureId]{requestSavedTileLayout(target,figureId(),false);});
    auto* load=menu->addAction(tr("Restore layout…"));load->setObjectName(prefix+QStringLiteral("Load"));
    connect(load,&QAction::triggered,this,[this,target,figureId]{requestSavedTileLayout(target,figureId(),true);});
    auto refresh=[this,menu,picker,target,figureId,load]{
        const int count=static_cast<int>(tileItemIds(target,figureId()).size());picker->setRequiredCount(count);
        for(auto* action:menu->actions()){
            if(action->isSeparator())continue;
            const int rows=action->property("tileRows").toInt(),columns=action->property("tileColumns").toInt();
            action->setEnabled(action==load||(count>0&&(rows==0||rows*columns>=count)));
        }
    };
    connect(menu,&QMenu::aboutToShow,this,refresh);connect(this,&FigureManager::figuresChanged,menu,refresh);
    refresh();return menu;
}

void FigureManager::requestTileLayout(bool desktop) {
    if(m_figures.empty())return;
    int rows=desktop?m_desktopRows:m_rows,columns=desktop?m_desktopColumns:m_columns;
    if(!gridDimensions(figureCount(),rows,columns,nullptr))rows=columns=0;
    gridDimensions(figureCount(),rows,columns,nullptr);
    {
        // Own the dialog through Studio, not an individual floating figure: live
        // figure updates may close/recreate those windows during this event loop.
        // Destroy it before changing the layout or docking floating figures.
        QDialog dialog(window());
        dialog.setObjectName(QStringLiteral("figureTileLayoutDialog"));
        dialog.setWindowTitle(desktop?tr("Tile figures on Desktop"):tr("Tile figures in Studio"));
        auto* layout=new QVBoxLayout(&dialog);
        auto* description=new QLabel(&dialog);layout->addWidget(description);
        auto* form=new QFormLayout;
        auto* rowInput=new QSpinBox(&dialog);rowInput->setObjectName(QStringLiteral("tileLayoutRows"));rowInput->setRange(1,64);rowInput->setValue(std::max(1,rows));
        auto* columnInput=new QSpinBox(&dialog);columnInput->setObjectName(QStringLiteral("tileLayoutColumns"));columnInput->setRange(1,64);columnInput->setValue(std::max(1,columns));
        form->addRow(tr("Rows:"),rowInput);form->addRow(tr("Columns:"),columnInput);layout->addLayout(form);
        auto* validation=new QLabel(&dialog);validation->setObjectName(QStringLiteral("tileLayoutValidation"));validation->setWordWrap(true);layout->addWidget(validation);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
        buttons->setObjectName(QStringLiteral("tileLayoutButtons"));
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Tile"));layout->addWidget(buttons);
        auto validate=[this,desktop,description,rowInput,columnInput,validation,buttons]{
            const int count=figureCount();description->setText(tr("Choose a grid for %1 open figure(s).").arg(count));
            int candidateRows=rowInput->value(),candidateColumns=columnInput->value();QString reason;
            bool valid=count>0&&gridDimensions(count,candidateRows,candidateColumns,&reason);
            if(count==0)reason=tr("Open a figure before choosing a layout.");
            if(valid&&desktop){
                const auto* selected=entry(currentFigureNumber());
                QScreen* display=selected&&selected->floating?selected->floating->screen():screen();
                if(!display)display=QGuiApplication::primaryScreen();
                if(!display){valid=false;reason=tr("No desktop display is available for tiling.");}
                else if(display->availableGeometry().width()/candidateColumns<120||display->availableGeometry().height()/candidateRows<100){
                    valid=false;reason=tr("The requested desktop cells are too small for figure windows on this monitor.");
                }
            }
            validation->setText(valid?tr("%1 cells; unused cells remain empty. Maximum: 64 cells.").arg(candidateRows*candidateColumns):reason);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
        };
        connect(rowInput,&QSpinBox::valueChanged,&dialog,validate);connect(columnInput,&QSpinBox::valueChanged,&dialog,validate);
        connect(this,&FigureManager::figuresChanged,&dialog,validate);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,[&dialog,buttons]{if(buttons->button(QDialogButtonBox::Ok)->isEnabled())dialog.accept();});
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        validate();
        if(dialog.exec()!=QDialog::Accepted)return;
        rows=rowInput->value();columns=columnInput->value();
    }
    QString message;
    if(!(desktop?tileDesktop(rows,columns,&message):tileStudio(rows,columns,&message)))emit error(message);
}

void FigureManager::rebuildInternalLayout() {
    if(m_rearranging||m_destroying)return;
    if(m_grouped){
        m_rearranging=true;
        for(const auto& figure:m_figures){if(!m_groups->containsFigure(figure->state.number))m_groups->addFigure(figure->state.number,displayName(*figure),figure->page);figure->setMinimumCanvasSize(1,1);figure->updateHeading();}
        m_stack->setCurrentWidget(m_figures.empty()?static_cast<QWidget*>(m_tabs):static_cast<QWidget*>(m_groups));m_rearranging=false;updateToolbar();return;
    }
    m_rearranging=true;
    const QSignalBlocker blocked(m_tabs);
    while(m_tabs->count())m_tabs->removeTab(0);
    while(auto* item=m_grid->takeAt(0))delete item;
    std::vector<Entry*> docked;
    for(const auto& figure:m_figures)if(figure->state.docked)docked.push_back(figure.get());
    std::sort(docked.begin(),docked.end(),[](const Entry* a,const Entry* b){return a->state.number<b->state.number;});
    int rows=m_rows,columns=m_columns;
    if(m_tiled&&!gridDimensions(static_cast<int>(docked.size()),rows,columns,nullptr)){
        rows=columns=0;gridDimensions(static_cast<int>(docked.size()),rows,columns,nullptr);
    }
    std::vector<int> ids;for(const auto* figure:docked)ids.push_back(figure->state.number);
    if(m_tiled){
        auto proposed=m_studioLayout;
        if(proposed.rows!=rows||proposed.columns!=columns)proposed=regularTileLayout(rows,columns,ids);
        else{
            std::erase_if(proposed.placements,[&](const auto& p){return std::find(ids.begin(),ids.end(),p.id)==ids.end();});
            for(const int id:ids)if(std::none_of(proposed.placements.begin(),proposed.placements.end(),[id](const auto& p){return p.id==id;})){
                bool placed=false;
                for(int cell=0;cell<rows*columns&&!placed;++cell){const int r=cell/columns,c=cell%columns;const bool occupied=std::any_of(proposed.placements.begin(),proposed.placements.end(),[r,c](const auto& p){return r>=p.row&&r<p.row+p.rowSpan&&c>=p.column&&c<p.column+p.columnSpan;});if(!occupied){proposed.placements.push_back({id,r,c,1,1});placed=true;}}
                if(!placed){proposed=regularTileLayout(rows,columns,ids);break;}
            }
        }
        if(!validateTileLayout(proposed,ids))proposed=regularTileLayout(rows,columns,ids);
        m_studioLayout=std::move(proposed);
    }
    for(std::size_t index=0;index<docked.size();++index){
        Entry* figure=docked[index];
        figure->setMinimumCanvasSize(m_tiled?1:320,m_tiled?1:240);
        figure->updateHeading();
        if(m_tiled){
            const auto p=std::find_if(m_studioLayout.placements.begin(),m_studioLayout.placements.end(),[figure](const auto& placement){return placement.id==figure->state.number;});
            if(p!=m_studioLayout.placements.end())m_grid->addWidget(figure->page,p->row,p->column,p->rowSpan,p->columnSpan);
        }
        else m_tabs->addTab(figure->page,displayName(*figure));
        figure->page->show();
    }
    static_cast<TileLayoutHost*>(m_tileHost)->setTileWeights(m_tiled?rows:0,m_tiled?columns:0,m_tiled?m_studioLayout.rowWeights:std::vector<int>{},m_tiled?m_studioLayout.columnWeights:std::vector<int>{});
    m_rows=rows;m_columns=columns;
    m_stack->setCurrentWidget(m_tiled?m_tileHost:m_tabs);
    if(!m_tiled)if(const auto* selected=entry(m_selectedNumber);selected&&selected->state.docked)
        m_tabs->setCurrentWidget(selected->page);
    m_rearranging=false;updateToolbar();
}

bool FigureManager::dockFigure(int number,QString* error) {
    if(m_grouped){if(!entry(number))return fail(error,tr("The requested figure is not open."));const bool ok=m_groups->floatFigure(number,false);if(ok){m_selectedNumber=number;if(auto* figure=entry(number))figure->state.docked=true;updateToolbar();emit figuresChanged();}return ok;} 
    if(error)error->clear();
    Entry* figure=entry(number);
    if(!figure)return fail(error,tr("Figure %1 is not open.").arg(number));
    if(figure->state.docked){selectFigure(number);return true;}
    m_rearranging=true;
    if(figure->floating){
        figure->floating->closeRequested={};figure->floating->activated={};
        figure->floating->takeCentralWidget();
        figure->page->setParent(m_tiled?m_tileHost:m_tabs);
        delete figure->floating.data();
    }
    figure->state.docked=true;m_selectedNumber=number;
    m_rearranging=false;rebuildInternalLayout();refreshNames();emit figuresChanged();
    return true;
}

bool FigureManager::undockFigure(int number,QString* error) {
    if(m_grouped){if(!entry(number))return fail(error,tr("The requested figure is not open."));const bool ok=m_groups->floatFigure(number,true);if(ok){m_selectedNumber=number;if(auto* figure=entry(number))figure->state.docked=false;updateToolbar();emit figuresChanged();}return ok;} 
    if(error)error->clear();
    Entry* figure=entry(number);
    if(!figure)return fail(error,tr("Figure %1 is not open.").arg(number));
    if(!figure->state.docked){selectFigure(number);return true;}
    m_rearranging=true;
    const int tab=m_tabs->indexOf(figure->page);
    if(tab>=0)m_tabs->removeTab(tab);
    m_grid->removeWidget(figure->page);
    auto* window=new FloatingFigureWindow(this);figure->floating=window;
    desktop::normalizeIndependentFigureWindow(window);
    window->setObjectName(QStringLiteral("floatingFigure%1").arg(number));
    window->setCentralWidget(figure->page);
    auto* toolbar=window->addToolBar(tr("Figure"));toolbar->setMovable(false);
    auto* dock=toolbar->addAction(tr("Dock"));dock->setObjectName(QStringLiteral("dockFloatingFigure"));
    connect(dock,&QAction::triggered,this,[this,number]{QString message;if(!dockFigure(number,&message))emit this->error(message);});
    auto* tile=new QToolButton(toolbar);tile->setText(tr("Tile"));tile->setPopupMode(QToolButton::InstantPopup);
    tile->setObjectName(QStringLiteral("floatingFigureTileButton"));
    auto* menu=new QMenu(tile);
    auto* inStudio=createTileMenu(false,tile);inStudio->setTitle(tr("In Studio"));menu->addMenu(inStudio);
    auto* onDesktop=createTileMenu(true,tile);onDesktop->setTitle(tr("On Desktop"));menu->addMenu(onDesktop);
    auto* inFigure=createTileMenu(false,tile,number);inFigure->setTitle(tr("Plots in this figure"));menu->addMenu(inFigure);
    tile->setMenu(menu);toolbar->addWidget(tile);
    connect(toolbar->addAction(tr("Export")),&QAction::triggered,this,[this,number]{m_selectedNumber=number;requestExport();});
    connect(toolbar->addAction(tr("Reset")),&QAction::triggered,this,[this,number]{if(auto* item=entry(number))item->resetView();});
    if(auto* closeAll=findChild<QAction*>(QStringLiteral("figuresCloseAll")))toolbar->addAction(closeAll);
    const QPointer<QWidget> originalPage=figure->page;
    window->closeRequested=[this,number,originalPage]{QTimer::singleShot(0,this,[this,number,originalPage]{
        const auto* current=entry(number);
        if(!m_destroying&&originalPage&&current&&current->page==originalPage.data())closeFigure(number);
    });};
    window->activated=[this,number]{if(!m_rearranging&&!m_destroying){m_selectedNumber=number;updateToolbar();}};
    figure->state.docked=false;m_selectedNumber=number;figure->updateHeading();
    figure->setMinimumCanvasSize(200,150);
    window->setWindowTitle(displayName(*figure));window->resize(900,650);window->show();figure->page->show();
    m_rearranging=false;rebuildInternalLayout();updateToolbar();emit figuresChanged();
    return true;
}

bool FigureManager::tileStudio(int rows,int columns,QString* error) {
    if(error)error->clear();
    if(!gridDimensions(figureCount(),rows,columns,error))return false;
    setGroupedFigures(false);
    const int selected=m_selectedNumber;
    for(const auto& figure:m_figures)if(!figure->state.docked)
        if(!dockFigure(figure->state.number,error))return false;
    m_selectedNumber=selected;m_tiled=true;m_rows=rows;m_columns=columns;
    m_studioLayout=regularTileLayout(rows,columns,tileItemIds(TileLayoutTarget::Studio));
    rebuildInternalLayout();refreshNames();emit figuresChanged();return true;
}

bool FigureManager::tileDesktop(int rows,int columns,QString* error) {
    if(error)error->clear();
    if(!gridDimensions(figureCount(),rows,columns,error))return false;
    if(m_figures.empty())return true;
    setGroupedFigures(false);
    Entry* selected=entry(currentFigureNumber());
    QScreen* display=selected&&selected->floating?selected->floating->screen():screen();
    if(!display)display=QGuiApplication::primaryScreen();
    if(!display)return fail(error,tr("No desktop display is available for tiling."));
    const QRect available=display->availableGeometry();
    if(available.width()/columns<120||available.height()/rows<100)
        return fail(error,tr("The requested desktop cells are too small for figure windows on this monitor."));
    const int selectedNumber=m_selectedNumber;
    std::vector<Entry*> ordered;
    for(const auto& figure:m_figures)ordered.push_back(figure.get());
    std::sort(ordered.begin(),ordered.end(),[](const Entry* a,const Entry* b){return a->state.number<b->state.number;});
    for(std::size_t index=0;index<ordered.size();++index){
        Entry* figure=ordered[index];
        if(!undockFigure(figure->state.number,error))return false;
        figure->setMinimumCanvasSize(1,1);
        const int row=static_cast<int>(index)/columns,column=static_cast<int>(index)%columns;
        const int left=available.left()+column*available.width()/columns;
        const int top=available.top()+row*available.height()/rows;
        const int right=available.left()+(column+1)*available.width()/columns;
        const int bottom=available.top()+(row+1)*available.height()/rows;
        const QMargins margins=figure->floating->windowHandle()?figure->floating->windowHandle()->frameMargins():QMargins();
        figure->floating->resize(std::max(1,right-left-margins.left()-margins.right()),
                                  std::max(1,bottom-top-margins.top()-margins.bottom()));
        if(figure->floating->windowHandle())figure->floating->windowHandle()->setFramePosition({left,top});
        else figure->floating->move(left,top);
    }
    m_selectedNumber=selectedNumber;
    m_desktopRows=rows;m_desktopColumns=columns;
    m_desktopLayout=regularTileLayout(rows,columns,tileItemIds(TileLayoutTarget::Desktop));
    if(auto* current=entry(selectedNumber);current&&current->floating){current->floating->raise();current->floating->activateWindow();}
    updateToolbar();emit figuresChanged();return true;
}

void FigureManager::showTabs() {
    setGroupedFigures(false);
    m_tiled=false;m_rows=m_columns=0;rebuildInternalLayout();refreshNames();emit figuresChanged();
}

void FigureManager::closeFigure(int number) {
    if(number==m_dragFigure||number==m_dropFigure)cancelFigureDrag();
    for(auto iterator=m_figures.begin();iterator!=m_figures.end();++iterator)
        if((*iterator)->state.number==number){
            const bool rearranging=m_rearranging;m_rearranging=true;
            // Cancel visible image work before transferring/destroying its page.
            (*iterator)->page->hide();
            if(m_groups&&m_groups->containsFigure(number))m_groups->takeFigure(number);
            m_tabs->removeTab(m_tabs->indexOf((*iterator)->page));
            m_grid->removeWidget((*iterator)->page);
            m_figures.erase(iterator);
            if(m_selectedNumber==number)m_selectedNumber=m_figures.empty()?0:m_figures.back()->state.number;
            m_rearranging=rearranging;
            if(m_grouped){if(m_selectedNumber)m_groups->setCurrentFigure(m_selectedNumber);}
            else if(m_tiled)rebuildInternalLayout();
            // Keep the saved tile arrangement for subsequent figures, while an
            // empty pane shows no leftover splitters or stale images.
            if(m_figures.empty())m_stack->setCurrentWidget(m_tabs);
            QTimer::singleShot(0,this,[this]{
                if(m_destroying)return;
                update();
                if(auto* top=window())top->update();
            });
            updateToolbar(); emit figuresChanged(); return;
        }
}

void FigureManager::closeAllFigures(){
    cancelFigureDrag();while(!m_figures.empty())closeFigure(m_figures.back()->state.number);
}

bool FigureManager::savePlotTemplate(int figure,int panel,const QString& path,bool overwrite,QString* message)const{
    if(message)message->clear();
    const auto* selected=entry(figure);if(!selected)return fail(message,tr("Select an open figure before saving its style."));const auto item=selected->panels.find(panel?panel:selected->state.activeSubplot);if(item==selected->panels.end())return fail(message,tr("The requested subplot is not allocated."));const auto& p=item->second->state;
    try{plotting::PlotTemplate value{p.style,{p.title.toStdString(),p.xLabel.toStdString(),p.yLabel.toStdString(),p.zLabel.toStdString(),p.caption.toStdString()},{p.titleLatex,p.xLabelLatex,p.yLabelLatex,p.zLabelLatex,p.captionLatex}};plotting::writePlotTemplate(value,std::filesystem::path(path.toStdWString()),overwrite);return true;}catch(const std::exception& error){return fail(message,QString::fromUtf8(error.what()));}
}
void FigureManager::synchronizeLinkedAxes(int number,int sourceIndex,bool interactive){
    if(m_linkingAxes)return;
    auto* figure=entry(number);
    if(figure&&figure->panels.contains(sourceIndex)&&std::find(figure->state.linkedCameras.begin(),figure->state.linkedCameras.end(),sourceIndex)!=figure->state.linkedCameras.end()){
        auto* source=figure->panels.at(sourceIndex).get();if(source->renderer){m_linkingAxes=true;for(const auto index:figure->state.linkedCameras)if(index!=sourceIndex&&figure->panels.contains(index)){auto* target=figure->panels.at(index).get();if(target->renderer){target->renderer->GetActiveCamera()->DeepCopy(source->renderer->GetActiveCamera());target->render();}}m_linkingAxes=false;}
    }
    if(!figure||figure->state.linkedAxesMask==0||std::find(figure->state.linkedPanels.begin(),figure->state.linkedPanels.end(),sourceIndex)==figure->state.linkedPanels.end()||!figure->panels.contains(sourceIndex))return;
    auto* source=figure->panels.at(sourceIndex).get();if(interactive&&!source->renderer)return;
    m_linkingAxes=true;
    std::array<std::optional<runtime::AxisLimits>,2> common;
    const auto visible=interactive?source->visibleLimits():std::array<runtime::AxisLimits,2>{};
    for(std::size_t axis=0;axis<2;++axis)if(figure->state.linkedAxesMask&(1u<<axis)){
        if(interactive)common[axis]=visible[axis];else common[axis]=source->state.style.limits[axis];
        if(!common[axis]){double low=INFINITY,high=-INFINITY;
            for(const int index:figure->state.linkedPanels)if(figure->panels.contains(index)){const auto& panel=figure->panels.at(index)->state;
                for(const auto& curve:panel.curves)for(const auto& point:curve.points){low=std::min(low,point[axis]);high=std::max(high,point[axis]);}
                if(panel.mesh)for(const auto& point:panel.mesh->points){low=std::min(low,point[axis]);high=std::max(high,point[axis]);}
                for(const auto& vector:panel.vectors){const auto value=vector.coefficient(static_cast<std::size_t>(panel.axes[axis]));low=std::min({low,0.,value});high=std::max({high,0.,value});}
            }
            if(std::isfinite(low)&&std::isfinite(high)){if(low==high){const auto pad=std::max(1.,std::abs(low)*.05);low-=pad;high+=pad;}if(std::isfinite(low)&&std::isfinite(high)&&low<high)common[axis]=runtime::AxisLimits{low,high};}
        }
    }
    for(const int index:figure->state.linkedPanels)if(figure->panels.contains(index)){
        auto* target=figure->panels.at(index).get();const auto previous=interactive&&target->renderer?target->visibleLimits():std::array<runtime::AxisLimits,2>{};
        for(std::size_t axis=0;axis<2;++axis){if(common[axis])target->state.style.limits[axis]=common[axis];else if(interactive)target->state.style.limits[axis]=previous[axis];}
        target->rebuild();if(interactive&&target->renderer){target->renderer->GetActiveCamera()->SetParallelScale(1);target->render();}
    }
    figure->syncState();m_linkingAxes=false;
}

bool FigureManager::applyOne(const runtime::FigureAction& action,QString* error) {
    using runtime::FigureActionKind;
    std::size_t retainedPanels=0;
    for(const auto& figure:m_figures)retainedPanels+=figure->panels.size();
    auto ensureFigureCapacity=[&](int number){
        return entry(number)||retainedPanels<maximumRetainedPanels
            ||fail(error,tr("Open figures may contain at most 128 allocated subplot panels. Clear or close a figure first."));
    };
    if(action.figure<0||action.figure>maximumFigures)
        return fail(error,tr("Figure numbers must be in 1..64."));
    if(action.kind==FigureActionKind::CloseAll){
        while(!m_figures.empty())closeFigure(m_figures.back()->state.number);
        return true;
    }
    if(action.kind==FigureActionKind::Dock)return dockFigure(action.figure,error);
    if(action.kind==FigureActionKind::Undock)return undockFigure(action.figure,error);
    if(action.kind==FigureActionKind::TileStudio)return tileStudio(action.rows,action.columns,error);
    if(action.kind==FigureActionKind::TileDesktop)return tileDesktop(action.rows,action.columns,error);
    if(action.kind==FigureActionKind::Close){closeFigure(action.figure==0?currentFigureNumber():action.figure);return true;}
    if(action.kind==FigureActionKind::Clear){
        if(auto* selected=entry(action.figure==0?currentFigureNumber():action.figure)){
            selected->clear();
        }
        return true;
    }
    if(action.kind==FigureActionKind::SelectNew){
        const int number=nextAvailableFigure();
        if(number==0)return fail(error,tr("At most 64 figure tabs may be open."));
        if(!ensureFigureCapacity(number))return false;
        auto* selected=selectFigure(number);
        if(action.name)selected->state.name=QString::fromStdString(*action.name);
        if(action.numberTitle)selected->state.numberTitle=*action.numberTitle;
        refreshNames();return true;
    }
    if(action.kind==FigureActionKind::Select){
        if(action.figure==0)return fail(error,tr("An explicitly selected figure requires a number in 1..64."));
        if(!ensureFigureCapacity(action.figure))return false;
        auto* selected=selectFigure(action.figure);
        if(action.name)selected->state.name=QString::fromStdString(*action.name);
        if(action.numberTitle)selected->state.numberTitle=*action.numberTitle;
        refreshNames();return true;
    }
    if(action.kind==FigureActionKind::Subplot){
        if(action.rows<1||action.columns<1||action.rows>maximumPanels||action.columns>maximumPanels
            ||action.rows*action.columns>maximumPanels||action.subplotIndex<1
            ||action.subplotIndex>action.rows*action.columns)
            return fail(error,tr("Subplot requires positive rows/columns, at most 64 cells, and a one-based index within the grid."));
        int number=action.figure==0?currentFigureNumber():action.figure;
        if(number==0)number=1;
        const auto* current=entry(number);
        const std::size_t extraPanels=current?(current->panels.contains(action.subplotIndex)?0:1):(action.subplotIndex==1?1:2);
        if(extraPanels>maximumRetainedPanels-retainedPanels)
            return fail(error,tr("Open figures may contain at most 128 allocated subplot panels. Clear or close a figure first."));
        if(current){
            for(const auto& [index,panel]:current->panels)
                if(index>action.rows*action.columns&&(panel->state.dimensions!=0||!panel->state.title.isEmpty()
                    ||!panel->state.xLabel.isEmpty()||!panel->state.yLabel.isEmpty()||!panel->state.zLabel.isEmpty()
                    ||!panel->state.caption.isEmpty()))
                    return fail(error,tr("The smaller subplot grid would discard an existing panel. Clear the figure first."));
        }
        auto* selected=selectFigure(number);
        for(auto it=selected->panels.begin();it!=selected->panels.end();)
            if(it->first>action.rows*action.columns)it=selected->panels.erase(it);else ++it;
        selected->state.rows=action.rows;selected->state.columns=action.columns;selected->layoutSpec={};
        selected->selectPanel(action.subplotIndex);selected->relayout();return true;
    }
    if(action.kind==FigureActionKind::ExportPlot){
        const auto number=action.figure? action.figure:currentFigureNumber();
        return exportFigure(number,QString::fromStdString(action.text),action.exportOptions.value_or(runtime::FigureExportOptions{}),error);
    }
    if(action.kind==FigureActionKind::SetAnimation){
        if(!action.animation)return fail(error,tr("Missing immutable field animation data."));
        try{(void)plotting::fieldAnimationFrame(*action.animation,0);}catch(const std::exception& e){return fail(error,QString::fromUtf8(e.what()));}
        int number=action.figure? action.figure:currentFigureNumber();if(number==0)number=1;if(!ensureFigureCapacity(number))return false;
        std::size_t retained=action.animation->values.size();if(retained>8000000)return fail(error,tr("Animation payload exceeds its coordinate budget."));std::set<const plotting::FieldAnimationData*> seen{action.animation.get()};
        for(const auto& item:m_figures)for(const auto& [index,panel]:item->panels)if(!(item->state.number==number&&index==item->state.activeSubplot)&&panel->state.animation&&seen.insert(panel->state.animation.get()).second){if(panel->state.animation->values.size()>8000000-retained)return fail(error,tr("Open animations exceed eight million retained real coordinates."));retained+=panel->state.animation->values.size();}
        auto* selected=selectFigure(number);auto* panel=selected->active();panel->state.algebraId=QString::fromStdString(action.animation->algebraId);panel->state.gamma=action.animation->gamma;panel->state.basisLabels.clear();panel->assignAnimation(action.animation);selected->syncState();return true;
    }
    if(action.kind==FigureActionKind::SelectAnimationFrame||action.kind==FigureActionKind::SetAnimationPlaying){
        auto* selected=entry(action.figure? action.figure:currentFigureNumber());if(!selected||!selected->active()||!selected->active()->state.animation)return fail(error,tr("The selected subplot has no saved field animation."));auto* panel=selected->active();
        if(action.kind==FigureActionKind::SelectAnimationFrame){panel->setAnimationPlaying(false);if(!panel->showAnimationFrame(action.animationFrame))return fail(error,tr("Frame number is outside the saved frame selection."));}
        else panel->setAnimationPlaying(action.animationPlaying);
        selected->syncState();return true;
    }
    if(action.kind==FigureActionKind::SavePlotTemplate)return savePlotTemplate(action.figure?action.figure:currentFigureNumber(),0,QString::fromStdString(action.text),action.exportOptions&&action.exportOptions->overwrite,error);
    if(action.kind==FigureActionKind::ApplyPlotTemplate){
        if(!action.plotTemplate)return fail(error,tr("Missing plot template."));
        int number=action.figure?action.figure:currentFigureNumber();if(!number)number=1;if(!ensureFigureCapacity(number))return false;try{plotting::validatePlotTemplate(*action.plotTemplate);}catch(const std::exception& problem){return fail(error,QString::fromUtf8(problem.what()));}
        std::array<QImage,5> images;for(std::size_t i=0;i<5;++i)if(!action.plotTemplate->labels[i].empty()){images[i]=renderMathText(QString::fromStdString(action.plotTemplate->labels[i]),publicationFont(action.plotTemplate->style,static_cast<int>(i)),Qt::black,action.plotTemplate->latex[i],error);if(images[i].isNull())return false;}
        auto* selected=selectFigure(number);auto* panel=selected->active();auto& p=panel->state;p.style=action.plotTemplate->style;p.title=QString::fromStdString(action.plotTemplate->labels[0]);p.xLabel=QString::fromStdString(action.plotTemplate->labels[1]);p.yLabel=QString::fromStdString(action.plotTemplate->labels[2]);p.zLabel=QString::fromStdString(action.plotTemplate->labels[3]);p.caption=QString::fromStdString(action.plotTemplate->labels[4]);p.titleLatex=action.plotTemplate->latex[0];p.xLabelLatex=action.plotTemplate->latex[1];p.yLabelLatex=action.plotTemplate->latex[2];p.zLabelLatex=action.plotTemplate->latex[3];p.captionLatex=action.plotTemplate->latex[4];panel->labelImages=std::move(images);selected->rebuild();return true;
    }
    if(action.kind==FigureActionKind::LinkCameras){
        auto* selected=entry(action.figure?action.figure:currentFigureNumber());if(!selected)return fail(error,tr("Select an open figure before linking3D views."));std::set<int> unique;if(!action.linkedPanels.empty()&&action.linkedPanels.size()<2)return fail(error,tr("Link at least two3D subplots."));for(const auto index:action.linkedPanels)if(!unique.insert(index).second||!selected->panels.contains(index)||selected->panels.at(index)->state.dimensions!=3)return fail(error,tr("Camera links require unique allocated3D subplots."));selected->state.linkedCameras=action.linkedPanels;if(!action.linkedPanels.empty())synchronizeLinkedAxes(selected->state.number,action.linkedPanels.front(),true);return true;
    }
    if(action.kind==FigureActionKind::LinkAxes){
        auto* figure=entry(action.figure? action.figure:currentFigureNumber());if(!figure)return fail(error,tr("Select a figure with allocated 2D subplots before linking axes."));
        if(action.linkedAxesMask==0){figure->state.linkedPanels.clear();figure->state.linkedAxesMask=0;return true;}
        if(action.linkedAxesMask>3||action.linkedPanels.size()<2)return fail(error,tr("Linked navigation currently supports x/y coordinates of at least two 2D subplots."));
        std::set<int> unique;
        for(int index:action.linkedPanels)if(!unique.insert(index).second||!figure->panels.contains(index)||figure->panels.at(index)->state.dimensions!=2)return fail(error,tr("Link unique allocated subplots containing 2D plots."));
        figure->state.linkedPanels=action.linkedPanels;figure->state.linkedAxesMask=action.linkedAxesMask;
        synchronizeLinkedAxes(figure->state.number,action.linkedPanels.front(),false);return true;
    }
    if(action.kind==FigureActionKind::ConfigurePlot){
        if(!action.plotStyle)return fail(error,tr("Missing plot property patch."));
        int number=action.figure? action.figure:currentFigureNumber();if(number==0)number=1;
        if(!ensureFigureCapacity(number))return false;
        auto* selected=selectFigure(number);auto* panel=selected->active();runtime::PlotStyle proposed;
        try{proposed=runtime::patchedPlotStyle(panel->state.style,*action.plotStyle);}catch(const std::exception& e){return fail(error,QString::fromUtf8(e.what()));}
        std::array<QImage,5> images;const std::array<QString,5> texts{panel->state.title,panel->state.xLabel,panel->state.yLabel,panel->state.zLabel,panel->state.caption};
        const std::array<bool,5> latex{panel->state.titleLatex,panel->state.xLabelLatex,panel->state.yLabelLatex,panel->state.zLabelLatex,panel->state.captionLatex};
        for(std::size_t k=0;k<texts.size();++k)if(!texts[k].isEmpty()){images[k]=renderMathText(texts[k],publicationFont(proposed,static_cast<int>(k)),Qt::black,latex[k],error);if(images[k].isNull())return false;}
        panel->state.style=std::move(proposed);panel->labelImages=std::move(images);
        const bool bounds=std::any_of(action.plotStyle->limitsSet.begin(),action.plotStyle->limitsSet.end(),[](bool value){return value;})||action.plotStyle->equalUnits.has_value();
        selected->rebuild(bounds);if(bounds)synchronizeLinkedAxes(number,panel->state.index,false);return true;
    }
    const bool annotation=action.kind==FigureActionKind::SetTitle||action.kind==FigureActionKind::SetXLabel
        ||action.kind==FigureActionKind::SetYLabel||action.kind==FigureActionKind::SetZLabel
        ||action.kind==FigureActionKind::SetCaption;
    if(annotation){
        int kind=0;
        if(action.kind==FigureActionKind::SetXLabel)kind=1;
        else if(action.kind==FigureActionKind::SetYLabel)kind=2;
        else if(action.kind==FigureActionKind::SetZLabel)kind=3;
        else if(action.kind==FigureActionKind::SetCaption)kind=4;
        const QString text=QString::fromStdString(action.text);
        QImage rendered;
        if(!text.isEmpty()){
            const auto* current=entry(action.figure? action.figure:currentFigureNumber());
            QFont font=publicationFont(current&&current->active()?current->active()->state.style:runtime::PlotStyle{},kind);
            QString message;rendered=renderMathText(text,font,Qt::black,action.latex,&message);
            if(rendered.isNull())return fail(error,tr("Figure label could not be rendered: %1").arg(message));
        }
        int number=action.figure==0?currentFigureNumber():action.figure;
        if(number==0)number=1;
        if(!ensureFigureCapacity(number))return false;
        Entry* selected=selectFigure(number);
        auto* panel=selected->active();
        std::array<QString*,5> texts{{&panel->state.title,&panel->state.xLabel,&panel->state.yLabel,
                                     &panel->state.zLabel,&panel->state.caption}};
        std::array<bool*,5> modes{{&panel->state.titleLatex,&panel->state.xLabelLatex,&panel->state.yLabelLatex,
                                  &panel->state.zLabelLatex,&panel->state.captionLatex}};
        *texts[static_cast<std::size_t>(kind)]=text;*modes[static_cast<std::size_t>(kind)]=action.latex;
        panel->labelImages[static_cast<std::size_t>(kind)]=rendered;
        selected->rebuild(false);return true;
    }
    if(action.kind!=FigureActionKind::Plot)return fail(error,tr("Unknown figure action."));
    if(action.dimensions!=2&&action.dimensions!=3)return fail(error,tr("A plot requires exactly two or three axes."));
    const bool generic=action.axes==std::array<int,3>{{-1,-1,-1}};
    if(generic&&action.curves.empty()&&!action.mesh&&!action.graph)
        return fail(error,tr("Generic numeric coordinates require a curve plot."));
    const auto basisWidth=action.basisLabels.empty()?std::size_t(5):action.basisLabels.size();
    if(basisWidth>64)return fail(error,tr("Algebra projections support at most64 stored basis coordinates."));
    if(!action.basisLabels.empty()){
        std::set<std::string> unique;
        for(const auto& name:action.basisLabels){
            if(name.empty()||name.size()>64||!unique.insert(name).second||!std::all_of(name.begin(),name.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';}))return fail(error,tr("Algebra projection basis labels are invalid or duplicated."));
        }
    }
    for(int axis=0;!generic&&axis<action.dimensions;++axis){
        const int coordinate=action.axes[static_cast<std::size_t>(axis)];
        if(coordinate<0||static_cast<std::size_t>(coordinate)>=basisWidth)return fail(error,tr("Projection coordinates must lie within the stored algebra basis."));
        for(int previous=0;previous<axis;++previous)
            if(action.axes[static_cast<std::size_t>(previous)]==coordinate)
                return fail(error,tr("Projection coordinates must be distinct."));
    }
    if((static_cast<int>(!action.vectors.empty())+static_cast<int>(!action.curves.empty())+static_cast<int>(action.mesh.has_value())+static_cast<int>(action.graph.has_value())!=1)||action.vectors.size()>maximumVectors||action.curves.size()>maximumVectors)
        return fail(error,tr("Each plot requires either 1..256 vectors or 1..256 curves."));
    if((action.basisLabels.empty()&&(action.algebraId!="A_gamma_v1"||!std::isfinite(action.gamma)||action.gamma<0||action.gamma>1))||(!action.basisLabels.empty()&&((!action.algebraId.starts_with("builtin/")&&!action.algebraId.starts_with("table/"))||action.algebraId.size()>512)))
        return fail(error,tr("The figure's algebra context is invalid or unsupported."));
    for(const auto& vector:action.vectors){
        if(vector.allCoefficients().size()!=basisWidth)return fail(error,tr("Every vector must retain exactly its algebra's complete coefficient set."));
        for(const double value:vector.allCoefficients())if(!std::isfinite(value))return fail(error,tr("Every stored algebra coefficient must be finite."));
    }
    std::size_t newPoints=0;
    if(action.graph){try{plotting::validateGraphView(*action.graph);if(action.graph->dimensions!=action.dimensions)return fail(error,tr("Graph and plot dimensions differ."));newPoints=action.graph->positions.size()+2*plotting::graphSegments(*action.graph).size();if(newPoints>maximumRetainedPoints)return fail(error,tr("Graph display exceeds the retained point budget."));}catch(const plotting::ExportError& e){return fail(error,QString::fromUtf8(e.what()));}}
    if(action.mesh){const auto& m=*action.mesh;newPoints=m.points.size();if(newPoints>maximumCurvePoints||m.triangles.size()>2000000||m.segments.size()>500000)return fail(error,tr("Geometry plot exceeds its bounded mesh layout."));if(!m.scalars.empty()&&m.scalars.size()!=newPoints)return fail(error,tr("Geometry scalar values require one value per vertex."));for(const auto& p:m.points)for(const auto v:p)if(!std::isfinite(v))return fail(error,tr("Geometry vertices must be finite."));for(const auto v:m.scalars)if(!std::isfinite(v))return fail(error,tr("Geometry scalar values must be finite."));for(const auto& f:m.triangles)for(const auto i:f)if(i>=newPoints)return fail(error,tr("Invalid geometry triangle index."));for(const auto& e:m.segments)for(const auto i:e)if(i>=newPoints)return fail(error,tr("Invalid geometry segment index."));}
    for(const auto& curve:action.curves){
        if(curve.points.size()<2||curve.points.size()>maximumCurvePoints-newPoints)
            return fail(error,tr("Curves require at least two points and at most 1,000,000 points per plot."));
        newPoints+=curve.points.size();
        for(const auto& point:curve.points)for(double value:point)
            if(!std::isfinite(value))return fail(error,tr("All curve coordinates must be finite."));
    }
    int number=action.figure;
    if(number==0)number=currentFigureNumber();
    if(number==0)number=1;
    if(!ensureFigureCapacity(number))return false;
    std::size_t retainedPoints=newPoints;
    for(const auto& figure:m_figures)for(const auto& [index,panel]:figure->panels){
        if(figure->state.number==number&&index==figure->state.activeSubplot&&!panel->state.style.hold)continue;
        if(panel->state.graph){const auto n=panel->state.graph->positions.size()+2*plotting::graphSegments(*panel->state.graph).size();if(n>maximumRetainedPoints-retainedPoints)return fail(error,tr("Open figures exceed the retained graph point budget."));retainedPoints+=n;}
        if(panel->state.mesh){const auto n=panel->state.mesh->points.size();if(n>maximumRetainedPoints-retainedPoints)return fail(error,tr("Open figures exceed the retained mesh/curve point budget."));retainedPoints+=n;}
        for(const auto& curve:panel->state.curves){
            if(curve.points.size()>maximumRetainedPoints-retainedPoints)
                return fail(error,tr("Open figures may retain at most 2,000,000 sampled curve points. Clear or close a figure first."));
            retainedPoints+=curve.points.size();
        }
    }
    Entry* selected=selectFigure(number);
    auto* panel=selected->active();
    if(panel->state.style.hold&&panel->state.dimensions!=0){
        if(panel->state.graph||action.graph)return fail(error,tr("Graph views require their own subplot; held graph overlays are unsupported."));
        if(panel->state.dimensions!=action.dimensions||panel->state.axes!=action.axes||panel->state.algebraId!=QString::fromStdString(action.algebraId)||panel->state.basisLabels!=action.basisLabels||(action.basisLabels.empty()&&panel->state.gamma!=action.gamma))return fail(error,tr("Held plots require matching dimensions, coordinate axes and algebra context."));
        if(panel->state.vectors.size()+panel->state.curves.size()+action.vectors.size()+action.curves.size()>maximumVectors)return fail(error,tr("Held plots may contain at most 256 vector/curve series."));
        if(panel->state.mesh&&action.mesh)return fail(error,tr("Use separate subplots for multiple field meshes."));
    }
    panel->clearAnimation();panel->clearAllTips();panel->state.graph.reset();
    panel->state.dimensions=action.dimensions;
    panel->state.axes=action.axes;
    if(!panel->state.style.hold){panel->state.vectors.clear();panel->state.curves.clear();panel->state.mesh.reset();}
    panel->state.vectors.insert(panel->state.vectors.end(),action.vectors.begin(),action.vectors.end());
    panel->state.curves.insert(panel->state.curves.end(),action.curves.begin(),action.curves.end());
    if(action.mesh)panel->state.mesh=action.mesh;
    if(action.graph){panel->state.graph=action.graph;panel->state.style.equalUnits=true;panel->state.style.legendVisible=false;panel->setTool(action.graph->interactive?PlotTool::GraphSelect:PlotTool::Navigate);}
    else if(panel->tool==PlotTool::GraphSelect||panel->tool==PlotTool::GraphDrag)panel->setTool(PlotTool::Navigate);
    panel->state.algebraId=QString::fromStdString(action.algebraId);
    panel->state.gamma=action.gamma;
    panel->state.basisLabels=action.basisLabels;
    selected->rebuild();
    synchronizeLinkedAxes(number,panel->state.index,false);
    return true;
}

void FigureManager::apply(const std::vector<runtime::FigureAction>& actions) {
    for(const auto& action:actions){
        QString message;
        if(!applyOne(action,&message)){emit error(message);break;}
    }
    updateToolbar();emit figuresChanged();
}

void FigureManager::updateToolbar() {
    updateSelectionBorders();
    const auto* selected=entry(currentFigureNumber());
    for(auto* action:m_toolbar->actions()){
        const QString name=action->objectName();
        if(name==QStringLiteral("exportFigureImage")||name==QStringLiteral("resetFigureView"))
            action->setEnabled(selected&&selected->hasPlot());
        else if(name==QStringLiteral("undockFigure"))action->setEnabled(selected&&selected->state.docked);
        else action->setEnabled(!m_figures.empty());
    }
}

void FigureManager::updateSelectionBorders(){
    const int selected=currentFigureNumber();
    for(const auto& figure:m_figures){
        const bool activeFigure=figure->state.number==selected;
        if(figure->page->property("studioFigureSelected").toBool()!=activeFigure)figure->page->setProperty("studioFigureSelected",activeFigure);
        if(figure->selectionBorder)figure->selectionBorder->setSelected(activeFigure);
        for(const auto& [index,panel]:figure->panels){
            // As in GTG, inactive figures remember their selected subplot.
            const bool activePanel=index==figure->state.activeSubplot;
            if(panel->page->property("studioSubplotActive").toBool()!=activePanel)panel->page->setProperty("studioSubplotActive",activePanel);
            if(panel->selectionBorder)panel->selectionBorder->setSelected(activePanel);
        }
    }
}

void FigureManager::activateFigureWidget(QWidget* widget){
    if(!widget||m_rearranging||m_destroying)return;
    for(const auto& figure:m_figures){
        auto* dock=m_grouped?m_groups->dockWidget(figure->state.number):nullptr;
        const auto* floating=figureFloatingWindow(figure->state.number);
        if(widget!=figure->page&&!figure->page->isAncestorOf(widget)
            &&!(dock&&(widget==dock||dock->isAncestorOf(widget)))&&widget!=floating)continue;
        bool changed=m_selectedNumber!=figure->state.number;m_selectedNumber=figure->state.number;
        for(const auto& [index,panel]:figure->panels)if(widget==panel->page||panel->page->isAncestorOf(widget)){
            if(figure->state.activeSubplot!=index){figure->state.activeSubplot=index;figure->syncState();changed=true;}
            break;
        }
        if(changed)updateToolbar();
        return;
    }
}

void FigureManager::requestExport() {
    const int number=currentFigureNumber();
    if(number==0)return;
    QString selectedFilter=tr("PNG image (*.png)");
    QString path=QFileDialog::getSaveFileName(this,tr("Export Figure %1").arg(number),
        QStringLiteral("figure-%1").arg(number),tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg);;SVG vector image (*.svg);;PDF vector figure (*.pdf)"),&selectedFilter);
    if(path.isEmpty())return;
    if(QFileInfo(path).suffix().isEmpty())path+=selectedFilter.contains(QStringLiteral("PDF"))?QStringLiteral(".pdf"):selectedFilter.contains(QStringLiteral("SVG"))?QStringLiteral(".svg"):
        selectedFilter.contains(QStringLiteral("JPEG"))?QStringLiteral(".jpg"):QStringLiteral(".png");
    QString message;
    if(!exportFigure(number,path,&message))emit error(message);
}

bool FigureManager::exportFigure(int number,const QString& path,QString* error) {
    runtime::FigureExportOptions options;options.overwrite=true;return exportSelection(number,std::nullopt,path,error,options);
}

bool FigureManager::exportSubplot(int figure,int panel,const QString& path,QString* error) {
    runtime::FigureExportOptions options;options.overwrite=true;return exportSelection(figure,panel,path,error,options);
}

void FigureManager::requestSubplotExport(int figure,int panel) {
    const auto* original=entry(figure);
    if(!original||!original->panels.contains(panel)){emit error(tr("The requested subplot is no longer open."));return;}
    const QPointer<QWidget> figurePage=original->page, panelPage=original->panels.at(panel)->page;
    QString selectedFilter=tr("PNG image (*.png)");
    QString path=QFileDialog::getSaveFileName(this,tr("Save Figure %1 — Subplot %2").arg(figure).arg(panel),
        QStringLiteral("figure-%1-subplot-%2").arg(figure).arg(panel),
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg);;SVG vector image (*.svg);;PDF vector figure (*.pdf)"),&selectedFilter);
    if(path.isEmpty())return;
    const auto* current=entry(figure);
    if(!figurePage||!panelPage||!current||current->page!=figurePage.data()||!current->panels.contains(panel)
        ||current->panels.at(panel)->page!=panelPage.data()){
        emit error(tr("The subplot changed or closed while the Save dialog was open; no image file was replaced."));return;
    }
    if(QFileInfo(path).suffix().isEmpty())path+=selectedFilter.contains(QStringLiteral("PDF"))?QStringLiteral(".pdf"):selectedFilter.contains(QStringLiteral("SVG"))?QStringLiteral(".svg"):
        selectedFilter.contains(QStringLiteral("JPEG"))?QStringLiteral(".jpg"):QStringLiteral(".png");
    QString message;if(!exportSubplot(figure,panel,path,&message))emit error(message);
}

bool FigureManager::exportFigure(int number,const QString& path,const runtime::FigureExportOptions& options,QString* error){
    return exportSelection(number,options.subplot?std::optional<int>(options.subplot):std::nullopt,path,error,options);
}

bool FigureManager::exportSelection(int number,std::optional<int> requestedPanel,const QString& path,QString* error,const runtime::FigureExportOptions& options) {
    if(error)error->clear();
    auto* selected=entry(number);
    if(!selected||!selected->hasPlot())return fail(error,tr("Select a figure containing a plot before exporting."));
    if(requestedPanel&&(!selected->panels.contains(*requestedPanel)||selected->panels.at(*requestedPanel)->state.dimensions==0))
        return fail(error,tr("The requested subplot does not contain a plot."));
    const QString extension=QFileInfo(path).suffix().toLower();
    if(extension!=QStringLiteral("png")&&extension!=QStringLiteral("jpg")&&extension!=QStringLiteral("jpeg")&&extension!=QStringLiteral("svg")&&extension!=QStringLiteral("pdf"))
        return fail(error,tr("Figure export supports PNG, JPEG, SVG and vector PDF."));
    if(!options.overwrite&&QFileInfo::exists(path))return fail(error,tr("Export destination already exists; enable overwrite explicitly."));
    if(options.dpi<36||options.dpi>1200||!std::isfinite(options.widthCm)||!std::isfinite(options.heightCm)||options.widthCm<0||options.heightCm<0||options.widthCm>100||options.heightCm>100||(options.widthCm==0)!=(options.heightCm==0))return fail(error,tr("Invalid physical export dimensions or DPI."));
    if(!m_renderingEnabled)return fail(error,tr("Image export requires an enabled plot renderer."));
    const QPointer<QWidget> requestedPage=selected->page;
    const QPointer<QWidget> requestedPanelPage=requestedPanel?selected->panels.at(*requestedPanel)->page:nullptr;
    if(m_grouped)m_groups->setCurrentFigure(number);
    else if(selected->floating){selected->floating->show();selected->floating->raise();}
    else if(!m_tiled)m_tabs->setCurrentWidget(selected->page);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    // A queued interpreter result may close/replace figure tabs while Qt
    // initializes the selected context. Never retain an Entry across that pump.
    selected=entry(number);
    if(!selected||!requestedPage||selected->page!=requestedPage.data()||!selected->hasPlot())
        return fail(error,tr("The selected figure changed or closed before export; no image file was replaced."));
    if(requestedPanel&&(!requestedPanelPage||!selected->panels.contains(*requestedPanel)
        ||selected->panels.at(*requestedPanel)->page!=requestedPanelPage.data()||selected->panels.at(*requestedPanel)->state.dimensions==0))
        return fail(error,tr("The selected subplot changed or closed before export; no image file was replaced."));
    // Export may immediately follow a plot/resize, before the coalesced screen
    // render has captured its final size. Projection/labels need that size now.
    for(const auto& [id,panel]:selected->panels){
        (void)id;if(!panel->canvas||!panel->window)continue;
        const auto ratio=panel->canvas->devicePixelRatioF();
        panel->window->SetSize(qCeil(panel->canvas->width()*ratio),qCeil(panel->canvas->height()*ratio));
        panel->positionOverlays();
    }
    const bool single=requestedPanel.has_value()||(selected->state.rows==1&&selected->state.columns==1);
    FigurePanel* one=single?selected->panels.at(requestedPanel.value_or(selected->state.activeSubplot)).get():nullptr;
    if(extension==QStringLiteral("svg")||extension==QStringLiteral("pdf")){
        auto* host=selected->grid->parentWidget();const qreal ratio=host->devicePixelRatioF();
        QSize size=host->size()*ratio;
        if(one){if(!one->canvas||!one->canvas->isVisible()||!one->canvas->isValid())return fail(error,tr("The subplot renderer is unavailable."));
            const int* pixels=one->window->GetSize();size=QSize(pixels[0],pixels[1]);}
        if(size.isEmpty()||static_cast<qint64>(size.width())*size.height()>64000000)return fail(error,tr("The SVG export size is unavailable or exceeds 64 million pixels."));
        const QSize sourceSize=size;
        if(options.widthCm>0)size=QSize(qRound(options.widthCm*options.dpi/2.54),qRound(options.heightCm*options.dpi/2.54));
        if(size.width()<1||size.height()<1||size.width()>16000||size.height()>16000||static_cast<qint64>(size.width())*size.height()>64000000)return fail(error,tr("Export dimensions exceed their pixel budget."));
        SvgBuffer buffer;buffer.open(QIODevice::WriteOnly);QSvgGenerator generator;std::unique_ptr<QPdfWriter> pdf;
        QPaintDevice* device=&generator;
        if(extension==QStringLiteral("pdf")){
            pdf=std::make_unique<QPdfWriter>(&buffer);pdf->setResolution(options.dpi);
            pdf->setPageSize(QPageSize(QSizeF(size.width()*25.4/options.dpi,size.height()*25.4/options.dpi),QPageSize::Millimeter));
            pdf->setPageMargins(QMarginsF(0,0,0,0));pdf->setTitle(tr("Pentarnion Studio figure %1").arg(number));pdf->setCreator(QStringLiteral("Pentarnion Studio native vector renderer"));device=pdf.get();
        }else{
            generator.setOutputDevice(&buffer);generator.setResolution(options.dpi);generator.setSize(size);generator.setViewBox(QRect(QPoint(),size));
            generator.setTitle(requestedPanel?tr("Figure %1 — Subplot %2").arg(number).arg(*requestedPanel):tr("Figure %1").arg(number));
            generator.setDescription(tr("Pentarnion Studio vector export of the current camera view. Curves, mesh projections, axes and glyph outlines are vector geometry; 3D surfaces use painter depth ordering."));
        }
        QPainter painter;if(!painter.begin(device))return fail(error,tr("The vector painter could not be started."));
        painter.scale(static_cast<double>(device->width())/sourceSize.width(),static_cast<double>(device->height())/sourceSize.height());
        painter.fillRect(QRect(QPoint(),sourceSize),Qt::white);std::size_t primitives=0;
        auto paint=[&](FigurePanel* panel){
            if(!panel->canvas||!panel->canvas->isVisible()||!panel->canvas->isValid())return fail(error,tr("A subplot renderer is unavailable."));
            panel->render();painter.save();
            if(!single){const QPoint position=panel->canvas->mapTo(host,QPoint());painter.translate(QPointF(position)*ratio);
                const int* pixels=panel->window->GetSize();painter.scale(panel->canvas->width()*ratio/std::max(1,pixels[0]),panel->canvas->height()*ratio/std::max(1,pixels[1]));}
            const bool okay=panel->paintVector(painter,primitives,error);painter.restore();return okay;
        };
        bool okay=true;
        if(one)okay=paint(one);else for(const auto& [index,panel]:selected->panels)if(!paint(panel.get())){okay=false;break;}
        const bool ended=painter.end();
        if(!okay)return false;
        if(!ended||buffer.limitReached)return fail(error,tr("SVG export exceeds its 128 MiB size limit or could not be completed."));
        pdf.reset();QByteArray bytes=buffer.data();if(extension==QStringLiteral("svg")&&!outlineSvgText(bytes,error))return false;
        QSaveFile file(path);file.setDirectWriteFallback(false);
        if(!file.open(QIODevice::WriteOnly))return fail(error,tr("Cannot open the export file: %1").arg(file.errorString()));
        if(file.write(bytes)!=bytes.size()){file.cancelWriting();return fail(error,tr("Cannot write the exported SVG: %1").arg(file.errorString()));}
        if(!file.commit())return fail(error,tr("Cannot safely save the exported SVG: %1").arg(file.errorString()));
        return true;
    }
    QImage frame;
    if(single){
        auto* panel=one;
        if(!panel||!panel->canvas||!panel->canvas->isVisible()||!panel->canvas->isValid())
            return fail(error,tr("The figure renderer is unavailable. Show the figure tab and try again."));
        panel->render();frame=panel->canvas->grabFramebuffer().convertToFormat(QImage::Format_RGB888);
    }else{
        auto* host=selected->grid->parentWidget();
        const qreal ratio=host->devicePixelRatioF();
        const QSize size=host->size()*ratio;
        if(size.isEmpty()||static_cast<qint64>(size.width())*size.height()>64000000)
            return fail(error,tr("The subplot export size is unavailable or exceeds 64 million pixels."));
        frame=QImage(size,QImage::Format_RGB888);frame.fill(Qt::white);
        QPainter painter(&frame);
        for(const auto& [index,panel]:selected->panels){
            if(!panel->canvas||!panel->canvas->isVisible()||!panel->canvas->isValid())
                return fail(error,tr("A subplot renderer is unavailable. Show the figure and try again."));
            panel->render();
            const QImage image=panel->canvas->grabFramebuffer();
            if(image.isNull())return fail(error,tr("A rendered subplot could not be captured."));
            const QPoint position=panel->canvas->mapTo(host,QPoint());
            painter.drawImage(QRectF(QPointF(position)*ratio,QSizeF(panel->canvas->size())*ratio),image);
        }
        painter.end();
    }
    if(frame.isNull()||frame.width()<=0||frame.height()<=0)return fail(error,tr("The rendered figure could not be captured."));
    if(options.widthCm>0){const QSize requested(qRound(options.widthCm*options.dpi/2.54),qRound(options.heightCm*options.dpi/2.54));if(requested.width()<1||requested.height()<1||requested.width()>16000||requested.height()>16000||static_cast<qint64>(requested.width())*requested.height()>64000000)return fail(error,tr("Raster export exceeds its pixel budget."));frame=frame.scaled(requested,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);}
    frame.setDotsPerMeterX(qRound(options.dpi/0.0254));frame.setDotsPerMeterY(qRound(options.dpi/0.0254));
    QByteArray encodedBytes;QBuffer encodedBuffer(&encodedBytes);encodedBuffer.open(QIODevice::WriteOnly);if(!frame.save(&encodedBuffer,extension==QStringLiteral("png")?"PNG":"JPEG",95))return fail(error,tr("The native image encoder failed."));
    QSaveFile output(path);output.setDirectWriteFallback(false);if(!output.open(QIODevice::WriteOnly)||output.write(encodedBytes)!=encodedBytes.size()||!output.commit())return fail(error,tr("Cannot safely save the exported image."));return true;

}

} // namespace penta

namespace penta {
#include "figure_groups.inc.hpp"
}
