#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <dxgi.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include "test_support.hpp"
#include "../src/globe_pick.hpp"

extern "C" {
    unsigned char g_viewport_storage[0x188]{};
    void* g_viewport_base=g_viewport_storage;
    float HostProject(const float* m,const gmf::Vec3* p,gmf::Vec2* out,const int* v) {return project_reference(m,p,out,v);}
    float FixtureProject(const float*,const gmf::Vec3*,gmf::Vec2*,const int*);
    float FixtureIcon(const float*,const gmf::Vec3*,gmf::Vec2*,const int*);
    void FixtureRay(void*,const int*,float,gmf::Vec3*,gmf::Vec3*);
    gmf::Vec3* FixturePick(void*,gmf::Vec3*,const gmf::Vec2*,char);
    unsigned char g_fixture_terrain_data[0x200]{};
    void* g_fixture_terrain=g_fixture_terrain_data;
    void* g_fixture_active=g_fixture_terrain_data;
    std::atomic<int> native_picks{0};
    gmf::Vec3* HostPick(void*,gmf::Vec3* out,const gmf::Vec2*,char) {++native_picks;*out={123,9.5f,456};return out;}
    void HostRay(void* camera,const int* pixel,float,gmf::Vec3* direction,gmf::Vec3* eye) {
        auto m=reinterpret_cast<float*>(static_cast<unsigned char*>(camera)+0x80);
        gmf::Vec3 forward;gmf::camera(m,*eye,forward);
        double px=2.0*pixel[0]/1920-1,py=1-2.0*pixel[1]/1080;
        gmf::D3 a{m[0]-px*m[3],m[4]-px*m[7],m[8]-px*m[11]};
        gmf::D3 b{m[1]-py*m[3],m[5]-py*m[7],m[9]-py*m[11]};
        auto ray=gmf::cross(a,b);if(gmf::dot(ray,gmf::d3(forward))<0)ray=ray*(-1);
        ray=ray*(1/std::sqrt(gmf::dot(ray,ray)));*direction={float(ray.x),float(ray.y),float(ray.z)};
    }
    float HostHeight(void*,float,float,char) {return 9.5f;}
}
int wmain(int argc,wchar_t** argv) {
    if(argc<3)return 20;
    int expected=_wtoi(argv[2]);int viewport[2]={1920,1080};std::memcpy(g_viewport_storage+0x180,viewport,8);
    if(argc>3&&wcscmp(argv[3],L"break_ray")==0) {
        auto code=reinterpret_cast<unsigned char*>(&FixtureRay);DWORD old,ignored;
        if(!VirtualProtect(code,1,PAGE_EXECUTE_READWRITE,&old))return 25;
        *code=0x90;FlushInstructionCache(GetCurrentProcess(),code,1);VirtualProtect(code,1,old,&ignored);
    }
    unsigned char before[128];auto fixture=reinterpret_cast<const unsigned char*>(&FixtureIcon);
    std::memcpy(before,fixture,sizeof(before));
    unsigned char pick_before[7];auto pick_code=reinterpret_cast<const unsigned char*>(&FixturePick);
    std::memcpy(pick_before,pick_code,7);
    alignas(16) unsigned char map[0x30]{},container[0x220]{};
    void* container_ptr=container;std::memcpy(map+0x20,&container_ptr,sizeof(container_ptr));
    void* factory=nullptr;
    HRESULT hr=CreateDXGIFactory1(IID_IDXGIFactory1,&factory);
    if(SUCCEEDED(hr))static_cast<IDXGIFactory1*>(factory)->Release();
    std::atomic<bool> run{true};std::atomic<unsigned long long> calls{0};
    std::vector<std::thread> workers;
    auto flat=perspective({2800,800,1200},{0,-1,0.2f});gmf::Vec3 anchor{2800,9.5f,1300};
    std::memcpy(container+0x90,flat.data(),64);
    for(int i=0;i<4;++i)workers.emplace_back([&]{while(run.load(std::memory_order_relaxed)) {
        gmf::Vec2 out;FixtureIcon(flat.data(),&anchor,&out,viewport);
        gmf::Vec3 hit;gmf::Vec2 mouse{960,540};FixturePick(map,&hit,&mouse,0);
        if(!std::isfinite(out.x)||!std::isfinite(out.y))std::abort();
        calls.fetch_add(1,std::memory_order_relaxed);
    }});
    bool injector=wcscmp(argv[1],L"injector")==0;
    HMODULE dll=injector?nullptr:LoadLibraryW(argv[1]);
    using Status=LONG(WINAPI*)();Status status=nullptr;LONG state=0;
    auto until=GetTickCount64()+15000;
    do {
        if(!dll&&injector)dll=GetModuleHandleW(L"Globe Map Fix.dll");
        if(dll&&!status) {auto proc=GetProcAddress(dll,"GlobeMapFix_GetStatus");static_assert(sizeof(proc)==sizeof(status));std::memcpy(&status,&proc,sizeof(status));}
        if(status)state=status();
        if(state)break;
        MSG message;
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
        Sleep(5);
    }while(GetTickCount64()<until);
    run=false;for(auto& worker:workers)worker.join();
    printf("status=%ld expected=%d concurrent_calls=%llu injector=%d\n",state,expected,calls.load(),injector);
    if(state!=expected)return 21;
    int changed=0;for(std::size_t i=0;i<sizeof(before);++i)if(before[i]!=fixture[i])++changed;
    printf("Changed bytes in fixture code=%d\n",changed);
    if((state==4&&(changed<1||changed>4))||(state!=4&&changed!=0))return 23;
    int pick_changed=0;for(int i=0;i<7;++i)if(pick_before[i]!=pick_code[i])++pick_changed;
    if((state==4&&pick_changed<5)||(state!=4&&pick_changed))return 24;
    MEMORY_BASIC_INFORMATION icon_page{},pick_page{};
    VirtualQuery(fixture,&icon_page,sizeof(icon_page));VirtualQuery(pick_code,&pick_page,sizeof(pick_page));
    if(icon_page.Protect!=PAGE_EXECUTE_READ||pick_page.Protect!=PAGE_EXECUTE_READ)return 26;
    if(state!=4)return 0;
    int checks=0,failures=0;auto check=[&](bool ok,const char* why){++checks;if(!ok){++failures;printf("FAIL %s\n",why);}};
    gmf::Vec2 baseline,actual;float d0=FixtureProject(flat.data(),&anchor,&baseline,viewport);
    float d1=FixtureIcon(flat.data(),&anchor,&actual,viewport);
    check(std::memcmp(&baseline,&actual,sizeof(actual))==0&&d0==d1,"flat ABI/output unchanged");
    gmf::Vec3 hit;gmf::Vec2 mouse{960,540};int prior=native_picks.load();
    check(FixturePick(map,&hit,&mouse,1)==&hit&&hit.x==123&&hit.z==456&&native_picks==prior+1,"flat picker trampoline/ABI unchanged");
    auto curved=perspective({2800,2000,1200},{0,-1,0.2f});gmf::Settings s;
    auto view=gmf::prepare(curved.data(),s);
    std::memcpy(container+0x90,curved.data(),64);
    gmf::Vec3 point{view.focus_x+400,9.5f,view.focus_z+80};
    auto warped=gmf::warp(point,view,s);gmf::Vec2 wanted;
    float expected_depth=FixtureProject(curved.data(),&warped.position,&wanted,viewport);
    float depth=FixtureIcon(curved.data(),&point,&actual,viewport);
    check(actual.x==wanted.x&&actual.y==wanted.y&&depth==expected_depth,"curved ABI/output matches warped anchor");
    mouse=wanted;prior=native_picks.load();FixturePick(map,&hit,&mouse,0);
    check(hit.x>=0&&hit.z>=0&&native_picks==prior,"curved picker bypasses planar routine");
    auto picked=gmf::warp(hit,view,s);gmf::Vec2 screen_hit;
    FixtureProject(curved.data(),&picked.position,&screen_hit,viewport);
    check(std::abs(screen_hit.x-int(mouse.x))<.1&&std::abs(screen_hit.y-int(mouse.y))<.1,"injected picker forward round trip in pixels");
    g_fixture_active=nullptr;prior=native_picks.load();FixturePick(nullptr,&hit,&mouse,0);
    check(hit.x==123&&hit.z==456&&native_picks==prior+1,"inactive map uses original picker even with a curved matrix");
    g_fixture_active=g_fixture_terrain_data;
    gmf::Vec3 aliased{mouse.x,mouse.y,0};
    FixturePick(map,&aliased,reinterpret_cast<const gmf::Vec2*>(&aliased),0);
    check(aliased.x>=0&&aliased.z>=0,"input/output alias preserves original mouse coordinate");
    mouse={-10000,-10000};FixturePick(map,&hit,&mouse,0);
    check(hit.x<0&&hit.z<0,"sky pick gives invalid native province coordinate");
    mouse={NAN,540};FixturePick(map,&hit,&mouse,0);
    check(hit.x<0&&hit.z<0,"invalid mouse coordinate gives miss");
    gmf::Vec3 back{view.focus_x+s.width/2,9.5f,view.focus_z};
    FixtureIcon(curved.data(),&back,&actual,viewport);
    check(actual.x==-1000000&&actual.y==-1000000,"back hemisphere off-screen");
    check(actual.x+512<0&&actual.y+512<0,"hidden icon rectangle cannot overlap viewport hit region");
    point.z=-10;FixtureIcon(curved.data(),&point,&actual,viewport);
    check(actual.x==-1000000&&actual.y==-1000000,"out-of-map off-screen");
    point.x=std::numeric_limits<float>::quiet_NaN();FixtureIcon(curved.data(),&point,&actual,viewport);
    check(actual.x==-1000000&&actual.y==-1000000,"invalid anchor off-screen");
    if(argc>4&&wcscmp(argv[3],L"duplicate")==0) {
        HMODULE second=LoadLibraryW(argv[4]);Status second_status=nullptr;
        if(second) {auto proc=GetProcAddress(second,"GlobeMapFix_GetStatus");std::memcpy(&second_status,&proc,sizeof(proc));}
        auto deadline=GetTickCount64()+5000;LONG duplicate_state=0;
        while(second_status&&GetTickCount64()<deadline) {duplicate_state=second_status();if(duplicate_state)break;Sleep(5);}
        check(duplicate_state==2,"duplicate DLL refuses to overwrite existing hook");
        check(status()==4,"first copy remains installed");
    }
    // A caller releasing its LoadLibrary reference must not invalidate the
    // relay/callback. Production deliberately pins the DLL for process life.
    if(!injector) {FreeLibrary(dll);check(status()==4,"DLL remains pinned after FreeLibrary");}
    // Concurrent callbacks after installation exercise each thread's own cache.
    run=true;workers.clear();
    point={view.focus_x+400,9.5f,view.focus_z+80};
    for(int i=0;i<4;++i)workers.emplace_back([&,i]{auto matrix=i%2?flat:curved;gmf::Vec3 p=i%2?anchor:point;for(int j=0;j<20000;++j){gmf::Vec2 output;FixtureIcon(matrix.data(),&p,&output,viewport);if(!std::isfinite(output.x))std::abort();}});
    for(auto& worker:workers)worker.join();
    constexpr int iterations=400000;volatile float sum=0;
    auto bench=[&](auto fn,const auto& m,const auto& p) {
        auto start=std::chrono::steady_clock::now();
        for(int i=0;i<iterations;++i){gmf::Vec2 output;sum+=fn(m.data(),&p,&output,viewport);}
        return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/iterations;
    };
    double original_ns=bench(FixtureProject,curved,point),flat_ns=bench(FixtureIcon,flat,anchor),globe_ns=bench(FixtureIcon,curved,point);
    printf("bench ns/icon: original=%.1f flat_fix=%.1f globe_fix=%.1f; estimated extra/10000 globe icons=%.3f ms\n",original_ns,flat_ns,globe_ns,(globe_ns-original_ns)*10000/1e6);
    mouse=wanted;auto pick_start=std::chrono::steady_clock::now();
    for(int i=0;i<10000;++i){FixturePick(map,&hit,&mouse,0);sum+=hit.x;}
    double pick_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-pick_start).count()/10000;
    printf("bench curved inverse picking=%.3f us/call\n",pick_us);
    bool tangent_found=false;gmf::Vec2 tangent_screen{};
    for(int i=0;i<4000&&!tangent_found;++i) {
        auto p=gmf::Vec3{view.focus_x+s.width*(float(i)/4000-.5f),9.5f,view.focus_z};
        auto wp=gmf::warp(p,view,s);auto delta=gmf::d3(wp.position)-gmf::d3(view.eye);
        auto radial=gmf::d3(wp.position)-gmf::D3{view.focus_x,s.water-view.radius,view.focus_z};
        double facing=-gmf::dot(radial,delta)/std::sqrt(gmf::dot(radial,radial)*gmf::dot(delta,delta));
        if(wp.visible&&facing>.05&&facing<.07) {
            gmf::Vec2 candidate;FixtureProject(curved.data(),&wp.position,&candidate,viewport);
            int pixel[2]={int(candidate.x),int(candidate.y)};gmf::Vec3 direction,eye;
            HostRay(container+0x10,pixel,0,&direction,&eye);
            auto o=gmf::d3(eye)-gmf::D3{view.focus_x,s.water-view.radius,view.focus_z};
            double b=gmf::dot(o,gmf::d3(direction));
            double disc=b*b-gmf::dot(o,o)+double(view.radius)*view.radius;
            // Native integer pixel rounding can put a float anchor just beyond
            // the silhouette. Only require a hit for a pixel inside the sphere.
            if(disc>0&&std::sqrt(disc)/view.radius>.03) {tangent_screen=candidate;tangent_found=true;}
        }
    }
    check(tangent_found,"grazing benchmark fixture found");
    if(tangent_found) {
        pick_start=std::chrono::steady_clock::now();
        for(int i=0;i<1000;++i){FixturePick(map,&hit,&tangent_screen,0);sum+=hit.x;}
        double tangent_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-pick_start).count()/1000;
        check(hit.x>=0&&hit.z>=0,"grazing injected sea pick succeeds");
        printf("bench grazing inverse picking=%.3f us/call\n",tangent_us);
    }
    printf("Integration checks=%d failures=%d checksum=%.2f\n",checks,failures,double(sum));
    return failures?22:0;
}
