#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gmf {
struct Location { std::size_t call = 0, projection = 0; int matches = 0; };
inline Location locate(const std::uint8_t* text, std::size_t length) {
    // Relocations, local stack offsets and camera member offsets are wildcarded.
    // No fixed RVA is used. The destination must independently match the
    // projection routine's instruction contract.
    const std::uint8_t pattern[] = {
        0x4c,0x8b,0x0d,0,0,0,0, 0x4c,0x8d,0x45,0,
        0x49,0x81,0xc1,0,0,0,0, 0x48,0x8d,0x8e,0,0,0,0,
        0xe8,0,0,0,0, 0xf3,0x0f,0x11,0x47,0};
    const char mask[] = "xxx????xxx?xxx????xxx????x????xxxx?";
    static_assert(sizeof(mask)-1 == sizeof(pattern));
    const std::uint8_t head[] = {0x48,0x83,0xec,0,0xf3,0x0f,0x10,0x62,0x04,
                               0xf3,0x0f,0x10,0x69,0x08,0x0f,0x28,0xd4};
    Location result;
    if (length < sizeof(pattern)) return result;
    for (std::size_t i = 0; i <= length-sizeof(pattern); ++i) {
        if (text[i] != pattern[0]) continue;
        bool same = true;
        for (std::size_t j = 1; j < sizeof(pattern); ++j)
            if (mask[j] == 'x' && text[i+j] != pattern[j]) { same = false; break; }
        if (!same) continue;
        std::int32_t disp;
        std::memcpy(&disp, text+i+26, sizeof(disp));
        std::int64_t target = std::int64_t(i)+30+disp;
        if (target < 0 || std::uint64_t(target)+sizeof(head) > length) continue;
        for (std::size_t j = 0; j < sizeof(head); ++j)
            if (j != 3 && text[std::size_t(target)+j] != head[j]) { same = false; break; }
        if (!same || text[std::size_t(target)+3] < 0x20) continue;
        ++result.matches;
        result.call = i+25;
        result.projection = std::size_t(target);
    }
    return result;
}
}
