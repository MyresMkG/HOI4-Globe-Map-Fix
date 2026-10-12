#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gmf {
struct Location { std::size_t call = 0, projection = 0; int matches = 0; std::int32_t matrix_offset = 0; };
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
        std::memcpy(&result.matrix_offset,text+i+21,4);
    }
    return result;
}

inline bool pattern_at(const std::uint8_t* p,const std::uint8_t* bytes,const char* mask) {
    for(std::size_t i=0;mask[i];++i)if(mask[i]=='x'&&p[i]!=bytes[i])return false;
    return true;
}
struct PickLocation {
    std::size_t entry=0, ray=0, height=0;
    std::int64_t terrain_cell=0, active_cell=0;
    unsigned map_camera=0, primary_camera=0;
    int matches=0, height_matches=0;
};
inline PickLocation locate_pick(const std::uint8_t* t,std::size_t size) {
    PickLocation result;
    // Instruction contracts, not RVAs. The copied seven-byte entry contains
    // no branch, RIP-relative operand, stack adjustment or nonvolatile write.
    const std::uint8_t entry[]={0x4c,0x8b,0xdc,0x45,0x88,0x4b,0x20,0x49,0x89,0x53,0x10,0x49,0x89,0x4b,0x08,
        0x55,0x53,0x56,0x57,0x49,0x8d,0xab,0,0,0,0,0x48,0x81,0xec,0,0,0,0,
        0x48,0x83,0x3d,0,0,0,0,0,0x41,0x0f,0xb6,0xf1,0x48,0x8b,0xc2,0x0f,0x84,0,0,0,0,
        0x48,0x8b,0x79,0,0x48,0x85,0xff,0x0f,0x84,0,0,0,0,0x48,0x8b,0x05,0,0,0,0};
    const char emask[]="xxxxxxxxxxxxxxxxxxxxxx????xxx????xxx????xxxxxxxxxx????xxx?xxxxx????xxx????";
    static_assert(sizeof(emask)-1==sizeof(entry));
    const std::uint8_t primary[]={0x33,0xdb,0x48,0x8d,0x47,0,0x48,0x89,0x45,0,0x89,0x5c,0x24,0,0x40,0x84,0xf6};
    const char pmask[]="xxxxx?xxx?xxx?xxx";
    const std::uint8_t call[]={0x48,0x89,0x44,0x24,0x20,0xe8,0,0,0,0,0xc7,0x44,0x24,0,0x00,0x01,0x00,0x00};
    const char cmask[]="xxxxxx????xxx?xxxx";
    const std::uint8_t ray[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x57,0x48,0x83,0xec,0,
        0x0f,0x29,0x70,0,0x48,0x8b,0xda,0x48,0x8b,0xf9};
    const char rmask[]="xxxxxxxxxxxxxxx?xxx?xxxxxx";
    const std::uint8_t height[]={0xf3,0x0f,0x58,0x0d,0,0,0,0,0xf3,0x0f,0x58,0x15,0,0,0,0,
        0x4c,0x8b,0x81,0,0,0,0,0x33,0xc9,0xf3,0x0f,0x2c,0xd1,0xf3,0x44,0x0f,0x2c,0xd2};
    const char hmask[]="xxxx????xxxx????xxx????xxxxxxxxxxx";
    static_assert(sizeof(hmask)-1==sizeof(height));
    if(size<1024)return result;
    for(std::size_t i=0;i+sizeof(height)<=size;++i) {
        if(t[i]==height[0]&&pattern_at(t+i,height,hmask)) {++result.height_matches;result.height=i;}
        if(i+1024>size)continue;
        if(t[i]!=entry[0]||!pattern_at(t+i,entry,emask))continue;
        unsigned po=0;int pn=0,rn=0;std::size_t rt=0;
        for(std::size_t j=i+sizeof(entry);j+sizeof(ray)<i+1024;++j) {
            if(t[j]==primary[0]&&pattern_at(t+j,primary,pmask)) {po=t[j+5];++pn;}
            if(t[j]==call[0]&&pattern_at(t+j,call,cmask)) {
                std::int32_t d;std::memcpy(&d,t+j+6,4);std::int64_t target=std::int64_t(j)+10+d;
                if(target>=0&&std::uint64_t(target)+sizeof(ray)<=size&&pattern_at(t+target,ray,rmask)) {rt=std::size_t(target);++rn;}
            }
        }
        if(pn!=1||rn!=1||po<8||po>127||t[i+57]<8||t[i+57]>127)continue;
        ++result.matches;result.entry=i;result.ray=rt;result.map_camera=t[i+57];result.primary_camera=po;
        std::int32_t d;std::memcpy(&d,t+i+70,4);result.terrain_cell=std::int64_t(i)+74+d;
        std::memcpy(&d,t+i+36,4);result.active_cell=std::int64_t(i)+41+d;
    }
    return result;
}
}
