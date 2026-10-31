#include "raster_depth_painter.hpp"
#include <QtGui/qrgb.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace penta {
namespace {
constexpr std::uint64_t maximumOperations = 250000000;
constexpr std::size_t maximumBytes = 256U * 1024U * 1024U;
constexpr std::size_t maximumPixels = 16000000;
constexpr int tileSide = 64, sampleCount = 4;
constexpr std::array<double, sampleCount> offsetX{.25, .75, .25, .75};
constexpr std::array<double, sampleCount> offsetY{.25, .25, .75, .75};

struct Failure { QString message; };
struct Limits {
    std::shared_ptr<std::atomic_bool> cancelled;
    std::uint64_t operations = 0;
    std::size_t bytes = 0;
    std::uint64_t maxOperations = maximumOperations;
    std::size_t maxBytes = maximumBytes;
    Limits(std::shared_ptr<std::atomic_bool> flag,RasterDepthBudget budget)
        :cancelled(std::move(flag)),maxOperations(std::min(maximumOperations,budget.maximumOperations)),
         maxBytes(std::min(maximumBytes,budget.maximumBytes)) {}
    void check() const {
        if (cancelled && cancelled->load(std::memory_order_relaxed))
            throw Failure{QStringLiteral("Figure rendering was cancelled.")};
    }
    void step(std::uint64_t count = 1) {
        if (count > maxOperations - operations)
            throw Failure{QStringLiteral("Figure geometry exceeds its operation display budget (maximum 250 million).")};
        operations += count;
        if ((operations & 1023U) == 0 || count > 1023) check();
    }
    void allocate(std::size_t count) {
        if (count > maxBytes - bytes)
            throw Failure{QStringLiteral("Figure buffers and transparent fragments exceed the storage display budget (maximum 256 MiB).")};
        bytes += count;
    }
};

struct Vertex { double x = 0, y = 0, z = 0; };
struct Prepared {
    std::array<Vertex, 3> p;
    QRgb color = 0;
    std::uint32_t order = 0;
    double radius = .5;
    int kind = 0, left = 0, top = 0, right = -1, bottom = -1;
};
struct Sample {
    double depth = std::numeric_limits<double>::infinity();
    QRgb color = 0;
    std::uint32_t order = 0;
};
struct Fragment {
    double depth = 0;
    QRgb color = 0;
    std::uint32_t sample = 0, order = 0;
};

double cross(const Vertex& a, const Vertex& b, const Vertex& c) {
    return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
}
bool samePosition(const Vertex& a, const Vertex& b) { return a.x==b.x && a.y==b.y; }
bool segmentsIntersect(const Vertex& a,const Vertex& b,const Vertex& c,const Vertex& d) {
    const double abC=cross(a,b,c),abD=cross(a,b,d),cdA=cross(c,d,a),cdB=cross(c,d,b);
    const auto within=[](const Vertex& p,const Vertex& x,const Vertex& y) {
        return p.x>=std::min(x.x,y.x) && p.x<=std::max(x.x,y.x) && p.y>=std::min(x.y,y.y) && p.y<=std::max(x.y,y.y);
    };
    if ((abC==0 && within(c,a,b)) || (abD==0 && within(d,a,b)) ||
        (cdA==0 && within(a,c,d)) || (cdB==0 && within(b,c,d))) return true;
    return ((abC<0 && abD>0) || (abC>0 && abD<0)) && ((cdA<0 && cdB>0) || (cdA>0 && cdB<0));
}
bool topLeft(const Vertex& a, const Vertex& b) { return b.y>a.y || (b.y==a.y && b.x<a.x); }
bool insideEdge(double value, const Vertex& a, const Vertex& b) {
    return value>0 || (value==0 && topLeft(a,b));
}
QRgb sourceOver(QRgb source, QRgb destination) {
    const int inverse = 255-qAlpha(source);
    return qRgba(qRed(source)+(qRed(destination)*inverse+127)/255,
                 qGreen(source)+(qGreen(destination)*inverse+127)/255,
                 qBlue(source)+(qBlue(destination)*inverse+127)/255,
                 qAlpha(source)+(qAlpha(destination)*inverse+127)/255);
}

std::vector<Prepared> prepare(const std::vector<RasterPrimitive>& input, QRectF clip, Limits& limits) {
    std::size_t count = 0;
    for (const auto& primitive : input) {
        limits.step();
        if (primitive.kind<0 || primitive.kind>2 || !primitive.color.isValid())
            throw Failure{QStringLiteral("Invalid figure primitive kind or color.")};
        const auto n=static_cast<std::size_t>(primitive.points.size());
        if (!primitive.vertexDepths.empty() && primitive.vertexDepths.size()!=n)
            throw Failure{QStringLiteral("Each projected figure vertex must have exactly one depth.")};
        if (!std::isfinite(primitive.depth) || !std::isfinite(primitive.width) ||
            (primitive.kind!=1 && (primitive.width<=0 || primitive.width>32000)))
            throw Failure{QStringLiteral("Invalid figure depth or stroke width.")};
        if (primitive.vertexDepths.empty() && (primitive.depth < -1e-12 || primitive.depth > 1+1e-12))
            throw Failure{QStringLiteral("Figure depths must lie in the normalized camera range [0,1].")};
        for (std::size_t i=0;i<n;++i) {
            limits.step();
            if (!std::isfinite(primitive.points[static_cast<qsizetype>(i)].x()) ||
                !std::isfinite(primitive.points[static_cast<qsizetype>(i)].y()) ||
                std::abs(primitive.points[static_cast<qsizetype>(i)].x())>1e9 ||
                std::abs(primitive.points[static_cast<qsizetype>(i)].y())>1e9 ||
                (!primitive.vertexDepths.empty() && !std::isfinite(primitive.vertexDepths[i])))
                throw Failure{QStringLiteral("Figure coordinates or depths are non-finite or outside the bounded raster range.")};
            if (!primitive.vertexDepths.empty() && (primitive.vertexDepths[i] < -1e-12 || primitive.vertexDepths[i] > 1+1e-12))
                throw Failure{QStringLiteral("Figure vertex depths must lie in the normalized camera range [0,1].")};
        }
        if (!n || primitive.color.alpha()==0) continue;
        const std::size_t added=primitive.kind==1?(n>=3?n-2:0):primitive.kind==0?std::max<std::size_t>(1,n-1):n;
        if (added>2000000-count)
            throw Failure{QStringLiteral("Figure geometry exceeds two million raster primitives.")};
        count+=added;
    }
    limits.allocate(count*sizeof(Prepared));
    std::vector<Prepared> result; result.reserve(count);
    std::uint32_t order=0;
    auto append=[&](Prepared value) {
        limits.step();
        double left=value.p[0].x,right=left,top=value.p[0].y,bottom=top;
        const int n=value.kind==1?3:value.kind==0?2:1;
        for (int i=1;i<n;++i) {left=std::min(left,value.p[i].x);right=std::max(right,value.p[i].x);top=std::min(top,value.p[i].y);bottom=std::max(bottom,value.p[i].y);}
        const double radius=value.kind==1?0:value.radius;
        left=std::max(clip.left(),left-radius); right=std::min(clip.right(),right+radius);
        top=std::max(clip.top(),top-radius); bottom=std::min(clip.bottom(),bottom+radius);
        if (left>=right || top>=bottom) return;
        value.left=static_cast<int>(std::floor(left));value.right=static_cast<int>(std::ceil(right))-1;
        value.top=static_cast<int>(std::floor(top));value.bottom=static_cast<int>(std::ceil(bottom))-1;
        value.order=order++;
        result.push_back(value);
    };
    for (const auto& primitive : input) {
        limits.check();
        const auto n=static_cast<std::size_t>(primitive.points.size());
        if (!n || primitive.color.alpha()==0) continue;
        const auto at=[&](std::size_t i){const auto p=primitive.points[static_cast<qsizetype>(i)];return Vertex{p.x(),p.y(),std::clamp(primitive.vertexDepths.empty()?primitive.depth:primitive.vertexDepths[i],0.,1.)};};
        Prepared value;value.color=qPremultiply(primitive.color.rgba());value.kind=primitive.kind;value.radius=primitive.width*.5;
        if (primitive.kind==2 || (primitive.kind==0 && n==1)) {
            value.kind=2;for (std::size_t i=0;i<n;++i) {value.p[0]=at(i);append(value);}continue;
        }
        if (primitive.kind==0) {
            for (std::size_t i=1;i<n;++i) {value.p[0]=at(i-1);value.p[1]=at(i);append(value);}continue;
        }
        if (n<3) continue;
        const std::size_t temporary=n*sizeof(Vertex);
        limits.allocate(temporary);
        std::vector<Vertex> polygon;polygon.reserve(n);
        for (std::size_t i=0;i<n;++i) {
            auto vertex=at(i);
            if (!polygon.empty() && samePosition(polygon.back(),vertex)) {
                if (polygon.back().z!=vertex.z) throw Failure{QStringLiteral("A filled polygon has conflicting depths at one vertex.")};
                continue;
            }
            polygon.push_back(vertex);
        }
        if (polygon.size()>1 && samePosition(polygon.front(),polygon.back())) {
            if (polygon.front().z!=polygon.back().z) throw Failure{QStringLiteral("A filled polygon has conflicting closing depths.")};
            polygon.pop_back();
        }
        if (polygon.size()<3) {limits.bytes-=temporary;continue;}
        // Reject non-simple polygons instead of returning a plausible, wrong
        // fill. Triangle input (the normal VTK path) skips this quadratic check.
        if (polygon.size()>3) for (std::size_t i=0;i<polygon.size();++i) for (std::size_t j=i+1;j<polygon.size();++j) {
            limits.step();const auto nextI=(i+1)%polygon.size(),nextJ=(j+1)%polygon.size();
            if (nextI==j || nextJ==i) continue;
            if (segmentsIntersect(polygon[i],polygon[nextI],polygon[j],polygon[nextJ]))
                throw Failure{QStringLiteral("Filled polygon is self-intersecting or touches itself.")};
        }
        double area=0;for (std::size_t i=0;i<polygon.size();++i) {limits.step();const auto& a=polygon[i];const auto& b=polygon[(i+1)%polygon.size()];area+=a.x*b.y-a.y*b.x;}
        if (area==0) {
            if (polygon.size()>3) throw Failure{QStringLiteral("Filled polygon is degenerate or self-intersecting.")};
            limits.bytes-=temporary;continue;
        }
        if (area<0) std::reverse(polygon.begin(),polygon.end());
        while (polygon.size()>3) {
            bool removed=false;
            for (std::size_t i=0;i<polygon.size();++i) {
                limits.step();
                const auto previous=(i+polygon.size()-1)%polygon.size(),next=(i+1)%polygon.size();
                const auto a=polygon[previous],b=polygon[i],c=polygon[next];
                const double orientation=cross(a,b,c);
                if (orientation==0) {polygon.erase(polygon.begin()+static_cast<std::ptrdiff_t>(i));removed=true;break;}
                if (orientation<0) continue;
                bool occupied=false;
                for (std::size_t j=0;j<polygon.size();++j) {
                    limits.step();if (j==previous || j==i || j==next) continue;
                    if (cross(a,b,polygon[j])>=0 && cross(b,c,polygon[j])>=0 && cross(c,a,polygon[j])>=0) {occupied=true;break;}
                }
                if (occupied) continue;
                value.p={a,b,c};append(value);polygon.erase(polygon.begin()+static_cast<std::ptrdiff_t>(i));removed=true;break;
            }
            if (!removed) throw Failure{QStringLiteral("Filled polygon cannot be triangulated safely; check for self-intersections.")};
        }
        if (polygon.size()==3 && cross(polygon[0],polygon[1],polygon[2])>0) {value.p={polygon[0],polygon[1],polygon[2]};append(value);}
        limits.bytes-=temporary;
    }
    return result;
}
}

