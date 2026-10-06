#pragma once

// Build-specific CLIENT unit reader. Call only on the SDK UI thread.
// No native calls, writes, or pointers escape in Frame. A coherent client update
// is still not a spawn notification: observation generations are NOT attack handles.
#include <array>
#include <cstdint>
#include <cstddef>
#include "target_selection.h"
#include "target_world.h"

namespace third_person {
enum class Relation { Unknown, OwnedPet, Friendly, Hostile };
struct ObservedTarget {
    TargetKey key{};
    uint32_t classId = 0;
    float x = 0, z = 0;
    Relation relation = Relation::Unknown;
    Sight sight = Sight::Unknown;
};
struct TargetFrame {
    uint64_t session = 0, tick = 0;
    float playerX = 0, playerZ = 0;
    uint32_t count = 0, rejected = 0, pets = 0;
    bool complete = false;
    std::array<ObservedTarget, 128> units{};
};

class TargetProvider {
    struct Identity { uintptr_t ptr = 0, path = 0; uint32_t id = 0, cls = 0, generation = 0; };
    std::array<Identity, 128> previous_{};
    size_t previousCount_ = 0;
    uint64_t session_ = 0;
    uint32_t serial_ = 0;
public:
    void reset() { previousCount_ = 0; session_ = 0; }

