#include "plotting/field_video.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>

namespace {
int checks=0,failures=0;
void expect(bool value,const char* message){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
using Bytes=std::vector<unsigned char>;
Bytes read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(file),{});}
std::uint32_t u32(const Bytes& b,std::size_t at){return b.at(at)|(std::uint32_t(b.at(at+1))<<8)|(std::uint32_t(b.at(at+2))<<16)|(std::uint32_t(b.at(at+3))<<24);}
std::uint16_t u16(const Bytes& b,std::size_t at){return static_cast<std::uint16_t>(b.at(at)|(std::uint16_t(b.at(at+1))<<8));}
std::string tag(const Bytes& b,std::size_t at){return std::string(reinterpret_cast<const char*>(b.data()+at),4);}
struct Avi {
    std::uint32_t width=0,height=0,totalFrames=0,microseconds=0,scale=0,rate=0,imageBytes=0;
    std::size_t movi=0,index=0;std::vector<std::size_t> frames;std::string metadata;
};
Avi decode(const Bytes& bytes){
    expect(bytes.size()>=12&&tag(bytes,0)=="RIFF"&&tag(bytes,8)=="AVI ","independent parser recognizes RIFF AVI container");expect(u32(bytes,4)+8==bytes.size(),"RIFF file length is exact");Avi avi;
    std::function<void(std::size_t,std::size_t)> chunks=[&](std::size_t begin,std::size_t end){for(auto at=begin;at+8<=end;){const std::size_t size=u32(bytes,at+4),data=at+8;if(size>end-data)throw std::runtime_error("RIFF chunk exceeds parent.");const auto kind=tag(bytes,at);
        if(kind=="LIST"){const auto type=tag(bytes,data);if(type=="movi"){avi.movi=data;for(auto frame=data+4;frame+8<=data+size;){expect(tag(bytes,frame)=="00db","video uses uncompressed frame chunk IDs");avi.frames.push_back(frame);frame+=8+u32(bytes,frame+4)+(u32(bytes,frame+4)&1u);}}else chunks(data+4,data+size);}
        if(kind=="avih"){expect(size==56,"AVI main header has standard size");avi.microseconds=u32(bytes,data);avi.totalFrames=u32(bytes,data+16);avi.width=u32(bytes,data+32);avi.height=u32(bytes,data+36);}
        if(kind=="strh"){expect(size==56&&tag(bytes,data)=="vids"&&tag(bytes,data+4)=="DIB ","stream header selects native DIB video");avi.scale=u32(bytes,data+20);avi.rate=u32(bytes,data+24);}
        if(kind=="strf"){expect(size==40&&u32(bytes,data)==40&&u16(bytes,data+12)==1&&u16(bytes,data+14)==24&&u32(bytes,data+16)==0,"BITMAPINFOHEADER is uncompressed RGB24");avi.imageBytes=u32(bytes,data+20);}
        if(kind=="idx1"){avi.index=data;expect(size==avi.totalFrames*16,"AVI index has one entry per saved frame");}
        if(kind=="ICMT")avi.metadata.assign(reinterpret_cast<const char*>(bytes.data()+data),size);
        at=data+size+(size&1u);
    }};chunks(12,bytes.size());expect(avi.frames.size()==avi.totalFrames,"declared and streamed frame counts agree");return avi;
}
}
int main(){using namespace penta;using namespace penta::plotting;const auto folder=std::filesystem::temp_directory_path()/("penta-video-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(folder);
    try{
        FieldAnimationData data;data.nx=3;data.x={0,.5,1};data.t={0,.1,.9};data.values={1,0,1,.7,0,.7,.4,0,.4};data.selectedFrames={0,2};data.context="A_gamma_v1 gamma=1";validateFieldAnimation(data);const auto original=data.values;
        VideoExportOptions settings;settings.image.width=321;settings.image.height=240;settings.image.dpi=100;settings.fps=29.97;settings.maximumBytes=8000000;const auto file=folder/"selected.avi";const auto report=exportFieldVideo(data,file,settings);const auto bytes=read(file);const auto avi=decode(bytes);
        expect(avi.width==321&&avi.height==240&&avi.totalFrames==2,"requested dimensions and selected source frame count preserved");expect(std::abs(static_cast<double>(avi.rate)/avi.scale-29.97)<1e-10&&avi.microseconds==33367,"fractional display FPS is recorded as a rational stream rate");expect(report.frames==2&&report.bytes==bytes.size()&&std::abs(report.sourceDuration-.9)<1e-12&&std::abs(report.duration-2/29.97)<1e-12,"video duration and original physical duration remain distinct");expect(avi.metadata.find("original_samples=1:0;3:0.90000000000000002;")!=std::string::npos,"container provenance records original selected indices and exact physical sample times");
        const auto stride=(avi.width*3+3)&~3u;expect(avi.imageBytes==stride*avi.height&&stride==964,"RGB24 row padding is explicitly represented");
        for(std::size_t frame=0;frame<avi.frames.size();++frame){const auto entry=avi.index+frame*16;expect(tag(bytes,entry)=="00db"&&u32(bytes,entry+4)==0x10&&avi.movi+u32(bytes,entry+8)==avi.frames[frame]&&u32(bytes,entry+12)==avi.imageBytes,"independent index offsets point at each complete keyframe");const auto expected=rasterRgb({fieldAnimationFrame(data,frame)},settings.image);bool pixels=true,padding=true;
            for(std::size_t row=0;row<avi.height;++row){const auto source=avi.frames[frame]+8+row*stride,target=(avi.height-1-row)*avi.width*3;for(std::size_t x=0;x<avi.width;++x)for(std::size_t c=0;c<3;++c)pixels=pixels&&bytes[source+3*x+(2-c)]==expected[target+3*x+c];for(auto x=avi.width*3;x<stride;++x)padding=padding&&bytes[source+x]==0;}
            expect(pixels,"independent BI_RGB decoder reproduces exact RGB pixels in the selected frame order");expect(padding,"bottom-up BGR rows include deterministic zero alignment bytes");
        }
        expect(data.values==original,"AVI export leaves every original scientific sample unchanged");auto reject=[&](auto action,const char* message){bool caught=false;try{action();}catch(const std::exception&){caught=true;}expect(caught,message);};
        reject([&]{(void)exportFieldVideo(data,file,settings);},"AVI overwrite is explicit");expect(read(file)==bytes,"rejected AVI overwrite preserves previous bytes");auto small=settings;small.maximumBytes=1024;reject([&]{(void)exportFieldVideo(data,folder/"disk.avi",small);},"large uncompressed stream rejected by disk preflight");expect(!std::filesystem::exists(folder/"disk.avi"),"disk preflight failure creates no AVI");auto stopped=settings;stopped.image.cancellation=std::make_shared<std::atomic_bool>(true);reject([&]{(void)exportFieldVideo(data,folder/"cancelled.avi",stopped);},"video rendering obeys cancellation");expect(!std::filesystem::exists(folder/"cancelled.avi"),"cancellation leaves no partial AVI destination");auto count=settings;count.maximumFrames=1;reject([&]{(void)exportFieldVideo(data,folder/"frames.avi",count);},"video frame cap is enforced before output");reject([&]{(void)exportFieldVideo(data,folder/"wrong.mp4",settings);},"unsupported codecs/containers are not silently substituted");
#ifndef PENTA_VIDEO_DRIVER_ONLY
        runtime::ExecutionOptions options;options.dataDirectory=folder;const auto result=runtime::execute("record f=options(dimension=1,x=[0,0.5,1],t=[0;0.1;0.9],values=[1,0,1;0.7,0,0.7;0.4,0,0.4]);record movie=exportvideo(f,\"language.avi\",options(first=1,last=3,stride=2,fps=29.97,width=321,height=240,max_bytes=8000000));assert(movie.frames==2);assert(movie.codec==\"BI_RGB24\");assert(!movie.physical_time_interpolated);",{},options);if(!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}expect(result.ok()&&std::filesystem::is_regular_file(folder/"language.avi"),"language video command streams through the same native headless exporter");
#endif
    }catch(const std::exception& error){++failures;std::cerr<<"EXCEPTION: "<<error.what()<<'\n';}
    std::error_code ignored;std::filesystem::remove_all(folder,ignored);std::cout<<checks<<" native AVI checks, "<<failures<<" failures\n";return failures?1:0;
}
