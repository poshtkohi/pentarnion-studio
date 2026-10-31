#include "command_window.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextBlock>

#include <algorithm>

namespace penta {
namespace {
QString rawText(const QTextDocument* document) {
    QString value = document->toRawText();
    value.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return value;
}
}

CommandWindow::CommandWindow(QWidget* parent) : QPlainTextEdit(parent) {
    setObjectName(QStringLiteral("commandWindow"));
    setAccessibleName(tr("Command Window"));
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setFrameShape(QFrame::NoFrame);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setUndoRedoEnabled(false); // Native undo must never remove a protected prompt/history.
    setAcceptDrops(false);
    auto colors=palette();colors.setColor(QPalette::Base,Qt::white);
    colors.setColor(QPalette::Text,Qt::black);colors.setColor(QPalette::Link,QColor(190,0,0));
    setPalette(colors);
    viewport()->setMouseTracking(true);
    rebuild({});
    connect(this, &QPlainTextEdit::textChanged, this, [this] {
        if (m_internalChange) return;
        if (m_busy) { rebuild(m_input); return; }
        const QString text = rawText(document());
        const QString prefix = m_transcript + QStringLiteral(">> ");
        if (!text.startsWith(prefix)) { rebuild(m_input, true); return; }
        m_input = text.mid(m_inputStart);
    });
}

QString CommandWindow::normalizedInput(QString text) {
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.remove(QChar::Null);
    return text;
}

QString CommandWindow::inputText() const { return m_input; }

QString CommandWindow::transcriptText() const {
    return m_transcript.isEmpty() ? QString() : m_transcript.left(m_transcript.size() - 1);
}

void CommandWindow::rebuild(const QString& input, bool cursorAtEnd, bool selectInput) {
    const int oldStart = m_inputStart;
    const auto oldCursor = textCursor();
    const int oldPosition = oldCursor.position(), oldAnchor = oldCursor.anchor();
    const int scroll = verticalScrollBar()->value();
    const bool following = scroll >= verticalScrollBar()->maximum() - 1;
    m_internalChange = true;
    m_input = normalizedInput(input);
    constexpr qsizetype maximumTranscript = 8 * 1024 * 1024;
    qsizetype removed = 0;
    qsizetype prefixLength = 0;
    if (m_transcript.size() > maximumTranscript) {
        removed = m_transcript.size() - maximumTranscript;
        const qsizetype nextLine = m_transcript.indexOf('\n', removed);
        if (nextLine >= 0) removed = nextLine + 1;
        const QString prefix=tr("[Older command transcript trimmed]\n");prefixLength=prefix.size();
        m_transcript = prefix + m_transcript.mid(removed);
        QList<quint64> discarded;
        for(auto it=m_diagnosticLinks.begin();it!=m_diagnosticLinks.end();) {
            if(it->start<removed){discarded.append(it->identifier);it=m_diagnosticLinks.erase(it);}
            else{it->start=it->start-removed+prefixLength;++it;}
        }
        if(!discarded.isEmpty())emit diagnosticLinksRemoved(discarded);
    }
    const QString tail = m_busy
        ? (m_stopping ? tr("Stopping…") : tr("Running… (Ctrl+C to stop)"))
        : QStringLiteral(">> ") + m_input;
    m_inputStart = static_cast<int>(m_transcript.size()) + static_cast<int>(m_busy ? tail.size() : 3);
    setPlainText(m_transcript + tail);
    // setPlainText can retain the cursor's previous anchor/foreground format.
    // Reset every character before applying only the diagnostic ranges.
    QTextCharFormat normal;normal.setForeground(Qt::black);normal.setAnchor(false);
    normal.setFontUnderline(false);
    QTextCursor all(document());all.select(QTextCursor::Document);all.setCharFormat(normal);
    for(const auto& link:m_diagnosticLinks) {
        QTextCursor format(document());format.setPosition(static_cast<int>(link.start));
        format.setPosition(static_cast<int>(link.start+link.length),QTextCursor::KeepAnchor);
        QTextCharFormat anchor;anchor.setAnchor(link.identifier!=0);
        if(link.identifier)anchor.setAnchorHref(QString::number(link.identifier));
        anchor.setForeground(QColor(190,0,0));anchor.setFontUnderline(link.identifier!=0);format.setCharFormat(anchor);
    }
    auto mapPosition = [this, oldStart, removed, prefixLength](int position) {
        const int mapped = position >= oldStart ? m_inputStart + position - oldStart
                                               : position - static_cast<int>(removed) + static_cast<int>(prefixLength);
        return std::clamp(mapped, 0, document()->characterCount() - 1);
    };
    QTextCursor cursor(document());
    if (selectInput) {
        cursor.setPosition(m_inputStart); cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    } else if (cursorAtEnd) cursor.movePosition(QTextCursor::End);
    else {
        cursor.setPosition(mapPosition(oldAnchor));
        cursor.setPosition(mapPosition(oldPosition), QTextCursor::KeepAnchor);
    }
    setTextCursor(cursor);
    if(!cursor.hasSelection())setCurrentCharFormat(normal);
    verticalScrollBar()->setValue(following ? verticalScrollBar()->maximum()
                                          : std::min(scroll, verticalScrollBar()->maximum()));
    m_internalChange = false;
}

void CommandWindow::setInputText(const QString& text, bool select) {
    rebuild(text, !select, select);
}

void CommandWindow::setHistory(QStringList history) {
    while (history.size() > 200) history.removeFirst();
    m_history = std::move(history); m_historyIndex = static_cast<int>(m_history.size()); m_draft.clear();
}

void CommandWindow::commitCommand(const QString& command) {
    const QString normalized = normalizedInput(command);
    m_history.append(normalized);
    while (m_history.size() > 200) m_history.removeFirst();
    m_historyIndex = static_cast<int>(m_history.size()); m_draft.clear();
    QString visible = normalized;
    visible.replace(QStringLiteral("\n"), QStringLiteral("\n... "));
    m_transcript += QStringLiteral(">> ") + visible + '\n';
    rebuild({}, true);
}

void CommandWindow::appendOutput(const QString& text) {
    m_transcript += text + '\n';
    rebuild(m_input);
}

void CommandWindow::appendDiagnostic(const QString& text,quint64 identifier) {
    constexpr qsizetype maximumLinks=4096;
    QList<quint64> discarded;
    while(m_diagnosticLinks.size()>=maximumLinks){discarded.append(m_diagnosticLinks.front().identifier);m_diagnosticLinks.removeFirst();}
    if(!discarded.isEmpty())emit diagnosticLinksRemoved(discarded);
    m_diagnosticLinks.append({m_transcript.size(),text.size(),identifier});
    m_transcript+=text+'\n';rebuild(m_input);
}

quint64 CommandWindow::diagnosticAt(int position) const {
    for(const auto& link:m_diagnosticLinks)if(position>=link.start&&position<link.start+link.length)return link.identifier;
    return 0;
}

void CommandWindow::clearTranscript() {
    QList<quint64> discarded;for(const auto& link:m_diagnosticLinks)discarded.append(link.identifier);
    m_diagnosticLinks.clear();if(!discarded.isEmpty())emit diagnosticLinksRemoved(discarded);
    m_transcript.clear();
    rebuild(m_input, true);
}

void CommandWindow::setBusy(bool busy) {
    if (m_busy == busy) return;
    m_busy = busy;
    m_stopping = false;
    setReadOnly(busy);
    rebuild(m_input);
    viewport()->setCursor(busy ? Qt::ArrowCursor : Qt::IBeamCursor);
}

void CommandWindow::setStopping() {
    if (!m_busy || m_stopping) return;
    m_stopping = true;
    rebuild(m_input);
}

bool CommandWindow::selectionIsInput() const {
    if (m_busy) return false;
    const auto cursor = textCursor();
    return cursor.selectionStart() >= m_inputStart;
}

void CommandWindow::makeInputEditable(bool deleting) {
    auto cursor = textCursor();
    if (cursor.selectionStart() < m_inputStart) {
        if (deleting && cursor.hasSelection() && cursor.selectionEnd() > m_inputStart) {
            const int end = cursor.selectionEnd();
            cursor.setPosition(m_inputStart); cursor.setPosition(end, QTextCursor::KeepAnchor);
        } else cursor.movePosition(QTextCursor::End);
        setTextCursor(cursor);
    }
}

void CommandWindow::safeCut() {
    if (m_busy || !selectionIsInput()) { copy(); return; }
    QPlainTextEdit::cut();
}

void CommandWindow::interruptInput() {
    if (m_busy) {
        if (m_stopping) return;
        m_transcript += QStringLiteral("^C\n");
        m_historyIndex = static_cast<int>(m_history.size()); m_draft.clear();
        m_stopping = true;
        rebuild({}, true); ensureCursorVisible(); emit interruptRequested();
        return;
    }
    QString visible=m_input;visible.replace(QStringLiteral("\n"),QStringLiteral("\n... "));
    m_transcript+=QStringLiteral(">> ")+visible+QStringLiteral("^C\n");
    m_historyIndex=static_cast<int>(m_history.size());m_draft.clear();
    rebuild({},true);ensureCursorVisible();emit interruptRequested();
}

void CommandWindow::keyPressEvent(QKeyEvent* event) {
    if (m_busy && event->key() == Qt::Key_C && event->modifiers() == Qt::ControlModifier) {
        interruptInput(); event->accept(); return;
    }
    if(event->matches(QKeySequence::Copy)) {
        if(textCursor().hasSelection())copy();else if(!m_busy)interruptInput();
        event->accept();return;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        QPlainTextEdit::keyPressEvent(event); return;
    }
    if (event->matches(QKeySequence::Cut)) { safeCut(); return; }
    if (event->matches(QKeySequence::Paste)) {
        if (!m_busy) insertFromMimeData(QApplication::clipboard()->mimeData());
        return;
    }
    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo)) return;
    const int key = event->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if(const auto identifier=diagnosticAt(textCursor().position());identifier&&textCursor().position()<m_inputStart) {
            emit diagnosticActivated(identifier);return;
        }
        if (m_busy) return;
        if (event->modifiers() & Qt::ShiftModifier) {
            makeInputEditable(); QPlainTextEdit::insertPlainText(QStringLiteral("\n"));
        } else if (!m_input.trimmed().isEmpty()) emit commandSubmitted(m_input);
        return;
    }
    const auto cursor = textCursor();
    if (!m_busy && cursor.position() >= m_inputStart && !m_history.isEmpty() &&
        ((key == Qt::Key_Up && cursor.block().position() <= m_inputStart) ||
         (key == Qt::Key_Down && cursor.block() == document()->lastBlock()))) {
        if (m_historyIndex == m_history.size()) m_draft = m_input;
        m_historyIndex = std::clamp(m_historyIndex + (key == Qt::Key_Up ? -1 : 1),
                                    0, static_cast<int>(m_history.size()));
        setInputText(m_historyIndex == m_history.size() ? m_draft : m_history[m_historyIndex]);
        return;
    }
    if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {
        if (m_busy) return;
        if (!cursor.hasSelection() && cursor.position() < m_inputStart) return;
        if (!cursor.hasSelection() && key == Qt::Key_Backspace && cursor.position() == m_inputStart) return;
        if (cursor.hasSelection() && cursor.selectionEnd() <= m_inputStart) return;
        makeInputEditable(true); QPlainTextEdit::keyPressEvent(event); return;
    }
    if (key == Qt::Key_Home && !(event->modifiers() & Qt::ControlModifier) && cursor.position() >= m_inputStart) {
        auto home = cursor;
        home.setPosition(std::max(m_inputStart, cursor.block().position()),
            event->modifiers() & Qt::ShiftModifier ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
        setTextCursor(home); return;
    }
    const bool insertion = !event->text().isEmpty() && !(event->modifiers() & Qt::ControlModifier);
    if (insertion || key == Qt::Key_Tab) {
        if (m_busy) return;
        makeInputEditable();
        if (key == Qt::Key_Tab) QPlainTextEdit::insertPlainText(QStringLiteral("    "));
        else QPlainTextEdit::keyPressEvent(event);
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

bool CommandWindow::canInsertFromMimeData(const QMimeData* source) const {
    return source && source->hasText() && !m_busy;
}

void CommandWindow::insertFromMimeData(const QMimeData* source) {
    if (!canInsertFromMimeData(source)) return;
    makeInputEditable();
    QPlainTextEdit::insertPlainText(normalizedInput(source->text()));
}

void CommandWindow::inputMethodEvent(QInputMethodEvent* event) {
    if (m_busy) { event->ignore(); return; }
    makeInputEditable(); QPlainTextEdit::inputMethodEvent(event);
}

void CommandWindow::focusInEvent(QFocusEvent* event) {
    const int previous = verticalScrollBar()->value();
    const bool following = previous >= verticalScrollBar()->maximum() - 1;
    QPlainTextEdit::focusInEvent(event);
    if (!following) verticalScrollBar()->setValue(previous);
}

void CommandWindow::mousePressEvent(QMouseEvent* event) {
    m_pressedDiagnostic=event->button()==Qt::LeftButton?diagnosticAt(cursorForPosition(event->position().toPoint()).position()):0;
    m_pressPoint=event->position().toPoint();QPlainTextEdit::mousePressEvent(event);
}

void CommandWindow::mouseReleaseEvent(QMouseEvent* event) {
    QPlainTextEdit::mouseReleaseEvent(event);
    const auto identifier=m_pressedDiagnostic;m_pressedDiagnostic=0;
    if(identifier&&event->button()==Qt::LeftButton&&
       (event->position().toPoint()-m_pressPoint).manhattanLength()<QApplication::startDragDistance()&&
       diagnosticAt(cursorForPosition(event->position().toPoint()).position())==identifier)
        emit diagnosticActivated(identifier);
}

void CommandWindow::mouseMoveEvent(QMouseEvent* event) {
    QPlainTextEdit::mouseMoveEvent(event);
    const bool linked=diagnosticAt(cursorForPosition(event->position().toPoint()).position());
    viewport()->setCursor(linked?Qt::PointingHandCursor:(m_busy?Qt::ArrowCursor:Qt::IBeamCursor));
}

void CommandWindow::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    if(const auto identifier=diagnosticAt(cursorForPosition(event->pos()).position())) {
        menu.addAction(tr("Go to diagnostic source"),this,[this,identifier]{emit diagnosticActivated(identifier);});
        menu.addSeparator();
    }
    auto* copyAction = menu.addAction(tr("Copy"), this, &QPlainTextEdit::copy);
    copyAction->setEnabled(textCursor().hasSelection());
    auto* cutAction = menu.addAction(tr("Cut"), this, &CommandWindow::safeCut);
    cutAction->setEnabled(!m_busy && textCursor().hasSelection() && selectionIsInput());
    auto* pasteAction = menu.addAction(tr("Paste"), this, [this] { insertFromMimeData(QApplication::clipboard()->mimeData()); });
    pasteAction->setEnabled(canInsertFromMimeData(QApplication::clipboard()->mimeData()));
    menu.addSeparator(); menu.addAction(tr("Select All"), this, &QPlainTextEdit::selectAll);
    menu.exec(event->globalPos());
}

} // namespace penta
