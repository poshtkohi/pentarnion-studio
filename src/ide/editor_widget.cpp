#include "editor_widget.hpp"
#include "syntax_highlighter.hpp"

#include <QFontDatabase>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QAbstractTextDocumentLayout>
#include <QScrollBar>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <climits>

namespace penta {

bool EditorWidget::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::Leave) emit mouseLeftViewport();
    return QPlainTextEdit::viewportEvent(event);
}

void EditorWidget::focusOutEvent(QFocusEvent* event) {
    emit focusLeftEditor();
    QPlainTextEdit::focusOutEvent(event);
}

namespace {
class LineNumbers final : public QWidget {
public:
    explicit LineNumbers(EditorWidget* editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return {m_editor->lineNumberAreaWidth(), 0}; }
protected:
    void paintEvent(QPaintEvent* event) override { m_editor->paintLineNumbers(event); }
private:
    EditorWidget* m_editor;
};

QList<QTextBlock> selectedBlocks(const QTextCursor& cursor) {
    QList<QTextBlock> blocks;
    QTextBlock first = cursor.document()->findBlock(cursor.selectionStart());
    int lastPosition = cursor.selectionEnd();
    if (cursor.hasSelection() && lastPosition > cursor.selectionStart()
            && cursor.document()->findBlock(lastPosition).position() == lastPosition)
        --lastPosition;
    const QTextBlock last = cursor.document()->findBlock(lastPosition);
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        blocks.append(block);
        if (block == last) break;
    }
    return blocks;
}

int indentationLength(const QString& text) {
    int count = 0;
    while (count < text.size()
           && (text.at(count) == QLatin1Char(' ') || text.at(count) == QLatin1Char('\t')))
        ++count;
    return count;
}
}

EditorWidget::EditorWidget(QWidget* parent) : QPlainTextEdit(parent) {
    setObjectName(QStringLiteral("sourceEditor"));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setStyleSheet(QStringLiteral(
        "QPlainTextEdit#sourceEditor {background:white;color:#202830;border:0;padding:0;selection-background-color:#cfe6fa;}"
        "QScrollBar:vertical {background:#f1f4f6;width:9px;margin:0;}"
        "QScrollBar:horizontal {background:#f1f4f6;height:9px;margin:0;}"
        "QScrollBar::handle:vertical {background:#b0bec8;border:2px solid #f1f4f6;border-radius:4px;min-height:24px;}"
        "QScrollBar::handle:horizontal {background:#b0bec8;border:2px solid #f1f4f6;border-radius:4px;min-width:24px;}"
        "QScrollBar::handle:hover {background:#7899ad;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;}"
        "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal {width:0;}"
        "QScrollBar::add-page,QScrollBar::sub-page {background:none;}"));
    QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    fixed.setPointSize(12);
    setFont(fixed);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4.0);
    m_lineNumbers = new LineNumbers(this);
    m_lineNumbers->setObjectName(QStringLiteral("lineNumbers"));
    // Owned by the editor, not a particular document, so switching documents
    // neither loses the highlighter nor leaves a dangling adapter behind.
    m_highlighter = new SyntaxHighlighter(this);
    m_highlighter->setDocument(document());
    connect(horizontalScrollBar(),&QScrollBar::rangeChanged,this,[this]{queueHorizontalRangeSync();});
    bindHorizontalRangeSync();
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, &EditorWidget::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &EditorWidget::highlightCurrentLine);
    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

EditorWidget::~EditorWidget() {
    // Detaching clears document formats and can emit contentsChanged. Do this
    // while the complete editor/widget tree still exists, before QWidget tears
    // down children and a receiver could traverse a partially destroyed tree.
    m_highlighter->setDocument(nullptr);
    delete m_highlighter;
    m_highlighter=nullptr;
}

