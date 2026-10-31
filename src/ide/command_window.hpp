#pragma once

#include <QPlainTextEdit>
#include <QStringList>
#include <QVector>

namespace penta {

// One console document: protected transcript and a ready prompt or busy status.
// Output is inserted before the current input, never into a separate widget.
class CommandWindow final : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CommandWindow(QWidget* parent = nullptr);
    QString inputText() const;
    QString transcriptText() const;
    int inputStart() const { return m_inputStart; }
    void setInputText(const QString& text, bool select = false);
    void commitCommand(const QString& command);
    void appendOutput(const QString& text);
    void appendDiagnostic(const QString& text, quint64 identifier);
    void appendError(const QString& text) { appendDiagnostic(text, 0); }
    void interruptInput();
    void clearTranscript();
    void setBusy(bool busy);
    bool isBusy() const { return m_busy; }
    void setStopping();
    bool isStopping() const { return m_stopping; }
    QStringList history() const { return m_history; }
    void setHistory(QStringList history);

signals:
    void commandSubmitted(const QString& command);
    void interruptRequested();
    void diagnosticActivated(quint64 identifier);
    void diagnosticLinksRemoved(const QList<quint64>& identifiers);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void rebuild(const QString& input, bool cursorAtEnd = false, bool selectInput = false);
    void makeInputEditable(bool deleting = false);
    bool selectionIsInput() const;
    void safeCut();
    static QString normalizedInput(QString text);
    quint64 diagnosticAt(int position) const;
    struct DiagnosticLink { qsizetype start=0, length=0; quint64 identifier=0; };
    QVector<DiagnosticLink> m_diagnosticLinks;
    quint64 m_pressedDiagnostic=0;
    QPoint m_pressPoint;
    QString m_transcript; // Contains the terminating newline of every output line.
    QString m_input;
    int m_inputStart = 3;
    bool m_internalChange = false;
    bool m_busy = false;
    bool m_stopping = false;
    QStringList m_history;
    int m_historyIndex = 0;
    QString m_draft;
};

} // namespace penta
