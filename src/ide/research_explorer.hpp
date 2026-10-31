#pragma once
#include "research/script_builder.hpp"
#include "runtime/interpreter.hpp"
#include <QWidget>
class QCheckBox;class QComboBox;class QDoubleSpinBox;class QLabel;class QLineEdit;class QPlainTextEdit;class QPushButton;class QSlider;class QSpinBox;class QTableWidget;class QTabWidget;
namespace penta {
class ResearchSpatialControls;
class ResearchExplorer final:public QWidget {
    Q_OBJECT
public:
    explicit ResearchExplorer(QWidget* parent=nullptr);
    QString generatedScript(QString* error=nullptr,bool preflightOnly=false)const;
    QString exportCommand(const QString& path,QString* error=nullptr)const;
    QString replayCommand(QString* error=nullptr)const;
    bool saveScript(const QString& path,QString* error=nullptr,bool overwrite=false,
                    const QByteArray& expectedDiskHash={})const;
    research::Configuration configuration()const;
    bool importDataFile(const QString& path,QString* error=nullptr);
    void setConfiguration(const research::Configuration&);
    void setExecutionBusy(bool);
    void acceptResult(const runtime::ExecutionResult&);
    void submissionRejected(const QString&);
    bool resultFresh()const{return m_fresh;}
    QString completedScript()const{return m_completedSource;}
signals:
    void runRequested(const QString& source);
    void exportRequested(const QString& command);
    void cancelRequested();
    void scriptSaved(const QString& path);
private:
    ResearchSpatialControls* m_spatial=nullptr;
    QTabWidget* m_inputTabs=nullptr;
    QComboBox *m_mode=nullptr,*m_method=nullptr,*m_fitType=nullptr,*m_fitMethod=nullptr,*m_filter=nullptr,*m_sliderParameter=nullptr;
    QDoubleSpinBox *m_gamma=nullptr,*m_t0=nullptr,*m_t1=nullptr,*m_step=nullptr,*m_seconds=nullptr,*m_transient=nullptr,*m_qr=nullptr,*m_lower=nullptr,*m_upper=nullptr,*m_sliderLow=nullptr,*m_sliderHigh=nullptr;
    QLineEdit *m_rtol=nullptr,*m_atol=nullptr,*m_fitExpression=nullptr,*m_xdata=nullptr,*m_ydata=nullptr,*m_sweep=nullptr,*m_checkpoint=nullptr;
    QPlainTextEdit *m_initialRows=nullptr,*m_preview=nullptr,*m_results=nullptr;
    QCheckBox *m_resume=nullptr,*m_overwrite=nullptr,*m_livePlots=nullptr;
    QSpinBox *m_threads=nullptr,*m_iterations=nullptr,*m_samples=nullptr,*m_maxRuns=nullptr,*m_replay=nullptr;
    QSlider* m_parameterSlider=nullptr;
    QTableWidget *m_states=nullptr,*m_parameters=nullptr,*m_sweeps=nullptr,*m_algebras=nullptr;
    QPushButton *m_run=nullptr,*m_preflight=nullptr,*m_cancel=nullptr,*m_export=nullptr,*m_replayButton=nullptr,*m_filterButton=nullptr;
    QLabel *m_status=nullptr,*m_sliderHint=nullptr;
    bool m_loading=false,m_busy=false,m_fresh=false,m_pendingPreflight=false;
    QString m_pendingHash,m_pendingAuxHash,m_completedHash,m_completedSource,m_pendingSource;
    research::Configuration m_submitted,m_completed;
    std::vector<language::Symbol> m_completedSymbols;
    void refreshParameterSlider(bool rebuild=true);
    void markChanged();
    void updateButtons();
    void submit(bool preflight);
    void submitAux(const QString&);
    QString checkedAux(const QString&,QString*)const;
};
}
