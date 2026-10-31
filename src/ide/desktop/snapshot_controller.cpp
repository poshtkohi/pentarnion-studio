// Adapted from GTG Cell Studio; user-authorized GUI refinement,31 October2026.
#include "ide/desktop/snapshot_controller.hpp"
#include "ide/desktop/studio_icons.hpp"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace penta::desktop {
namespace {
class SnapshotSelection final:public QWidget {
public:
    SnapshotSelection(QWidget* target,QImage image,std::function<void(QRectF)> selected):
        QWidget(target),image_(std::move(image)),selected_(std::move(selected)){
        setObjectName("studioSnapshotSelection");setAttribute(Qt::WA_DeleteOnClose);setAttribute(Qt::WA_TranslucentBackground,false);
        setProperty("studioSnapshotInteraction",true);
        setFocusPolicy(Qt::StrongFocus);setCursor(Qt::CrossCursor);setMouseTracking(true);
        // A child overlay needs no desktop coordinates or screen-capture
        // permission, including when Studio runs on Wayland.
        setGeometry(target->rect());
        setAccessibleName("Select snapshot area");setAccessibleDescription("Drag a rectangle anywhere in this Studio window. Escape cancels.");
    }
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter painter(this);painter.drawImage(rect(),image_);painter.fillRect(rect(),QColor(11,36,45,75));
        const auto area=selection_.normalized().intersected(QRectF(rect()));
        if(area.width()>0&&area.height()>0){painter.save();painter.setClipRect(area);painter.drawImage(rect(),image_);painter.restore();painter.setPen(QPen(QColor("#15b8c7"),1.5));painter.setBrush(Qt::NoBrush);painter.drawRect(area.adjusted(.5,.5,-.5,-.5));}
        const QString caption=dragging_?QString("%1 × %2 · release to copy").arg(qRound(area.width())).arg(qRound(area.height())):QString("Drag to select any area · Esc to cancel");
        const auto textRect=QRect(12,12,std::min(width()-24,440),30);painter.fillRect(textRect,QColor(10,62,73,235));painter.setPen(Qt::white);painter.drawText(textRect.adjusted(9,0,-5,0),Qt::AlignVCenter,caption);
    }
    void mousePressEvent(QMouseEvent* event)override{if(event->button()==Qt::LeftButton){origin_=event->position();selection_=QRectF(origin_,origin_);dragging_=true;update();}else if(event->button()==Qt::RightButton)close();}
    void mouseMoveEvent(QMouseEvent* event)override{if(dragging_){selection_=QRectF(origin_,event->position());update();}}
    void mouseReleaseEvent(QMouseEvent* event)override{
        if(event->button()!=Qt::LeftButton||!dragging_)return;
        selection_=QRectF(origin_,event->position()).normalized().intersected(QRectF(rect()));dragging_=false;
        if(selection_.width()<2||selection_.height()<2){update();return;}
        const auto callback=selected_;const auto area=selection_;hide();if(callback)callback(area);close();
    }
    void keyPressEvent(QKeyEvent* event)override{if(event->key()==Qt::Key_Escape){close();event->accept();}else QWidget::keyPressEvent(event);}
private:
    QImage image_;std::function<void(QRectF)> selected_;QPointF origin_;QRectF selection_;bool dragging_=false;
};
bool snapshotKey(const QKeyEvent* key){return key->key()==Qt::Key_S&&key->modifiers()==(Qt::ControlModifier|Qt::AltModifier);}
}
SnapshotController::SnapshotController(QWidget* owner):QObject(owner),owner_(owner){
    setObjectName("snapshotController");
    regionAction_=new QAction(studioCommandIcon("Snapshot","editSnapshot"),"Snapshot",this);regionAction_->setObjectName("editSnapshot");regionAction_->setShortcut(QKeySequence("Ctrl+Alt+S"));regionAction_->setShortcutContext(Qt::ApplicationShortcut);regionAction_->setToolTip("Drag a rectangle anywhere in this Studio window, then copy or save it (Ctrl+Alt+S)");
    windowAction_=new QAction(studioCommandIcon("Snapshot","snapshotWindow"),"Snapshot whole window",this);windowAction_->setObjectName("snapshotWindow");windowAction_->setToolTip("Copy an image of the active Studio window");
    connect(regionAction_,&QAction::triggered,this,[this]{QPointer<QWidget> target=currentWindow();QTimer::singleShot(0,this,[this,target]{if(target)beginRegion(target);});});
    connect(windowAction_,&QAction::triggered,this,[this]{QPointer<QWidget> target=currentWindow();QTimer::singleShot(0,this,[this,target]{if(target)captureWindow(target);});});
    qApp->installEventFilter(this);
}
SnapshotController::~SnapshotController(){if(qApp)qApp->removeEventFilter(this);if(overlay_)delete overlay_.data();if(preview_)delete preview_.data();}
bool SnapshotController::ownsWidget(const QWidget* target)const{
    for(auto* widget=target;widget;widget=widget->parentWidget())if(widget==owner_)return true;
    return false;
}
QWidget* SnapshotController::currentWindow()const{
    QWidget* window=QApplication::activeModalWidget();
    if(!window)window=QApplication::activeWindow();
    if(window&&!ownsWidget(window))return nullptr;
    if(!window||qobject_cast<QMenu*>(window)||window->windowType()==Qt::Popup)window=owner_;
    return window?window->window():nullptr;
}
void SnapshotController::notify(const QString& text,bool error){if(feedback)feedback(text,error);}
QImage SnapshotController::grab(QWidget* target){
    if(!ownsWidget(target)){notify("Choose a Studio window or panel for the snapshot.",true);return {};}
    if(!target||!target->isVisible()){notify("Choose a visible Studio window or panel for the snapshot.",true);return {};}
    const qreal ratio=target->devicePixelRatioF();const qint64 width=qCeil(target->width()*ratio),height=qCeil(target->height()*ratio);
    if(width<=0||height<=0||width>PixelLimit/std::max<qint64>(1,height)){notify("The snapshot exceeds the 32-megapixel limit. Reduce the window size and try again.",true);return {};}
    auto image=target->grab().toImage();if(image.isNull())notify("Studio could not capture this panel.",true);return image;
}
QImage SnapshotController::cropImage(const QImage& image,const QRectF& rectangle){
    if(image.isNull()||!rectangle.normalized().isValid()||!std::isfinite(rectangle.x())||!std::isfinite(rectangle.y())||!std::isfinite(rectangle.width())||!std::isfinite(rectangle.height()))return {};
    const qreal ratio=image.devicePixelRatio();const QRectF logicalBounds(0,0,image.width()/ratio,image.height()/ratio);const auto area=rectangle.normalized().intersected(logicalBounds);
    if(area.isEmpty())return {};
    const int left=std::max(0,int(std::floor(area.left()*ratio))),top=std::max(0,int(std::floor(area.top()*ratio)));
    const int right=std::min(image.width(),int(std::ceil((area.x()+area.width())*ratio))),bottom=std::min(image.height(),int(std::ceil((area.y()+area.height())*ratio)));
    auto cropped=image.copy(QRect(left,top,right-left,bottom-top));cropped.setDevicePixelRatio(ratio);return cropped;
}
void SnapshotController::beginRegion(QWidget* target){
    target=target?target->window():currentWindow();if(!target)return;
    if(!ownsWidget(target)){notify("Choose a Studio window or panel for the snapshot.",true);return;}
    if(overlay_){
        if(overlay_->parentWidget()==target){overlay_->raise();overlay_->setFocus(Qt::OtherFocusReason);return;}
        // Moving to another Studio window replaces the old selection. It must
        // not steal focus back to the earlier window or capture its overlay.
        replacingSelection_=true;delete overlay_.data();replacingSelection_=false;
    }
    const auto image=grab(target);if(image.isNull())return;
    const bool popup=target->windowType()==Qt::Popup;QWidget* stable=target;
    if(popup){stable=QApplication::activeModalWidget();for(auto* ancestor=target->parentWidget();!stable&&ancestor;ancestor=ancestor->parentWidget())if(ancestor->isWindow()&&ancestor->windowType()!=Qt::Popup)stable=ancestor;if(!stable)stable=owner_;}
    if(preview_&&(stable==preview_||preview_->isAncestorOf(stable)))stable=preview_->parentWidget()?preview_->parentWidget():owner_.data();
    const QPointer<QWidget> parent=stable,origin=target,focus=QApplication::focusWidget();
    auto* selection=new SnapshotSelection(target,image,[this,image,parent,origin,popup](const QRectF& area){const auto result=cropImage(image,area);if(popup&&origin)origin->close();QTimer::singleShot(0,this,[this,result,parent]{if(parent)acceptImage(result,parent);});});overlay_=selection;
    connect(selection,&QObject::destroyed,this,[this,origin,focus]{overlay_.clear();if(!replacingSelection_&&(!preview_||!preview_->isVisible())){if(origin&&origin->isVisible())origin->activateWindow();if(focus&&focus->isVisible())focus->setFocus(Qt::OtherFocusReason);}});
    selection->show();selection->raise();selection->activateWindow();selection->setFocus(Qt::OtherFocusReason);
}
void SnapshotController::captureWindow(QWidget* target){captureWidget(target?target->window():currentWindow());}
void SnapshotController::captureWidget(QWidget* target){auto image=grab(target);if(!image.isNull())acceptImage(std::move(image),target?target->window():owner_.data());}
void SnapshotController::acceptImage(QImage image,QWidget* parent){
    if(image.isNull())return;
    if(preview_&&(parent==preview_||preview_->isAncestorOf(parent)))parent=preview_->parentWidget()?preview_->parentWidget():owner_.data();
    latest_=std::move(image);copyImage();showPreview(parent);emit snapshotCaptured(latest_);
}
void SnapshotController::copyImage(){if(latest_.isNull())return;QApplication::clipboard()->setImage(latest_);notify("Snapshot copied. Paste it into another application, or save it as PNG.");}
bool SnapshotController::saveImage(const QString& path,QString* error)const{
    const auto fail=[&](const QString& text){if(error)*error=text;return false;};if(latest_.isNull())return fail("Take a snapshot first.");if(QFileInfo(path).suffix().compare("png",Qt::CaseInsensitive)!=0)return fail("Choose a .png file for the snapshot.");
    QSaveFile file(path);file.setDirectWriteFallback(false);if(!file.open(QIODevice::WriteOnly))return fail(file.errorString());if(!latest_.save(&file,"PNG")){file.cancelWriting();return fail("The snapshot could not be encoded as PNG.");}if(!file.commit())return fail(file.errorString());return true;
}
void SnapshotController::showPreview(QWidget* parent){
    if(preview_&&preview_->parentWidget()!=parent){delete preview_.data();previewImage_.clear();}
    if(!preview_){
        auto* dialog=new QDialog(parent,Qt::Window);dialog->setObjectName("snapshotPreview");dialog->setWindowTitle("Snapshot");dialog->setModal(false);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowIcon(studioCommandIcon("Snapshot","snapshotPreview"));preview_=dialog;
        dialog->setProperty("studioSnapshotInteraction",true);
        auto* layout=new QVBoxLayout(dialog);auto* scroll=new QScrollArea(dialog);scroll->setObjectName("snapshotScroll");scroll->setAlignment(Qt::AlignCenter);previewImage_=new QLabel(scroll);previewImage_->setObjectName("snapshotImage");scroll->setWidget(previewImage_);layout->addWidget(scroll,1);
        auto* note=new QLabel("Copied to clipboard · Paste into Word or another application",dialog);note->setObjectName("snapshotStatus");note->setWordWrap(true);layout->addWidget(note);
        auto* buttons=new QDialogButtonBox(dialog);auto* copy=buttons->addButton("Copy",QDialogButtonBox::ActionRole);copy->setObjectName("snapshotCopy");copy->setIcon(studioCommandIcon("Copy","snapshotCopy"));connect(copy,&QPushButton::clicked,this,&SnapshotController::copyImage);
        auto* save=buttons->addButton("Save PNG…",QDialogButtonBox::ActionRole);save->setObjectName("snapshotSave");save->setIcon(studioCommandIcon("Save","snapshotSave"));
        connect(save,&QPushButton::clicked,this,[this]{if(!preview_)return;const auto name=QFileDialog::getSaveFileName(preview_,"Save snapshot","Snapshot.png","PNG image (*.png)");if(name.isEmpty())return;const auto path=QFileInfo(name).suffix().isEmpty()?name+".png":name;QString error;if(!saveImage(path,&error)){if(auto* label=preview_->findChild<QLabel*>("snapshotStatus"))label->setText(error);notify(error,true);}else{if(auto* label=preview_->findChild<QLabel*>("snapshotStatus"))label->setText("Saved to "+path);notify("Snapshot saved to "+path);}});
        auto* close=buttons->addButton(QDialogButtonBox::Close);close->setObjectName("snapshotClose");close->setIcon(studioCommandIcon("Close","snapshotClose"));connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);layout->addWidget(buttons);
        QSize available(1000,760);if(auto* screen=parent?parent->screen():QApplication::primaryScreen())available=screen->availableGeometry().size()-QSize(60,80);
        dialog->resize(std::min(900,std::max(320,available.width())),std::min(680,std::max(240,available.height())));
    }
    previewImage_->setPixmap(QPixmap::fromImage(latest_));previewImage_->resize(latest_.deviceIndependentSize().toSize());preview_->show();preview_->raise();preview_->activateWindow();
}
bool SnapshotController::eventFilter(QObject* watched,QEvent* event){
    if(event->type()!=QEvent::ShortcutOverride&&event->type()!=QEvent::KeyPress)return false;
    auto* key=static_cast<QKeyEvent*>(event);
    auto* widget=qobject_cast<QWidget*>(watched);if(!widget)widget=QApplication::focusWidget();
    if(!ownsWidget(widget))return false;
    if(key->key()==Qt::Key_C&&key->modifiers()==Qt::ControlModifier){
        const bool inPreview=preview_&&widget->window()==preview_;
        const bool inSelection=overlay_&&(widget==overlay_||overlay_->isAncestorOf(widget));
        if(inPreview||inSelection){if(event->type()==QEvent::KeyPress&&inPreview)copyImage();key->accept();return true;}
    }
    if(!snapshotKey(key))return false;
    if(event->type()==QEvent::ShortcutOverride){key->accept();return true;}
    if(!key->isAutoRepeat())beginRegion(QApplication::activePopupWidget());
    key->accept();return true;
}
}
