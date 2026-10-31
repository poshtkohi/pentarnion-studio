// Adapted from GTG Cell Studio; user-authorized GUI refinement,31 October2026.
#pragma once
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <functional>
class QAction;
class QDialog;
class QLabel;
class QWidget;
namespace penta::desktop {
// Application-local pixels only: never reads another application or screen.
class SnapshotController final:public QObject {
    Q_OBJECT
public:
    explicit SnapshotController(QWidget* owner);
    ~SnapshotController()override;
    static constexpr qint64 PixelLimit=32*1024*1024;
    QAction* regionAction()const{return regionAction_;}
    QAction* windowAction()const{return windowAction_;}
    const QImage& latestImage()const{return latest_;}
    static QImage cropImage(const QImage&,const QRectF& logicalRectangle);
    void beginRegion(QWidget* target=nullptr);
    void captureWindow(QWidget* target=nullptr);
    void captureWidget(QWidget* target);
    bool saveImage(const QString& path,QString* error=nullptr)const;
    void copyImage();
    std::function<void(const QString&,bool)> feedback;
signals:
    void snapshotCaptured(const QImage&);
protected:
    bool eventFilter(QObject*,QEvent*)override;
private:
    bool ownsWidget(const QWidget*)const;
    QWidget* currentWindow()const;
    QImage grab(QWidget*);
    void acceptImage(QImage,QWidget* parent);
    void showPreview(QWidget* parent);
    void notify(const QString&,bool error=false);
    QPointer<QWidget> owner_,overlay_;
    QPointer<QDialog> preview_;
    QPointer<QLabel> previewImage_;
    QImage latest_;
    bool replacingSelection_=false;
    QAction *regionAction_=nullptr,*windowAction_=nullptr;
};
}
