// Presentation adapted from GTG Cell Studio's HelpMathBrowser.
#pragma once
#include "copilot_knowledge.hpp"
#include <QTextBrowser>
#include <QHash>
#include <QImage>
class QTimer;
namespace penta::assistant {
class HelpArticleBrowser final:public QTextBrowser {
public:
    explicit HelpArticleBrowser(QWidget* parent=nullptr);
    void showReference(const CopilotReference&,const QList<CopilotReference>& catalogue);
protected:
    QVariant loadResource(int,const QUrl&)override;
    QMimeData* createMimeDataFromSelection()const override;
    void resizeEvent(QResizeEvent*)override;
private:
    void render(bool preserveView);
    QString inlineHtml(const QString&);
    QString bodyHtml(const QString&);
    QString mathHtml(const QString&,bool display=false);
    CopilotReference m_reference;
    QList<CopilotReference> m_catalogue;
    QHash<QString,QImage> m_images;
    QHash<QString,QString> m_sources;
    QTimer* m_resize=nullptr;
    qint64 m_imageBytes=0;
};
}
