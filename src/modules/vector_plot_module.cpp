#include "vector_plot_api.h"
#include "ide/math_text_renderer.hpp"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIODevice>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSvgGenerator>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QTextLayout>
#include <QTextOption>
#include <QGlyphRun>
#include <QRawFont>
#include <QPainterPath>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
namespace {
std::mutex drawingMutex;
struct PainterState final {
    QPainter& painter;
    explicit PainterState(QPainter& value):painter(value){painter.save();}
    ~PainterState(){painter.restore();}
    PainterState(const PainterState&)=delete;
    PainterState& operator=(const PainterState&)=delete;
};
void application(){if(auto* current=QCoreApplication::instance()){if(!qobject_cast<QGuiApplication*>(current))throw std::runtime_error("Vector typography requires a Qt GUI-capable application host.");return;}static int argc=3;static char name[]="penta-vector-export",option[]="-platform",platform[]="offscreen";static char* argv[]{name,option,platform,nullptr};
    // This lazy CLI host lives for the process lifetime, as does the loaded
    // module. Studio already owns its QApplication and never creates this one.
    static auto* host=new QGuiApplication(argc,argv);(void)host;
}
class Output final:public QIODevice {
public:
    const penta_vector_host_v1& host;uint64_t maximum,written=0;bool failed=false,buffered=false;QByteArray bytes;
    Output(const penta_vector_host_v1& h,uint64_t limit,bool capture=false):host(h),maximum(limit),buffered(capture){open(QIODevice::WriteOnly);}
    bool isSequential()const override{return true;}
    qint64 readData(char*,qint64)override{return -1;}
    qint64 writeData(const char* value,qint64 count)override{try{if(count<0||static_cast<uint64_t>(count)>maximum-written||(host.cancelled&&host.cancelled(host.context))){failed=true;return -1;}if(buffered)bytes.append(value,count);else if(host.append(host.context,value,static_cast<uint64_t>(count))){failed=true;return -1;}written+=static_cast<uint64_t>(count);return count;}catch(...){failed=true;return -1;}}

};
#include "vector_outlines.inc"
void message(char* text,uint64_t cap,const char* value){if(text&&cap){const auto count=std::min<uint64_t>(cap-1,std::strlen(value));std::memcpy(text,value,count);text[count]=0;}}
int render(const penta_vector_host_v1* host,const penta_vector_request_v1* request,char* error,uint64_t capacity)noexcept{
    try{
        if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_VECTOR_PLOT_ABI_V1||!host->append||!request||request->struct_size<sizeof(*request))throw std::invalid_argument("Incomplete vector export ABI.");
        const auto& r=*request;
        if(r.format<1||r.format>2||r.width<160||r.width>16000||r.height<120||r.height>16000||r.dpi<36||r.dpi>1200||r.commands>1000000||(r.commands&&!r.data)||r.max_bytes>134217728||r.max_bytes<1024||r.metadata_bytes>1048576||(r.metadata_bytes&&!r.metadata))throw std::invalid_argument("Vector export dimensions/command/metadata budget is invalid.");
        std::lock_guard lock(drawingMutex);application();Output output(*host,r.max_bytes,r.format==1);std::unique_ptr<QSvgGenerator> svg;std::unique_ptr<QPdfWriter> pdf;QPaintDevice* device=nullptr;
        if(r.format==1){svg=std::make_unique<QSvgGenerator>();svg->setOutputDevice(&output);svg->setSize(QSize(static_cast<int>(r.width),static_cast<int>(r.height)));svg->setViewBox(QRect(0,0,r.width,r.height));svg->setResolution(r.dpi);svg->setTitle("Pentarnion Studio native scientific figure");svg->setDescription(QString::fromUtf8(r.metadata,static_cast<qsizetype>(r.metadata_bytes)));device=svg.get();}
        else{pdf=std::make_unique<QPdfWriter>(&output);pdf->setResolution(r.dpi);pdf->setPageSize(QPageSize(QSizeF(r.width*25.4/r.dpi,r.height*25.4/r.dpi),QPageSize::Millimeter));pdf->setPageMargins(QMarginsF(0,0,0,0));pdf->setCreator("Pentarnion Studio native vector export");pdf->setTitle("Scientific figure");pdf->setDocumentXmpMetadata((QString("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><penta:figure xmlns:penta=\"urn:pentarnion:figure:v1\">%1</penta:figure></x:xmpmeta>").arg(QString::fromUtf8(r.metadata,static_cast<qsizetype>(r.metadata_bytes)).toHtmlEscaped())).toUtf8());device=pdf.get();}
        QPainter painter;if(!painter.begin(device))throw std::runtime_error("Cannot initialize native vector painter.");painter.setRenderHint(QPainter::Antialiasing);painter.setRenderHint(QPainter::TextAntialiasing);painter.fillRect(QRectF(0,0,r.width,r.height),Qt::white);uint64_t textBytes=0;
        for(uint64_t index=0;index<r.commands;++index){if(host->cancelled&&host->cancelled(host->context))throw std::runtime_error("Native vector export cancelled.");const auto& c=r.data[index];if(c.kind>3||c.style>3||c.font_family>2||c.latex>1||c.text_bytes>65536||textBytes>1048576-c.text_bytes||(c.text_bytes&&!c.text))throw std::invalid_argument("Invalid bounded vector drawing command.");textBytes+=c.text_bytes;for(double value:c.x)if(!std::isfinite(value)||std::abs(value)>10000000)throw std::invalid_argument("Projected vector coordinates exceed the portable painter allowance; adjust axis limits.");for(double value:c.y)if(!std::isfinite(value)||std::abs(value)>10000000)throw std::invalid_argument("Projected vector coordinates exceed the portable painter allowance; adjust axis limits.");for(double value:c.color)if(!std::isfinite(value)||value<0||value>1)throw std::invalid_argument("Invalid vector color.");for(double value:c.clip)if(!std::isfinite(value))throw std::invalid_argument("Invalid vector clipping rectangle.");if(!std::isfinite(c.width)||!std::isfinite(c.height)||c.width<0||c.height<0||c.width>16000||c.height>16000)throw std::invalid_argument("Invalid vector width/height.");
            const PainterState commandState(painter);painter.setClipRect(QRectF(c.clip[0],c.clip[1],std::max(0.,c.clip[2]-c.clip[0]),std::max(0.,c.clip[3]-c.clip[1])));const QColor color=QColor::fromRgbF(c.color[0],c.color[1],c.color[2]);QPen pen(color);pen.setWidthF(c.width);pen.setStyle(c.style==1?Qt::DashLine:c.style==2?Qt::DotLine:c.style==3?Qt::DashDotLine:Qt::SolidLine);painter.setPen(pen);
            if(c.kind==0)painter.drawLine(QPointF(c.x[0],c.y[0]),QPointF(c.x[1],c.y[1]));
            else if(c.kind==1){painter.setPen(Qt::NoPen);painter.setBrush(color);painter.drawPolygon(QPolygonF{{c.x[0],c.y[0]},{c.x[1],c.y[1]},{c.x[2],c.y[2]}});}
            else if(c.kind==3){painter.setPen(Qt::NoPen);painter.setBrush(color);painter.drawEllipse(QPointF(c.x[0],c.y[0]),c.width,c.width);}
            else{const auto text=QString::fromUtf8(c.text,static_cast<qsizetype>(c.text_bytes));QFont font(c.font_family==1?"Times New Roman":c.font_family==2?"Consolas":"Arial");font.setPointSizeF(std::max(6.,(c.height-8)*72.0/96.0));QString failure;const auto layout=penta::renderMathText(text,font,color,c.latex!=0,&failure);if(layout.isNull())throw std::runtime_error(failure.toStdString());const double scale=std::min(c.width/std::max(1,layout.width()),c.height/std::max(1,layout.height()));if(scale>0&&!penta::paintMathText(painter,QRectF(c.x[0]+(c.x[1]>0.5?(c.width-layout.width()*scale)*.5:0),c.y[0],layout.width()*scale,layout.height()*scale),text,font,color,c.latex!=0,&failure))throw std::runtime_error(failure.toStdString());}
            if(output.failed)throw std::runtime_error("Vector output quota, cancellation or host sink failure.");
        }
        painter.end();svg.reset();pdf.reset();if(r.format==1&&!output.failed)outlineSvg(output.bytes,*host,r.max_bytes);if(output.failed)throw std::runtime_error("Vector output quota, cancellation or host sink failure.");return 0;
    }catch(const std::exception& failure){message(error,capacity,failure.what());return 1;}catch(...){message(error,capacity,"Native vector renderer failed.");return 1;}
}
const penta_vector_plot_api_v1 api{sizeof(api),PENTA_VECTOR_PLOT_ABI_V1,"penta_vector_plot",render};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_vector_plot_api_v1* penta_vector_plot_query_v1(uint32_t abi){return abi==PENTA_VECTOR_PLOT_ABI_V1?&api:nullptr;}
