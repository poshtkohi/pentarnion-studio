#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace penta::discovery::basin {
// Candidate labels compare complete physical-coordinate tail clouds. These are
// finite-sample equivalence heuristics, never certified attractor identities.
struct Tail {std::vector<double> samples;std::size_t dimension=0;bool equilibrium=false;};
inline double cloudDistance(const Tail& a,const Tail& b){
    if(!a.dimension||a.dimension!=b.dimension||a.samples.empty()||b.samples.empty()||a.samples.size()%a.dimension||b.samples.size()%b.dimension)throw std::runtime_error("Invalid basin tail dimensions.");
    auto directed=[](const Tail& x,const Tail& y){double maximum=0;for(std::size_t i=0;i<x.samples.size();i+=x.dimension){double nearest=INFINITY;for(std::size_t j=0;j<y.samples.size();j+=y.dimension){double d=0;for(std::size_t k=0;k<x.dimension;++k)d=std::hypot(d,x.samples[i+k]-y.samples[j+k]);nearest=std::min(nearest,d);}maximum=std::max(maximum,nearest);}return maximum;};
    return std::max(directed(a,b),directed(b,a));
}
inline Tail sampledTail(const std::vector<double>& values,std::size_t full,std::size_t physical,std::size_t begin,std::size_t limit,bool equilibrium){
    if(!full||!physical||physical>full||values.size()%full||begin>=values.size()/full||!limit)throw std::runtime_error("Invalid basin tail sample range.");
    Tail tail;tail.dimension=physical;tail.equilibrium=equilibrium;const auto total=values.size()/full-begin,count=std::min(limit,total);
    for(std::size_t i=0;i<count;++i){const auto row=begin+(count==1?total-1:i*(total-1)/(count-1));for(std::size_t k=0;k<physical;++k){const double v=values[row*full+k];if(!std::isfinite(v))throw std::runtime_error("Nonfinite basin tail.");tail.samples.push_back(v);}}
    return tail;
}
inline bool stationary(const Tail& tail,double tolerance){
    const auto count=tail.samples.size()/tail.dimension;if(count<8)return tail.equilibrium;
    const auto half=count/2;Tail a,b;a.dimension=b.dimension=tail.dimension;
    a.samples.assign(tail.samples.begin(),tail.samples.begin()+static_cast<std::ptrdiff_t>(half*tail.dimension));b.samples.assign(tail.samples.begin()+static_cast<std::ptrdiff_t>(half*tail.dimension),tail.samples.end());
    // Distribution moments avoid demanding identical orbit phases in disjoint
    // windows. Full-cloud comparison still decides equivalence between runs.
    for(std::size_t k=0;k<tail.dimension;++k){double ma=0,mb=0,va=0,vb=0;for(std::size_t i=k;i<a.samples.size();i+=tail.dimension)ma+=a.samples[i]/half;for(std::size_t i=k;i<b.samples.size();i+=tail.dimension)mb+=b.samples[i]/(count-half);for(std::size_t i=k;i<a.samples.size();i+=tail.dimension)va+=std::pow(a.samples[i]-ma,2)/half;for(std::size_t i=k;i<b.samples.size();i+=tail.dimension)vb+=std::pow(b.samples[i]-mb,2)/(count-half);if(std::abs(ma-mb)>tolerance||std::abs(std::sqrt(va)-std::sqrt(vb))>tolerance)return false;}
    return true;
}
struct Catalogue {
    double tolerance=.1,separation=.3;std::size_t maximum=32;
    std::vector<Tail> representatives;
    int classify(const Tail& tail){
        if(!(tolerance>0)||!(separation>tolerance)||!std::isfinite(separation)||maximum<1||maximum>256)throw std::runtime_error("Invalid basin clustering thresholds.");
        std::vector<std::size_t> matches;double nearest=INFINITY;
        for(std::size_t k=0;k<representatives.size();++k){if(tail.equilibrium!=representatives[k].equilibrium)continue;const double distance=cloudDistance(tail,representatives[k]);nearest=std::min(nearest,distance);if(distance<=tolerance)matches.push_back(k);}
        if(matches.size()==1)return static_cast<int>(matches.front()+1);
        if(!matches.empty()||nearest<separation||representatives.size()>=maximum)return 0;
        representatives.push_back(tail);return static_cast<int>(representatives.size());
    }
};
}