    // Reader: bool operator()(uintptr_t address, void* destination, size_t size).
    template<class Reader> bool sample(Reader& read, uintptr_t base, uint64_t session,
                                      uint32_t expectedPlayer, uint64_t tick, TargetFrame& out) {
        out = {};
        TargetFrame frame{};
        if (!session) { reset(); return false; }
        if (session_ != session) { reset(); session_ = session; }
        const auto get = [&](uintptr_t p, auto& value) { return p >= 0x10000 && read(p, &value, sizeof value); };
        uint32_t slot = 0, id = 0;
        uintptr_t player = 0, unit = 0, path = 0, room = 0;
        const auto fail = [&]() { reset(); return false; };
        if (!get(base+0x2a23704, slot) || slot >= 8 ||
            !get(base+0x2a238f0+slot*4, id) || id == UINT32_MAX || id != expectedPlayer ||
            !get(base+0x2a23910+(id&127)*8, unit)) return fail();
        std::array<uintptr_t, 512> hashSeen{};
        size_t hashCount = 0;
        while (unit) {
            if (hashCount == hashSeen.size()) return fail();
            for (size_t i=0; i<hashCount; ++i) if (hashSeen[i]==unit) return fail();
            hashSeen[hashCount++]=unit;
            uint32_t type=0, uid=0;
            if (!get(unit,type) || !get(unit+8,uid)) return fail();
            if (type==0 && uid==id) { player=unit; break; }
            if (!get(unit+0x158,unit)) return fail();
        }
        uint32_t playerMode=0, px=0, pz=0;
        if (!player || !get(player+12,playerMode) || playerMode==0 || playerMode==17 ||
            !get(player+0x38,path) || !path || !get(path,px) || !get(path+4,pz) ||
            !get(path+0x20,room) || !room) return fail();
        frame.playerX=float(px)/32768.0f; frame.playerZ=float(pz)/32768.0f;

        // Require a COMPLETE owner list. Missing ownership data is not "no owner".
        uintptr_t pet=0;
        if (!get(base+0x2a4dc10,pet)) return fail();
        std::array<uintptr_t,512> petSeen{};
        std::array<uint32_t,512> petIds{};
        size_t petCount=0;
        while (pet) {
            if (petCount==petSeen.size()) return fail();
            for (size_t i=0;i<petCount;++i) if (petSeen[i]==pet) return fail();
            petSeen[petCount]=pet;
            if (!get(pet+8,petIds[petCount]) || !get(pet+0x30,pet)) return fail();
            ++petCount;
        }
        uintptr_t neighbors=0;
        uint32_t roomCount=0;
        std::array<uintptr_t,65> rooms{};
        rooms[0]=room;
        if (!get(room,neighbors) || !get(room+0x40,roomCount) || roomCount>64 ||
            (roomCount && (!neighbors || !read(neighbors,rooms.data()+1,roomCount*sizeof(uintptr_t))))) return fail();
        const auto playerAlignment = ReadAlignment(read,player);
        std::array<CollisionGrid,65> grids{};
        size_t gridCount=0;
        for (size_t i=0;i<=roomCount;++i) {
            if (!rooms[i]) continue;
            bool duplicate=false;
            for (size_t j=0;j<i;++j) if (rooms[j]==rooms[i]) duplicate=true;
            CollisionGrid grid{};
            if (!duplicate && ReadCollisionGrid(read,rooms[i],grid)) grids[gridCount++]=grid;
        }
        std::array<uintptr_t,2048> seen{};
        size_t seenCount=0;
        std::array<Identity,128> current{};
        for (size_t r=0;r<=roomCount;++r) {
            if (!rooms[r]) continue;
            bool duplicate=false;
            for (size_t j=0;j<r;++j) if (rooms[j]==rooms[r]) duplicate=true;
            if (duplicate) continue;
            if (!get(rooms[r]+0xa8,unit)) return fail();
            while (unit) {
                if (seenCount==seen.size()) return fail();
                // A repeated node in different distinct room lists is also inconsistent.
                for (size_t j=0;j<seenCount;++j) if (seen[j]==unit) return fail();
                seen[seenCount++]=unit;
                uint32_t type=0, cls=0, uid=0, mode=0, flags=0;
                uintptr_t next=0, targetPath=0;
                if (!get(unit,type) || type>6 || !get(unit+0x160,next)) return fail();
                if (type==1) {
                    if (!get(unit+4,cls) || !get(unit+8,uid) || !get(unit+12,mode) ||
                        !get(unit+0x124,flags)) return fail();
                    if (mode==0 || mode==12 || (flags&0x0e)!=0x0e) ++frame.rejected;
                    else {
                        if (frame.count==frame.units.size() || !get(unit+0x38,targetPath) || !targetPath) return fail();
                        uint32_t x=0,z=0;
                        if (!get(targetPath,x) || !get(targetPath+4,z)) return fail();
                        for (size_t j=0;j<frame.count;++j) if (current[j].id==uid) return fail();
                        Identity identity{unit,targetPath,uid,cls,0};
                        for (size_t j=0;j<previousCount_;++j) {
                            const auto& old=previous_[j];
                            if (old.ptr==unit && old.path==targetPath && old.id==uid && old.cls==cls)
                                identity.generation=old.generation;
                        }
                        if (!identity.generation) {
                            if (serial_==UINT32_MAX) return fail();
                            identity.generation=++serial_;
                        }
                        bool owned=false;
                        for (size_t j=0;j<petCount;++j) if (petIds[j]==uid) owned=true;
                        if (owned) ++frame.pets;
                        Relation relation=owned ? Relation::OwnedPet : Relation::Unknown;
                        // Restrict to good player vs evil monster. Neutral, friendly,
                        // unknown and ownership-redirect flag0x400 are never hostile.
                        if (!owned && !(flags&0x400) && playerAlignment==Alignment::Good) {
                            const auto alignment=ReadAlignment(read,unit);
                            if (alignment==Alignment::Evil) relation=Relation::Hostile;
                            else if (alignment!=Alignment::Unknown) relation=Relation::Friendly;
                        }
                        auto& observed=frame.units[frame.count];
                        observed={{session,uid,identity.generation},cls,
                            float(x)/32768.0f,float(z)/32768.0f,relation};
                        if (relation==Relation::Hostile)
                            observed.sight=GroundSight(read,grids.data(),gridCount,
                                frame.playerX,frame.playerZ,observed.x,observed.z);
                        current[frame.count++]=identity;
                    }
                }
                unit=next;
            }
        }
        uint32_t finalSlot=0, finalId=0;
        uintptr_t finalPath=0, finalRoom=0;
        if (!get(base+0x2a23704,finalSlot) || finalSlot!=slot ||
            !get(base+0x2a238f0+slot*4,finalId) || finalId!=id ||
            !get(player+0x38,finalPath) || finalPath!=path ||
            !get(path+0x20,finalRoom) || finalRoom!=room) return fail();
        previous_=current; previousCount_=frame.count;
        frame.session=session; frame.tick=tick; frame.complete=true;
        out=frame;
        return true;
    }
};
} // namespace third_person
