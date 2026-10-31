#include "workbook_data.hpp"

#include <QDir>
#include <QDomDocument>
#include <QLocale>
#include <QSet>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <minizip/ioapi.h>
#include <minizip/unzip.h>
#include <minizip/zip.h>

namespace penta::workbook {
namespace {
constexpr qsizetype ExpandedLimit=64*1024*1024,PartLimit=16*1024*1024;
constexpr auto MainNs="http://schemas.openxmlformats.org/spreadsheetml/2006/main";
constexpr auto RelNs="http://schemas.openxmlformats.org/officeDocument/2006/relationships";
[[noreturn]] void fail(const QString& message){throw std::runtime_error(message.toUtf8().constData());}
void poll(const std::atomic_bool& cancel){if(cancel.load(std::memory_order_relaxed))fail(QStringLiteral("Workbook loading cancelled."));}
QString decode(const QByteArray& bytes){
    QStringDecoder decoder(QStringDecoder::Utf8,QStringDecoder::Flag::Stateless);
    // Decoding is lazy: materialize the QString before inspecting error state.
    // Each input is complete, so a trailing incomplete sequence is invalid too.
    const QString text=decoder.decode(bytes);
    if(decoder.hasError())fail(QStringLiteral("The file is not valid UTF-8."));
    return text;
}
void validText(const QString& text){
    if(text.size()>MaximumCellCharacters)fail(QStringLiteral("A cell exceeds the 65,536-character limit."));
    for(qsizetype i=0;i<text.size();++i){const auto c=text[i];
        if(c.unicode()<32&&c!='\t'&&c!='\r'&&c!='\n')fail(QStringLiteral("Cell text contains an unsupported control character."));
        if(c.isHighSurrogate()){if(i+1>=text.size()||!text[++i].isLowSurrogate())fail(QStringLiteral("Cell text contains invalid Unicode."));}
        else if(c.isLowSurrogate())fail(QStringLiteral("Cell text contains invalid Unicode."));
    }
}
struct MemoryFile {QByteArray bytes;qint64 position=0;bool failed=false;};
voidpf ZCALLBACK openMemory(voidpf opaque,const void*,int mode){auto* file=static_cast<MemoryFile*>(opaque);file->position=0;if(mode&ZLIB_FILEFUNC_MODE_CREATE)file->bytes.clear();return opaque;}
uLong ZCALLBACK readMemory(voidpf,voidpf stream,void* buffer,uLong size){auto* file=static_cast<MemoryFile*>(stream);const auto count=std::min<qint64>(size,std::max<qint64>(0,file->bytes.size()-file->position));if(count)std::memcpy(buffer,file->bytes.constData()+file->position,static_cast<std::size_t>(count));file->position+=count;return static_cast<uLong>(count);}
uLong ZCALLBACK writeMemory(voidpf,voidpf stream,const void* buffer,uLong size){
    auto* file=static_cast<MemoryFile*>(stream);if(file->position<0||size>quint64(ExpandedLimit+MaximumInputBytes-file->position)){file->failed=true;return 0;}
    try{const auto end=file->position+size;if(end>file->bytes.size())file->bytes.resize(end);if(size)std::memcpy(file->bytes.data()+file->position,buffer,size);file->position=end;return size;}catch(...){file->failed=true;return 0;}
}
ZPOS64_T ZCALLBACK tellMemory(voidpf,voidpf stream){return static_cast<ZPOS64_T>(static_cast<MemoryFile*>(stream)->position);}
long ZCALLBACK seekMemory(voidpf,voidpf stream,ZPOS64_T offset,int origin){auto* file=static_cast<MemoryFile*>(stream);const auto delta=static_cast<qint64>(offset);const qint64 base=origin==ZLIB_FILEFUNC_SEEK_SET?0:origin==ZLIB_FILEFUNC_SEEK_CUR?file->position:file->bytes.size();if(delta<-base||delta>ExpandedLimit+MaximumInputBytes-base){file->failed=true;return -1;}file->position=base+delta;return 0;}
int ZCALLBACK closeMemory(voidpf,voidpf){return 0;}
int ZCALLBACK errorMemory(voidpf,voidpf stream){return static_cast<MemoryFile*>(stream)->failed?1:0;}
zlib_filefunc64_def memoryFunctions(MemoryFile& file){return {openMemory,readMemory,writeMemory,tellMemory,seekMemory,closeMemory,errorMemory,&file};}
QString local(const QDomElement& element){return element.localName().isEmpty()?element.tagName().section(':',-1):element.localName();}
QDomElement child(const QDomElement& parent,const QString& name){for(auto node=parent.firstChild();!node.isNull();node=node.nextSibling()){auto element=node.toElement();if(!element.isNull()&&local(element)==name)return element;}return {};}
QDomDocument xml(const QByteArray& bytes){
    if(bytes.size()>PartLimit)fail(QStringLiteral("A workbook XML part exceeds its limit."));
    // Validate tokens before constructing a DOM. The streaming reader recognizes
    // UTF-8/UTF-16 declarations, unlike byte scans, and distinguishes a DTD from
    // harmless literal text inside a CDATA section. No external entity is read.
    QXmlStreamReader preflight(bytes);int depth=0,nodes=0;
    while(!preflight.atEnd()){
        const auto token=preflight.readNext();
        if(token==QXmlStreamReader::DTD||token==QXmlStreamReader::EntityReference)
            fail(QStringLiteral("DTD/entity declarations are not supported in workbook XML."));
        if(token==QXmlStreamReader::StartElement){
            if(++depth>64||++nodes>600000)fail(QStringLiteral("Workbook XML exceeds its nesting/node limit."));
        }else if(token==QXmlStreamReader::EndElement)--depth;
    }
    if(preflight.hasError())fail(QStringLiteral("Malformed workbook XML: %1").arg(preflight.errorString()));
    QDomDocument document;
#if QT_VERSION >= QT_VERSION_CHECK(6,5,0)
    const auto result=document.setContent(bytes,QDomDocument::ParseOption::UseNamespaceProcessing);if(!result)fail(QStringLiteral("Malformed workbook XML: %1").arg(result.errorMessage));
#else
    QString error;int line=0,column=0;if(!document.setContent(bytes,true,&error,&line,&column))fail(QStringLiteral("Malformed workbook XML: %1").arg(error));
#endif
    return document;
}
QDomElement mainElement(QDomDocument& document,const QString& name){return document.createElementNS(QString::fromLatin1(MainNs),name);}
const QByteArray& part(const WorkbookData& data,const QString& name){for(const auto& item:data.parts)if(decode(item.name)==name)return item.data;fail(QStringLiteral("A required workbook part is missing: %1").arg(name));}
QString relatedPath(const QString& owner,const QString& target){
    if(target.contains('\\')||target.contains(':')||target.contains('?')||target.contains('#'))fail(QStringLiteral("Unsupported workbook relationship path."));
    const auto base=owner.left(owner.lastIndexOf('/')+1);const auto value=QDir::cleanPath(target.startsWith('/')?target.mid(1):base+target);
    if(value.isEmpty()||value.startsWith('/')||value==".."||value.startsWith("../"))fail(QStringLiteral("Workbook relationship escapes the package."));
    return value;
}
QString relPart(const QString& name){const auto slash=name.lastIndexOf('/');return name.left(slash+1)+QStringLiteral("_rels/")+name.mid(slash+1)+QStringLiteral(".rels");}
QString inlineText(const QDomElement& node){QString text;std::function<void(const QDomElement&)> walk=[&](const auto& element){if(local(element)=="rPh")return;if(local(element)=="t")text+=element.text();else for(auto n=element.firstChild();!n.isNull();n=n.nextSibling())if(n.isElement())walk(n.toElement());if(text.size()>MaximumCellCharacters)fail(QStringLiteral("A workbook string exceeds the cell limit."));};walk(node);validText(text);return text;}
std::pair<int,int> address(QString text){text.remove('$');int column=0,pos=0;while(pos<text.size()&&text[pos].isLetter()){const auto c=text[pos++].toUpper().unicode();if(c<'A'||c>'Z')fail(QStringLiteral("Invalid worksheet cell address."));column=column*26+c-'A'+1;if(column>MaximumColumns)fail(QStringLiteral("Worksheet column exceeds Excel bounds."));}bool ok=false;const auto row=text.mid(pos).toInt(&ok);if(!ok||row<1||row>MaximumRows||column<1)fail(QStringLiteral("Invalid worksheet cell address."));return {row-1,column-1};}
CellRange range(const QString& text){const auto terms=text.split(':');if(terms.size()>2)fail(QStringLiteral("Invalid formula range."));const auto a=address(terms.front()),b=address(terms.back());return {std::min(a.first,b.first),std::max(a.first,b.first),std::min(a.second,b.second),std::max(a.second,b.second)};}
CellMap currentCells(const Sheet& sheet,const CellMap& changes){auto cells=sheet.cells;for(auto it=changes.cbegin();it!=changes.cend();++it){if(it->kind==CellKind::Text&&it->text.isEmpty())cells.remove(it.key());else cells.insert(it.key(),it.value());}return cells;}
void replacePart(QVector<ArchivePart>& parts,const QString& name,const QByteArray& bytes){for(auto& item:parts)if(decode(item.name)==name){item.data=bytes;return;}parts.push_back({name.toUtf8(),bytes});}
void setCellXml(QDomDocument& document,QDomElement root,int row,int column,const Cell& value){
    auto data=child(root,QStringLiteral("sheetData"));if(data.isNull()){data=mainElement(document,QStringLiteral("sheetData"));root.appendChild(data);}
    QDomElement rowNode;int implicit=0;
    for(auto node=data.firstChild();!node.isNull();node=node.nextSibling())if(node.isElement()&&local(node.toElement())=="row"){
        auto candidate=node.toElement();const int number=candidate.attribute("r",QString::number(implicit+1)).toInt()-1;implicit=number+1;if(number==row){rowNode=candidate;break;}if(number>row){rowNode=mainElement(document,"row");rowNode.setAttribute("r",row+1);data.insertBefore(rowNode,node);break;}}
    if(rowNode.isNull()){rowNode=mainElement(document,"row");rowNode.setAttribute("r",row+1);data.appendChild(rowNode);}
    QDomElement cell;implicit=0;
    for(auto node=rowNode.firstChild();!node.isNull();node=node.nextSibling())if(node.isElement()&&local(node.toElement())=="c"){
        auto candidate=node.toElement();const auto reference=candidate.attribute("r");const int number=reference.isEmpty()?implicit:address(reference).second;implicit=number+1;
        if(number==column){cell=candidate;break;}if(number>column){cell=mainElement(document,"c");cell.setAttribute("r",cellAddress(row,column));rowNode.insertBefore(cell,node);break;}}
    if(cell.isNull()){cell=mainElement(document,"c");cell.setAttribute("r",cellAddress(row,column));rowNode.appendChild(cell);}
    for(auto node=cell.firstChild();!node.isNull();){const auto next=node.nextSibling();if(node.isElement()&&QStringList{"f","v","is"}.contains(local(node.toElement())))cell.removeChild(node);node=next;}
    cell.removeAttribute("t");const auto append=[&](const QString& tag,const QString& text){auto element=mainElement(document,tag);element.appendChild(document.createTextNode(text));cell.appendChild(element);};
    if(value.kind==CellKind::Text){if(!value.text.isEmpty()){cell.setAttribute("t","inlineStr");auto is=mainElement(document,"is"),t=mainElement(document,"t");t.setAttribute("xml:space","preserve");t.appendChild(document.createTextNode(value.text));is.appendChild(t);cell.appendChild(is);}}
    else if(value.kind==CellKind::Formula)append("f",value.text);
    else {if(value.kind==CellKind::Boolean)cell.setAttribute("t","b");else if(value.kind==CellKind::Error)cell.setAttribute("t","e");append("v",value.text);}
}
QVector<ArchivePart> minimalPackage(const QVector<Sheet>& sheets){
    QVector<ArchivePart> parts;
    QByteArray types="<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>";
    QDomDocument workbook;auto book=mainElement(workbook,"workbook");book.setAttribute("xmlns:r",QString::fromLatin1(RelNs));workbook.appendChild(book);auto list=mainElement(workbook,"sheets");book.appendChild(list);
    QDomDocument relationships;auto rels=relationships.createElementNS("http://schemas.openxmlformats.org/package/2006/relationships","Relationships");relationships.appendChild(rels);
    for(qsizetype i=0;i<sheets.size();++i){auto sheet=mainElement(workbook,"sheet");sheet.setAttribute("name",sheets[i].name);sheet.setAttribute("sheetId",i+1);sheet.setAttributeNS(QString::fromLatin1(RelNs),"r:id",QStringLiteral("rId%1").arg(i+1));list.appendChild(sheet);auto rel=relationships.createElementNS(rels.namespaceURI(),"Relationship");rel.setAttribute("Id",QStringLiteral("rId%1").arg(i+1));rel.setAttribute("Type",QString::fromLatin1(RelNs)+"/worksheet");rel.setAttribute("Target",QStringLiteral("worksheets/sheet%1.xml").arg(i+1));rels.appendChild(rel);types+="<Override PartName=\"/xl/worksheets/sheet"+QByteArray::number(i+1)+".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";}
    types+="</Types>";parts.push_back({"[Content_Types].xml",types});parts.push_back({"_rels/.rels","<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>"});parts.push_back({"xl/workbook.xml",workbook.toByteArray(-1)});parts.push_back({"xl/_rels/workbook.xml.rels",relationships.toByteArray(-1)});return parts;
}
} // namespace

quint64 cellKey(int row,int column){return (quint64(row)<<14)|quint64(column);}
int cellRow(quint64 key){return static_cast<int>(key>>14);}
int cellColumn(quint64 key){return static_cast<int>(key&16383);}
QString cellAddress(int row,int column){QString name;for(int value=column+1;value;value=(value-1)/26)name.prepend(QChar('A'+(value-1)%26));return name+QString::number(row+1);}
QString cellDisplay(const Cell& cell){if(cell.kind==CellKind::Formula)return cell.cached.isEmpty()?QStringLiteral("=")+cell.text:cell.cached;if(cell.kind==CellKind::Boolean)return cell.text=="1"?QStringLiteral("TRUE"):QStringLiteral("FALSE");return cell.text;}
QString cellEditText(const Cell& cell){return cell.kind==CellKind::Formula?QStringLiteral("=")+cell.text:cellDisplay(cell);}
Cell enteredCell(const QString& text,bool formulas){validText(text);if(text.startsWith('\''))return {CellKind::Text,text.mid(1),{}};if(formulas&&text.startsWith('=')){if(text.size()==1)fail(QStringLiteral("Enter a formula after '=' or prefix it with an apostrophe for literal text."));return {CellKind::Formula,text.mid(1),{}};}if(text.compare("TRUE",Qt::CaseInsensitive)==0)return {CellKind::Boolean,"1",{}};if(text.compare("FALSE",Qt::CaseInsensitive)==0)return {CellKind::Boolean,"0",{}};bool ok=false;const auto value=text.toDouble(&ok);if(ok&&std::isfinite(value)&&text==text.trimmed())return {CellKind::Number,text,{}};return {CellKind::Text,text,{}};}
bool formulaRangeProtected(const Sheet& sheet,int row,int column){return std::any_of(sheet.protectedFormulaRanges.cbegin(),sheet.protectedFormulaRanges.cend(),[&](const auto& r){return row>=r.firstRow&&row<=r.lastRow&&column>=r.firstColumn&&column<=r.lastColumn;});}

QVector<ArchivePart> readPackage(const QByteArray& bytes,const std::atomic_bool& cancel){
    MemoryFile memory{bytes};auto functions=memoryFunctions(memory);unzFile archive=unzOpen2_64("memory",&functions);if(!archive)fail(QStringLiteral("The XLSX package is malformed or unreadable."));
    QVector<ArchivePart> result;QSet<QByteArray> names;qsizetype expanded=0;
    try{int status=unzGoToFirstFile(archive);while(status==UNZ_OK){poll(cancel);if(result.size()>=4096)fail(QStringLiteral("The workbook has too many ZIP parts."));unz_file_info64 info{};if(unzGetCurrentFileInfo64(archive,&info,nullptr,0,nullptr,0,nullptr,0)!=UNZ_OK||info.size_filename>4096||info.uncompressed_size>quint64(PartLimit)||info.flag&1)fail(QStringLiteral("A workbook part is encrypted, malformed or exceeds its limit."));QByteArray name(info.size_filename+1,'\0');if(unzGetCurrentFileInfo64(archive,&info,name.data(),static_cast<uLong>(name.size()),nullptr,0,nullptr,0)!=UNZ_OK)fail(QStringLiteral("A workbook part name cannot be read."));name.resize(info.size_filename);const auto decoded=decode(name);if(decoded.contains('\\')||decoded.startsWith('/')||decoded.split('/').contains("..")||decoded.contains(QChar::Null)||names.contains(name))fail(QStringLiteral("The workbook has unsafe or duplicate ZIP part names."));if(decoded.startsWith("_xmlsignatures/",Qt::CaseInsensitive))fail(QStringLiteral("Digitally signed workbooks require a signature-aware editor."));names.insert(name);expanded+=static_cast<qsizetype>(info.uncompressed_size);if(expanded>ExpandedLimit)fail(QStringLiteral("The expanded workbook exceeds 64 MiB."));if(unzOpenCurrentFile(archive)!=UNZ_OK)fail(QStringLiteral("A workbook part cannot be opened."));QByteArray data;data.reserve(static_cast<qsizetype>(info.uncompressed_size));char buffer[32768];int count=0;while((count=unzReadCurrentFile(archive,buffer,sizeof buffer))>0){poll(cancel);data.append(buffer,count);if(data.size()>PartLimit)fail(QStringLiteral("A workbook part exceeds its limit."));}const int closed=unzCloseCurrentFile(archive);if(count<0||closed!=UNZ_OK||data.size()!=static_cast<qsizetype>(info.uncompressed_size))fail(QStringLiteral("A workbook part is truncated or has a bad checksum."));result.push_back({name,std::move(data),static_cast<quint32>(info.flag)});status=unzGoToNextFile(archive);}if(status!=UNZ_END_OF_LIST_OF_FILE)fail(QStringLiteral("The workbook directory is malformed."));unzClose(archive);return result;}catch(...){unzClose(archive);throw;}
}
QByteArray writePackage(const QVector<ArchivePart>& parts){
    MemoryFile memory;auto functions=memoryFunctions(memory);zipFile archive=zipOpen2_64("memory",APPEND_STATUS_CREATE,nullptr,&functions);if(!archive)fail(QStringLiteral("Cannot create an XLSX package."));
    try{for(const auto& item:parts){if(zipOpenNewFileInZip4_64(archive,item.name.constData(),nullptr,nullptr,0,nullptr,0,nullptr,Z_DEFLATED,Z_BEST_SPEED,0,-MAX_WBITS,DEF_MEM_LEVEL,Z_DEFAULT_STRATEGY,nullptr,0,0,1U<<11,0)!=ZIP_OK)fail(QStringLiteral("Cannot create a workbook part."));if(zipWriteInFileInZip(archive,item.data.constData(),static_cast<unsigned>(item.data.size()))!=ZIP_OK||zipCloseFileInZip(archive)!=ZIP_OK)fail(QStringLiteral("Cannot write a workbook part."));}const int closed=zipClose(archive,nullptr);archive=nullptr;if(closed!=ZIP_OK||memory.failed)fail(QStringLiteral("Cannot finish the XLSX package."));return memory.bytes;}catch(...){if(archive)zipClose(archive,nullptr);throw;}
}

QVector<QStringList> parseDelimited(const QString& text,QChar delimiter,const std::atomic_bool* cancel){
    QVector<QStringList> rows;QStringList row;QString cell;bool quoted=false,afterQuote=false;qsizetype characters=0;int cells=0;
    const auto finishCell=[&]{validText(cell);characters+=cell.size();if(++cells>MaximumCells||characters>MaximumTextCharacters||row.size()>=MaximumColumns)fail(QStringLiteral("The table exceeds its cell/text limits."));row.push_back(cell);cell.clear();afterQuote=false;};
    const auto finishRow=[&]{finishCell();if(rows.size()>=MaximumRows)fail(QStringLiteral("The table exceeds its row limit."));rows.push_back(row);row.clear();};
    for(qsizetype i=0;i<text.size();++i){if(cancel&&(i&4095)==0)poll(*cancel);const auto c=text[i];if(quoted){if(c=='"'){if(i+1<text.size()&&text[i+1]=='"'){cell+='"';++i;}else{quoted=false;afterQuote=true;}}else cell+=c;}else if(c==delimiter)finishCell();else if(c=='\r'||c=='\n'){if(c=='\r'&&i+1<text.size()&&text[i+1]=='\n')++i;finishRow();}else if(c=='"'){if(!cell.isEmpty()||afterQuote)fail(QStringLiteral("A quote appears inside an unquoted field."));quoted=true;}else{if(afterQuote)fail(QStringLiteral("Unexpected text after a quoted field."));cell+=c;}if(cell.size()>MaximumCellCharacters)fail(QStringLiteral("A table cell exceeds its limit."));}
    if(quoted)fail(QStringLiteral("A quoted field is not terminated."));
    if(!cell.isEmpty()||!row.isEmpty()||afterQuote||(!text.isEmpty()&&text.back()==delimiter))finishRow();
    return rows;
}
QString encodeDelimited(const QVector<QStringList>& rows,QChar delimiter,const QString& newline,bool trailingNewline){QString result;for(qsizetype r=0;r<rows.size();++r){if(r)result+=newline;for(qsizetype c=0;c<rows[r].size();++c){if(c)result+=delimiter;auto value=rows[r][c];if(value.contains(delimiter)||value.contains('"')||value.contains('\r')||value.contains('\n')){value.replace("\"","\"\"");result+='"'+value+'"';}else result+=value;}}if(trailingNewline&&!rows.isEmpty())result+=newline;return result;}

WorkbookData readWorkbook(const QByteArray& bytes,const QString& requested,const std::atomic_bool& cancel){
    if(bytes.size()>MaximumInputBytes)fail(QStringLiteral("The workbook exceeds the 16 MiB input limit."));
    WorkbookData out;out.sourceBytes=bytes;out.format=requested.toLower();
    if(out.format=="csv"||out.format=="tsv"){
        auto content=bytes;out.bom=content.startsWith(QByteArray::fromHex("efbbbf"));if(out.bom)content.remove(0,3);const auto text=decode(content);const auto rows=parseDelimited(text,out.format=="tsv"?'\t':',',&cancel);out.newline=text.contains("\r\n")?QStringLiteral("\r\n"):QStringLiteral("\n");out.trailingNewline=text.endsWith('\n')||text.endsWith('\r');Sheet sheet;sheet.name="Sheet1";sheet.rows=rows.size();for(int r=0;r<rows.size();++r){out.csvWidths.push_back(rows[r].size());sheet.columns=std::max(sheet.columns,static_cast<int>(rows[r].size()));for(int c=0;c<rows[r].size();++c)if(!rows[r][c].isEmpty())sheet.cells.insert(cellKey(r,c),Cell{CellKind::Text,rows[r][c],{}});}out.sheets.push_back(std::move(sheet));return out;
    }
    if(out.format!="xlsx")fail(QStringLiteral("Editable documents support CSV, TSV and XLSX."));
    out.parts=readPackage(bytes,cancel);
    out.workbookPart="xl/workbook.xml";for(auto e=xml(part(out,"_rels/.rels")).documentElement().firstChildElement();!e.isNull();e=e.nextSiblingElement())if(e.attribute("Type").endsWith("/officeDocument")){if(e.attribute("TargetMode")=="External")fail(QStringLiteral("An external workbook relationship is unsupported."));out.workbookPart=relatedPath({},e.attribute("Target"));break;}
    out.relationshipsPart=relPart(out.workbookPart);QMap<QString,QString> relationships;QString stringsPart;
    const auto relDoc=xml(part(out,out.relationshipsPart));for(auto e=relDoc.documentElement().firstChildElement();!e.isNull();e=e.nextSiblingElement()){if(e.attribute("TargetMode")=="External")continue;const auto target=relatedPath(out.workbookPart,e.attribute("Target"));relationships.insert(e.attribute("Id"),target);if(e.attribute("Type").endsWith("/sharedStrings"))stringsPart=target;}
    QStringList strings;qsizetype totalText=0;if(!stringsPart.isEmpty()){const auto doc=xml(part(out,stringsPart));for(auto si=doc.documentElement().firstChildElement();!si.isNull();si=si.nextSiblingElement())if(local(si)=="si"){const auto value=inlineText(si);totalText+=value.size();if(strings.size()>=MaximumCells||totalText>MaximumTextCharacters)fail(QStringLiteral("Shared strings exceed the workbook limits."));strings.push_back(value);}}
    const auto book=xml(part(out,out.workbookPart));const auto list=child(book.documentElement(),"sheets");int count=0;
    for(auto e=list.firstChildElement();!e.isNull();e=e.nextSiblingElement())if(local(e)=="sheet"){
        poll(cancel);if(out.sheets.size()>=64)fail(QStringLiteral("The workbook exceeds the 64-worksheet limit."));Sheet sheet;sheet.name=e.attribute("name");const auto id=e.attributeNS(QString::fromLatin1(RelNs),"id",e.attribute("r:id"));sheet.part=relationships.value(id);if(sheet.part.isEmpty())fail(QStringLiteral("A worksheet relationship is missing."));const auto doc=xml(part(out,sheet.part));if(local(doc.documentElement())!="worksheet")fail(QStringLiteral("This workbook includes an unsupported non-worksheet sheet."));const auto data=child(doc.documentElement(),"sheetData");int implicitRow=0;
        for(auto r=data.firstChildElement();!r.isNull();r=r.nextSiblingElement())if(local(r)=="row"){
            bool ok=false;const int row=r.attribute("r",QString::number(implicitRow+1)).toInt(&ok)-1;if(!ok||row<0||row>=MaximumRows)fail(QStringLiteral("Invalid worksheet row."));implicitRow=row+1;sheet.rows=std::max(sheet.rows,row+1);int implicitColumn=0;
            for(auto c=r.firstChildElement();!c.isNull();c=c.nextSiblingElement())if(local(c)=="c"){
                poll(cancel);if(++count>MaximumCells)fail(QStringLiteral("The workbook exceeds the 100,000-cell editing limit."));const auto ref=c.attribute("r");const auto position=ref.isEmpty()?std::pair<int,int>{row,implicitColumn}:address(ref);if(position.first!=row||position.second>=MaximumColumns)fail(QStringLiteral("Worksheet cell/row addresses disagree."));const int col=position.second;implicitColumn=col+1;sheet.columns=std::max(sheet.columns,col+1);const auto type=c.attribute("t"),v=child(c,"v").text();const auto formula=child(c,"f");Cell value;
                if(!formula.isNull()){value={CellKind::Formula,formula.text(),v};const auto t=formula.attribute("t");if(t=="shared"||t=="array"||t=="dataTable"){sheet.protectedFormulaRanges.push_back(formula.hasAttribute("ref")?range(formula.attribute("ref")):CellRange{row,row,col,col});}}
                else if(type=="s"){bool valid=false;const int index=v.toInt(&valid);if(!valid||index<0||index>=strings.size())fail(QStringLiteral("Invalid shared-string index."));value={CellKind::Text,strings[index],{}};}
                else if(type=="inlineStr")value={CellKind::Text,inlineText(child(c,"is")),{}};
                else if(type=="b")value={CellKind::Boolean,v,{}};
                else if(type=="e")value={CellKind::Error,v,{}};
                else if(type=="str"||type=="d")value={CellKind::Text,v,{}};
                else value={v.isEmpty()?CellKind::Text:CellKind::Number,v,{}};
                validText(value.text);totalText+=value.text.size()+value.cached.size();if(totalText>MaximumTextCharacters)fail(QStringLiteral("Workbook text exceeds the 8 MiB limit."));const auto key=cellKey(row,col);if(sheet.cells.contains(key))fail(QStringLiteral("The worksheet contains duplicate cell addresses."));sheet.cells.insert(key,std::move(value));
            }
        }
        out.sheets.push_back(std::move(sheet));
    }
    if(out.sheets.isEmpty())fail(QStringLiteral("The workbook contains no supported worksheets."));
    return out;
}

QByteArray writeWorkbook(const WorkbookData& data,const QVector<CellMap>& changes,const QString& requested){
    const auto format=requested.toLower();if(changes.size()!=data.sheets.size())fail(QStringLiteral("Workbook change metadata is inconsistent."));
    bool changed=false;for(const auto& sheet:changes)changed|=!sheet.isEmpty();
    if(format==data.format&&!changed&&!data.sourceBytes.isEmpty())return data.sourceBytes;
    if(format=="csv"||format=="tsv"){
        if(data.sheets.size()!=1)fail(QStringLiteral("Use Export Active Sheet to write a multi-sheet workbook as CSV."));
        const auto cells=currentCells(data.sheets.front(),changes.front());int rows=data.sheets.front().rows;for(auto it=cells.cbegin();it!=cells.cend();++it)rows=std::max(rows,cellRow(it.key())+1);if(rows>MaximumCells)fail(QStringLiteral("CSV expansion exceeds the 100,000-cell limit."));QVector<QStringList> lines;lines.reserve(rows);qint64 area=0;
        for(int r=0;r<rows;++r){int columns=r<data.csvWidths.size()?data.csvWidths[r]:0;auto it=cells.lowerBound(cellKey(r,0));for(auto scan=it;scan!=cells.end()&&cellRow(scan.key())==r;++scan)columns=std::max(columns,cellColumn(scan.key())+1);if((area+=std::max(1,columns))>MaximumCells)fail(QStringLiteral("CSV expansion exceeds the 100,000-cell limit."));QStringList line;for(int c=0;c<columns;++c)line.push_back(cellEditText(cells.value(cellKey(r,c))));lines.push_back(std::move(line));}
        auto bytes=encodeDelimited(lines,format=="tsv"?'\t':',',data.newline,data.trailingNewline).toUtf8();if(data.bom)bytes.prepend(QByteArray::fromHex("efbbbf"));return bytes;
    }
    if(format!="xlsx")fail(QStringLiteral("Save supports XLSX, CSV and TSV."));
    auto parts=data.parts.isEmpty()?minimalPackage(data.sheets):data.parts;
    const QString workbook=data.parts.isEmpty()?QStringLiteral("xl/workbook.xml"):data.workbookPart;
    const QString relations=data.parts.isEmpty()?QStringLiteral("xl/_rels/workbook.xml.rels"):data.relationshipsPart;
    for(qsizetype index=0;index<data.sheets.size();++index){
        const auto& sheet=data.sheets[index];const auto path=data.parts.isEmpty()?QStringLiteral("xl/worksheets/sheet%1.xml").arg(index+1):sheet.part;
        if(!data.parts.isEmpty()&&changes[index].isEmpty())continue;
        QDomDocument document;QDomElement root;
        if(data.parts.isEmpty()){root=mainElement(document,"worksheet");document.appendChild(root);root.appendChild(mainElement(document,"sheetData"));}else {document=xml(part(data,path));root=document.documentElement();}
        const auto entries=data.parts.isEmpty()?currentCells(sheet,changes[index]):changes[index];for(auto it=entries.cbegin();it!=entries.cend();++it)setCellXml(document,root,cellRow(it.key()),cellColumn(it.key()),it.value());
        const auto all=currentCells(sheet,changes[index]);int row=0,col=0;for(auto it=all.cbegin();it!=all.cend();++it){row=std::max(row,cellRow(it.key()));col=std::max(col,cellColumn(it.key()));}auto dimension=child(root,"dimension");if(!dimension.isNull())dimension.setAttribute("ref",QStringLiteral("A1:")+cellAddress(row,col));replacePart(parts,path,document.toByteArray(-1));
    }
    WorkbookData changedBook=data;changedBook.parts=parts;auto book=xml(part(changedBook,workbook));auto calc=child(book.documentElement(),"calcPr");if(calc.isNull()){calc=mainElement(book,"calcPr");book.documentElement().appendChild(calc);}calc.setAttribute("fullCalcOnLoad","1");calc.setAttribute("forceFullCalc","1");replacePart(parts,workbook,book.toByteArray(-1));
    changedBook.parts=parts;auto rels=xml(part(changedBook,relations));QStringList removed;for(auto e=rels.documentElement().firstChildElement();!e.isNull();){const auto next=e.nextSiblingElement();if(e.attribute("Type").endsWith("/calcChain")){removed.push_back(relatedPath(workbook,e.attribute("Target")));rels.documentElement().removeChild(e);}e=next;}
    if(!removed.isEmpty()){replacePart(parts,relations,rels.toByteArray(-1));parts.erase(std::remove_if(parts.begin(),parts.end(),[&](const auto& item){return removed.contains(decode(item.name));}),parts.end());changedBook.parts=parts;auto types=xml(part(changedBook,"[Content_Types].xml"));for(auto e=types.documentElement().firstChildElement();!e.isNull();){const auto next=e.nextSiblingElement();if(removed.contains(e.attribute("PartName").mid(1)))types.documentElement().removeChild(e);e=next;}replacePart(parts,"[Content_Types].xml",types.toByteArray(-1));}
    return writePackage(parts);
}
} // namespace penta::workbook
