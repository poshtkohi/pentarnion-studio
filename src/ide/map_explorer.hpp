#pragma once
#include <QWidget>
class QComboBox;class QDoubleSpinBox;class QLabel;class QLineEdit;class QPlainTextEdit;class QSpinBox;class QTableWidget;
namespace penta {
class MapExplorer final:public QWidget {
    Q_OBJECT
public:
    explicit MapExplorer(QWidget* parent=nullptr);
    QString generatedScript(QString* error=nullptr)const;
    QString exportCommand(const QString& path,QString* error=nullptr)const;
    void resetDefaults();
signals:
    void runRequested(const QString& source);
    void exportRequested(const QString& command);
    void scriptSaved(const QString& path);
private:
    QComboBox* m_type=nullptr;
    QComboBox* m_x=nullptr;QComboBox* m_y=nullptr;QComboBox* m_z=nullptr;
    QDoubleSpinBox* m_gamma=nullptr;
    QLineEdit* m_expression=nullptr;QLineEdit* m_initial=nullptr;
    QSpinBox* m_iterations=nullptr;QSpinBox* m_transient=nullptr;QSpinBox* m_stride=nullptr;
    QTableWidget* m_parameters=nullptr;
    QPlainTextEdit* m_preview=nullptr;QLabel* m_status=nullptr;
    void chooseExample();
    void generatePreview();
};
}
