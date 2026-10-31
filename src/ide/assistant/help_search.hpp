// Adapted from GTG Cell Studio desktop components; scientific engine remains separate.
#pragma once
#include <QLineEdit>
#include <functional>
class QCompleter;
class QStandardItemModel;
class QTimer;
namespace penta::assistant {
// Local documentation search; never sends editor text or questions to a provider.
class HelpSearchBox final:public QLineEdit {
    Q_OBJECT
public:
    explicit HelpSearchBox(QWidget* parent=nullptr);
    QSize sizeHint()const override;
    QSize minimumSizeHint()const override;
    std::function<void(const QString&)> searchRequested;
    std::function<void(const QString&)> topicRequested;
private:
    void updateSuggestions(const QString&);
    QCompleter* suggestions_;
    QStandardItemModel* model_;
    QTimer* suggestionsTimer_;
    bool completionHandled_=false;
};
}
