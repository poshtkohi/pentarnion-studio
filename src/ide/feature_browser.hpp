#pragma once
#include <QWidget>
#include <functional>
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTreeWidget;
namespace penta {
class FeatureBrowser final:public QWidget {
    Q_OBJECT
public:
    explicit FeatureBrowser(const QString& referenceRoot,std::function<bool()> busy,QWidget* parent=nullptr);
    void refreshModules();
    int ruleCount()const;
private:
    void loadRules();
    void showRule();
    void loadSelectedModule();
    QString root_;
    std::function<bool()> busy_;
    QTreeWidget *rules_,*modules_;
    QPlainTextEdit* text_;
    QLineEdit* filter_;
    QLabel *version_,*moduleStatus_;
};
}