QImage rasterizePlotGeometry(QSize physical,const std::vector<RasterPrimitive>& primitives,
                            QRectF clip,std::shared_ptr<std::atomic_bool> cancelled,QString* error) {
    return rasterizePlotGeometry(physical,primitives,clip,std::move(cancelled),RasterDepthBudget{},error);
}

QImage rasterizePlotGeometry(QSize physical,const std::vector<RasterPrimitive>& primitives,
                            QRectF clip,std::shared_ptr<std::atomic_bool> cancelled,RasterDepthBudget budget,QString* error) {
    if (error) error->clear();
    try {
        Limits limits{std::move(cancelled),budget};limits.check();
        if (physical.width()<1 || physical.height()<1 || physical.width()>16000 || physical.height()>16000 ||
            static_cast<std::uint64_t>(physical.width())*physical.height()>std::min(maximumPixels,budget.maximumPixels))
            throw Failure{QStringLiteral("Figure dimensions exceed the 16 million pixel display budget.")};
        if (!std::isfinite(clip.x()) || !std::isfinite(clip.y()) || !std::isfinite(clip.width()) || !std::isfinite(clip.height()) || clip.width()<0 || clip.height()<0)
            throw Failure{QStringLiteral("Invalid figure clip rectangle.")};
        limits.allocate(static_cast<std::size_t>(physical.width())*physical.height()*sizeof(QRgb));
        QImage output(physical,QImage::Format_ARGB32_Premultiplied);
        if (output.isNull()) throw Failure{QStringLiteral("Cannot allocate the bounded figure image.")};
        output.fill(Qt::transparent);
        clip=clip.intersected(QRectF(QPointF(0,0),QSizeF(physical)));
        if (clip.isEmpty() || primitives.empty()) return output;
        constexpr std::size_t tileSamples=tileSide*tileSide*sampleCount;
        limits.allocate(tileSamples*sizeof(Sample));
        std::vector<Sample> samples(tileSamples);
        auto geometry=prepare(primitives,clip,limits);
        if (geometry.empty()) return output;
        const int columns=(physical.width()+tileSide-1)/tileSide,rows=(physical.height()+tileSide-1)/tileSide;
        const std::size_t tiles=static_cast<std::size_t>(columns)*rows;
        limits.allocate((tiles+1)*sizeof(std::size_t)*2);
        std::vector<std::size_t> offsets(tiles+1,0),positions(tiles+1,0);
        std::size_t references=0;
        for (const auto& value:geometry) {
            const auto count=static_cast<std::size_t>(value.right/tileSide-value.left/tileSide+1)*(value.bottom/tileSide-value.top/tileSide+1);
            limits.step(count);
            if (count>(limits.maxBytes-limits.bytes)/sizeof(std::uint32_t)-references)
                throw Failure{QStringLiteral("Figure tile references exceed the 256 MiB display budget.")};
            references+=count;
            for (int y=value.top/tileSide;y<=value.bottom/tileSide;++y) for (int x=value.left/tileSide;x<=value.right/tileSide;++x)
                ++offsets[static_cast<std::size_t>(y)*columns+x+1];
        }
        limits.allocate(references*sizeof(std::uint32_t));
        std::vector<std::uint32_t> indices(references);
        std::partial_sum(offsets.begin(),offsets.end(),offsets.begin());positions=offsets;
        for (std::size_t i=0;i<geometry.size();++i) {
            const auto& value=geometry[i];
            for (int y=value.top/tileSide;y<=value.bottom/tileSide;++y) for (int x=value.left/tileSide;x<=value.right/tileSide;++x) {
                limits.step();const auto tile=static_cast<std::size_t>(y)*columns+x;
                indices[positions[tile]++]=static_cast<std::uint32_t>(i);
            }
        }
        std::vector<Fragment> fragments;
        const auto permanentBytes=limits.bytes;
        auto fragment=[&](Fragment value) {
            if (fragments.size()==fragments.capacity()) {
                const std::size_t capacity=std::max<std::size_t>(256,fragments.capacity()*2);
                // During reserve both the old and new allocations can be live.
                if (capacity>(limits.maxBytes-limits.bytes)/sizeof(Fragment))
                    throw Failure{QStringLiteral("Transparent figure fragments exceed the 256 MiB display budget.")};
                fragments.reserve(capacity);limits.bytes=permanentBytes+fragments.capacity()*sizeof(Fragment);
            }
            fragments.push_back(value);
        };
        for (std::size_t tile=0;tile<tiles;++tile) {
            limits.check();if (offsets[tile]==offsets[tile+1]) continue;
            std::fill(samples.begin(),samples.end(),Sample{});fragments.clear();
            const int tileX=static_cast<int>(tile%columns)*tileSide,tileY=static_cast<int>(tile/columns)*tileSide;
            for (auto index=offsets[tile];index<offsets[tile+1];++index) {
                limits.step();const auto& value=geometry[indices[index]];
                const int left=std::max(tileX,value.left),right=std::min(tileX+tileSide-1,value.right);
                const int top=std::max(tileY,value.top),bottom=std::min(tileY+tileSide-1,value.bottom);
                const double area=value.kind==1?cross(value.p[0],value.p[1],value.p[2]):0;
                const double dx=value.p[1].x-value.p[0].x,dy=value.p[1].y-value.p[0].y;
                const double square=dx*dx+dy*dy;
                for (int y=top;y<=bottom;++y) for (int x=left;x<=right;++x) for (int sample=0;sample<sampleCount;++sample) {
                    limits.step();const double px=x+offsetX[sample],py=y+offsetY[sample];
                    if (px<clip.left() || px>=clip.right() || py<clip.top() || py>=clip.bottom()) continue;
                    double depth=value.p[0].z;
                    if (value.kind==1) {
                        const Vertex point{px,py,0};
                        const double a=cross(value.p[1],value.p[2],point),b=cross(value.p[2],value.p[0],point),c=cross(value.p[0],value.p[1],point);
                        if (!insideEdge(a,value.p[1],value.p[2]) || !insideEdge(b,value.p[2],value.p[0]) || !insideEdge(c,value.p[0],value.p[1])) continue;
                        depth=(a*value.p[0].z+b*value.p[1].z+c*value.p[2].z)/area;
                    } else {
                        const double t=value.kind==0 && square>0?std::clamp(((px-value.p[0].x)*dx+(py-value.p[0].y)*dy)/square,0.,1.):0;
                        const double rx=px-value.p[0].x-t*dx,ry=py-value.p[0].y-t*dy;
                        if (rx*rx+ry*ry>value.radius*value.radius) continue;
                        if (value.kind==0) depth+=(value.p[1].z-value.p[0].z)*t;
                    }
                    if (depth<0 || depth>1 || !std::isfinite(depth)) continue;
                    const auto slot=static_cast<std::uint32_t>(((y-tileY)*tileSide+x-tileX)*sampleCount+sample);
                    auto& destination=samples[slot];
                    if (depth>destination.depth || (depth==destination.depth && value.order<destination.order)) continue;
                    if (qAlpha(value.color)==255) destination={depth,value.color,value.order};
                    else fragment({depth,value.color,slot,value.order});
                }
            }
            if (!fragments.empty()) {
                limits.check();
                std::sort(fragments.begin(),fragments.end(),[&](const Fragment& a,const Fragment& b) {
                    limits.step();
                    if (a.sample!=b.sample) return a.sample<b.sample;
                    if (a.depth!=b.depth) return a.depth>b.depth;
                    return a.order<b.order;
                });
                for (const auto& value:fragments) {
                    limits.step();auto& destination=samples[value.sample];
                    if (value.depth<destination.depth || (value.depth==destination.depth && value.order>=destination.order))
                        destination.color=sourceOver(value.color,destination.color);
                }
            }
            const int lastX=std::min(tileX+tileSide,physical.width()),lastY=std::min(tileY+tileSide,physical.height());
            for (int y=tileY;y<lastY;++y) {
                auto* row=reinterpret_cast<QRgb*>(output.scanLine(y));
                for (int x=tileX;x<lastX;++x) {
                    limits.step();int red=0,green=0,blue=0,alpha=0;
                    const auto first=((y-tileY)*tileSide+x-tileX)*sampleCount;
                    for (int i=0;i<sampleCount;++i) {const auto color=samples[first+i].color;red+=qRed(color);green+=qGreen(color);blue+=qBlue(color);alpha+=qAlpha(color);}
                    row[x]=qRgba((red+2)/sampleCount,(green+2)/sampleCount,(blue+2)/sampleCount,(alpha+2)/sampleCount);
                }
            }
        }
        limits.check();return output;
    } catch (const Failure& failure) {if (error) *error=failure.message;}
      catch (const std::bad_alloc&) {if (error) *error=QStringLiteral("Cannot allocate bounded figure raster buffers.");}
      catch (const std::exception& exception) {if (error) *error=QStringLiteral("CPU figure rasterization failed: %1").arg(QString::fromUtf8(exception.what()));}
    return {};
}
}
