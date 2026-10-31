// Adapted from GTG Cell Studio; user-authorized GUI refinement,31 October2026.
#include "ide/desktop/parallel_pool_indicator.hpp"
#include <QHideEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QTimer>
#include <algorithm>

namespace penta::desktop {
ParallelPoolIndicator::ParallelPoolIndicator(QWidget* parent):QWidget(parent){
    setObjectName("parallelPoolIndicator");setFocusPolicy(Qt::NoFocus);setFixedSize(sizeHint());
    pulseTimer_=new QTimer(this);pulseTimer_->setObjectName("parallelExecutionPulse");pulseTimer_->setInterval(450);pulseTimer_->setTimerType(Qt::CoarseTimer);
    connect(pulseTimer_,&QTimer::timeout,this,[this]{if(!executing()||!isVisible()){refreshAnimation();return;}pulseBright_=!pulseBright_;update();});refresh();
}
void ParallelPoolIndicator::beginRequest(quint64 id,const QString& phase,bool execution){if(id<request_)return;request_=id;running_=true;workers_=0;stopping_=false;execution_=execution;pulseBright_=false;reservation_=false;phase_=phase;refresh();}
void ParallelPoolIndicator::setWorkers(quint64 id,std::size_t count,const QString& phase){
    if(!running_||id!=request_||count>4096)return;
    if(workers_==count&&phase_==phase&&!reservation_)return;
    workers_=count;phase_=phase;reservation_=false;refresh();
}
void ParallelPoolIndicator::setWorkerReservation(quint64 id,std::size_t count,const QString& phase){
    if(!running_||id!=request_||count>4096)return;
    if(workers_==count&&phase_==phase&&reservation_)return;
    workers_=count;phase_=phase;reservation_=true;refresh();
}
void ParallelPoolIndicator::finishRequest(quint64 id){if(id!=request_)return;running_=false;workers_=0;stopping_=false;execution_=false;phase_.clear();refresh();}
void ParallelPoolIndicator::setStopping(quint64 id){if(id!=request_||!running_)return;stopping_=true;refresh();}
QColor ParallelPoolIndicator::indicatorColor()const{if(!executing())return QColor(130,141,151);if(stopping_)return pulseBright_?QColor(205,143,36):QColor(165,112,34);return pulseBright_?QColor(0,155,175):QColor(35,112,179);}
QSize ParallelPoolIndicator::sizeHint()const{return {52,18};}
bool ParallelPoolIndicator::animationRunning()const{return pulseTimer_->isActive();}
void ParallelPoolIndicator::refreshAnimation(){if(executing()&&isVisible()){if(!pulseTimer_->isActive())pulseTimer_->start();}else{pulseTimer_->stop();pulseBright_=false;}}
void ParallelPoolIndicator::showEvent(QShowEvent* event){QWidget::showEvent(event);refreshAnimation();}
void ParallelPoolIndicator::hideEvent(QHideEvent* event){pulseTimer_->stop();pulseBright_=false;QWidget::hideEvent(event);}
void ParallelPoolIndicator::refresh(){
    QString description;
    if(!running_)description="Parallel workers idle";
    else if(reservation_)description=QStringLiteral("Up to %1 native BLAS worker slots reserved").arg(workers_);
    else if(workers_)description=workers_==1?QStringLiteral("1 worker (serial)"):QStringLiteral("%1 parallel workers in use").arg(workers_);
    else description="Parallel worker count has not been reported for this stage";
    if(running_&&!phase_.isEmpty())description+="\n"+phase_;
    if(stopping_)description+="\nStopping — waiting for workers to finish";
    setToolTip(description);setAccessibleName(description.section('\n',0,0));setAccessibleDescription(description);setProperty("parallelActive",active());setProperty("executionActive",executing());refreshAnimation();update();
}
void ParallelPoolIndicator::paintEvent(QPaintEvent*){
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);const auto color=indicatorColor();
    // Fit the original symbol proportionally in a text-height status bar.
    // Logical coordinates keep all node strokes inside the icon at any DPR.
    painter.save();const double scale=std::min(24/28.,height()/25.);painter.translate((24-28*scale)/2,(height()-25*scale)/2);painter.scale(scale,scale);
    const QPointF center(13,12.5);const QPointF nodes[]={{5,5.5},{21,5.5},{5,19.5},{21,19.5}};
    painter.setPen(QPen(color.lighter(executing()?130:120),1.3));for(const auto& node:nodes)painter.drawLine(center,node);
    QLinearGradient fill(0,2,0,23);fill.setColorAt(0,color.lighter(executing()?135:120));fill.setColorAt(1,color.darker(110));painter.setBrush(fill);painter.setPen(QPen(color.darker(115),.8));
    for(const auto& node:nodes)painter.drawRoundedRect(QRectF(node.x()-2.6,node.y()-2.6,5.2,5.2),1.1,1.1);
    painter.drawEllipse(center,3.4,3.4);painter.restore();painter.setPen(color.darker(115));
    painter.drawText(QRect(26,0,width()-26,height()),Qt::AlignVCenter|Qt::AlignLeft,(reservation_?QStringLiteral("≤"):QString())+QString::number(workers_));
}
}
