#include "runtime/data_io.hpp"
#include <xlsxio_write.h>
#include <minizip/zip.h>
#include <minizip/unzip.h>
#include <array>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

using namespace penta::runtime::dataio;
namespace {
int checks=0;
void check(bool condition,const char* expression,int line){++checks;if(!condition)throw std::runtime_error(std::string("line ")+std::to_string(line)+": "+expression);}
#define CHECK(x) check((x),#x,__LINE__)
template<class F>void rejects(F call){bool rejected=false;try{call();}catch(const Error&){rejected=true;}CHECK(rejected);}
std::string text(const std::filesystem::path& path){return readBytes(path);}
void zipEntry(zipFile zip,const char* name,const std::string& content){
    CHECK(zipOpenNewFileInZip(zip,name,nullptr,nullptr,0,nullptr,0,nullptr,Z_DEFLATED,9)==ZIP_OK);
    CHECK(zipWriteInFileInZip(zip,content.data(),static_cast<unsigned>(content.size()))==ZIP_OK);
    CHECK(zipCloseFileInZip(zip)==ZIP_OK);
}
std::string zipText(const std::filesystem::path& path,const char* part){
    const auto zip=unzOpen(path.string().c_str());CHECK(zip!=nullptr);
    CHECK(unzLocateFile(zip,part,1)==UNZ_OK);CHECK(unzOpenCurrentFile(zip)==UNZ_OK);
    std::string result;std::array<char,4096> buffer{};int count=0;
    while((count=unzReadCurrentFile(zip,buffer.data(),buffer.size()))>0){result.append(buffer.data(),static_cast<std::size_t>(count));CHECK(result.size()<65536);}
    CHECK(count==0);CHECK(unzCloseCurrentFile(zip)==UNZ_OK);CHECK(unzClose(zip)==UNZ_OK);return result;
}
}
int main(){
    const auto stamp=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root=std::filesystem::temp_directory_path()/std::filesystem::path("penta-data-io-"+stamp);
    try{
        std::filesystem::create_directory(root);
        const auto unicode=root/std::filesystem::path(u8"Δonnées_日本語");std::filesystem::create_directory(unicode);
        const NumericTable expected{2,3,{1.25,-2.0,1e-200,3.141592653589793,1e200,0}};
        for(const auto& suffix:{".csv",".xlsx"}){
            const auto path=unicode/(std::string("numbers")+suffix);
            writeMatrix(path,expected);
            const auto actual=readMatrix(path);
            CHECK(actual.rows==2);CHECK(actual.columns==3);CHECK(actual.values==expected.values);
            const auto previous=readBytes(path);
            NumericTable bad=expected;bad.values[2]=std::numeric_limits<double>::quiet_NaN();
            rejects([&]{writeMatrix(path,bad);});CHECK(readBytes(path)==previous);
            bad=expected;bad.columns=2;rejects([&]{writeMatrix(path,bad);});CHECK(readBytes(path)==previous);
            Options small;small.maxElements=5;rejects([&]{(void)readMatrix(path,{},small);});rejects([&]{writeMatrix(path,expected,{},small);});CHECK(readBytes(path)==previous);
            auto stop=std::make_shared<std::atomic_bool>(true);Options cancelled;cancelled.cancellation=stop;
            rejects([&]{writeMatrix(path,expected,{},cancelled);});rejects([&]{(void)readMatrix(path,{},cancelled);});CHECK(readBytes(path)==previous);
            Options bytes;bytes.maxFileBytes=2;rejects([&]{(void)readMatrix(path,{},bytes);});rejects([&]{writeMatrix(path,expected,{},bytes);});CHECK(readBytes(path)==previous);
            writeMatrix(path,NumericTable{});const auto empty=readMatrix(path);CHECK(empty.rows==0);CHECK(empty.columns==0);CHECK(empty.values.empty());
            rejects([&]{writeMatrix(path,NumericTable{0,3,{}});});
        }
        const auto stylePath=root/"valid-styles.xlsx";writeMatrix(stylePath,expected);
        const auto styles=zipText(stylePath,"xl/styles.xml");
        CHECK(styles.find("<fill/>")==std::string::npos);
        CHECK(styles.find("<fills count=\"2\">")!=std::string::npos);
        CHECK(styles.find("<patternFill patternType=\"none\"/>")!=std::string::npos);
        CHECK(styles.find("<patternFill patternType=\"gray125\"/>")!=std::string::npos);
        CHECK(styles.find("<cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/>")!=std::string::npos);
        const auto csv=root/"quoted.csv";
        writeBytesAtomic(csv,"\xEF\xBB\xBF\" 1.25 \", +2,-3e-2\r\n4,\"5\",6\r\n");
        const auto quote=readMatrix(csv);CHECK(quote.rows==2);CHECK(quote.columns==3);CHECK(quote.values==std::vector<double>({1.25,2,-.03,4,5,6}));
        for(const auto& invalid:{"header,2\n3,4\n","1,,3\n","1,2\n3\n","\"1,2","\"1\"junk,2","NaN,2","inf,2","1e999,2","1,2,","\n","1\"2,3","\"1\n2\",3","1,2\n\n","+-1,2","++1,2"}){
            writeBytesAtomic(csv,invalid);rejects([&]{(void)readMatrix(csv);});
        }
        writeBytesAtomic(csv,std::string("1,\0\n",4));rejects([&]{(void)readMatrix(csv);});
        writeBytesAtomic(csv,"1,2\n");rejects([&]{(void)readMatrix(csv,"Sheet1");});rejects([&]{writeMatrix(csv,expected,"Sheet1");});
        rejects([&]{(void)readMatrix(root/"old.xls");});rejects([&]{writeMatrix(root/"unknown.txt",expected);});rejects([&]{(void)readMatrix(root/"missing.csv");});
        rejects([&]{writeMatrix(root/"missing-dir"/"data.xlsx",expected);});

        const auto xlsx=unicode/std::filesystem::path(u8"résultats_λ.xlsx");
        writeMatrix(xlsx,expected,"Δonnées");const auto named=readMatrix(xlsx,"Δonnées");CHECK(named.values==expected.values);
        rejects([&]{(void)readMatrix(xlsx,"absent");});
        const auto original=readBytes(xlsx);
        for(const auto& invalid:{"bad/name","bad:name","'bad","bad'","abcdefghijklmnopqrstuvwxyz012345"})rejects([&]{writeMatrix(xlsx,expected,invalid);});
        CHECK(readBytes(xlsx)==original);
        const auto truncated=root/"truncated.xlsx";writeBytesAtomic(truncated,original.substr(0,original.size()/2));rejects([&]{(void)readMatrix(truncated);});
        Options expanded;expanded.maxUncompressedBytes=10;rejects([&]{(void)readMatrix(xlsx,{},expanded);});

        const auto textXlsx=root/"text.xlsx";
        auto writer=xlsxiowrite_open(textXlsx.string().c_str(),"Data");CHECK(writer!=nullptr);
        xlsxiowrite_add_cell_float(writer,1);xlsxiowrite_add_cell_string(writer,"header");xlsxiowrite_next_row(writer);CHECK(xlsxiowrite_close(writer)==0);
        rejects([&]{(void)readMatrix(textXlsx);});
        writer=xlsxiowrite_open(textXlsx.string().c_str(),"Data");CHECK(writer!=nullptr);
        xlsxiowrite_add_cell_float(writer,1);xlsxiowrite_add_cell_string(writer,nullptr);xlsxiowrite_next_row(writer);CHECK(xlsxiowrite_close(writer)==0);
        rejects([&]{(void)readMatrix(textXlsx);});
        const auto malformed=root/"malformed.xlsx";
        auto zip=zipOpen(malformed.string().c_str(),APPEND_STATUS_CREATE);CHECK(zip!=nullptr);
        zipEntry(zip,"[Content_Types].xml","<Types>");zipEntry(zip,"xl/workbook.xml","<workbook/>");CHECK(zipClose(zip,nullptr)==ZIP_OK);
        rejects([&]{(void)readMatrix(malformed);});
        zip=zipOpen(malformed.string().c_str(),APPEND_STATUS_CREATE);CHECK(zip!=nullptr);
        zipEntry(zip,"[Content_Types].xml","<!DOCTYPE a [<!ENTITY e '3'>]><Types>&e;</Types>");zipEntry(zip,"xl/workbook.xml","<workbook/>");CHECK(zipClose(zip,nullptr)==ZIP_OK);
        rejects([&]{(void)readMatrix(malformed);});
        zip=zipOpen(malformed.string().c_str(),APPEND_STATUS_CREATE);CHECK(zip!=nullptr);
        zipEntry(zip,"[Content_Types].xml","<Types/>");zipEntry(zip,"xl/workbook.xml","<workbook/>");zipEntry(zip,"xl/workbook.xml","<workbook/>");CHECK(zipClose(zip,nullptr)==ZIP_OK);
        rejects([&]{(void)readMatrix(malformed);});
        for(const bool missing:{true,false}){
            zip=zipOpen(malformed.string().c_str(),APPEND_STATUS_CREATE);CHECK(zip!=nullptr);
            zipEntry(zip,"[Content_Types].xml",R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/></Types>)");
            zipEntry(zip,"xl/workbook.xml",R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Data" sheetId="1" r:id="rId1"/></sheets></workbook>)");
            zipEntry(zip,"xl/_rels/workbook.xml.rels",R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)");
            if(!missing)zipEntry(zip,"xl/worksheets/sheet1.xml","<unrelated/>");
            CHECK(zipClose(zip,nullptr)==ZIP_OK);
            rejects([&]{(void)readMatrix(malformed,"Data");});
        }

        const auto native=unicode/"workspace.pws";writeBytesAtomic(native,"first\n");CHECK(text(native)=="first\n");
        Options small;small.maxFileBytes=2;rejects([&]{writeBytesAtomic(native,"replacement",small);});CHECK(text(native)=="first\n");
        writeBytesAtomic(native,"second\n");CHECK(text(native)=="second\n");
        std::size_t leftovers=0;for(const auto& entry:std::filesystem::recursive_directory_iterator(root))if(entry.path().filename().string().starts_with(".penta-data-"))++leftovers;CHECK(leftovers==0);
        CHECK(std::filesystem::equivalent(root.parent_path(),std::filesystem::temp_directory_path()));CHECK(root.filename().string().starts_with("penta-data-io-"));
        std::filesystem::remove_all(root);
        std::cout<<"Native CSV/XLSX data I/O: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\nFixtures retained at "<<root<<'\n';return 1;}
}
