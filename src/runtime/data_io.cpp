#include "data_io.hpp"

#include <xlsxio_read.h>
#include <xlsxio_write.h>
#include <minizip/unzip.h>
#include <expat.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <locale.h>
#include <memory>
#include <map>
#include <string_view>
#include <unordered_set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace penta::runtime::dataio {
namespace {
void cancelled(const Options& o) {
    if(o.cancellation&&o.cancellation->load())throw Error("Data I/O was cancelled.");
}
std::string utf8(const std::filesystem::path& path) {
    const auto text=path.u8string();return {text.begin(),text.end()};
}
std::string extension(const std::filesystem::path& path) {
    if(path.empty()||path.filename().empty()||path.native().find(std::filesystem::path::value_type{})!=std::filesystem::path::string_type::npos)
        throw Error("A nonempty file path without NUL characters is required.");
    std::string result=utf8(path.extension());
    for(auto& c:result)if(c>='A'&&c<='Z')c+=('a'-'A');
    if(result!=".csv"&&result!=".xlsx")throw Error("Use a .csv or .xlsx file; legacy .xls files are not supported.");
    return result;
}
std::string readFileBytes(const std::filesystem::path& path,const Options& o) {
    cancelled(o);std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if(!stream)throw Error("Cannot open the data file for reading.");
    const auto size=stream.tellg();
    if(size<0||static_cast<std::uint64_t>(size)>o.maxFileBytes)throw Error("The data file exceeds the configured byte limit.");
    std::string bytes(static_cast<std::size_t>(size),'\0');stream.seekg(0);
    if(!bytes.empty()&&!stream.read(bytes.data(),static_cast<std::streamsize>(bytes.size())))throw Error("The data file could not be read completely.");
    cancelled(o);return bytes;
}
double number(std::string_view text,std::size_t row,std::size_t col) {
    auto space=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
    while(!text.empty()&&space(text.front()))text.remove_prefix(1);
    while(!text.empty()&&space(text.back()))text.remove_suffix(1);
    bool repeatedSign=false;
    if(!text.empty()&&text.front()=='+'){
        text.remove_prefix(1);repeatedSign=!text.empty()&&(text.front()=='-'||text.front()=='+');
    }
    double value=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value,std::chars_format::general);
    if(text.empty()||repeatedSign||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!std::isfinite(value))
        throw Error("Expected a finite number at row "+std::to_string(row)+", column "+std::to_string(col)+"; headers, text and empty cells are not numeric data.");
    return value;
}
void validateTable(const NumericTable& table,const Options& o) {
    cancelled(o);
    if((table.rows==0)!=(table.columns==0))throw Error("An empty data table must have shape 0-by-0.");
    if(table.columns!=0&&(table.rows>o.maxElements/table.columns))throw Error("The data table exceeds the configured element limit.");
    if(table.rows*table.columns!=table.values.size())throw Error("The numeric table shape does not match its values.");
    for(std::size_t n=0;n<table.values.size();++n){if((n&1023)==0)cancelled(o);if(!std::isfinite(table.values[n]))throw Error("Only finite real values can be written as numeric data.");}
}
NumericTable readCsv(std::string_view bytes,const Options& o) {
    if(bytes.starts_with("\xEF\xBB\xBF"))bytes.remove_prefix(3);
    NumericTable table;std::string field;std::size_t columns=0;bool pending=false;
    enum class State{Start,Plain,Quoted,AfterQuote};State state=State::Start;
    auto cell=[&]{
        if(table.values.size()>=o.maxElements)throw Error("The CSV table exceeds the configured element limit.");
        table.values.push_back(number(field,table.rows+1,columns+1));++columns;field.clear();state=State::Start;pending=false;
    };
    auto row=[&]{
        cell();if(table.rows==0)table.columns=columns;
        else if(columns!=table.columns)throw Error("CSV rows must all have the same number of columns.");
        ++table.rows;columns=0;pending=false;
    };
    for(std::size_t n=0;n<bytes.size();++n){
        if((n&4095)==0)cancelled(o);
        const char c=bytes[n];
        if(c=='\0')throw Error("CSV data contains a NUL character.");
        if(state==State::Quoted){
            if(c=='"'){if(n+1<bytes.size()&&bytes[n+1]=='"'){field+='"';++n;}else state=State::AfterQuote;}
            else field+=c;
        }else if(c==','){cell();pending=true;}
        else if(c=='\r'||c=='\n'){row();if(c=='\r'&&n+1<bytes.size()&&bytes[n+1]=='\n')++n;}
        else if(state==State::AfterQuote){if(c!=' '&&c!='\t')throw Error("Unexpected characters after a quoted CSV field.");}
        else if(c=='"'){if(state!=State::Start)throw Error("A CSV quote must start a field.");state=State::Quoted;pending=true;}
        else{field+=c;state=State::Plain;pending=true;}
        if(field.size()>4096)throw Error("A numeric CSV field is too long.");
    }
    if(state==State::Quoted)throw Error("A quoted CSV field is not terminated.");
    if(pending||columns!=0||!field.empty()||state==State::AfterQuote)row();
    return table;
}

