#include "native_internal.hpp"
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace penta::ode::detail {
namespace {
constexpr std::array<std::uint32_t,64> k{{
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2}};
std::uint32_t rotate(std::uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
class Hash {
    std::array<std::uint32_t,8> h{{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    std::array<unsigned char,64> buffer{};std::size_t used=0;std::uint64_t bytes=0;
    void block(){
        std::array<std::uint32_t,64>w{};for(unsigned i=0;i<16;++i)w[i]=(std::uint32_t(buffer[4*i])<<24)|(std::uint32_t(buffer[4*i+1])<<16)|(std::uint32_t(buffer[4*i+2])<<8)|buffer[4*i+3];
        for(unsigned i=16;i<64;++i){const auto a=w[i-15],b=w[i-2];w[i]=w[i-16]+(rotate(a,7)^rotate(a,18)^(a>>3))+w[i-7]+(rotate(b,17)^rotate(b,19)^(b>>10));}
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],q=h[7];
        for(unsigned i=0;i<64;++i){const auto t1=q+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^((~e)&g))+k[i]+w[i];const auto t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));q=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=q;
    }
public:
    void add(std::string_view s){bytes+=s.size();for(unsigned char c:s){buffer[used++]=c;if(used==64){block();used=0;}}}
    std::string finish(){const auto bits=bytes*8;buffer[used++]=0x80;if(used>56){while(used<64)buffer[used++]=0;block();used=0;}while(used<56)buffer[used++]=0;for(unsigned i=0;i<8;++i)buffer[63-i]=static_cast<unsigned char>(bits>>(8*i));block();std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto x:h)out<<std::setw(8)<<x;return out.str();}
};
}
std::string sha256(std::string_view s){Hash h;h.add(s);return h.finish();}
std::string fileHash(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);if(!f)throw OdeError("O1002","Cannot read native cache/compiler artifact.");Hash h;std::array<char,65536>b{};std::size_t size=0;while(f){f.read(b.data(),b.size());const auto n=f.gcount();size+=static_cast<std::size_t>(n);if(size>512*1024*1024)throw OdeError("O1002","Native cache/compiler artifact exceeds its size limit.");h.add(std::string_view(b.data(),static_cast<std::size_t>(n)));}if(!f.eof())throw OdeError("O1002","Cannot fully read native cache/compiler artifact.");return h.finish();}
} // namespace penta::ode::detail
