#pragma once
#include "click_target.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace click_target {
// Traced from live 3.2.92777 code; see diagnostics/CLICK-TARGET-NATIVE-PATH.md.
// Read-only adapter. Not linked/installed into experimental.11.
struct NativePick {
    bool complete = false;
    bool available = false;
    bool held = false;
    std::uint32_t slot = 0, type = 6, id = 0xffffffffu, classId = 0, mode = 0;
    std::uintptr_t address = 0; // Transient diagnostic value, NEVER an attack handle.
};

class SelectionReader {
public:
    template<class Read> bool initialize(Read& read, std::uintptr_t base) {
        base_ = 0;
        struct Guard { std::uintptr_t rva; const char* bytes; std::size_t size; };
        const Guard guards[] = {
            {0x8b2d0, "\x8b\x05\x2e\x84\x99\x02\xc3", 7},
            {0x9a5d0, "\x4c\x63\xca\x48\x8d\x05\x36\x93\x98\x02", 10},
            {0xf1935, "\x48\x8d\x2d\xf4\xfc\x94\x02", 7},
            {0xf193f, "\x80\x7c\xc5\x00\x00", 5},
            {0xf1996, "\x8b\x7c\xc5\x04", 4},
            {0xf19c3, "\x8b\x4c\xc5\x08", 4},
            {0xf1c9f, "\x48\x8d\x0d\x8b\xf9\x94\x02", 7},
        };
        if (base < 0x10000 || base > std::numeric_limits<std::uintptr_t>::max()-0x2a25110)
            return false;
        for (const auto& g : guards) {
            std::array<unsigned char, 16> actual{};
            if (!read(base+g.rva, actual.data(), g.size) ||
                std::memcmp(actual.data(), g.bytes, g.size)) return false;
        }
        base_ = base;
        return true;
    }
    // Does NOT call SelectUpdate or return a gameplay-approved candidate.
    // External readers remain non-atomic even after these endpoint checks.
    template<class Read> NativePick sample(Read& read) const {
        NativePick out;
        if (!base_ || !get(read, base_+0x2a23704, out.slot) || out.slot >= 8) return {};
        std::uint32_t player = 0xffffffffu;
        if (!get(read, base_+0x2a238f0+out.slot*4, player) || player == 0xffffffffu) return {};
        const auto at = base_+0x2a41630+out.slot*16;
        std::array<unsigned char,16> record{}, end{};
        if (!read(at, record.data(), record.size()) || record[0]>1 || record[1]>1) return {};
        out.available = record[0] != 0;
        out.held = record[1] != 0;
        std::memcpy(&out.type, record.data()+4, 4);
        std::memcpy(&out.id, record.data()+8, 4);
        if (out.available) {
            if (out.type > 5 || out.id == 0xffffffffu) return {};
            std::uintptr_t unit = 0;
            if (!get(read, base_+0x2a23910+out.type*0x400+(out.id&127)*8, unit)) return {};
            std::array<std::uintptr_t,512> seen{};
            std::size_t count = 0;
            while (unit) {
                if (unit<0x10000 || unit>std::numeric_limits<std::uintptr_t>::max()-0x160 || count==seen.size()) return {};
                for (std::size_t i=0; i<count; ++i) if (seen[i]==unit) return {};
                seen[count++] = unit;
                std::array<std::uint32_t,4> header{};
                if (!read(unit, header.data(), sizeof(header))) return {};
                if (header[0]==out.type && header[2]==out.id) {
                    out.address=unit; out.classId=header[1]; out.mode=header[3];
                    break;
                }
                if (!get(read, unit+0x158, unit)) return {};
            }
            // A selected ID not resolving is NOT a safe empty-ground click.
            if (!out.address) return {};
        }
        std::uint32_t slotEnd=0, playerEnd=0;
        if (!read(at, end.data(), end.size()) || record!=end ||
            !get(read, base_+0x2a23704, slotEnd) || slotEnd!=out.slot ||
            !get(read, base_+0x2a238f0+out.slot*4, playerEnd) || playerEnd!=player) return {};
        out.complete=true;
        return out;
    }
private:
    template<class Read, class T> static bool get(Read& read, std::uintptr_t at, T& value) {
        return read(at, &value, sizeof(value));
    }
    std::uintptr_t base_ = 0;
};

// One selected lifetime, NOT a polling-based generation provider. Adapter must
// serialize these calls with native client-unit destruction on the game thread,
// invalidate BEFORE original destructor, and clear on session/control loss.
// Tracking is forbidden until destructor-hook coverage and thread are verified.
class Lifetime {
public:
    Identity bind(std::uint64_t session, std::uint32_t id, std::uintptr_t address,
                  bool destructionCoverageVerified) {
        clear();
        if (!destructionCoverageVerified || !session || address<0x10000 ||
            serial_==std::numeric_limits<std::uint64_t>::max()) return {};
        address_ = address;
        identity_ = {session, ++serial_, id};
        return identity_;
    }
    bool destroying(std::uintptr_t address) {
        if (address && address==address_) { clear(); return true; }
        return false;
    }
    Identity resolve(std::uint64_t session, std::uint32_t id, std::uintptr_t freshAddress) {
        if (session!=identity_.session || id!=identity_.id || !freshAddress || freshAddress!=address_)
            clear();
        return identity_;
    }
    void clear() { address_=0; identity_={}; }
private:
    std::uintptr_t address_=0; // Equality witness only; never dereferenced.
    std::uint64_t serial_=0;
    Identity identity_;
};
} // namespace click_target
