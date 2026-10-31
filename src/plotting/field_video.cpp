#include "field_video.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace penta::plotting {
namespace {
using Bytes=std::vector<unsigned char>;
void u16(Bytes& out,std::uint16_t x){out.push_back(static_cast<unsigned char>(x));out.push_back(static_cast<unsigned char>(x>>8));}
void u32(Bytes& out,std::uint32_t x){for(int shift:{0,8,16,24})out.push_back(static_cast<unsigned char>(x>>shift));}
void tag(Bytes& out,const char* name){for(int k=0;k<4;++k)out.push_back(static_cast<unsigned char>(name[k]));}
Bytes chunk(const char* name,const Bytes& content){Bytes out;out.reserve(content.size()+9);tag(out,name);u32(out,static_cast<std::uint32_t>(content.size()));out.insert(out.end(),content.begin(),content.end());if(content.size()%2)out.push_back(0);return out;}
Bytes list(const char* name,const Bytes& content){Bytes body;tag(body,name);body.insert(body.end(),content.begin(),content.end());return chunk("LIST",body);}
void append(Bytes& out,const Bytes& content){out.insert(out.end(),content.begin(),content.end());}
class AtomicVideo {
    std::filesystem::path target_,temporary_;const VideoExportOptions& options_;bool committed_=false;std::uint64_t written_=0;
#ifdef _WIN32
    HANDLE handle_=INVALID_HANDLE_VALUE;
#else
    int handle_=-1;
#endif
public:
    AtomicVideo(const std::filesystem::path& target,const VideoExportOptions& options):target_(target),options_(options){
        if(!target.is_absolute()||target.filename().empty()||!std::filesystem::is_directory(target.parent_path()))throw ExportError("AVI destination requires an absolute file in an existing directory.");
        if(!options.image.overwrite&&std::filesystem::exists(target))throw ExportError("AVI destination exists; set overwrite=1 explicitly.");
        static std::atomic_uint64_t serial{0};temporary_=target;temporary_+=std::filesystem::path(".penta-video-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial.fetch_add(1))+".tmp");
#ifdef _WIN32
        handle_=CreateFileW(temporary_.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(handle_==INVALID_HANDLE_VALUE)throw ExportError("Cannot create temporary native AVI.");
#else
        handle_=open(temporary_.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);if(handle_<0)throw ExportError("Cannot create temporary native AVI.");
#endif
    }
    ~AtomicVideo(){
#ifdef _WIN32
        if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);
#else
        if(handle_>=0)close(handle_);
#endif
        if(!committed_){std::error_code ignored;std::filesystem::remove(temporary_,ignored);}
    }
    void write(const unsigned char* bytes,std::size_t count){
        if(count>options_.maximumBytes-written_)throw ExportError("AVI output exceeded its preflight disk budget.");
        for(std::size_t offset=0;offset<count;){if(options_.image.cancellation&&options_.image.cancellation->load())throw ExportError("Native AVI export cancelled; its previous destination is intact.");const auto amount=std::min<std::size_t>(1048576,count-offset);
#ifdef _WIN32
            DWORD written=0;if(!WriteFile(handle_,bytes+offset,static_cast<DWORD>(amount),&written,nullptr)||written!=amount)throw ExportError("Cannot stream a complete native AVI frame.");
#else
            const auto written=::write(handle_,bytes+offset,amount);if(written<=0)throw ExportError("Cannot stream a complete native AVI frame.");
#endif
            offset+=static_cast<std::size_t>(written);written_+=static_cast<std::size_t>(written);
        }
    }
    void write(const Bytes& bytes){write(bytes.data(),bytes.size());}
    void commit(std::uint64_t expected){
        if(written_!=expected)throw ExportError("AVI stream length does not match its RIFF/index headers.");
        if(options_.image.cancellation&&options_.image.cancellation->load())throw ExportError("Native AVI export cancelled before commit.");
#ifdef _WIN32
        const bool flushed=FlushFileBuffers(handle_)!=0;CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;if(!flushed)throw ExportError("Cannot flush native AVI.");if(!MoveFileExW(temporary_.c_str(),target_.c_str(),MOVEFILE_WRITE_THROUGH|(options_.image.overwrite?MOVEFILE_REPLACE_EXISTING:0)))throw ExportError("Cannot atomically commit native AVI; previous output is intact.");
#else
        const bool flushed=fsync(handle_)==0;const bool closed=close(handle_)==0;handle_=-1;if(!flushed||!closed)throw ExportError("Cannot flush native AVI.");if(options_.image.overwrite)std::filesystem::rename(temporary_,target_);else{std::filesystem::create_hard_link(temporary_,target_);std::filesystem::remove(temporary_);}
#endif
        committed_=true;
    }
};
}
VideoExportReport exportFieldVideo(const FieldAnimationData& input,const std::filesystem::path& path,const VideoExportOptions& options){
    auto data=input;validateFieldAnimation(data);const double fps=options.fps>0?options.fps:data.fps;
    if(!std::isfinite(fps)||fps<1||fps>60||!std::isfinite(options.maximumSeconds)||options.maximumSeconds<=0||options.maximumSeconds>3600||options.maximumFrames<1||options.maximumFrames>240||data.selectedFrames.size()>options.maximumFrames||options.maximumBytes<1024||options.maximumBytes>=1024ull*1024*1024||options.image.dpi<36||options.image.dpi>1200||options.image.maximumPixels>64000000||options.image.width<160||options.image.height<120||options.image.width>16000||options.image.height>16000||static_cast<std::size_t>(options.image.width)*options.image.height>options.image.maximumPixels)throw ExportError("AVI frame count, FPS, dimensions, wall or <1 GiB disk budget is invalid.");
    auto extension=path.extension().string();for(char& c:extension)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');if(extension!=".avi")throw ExportError("Native video export supports uncompressed .avi (BI_RGB24), not an external codec/container.");
    const auto width=static_cast<std::uint32_t>(options.image.width),height=static_cast<std::uint32_t>(options.image.height),rowStride=(width*3u+3u)&~3u;
    const auto frameBytes64=static_cast<std::uint64_t>(rowStride)*height;const auto frameCount=static_cast<std::uint32_t>(data.selectedFrames.size());
    if(frameBytes64>0xffffffffu||std::ceil(frameBytes64*fps)>0xffffffffu)throw ExportError("AVI frame/byte-rate fields exceed classic RIFF limits.");
    const auto frameBytes=static_cast<std::uint32_t>(frameBytes64),frameChunk=frameBytes+8u+(frameBytes&1u);
    Bytes avih;u32(avih,static_cast<std::uint32_t>(std::lround(1000000/fps)));u32(avih,static_cast<std::uint32_t>(std::ceil(frameBytes64*fps)));u32(avih,0);u32(avih,0x10);u32(avih,frameCount);u32(avih,0);u32(avih,1);u32(avih,frameBytes);u32(avih,width);u32(avih,height);for(int k=0;k<4;++k)u32(avih,0);
    Bytes strh;tag(strh,"vids");tag(strh,"DIB ");u32(strh,0);u16(strh,0);u16(strh,0);u32(strh,0);u32(strh,1000000);u32(strh,static_cast<std::uint32_t>(std::lround(fps*1000000)));u32(strh,0);u32(strh,frameCount);u32(strh,frameBytes);u32(strh,0xffffffffu);u32(strh,0);u16(strh,0);u16(strh,0);u16(strh,static_cast<std::uint16_t>(width));u16(strh,static_cast<std::uint16_t>(height));
    Bytes bitmap;u32(bitmap,40);u32(bitmap,width);u32(bitmap,height);u16(bitmap,1);u16(bitmap,24);u32(bitmap,0);u32(bitmap,frameBytes);u32(bitmap,static_cast<std::uint32_t>(std::lround(options.image.dpi/.0254)));u32(bitmap,static_cast<std::uint32_t>(std::lround(options.image.dpi/.0254)));u32(bitmap,0);u32(bitmap,0);
    if(avih.size()!=56||strh.size()!=56||bitmap.size()!=40)throw ExportError("Internal AVI header layout mismatch.");
    Bytes stream;append(stream,chunk("strh",strh));append(stream,chunk("strf",bitmap));Bytes headers;append(headers,chunk("avih",avih));append(headers,list("strl",stream));const auto hdrl=list("hdrl",headers);
    std::ostringstream metadata;metadata<<"Pentarnion Studio native BI_RGB24; display_fps="<<std::setprecision(17)<<fps<<"; coefficient="<<data.coefficient<<"; fixed display FPS differs from original physical sample times; "<<data.algebraId<<"; gamma="<<data.gamma<<"; "<<data.context<<"; original_samples=";for(auto index:data.selectedFrames)metadata<<(index+1)<<':'<<std::setprecision(17)<<data.t[index]<<';';const auto note=metadata.str();if(note.size()>65536)throw ExportError("AVI provenance metadata exceeds 64 KiB.");Bytes comment;comment.reserve(note.size()+1);for(char c:note)comment.push_back(static_cast<unsigned char>(c));comment.push_back(0);const auto info=list("INFO",chunk("ICMT",comment));
    const std::uint64_t moviSize=4+static_cast<std::uint64_t>(frameCount)*frameChunk,indexSize=static_cast<std::uint64_t>(frameCount)*16,total=12+hdrl.size()+info.size()+8+moviSize+8+indexSize;
    if(total>options.maximumBytes||total>=1024ull*1024*1024)throw ExportError("Uncompressed AVI sequence exceeds max_bytes before creating output.");
    const auto started=std::chrono::steady_clock::now();auto check=[&]{if(options.image.cancellation&&options.image.cancellation->load())throw ExportError("Native AVI export cancelled.");if(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>options.maximumSeconds)throw ExportError("Native AVI wall-time budget reached; previous output is intact.");};check();
    AtomicVideo output(path,options);Bytes prefix;tag(prefix,"RIFF");u32(prefix,static_cast<std::uint32_t>(total-8));tag(prefix,"AVI ");append(prefix,hdrl);append(prefix,info);tag(prefix,"LIST");u32(prefix,static_cast<std::uint32_t>(moviSize));tag(prefix,"movi");output.write(prefix);Bytes scanline(rowStride,0);
    for(std::size_t frame=0;frame<data.selectedFrames.size();++frame){check();const auto rgb=rasterRgb({fieldAnimationFrame(data,frame)},options.image);check();Bytes frameHeader;tag(frameHeader,"00db");u32(frameHeader,frameBytes);output.write(frameHeader);
        for(std::uint32_t row=0;row<height;++row){check();const auto source=static_cast<std::size_t>(height-1-row)*width*3;for(std::uint32_t x=0;x<width;++x){scanline[x*3]=rgb[source+x*3+2];scanline[x*3+1]=rgb[source+x*3+1];scanline[x*3+2]=rgb[source+x*3];}output.write(scanline);}if(frameBytes&1u){const unsigned char zero=0;output.write(&zero,1);}
    }
    Bytes index;index.reserve(static_cast<std::size_t>(indexSize));for(std::uint32_t frame=0;frame<frameCount;++frame){tag(index,"00db");u32(index,0x10);u32(index,4+frame*frameChunk);u32(index,frameBytes);}output.write(chunk("idx1",index));check();output.commit(total);
    return {frameCount,total,fps,frameCount/fps,data.t[data.selectedFrames.back()]-data.t[data.selectedFrames.front()],path};
}
}
