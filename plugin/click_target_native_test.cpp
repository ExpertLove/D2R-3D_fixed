#include "click_target_native.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
using namespace click_target;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
constexpr std::uintptr_t base=0x140000000, unit=0x20000, table=base+0x2a41630;
struct Memory {
    std::map<std::uintptr_t, unsigned char> bytes;
    bool tear=false;
    unsigned recordReads=0;
    void put(std::uintptr_t at, const void* data, std::size_t n) {
        for (std::size_t i=0;i<n;++i) bytes[at+i]=static_cast<const unsigned char*>(data)[i];
    }
    template<class T> void value(std::uintptr_t at,T v) { put(at,&v,sizeof v); }
    bool operator()(std::uintptr_t at,void* data,std::size_t n) {
        if (at==table && ++recordReads==2 && tear) return false;
        for (std::size_t i=0;i<n;++i) {
            auto it=bytes.find(at+i); if(it==bytes.end()) return false;
            static_cast<unsigned char*>(data)[i]=it->second;
        }
        return true;
    }
};
Memory fixture() {
    Memory m;
    m.put(base+0x8b2d0,"\x8b\x05\x2e\x84\x99\x02\xc3",7);
    m.put(base+0x9a5d0,"\x4c\x63\xca\x48\x8d\x05\x36\x93\x98\x02",10);
    m.put(base+0xf1935,"\x48\x8d\x2d\xf4\xfc\x94\x02",7);
    m.put(base+0xf193f,"\x80\x7c\xc5\x00\x00",5);
    m.put(base+0xf1996,"\x8b\x7c\xc5\x04",4);
    m.put(base+0xf19c3,"\x8b\x4c\xc5\x08",4);
    m.put(base+0xf1c9f,"\x48\x8d\x0d\x8b\xf9\x94\x02",7);
    m.value<std::uint32_t>(base+0x2a23704,0);
    m.value<std::uint32_t>(base+0x2a238f0,1);
    const std::uint32_t row[]={1,1,7,0x00130013};
    m.put(table,row,sizeof row);
    m.value<std::uintptr_t>(base+0x2a23910+0x400+7*8,unit);
    const std::uint32_t header[]={1,19,7,1};
    m.put(unit,header,sizeof header);
    m.value<std::uintptr_t>(unit+0x158,0);
    return m;
}
int main() {
    SelectionReader reader;
    auto m=fixture();
    CHECK(!reader.sample(m).complete);
    CHECK(reader.initialize(m,base));
    auto pick=reader.sample(m);
    CHECK(pick.complete && pick.available && !pick.held && pick.id==7 && pick.classId==19 && pick.address==unit);
    m.value<unsigned char>(table+1,1);
    CHECK(reader.sample(m).held);
    m.value<unsigned char>(table,0);
    CHECK(reader.sample(m).complete && !reader.sample(m).available && !reader.sample(m).address);
    m.value<unsigned char>(table,2);
    CHECK(!reader.sample(m).complete);
    m=fixture(); m.value<std::uint32_t>(table+4,6);
    CHECK(!reader.sample(m).complete);
    m=fixture(); m.value<std::uint32_t>(base+0x2a23704,8);
    CHECK(!reader.sample(m).complete);
    m=fixture(); m.value<std::uint32_t>(base+0x2a238f0,0xffffffffu);
    CHECK(!reader.sample(m).complete);
    m=fixture(); m.value<std::uintptr_t>(base+0x2a23910+0x400+7*8,0);
    CHECK(!reader.sample(m).complete);
    m=fixture(); m.value<std::uint32_t>(unit+8,8); m.value<std::uintptr_t>(unit+0x158,unit);
    CHECK(!reader.sample(m).complete); // Hash cycle, not a miss.
    m=fixture(); m.tear=true;
    CHECK(!reader.sample(m).complete);
    for (auto rva : {0x8b2d0,0x9a5d0,0xf1935,0xf193f,0xf1996,0xf19c3,0xf1c9f}) {
        m=fixture(); m.bytes[base+rva]^=1;
        CHECK(!reader.initialize(m,base));
        CHECK(!reader.sample(m).complete);
    }
    Lifetime life;
    CHECK(!life.bind(10,7,unit,false).valid());
    auto first=life.bind(10,7,unit,true);
    CHECK(first.valid());
    life.destroying(unit+0x100);
    CHECK(life.resolve(10,7,unit)==first);
    life.destroying(unit);
    CHECK(!life.resolve(10,7,unit).valid()); // Same address/ID reused between polls.
    auto second=life.bind(10,7,unit,true);
    CHECK(second.valid() && !(second==first));
    CHECK(!life.resolve(11,7,unit).valid());
    life.bind(10,7,unit,true);
    CHECK(!life.resolve(10,7,unit+0x100).valid());
    life.bind(10,7,unit,true);
    CHECK(!life.resolve(10,8,unit).valid());
    life.bind(10,7,unit,true);
    CHECK(!life.resolve(10,7,0).valid());
    CHECK(!life.bind(0,7,unit,true).valid());
    CHECK(!life.bind(10,7,0,true).valid());
    std::puts("native selection reader / destruction lifetime contract PASS; hooks not installed");
}