QMimeData* EditorWidget::createMimeDataFromSelection() const {
    // Keep Qt's exact plain-text representation (and any platform formats).
    // Qt supplies CF_HTML on Windows from the portable text/html MIME payload.
    auto* mime=QPlainTextEdit::createMimeDataFromSelection();
    // Qt returns a lazy QTextEditMimeData: materialize its original fragment
    // before replacing HTML, otherwise a later retrieveData() overwrites it.
    if(mime) { (void)mime->text();mime->removeFormat(QStringLiteral("text/html")); }
    const auto selection=textCursor();
    constexpr qsizetype maximumSelectedCharacters=4*1024*1024;
    constexpr qsizetype maximumHtmlBytes=8*1024*1024;
    if(!mime || !selection.hasSelection() || selection.selectionEnd()-selection.selectionStart()>maximumSelectedCharacters)
        return mime;

    // Formatting can be queued after the last edit. Re-run only the shared
    // lexer/highlighter, never analysis or execution, before reading its ranges.
    m_highlighter->rehighlight();
    const QFont baseFont=font();
    QString family=baseFont.family();
    family.replace(QLatin1Char('\\'),QStringLiteral("\\\\"));
    family.replace(QLatin1Char('\''),QStringLiteral("\\'"));
    family.replace(QLatin1Char('\r'),QLatin1Char(' '));
    family.replace(QLatin1Char('\n'),QLatin1Char(' '));
    const QString fontSize=baseFont.pointSizeF()>0?QString::number(baseFont.pointSizeF(),'g',6)+QStringLiteral("pt"):
        QString::number(std::max(1,baseFont.pixelSize()))+QStringLiteral("px");
    const QString defaults=QStringLiteral("font-family:'%1','Courier New',monospace;font-size:%2;color:%3;background-color:%4;white-space:pre;tab-size:4;margin:0;")
        .arg(family,fontSize,palette().text().color().name(QColor::HexRgb),palette().base().color().name(QColor::HexRgb));
    // The outer span prevents HTML's special leading-newline removal in <pre>.
    QString html=QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head><body><pre style=\"%1\"><span>")
        .arg(defaults.toHtmlEscaped());
    bool exceeded=false;
    auto append=[&](const QString& value) {
        if(html.size()>maximumHtmlBytes-value.size()){exceeded=true;return;}
        html+=value;
    };
    const auto emitRun=[&](const QString& text,const QTextCharFormat& format) {
        if(text.isEmpty() || exceeded)return;
        const QColor color=format.foreground().style()!=Qt::NoBrush?format.foreground().color():palette().text().color();
        const int weight=format.hasProperty(QTextFormat::FontWeight)?format.fontWeight():static_cast<int>(baseFont.weight());
        const bool italic=format.hasProperty(QTextFormat::FontItalic)?format.fontItalic():baseFont.italic();
        const QString style=QStringLiteral("color:%1;font-weight:%2;font-style:%3;")
            .arg(color.name(QColor::HexRgb),QString::number(weight),italic?QStringLiteral("italic"):QStringLiteral("normal"));
        append(QStringLiteral("<span style=\"%1\">%2</span>").arg(style,text.toHtmlEscaped()));
    };
    const int start=selection.selectionStart(),end=selection.selectionEnd();
    for(QTextBlock block=document()->findBlock(start);block.isValid() && block.position()<end;block=block.next()) {
        const QString text=block.text();
        const int first=std::max(0,start-block.position());
        const int last=std::min(static_cast<int>(text.size()),end-block.position());
        int position=first;
        if(block.layout()) {
            auto formats=block.layout()->formats();
            std::stable_sort(formats.begin(),formats.end(),[](const auto& a,const auto& b){return a.start<b.start;});
            for(const auto& range:formats) {
                const int begin=std::max(position,range.start),finish=std::min(last,range.start+range.length);
                if(finish<=begin)continue;
                if(begin>position)emitRun(text.mid(position,begin-position),{});
                emitRun(text.mid(begin,finish-begin),range.format);position=finish;
                if(exceeded)break;
            }
        }
        if(position<last)emitRun(text.mid(position,last-position),{});
        if(end>block.position()+text.size() && block.next().isValid())append(QStringLiteral("\n"));
        if(exceeded)return mime;
    }
    append(QStringLiteral("</span></pre></body></html>"));
    if(!exceeded && html.toUtf8().size()<=maximumHtmlBytes)mime->setHtml(html);
    return mime;
}

