#include "offscreen_test_bootstrap.hpp"
#include "ide/workbook/workbook_document.hpp"
#include "ide/workbook/workbook_data.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDomDocument>
#include <QFile>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <stdexcept>

using namespace penta::workbook;
namespace {
QByteArray fixture(){
    QVector<ArchivePart> parts={
        {"[Content_Types].xml",R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/><Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/><Override PartName="/xl/calcChain.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.calcChain+xml"/></Types>)"},
        {"_rels/.rels",R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="office" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>)"},
        {"xl/workbook.xml",R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Measurements" sheetId="1" r:id="first"/><sheet name="Untouched" sheetId="2" r:id="second"/></sheets><calcPr calcId="1"/></workbook>)"},
        {"xl/_rels/workbook.xml.rels",R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="first" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="second" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/><Relationship Id="styles" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/><Relationship Id="strings" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/><Relationship Id="chain" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/></Relationships>)"},
        {"xl/worksheets/sheet1.xml",R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><dimension ref="A1:E1"/><cols><col min="1" max="1" width="19" customWidth="1"/></cols><sheetData><row r="1"><c r="A1" s="1"><v>2.5</v></c><c r="B1"><f>A1*2</f><v>5</v></c><c r="C1" t="inlineStr"><is><t xml:space="preserve"> literal </t></is></c><c r="D1" t="s"><v>0</v></c><c r="E1" t="b"><v>1</v></c></row></sheetData><pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/></worksheet>)"},
        {"xl/worksheets/sheet2.xml",R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>retained second sheet</t></is></c></row></sheetData></worksheet>)"},
        {"xl/styles.xml",R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><numFmts count="1"><numFmt numFmtId="164" formatCode="0.000"/></numFmts><fonts count="1"><font><sz val="11"/><name val="Calibri"/></font></fonts><fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills><borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders><cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs><cellXfs count="2"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/><xf numFmtId="164" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/></cellXfs><cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles></styleSheet>)"},
        {"xl/sharedStrings.xml",R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1"><si><r><t>alpha</t></r><r><t> beta</t></r></si></sst>)"},
        {"xl/calcChain.xml",R"(<calcChain xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><c r="B1" i="1"/></calcChain>)"},
        {"custom/retained.bin",QByteArray::fromHex("00112233aabbff0066")}
    };return writePackage(parts);
}
QMap<QByteArray,QByteArray> mapParts(const QByteArray& bytes){std::atomic_bool cancel{false};QMap<QByteArray,QByteArray> result;for(const auto& part:readPackage(bytes,cancel))result.insert(part.name,part.data);return result;}
QByteArray readFile(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll();}
bool writeFile(const QString& path,const QByteArray& bytes){QFile file(path);if(!file.open(QIODevice::WriteOnly))return false;return file.write(bytes)==bytes.size();}
QDomDocument parseXml(const QByteArray& bytes){QDomDocument document;
#if QT_VERSION >= QT_VERSION_CHECK(6,5,0)
    document.setContent(bytes,QDomDocument::ParseOption::UseNamespaceProcessing);
#else
    document.setContent(bytes,true);
#endif
    return document;
}
QDomElement cell(QDomDocument document,const QString& address){const auto nodes=document.elementsByTagNameNS("http://schemas.openxmlformats.org/spreadsheetml/2006/main","c");for(int i=0;i<nodes.size();++i){const auto element=nodes.at(i).toElement();if(element.attribute("r")==address)return element;}return {};}
}

class WorkbookEditorTests final:public QObject {
    Q_OBJECT
private slots:
    void xlsxUneditedIsByteExact();
    void unicodeFileAndPackageNamesSurviveSave();
    void xlsxEditPreservesStylesFormulasOtherSheetsAndOpaqueParts();
    void clearingStyledCellPreservesStyle();
    void formulaEditingMarksRecalculationWithoutInventingValues();
    void csvQuotedUnicodeRoundTripAndEdits();
    void malformedInputsAreRejected();
    void xmlPreflightHandlesUtf16AndLiteralCdata();
    void rectangularPasteUndoAndSave();
    void tableExtentsFollowPasteUndoRedoAndClear();
    void pendingFormulaBarIsDirtyAndSaved();
    void cutFormulaTextPreservesTableCells();
    void rejectedCutPreservesCellsAndClipboard();
    void worksheetSwitchCommitsOrRetainsFormulaEdits();
    void worksheetEditsRemainIndependent();
    void externalChangeRefusesOverwrite();
    void failedSaveAndDirtyOpenPreserveEdits();
    void unsupportedFormulaRangesRemainProtected();
};

