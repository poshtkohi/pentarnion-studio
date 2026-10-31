#pragma once
#include <QWidget>
#include <QPointer>
#include <QRegularExpression>
#include <QVector>
#include <QStringList>
#include <functional>
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QToolButton;
namespace penta {
class EditorWidget;
// Persistent child of the editor surface. No nested dialog/event loop and no
// document formatting changes: matches use QPlainTextEdit extra selections.
class FindReplaceBar final:public QWidget {
    Q_OBJECT
public:
    explicit FindReplaceBar(QWidget* parent=nullptr);
    ~FindReplaceBar() override;
    void setEditor(EditorWidget* editor);
    void openFor(EditorWidget* editor,const QString& selectedText={});
    void focusSearch();
    void closePanel();
    void setSourceName(const QString&);
    void setReplaceVisible(bool);
    void setHistories(const QStringList& search,const QStringList& replacement);
    QStringList searchHistory()const{return m_searchHistory;}
    QStringList replacementHistory()const{return m_replacementHistory;}
    std::function<void()> closeRequested;
    int matchCount()const{return static_cast<int>(m_matches.size());}
protected:
    bool eventFilter(QObject*,QEvent*)override;
    void hideEvent(QHideEvent*)override;
    void showEvent(QShowEvent*)override;
private:
    void rebuild();
    void updateHighlights();
    void findMatch(bool backwards);
    void replaceOne();
    void replaceEvery();
    void selectMatch(int index);
    QString replacementFor(const QRegularExpressionMatch&)const;
    void rememberSearch(bool replacement=false);
    QPointer<EditorWidget> m_editor;
    QMetaObject::Connection m_contentsConnection,m_cursorConnection;
    QLineEdit* m_find=nullptr;
    QLineEdit* m_replacement=nullptr;
    QCheckBox* m_case=nullptr;
    QCheckBox* m_word=nullptr;
    QCheckBox* m_regex=nullptr;
    QLabel* m_count=nullptr;
    QLabel* m_status=nullptr;
    QToolButton* m_previous=nullptr;
    QToolButton* m_next=nullptr;
    QPushButton* m_replace=nullptr;
    QPushButton* m_replaceAll=nullptr;
    QTimer* m_refresh=nullptr;
    QLabel* m_sourceName=nullptr;
    QWidget* m_replacementRow=nullptr;
    QStringList m_searchHistory,m_replacementHistory;
    QVector<QRegularExpressionMatch> m_matches;
    bool m_overflow=false,m_valid=true,m_selecting=false;
    int m_lastZeroStart=-1;
};
}
