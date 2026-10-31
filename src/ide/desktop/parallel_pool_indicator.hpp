// Adapted from GTG Cell Studio; user-authorized GUI refinement,31 October2026.
#pragma once
#include <QColor>
#include <QString>
#include <QWidget>
#include <cstddef>
class QTimer;class QShowEvent;class QHideEvent;

namespace penta::desktop {
// Request-scoped execution pulse with worker evidence kept in its tooltip.
// active() still reports only observed parallel workers; an executing serial
// model pulses too without claiming that a parallel pool exists.
class ParallelPoolIndicator final:public QWidget {
public:
    explicit ParallelPoolIndicator(QWidget* parent=nullptr);
    void beginRequest(quint64,const QString& phase,bool execution=true);
    void setWorkers(quint64,std::size_t,const QString& phase);
    void setWorkerReservation(quint64,std::size_t,const QString& phase);
    void finishRequest(quint64);
    void setStopping(quint64);
    bool active()const{return running_&&workers_>1&&!reservation_;}
    bool executing()const{return running_&&execution_;}
    bool animationRunning()const;
    bool stopping()const{return stopping_;}
    std::size_t workers()const{return workers_;}
    quint64 request()const{return request_;}
    QColor indicatorColor()const;
    QSize sizeHint()const override;
protected:
    void paintEvent(QPaintEvent*)override;
    void showEvent(QShowEvent*)override;
    void hideEvent(QHideEvent*)override;
private:
    void refresh();
    void refreshAnimation();
    quint64 request_=0;
    std::size_t workers_=0;
    bool running_=false,stopping_=false,execution_=false,pulseBright_=false,reservation_=false;
    QTimer* pulseTimer_=nullptr;
    QString phase_;
};
}
