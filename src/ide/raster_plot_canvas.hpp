#pragma once
#include <QImage>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <atomic>
#include <functional>
#include <memory>

class QTimer;
namespace penta {
// Immutable CPU-only work. The provider is invoked on the GUI thread; the work
// never accesses a QWidget or live scene. One queued scene per canvas is kept.
struct RasterPlotTask {
    std::function<QImage()> paint;
    std::shared_ptr<std::atomic_bool> cancelled;
    std::size_t estimatedBytes=0;
};
class RasterPlotCanvas:public QWidget {
    Q_OBJECT
public:
    explicit RasterPlotCanvas(QWidget* parent=nullptr);
    ~RasterPlotCanvas()override;
    using Provider=std::function<RasterPlotTask(QSize,double)>;
    void setSceneProvider(Provider);
    void requestScene(bool resizeOnly=false);
    bool isValid()const{return bool(provider_);}
    bool frameCurrent()const;
    bool renderPending()const{return pending_||timer_->isActive();}
    QImage grabFramebuffer(); // Explicit CPU export, never an OpenGL readback.
    QImage cachedImage()const{return front_;}
    QString renderError()const{return error_;}
    static std::size_t pendingSceneCount();
signals:
    void frameReady();
protected:
    void paintEvent(QPaintEvent*)override;
    void resizeEvent(QResizeEvent*)override;
    void showEvent(QShowEvent*)override;
    void hideEvent(QHideEvent*)override;
    bool event(QEvent*)override;
private:
    void capture();
    void accept(std::uint64_t,QSize,double,QImage,QString);
    QSize pixelSize()const;
    Provider provider_;
    QTimer* timer_=nullptr;
    QImage front_;
    QSize committedSize_;
    double committedDpr_=1;
    std::uint64_t owner_=0,generation_=0,committed_=0;
    bool pending_=false,closing_=false;
    QString error_;
    std::shared_ptr<std::atomic_bool> cancelled_;
};
void shutdownRasterPlotRenderer();
}
