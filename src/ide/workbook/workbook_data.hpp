#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <cstdint>

namespace penta::workbook {

// The document codec is independent of widgets and never executes formulas.
// Worksheet presentation follows GTG Cell Studio's bounded native data viewer.
enum class CellKind { Text, Number, Boolean, Formula, Error };
struct Cell {
    CellKind kind=CellKind::Text;
    QString text;
    QString cached;
    bool operator==(const Cell&)const=default;
};
struct CellRange {int firstRow=0,lastRow=0,firstColumn=0,lastColumn=0;};
using CellMap=QMap<quint64,Cell>;
struct Sheet {
    QString name,part;
    CellMap cells;
    QVector<CellRange> protectedFormulaRanges;
    int rows=0,columns=0;
};
struct ArchivePart {
    QByteArray name,data;
    quint32 flags=0;
};
struct WorkbookData {
    QString format=QStringLiteral("xlsx");
    QVector<Sheet> sheets;
    QVector<ArchivePart> parts;
    QString workbookPart,relationshipsPart;
    QByteArray sourceBytes;
    QVector<int> csvWidths;
    QString newline=QStringLiteral("\n");
    bool bom=false,trailingNewline=false;
};

constexpr int MaximumRows=1048576,MaximumColumns=16384,MaximumCells=100000;
constexpr qsizetype MaximumInputBytes=16*1024*1024,MaximumTextCharacters=8*1024*1024;
constexpr qsizetype MaximumCellCharacters=65536,MaximumEditCharacters=2*1024*1024;
quint64 cellKey(int row,int column);
int cellRow(quint64 key);
int cellColumn(quint64 key);
QString cellAddress(int row,int column);
QString cellDisplay(const Cell& cell);
QString cellEditText(const Cell& cell);
Cell enteredCell(const QString& text,bool formulas=true);
bool formulaRangeProtected(const Sheet& sheet,int row,int column);
WorkbookData readWorkbook(const QByteArray& bytes,const QString& format,const std::atomic_bool& cancel);
QByteArray writeWorkbook(const WorkbookData& data,const QVector<CellMap>& changes,const QString& format);
QVector<QStringList> parseDelimited(const QString& text,QChar delimiter,const std::atomic_bool* cancel=nullptr);
QString encodeDelimited(const QVector<QStringList>& rows,QChar delimiter,const QString& newline,bool trailingNewline);
// Used by the native codec and preservation tests; no filesystem extraction.
QVector<ArchivePart> readPackage(const QByteArray& bytes,const std::atomic_bool& cancel);
QByteArray writePackage(const QVector<ArchivePart>& parts);

} // namespace penta::workbook