void EditorWidget::setDocument(QTextDocument* next) {
    m_searchSelections.clear();
    m_highlighter->setDocument(nullptr);
    QPlainTextEdit::setDocument(next);
    m_highlighter->setDocument(document());
    m_highlighter->rehighlight();
    // The adopted document belongs to the source model. setDocumentMargin()
    // changes its root-frame format and creates a modification/undo command;
    // view styling must never turn an unchanged source file into an edit.
    bindHorizontalRangeSync();
    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

int EditorWidget::lineNumberAreaWidth() const {
    int digits = 1;
    for (int count = std::max(1, blockCount()); count >= 10; count /= 10) ++digits;
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void EditorWidget::updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    m_lineNumbers->update();
}

void EditorWidget::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy) m_lineNumbers->scroll(0, dy);
    else m_lineNumbers->update(0, rect.y(), m_lineNumbers->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth();
}

void EditorWidget::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    m_lineNumbers->setGeometry(area.left(), area.top(), lineNumberAreaWidth(), area.height());
    queueHorizontalRangeSync();
}

void EditorWidget::bindHorizontalRangeSync() {
    disconnect(m_horizontalLayoutConnection);
    m_horizontalLayoutConnection=connect(document()->documentLayout(),&QAbstractTextDocumentLayout::documentSizeChanged,this,[this]{queueHorizontalRangeSync();});
    queueHorizontalRangeSync();
}

void EditorWidget::queueHorizontalRangeSync() {
    if(m_horizontalSyncPending)return;
    m_horizontalSyncPending=true;
    QTimer::singleShot(0,this,[this]{
        m_horizontalSyncPending=false;
        if(lineWrapMode()!=NoWrap)return;
        const auto width=document()->documentLayout()->documentSize().width();
        if(!std::isfinite(width))return;
        auto* bar=horizontalScrollBar();bar->setPageStep(viewport()->width());
        bar->setRange(0,static_cast<int>(std::clamp(std::ceil(width)-viewport()->width(),0.0,double(INT_MAX))));
    });
}

void EditorWidget::paintLineNumbers(QPaintEvent* event) {
    QPainter painter(m_lineNumbers);
    painter.fillRect(event->rect(), palette().alternateBase());
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int current = textCursor().blockNumber();
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(number == current ? palette().text().color()
                                            : palette().color(QPalette::PlaceholderText));
            painter.drawText(0, top, m_lineNumbers->width() - 6, fontMetrics().height(),
                             Qt::AlignRight, QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void EditorWidget::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> selections;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(palette().alternateBase());
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        selections.append(selection);
    }
    selections.append(m_searchSelections);
    setExtraSelections(selections);
    m_lineNumbers->update();
}

void EditorWidget::setSearchSelections(const QList<QTextEdit::ExtraSelection>& selections) {
    m_searchSelections=selections;
    highlightCurrentLine();
}

void EditorWidget::setFontPointSize(int points) {
    QFont next = font();
    next.setPointSize(std::clamp(points, 7, 40));
    setFont(next);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4.0);
    updateLineNumberAreaWidth();
}