// Validate the ZIP and all XML before invoking XLSXIO. Its callback API does not
// propagate all XML errors; accepting a truncated worksheet could lose data.
struct MemoryZip {std::string_view bytes;std::uint64_t position=0;};
voidpf ZCALLBACK memoryOpen(voidpf opaque,const void*,int mode) {
    if(!(mode&ZLIB_FILEFUNC_MODE_READ))return nullptr;
    static_cast<MemoryZip*>(opaque)->position=0;return opaque;
}
uLong ZCALLBACK memoryRead(voidpf,voidpf stream,void* buffer,uLong size) {
    auto& m=*static_cast<MemoryZip*>(stream);const auto count=std::min<std::uint64_t>(size,m.bytes.size()-m.position);
    if(count)std::memcpy(buffer,m.bytes.data()+m.position,static_cast<std::size_t>(count));
    m.position+=count;return static_cast<uLong>(count);
}
uLong ZCALLBACK memoryWrite(voidpf,voidpf,const void*,uLong){return 0;}
ZPOS64_T ZCALLBACK memoryTell(voidpf,voidpf stream){return static_cast<MemoryZip*>(stream)->position;}
long ZCALLBACK memorySeek(voidpf,voidpf stream,ZPOS64_T offset,int origin) {
    auto& m=*static_cast<MemoryZip*>(stream);std::uint64_t base=0;
    if(origin==ZLIB_FILEFUNC_SEEK_CUR)base=m.position;
    else if(origin==ZLIB_FILEFUNC_SEEK_END)base=m.bytes.size();
    else if(origin!=ZLIB_FILEFUNC_SEEK_SET)return -1;
    // Minizip seeks backwards from EOF by passing a two's-complement offset.
    const auto delta=static_cast<std::int64_t>(offset);
    if(delta<0){const auto magnitude=std::uint64_t(-(delta+1))+1;if(magnitude>base)return -1;m.position=base-magnitude;}
    else{if(static_cast<std::uint64_t>(delta)>m.bytes.size()-base)return -1;m.position=base+static_cast<std::uint64_t>(delta);}
    return 0;
}
int ZCALLBACK memoryClose(voidpf,voidpf){return 0;}
int ZCALLBACK memoryError(voidpf,voidpf){return 0;}
struct XmlValidation {
    XML_Parser parser=nullptr;
    std::string filename,root,error;
    std::vector<std::string>* targets=nullptr;
    std::vector<std::string>* worksheets=nullptr;
};
std::string relationshipPart(const std::string& source,std::string_view target) {
    std::string decoded;decoded.reserve(target.size());
    auto hex=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;};
    for(std::size_t n=0;n<target.size();++n){
        unsigned char c=target[n];
        if(c=='%'){
            if(n+2>=target.size()||hex(target[n+1])<0||hex(target[n+2])<0)throw Error("An XLSX relationship has an invalid part URI.");
            c=static_cast<unsigned char>(hex(target[n+1])*16+hex(target[n+2]));n+=2;
        }
        if(c<32||c=='\\'||c==':'||c=='?'||c=='#')throw Error("An XLSX internal relationship has an unsupported part URI.");
        decoded+=static_cast<char>(c);
    }
    std::string base;
    if(!source.starts_with("_rels/")){
        const auto directory=source.rfind("/_rels/");
        if(directory==std::string::npos)throw Error("An XLSX relationship part has an invalid location.");
        base=source.substr(0,directory+1);
    }
    std::string path=decoded.starts_with('/')?decoded.substr(1):base+decoded;
    std::vector<std::string> parts;std::size_t begin=0;
    while(begin<=path.size()){
        const auto end=path.find('/',begin);const auto segment=path.substr(begin,end==std::string::npos?std::string::npos:end-begin);
        if(segment==".."){if(parts.empty())throw Error("An XLSX relationship escapes the workbook package.");parts.pop_back();}
        else if(!segment.empty()&&segment!=".")parts.push_back(segment);
        if(end==std::string::npos)break;
        begin=end+1;
    }
    std::string result;for(const auto& part:parts){if(!result.empty())result+='/';result+=part;}
    if(result.empty())throw Error("An XLSX relationship has no target part.");
    return result;
}
void XMLCALL rejectDoctype(void* opaque,const XML_Char*,const XML_Char*,const XML_Char*,int){
    auto& state=*static_cast<XmlValidation*>(opaque);state.error="XLSX XML document types are unsupported.";XML_StopParser(state.parser,XML_FALSE);
}
void XMLCALL xmlElement(void* opaque,const XML_Char* element,const XML_Char** attributes) noexcept {
    auto& state=*static_cast<XmlValidation*>(opaque);
    try{
        if(state.root.empty())state.root=element;
        if(!state.filename.ends_with(".rels"))return;
        const std::string_view name(element);if(name!="Relationship"&&!name.ends_with(":Relationship"))return;
        std::string target,mode,type;
        for(std::size_t n=0;attributes[n];n+=2){const std::string_view attribute(attributes[n]);
            if(attribute=="Target")target=attributes[n+1];else if(attribute=="TargetMode")mode=attributes[n+1];else if(attribute=="Type")type=attributes[n+1];}
        if(mode=="External")return;
        if(!mode.empty()&&mode!="Internal")throw Error("An XLSX relationship has an invalid target mode.");
        if(state.targets->size()>=16384)throw Error("The XLSX workbook contains excessive internal relationships.");
        const auto part=relationshipPart(state.filename,target);state.targets->push_back(part);
        if(type.ends_with("/worksheet"))state.worksheets->push_back(part);
    }catch(const std::exception& error){state.error=error.what();XML_StopParser(state.parser,XML_FALSE);}
}
void validateArchive(std::string_view bytes,const Options& o) {
    MemoryZip memory{bytes};zlib_filefunc64_def functions{};
    functions.zopen64_file=memoryOpen;functions.zread_file=memoryRead;functions.zwrite_file=memoryWrite;
    functions.ztell64_file=memoryTell;functions.zseek64_file=memorySeek;functions.zclose_file=memoryClose;functions.zerror_file=memoryError;functions.opaque=&memory;
    const auto zip=unzOpen2_64(nullptr,&functions);if(!zip)throw Error("The XLSX file is not a valid ZIP workbook.");
    struct Close{unzFile zip;~Close(){unzClose(zip);}} close{zip};
    unz_global_info64 info{};if(unzGetGlobalInfo64(zip,&info)!=UNZ_OK||info.number_entry>4096)throw Error("The XLSX archive has invalid or excessive entries.");
    std::uint64_t total=0;std::unordered_set<std::string> names;std::map<std::string,std::string> roots;
    std::vector<std::string> targets,worksheets;bool contentTypes=false,workbook=false;
    int status=unzGoToFirstFile(zip);
    for(std::uint64_t index=0;index<info.number_entry;++index){
        cancelled(o);if(status!=UNZ_OK)throw Error("The XLSX archive directory is truncated.");
        unz_file_info64 entry{};std::array<char,4096> name{};
        if(unzGetCurrentFileInfo64(zip,&entry,name.data(),static_cast<uLong>(name.size()),nullptr,0,nullptr,0)!=UNZ_OK||entry.size_filename>=name.size())throw Error("Invalid XLSX archive entry.");
        if(!names.emplace(name.data()).second)throw Error("Duplicate XLSX archive entries are not supported.");
        if(entry.flag&1)throw Error("Encrypted XLSX files are not supported.");
        if(entry.uncompressed_size>o.maxUncompressedBytes-total)throw Error("The expanded XLSX workbook exceeds the configured byte limit.");
        total+=entry.uncompressed_size;
        const std::string_view filename(name.data());contentTypes|=filename=="[Content_Types].xml";workbook|=filename=="xl/workbook.xml";
        if(filename.ends_with(".xml")||filename.ends_with(".rels")){
            if(unzOpenCurrentFile(zip)!=UNZ_OK)throw Error("Cannot open an XML part of the XLSX workbook.");
            XML_Parser parser=XML_ParserCreate(nullptr);if(!parser){unzCloseCurrentFile(zip);throw Error("Cannot allocate the XLSX XML parser.");}
            XmlValidation state;state.parser=parser;state.filename=filename;state.targets=&targets;state.worksheets=&worksheets;
            XML_SetUserData(parser,&state);XML_SetStartDoctypeDeclHandler(parser,rejectDoctype);XML_SetStartElementHandler(parser,xmlElement);
            std::array<char,8192> buffer{};std::uint64_t expanded=0;bool okay=true;int got=0;
            while((got=unzReadCurrentFile(zip,buffer.data(),static_cast<unsigned>(buffer.size())))>0){
                if(o.cancellation&&o.cancellation->load()){okay=false;break;}
                expanded+=static_cast<unsigned>(got);
                if(expanded>entry.uncompressed_size||XML_Parse(parser,buffer.data(),got,XML_FALSE)==XML_STATUS_ERROR){okay=false;break;}
            }
            if(got<0||expanded!=entry.uncompressed_size||XML_Parse(parser,nullptr,0,XML_TRUE)==XML_STATUS_ERROR)okay=false;
            roots[state.filename]=state.root;XML_ParserFree(parser);if(unzCloseCurrentFile(zip)!=UNZ_OK)okay=false;
            cancelled(o);if(!okay)throw Error("The XLSX workbook contains invalid, truncated or unsupported XML.");
        }
        status=unzGoToNextFile(zip);
    }
    if(!contentTypes||!workbook)throw Error("The XLSX file is missing its workbook structure.");
    for(const auto& target:targets)if(!names.contains(target))throw Error("The XLSX workbook references a missing internal part.");
    if(worksheets.empty())throw Error("The XLSX workbook has no numeric worksheet relationship.");
    for(const auto& target:worksheets)if(roots[target]!="worksheet")throw Error("The XLSX worksheet part is missing or uses an unsupported XML root.");
}
struct SheetData {
    NumericTable table;const Options* options=nullptr;std::size_t column=0;std::string error;
};
int cellCallback(std::size_t row,std::size_t col,const char* value,void* opaque) noexcept {
    auto& data=*static_cast<SheetData*>(opaque);
    try{
        cancelled(*data.options);
        if(row!=data.table.rows+1||col!=data.column+1)throw Error("The XLSX worksheet is not a dense rectangular numeric table.");
        if(data.table.values.size()>=data.options->maxElements)throw Error("The XLSX table exceeds the configured element limit.");
        data.table.values.push_back(number(value?std::string_view(value):std::string_view{},row,col));++data.column;
        return 0;
    }catch(const std::exception& e){data.error=e.what();return 1;}
}
int rowCallback(std::size_t row,std::size_t maxcol,void* opaque) noexcept {
    auto& data=*static_cast<SheetData*>(opaque);
    try{
        cancelled(*data.options);
        if(row!=data.table.rows+1||maxcol!=data.column||maxcol==0)throw Error("The XLSX worksheet contains an empty or incomplete numeric row.");
        if(data.table.rows==0)data.table.columns=maxcol;
        else if(maxcol!=data.table.columns)throw Error("XLSX numeric rows must all have the same number of columns.");
        ++data.table.rows;data.column=0;return 0;
    }catch(const std::exception& e){data.error=e.what();return 1;}
}
NumericTable readXlsx(std::string& bytes,const std::string& sheet,const Options& o) {
    validateArchive(bytes,o);
    const auto reader=xlsxioread_open_memory(bytes.data(),bytes.size(),0);
    if(!reader)throw Error("Cannot open the XLSX workbook.");
    struct Close{xlsxioreader reader;~Close(){xlsxioread_close(reader);}} close{reader};
    const auto sheets=xlsxioread_sheetlist_open(reader);if(!sheets)throw Error("The XLSX workbook has no readable worksheet.");
    std::string selected;
    while(const char* name=xlsxioread_sheetlist_next(sheets))if(selected.empty()&&(sheet.empty()||sheet==name))selected=name;
    xlsxioread_sheetlist_close(sheets);
    if(selected.empty())throw Error(sheet.empty()?"The XLSX workbook has no readable worksheet.":"The requested XLSX worksheet does not exist.");
    SheetData data;data.options=&o;
    const int status=xlsxioread_process(reader,selected.c_str(),XLSXIOREAD_SKIP_NONE,cellCallback,rowCallback,&data);
    cancelled(o);if(!data.error.empty())throw Error(data.error);
    if(status!=0||data.column!=0)throw Error("The XLSX worksheet could not be read completely.");
    validateTable(data.table,o);return data.table;
}
class TemporaryFile {
public:
    explicit TemporaryFile(const std::filesystem::path& target):target_(target){
        auto parent=target.parent_path();if(parent.empty())parent=".";
        static std::atomic_uint64_t sequence{0};
        for(int attempt=0;attempt<64;++attempt){
            const auto stamp=static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            path_=parent/(".penta-data-"+std::to_string(stamp)+"-"+std::to_string(sequence.fetch_add(1))+".tmp");
#ifdef _WIN32
            const auto file=CreateFileW(path_.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);return;}
            if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)break;
#else
            const int file=::open(path_.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);
            if(file>=0){::close(file);return;}
            if(errno!=EEXIST)break;
#endif
        }
        throw Error("Cannot create a temporary output file beside the requested destination.");
    }
    ~TemporaryFile(){if(!committed_){std::error_code error;std::filesystem::remove(path_,error);}}
    const std::filesystem::path& path()const{return path_;}
    void commit(){
#ifdef _WIN32
        if(!MoveFileExW(path_.c_str(),target_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw Error("Cannot replace the data destination; the previous file was preserved.");
#else
        std::error_code error;std::filesystem::rename(path_,target_,error);
        if(error)throw Error("Cannot replace the data destination; the previous file was preserved.");
#endif
        committed_=true;
    }
private:std::filesystem::path path_,target_;bool committed_=false;
};
struct NumericLocale {
#ifdef _WIN32
    int previousConfig=0;std::string previous;
    NumericLocale(){previousConfig=_configthreadlocale(0);_configthreadlocale(_ENABLE_PER_THREAD_LOCALE);if(const auto* value=setlocale(LC_NUMERIC,nullptr))previous=value;if(!setlocale(LC_NUMERIC,"C"))throw Error("Cannot select the numeric XLSX output locale.");}
    ~NumericLocale(){if(!previous.empty())setlocale(LC_NUMERIC,previous.c_str());_configthreadlocale(previousConfig);}
#else
    locale_t cLocale=nullptr,previous=nullptr;
    NumericLocale(){cLocale=newlocale(LC_NUMERIC_MASK,"C",nullptr);if(!cLocale)throw Error("Cannot select the numeric XLSX output locale.");previous=uselocale(cLocale);}
    ~NumericLocale(){uselocale(previous);freelocale(cLocale);}
#endif
};
void validateSheet(const std::string& sheet) {
    if(sheet.empty()||sheet.size()>31||sheet.find_first_of("[]:*?/\\")!=std::string::npos||sheet.front()=='\''||sheet.back()=='\'')throw Error("An XLSX sheet name must contain 1..31 UTF-8 bytes and no []:*?/\\ characters or outer apostrophes.");
    for(unsigned char c:sheet)if(c<32)throw Error("An XLSX sheet name cannot contain control characters.");
}
}

