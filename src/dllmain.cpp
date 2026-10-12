#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shellapi.h>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <regex>
#include <string>
#include <vector>
#include "globe_math.hpp"
#include "globe_pick.hpp"
#include "json.hpp"
#include "locator.hpp"

namespace {
HMODULE g_self;
HANDLE g_timer;
volatile LONG g_status = 0; // 0..5 legacy; 6 committed/degraded; 7 protection failure before commit
gmf::Settings g_settings;
using Project = float (*)(const float*, const gmf::Vec3*, gmf::Vec2*, const int*);
Project g_original = nullptr;
using Pick = gmf::Vec3* (*)(void*,gmf::Vec3*,const gmf::Vec2*,char);
using Ray = void (*)(void*,const int*,float,gmf::Vec3*,gmf::Vec3*);
using NativeHeight = float (*)(void*,float,float,char);
Pick g_original_pick=nullptr;
Ray g_ray=nullptr;
NativeHeight g_height=nullptr;
void** g_terrain=nullptr;
void** g_active=nullptr;
unsigned g_map_camera=0,g_primary_camera=0,g_matrix_offset=0;
HANDLE g_log = INVALID_HANDLE_VALUE;

std::wstring module_path(HMODULE m) {
    std::vector<wchar_t> b(32768);
    DWORD n = GetModuleFileNameW(m, b.data(), DWORD(b.size()));
    return n && n < b.size() ? std::wstring(b.data(), n) : std::wstring();
}
std::wstring parent(const std::wstring& p) {
    auto n = p.find_last_of(L"\\/"); return n == p.npos ? L"." : p.substr(0, n);
}
std::wstring join(const std::wstring& p, const std::wstring& q) {
    return p + L"\\" + q;
}
bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool regular(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
std::wstring full_path(const std::wstring& p) {
    std::vector<wchar_t> b(32768);
    DWORD n = GetFullPathNameW(p.c_str(), DWORD(b.size()), b.data(), nullptr);
    return n && n < b.size() ? std::wstring(b.data(), n) : std::wstring();
}
bool absolute(const std::wstring& p) {
    return p.size() > 1 && (p[1] == L':' || p[0] == L'\\' || p[0] == L'/');
}
std::wstring wide(const std::string& p) {
    if (p.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p.data(), int(p.size()), nullptr, 0);
    if (!n) return {};
    std::wstring s(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p.data(), int(p.size()), s.data(), n);
    return s;
}
void log(const char* format, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char b[1024]; va_list args; va_start(args, format);
    int n = vsnprintf(b, sizeof(b)-3, format, args); va_end(args);
    if (n < 0) return;
    n = std::min(n, int(sizeof(b)-3)); b[n++]='\r'; b[n++]='\n';
    DWORD wrote; WriteFile(g_log, b, DWORD(n), &wrote, nullptr);
}
bool read(const std::wstring& p, std::string& s, DWORD limit = 1024*1024) {
    s.clear(); HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size; bool ok = GetFileSizeEx(f, &size) && size.QuadPart >= 0 && size.QuadPart <= limit;
    if (ok) {
        s.resize(std::size_t(size.QuadPart)); DWORD got = 0;
        ok = ReadFile(f, s.data(), DWORD(s.size()), &got, nullptr) && got == s.size();
    }
    CloseHandle(f);
    if (!ok) s.clear();
    if (s.compare(0, 3, "\xef\xbb\xbf") == 0) s.erase(0, 3);
    return ok;
}
std::string compact(const std::string& s) {
    std::string out;
    for (std::size_t i=0; i<s.size();) {
        if (s[i]=='/' && i+1<s.size() && s[i+1]=='/') {
            i=s.find('\n', i+2); if (i==s.npos) break;
        } else if (s[i]=='/' && i+1<s.size() && s[i+1]=='*') {
            auto end=s.find("*/", i+2); if (end==s.npos) return {}; i=end+2;
        } else if(s[i]=='"') {
            out+=s[i++];bool closed=false;
            while(i<s.size()) {
                char c=s[i++];out+=c;
                if(c=='\\'&&i<s.size())out+=s[i++];
                else if(c=='"'){closed=true;break;}
            }
            if(!closed)return {};
        } else {
            unsigned char c=s[i++];
            if (c!=' ' && c!='\t' && c!='\r' && c!='\n') out+=char(c);
        }
    }
    return out;
}
bool shader_contract(const std::string& s) {
    std::string c=compact(s);
    auto begin=c.find("staticconstfloatGLOBE_PI"), end=c.find("float3GlobeOcean(");
    if (begin==c.npos || end==c.npos || end<=begin) return false;
    std::uint64_t hash=14695981039346656037ull;
    for (std::size_t i=begin; i<end; ++i) hash=(hash ^ std::uint8_t(c[i]))*1099511628211ull;
    // Globe Mod 0.2.0 transform, ignoring whitespace/comments. This is a math
    // contract, not a game-version pin; changed math requires a reviewed port.
    return hash==0x546da19246da7752ull;
}
bool terrain_shader_contract(const std::string& s) {
    auto c=compact(s);if(c.empty())return false;
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned char ch:c)hash=(hash^ch)*1099511628211ull;
    // Includes, actual vertex code, preprocessor branches and effect bindings
    // are all covered. Comment-only references or an unused helper cannot
    // enable hooks. Pixel-code edits need revalidation of this contract too.
    return hash==0x13c6d288c889e587ull;
}
std::wstring user_directory(const std::wstring& game, const std::wstring& ini) {
    wchar_t custom[32768]{};
    GetPrivateProfileStringW(L"fix", L"user_data_dir", L"", custom, 32768, ini.c_str());
    int count=0; wchar_t** args=CommandLineToArgvW(GetCommandLineW(), &count);
    std::wstring result;
    if (args) {
        for (int i=1;i<count;++i) {
            std::wstring a=args[i];
            if (a.compare(0,9,L"-userdir=")==0) result=a.substr(9);
            else if (a==L"-userdir" && i+1<count) result=args[++i];
            // Direct command-line mod lists need their own VFS resolver.
            if (a==L"-mod" || a.compare(0,5,L"-mod=")==0) { LocalFree(args); return {}; }
        }
        LocalFree(args);
    }else return {};
    if (*custom) return full_path(custom);
    if (!result.empty()) return full_path(absolute(result)?result:join(game,result));
    std::string userdir;
    if (regular(join(game,L"userdir.txt"))) {
        if (!read(join(game,L"userdir.txt"),userdir,32768)) return {};
        auto last=userdir.find_last_not_of(" \r\n\t");
        if (last==userdir.npos) return {};
        userdir.erase(last+1); auto first=userdir.find_first_not_of(" \r\n\t"); userdir.erase(0,first);
        result=wide(userdir);
        return result.empty()?result:full_path(absolute(result)?result:join(game,result));
    }
    PWSTR docs=nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&docs))) return {};
    result=join(docs,L"Paradox Interactive\\Hearts of Iron IV"); CoTaskMemFree(docs);
    return result;
}
struct Mod { std::wstring root; std::vector<std::wstring> replace; };
bool descriptor(const std::string& s, const std::wstring& user, Mod& m) {
    // Anchor at the start of lines: commented-out paths do not count.
    const std::regex field(R"rx((?:^|\n)[ \t]*(path|archive|replace_path)[ \t]*=[ \t]*"((?:\\.|[^"\\])*)")rx");
    bool found=false;
    for (std::sregex_iterator i(s.begin(),s.end(),field),end;i!=end;++i) {
        std::string value=(*i)[2]; std::string decoded;
        for(std::size_t k=0;k<value.size();++k) {
            if(value[k]=='\\' && k+1<value.size() && (value[k+1]=='\\'||value[k+1]=='"')) ++k;
            decoded+=value[k];
        }
        std::wstring w=wide(decoded); if(w.empty()) return false;
        if((*i)[1]=="archive") return false; // Compressed mods require an archive-aware resolver.
        if((*i)[1]=="path") {
            if(found) return false;
            found=true; m.root=full_path(absolute(w)?w:join(user,w));
        } else {
            for(auto& c:w) {if(c==L'\\')c=L'/';if(c>=L'A'&&c<=L'Z')c+=32;}
            while(!w.empty()&&w.back()==L'/')w.pop_back();
            m.replace.push_back(w);
        }
    }
    return found && exists(m.root);
}
std::wstring effective(const std::wstring& game, const std::vector<Mod>& mods, const std::wstring& rel) {
    std::wstring result=regular(join(game,rel))?join(game,rel):L"";
    std::wstring key=rel;
    for(auto& c:key) {if(c==L'\\')c=L'/';if(c>=L'A'&&c<=L'Z')c+=32;}
    for(const auto& mod:mods) {
        for(const auto& replace:mod.replace)
            if(key==replace || (key.size()>replace.size() && key.compare(0,replace.size(),replace)==0 && key[replace.size()]==L'/')) result.clear();
        auto p=join(mod.root,rel); if(regular(p))result=p;
    }
    return result;
}
bool settings(const std::wstring& game, const std::wstring& ini) {
    std::wstring user=user_directory(game,ini); std::string dlc;
    std::vector<std::string> enabled;
    if(user.empty() || !read(join(user,L"dlc_load.json"),dlc) || !gmf::Json(dlc).enabled(enabled)) {
        log("Refused: active mod list is unavailable or malformed (user_data_dir must match the game)."); return false;
    }
    std::vector<Mod> mods;
    for(const auto& name:enabled) {
        std::wstring w=wide(name); if(w.empty())return false;
        std::string text; Mod mod;
        if(!read(absolute(w)?w:join(user,w),text) || !descriptor(text,user,mod)) {
            log("Refused: an enabled mod descriptor cannot be resolved as a loose folder."); return false;
        }
        mods.push_back(std::move(mod));
    }
    std::wstring globe=effective(game,mods,L"gfx/FX/globe.fxh");
    if(globe.empty()) {InterlockedExchange(&g_status,1);log("Inactive: the active mod stack has no globe.fxh.");return false;}
    std::string source;
    if(!read(globe,source,65536)||!shader_contract(source)) {
        log("Refused: globe.fxh does not match the supported Globe Mod 0.2.0 transform.");return false;
    }
    std::string map_shader;
    if(!read(effective(game,mods,L"gfx/FX/pdxmap.shader"),map_shader) ||!terrain_shader_contract(map_shader)) {
        log("Refused: the active terrain shader does not match the verified Globe Mod 0.2.0 pipeline.");return false;
    }
    std::string constants;
    if(!read(effective(game,mods,L"gfx/FX/constants.fxh"),constants))return false;
    std::smatch match; std::string c=compact(constants);
    if(!std::regex_search(c,match,std::regex(R"(staticconstfloatWATER_HEIGHT=([0-9]+(?:\.[0-9]+)?)f?;)"))) {
        log("Refused: WATER_HEIGHT could not be read.");return false;
    }
    g_settings.water=std::stof(match[1]);
    if(!std::isfinite(g_settings.water)||g_settings.water<0||g_settings.water>25.5f)return false;
    auto bmp=effective(game,mods,L"map/provinces.bmp");
    HANDLE f=CreateFileW(bmp.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    unsigned char header[54]{}; DWORD got=0;
    bool ok=f!=INVALID_HANDLE_VALUE && ReadFile(f,header,sizeof(header),&got,nullptr) && got==sizeof(header);
    if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);
    std::int32_t width=0,height=0;std::uint32_t dib=0;
    std::memcpy(&dib,header+14,4);std::memcpy(&width,header+18,4);std::memcpy(&height,header+22,4);
    std::int64_t h=height<0?-std::int64_t(height):height;
    if(!ok || header[0]!='B'||header[1]!='M'||dib<40||width<128||width>100000||h<128||h>100000||
       double(width)/h<0.25||double(width)/h>16) {log("Refused: map bitmap dimensions are unsupported.");return false;}
    g_settings.width=float(width);g_settings.height=float(h);
    log("Active Globe Mod transform verified; map %.0f x %.0f; water %.3f.",g_settings.width,g_settings.height,g_settings.water);
    return true;
}

// The game still owns interpolation, grouping, alignment, integer rounding and
// GUI hit boxes. Only this call's temporary world anchor is changed.
float project(const float* matrix,const gmf::Vec3* point,gmf::Vec2* out,const int* viewport) {
    thread_local gmf::ViewCache cache;
    const auto& view=cache.get(matrix,g_settings);
    if(!view.valid || view.blend==0)return g_original(matrix,point,out,viewport);
    const auto warped=gmf::warp(*point,view,g_settings);
    if(!warped.visible) {*out={-1000000.0f,-1000000.0f};return 1.0f;}
    float w=matrix[3]*warped.position.x+matrix[7]*warped.position.y+matrix[11]*warped.position.z+matrix[15];
    if(!(w>0.00001f)) {*out={-1000000.0f,-1000000.0f};return 1.0f;}
    float depth=g_original(matrix,&warped.position,out,viewport);
    // Prevent non-finite/overflowing values from reaching integer GUI rounding.
    if(!std::isfinite(out->x)||!std::isfinite(out->y)||!std::isfinite(depth)||
       std::abs(out->x)>500000||std::abs(out->y)>500000) {*out={-1000000.0f,-1000000.0f};return 1.0f;}
    return depth;
}
float native_height(void* terrain,float x,float z) {return g_height(terrain,x,z,0);}
gmf::Vec3* pick(void* map,gmf::Vec3* out,const gmf::Vec2* screen,char primary_only) {
    if(!*g_active)return g_original_pick(map,out,screen,primary_only);
    auto container=*reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(map)+g_map_camera);
    if(!container)return g_original_pick(map,out,screen,primary_only);
    thread_local gmf::ViewCache cache;
    const auto& view=cache.get(reinterpret_cast<const float*>(container+g_matrix_offset),g_settings);
    if(!view.valid||view.blend==0)return g_original_pick(map,out,screen,primary_only);
    gmf::Vec2 mouse;std::memcpy(&mouse,screen,sizeof(mouse));
    *out={-32,0,-32}; // Native province bounds checks translate this to ID zero.
    if(!std::isfinite(mouse.x)||!std::isfinite(mouse.y)||std::abs(mouse.x)>1000000||std::abs(mouse.y)>1000000)return out;
    void* terrain=*g_terrain;if(!terrain)return out;
    int pixel[2]={int(mouse.x),int(mouse.y)};
    gmf::Vec3 ray{},eye{};
    g_ray(container+g_primary_camera,pixel,0,&ray,&eye);
    if(!gmf::finite(eye)||std::abs(eye.x-view.eye.x)>.1f||std::abs(eye.y-view.eye.y)>.1f||std::abs(eye.z-view.eye.z)>.1f)return out;
    gmf::Vec3 result;
    if(gmf::pick(eye,ray,view,g_settings,native_height,terrain,result))*out=result;
    return out;
}
bool text_section(HMODULE module,std::uint8_t*& text,std::size_t& size) {
    auto base=reinterpret_cast<std::uint8_t*>(module);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||dos->e_lfanew>4096)return false;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
    auto sec=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        if(std::memcmp(sec[i].Name,".text\0\0\0",8)==0 && (sec[i].Characteristics&IMAGE_SCN_MEM_EXECUTE)) {
            if(sec[i].VirtualAddress>=nt->OptionalHeader.SizeOfImage||sec[i].Misc.VirtualSize>nt->OptionalHeader.SizeOfImage-sec[i].VirtualAddress)return false;
            text=base+sec[i].VirtualAddress;size=sec[i].Misc.VirtualSize;return true;
        }
    return false;
}
void* near_page(std::uint8_t* call) {
    SYSTEM_INFO info;GetSystemInfo(&info);
    std::uintptr_t origin=reinterpret_cast<std::uintptr_t>(call)&~(std::uintptr_t(info.dwAllocationGranularity)-1);
    std::uintptr_t min=reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
    std::uintptr_t max=reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);
    for(std::uintptr_t d=info.dwAllocationGranularity;d<0x7fff0000;d+=info.dwAllocationGranularity) {
        for(int side=0;side<2;++side) {
            if((side==0&&origin<min+d)||(side==1&&origin>max-d))continue;
            std::uintptr_t a=side==0?origin-d:origin+d;MEMORY_BASIC_INFORMATION mb{};
            if(VirtualQuery(reinterpret_cast<void*>(a),&mb,sizeof(mb))!=sizeof(mb)||mb.State!=MEM_FREE)continue;
            if(void* p=VirtualAlloc(reinterpret_cast<void*>(a),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))return p;
        }
    }
    return nullptr;
}
struct Patch {std::uint8_t* address;std::size_t size;std::uint8_t expected[7]{},replacement[7]{};DWORD protection=0;};
enum class PatchResult {retryable,installed,degraded,protection_failure};
bool restore_protection(Patch* patches,std::size_t total) {
    for(int attempt=0;attempt<3;++attempt) {
        bool ok=true;
        for(std::size_t i=total;i>0;--i) {
            auto& p=patches[i-1];DWORD ignored=0;
            if(!VirtualProtect(p.address,p.size,p.protection,&ignored))ok=false;
            MEMORY_BASIC_INFORMATION mb{};
            if(VirtualQuery(p.address,&mb,sizeof(mb))!=sizeof(mb)||mb.Protect!=p.protection)ok=false;
        }
        if(ok)return true;
    }
    return false;
}
PatchResult patch_all(Patch* patches,std::size_t total) {
    if(!total||total>2)return PatchResult::retryable;
    // Collect/open all handles before suspending anyone: no CRT allocation or
    // logging is done while another thread could own a heap/stdio lock.
    HANDLE threads[4096]{};std::size_t count=0;bool ok=true;
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot==INVALID_HANDLE_VALUE)return PatchResult::retryable;
    THREADENTRY32 e{};e.dwSize=sizeof(e);
    if(!Thread32First(snapshot,&e))ok=false;
    else do {
        if(e.th32OwnerProcessID==GetCurrentProcessId() && e.th32ThreadID!=GetCurrentThreadId()) {
            if(count==4096) {ok=false;break;}
            HANDLE h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_LIMITED_INFORMATION,FALSE,e.th32ThreadID);
            if(!h) {ok=false;break;}
            threads[count++]=h;
        }
    }while(Thread32Next(snapshot,&e));
    CloseHandle(snapshot);
    std::size_t suspended=0;
    if(ok)for(;suspended<count;++suspended) {
        if(SuspendThread(threads[suspended])==DWORD(-1)) {ok=false;break;}
        CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(threads[suspended],&context)) {++suspended;ok=false;break;}
        auto ip=context.Rip;
        for(std::size_t j=0;j<total;++j) {
            auto start=reinterpret_cast<DWORD64>(patches[j].address);
            if(ip>=start&&ip<start+patches[j].size)ok=false;
        }
        if(!ok) {++suspended;break;}
    }
    for(std::size_t i=0;ok&&i<total;++i)
        if(std::memcmp(patches[i].address,patches[i].expected,patches[i].size)!=0)ok=false;
    PatchResult result=PatchResult::retryable;
    // Snapshot every original protection before opening either range. Shared
    // pages must not record the first range's temporary RWX as their baseline.
    for(std::size_t i=0;ok&&i<total;++i) {
        auto& p=patches[i];MEMORY_BASIC_INFORMATION mb{};
        if(VirtualQuery(p.address,&mb,sizeof(mb))!=sizeof(mb)||mb.State!=MEM_COMMIT||
           p.address+p.size>static_cast<std::uint8_t*>(mb.BaseAddress)+mb.RegionSize)ok=false;
        else p.protection=mb.Protect;
    }
    std::size_t writable=0;
    if(ok) {
        for(;writable<total;++writable) {
            DWORD ignored=0;auto& p=patches[writable];
            if(!VirtualProtect(p.address,p.size,PAGE_EXECUTE_READWRITE,&ignored))break;
        }
        if(writable==total) {
            // Commit both ranges while threads are stopped. Once committed,
            // protection failure never triggers a partial byte rollback.
            for(std::size_t i=0;i<total;++i)
                std::memcpy(patches[i].address,patches[i].replacement,patches[i].size);
            bool flushed=true;
            for(std::size_t i=0;i<total;++i) {
                auto& p=patches[i];bool done=false;
                for(int attempt=0;attempt<3&&!done;++attempt)
                    done=FlushInstructionCache(GetCurrentProcess(),p.address,p.size)!=FALSE;
                flushed=flushed&&done;
            }
            bool restored=restore_protection(patches,total);
            for(std::size_t i=0;i<total;++i)
                if(std::memcmp(patches[i].address,patches[i].replacement,patches[i].size)!=0)restored=false;
            result=restored&&flushed?PatchResult::installed:PatchResult::degraded;
        }else if(!restore_protection(patches,writable))result=PatchResult::protection_failure;
    }
    for(std::size_t i=0;i<suspended;++i)ResumeThread(threads[i]);
    for(std::size_t i=0;i<count;++i)CloseHandle(threads[i]);
    return result;
}
void initialize() {
    auto dll=module_path(g_self), dir=parent(dll), exe=module_path(nullptr), game=parent(exe);
    g_log=CreateFileW(join(dir,L"Globe Map Fix.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,0,nullptr);
    log("Globe Map Fix 1.1.1 - CPU icons, hemisphere occlusion and inverse map picking");
    std::wstring name=exe.substr(exe.find_last_of(L"\\/")+1);
    if(_wcsicmp(name.c_str(),L"hoi4.exe")!=0) {log("Inactive: host is not hoi4.exe.");InterlockedExchange(&g_status,1);return;}
    auto ini=join(dir,L"Globe Map Fix.ini");
    if(GetPrivateProfileIntW(L"fix",L"enabled",1,ini.c_str())==0) {log("Disabled by INI.");InterlockedExchange(&g_status,5);return;}
    if(!settings(game,ini)) {if(g_status==0)InterlockedExchange(&g_status,2);return;}
    std::uint8_t* text=nullptr;std::size_t length=0;
    if(!text_section(GetModuleHandleW(nullptr),text,length)) {log("Refused: host PE .text cannot be validated.");InterlockedExchange(&g_status,2);return;}
    auto location=gmf::locate(text,length);
    if(location.matches!=1) {log("Refused: projection call candidates = %d (exactly one required).",location.matches);InterlockedExchange(&g_status,2);return;}
    auto call=text+location.call;g_original=reinterpret_cast<Project>(text+location.projection);
    auto picking=gmf::locate_pick(text,length);
    if(picking.matches!=1||picking.height_matches!=1||location.matrix_offset<64||location.matrix_offset>4096||
       unsigned(location.matrix_offset)<=picking.primary_camera) {
        log("Refused: picking contracts/camera offsets are unsupported (pick=%d height=%d).",picking.matches,picking.height_matches);
        InterlockedExchange(&g_status,2);return;
    }
    auto pick_entry=text+picking.entry;
    auto base=reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    std::intptr_t cells[2]={reinterpret_cast<std::intptr_t>(text)+picking.terrain_cell,
                           reinterpret_cast<std::intptr_t>(text)+picking.active_cell};
    for(auto cell:cells) {
        MEMORY_BASIC_INFORMATION mb{};
        if(cell<reinterpret_cast<std::intptr_t>(base)||cell+8>reinterpret_cast<std::intptr_t>(base)+nt->OptionalHeader.SizeOfImage||
           VirtualQuery(reinterpret_cast<void*>(cell),&mb,sizeof(mb))!=sizeof(mb)||mb.State!=MEM_COMMIT||
           (mb.Protect&(PAGE_NOACCESS|PAGE_GUARD))||cell+8>reinterpret_cast<std::intptr_t>(mb.BaseAddress)+std::intptr_t(mb.RegionSize)) {
            log("Refused: picker globals are outside the readable host image.");InterlockedExchange(&g_status,2);return;
        }
    }
    g_terrain=reinterpret_cast<void**>(cells[0]);g_active=reinterpret_cast<void**>(cells[1]);g_map_camera=picking.map_camera;
    g_primary_camera=picking.primary_camera;g_matrix_offset=unsigned(location.matrix_offset);
    g_ray=reinterpret_cast<Ray>(text+picking.ray);g_height=reinterpret_cast<NativeHeight>(text+picking.height);
    std::uint8_t original[5];std::memcpy(original,call,5);
    std::int32_t original_disp;std::memcpy(&original_disp,original+1,4);
    if(original[0]!=0xe8 || call+5+original_disp!=text+location.projection) {
        log("Refused: the call changed during signature verification.");InterlockedExchange(&g_status,2);return;
    }
    log("Located call RVA 0x%llx; original projection RVA 0x%llx.",
        static_cast<unsigned long long>(call-base),static_cast<unsigned long long>(text+location.projection-base));
    log("Located pick RVA 0x%llx, ray RVA 0x%llx, height RVA 0x%llx; decoded camera offsets 0x%x/0x%x/0x%x.",
        static_cast<unsigned long long>(pick_entry-base),static_cast<unsigned long long>(text+picking.ray-base),
        static_cast<unsigned long long>(text+picking.height-base),g_map_camera,g_primary_camera,g_matrix_offset);
    if(GetPrivateProfileIntW(L"fix",L"probe",0,ini.c_str())!=0 || regular(join(dir,L"diplo_action_hook_probe_only.txt"))) {
        log("Probe only: no memory modification.");InterlockedExchange(&g_status,3);return;
    }
    void* page=near_page(call),*pick_page=near_page(pick_entry);
    if(!page||!pick_page) {
        if(page)VirtualFree(page,0,MEM_RELEASE);
        if(pick_page)VirtualFree(pick_page,0,MEM_RELEASE);
        log("Refused: no executable relay page within rel32 range.");InterlockedExchange(&g_status,2);return;
    }
    auto relay=static_cast<std::uint8_t*>(page);
    const std::uint8_t jump[6]={0xff,0x25,0,0,0,0};std::memcpy(relay,jump,6);
    auto destination=reinterpret_cast<std::uintptr_t>(&project);std::memcpy(relay+6,&destination,8);
    auto trampoline=static_cast<std::uint8_t*>(pick_page);
    std::memcpy(trampoline,pick_entry,7);std::memcpy(trampoline+7,jump,6);
    auto back=reinterpret_cast<std::uintptr_t>(pick_entry+7);std::memcpy(trampoline+13,&back,8);
    auto pick_relay=trampoline+32;std::memcpy(pick_relay,jump,6);
    destination=reinterpret_cast<std::uintptr_t>(&pick);std::memcpy(pick_relay+6,&destination,8);
    g_original_pick=reinterpret_cast<Pick>(trampoline);
    DWORD old=0;
    if(!VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old)||!VirtualProtect(pick_page,4096,PAGE_EXECUTE_READ,&old)) {
        VirtualFree(page,0,MEM_RELEASE);VirtualFree(pick_page,0,MEM_RELEASE);InterlockedExchange(&g_status,2);return;
    }
    FlushInstructionCache(GetCurrentProcess(),relay,14);
    FlushInstructionCache(GetCurrentProcess(),trampoline,46);
    auto distance=reinterpret_cast<std::intptr_t>(relay)-reinterpret_cast<std::intptr_t>(call+5);
    if(distance<std::numeric_limits<std::int32_t>::min()||distance>std::numeric_limits<std::int32_t>::max()) {
        VirtualFree(page,0,MEM_RELEASE);VirtualFree(pick_page,0,MEM_RELEASE);InterlockedExchange(&g_status,2);return;
    }
    std::uint8_t replacement[5]={0xe8,0,0,0,0};
    auto disp=static_cast<std::int32_t>(distance);std::memcpy(replacement+1,&disp,4);
    Patch patches[2]={{call,5,{},{},0},{pick_entry,7,{},{},0}};
    std::memcpy(patches[0].expected,original,5);std::memcpy(patches[0].replacement,replacement,5);
    std::memcpy(patches[1].expected,pick_entry,7);
    patches[1].replacement[0]=0xe9;patches[1].replacement[5]=0x90;patches[1].replacement[6]=0x90;
    distance=reinterpret_cast<std::intptr_t>(pick_relay)-reinterpret_cast<std::intptr_t>(pick_entry+5);
    if(distance<std::numeric_limits<std::int32_t>::min()||distance>std::numeric_limits<std::int32_t>::max()) {
        VirtualFree(page,0,MEM_RELEASE);VirtualFree(pick_page,0,MEM_RELEASE);InterlockedExchange(&g_status,2);return;
    }
    disp=static_cast<std::int32_t>(distance);std::memcpy(patches[1].replacement+1,&disp,4);
    MemoryBarrier();PatchResult result=PatchResult::retryable;
    for(int attempt=0;attempt<8&&result==PatchResult::retryable;++attempt) {
        result=patch_all(patches,2);if(result==PatchResult::retryable)Sleep(10);
    }
    if(result==PatchResult::retryable) {
        log("Refused: threads/code bytes could not be safely quiesced; neither hook was written.");
        InterlockedExchange(&g_status,2);return;
    }
    if(result==PatchResult::protection_failure) {
        log("Protection failure before commit: neither hook was written; host page protection could not be restored.");
        InterlockedExchange(&g_status,7);return;
    }
    if(result==PatchResult::degraded) {
        log("Installed degraded: BOTH hooks committed; host protection or instruction-cache API failed after retries. Restart required.");
        InterlockedExchange(&g_status,6);return;
    }
    log("Installed: 5-byte projection CALL + 7-byte world-picker entry; both relays RX; flat paths unchanged.");
    log("Back-side icons and their GUI anchors are off-screen; curved picking inverts GlobeWarp with live terrain height.");
    InterlockedExchange(&g_status,4);
}
void CALLBACK start(void*,BOOLEAN) {
    // A timer callback executes after DllMain, outside the loader lock.
    // Named mutex also serializes accidentally installed duplicate copies.
    wchar_t name[96];swprintf(name,96,L"Local\\GlobeMapFix.initialize.%lu",GetCurrentProcessId());
    HANDLE mutex=CreateMutexW(nullptr,FALSE,name);
    if(mutex && WaitForSingleObject(mutex,10000)==WAIT_OBJECT_0) {
        try {initialize();}catch(...) {log("Refused: initialization raised a C++ exception.");InterlockedExchange(&g_status,2);}
        ReleaseMutex(mutex);
    }else InterlockedExchange(&g_status,2);
    if(mutex)CloseHandle(mutex);
    if(g_log!=INVALID_HANDLE_VALUE) {CloseHandle(g_log);g_log=INVALID_HANDLE_VALUE;}
}
}
extern "C" __declspec(dllexport) LONG WINAPI GlobeMapFix_GetStatus() {
    return InterlockedCompareExchange(&g_status,0,0);
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        g_self=instance;
        // The timer and relay may outlive LoadLibrary's caller. Pin this module
        // before scheduling either; disable/uninstall takes effect on restart.
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(instance),&pinned)) return FALSE;
        if(!CreateTimerQueueTimer(&g_timer,nullptr,start,nullptr,100,0,WT_EXECUTEONLYONCE))InterlockedExchange(&g_status,2);
    }
    return TRUE;
}
