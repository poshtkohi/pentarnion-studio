#pragma once

#include <QPlainTextEdit>
#include <QTextEdit>

namespace penta {

class SyntaxHighlighter;

class EditorWidget final : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit EditorWidget(QWidget* parent = nullptr);
    ~EditorWidget() override;
    // QPlainTextEdit's setter is non-virtual. Keep the styling adapter bound
    // when the IDE attaches its externally owned source QTextDocument.
    void setDocument(QTextDocument* document);
    SyntaxHighlighter* syntaxHighlighter() const { return m_highlighter; }
    int lineNumberAreaWidth() const;
    void paintLineNumbers(QPaintEvent* event);
    void setFontPointSize(int points);
    void toggleComment();
    void indentSelection(bool unindent = false);
    bool findLiteral(const QString& needle, bool backwards = false,
                     bool caseSensitive = false);
    bool replaceCurrent(const QString& needle, const QString& replacement,
                        bool caseSensitive = false);
    int replaceAll(const QString& needle, const QString& replacement,
                   bool caseSensitive = false);
    // Search decorations are view-only and coexist with syntax colors/current line.
    void setSearchSelections(const QList<QTextEdit::ExtraSelection>& selections);

signals:
    void mouseLeftViewport();
    void focusLeftEditor();

protected:
    QMimeData* createMimeDataFromSelection() const override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();
    void bindHorizontalRangeSync();
    void queueHorizontalRangeSync();
    QWidget* m_lineNumbers = nullptr;
    SyntaxHighlighter* m_highlighter = nullptr;
    QList<QTextEdit::ExtraSelection> m_searchSelections;
    QMetaObject::Connection m_horizontalLayoutConnection;
    bool m_horizontalSyncPending=false;
};

} // namespace penta