std::string readBytes(const std::filesystem::path& path,const Options& o) {return readFileBytes(path,o);}
void writeBytesAtomic(const std::filesystem::path& path,std::string_view bytes,const Options& o) {
    cancelled(o);
    if(path.empty()||path.filename().empty()||path.native().find(std::filesystem::path::value_type{})!=std::filesystem::path::string_type::npos)
        throw Error("A nonempty file path without NUL characters is required.");
    if(bytes.size()>o.maxFileBytes)throw Error("The output exceeds the configured byte limit.");
    TemporaryFile temporary(path);std::ofstream stream(temporary.path(),std::ios::binary|std::ios::trunc);
    if(!stream)throw Error("Cannot open the temporary data destination.");
    for(std::size_t n=0;n<bytes.size();){cancelled(o);const auto amount=std::min<std::size_t>(8192,bytes.size()-n);stream.write(bytes.data()+n,static_cast<std::streamsize>(amount));n+=amount;}
    stream.flush();if(!stream)throw Error("The output could not be written completely.");stream.close();if(!stream)throw Error("The output could not be closed successfully.");
    cancelled(o);temporary.commit();
}
NumericTable readMatrix(const std::filesystem::path& path,const std::string& sheet,const Options& o) {
    const auto format=extension(path);if(format==".csv"&&!sheet.empty())throw Error("The Sheet option is valid only for XLSX files.");
    auto bytes=readFileBytes(path,o);return format==".csv"?readCsv(bytes,o):readXlsx(bytes,sheet,o);
}
NumericTable readMatrixBytes(std::string_view bytes,const std::filesystem::path& formatPath,const std::string& sheet,const Options& o) {
    cancelled(o);const auto format=extension(formatPath);
    if(bytes.size()>o.maxFileBytes)throw Error("The data snapshot exceeds the configured byte limit.");
    if(format==".csv"){if(!sheet.empty())throw Error("The Sheet option is valid only for XLSX files.");return readCsv(bytes,o);}
    std::string captured(bytes);return readXlsx(captured,sheet,o);
}
void writeMatrix(const std::filesystem::path& path,const NumericTable& table,const std::string& sheet,const Options& o) {
    const auto format=extension(path);validateTable(table,o);
    if(format==".csv"&&!sheet.empty())throw Error("The Sheet option is valid only for XLSX files.");
    const std::string selected=sheet.empty()?"Sheet1":sheet;if(format==".xlsx")validateSheet(selected);
    if(format==".xlsx"&&(table.rows>1048576||table.columns>16384))throw Error("The table exceeds the XLSX worksheet dimension limits.");
    TemporaryFile temporary(path);
    if(format==".csv"){
        std::ofstream stream(temporary.path(),std::ios::binary|std::ios::trunc);if(!stream)throw Error("Cannot open the temporary CSV destination.");
        std::array<char,64> buffer{};
        for(std::size_t row=0;row<table.rows;++row){cancelled(o);
            for(std::size_t col=0;col<table.columns;++col){if(col)stream.put(',');const double value=table.values[row*table.columns+col];
                const auto converted=std::to_chars(buffer.data(),buffer.data()+buffer.size(),value,std::chars_format::general,std::numeric_limits<double>::max_digits10);
                if(converted.ec!=std::errc{})throw Error("Cannot format a numeric CSV value.");
                stream.write(buffer.data(),converted.ptr-buffer.data());}
            stream.put('\n');
        }
        stream.flush();if(!stream)throw Error("The CSV output could not be written completely.");stream.close();if(!stream)throw Error("The CSV output could not be closed successfully.");
    }else{
        NumericLocale locale;
        const auto writer=xlsxiowrite_open(utf8(temporary.path()).c_str(),selected.c_str());
        if(!writer)throw Error("Cannot open the temporary XLSX destination.");
        struct Close{xlsxiowriter writer;~Close(){if(writer)xlsxiowrite_close(writer);}} close{writer};
        xlsxiowrite_set_detection_rows(writer,0);
        for(std::size_t row=0;row<table.rows;++row){cancelled(o);
            for(std::size_t col=0;col<table.columns;++col)xlsxiowrite_add_cell_float(writer,table.values[row*table.columns+col]);
            xlsxiowrite_next_row(writer);
        }
        const int status=xlsxiowrite_close(writer);close.writer=nullptr;if(status!=0)throw Error("The XLSX output could not be finalized.");
    }
    // Verify the produced file, including XML/ZIP integrity and exact numbers,
    // before the atomic rename. This also catches XLSXIO's void write failures.
    auto bytes=readFileBytes(temporary.path(),o);const auto check=format==".csv"?readCsv(bytes,o):readXlsx(bytes,selected,o);
    if(check.rows!=table.rows||check.columns!=table.columns||check.values!=table.values)throw Error("The generated data file did not pass round-trip verification; the previous destination was preserved.");
    cancelled(o);temporary.commit();
}
}
