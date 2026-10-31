#pragma once
#include "ide/assistant/copilot_knowledge.hpp"
#include <QDialog>
#include <atomic>
class QLineEdit;
class QListWidget;
class QTextBrowser;
class QAction;
class QLabel;
class QComboBox;
class QTimer;
namespace penta::assistant {
class HelpArticleBrowser;
struct HelpSearchResult {CopilotReference reference;QString snippet;int score=0;};
// Pure, bounded file indexing; call on a worker. Only local documentation/source
// text is read. It never executes examples, loads modules or opens URLs.
QList<CopilotReference> loadDocumentationReferences(const QString& root,
                                                   const std::atomic_bool* cancel=nullptr);
void installDocumentationReferences(QList<CopilotReference> references);
class HelpDialog final:public QDialog {
    Q_OBJECT
public:
    explicit HelpDialog(QWidget* parent=nullptr);
    static QList<CopilotReference> allReferences();
    static QList<HelpSearchResult> searchReferences(const QString& query,int maximum=32);
    void search(const QString& query);
    void openTopic(const QString& id);
    void refreshIndex();
    void openLanguageReference(){openTopic(QStringLiteral("PENTA-language"));}
    QString currentTopicId()const{return currentTopic_;}
private:
    void populate(const QString&);
    void display(const QString& id,bool remember=true);
    void navigate(int delta);
    QLineEdit* query_;
    QListWidget* topics_;
    HelpArticleBrowser* article_;
    QLabel *summary_,*provenance_;
    QComboBox* category_;
    QTimer* searchTimer_;
    QAction *back_,*forward_;
    QStringList history_;
    int historyIndex_=-1;
    QString currentTopic_;
};
}
