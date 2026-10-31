// Adapted from GTG Cell Studio's modeless FindReplaceDialog.
#pragma once
#include <QDialog>
#include <QPointer>
class QSettings;
class QHideEvent;
class QMoveEvent;
class QResizeEvent;
namespace penta {
class FindReplaceBar;
class EditorWidget;
class FindReplaceDialog final:public QDialog {
    Q_OBJECT
public:
    explicit FindReplaceDialog(QWidget* parent=nullptr,QSettings* settings=nullptr);
    ~FindReplaceDialog()override;
    FindReplaceBar* bar()const{return m_bar;}
    void showFind(EditorWidget*,const QString& sourceName={},bool replace=false);
    void followEditor(EditorWidget*,const QString& sourceName={});
    void reject()override;
protected:
    bool event(QEvent*)override;
    void moveEvent(QMoveEvent*)override;
    void resizeEvent(QResizeEvent*)override;
    void hideEvent(QHideEvent*)override;
private:
    void rememberGeometry();
    void savePreferences();
    FindReplaceBar* m_bar=nullptr;
    QPointer<QSettings> m_settings;
    QByteArray m_geometry;
    bool m_shown=false,m_closing=false,m_focusSearchPending=false;
};
}