void WorkbookEditorTests::xlsxUneditedIsByteExact(){
    const auto bytes=fixture();std::atomic_bool cancel{false};const auto book=readWorkbook(bytes,"xlsx",cancel);
    QCOMPARE(book.sheets.size(),2);QCOMPARE(book.sheets[0].cells.value(cellKey(0,0)).kind,CellKind::Number);
    QCOMPARE(book.sheets[0].cells.value(cellKey(0,1)).kind,CellKind::Formula);QCOMPARE(cellDisplay(book.sheets[0].cells.value(cellKey(0,1))),QStringLiteral("5"));
    QCOMPARE(book.sheets[0].cells.value(cellKey(0,3)).text,QStringLiteral("alpha beta"));
    QCOMPARE(writeWorkbook(book,QVector<CellMap>(2),"xlsx"),bytes);
}
void WorkbookEditorTests::unicodeFileAndPackageNamesSurviveSave(){
    std::atomic_bool cancel{false};auto parts=readPackage(fixture(),cancel);
    const auto name=QString::fromUtf8("custom/αβ.bin").toUtf8();const auto payload=QByteArray::fromHex("ffab000112");parts.push_back({name,payload});
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const auto path=temporary.filePath(QString::fromUtf8("δ-workbook.xlsx"));QVERIFY(writeFile(path,writePackage(parts)));
    WorkbookDocument document;QSignalSpy loaded(&document,&WorkbookDocument::loadingFinished);QString error;QVERIFY2(document.openFile(path,&error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,5000);QVERIFY(loaded.front().front().toBool());QVERIFY2(document.setCell(0,0,"8.5",&error),qPrintable(error));QVERIFY2(document.save(&error),qPrintable(error));
    bool found=false;for(const auto& item:readPackage(readFile(path),cancel))if(item.name==name){found=true;QCOMPARE(item.data,payload);QVERIFY(item.flags&(1U<<11));}QVERIFY(found);
}
void WorkbookEditorTests::xlsxEditPreservesStylesFormulasOtherSheetsAndOpaqueParts(){
    const auto original=fixture();std::atomic_bool cancel{false};const auto book=readWorkbook(original,"xlsx",cancel);QVector<CellMap> changes(2);changes[0].insert(cellKey(0,0),enteredCell("3.75"));
    const auto saved=writeWorkbook(book,changes,"xlsx");const auto before=mapParts(original),after=mapParts(saved);
    for(const auto& name:{QByteArray("xl/styles.xml"),QByteArray("xl/worksheets/sheet2.xml"),QByteArray("xl/sharedStrings.xml"),QByteArray("custom/retained.bin")})QCOMPARE(after.value(name),before.value(name));
    const auto worksheet=parseXml(after.value("xl/worksheets/sheet1.xml"));QCOMPARE(cell(worksheet,"A1").attribute("s"),QStringLiteral("1"));QCOMPARE(cell(worksheet,"A1").firstChildElement("v").text(),QStringLiteral("3.75"));
    QCOMPARE(cell(worksheet,"B1").firstChildElement("f").text(),QStringLiteral("A1*2"));QVERIFY(!after.contains("xl/calcChain.xml"));
    const auto result=readWorkbook(saved,"xlsx",cancel);QCOMPARE(result.sheets[1].cells.value(cellKey(0,0)).text,QStringLiteral("retained second sheet"));
}
void WorkbookEditorTests::clearingStyledCellPreservesStyle(){
    std::atomic_bool cancel{false};const auto book=readWorkbook(fixture(),"xlsx",cancel);QVector<CellMap> changes(2);changes[0].insert(cellKey(0,0),Cell{});
    const auto parts=mapParts(writeWorkbook(book,changes,"xlsx"));const auto worksheet=parseXml(parts.value("xl/worksheets/sheet1.xml"));const auto edited=cell(worksheet,"A1");QCOMPARE(edited.attribute("s"),QStringLiteral("1"));QVERIFY(edited.firstChildElement("v").isNull());QVERIFY(!cell(worksheet,"B1").firstChildElement("f").isNull());
}
void WorkbookEditorTests::formulaEditingMarksRecalculationWithoutInventingValues(){
    std::atomic_bool cancel{false};const auto book=readWorkbook(fixture(),"xlsx",cancel);QVector<CellMap> changes(2);changes[0].insert(cellKey(1,1),enteredCell("=SUM(A1:B1)"));
    const auto saved=writeWorkbook(book,changes,"xlsx");const auto parts=mapParts(saved);const auto worksheet=parseXml(parts.value("xl/worksheets/sheet1.xml"));const auto edited=cell(worksheet,"B2");QCOMPARE(edited.firstChildElement("f").text(),QStringLiteral("SUM(A1:B1)"));QVERIFY(edited.firstChildElement("v").isNull());
    const auto result=readWorkbook(saved,"xlsx",cancel);QCOMPARE(cellDisplay(result.sheets[0].cells.value(cellKey(1,1))),QStringLiteral("=SUM(A1:B1)"));QVERIFY(parts.value("xl/workbook.xml").contains("fullCalcOnLoad=\"1\""));
}
void WorkbookEditorTests::csvQuotedUnicodeRoundTripAndEdits(){
    const QByteArray bytes=QByteArray::fromHex("efbbbf")+QString::fromUtf8("name,value\r\n\"α,β\",\"line 1\nline \"\"2\"\"\"\r\n").toUtf8();std::atomic_bool cancel{false};const auto book=readWorkbook(bytes,"csv",cancel);QCOMPARE(book.sheets[0].rows,2);QCOMPARE(writeWorkbook(book,QVector<CellMap>(1),"csv"),bytes);
    QVector<CellMap> changes(1);changes[0].insert(cellKey(1,0),Cell{CellKind::Text,QStringLiteral("changed, name"),{}});const auto saved=writeWorkbook(book,changes,"csv");QVERIFY(saved.startsWith(QByteArray::fromHex("efbbbf")));const auto result=readWorkbook(saved,"csv",cancel);QCOMPARE(result.sheets[0].cells.value(cellKey(1,0)).text,QStringLiteral("changed, name"));QCOMPARE(result.sheets[0].cells.value(cellKey(1,1)).text,QStringLiteral("line 1\nline \"2\""));
}
void WorkbookEditorTests::malformedInputsAreRejected(){
    std::atomic_bool cancel{false};
    for(const auto& invalid:{QByteArray::fromHex("ff"),QByteArray::fromHex("c3"),QByteArray::fromHex("e282"),QByteArray::fromHex("c328")}){
        QVERIFY_EXCEPTION_THROWN(readWorkbook(invalid,"csv",cancel),std::exception);
        QVERIFY_EXCEPTION_THROWN(readWorkbook(invalid,"tsv",cancel),std::exception);
    }
    QVERIFY_EXCEPTION_THROWN(parseDelimited("\"unterminated",','),std::exception);
    auto parts=readPackage(fixture(),cancel);parts.push_back(parts.front());QVERIFY_EXCEPTION_THROWN(readWorkbook(writePackage(parts),"xlsx",cancel),std::exception);
    parts=readPackage(fixture(),cancel);for(auto& part:parts)if(part.name=="xl/worksheets/sheet1.xml")part.data="<!DOCTYPE x [<!ENTITY a 'abc'>]><worksheet/>";QVERIFY_EXCEPTION_THROWN(readWorkbook(writePackage(parts),"xlsx",cancel),std::exception);
}
void WorkbookEditorTests::xmlPreflightHandlesUtf16AndLiteralCdata(){
    std::atomic_bool cancel{false};auto parts=readPackage(fixture(),cancel);
    const QString malicious=QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-16\"?><!DOCTYPE worksheet [<!ENTITY payload 'expanded'>]><worksheet>&payload;</worksheet>");
    QByteArray utf16=QByteArray::fromHex("fffe");for(const auto c:malicious){utf16.append(char(c.unicode()&255));utf16.append(char(c.unicode()>>8));}
    for(auto& item:parts)if(item.name=="xl/worksheets/sheet1.xml")item.data=utf16;
    QVERIFY_EXCEPTION_THROWN(readWorkbook(writePackage(parts),"xlsx",cancel),std::exception);
    parts=readPackage(fixture(),cancel);
    for(auto& item:parts)if(item.name=="xl/worksheets/sheet1.xml")item.data=R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t><![CDATA[literal <!DOCTYPE example>]]></t></is></c></row></sheetData></worksheet>)";
    const auto book=readWorkbook(writePackage(parts),"xlsx",cancel);QCOMPARE(book.sheets[0].cells.value(cellKey(0,0)).text,QStringLiteral("literal <!DOCTYPE example>"));
}
void WorkbookEditorTests::rectangularPasteUndoAndSave(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());WorkbookDocument document;QString error;QVERIFY2(document.setCell(0,0,"7",&error),qPrintable(error));QVERIFY(document.isModified());
    QVERIFY2(document.pasteText("1\tTRUE\n\"line 1\nline 2\"\t=1+2",1,0,&error),qPrintable(error));QCOMPARE(document.cellText(1,1),QStringLiteral("TRUE"));QCOMPARE(document.cellText(2,0),QStringLiteral("line 1\nline 2"));document.undo();QVERIFY(document.cellText(1,0).isEmpty());QCOMPARE(document.cellText(0,0),QStringLiteral("7"));document.redo();QCOMPARE(document.cellText(2,1),QStringLiteral("=1+2"));
    const auto path=temporary.filePath("new.xlsx");QVERIFY2(document.saveAs(path,&error),qPrintable(error));QVERIFY(!document.isModified());document.undo();QVERIFY(document.isModified());document.redo();QVERIFY(!document.isModified());
    std::atomic_bool cancel{false};const auto result=readWorkbook(readFile(path),"xlsx",cancel);QCOMPARE(result.sheets[0].cells.value(cellKey(0,0)).kind,CellKind::Number);QCOMPARE(result.sheets[0].cells.value(cellKey(1,1)).kind,CellKind::Boolean);QCOMPARE(result.sheets[0].cells.value(cellKey(2,1)).kind,CellKind::Formula);
}
void WorkbookEditorTests::tableExtentsFollowPasteUndoRedoAndClear(){
    WorkbookDocument document;auto* model=document.table()->model();const int initialRows=model->rowCount(),initialColumns=model->columnCount();QString error;
    QVERIFY2(document.pasteText("11\t22\n33\t44",120,30,&error),qPrintable(error));QVERIFY(model->rowCount()>=122);QVERIFY(model->columnCount()>=32);QCOMPARE(document.cellText(121,31),QStringLiteral("44"));
    document.undo();QCOMPARE(model->rowCount(),initialRows);QCOMPARE(model->columnCount(),initialColumns);document.redo();QVERIFY(model->rowCount()>=122);QVERIFY(model->columnCount()>=32);
    document.table()->selectionModel()->select(QItemSelection(model->index(120,30),model->index(121,31)),QItemSelectionModel::ClearAndSelect);document.clearSelection();QCOMPARE(model->rowCount(),initialRows);QCOMPARE(model->columnCount(),initialColumns);QVERIFY(document.cellText(121,31).isEmpty());
    document.undo();QCOMPARE(document.cellText(121,31),QStringLiteral("44"));QVERIFY(model->rowCount()>=122);QVERIFY(model->columnCount()>=32);
}
void WorkbookEditorTests::pendingFormulaBarIsDirtyAndSaved(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());WorkbookDocument document;QVERIFY(!document.isModified());
    auto* formula=document.findChild<QLineEdit*>(QStringLiteral("workbookFormula"));QVERIFY(formula);formula->setText(QStringLiteral("=1+2"));QVERIFY(document.isModified());
    QString error;const auto path=temporary.filePath(QStringLiteral("pending.xlsx"));QVERIFY2(document.saveAs(path,&error),qPrintable(error));QVERIFY(!document.isModified());QCOMPARE(document.cellText(0,0),QStringLiteral("=1+2"));
    std::atomic_bool cancel{false};const auto book=readWorkbook(readFile(path),QStringLiteral("xlsx"),cancel);QCOMPARE(book.sheets[0].cells.value(cellKey(0,0)).kind,CellKind::Formula);
}
void WorkbookEditorTests::worksheetEditsRemainIndependent(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const auto path=temporary.filePath("input.xlsx");QVERIFY(writeFile(path,fixture()));WorkbookDocument document;QSignalSpy loaded(&document,&WorkbookDocument::loadingFinished);QString error;QVERIFY2(document.openFile(path,&error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,5000);QVERIFY(loaded.front().front().toBool());
    QCOMPARE(document.sheetNames(),QStringList({"Measurements","Untouched"}));QVERIFY2(document.setCell(0,0,"17",&error),qPrintable(error));QVERIFY(document.selectSheet(1));QVERIFY2(document.setCell(0,0,"second edited",&error),qPrintable(error));QVERIFY(document.selectSheet(0));QCOMPARE(document.cellText(0,0),QStringLiteral("17"));
    const auto copy=temporary.filePath("copy.xlsx");QVERIFY2(document.saveAs(copy,&error),qPrintable(error));std::atomic_bool cancel{false};const auto result=readWorkbook(readFile(copy),"xlsx",cancel);QCOMPARE(result.sheets[0].cells.value(cellKey(0,0)).text,QStringLiteral("17"));QCOMPARE(result.sheets[1].cells.value(cellKey(0,0)).text,QStringLiteral("second edited"));QCOMPARE(readFile(path),fixture());
}
void WorkbookEditorTests::cutFormulaTextPreservesTableCells(){
    WorkbookDocument document;QString error;
    QVERIFY2(document.setCell(0,0,QStringLiteral("abcdef"),&error),qPrintable(error));
    auto* formula=document.findChild<QLineEdit*>(QStringLiteral("workbookFormula"));QVERIFY(formula);
    document.resize(600,400);document.show();document.activateWindow();formula->setFocus();
    QTRY_VERIFY_WITH_TIMEOUT(formula->hasFocus(),1000);formula->setSelection(1,3);
    document.cut();
    QCOMPARE(QApplication::clipboard()->text(),QStringLiteral("bcd"));
    QCOMPARE(formula->text(),QStringLiteral("aef"));
    QCOMPARE(document.cellText(0,0),QStringLiteral("abcdef"));
    document.undo();QCOMPARE(formula->text(),QStringLiteral("abcdef"));
    QCOMPARE(document.cellText(0,0),QStringLiteral("abcdef"));
    document.table()->setFocus();
    QTRY_VERIFY_WITH_TIMEOUT(document.table()->hasFocus(),1000);
    const auto cell=document.table()->model()->index(0,0);
    document.table()->selectionModel()->select(cell,QItemSelectionModel::ClearAndSelect);
    document.cut();QVERIFY(document.cellText(0,0).isEmpty());
    document.undo();QCOMPARE(document.cellText(0,0),QStringLiteral("abcdef"));
}
void WorkbookEditorTests::worksheetSwitchCommitsOrRetainsFormulaEdits(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const auto path=temporary.filePath("input.xlsx");
    const auto original=fixture();QVERIFY(writeFile(path,original));WorkbookDocument document;
    QSignalSpy loaded(&document,&WorkbookDocument::loadingFinished);QString error;
    QVERIFY2(document.openFile(path,&error),qPrintable(error));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,5000);QVERIFY(loaded.front().front().toBool());
    auto* formula=document.findChild<QLineEdit*>(QStringLiteral("workbookFormula"));
    auto* sheets=document.findChild<QComboBox*>(QStringLiteral("workbookSheet"));QVERIFY(formula&&sheets);
    formula->setText(QStringLiteral("=2+3"));QVERIFY(document.selectSheet(1));
    QCOMPARE(document.cellText(0,0),QStringLiteral("retained second sheet"));
    QVERIFY(document.selectSheet(0));QCOMPARE(document.cellText(0,0),QStringLiteral("=2+3"));
    document.undo();QCOMPARE(document.cellText(0,0),QStringLiteral("2.5"));
    formula->setText(QStringLiteral("="));QSignalSpy errors(&document,&WorkbookDocument::error);
    sheets->setCurrentIndex(1);
    QCOMPARE(document.activeSheet(),0);QCOMPARE(sheets->currentIndex(),0);
    QCOMPARE(formula->text(),QStringLiteral("="));QVERIFY(document.isModified());QCOMPARE(errors.size(),1);
    QVERIFY(!document.selectSheet(1));QCOMPARE(document.activeSheet(),0);
    QCOMPARE(formula->text(),QStringLiteral("="));QCOMPARE(errors.size(),2);
    QVERIFY(!document.save(&error));QCOMPARE(formula->text(),QStringLiteral("="));
    QCOMPARE(readFile(path),original);
}
void WorkbookEditorTests::rejectedCutPreservesCellsAndClipboard(){
    WorkbookDocument document;QString error;
    QVERIFY2(document.setCell(0,0,QStringLiteral("near"),&error),qPrintable(error));
    QVERIFY2(document.setCell(1000,1000,QStringLiteral("far"),&error),qPrintable(error));
    auto* table=document.table();auto* selection=table->selectionModel();
    selection->select(table->model()->index(0,0),QItemSelectionModel::ClearAndSelect);
    selection->select(table->model()->index(1000,1000),QItemSelectionModel::Select);
    QApplication::clipboard()->setText(QStringLiteral("retained clipboard"));
    QSignalSpy errors(&document,&WorkbookDocument::error);document.cut();
    QCOMPARE(errors.size(),1);
    QCOMPARE(document.cellText(0,0),QStringLiteral("near"));
    QCOMPARE(document.cellText(1000,1000),QStringLiteral("far"));
    QCOMPARE(QApplication::clipboard()->text(),QStringLiteral("retained clipboard"));
    document.undo();QVERIFY(document.cellText(1000,1000).isEmpty());
    QCOMPARE(document.cellText(0,0),QStringLiteral("near"));
}
void WorkbookEditorTests::externalChangeRefusesOverwrite(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const auto path=temporary.filePath("input.xlsx");QVERIFY(writeFile(path,fixture()));WorkbookDocument document;QSignalSpy loaded(&document,&WorkbookDocument::loadingFinished);QString error;QVERIFY(document.openFile(path,&error));QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,5000);QVERIFY(loaded.front().front().toBool());QVERIFY(document.setCell(0,0,"9",&error));QVERIFY(writeFile(path,"external replacement"));QVERIFY(!document.save(&error));QVERIFY(error.contains("changed"));QCOMPARE(readFile(path),QByteArray("external replacement"));QVERIFY(document.isModified());QVERIFY2(document.saveAs(temporary.filePath("retained.xlsx"),&error),qPrintable(error));
}
void WorkbookEditorTests::failedSaveAndDirtyOpenPreserveEdits(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const auto original=temporary.filePath("input.xlsx");QVERIFY(writeFile(original,fixture()));WorkbookDocument document;QString error;QVERIFY(document.setCell(0,0,"123",&error));QVERIFY(!document.openFile(original,&error));QCOMPARE(document.cellText(0,0),QStringLiteral("123"));QVERIFY(!document.saveAs(temporary.filePath("missing/out.xlsx"),&error));QVERIFY(document.isModified());QCOMPARE(readFile(original),fixture());
}
void WorkbookEditorTests::unsupportedFormulaRangesRemainProtected(){
    std::atomic_bool cancel{false};auto parts=readPackage(fixture(),cancel);for(auto& part:parts)if(part.name=="xl/worksheets/sheet1.xml")part.data.replace("<f>A1*2</f>","<f t=\"array\" ref=\"B1:B3\">A1*2</f>");const auto book=readWorkbook(writePackage(parts),"xlsx",cancel);QVERIFY(formulaRangeProtected(book.sheets[0],2,1));QVERIFY(!formulaRangeProtected(book.sheets[0],2,0));
}

PENTA_OFFSCREEN_TEST_MAIN(WorkbookEditorTests)
#include "workbook_editor_tests.moc"
