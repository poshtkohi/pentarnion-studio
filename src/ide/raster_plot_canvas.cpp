#include "raster_plot_canvas.hpp"
#include <QApplication>
#include <QMetaObject>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace penta {
namespace {
constexpr std::size_t maximumQueuedBytes=256U*1024U*1024U;
// Accessed only by the GUI thread. Hidden/deleted canvases release their image.
std::size_t cachedDisplayBytes=0;
struct Work {
    std::uint64_t owner=0,generation=0;
    QSize pixels;double dpr=1;
    RasterPlotTask task;
    std::function<void(QImage,QString)> completion;
};
struct Completed {
    QImage image;
    QString error;
    std::shared_ptr<std::atomic_bool> cancelled;
    std::function<void(QImage,QString)> completion;
};
class Service final:public QObject {
public:
    explicit Service(QObject* parent):QObject(parent),thread_([this]{run();}){}
    ~Service()override{stop();}
    void stop(){
        std::map<std::uint64_t,Work> retired;
        std::map<std::uint64_t,Completed> completed;
        {
            std::lock_guard lock(mutex_);stopping_=true;
            for(auto& [id,work]:pending_){(void)id;if(work.task.cancelled)work.task.cancelled->store(true);}
            if(activeCancel_)activeCancel_->store(true);
            retired.swap(pending_);completed.swap(completed_);order_.clear();bytes_=completedBytes_=0;
        }
        available_.notify_all();
        if(thread_.joinable())thread_.join();
    }
    bool submit(Work work){
        std::optional<Work> retired;
        {
            std::lock_guard lock(mutex_);
            if(stopping_)return false;
            if(auto it=pending_.find(work.owner);it!=pending_.end()){
                bytes_-=it->second.task.estimatedBytes;retired=std::move(it->second);pending_.erase(it);std::erase(order_,work.owner);
            }
            if(work.task.estimatedBytes>maximumQueuedBytes-bytes_)return false;
            bytes_+=work.task.estimatedBytes;order_.push_back(work.owner);pending_.emplace(work.owner,std::move(work));
        }
        available_.notify_one();return true;
    }
    void cancel(std::uint64_t owner){
        std::optional<Work> retired;std::optional<Completed> completed;
        {
            std::lock_guard lock(mutex_);
            if(auto it=pending_.find(owner);it!=pending_.end()){
                bytes_-=it->second.task.estimatedBytes;retired=std::move(it->second);pending_.erase(it);
            }
            if(auto it=completed_.find(owner);it!=completed_.end()){
                completedBytes_-=static_cast<std::size_t>(it->second.image.sizeInBytes());completed=std::move(it->second);completed_.erase(it);
            }
            if(activeOwner_==owner&&activeCancel_)activeCancel_->store(true);
            std::erase(order_,owner);
        }
    }
    std::size_t count(){std::lock_guard lock(mutex_);return pending_.size();}
private:
    void run(){
        QThread::currentThread()->setPriority(QThread::LowPriority);
        for(;;){
            Work work;
            {
                std::unique_lock lock(mutex_);available_.wait(lock,[this]{return stopping_||!order_.empty();});
                if(stopping_)return;
                const auto id=order_.front();order_.pop_front();auto it=pending_.find(id);
                if(it==pending_.end())continue;
                work=std::move(it->second);bytes_-=work.task.estimatedBytes;pending_.erase(it);activeCancel_=work.task.cancelled;activeOwner_=id;
            }
            QImage result;QString error;
            if(!work.task.cancelled||!work.task.cancelled->load())try{
                result=work.task.paint();
                if(result.isNull())error=QStringLiteral("The figure renderer returned no image.");
            }catch(const std::exception& e){error=QString::fromUtf8(e.what());}catch(...){error=QStringLiteral("The figure could not be rendered.");}
            // Release the detached geometry before posting the screen result.
            work.task.paint={};
            bool schedule=false;std::optional<Completed> retired;
            {
                std::lock_guard lock(mutex_);activeCancel_.reset();activeOwner_=0;
                if(stopping_)return;
                if(work.task.cancelled&&work.task.cancelled->load())continue;
                if(auto it=completed_.find(work.owner);it!=completed_.end()){
                    completedBytes_-=static_cast<std::size_t>(it->second.image.sizeInBytes());retired=std::move(it->second);completed_.erase(it);
                }
                if(static_cast<std::size_t>(result.sizeInBytes())>maximumQueuedBytes-completedBytes_){
                    result={};error=QStringLiteral("The completed figure images exceed the 256 MiB display budget.");
                }
                completedBytes_+=static_cast<std::size_t>(result.sizeInBytes());
                completed_.emplace(work.owner,Completed{std::move(result),std::move(error),work.task.cancelled,std::move(work.completion)});
                if(!deliveryQueued_){deliveryQueued_=true;schedule=true;}
            }
            if(schedule)QMetaObject::invokeMethod(this,[this]{deliver();},Qt::QueuedConnection);
        }
    }
    void deliver(){
        std::map<std::uint64_t,Completed> completed;
        {std::lock_guard lock(mutex_);completed.swap(completed_);completedBytes_=0;deliveryQueued_=false;}
        for(auto& [id,result]:completed){
            (void)id;
            if(result.cancelled&&result.cancelled->load())continue;
            if(result.completion)result.completion(std::move(result.image),std::move(result.error));
        }
    }
    std::mutex mutex_;std::condition_variable available_;bool stopping_=false,deliveryQueued_=false;
    std::map<std::uint64_t,Work> pending_;std::deque<std::uint64_t> order_;std::size_t bytes_=0,completedBytes_=0;
    std::map<std::uint64_t,Completed> completed_;
    std::shared_ptr<std::atomic_bool> activeCancel_;std::uint64_t activeOwner_=0;std::thread thread_;
};

QPointer<Service> service;
Service* renderer(){
    Q_ASSERT(qApp&&QThread::currentThread()==qApp->thread());
    if(!service){service=new Service(qApp);qAddPostRoutine(shutdownRasterPlotRenderer);
        QObject::connect(qApp,&QCoreApplication::aboutToQuit,service,[]{shutdownRasterPlotRenderer();});}
    return service;
}
}
void shutdownRasterPlotRenderer(){qRemovePostRoutine(shutdownRasterPlotRenderer);if(service){auto* old=service.data();service=nullptr;old->stop();delete old;}}
RasterPlotCanvas::RasterPlotCanvas(QWidget* parent):QWidget(parent){
    static std::atomic<std::uint64_t> next{1};owner_=next.fetch_add(1);
    setAttribute(Qt::WA_OpaquePaintEvent);setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);
    setProperty("pentaRasterCanvas",true);
    timer_=new QTimer(this);timer_->setSingleShot(true);timer_->setInterval(0);
    connect(timer_,&QTimer::timeout,this,[this]{capture();});
}
RasterPlotCanvas::~RasterPlotCanvas(){closing_=true;timer_->stop();if(cancelled_)cancelled_->store(true);if(service)service->cancel(owner_);provider_={};cachedDisplayBytes-=static_cast<std::size_t>(front_.sizeInBytes());}
void RasterPlotCanvas::setSceneProvider(Provider provider){provider_=std::move(provider);requestScene();}
QSize RasterPlotCanvas::pixelSize()const{const double dpr=devicePixelRatioF();return {qCeil(width()*dpr),qCeil(height()*dpr)};}
bool RasterPlotCanvas::frameCurrent()const{return !front_.isNull()&&committed_==generation_&&committedSize_==pixelSize()&&qFuzzyCompare(committedDpr_,devicePixelRatioF());}
void RasterPlotCanvas::requestScene(bool resizeOnly){
    if(closing_||!provider_)return;
    ++generation_;if(cancelled_)cancelled_->store(true);pending_=false;
    timer_->start(resizeOnly?80:0);update();
}
void RasterPlotCanvas::capture(){
    if(closing_||!provider_||!isVisible())return;
    const auto pixels=pixelSize();const double dpr=devicePixelRatioF();
    if(pixels.width()<2||pixels.height()<2||pixels.width()>16000||pixels.height()>16000||qint64(pixels.width())*pixels.height()>16000000){error_=QStringLiteral("Plot dimensions exceed the display budget.");pending_=false;update();return;}
    RasterPlotTask task;
    try{task=provider_(pixels,dpr);}
    catch(const std::exception& error){pending_=false;error_=QString::fromUtf8(error.what());update();return;}
    catch(...){pending_=false;error_=tr("Cannot capture the figure scene.");update();return;}
    if(!task.paint){pending_=false;update();return;}
    cancelled_=task.cancelled?task.cancelled:std::make_shared<std::atomic_bool>(false);task.cancelled=cancelled_;
    const auto generation=generation_;QPointer<RasterPlotCanvas> self=this;pending_=true;
    if(!renderer()->submit({owner_,generation,pixels,dpr,std::move(task),[self,generation,pixels,dpr](QImage image,QString error){if(self)self->accept(generation,pixels,dpr,std::move(image),std::move(error));}})){
        pending_=false;error_=QStringLiteral("The queued figure scenes exceed the 256 MiB display budget.");update();}
}
void RasterPlotCanvas::accept(std::uint64_t generation,QSize pixels,double dpr,QImage image,QString error){
    if(closing_||generation!=generation_||pixels!=pixelSize()||!qFuzzyCompare(dpr,devicePixelRatioF()))return;
    pending_=false;error_=std::move(error);
    if(!image.isNull()){
        const auto oldBytes=static_cast<std::size_t>(front_.sizeInBytes());
        const auto newBytes=static_cast<std::size_t>(image.sizeInBytes());
        if(newBytes>maximumQueuedBytes-(cachedDisplayBytes-oldBytes))error_=tr("Visible figure images exceed the 256 MiB display cache budget.");
        else{cachedDisplayBytes=cachedDisplayBytes-oldBytes+newBytes;image.setDevicePixelRatio(dpr);front_=std::move(image);committed_=generation;committedSize_=pixels;committedDpr_=dpr;}
    }
    setProperty("pentaRenderError",error_);
    update();emit frameReady();
}
void RasterPlotCanvas::paintEvent(QPaintEvent*){
    QPainter painter(this);painter.fillRect(rect(),Qt::white);
    if(!front_.isNull())painter.drawImage(rect(),front_);
    else{painter.setPen(QColor(90,90,90));painter.drawText(rect().adjusted(12,12,-12,-12),Qt::AlignCenter|Qt::TextWordWrap,error_.isEmpty()?tr("Rendering figure…"):error_);}
    if(!front_.isNull()&&!error_.isEmpty()){
        const QRect area=rect().adjusted(8,8,-8,-8);
        const QRect text=painter.boundingRect(area,Qt::AlignBottom|Qt::TextWordWrap,error_);
        painter.fillRect(text.adjusted(-4,-4,4,4),QColor(255,255,255,240));
        painter.setPen(QColor(160,25,25));painter.drawText(area,Qt::AlignBottom|Qt::TextWordWrap,error_);
    }
}
void RasterPlotCanvas::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);requestScene(true);}
void RasterPlotCanvas::showEvent(QShowEvent* event){QWidget::showEvent(event);if(!frameCurrent())requestScene(true);}
void RasterPlotCanvas::hideEvent(QHideEvent* event){
    timer_->stop();if(cancelled_)cancelled_->store(true);if(service)service->cancel(owner_);
    pending_=false;cachedDisplayBytes-=static_cast<std::size_t>(front_.sizeInBytes());front_={};committedSize_={};QWidget::hideEvent(event);
}
bool RasterPlotCanvas::event(QEvent* event){
    bool displayChanged=event->type()==QEvent::ScreenChangeInternal;
#if QT_VERSION >= QT_VERSION_CHECK(6,6,0)
    displayChanged=displayChanged||event->type()==QEvent::DevicePixelRatioChange;
#endif
    if(displayChanged)requestScene(true);
    return QWidget::event(event);
}
QImage RasterPlotCanvas::grabFramebuffer(){
    if(!provider_)return {};
    if(frameCurrent())return front_;
    try{
        auto task=provider_(pixelSize(),devicePixelRatioF());if(!task.paint)return {};
        QImage result=task.paint();if(!result.isNull())result.setDevicePixelRatio(devicePixelRatioF());return result;
    }catch(const std::exception& error){error_=QString::fromUtf8(error.what());}
    catch(...){error_=tr("Cannot export the figure image.");}
    return {};
}
std::size_t RasterPlotCanvas::pendingSceneCount(){return service?service->count():0;}
}