void EditorWidget::toggleComment() {
    QTextCursor selection = textCursor();
    const auto blocks = selectedBlocks(selection);
    bool uncomment = !blocks.isEmpty();
    for (const QTextBlock& block : blocks) {
        const QString value = block.text();
        const int at = indentationLength(value);
        if (at == value.size() || value.at(at) != QLatin1Char('%')) { uncomment = false; break; }
    }
    selection.beginEditBlock();
    for (auto it = blocks.crbegin(); it != blocks.crend(); ++it) {
        const QString value = it->text();
        const int indentation = indentationLength(value);
        QTextCursor edit(document());
        edit.setPosition(it->position() + indentation);
        if (uncomment) {
            int count = 1;
            if (indentation + 1 < value.size() && value.at(indentation + 1) == QLatin1Char(' ')) ++count;
            edit.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, count);
            edit.removeSelectedText();
        } else {
            edit.insertText(QStringLiteral("% "));
        }
    }
    selection.endEditBlock();
    setTextCursor(selection);
}

void EditorWidget::indentSelection(bool unindent) {
    QTextCursor selection = textCursor();
    if (!unindent && !selection.hasSelection()) {
        const int spaces = 4 - selection.positionInBlock() % 4;
        selection.insertText(QString(spaces, QLatin1Char(' ')));
        setTextCursor(selection);
        return;
    }
    const auto blocks = selectedBlocks(selection);
    selection.beginEditBlock();
    for (auto it = blocks.crbegin(); it != blocks.crend(); ++it) {
        QTextCursor edit(document());
        edit.setPosition(it->position());
        if (unindent) {
            const QString value = it->text();
            int remove = 0;
            if (value.startsWith(QLatin1Char('\t'))) remove = 1;
            else while (remove < 4 && remove < value.size() && value.at(remove) == QLatin1Char(' ')) ++remove;
            edit.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, remove);
            edit.removeSelectedText();
        } else {
            edit.insertText(QStringLiteral("    "));
        }
    }
    selection.endEditBlock();
    setTextCursor(selection);
}

void EditorWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier) {
        indentSelection();
        return;
    }
    if (event->key() == Qt::Key_Backtab
            || (event->key() == Qt::Key_Tab && event->modifiers() == Qt::ShiftModifier)) {
        indentSelection(true);
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && event->modifiers() == Qt::NoModifier) {
        QTextCursor cursor = textCursor();
        const QString line = cursor.block().text();
        const int indentation = std::min(indentationLength(line), cursor.positionInBlock());
        cursor.beginEditBlock();
        cursor.insertText(QStringLiteral("\n") + line.left(indentation));
        cursor.endEditBlock();
        setTextCursor(cursor);
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

bool EditorWidget::findLiteral(const QString& needle, bool backwards, bool caseSensitive) {
    if (needle.isEmpty()) return false;
    QTextDocument::FindFlags flags;
    if (backwards) flags |= QTextDocument::FindBackward;
    if (caseSensitive) flags |= QTextDocument::FindCaseSensitively;
    const QTextCursor original = textCursor();
    if (find(needle, flags)) return true;
    QTextCursor wrap(document());
    wrap.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
    setTextCursor(wrap);
    if (find(needle, flags)) return true;
    setTextCursor(original);
    return false;
}

bool EditorWidget::replaceCurrent(const QString& needle, const QString& replacement,
                                  bool caseSensitive) {
    if (needle.isEmpty()) return false;
    QTextCursor cursor = textCursor();
    const Qt::CaseSensitivity sensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (!cursor.hasSelection() || cursor.selectedText().compare(needle, sensitivity) != 0)
        return findLiteral(needle, false, caseSensitive);
    cursor.beginEditBlock();
    cursor.insertText(replacement);
    cursor.endEditBlock();
    setTextCursor(cursor);
    return true;
}

int EditorWidget::replaceAll(const QString& needle, const QString& replacement,
                             bool caseSensitive) {
    if (needle.isEmpty()) return 0;
    QTextDocument::FindFlags flags;
    if (caseSensitive) flags |= QTextDocument::FindCaseSensitively;
    QTextCursor edit(document());
    edit.beginEditBlock();
    int count = 0;
    QTextCursor found = document()->find(needle, 0, flags);
    while (!found.isNull()) {
        found.insertText(replacement);
        ++count;
        found = document()->find(needle, found, flags);
    }
    edit.endEditBlock();
    return count;
}

} // namespace penta
