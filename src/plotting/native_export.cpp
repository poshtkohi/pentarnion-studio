#include "native_export.hpp"
#include "plot_template.hpp"
#include "vector_export.hpp"
#include "field_animation.hpp"
#include "graph_view.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <limits>
#include <numeric>
#include <set>
#include <nlohmann/json.hpp>
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
using Point=std::array<double,3>;
using Color=std::array<double,3>;
constexpr double pi=3.14159265358979323846;
struct Bounds {int left,top,right,bottom;};
struct Image {
    const PngOptions& options;std::vector<unsigned char> rgb;std::vector<float> depth;std::uint64_t operations=0;std::vector<VectorCommand>* commands=nullptr;std::string fontFamily="sans";
    explicit Image(const PngOptions& o,std::vector<VectorCommand>* capture=nullptr):options(o),rgb(capture?0:static_cast<std::size_t>(o.width)*o.height*3,255),depth(capture?0:static_cast<std::size_t>(o.width)*o.height,-std::numeric_limits<float>::infinity()),commands(capture){}
    void circle(Point point,int radius,Color color,Bounds clip){if(commands){VectorCommand item;item.data.kind=3;item.data.x[0]=point[0];item.data.y[0]=point[1];item.data.width=radius;std::copy(color.begin(),color.end(),item.data.color);item.data.clip[0]=clip.left;item.data.clip[1]=clip.top;item.data.clip[2]=clip.right;item.data.clip[3]=clip.bottom;commands->push_back(std::move(item));return;}for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x)if(x*x+y*y<=radius*radius)pixel(static_cast<int>(std::lround(point[0]))+x,static_cast<int>(std::lround(point[1]))+y,color,point[2]+.001,true); }
    void check(std::uint64_t n=1){if(options.cancellation&&options.cancellation->load(std::memory_order_relaxed))throw ExportError("Native PNG export cancelled.");if(n>options.maximumRasterOperations-std::min(operations,options.maximumRasterOperations))throw ExportError("Native PNG raster work budget exceeded.");operations+=n;}
    void pixel(int x,int y,Color c,double z=0,bool testDepth=false){if(commands)return;if(x<0||y<0||x>=options.width||y>=options.height)return;const auto at=static_cast<std::size_t>(y)*options.width+x;if(testDepth&&z<depth[at])return;if(testDepth)depth[at]=static_cast<float>(z);for(std::size_t k=0;k<3;++k)rgb[3*at+k]=static_cast<unsigned char>(std::lround(std::clamp(c[k],0.,1.)*255));}
    void line(Point a,Point b,const Color& c,double width,const std::string& style,Bounds clip,bool testDepth=false){
        double first=0,last=1;const double dx=b[0]-a[0],dy=b[1]-a[1];
        auto bound=[&](double p,double q){if(p==0)return q>=0;const double ratio=q/p;if(p<0){if(ratio>last)return false;first=std::max(first,ratio);}else{if(ratio<first)return false;last=std::min(last,ratio);}return true;};
        if(!bound(-dx,a[0]-clip.left)||!bound(dx,clip.right-a[0])||!bound(-dy,a[1]-clip.top)||!bound(dy,clip.bottom-a[1]))return;
        Point begin{},end{};for(std::size_t k=0;k<3;++k){begin[k]=a[k]+first*(b[k]-a[k]);end[k]=a[k]+last*(b[k]-a[k]);}
        if(commands){check(1);VectorCommand item;item.data.kind=0;item.data.width=width;item.data.style=style=="dash"?1u:style=="dot"?2u:style=="dashdot"?3u:0u;item.data.x[0]=begin[0];item.data.y[0]=begin[1];item.data.x[1]=end[0];item.data.y[1]=end[1];std::copy(c.begin(),c.end(),item.data.color);item.data.clip[0]=clip.left;item.data.clip[1]=clip.top;item.data.clip[2]=clip.right;item.data.clip[3]=clip.bottom;commands->push_back(std::move(item));return;}
        const int steps=std::max(1,static_cast<int>(std::ceil(std::max(std::abs(end[0]-begin[0]),std::abs(end[1]-begin[1])))));
        const int radius=std::clamp(static_cast<int>(std::floor(width*.5)),0,20);check(static_cast<std::uint64_t>(steps+1)*(2*radius+1)*(2*radius+1));
        for(int i=0;i<=steps;++i){if((i&255)==0&&options.cancellation&&options.cancellation->load())throw ExportError("Native PNG export cancelled.");bool draw=true;
            if(style=="dash")draw=i%16<10;else if(style=="dot")draw=i%7<2;else if(style=="dashdot")draw=i%24<12||(i%24>=17&&i%24<19);if(!draw)continue;
            const double t=static_cast<double>(i)/steps;const int x=static_cast<int>(std::lround(begin[0]+t*(end[0]-begin[0]))),y=static_cast<int>(std::lround(begin[1]+t*(end[1]-begin[1])));const double z=begin[2]+t*(end[2]-begin[2]);
            for(int oy=-radius;oy<=radius;++oy)for(int ox=-radius;ox<=radius;++ox)if(x+ox>=clip.left&&x+ox<=clip.right&&y+oy>=clip.top&&y+oy<=clip.bottom)pixel(x+ox,y+oy,c,z,testDepth);
        }
    }
};
std::array<unsigned char,7> glyph(char input){
    const char c=input>='a'&&input<='z'?static_cast<char>(input-'a'+'A'):input;
    static const std::map<char,std::array<unsigned char,7>> shapes{
        {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},{'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},
        {'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},{'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},
        {'I',{14,4,4,4,4,4,14}},{'J',{7,2,2,2,18,18,12}},{'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
        {'M',{17,27,21,21,17,17,17}},{'N',{17,25,21,19,17,17,17}},{'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},
        {'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},{'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},
        {'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},{'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
        {'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},{'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},
        {'2',{14,17,1,2,4,8,31}},{'3',{30,1,1,14,1,1,30}},{'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},
        {'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},{'8',{14,17,17,14,17,17,14}},{'9',{14,17,17,15,1,1,14}},
        {' ',{0,0,0,0,0,0,0}},{'.',{0,0,0,0,0,12,12}},{',',{0,0,0,0,4,4,8}},{'-',{0,0,0,31,0,0,0}},{'+',{0,4,4,31,4,4,0}},
        {'=',{0,0,31,0,31,0,0}},{'/',{1,2,2,4,8,8,16}},{':',{0,12,12,0,12,12,0}},{';',{0,12,12,0,4,4,8}},{'_', {0,0,0,0,0,0,31}},
        {'(',{2,4,8,8,8,4,2}},{')',{8,4,2,2,2,4,8}},{'[',{14,8,8,8,8,8,14}},{']',{14,2,2,2,2,2,14}},
        {'<',{2,4,8,16,8,4,2}},{'>',{8,4,2,1,2,4,8}},{'*',{0,21,14,31,14,21,0}},{'|',{4,4,4,4,4,4,4}},
        {'?',{14,17,1,2,4,0,4}},{'!',{4,4,4,4,4,0,4}},{'\\',{16,8,8,4,2,2,1}},{'$',{4,15,20,14,5,30,4}},
        {'^',{4,10,17,0,0,0,0}},{'%',{25,26,2,4,8,11,19}},{'"',{10,10,10,0,0,0,0}},{'\'',{4,4,8,0,0,0,0}}
    };
    const auto found=shapes.find(c);return found==shapes.end()?shapes.at('?'):found->second;
}
void text(Image& image,int x,int y,const std::string& value,int scale,int maximumWidth,Color color={.12,.12,.12},bool latex=false,bool centered=false){
    if(maximumWidth<=0)return;
    if(image.commands){image.check(1);VectorCommand item;item.data.kind=2;item.data.x[0]=x;item.data.y[0]=y;item.data.width=maximumWidth;item.data.height=9*scale+8;item.data.x[1]=centered?1:0;item.data.font_family=image.fontFamily=="serif"?1u:image.fontFamily=="mono"?2u:0u;item.data.latex=latex;std::copy(color.begin(),color.end(),item.data.color);item.data.clip[0]=0;item.data.clip[1]=0;item.data.clip[2]=image.options.width;item.data.clip[3]=image.options.height;item.text=value;image.commands->push_back(std::move(item));return;}
    const auto count=std::min<std::size_t>(value.size(),static_cast<std::size_t>(maximumWidth/(6*scale)));
    image.check(count*35*scale*scale);
    for(std::size_t index=0;index<count;++index){const auto bitmap=glyph(value[index]);for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(bitmap[static_cast<std::size_t>(row)]&(1<<(4-col)))for(int dy=0;dy<scale;++dy)for(int dx=0;dx<scale;++dx)image.pixel(x+static_cast<int>(index)*6*scale+col*scale+dx,y+row*scale+dy,color);}
}
std::vector<std::string> textLines(const std::string& value,int width,int scale,std::size_t maximumLines){
    const auto columns=static_cast<std::size_t>(std::max(1,width/(6*scale)));std::vector<std::string> result;std::size_t offset=0;
    while(offset<value.size()&&result.size()<maximumLines){auto count=std::min(columns,value.size()-offset);if(count<value.size()-offset){const auto space=value.rfind(' ',offset+count);if(space!=std::string::npos&&space>offset)count=space-offset;}auto line=value.substr(offset,count);offset+=count;while(offset<value.size()&&value[offset]==' ')++offset;if(result.size()+1==maximumLines&&offset<value.size()){if(line.size()>3)line.resize(std::min(line.size(),columns)-3);line+="...";}result.push_back(std::move(line));}
    return result;
}
Color fieldColor(double value,double low,double high){const double t=high>low?static_cast<double>(std::clamp((static_cast<long double>(value)-low)/(static_cast<long double>(high)-low),0.L,1.L)):0;return {std::clamp(1.5-std::abs(4*t-3),0.,1.),std::clamp(1.5-std::abs(4*t-2),0.,1.),std::clamp(1.5-std::abs(4*t-1),0.,1.)};}
void triangle(Image& image,Point a,Point b,Point c,Color color,Bounds clip){
    if(image.commands){image.check(1);VectorCommand item;item.data.kind=1;item.data.x[0]=a[0];item.data.y[0]=a[1];item.data.x[1]=b[0];item.data.y[1]=b[1];item.data.x[2]=c[0];item.data.y[2]=c[1];std::copy(color.begin(),color.end(),item.data.color);item.data.clip[0]=clip.left;item.data.clip[1]=clip.top;item.data.clip[2]=clip.right;item.data.clip[3]=clip.bottom;image.commands->push_back(std::move(item));return;}

    const double area=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);if(!std::isfinite(area)||std::abs(area)<1e-12)return;
    const int left=static_cast<int>(std::clamp(std::floor(std::min({a[0],b[0],c[0]})),static_cast<double>(clip.left),static_cast<double>(clip.right)));
    const int right=static_cast<int>(std::clamp(std::ceil(std::max({a[0],b[0],c[0]})),static_cast<double>(clip.left),static_cast<double>(clip.right)));
    const int top=static_cast<int>(std::clamp(std::floor(std::min({a[1],b[1],c[1]})),static_cast<double>(clip.top),static_cast<double>(clip.bottom)));
    const int bottom=static_cast<int>(std::clamp(std::ceil(std::max({a[1],b[1],c[1]})),static_cast<double>(clip.top),static_cast<double>(clip.bottom)));
    for(int y=top;y<=bottom;++y){image.check(static_cast<std::uint64_t>(right-left+1));for(int x=left;x<=right;++x){const double p=x+.5,q=y+.5;const double u=((b[0]-p)*(c[1]-q)-(b[1]-q)*(c[0]-p))/area,v=((c[0]-p)*(a[1]-q)-(c[1]-q)*(a[0]-p))/area,w=1-u-v;if(u>=0&&v>=0&&w>=0)image.pixel(x,y,color,u*a[2]+v*b[2]+w*c[2],true);}}
}
std::string number(double x){char out[64];std::snprintf(out,sizeof(out),"%.5g",x);return out;}
std::vector<std::pair<double,std::string>> axisTicks(long double low,long double high){
    std::vector<std::pair<double,std::string>> ticks;const long double raw=(high-low)/4;if(!(raw>0)||!std::isfinite(raw))return ticks;
    const long double magnitude=std::pow(10.L,std::floor(std::log10(raw))),fraction=raw/magnitude;
    const long double step=magnitude*(fraction<=1?1:fraction<=2?2:fraction<=2.5?2.5:fraction<=5?5:10),first=std::ceil(low/step),last=std::floor(high/step);
    for(int index=0;index<8&&first+index<=last;++index){const auto candidate=(first+index)*step;if(std::abs(candidate)>std::numeric_limits<double>::max())continue;double value=static_cast<double>(candidate);if(value==0)value=0;if(ticks.empty()||ticks.back().first!=value)ticks.emplace_back(value,number(value));}
    if(ticks.empty()){ticks.emplace_back(static_cast<double>(low),number(static_cast<double>(low)));ticks.emplace_back(static_cast<double>(high),number(static_cast<double>(high)));}
    return ticks;
}
void panel(Image& image,const NativePanel& p,Bounds box){
    image.fontFamily=p.style.fontFamily;runtime::validatePlotStyle(p.style);if(p.dimensions!=2&&p.dimensions!=3)throw ExportError("Native PNG plots require 2D or 3D coordinates.");
    if(!std::isfinite(p.cameraAzimuth)||!std::isfinite(p.cameraElevation))throw ExportError("Native PNG camera angles must be finite.");
    const int font=std::clamp(static_cast<int>(std::lround(p.style.fontSize/8.0)),1,4);
    std::array<long double,3> low{{INFINITY,INFINITY,INFINITY}},high{{-INFINITY,-INFINITY,-INFINITY}};
    std::size_t points=0;auto include=[&](const Point& x){if(++points>image.options.maximumPoints)throw ExportError("Native PNG point budget exceeded.");for(int d=0;d<p.dimensions;++d){if(!std::isfinite(x[d]))throw ExportError("Native PNG coordinates must be finite.");low[d]=std::min(low[d],static_cast<long double>(x[d]));high[d]=std::max(high[d],static_cast<long double>(x[d]));}};
    for(const auto& curve:p.curves)for(const auto& point:curve.points)include(point);
    if(p.mesh){
        const auto& mesh=*p.mesh;
        if(mesh.triangles.size()>2000000||mesh.segments.size()>500000)throw ExportError("Native PNG mesh connectivity exceeds its budget.");
        if(!mesh.scalars.empty()&&mesh.scalars.size()!=mesh.points.size())throw ExportError("Mesh scalar/point counts differ.");
        for(const auto& face:mesh.triangles)for(const auto index:face)if(index>=mesh.points.size())throw ExportError("Native PNG triangle index is invalid.");
        for(const auto& edge:mesh.segments)for(const auto index:edge)if(index>=mesh.points.size())throw ExportError("Native PNG segment index is invalid.");
        for(const auto& point:mesh.points)include(point);
        for(double x:mesh.scalars)if(!std::isfinite(x))throw ExportError("Native PNG scalar values must be finite.");
    }
    if(p.basisLabels.size()>64)throw ExportError("Native algebra projection basis exceeds64 coordinates.");
    for(const auto& label:p.basisLabels)if(label.empty()||label.size()>256)throw ExportError("Native algebra basis labels require1..256 bytes.");
    auto endpoint=[&](const runtime::PlotVector& v){Point result{};const auto coefficients=v.allCoefficients();if(coefficients.empty()||coefficients.size()>64||(!p.basisLabels.empty()&&coefficients.size()!=p.basisLabels.size()))throw ExportError("Native algebra vectors must retain every declared basis coefficient.");for(int d=0;d<p.dimensions;++d){if(p.axes[d]<0||static_cast<std::size_t>(p.axes[d])>=coefficients.size())throw ExportError("Native vector projection axis is outside the retained algebra basis.");for(int previous=0;previous<d;++previous)if(p.axes[d]==p.axes[previous])throw ExportError("Native algebra projection axes must be distinct.");result[d]=v.coefficient(static_cast<std::size_t>(p.axes[d]));}return result;};
    if(!p.vectors.empty())include({0,0,0});
    for(const auto& v:p.vectors){for(double x:v.allCoefficients())if(!std::isfinite(x))throw ExportError("All original algebra vector coefficients must be finite.");include(endpoint(v));}
    std::vector<GraphSegment> graphLines;
    if(p.graph){validateGraphView(*p.graph);graphLines=graphSegments(*p.graph);for(const auto& point:p.graph->positions)include(point);for(const auto& edge:graphLines){include(edge.first);include(edge.last);}}
    if(points==0)return;
    std::array<long double,3> center{},units{{1,1,1}};long double common=0;
    for(int d=0;d<p.dimensions;++d){if(p.style.limits[static_cast<std::size_t>(d)]){low[d]=p.style.limits[static_cast<std::size_t>(d)]->lower;high[d]=p.style.limits[static_cast<std::size_t>(d)]->upper;}center[d]=low[d]*.5L+high[d]*.5L;units[d]=high[d]*.5L-low[d]*.5L;if(units[d]==0)units[d]=std::max(1.L,std::abs(center[d])*.1L);common=std::max(common,units[d]);}
    const bool equal=p.style.equalUnits||p.axes[0]>=0;
    if(equal)for(int d=0;d<p.dimensions;++d)units[d]=common;
    const std::array<std::vector<std::pair<double,std::string>>,2> tickText{axisTicks(low[0],high[0]),axisTicks(low[1],high[1])};
    std::size_t widestY=1,widestX=1;for(const auto& [value,label]:tickText[1]){(void)value;widestY=std::max(widestY,label.size());}for(const auto& [value,label]:tickText[0]){(void)value;widestX=std::max(widestX,label.size());}
    int yTickFont=font;while(yTickFont>1&&static_cast<int>(widestY)*6*yTickFont>(box.right-box.left)*.4)--yTickFont;
    const int margin=std::max(35,static_cast<int>(widestY)*6*yTickFont+12);
    const int contextFont=image.commands?font:std::max(1,font-1),textWidth=box.right-box.left-16;const auto whole=[&](const std::string& value,int size,std::size_t lines){return image.commands?(value.empty()?std::vector<std::string>{}:std::vector<std::string>{value}):textLines(value,textWidth,size,lines);};const auto contextLines=whole(p.context,contextFont,3),captionLines=whole(p.caption,contextFont,2),titleLines=whole(p.title,font+1,2);
    const int footer=static_cast<int>(contextLines.size()+captionLines.size())*(9*contextFont+(image.commands?8:0))+6;
    const Bounds area{box.left+margin,box.top+static_cast<int>(std::max<std::size_t>(1,titleLines.size()))*(9*(font+1)+(image.commands?8:0))+18*font,box.right-18*font,box.bottom-(image.commands?40:24)*font-footer};if(area.right-area.left<30||area.bottom-area.top<30)throw ExportError("Native PNG tile is too small for complete numerical labels.");
    int xTickFont=font;while(xTickFont>1&&static_cast<int>(widestX)*6*xTickFont>(area.right-area.left)/4)--xTickFont;
    const double az=p.cameraAzimuth*pi/180,el=p.cameraElevation*pi/180;
    const double width=area.right-area.left,height=area.bottom-area.top,uniform=std::min(width,height);
    auto project=[&](const Point& x){double a[3]{};for(int d=0;d<p.dimensions;++d)a[d]=static_cast<double>(std::clamp((static_cast<long double>(x[d])-center[d])/units[d],-1e10L,1e10L));double u=a[0],v=a[1],z=0;if(p.dimensions==3){const double side=-std::sin(az)*a[0]+std::cos(az)*a[1];u=std::cos(az)*a[0]+std::sin(az)*a[1];v=-std::sin(el)*side+std::cos(el)*a[2];z=std::cos(el)*side+std::sin(el)*a[2];}const double sx=p.dimensions==3?uniform*.28:(equal?uniform:width)*.45,sy=p.dimensions==3?uniform*.28:(equal?uniform:height)*.45;return Point{area.left+width*.5+u*sx,area.top+height*.5-v*sy,z};};
    auto line=[&](const Point& a,const Point& b,Color color,double weight=1,const std::string& style="solid",bool depth=false){image.line(project(a),project(b),color,weight,style,area,depth);};
    if(p.mesh){
        const auto& m=*p.mesh;const bool field=!m.scalars.empty();
        const auto uniformColor=runtime::plotColor(p.style,p.vectors.size()+p.curves.size());
        double colorLow=0,colorHigh=1;
        if(field){const auto [lo,hi]=std::minmax_element(m.scalars.begin(),m.scalars.end());colorLow=p.style.colorLimits?p.style.colorLimits->lower:*lo;colorHigh=p.style.colorLimits?p.style.colorLimits->upper:*hi;}
        if(!m.points.empty()){
            std::vector<std::array<std::uint32_t,3>> ordered;std::span<const std::array<std::uint32_t,3>> faces(m.triangles);
            if(image.commands&&p.dimensions==3){ordered=m.triangles;std::stable_sort(ordered.begin(),ordered.end(),[&](const auto& a,const auto& b){return project(m.points[a[0]])[2]+project(m.points[a[1]])[2]+project(m.points[a[2]])[2]<project(m.points[b[0]])[2]+project(m.points[b[1]])[2]+project(m.points[b[2]])[2];});faces=ordered;}
            for(const auto& face:faces){
                const auto color=field?fieldColor(m.scalars[face[0]]/3+m.scalars[face[1]]/3+m.scalars[face[2]]/3,colorLow,colorHigh):uniformColor;
                triangle(image,project(m.points[face[0]]),project(m.points[face[1]]),project(m.points[face[2]]),color,area);
            }
            for(const auto& edge:m.segments){
                const auto color=field?fieldColor(m.scalars[edge[0]]*.5+m.scalars[edge[1]]*.5,colorLow,colorHigh):uniformColor;
                line(m.points[edge[0]],m.points[edge[1]],color,p.style.lineWidth,"solid",true);
            }
        }
    }
    for(std::size_t index=0;index<p.curves.size();++index){const auto& curve=p.curves[index];const auto color=runtime::plotColor(p.style,index+p.vectors.size());for(std::size_t k=1;k<curve.points.size();++k)line(curve.points[k-1],curve.points[k],color,p.style.lineWidth,p.style.lineStyle,true);}
    for(std::size_t index=0;index<p.vectors.size();++index){const auto color=runtime::plotColor(p.style,index);const auto end=endpoint(p.vectors[index]);line({0,0,0},end,color,p.style.lineWidth,"solid",true);const auto first=project({0,0,0}),last=project(end);const double dx=last[0]-first[0],dy=last[1]-first[1],length=std::hypot(dx,dy);if(length>1){const double head=std::clamp(length*.12,6.,18.);const Point a{last[0]-head*dx/length+head*.4*dy/length,last[1]-head*dy/length-head*.4*dx/length,last[2]+.001},b{last[0]-head*dx/length-head*.4*dy/length,last[1]-head*dy/length+head*.4*dx/length,last[2]+.001};triangle(image,last,a,b,color,area);}}
    if(p.graph){const auto& graph=*p.graph;const Color selected{.96,.56,.10},highlighted{.88,.12,.20},normal{.38,.40,.43};const std::set<std::uint32_t> selectedNodes(graph.selectedNodes.begin(),graph.selectedNodes.end()),highlightedNodes(graph.highlightedNodes.begin(),graph.highlightedNodes.end());
        for(int pass=0;pass<3;++pass)for(const auto& segment:graphLines){const int kind=segment.selected?2:segment.highlighted?1:0;if(kind==pass)line(segment.first,segment.last,kind==2?selected:kind==1?highlighted:normal,graph.edgeWidth+(kind?1.5:0),"solid",true);}
        for(std::size_t node=0;node<graph.positions.size();++node){image.check();const auto id=static_cast<std::uint32_t>(node+1);const auto at=project(graph.positions[node]);const bool active=selectedNodes.contains(id),path=highlightedNodes.contains(id);const auto color=active?selected:path?highlighted:runtime::plotColor(p.style,0);const int radius=std::max(2,static_cast<int>(std::lround(graph.nodeSize*.5)));image.check(static_cast<std::uint64_t>((2*radius+1)*(2*radius+1)));image.circle(at,radius,color,area);if(graph.nodeLabels){const auto label=graphNodeLabel(graph,id);const int x=std::clamp(static_cast<int>(at[0])+radius+3,area.left,std::max(area.left,area.right-static_cast<int>(label.size())*6*font));text(image,x,static_cast<int>(at[1])-radius-7*font,label,font,area.right-x);}}
        if(graph.edgeLabels){const auto anchors=graphEdgeAnchors(graph,graphLines);for(std::size_t edge=0;edge<graph.edges.size();++edge){const auto at=project(anchors[edge]);text(image,static_cast<int>(at[0])+3,static_cast<int>(at[1])+3,std::to_string(edge+1)+":"+number(graph.weights[edge]),std::max(1,font-1),100);}}
    }
    auto axisLabel=[&](int axis){const auto& configured=axis==0?p.xLabel:axis==1?p.yLabel:p.zLabel;if(!configured.empty())return configured;const auto coefficient=p.axes[static_cast<std::size_t>(axis)];if(coefficient>=0){if(!p.basisLabels.empty()){if(static_cast<std::size_t>(coefficient)>=p.basisLabels.size())throw ExportError("Native projection label axis is outside its declared basis.");return p.basisLabels[static_cast<std::size_t>(coefficient)];}static const std::array<std::string,5> legacy{{"1","i","j","k","ell"}};if(coefficient<5)return legacy[static_cast<std::size_t>(coefficient)];return std::string("e")+std::to_string(coefficient);}return std::string(1,"xyz"[axis]);};
    const Color gray{.45,.45,.45};
    if(p.dimensions==2){image.line({double(area.left),double(area.bottom),0},{double(area.right),double(area.bottom),0},gray,1,"solid",area);image.line({double(area.left),double(area.top),0},{double(area.left),double(area.bottom),0},gray,1,"solid",area);
        for(int axis=0;axis<2;++axis)for(const auto& [value,label]:tickText[static_cast<std::size_t>(axis)]){Point at{static_cast<double>(center[0]),static_cast<double>(center[1]),0};at[axis]=value;const auto pixel=project(at);const int tickFont=axis==0?xTickFont:yTickFont,labelWidth=static_cast<int>(label.size())*6*tickFont;
            if(axis==0){const int x=std::clamp(static_cast<int>(pixel[0])-labelWidth/2,box.left+2,std::max(box.left+2,box.right-labelWidth-2));text(image,x,area.bottom+5*font,label,tickFont,labelWidth);}
            else text(image,area.left-labelWidth-8,static_cast<int>(pixel[1])-(image.commands?5*tickFont+4:3*tickFont),label,tickFont,labelWidth);
        }}
    else for(int axis=0;axis<3;++axis){Point a{},b{};for(int d=0;d<3;++d)a[d]=b[d]=static_cast<double>(center[d]);a[axis]=static_cast<double>(low[axis]);b[axis]=static_cast<double>(high[axis]);line(a,b,gray);const auto at=project(b);text(image,static_cast<int>(at[0]),static_cast<int>(at[1]),axisLabel(axis),font,100*font,{.12,.12,.12},p.latex[static_cast<std::size_t>(axis+1)]);}
    for(std::size_t k=0;k<titleLines.size();++k)text(image,box.left+8,box.top+5+static_cast<int>(k)*9*(font+1),titleLines[k],font+1,textWidth,{.12,.12,.12},p.latex[0],image.commands!=nullptr);
    if(p.dimensions==2){text(image,image.commands?area.left:area.left+(area.right-area.left)/3,area.bottom+(image.commands?23:17)*font,axisLabel(0),font,area.right-area.left,{.12,.12,.12},p.latex[1],image.commands!=nullptr);text(image,box.left+4,area.top-(image.commands?16:12)*font,axisLabel(1),font,area.right-area.left,{.12,.12,.12},p.latex[2]);}
    int footerY=box.bottom-footer+3;for(const auto& line:captionLines){text(image,box.left+8,footerY,line,contextFont,textWidth,{.12,.12,.12},p.latex[4]);footerY+=9*contextFont+(image.commands?8:0);}for(const auto& line:contextLines){text(image,box.left+8,footerY,line,contextFont,textWidth);footerY+=9*contextFont+(image.commands?8:0);}
    if(p.style.legendVisible){const auto count=p.curves.size()+p.vectors.size();const bool west=p.style.legendLocation=="northwest"||p.style.legendLocation=="southwest",south=p.style.legendLocation=="southeast"||p.style.legendLocation=="southwest"||p.style.legendLocation=="outside";const int left=west?area.left+8:std::max(area.left+8,area.right-180*font),top=south?std::max(area.top+8,area.bottom-static_cast<int>(std::min<std::size_t>(count,12))*10*font):area.top+8;
        for(std::size_t k=0;k<std::min<std::size_t>(count,12);++k){const int y=top+static_cast<int>(k)*10*font;const auto label=k<p.vectors.size()?runtime::plotLegendLabel(p.style,k,p.vectors[k].label):runtime::plotLegendLabel(p.style,k,p.curves[k-p.vectors.size()].label);const auto color=runtime::plotColor(p.style,k);image.line({double(left),double(y+3*font),0},{double(left+15*font),double(y+3*font),0},color,p.style.lineWidth,p.style.lineStyle,area);text(image,left+20*font,y,label,font,std::max(1,area.right-left-20*font));}
    }
}
void bigEndian(std::vector<unsigned char>& out,std::uint32_t value){for(int shift:{24,16,8,0})out.push_back(static_cast<unsigned char>(value>>shift));}
std::uint32_t crc(const unsigned char* data,std::size_t size){std::uint32_t value=0xffffffffu;for(std::size_t i=0;i<size;++i){value^=data[i];for(int bit=0;bit<8;++bit)value=(value>>1)^((value&1)?0xedb88320u:0u);}return value^0xffffffffu;}
void chunk(std::vector<unsigned char>& png,const char* type,const std::vector<unsigned char>& data){bigEndian(png,static_cast<std::uint32_t>(data.size()));const auto offset=png.size();png.insert(png.end(),type,type+4);png.insert(png.end(),data.begin(),data.end());bigEndian(png,crc(png.data()+offset,data.size()+4));}
std::vector<unsigned char> encode(Image& image,const std::string& metadata){
    std::vector<unsigned char> raw;raw.reserve(image.rgb.size()+image.options.height);for(int y=0;y<image.options.height;++y){image.check();raw.push_back(0);const auto first=image.rgb.begin()+static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y)*image.options.width*3);raw.insert(raw.end(),first,first+image.options.width*3);}
    std::vector<unsigned char> deflate{0x78,0x01};deflate.reserve(raw.size()+raw.size()/65535*5+16);std::uint32_t a=1,b=0;
    for(std::size_t offset=0;offset<raw.size();){image.check();const auto count=std::min<std::size_t>(65535,raw.size()-offset);deflate.push_back(offset+count==raw.size()?1:0);const auto n=static_cast<std::uint16_t>(count),inverse=static_cast<std::uint16_t>(~n);deflate.push_back(static_cast<unsigned char>(n));deflate.push_back(static_cast<unsigned char>(n>>8));deflate.push_back(static_cast<unsigned char>(inverse));deflate.push_back(static_cast<unsigned char>(inverse>>8));for(std::size_t k=0;k<count;++k){const auto value=raw[offset+k];deflate.push_back(value);a=(a+value)%65521;b=(b+a)%65521;}offset+=count;}
    bigEndian(deflate,(b<<16)|a);std::vector<unsigned char> png{137,80,78,71,13,10,26,10},header;bigEndian(header,static_cast<std::uint32_t>(image.options.width));bigEndian(header,static_cast<std::uint32_t>(image.options.height));header.insert(header.end(),{8,2,0,0,0});chunk(png,"IHDR",header);
    std::vector<unsigned char> physical;const auto ppm=static_cast<std::uint32_t>(std::lround(image.options.dpi/.0254));bigEndian(physical,ppm);bigEndian(physical,ppm);physical.push_back(1);chunk(png,"pHYs",physical);
    std::vector<unsigned char> description{'D','e','s','c','r','i','p','t','i','o','n',0,0,0,0,0};description.reserve(description.size()+metadata.size());for(const char value:metadata)description.push_back(static_cast<unsigned char>(value));chunk(png,"iTXt",description);chunk(png,"IDAT",deflate);chunk(png,"IEND",{});return png;
}
void atomicWrite(const std::filesystem::path& target,const std::vector<unsigned char>& bytes,bool overwrite){
    if(!target.is_absolute()||target.filename().empty()||!std::filesystem::is_directory(target.parent_path()))throw ExportError("PNG destination must be an absolute file in an existing directory.");
    if(!overwrite&&std::filesystem::exists(target))throw ExportError("PNG destination already exists; set overwrite=1 explicitly.");
    static std::atomic_uint64_t serial{0};auto temporary=target;temporary+=std::filesystem::path(".penta-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial.fetch_add(1))+".tmp");
    try{
#ifdef _WIN32
        const HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw ExportError("Cannot create temporary PNG export.");
        bool good=true;for(std::size_t offset=0;offset<bytes.size();){const DWORD count=static_cast<DWORD>(std::min<std::size_t>(1048576,bytes.size()-offset));DWORD written=0;if(!WriteFile(file,bytes.data()+offset,count,&written,nullptr)||written!=count){good=false;break;}offset+=count;}good=FlushFileBuffers(file)&&good;CloseHandle(file);if(!good)throw ExportError("Cannot write complete native PNG.");
        if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH|(overwrite?MOVEFILE_REPLACE_EXISTING:0)))throw ExportError("Cannot safely commit PNG destination; an existing file was not changed.");
#else
        const int file=open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);if(file<0)throw ExportError("Cannot create temporary PNG export.");bool good=true;for(std::size_t offset=0;offset<bytes.size();){const auto written=write(file,bytes.data()+offset,bytes.size()-offset);if(written<=0){good=false;break;}offset+=static_cast<std::size_t>(written);}if(close(file)!=0)good=false;if(!good)throw ExportError("Cannot write complete native PNG.");
        if(overwrite)std::filesystem::rename(temporary,target);else{std::filesystem::create_hard_link(temporary,target);std::filesystem::remove(temporary);}
#endif
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
}
namespace {
void validateRaster(const std::vector<NativePanel>& panels,const PngOptions& options){
    if(options.width<160||options.height<120||options.width>16000||options.height>16000||options.dpi<36||options.dpi>1200||options.maximumPixels>64000000||static_cast<std::size_t>(options.width)*options.height>options.maximumPixels||panels.empty()||panels.size()>64||options.maximumPoints>2000000)throw ExportError("Native PNG size, DPI, panel count or memory budget is invalid.");

}
std::string rasterPanels(Image& image,const std::vector<NativePanel>& panels,const PngOptions& options){
    int rows=options.rows,columns=options.columns;if(rows==0&&columns==0){columns=static_cast<int>(std::ceil(std::sqrt(panels.size())));rows=(static_cast<int>(panels.size())+columns-1)/columns;}if(rows<1||columns<1||rows>64||columns>64||rows*columns>64)throw ExportError("Native PNG panel grid is invalid.");
    std::string metadata=image.commands?"Pentarnion Studio native CPU projection, vector paths and native mathematical typography.\n":"Pentarnion Studio native CPU projection. ASCII bitmap annotations; UTF-8 source labels retained below.\n";std::size_t aggregate=0;
    for(std::size_t index=0;index<panels.size();++index){image.check();const auto& p=panels[index];aggregate+=p.vectors.size();for(const auto& c:p.curves)aggregate+=c.points.size();if(p.mesh)aggregate+=p.mesh->points.size();if(p.graph)aggregate+=p.graph->positions.size()+p.graph->edges.size();if(aggregate>options.maximumPoints)throw ExportError("Native PNG aggregate plot-point budget exceeded.");
        const bool explicitPlacement=options.rows!=0||options.columns!=0;const int row=explicitPlacement?p.row:static_cast<int>(index)/columns,column=explicitPlacement?p.column:static_cast<int>(index)%columns;
        if(row<0||column<0||p.rowSpan<1||p.columnSpan<1||row+p.rowSpan>rows||column+p.columnSpan>columns)throw ExportError("Native PNG panel placement is outside its grid.");
        panel(image,p,{column*options.width/columns,row*options.height/rows,(column+p.columnSpan)*options.width/columns-1,(row+p.rowSpan)*options.height/rows-1});
        for(const auto& value:{p.title,p.xLabel,p.yLabel,p.zLabel,p.caption,p.context}){if(value.size()>65536||metadata.size()>1048576-value.size())throw ExportError("Native PNG annotation metadata exceeds 1 MiB.");metadata+=value;metadata+='\n';}
        if(!p.vectors.empty()||!p.basisLabels.empty()){
            std::size_t estimate=256+p.algebraId.size();for(const auto& label:p.basisLabels)estimate+=label.size()*6+8;
            if(p.algebraId.size()>65536||!std::isfinite(p.gamma))throw ExportError("Invalid native algebra projection identity/context.");
            for(const auto& vector:p.vectors){const auto count=vector.allCoefficients().size();if(vector.label.size()>65536||count>64)throw ExportError("Original algebra vector metadata exceeds its bounded shape.");const auto bytes=128+vector.label.size()*6+count*32;if(bytes>1048576-estimate)throw ExportError("Original algebra vector metadata exceeds the PNG1 MiB budget.");estimate+=bytes;}
            if(estimate>1048576-metadata.size())throw ExportError("Original algebra vector metadata exceeds the PNG1 MiB budget.");
            using Json=nlohmann::json;Json original={{"kind","algebra_projection"},{"algebra_id",p.algebraId},{"gamma",p.gamma},{"basis_labels",p.basisLabels},{"axes",p.axes},{"dimensions",p.dimensions},{"vectors",Json::array()}};
            for(const auto& vector:p.vectors){const auto coefficients=vector.allCoefficients();original["vectors"].push_back({{"label",vector.label},{"coefficients",std::vector<double>(coefficients.begin(),coefficients.end())}});}
            const auto bytes=original.dump();if(bytes.size()>=1048576||metadata.size()>=1048576-bytes.size())throw ExportError("Original algebra vector metadata exceeds the PNG1 MiB budget.");metadata+=bytes;metadata+='\n';
        }
        if(p.graph)for(const auto& label:p.graph->labels){if(metadata.size()+label.size()+1>1048576)throw ExportError("Graph label metadata exceeds the PNG budget.");metadata+=label;metadata+='\n';}
    }
    return metadata;

}
}
std::vector<unsigned char> rasterRgb(const std::vector<NativePanel>& panels,const PngOptions& options){
    validateRaster(panels,options);Image image(options);(void)rasterPanels(image,panels,options);return std::move(image.rgb);
}
void writePng(const std::vector<NativePanel>& panels,const std::filesystem::path& path,const PngOptions& options){
    validateRaster(panels,options);Image image(options);const auto metadata=rasterPanels(image,panels,options);atomicWrite(path,encode(image,metadata),options.overwrite);
}
void writeVector(const std::vector<NativePanel>& panels,const std::filesystem::path& path,const PngOptions& options,const std::filesystem::path& directory){
    validateRaster(panels,options);std::vector<VectorCommand> commands;Image image(options,&commands);const auto metadata=rasterPanels(image,panels,options);auto extension=path.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});if(extension!=".svg"&&extension!=".pdf")throw ExportError("Native vector exports require SVG or PDF.");atomicWrite(path,renderVector(commands,options.width,options.height,options.dpi,extension==".pdf",metadata,directory,options.cancellation),options.overwrite);
}
void NativeFigureScene::ingest(const runtime::FigureAction& action){
    using runtime::FigureActionKind;const auto kind=action.kind;
    if(kind==FigureActionKind::SavePlotTemplate||kind==FigureActionKind::LinkCameras||kind==FigureActionKind::ExportPlot||kind==FigureActionKind::SetAnimationPlaying||kind==FigureActionKind::LinkAxes||kind==FigureActionKind::Dock||kind==FigureActionKind::Undock||kind==FigureActionKind::TileStudio||kind==FigureActionKind::TileDesktop)return;
    if(kind==FigureActionKind::CloseAll){figures_.clear();selected_=0;return;}
    if(kind==FigureActionKind::SelectNew){int number=1;while(figures_.contains(number))++number;if(number>64)throw ExportError("At most 64 headless figures may be open.");selected_=number;figures_[number];return;}
    int number=action.figure? action.figure:selected_?selected_:1;if(number<1||number>64)throw ExportError("Headless figure number must be 1..64.");
    if(kind==FigureActionKind::Close){figures_.erase(number);if(selected_==number)selected_=figures_.empty()?0:figures_.begin()->first;return;}
    if(kind==FigureActionKind::Clear){figures_[number]=Figure{};return;}
    selected_=number;auto& figure=figures_[number];
    if(kind==FigureActionKind::Select)return;
    if(kind==FigureActionKind::Subplot){if(action.rows<1||action.columns<1||action.rows*action.columns>64||action.subplotIndex<1||action.subplotIndex>action.rows*action.columns)throw ExportError("Invalid native subplot grid.");figure.rows=action.rows;figure.columns=action.columns;figure.active=action.subplotIndex;return;}
    auto& p=figure.panels[figure.active];p.row=(figure.active-1)/figure.columns;p.column=(figure.active-1)%figure.columns;
    if(kind==FigureActionKind::SetAnimation||kind==FigureActionKind::SelectAnimationFrame){
        const auto data=kind==FigureActionKind::SetAnimation?action.animation:figure.animations[figure.active];if(!data)throw ExportError("The selected native subplot has no field animation.");const auto index=kind==FigureActionKind::SetAnimation?0:action.animationFrame;
        auto next=fieldAnimationFrame(*data,index);auto style=p.style;if(kind==FigureActionKind::SetAnimation)style.limits=next.style.limits;style.colorLimits=next.style.colorLimits;style.hold=false;style.legendVisible=data->dimension==1;
        if(kind==FigureActionKind::SelectAnimationFrame){const auto previous=fieldAnimationFrame(*data,figure.animationFrames[figure.active]);if(p.title!=previous.title)next.title=p.title;if(!p.xLabel.empty())next.xLabel=p.xLabel;if(!p.yLabel.empty())next.yLabel=p.yLabel;if(!p.zLabel.empty())next.zLabel=p.zLabel;}
        p=std::move(next);p.style=std::move(style);p.row=(figure.active-1)/figure.columns;p.column=(figure.active-1)%figure.columns;figure.animations[figure.active]=data;figure.animationFrames[figure.active]=index;return;
    }
    if(kind==FigureActionKind::ApplyPlotTemplate){if(!action.plotTemplate)throw ExportError("Missing plot template.");validatePlotTemplate(*action.plotTemplate);p.style=action.plotTemplate->style;p.title=action.plotTemplate->labels[0];p.xLabel=action.plotTemplate->labels[1];p.yLabel=action.plotTemplate->labels[2];p.zLabel=action.plotTemplate->labels[3];p.caption=action.plotTemplate->labels[4];p.latex=action.plotTemplate->latex;return;}
    if(kind==FigureActionKind::ConfigurePlot){if(!action.plotStyle)throw ExportError("Missing plot style patch.");p.style=runtime::patchedPlotStyle(p.style,*action.plotStyle);return;}
    if(kind==FigureActionKind::SetTitle){p.title=action.text;p.latex[0]=action.latex;return;}if(kind==FigureActionKind::SetXLabel){p.xLabel=action.text;p.latex[1]=action.latex;return;}if(kind==FigureActionKind::SetYLabel){p.yLabel=action.text;p.latex[2]=action.latex;return;}if(kind==FigureActionKind::SetZLabel){p.zLabel=action.text;p.latex[3]=action.latex;return;}if(kind==FigureActionKind::SetCaption){p.caption=action.text;p.latex[4]=action.latex;return;}
    if(kind!=FigureActionKind::Plot)throw ExportError("Unknown native plot action.");
    if(action.graph){if(p.style.hold&&(!p.curves.empty()||!p.vectors.empty()||p.mesh||p.graph))throw ExportError("Use a separate subplot for a graph view; held graph overlays are unsupported.");validateGraphView(*action.graph);if(figure.animations.erase(figure.active)){p.style.limits={};p.style.colorLimits.reset();}p.dimensions=action.dimensions;p.axes=action.axes;p.basisLabels.clear();p.algebraId.clear();p.curves.clear();p.vectors.clear();p.mesh.reset();p.graph=action.graph;p.style.equalUnits=true;p.context="Graph: "+std::to_string(p.graph->positions.size())+" nodes, "+std::to_string(p.graph->edges.size())+" stable edge IDs";return;}
    if(p.graph&&p.style.hold)throw ExportError("Use separate subplots for graph and numeric plot overlays.");
    p.graph.reset();
    if(p.style.hold&&(!p.curves.empty()||!p.vectors.empty()||p.mesh)&&p.dimensions!=action.dimensions)throw ExportError("Held plots must use the same dimensions.");
    if(p.style.hold&&(!p.curves.empty()||!p.vectors.empty()||p.mesh)&&p.axes!=action.axes)throw ExportError("Held plots must use the same coordinate axes.");
    if(p.style.hold&&(!p.curves.empty()||!p.vectors.empty()||p.mesh)&&(p.basisLabels!=action.basisLabels||(!p.basisLabels.empty()&&p.algebraId!=action.algebraId)))throw ExportError("Held algebra projections must use the same immutable definition and basis.");
    if(p.style.hold&&p.mesh&&action.mesh)throw ExportError("Holding two field meshes in one plot is currently unsupported; use separate subplots.");
    if(figure.animations.erase(figure.active)){p.style.limits={};p.style.colorLimits.reset();}figure.animationFrames.erase(figure.active);
    if(!p.style.hold){p.curves.clear();p.vectors.clear();p.mesh.reset();}
    p.dimensions=action.dimensions;p.axes=action.axes;p.basisLabels=action.basisLabels;p.algebraId=action.algebraId;p.gamma=action.gamma;p.curves.insert(p.curves.end(),action.curves.begin(),action.curves.end());p.vectors.insert(p.vectors.end(),action.vectors.begin(),action.vectors.end());if(action.mesh)p.mesh=action.mesh;
    if(p.curves.size()+p.vectors.size()>256)throw ExportError("Held plots exceed 256 series.");
    p.context=action.algebraId+(action.basisLabels.empty()?" | gamma="+std::to_string(action.gamma):" | "+std::to_string(action.basisLabels.size())+" real coefficients retained");
}
std::vector<NativePanel> NativeFigureScene::panels(int figure,int subplot)const{
    const auto found=figures_.find(figure?figure:selected_);if(found==figures_.end())throw ExportError("Requested headless figure is not open.");std::vector<NativePanel> result;
    for(const auto& [index,p]:found->second.panels)if((subplot==0||subplot==index)&&(!p.curves.empty()||!p.vectors.empty()||p.mesh||p.graph)){auto copy=p;copy.row=(index-1)/found->second.columns;copy.column=(index-1)%found->second.columns;result.push_back(std::move(copy));}
    if(result.empty())throw ExportError("Requested headless figure/subplot has no plot data.");
    return result;
}
PlotTemplate NativeFigureScene::currentTemplate(int figure,int subplot)const{const auto item=figures_.find(figure?figure:selected_);if(item==figures_.end())throw ExportError("Select a headless figure before saving its template.");const auto panel=item->second.panels.find(subplot?subplot:item->second.active);if(panel==item->second.panels.end())throw ExportError("Selected headless subplot has no style or labels.");const auto& p=panel->second;return {p.style,{p.title,p.xLabel,p.yLabel,p.zLabel,p.caption},p.latex};}
void NativeFigureScene::exportCurrentPng(const std::filesystem::path& path,PngOptions options,int figure,int subplot)const{
    const auto found=figures_.find(figure?figure:selected_);if(found==figures_.end())throw ExportError("Requested headless figure is not open.");auto items=panels(figure,subplot);if(subplot){items.front().row=items.front().column=0;options.rows=options.columns=1;}else{options.rows=found->second.rows;options.columns=found->second.columns;}writePng(items,path,options);
}
void NativeFigureScene::exportCurrentVector(const std::filesystem::path& path,PngOptions options,int figure,int subplot,const std::filesystem::path& directory)const{
    const auto found=figures_.find(figure?figure:selected_);if(found==figures_.end())throw ExportError("Requested headless figure is not open.");auto items=panels(figure,subplot);if(subplot){items.front().row=items.front().column=0;options.rows=options.columns=1;}else{options.rows=found->second.rows;options.columns=found->second.columns;}writeVector(items,path,options,directory);
}

}
