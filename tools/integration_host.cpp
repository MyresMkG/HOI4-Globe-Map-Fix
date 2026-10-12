#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <dxgi.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include "test_support.hpp"

extern "C" {
    unsigned char g_viewport_storage[0x188]{};
    void* g_viewport_base=g_viewport_storage;
    float HostProject(const float* m,const gmf::Vec3* p,gmf::Vec2* out,const int* v) {return project_reference(m,p,out,v);}
    float FixtureProject(const float*,const gmf::Vec3*,gmf::Vec2*,const int*);
    float FixtureIcon(const float*,const gmf::Vec3*,gmf::Vec2*,const int*);
}
int wmain(int argc,wchar_t** argv) {
    if(argc<3)return 20;
    int expected=_wtoi(argv[2]);int viewport[2]={1920,1080};std::memcpy(g_viewport_storage+0x180,viewport,8);
    unsigned char before[128];auto fixture=reinterpret_cast<const unsigned char*>(&FixtureIcon);
    std::memcpy(before,fixture,sizeof(before));
    void* factory=nullptr;
    HRESULT hr=CreateDXGIFactory1(IID_IDXGIFactory1,&factory);
    if(SUCCEEDED(hr))static_cast<IDXGIFactory1*>(factory)->Release();
    std::atomic<bool> run{true};std::atomic<unsigned long long> calls{0};
    std::vector<std::thread> workers;
    auto flat=perspective({2800,800,1200},{0,-1,0.2f});gmf::Vec3 anchor{2800,9.5f,1300};
    for(int i=0;i<4;++i)workers.emplace_back([&]{while(run.load(std::memory_order_relaxed)) {
        gmf::Vec2 out;FixtureIcon(flat.data(),&anchor,&out,viewport);
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
    if(state!=4)return 0;
    int checks=0,failures=0;auto check=[&](bool ok,const char* why){++checks;if(!ok){++failures;printf("FAIL %s\n",why);}};
    gmf::Vec2 baseline,actual;float d0=FixtureProject(flat.data(),&anchor,&baseline,viewport);
    float d1=FixtureIcon(flat.data(),&anchor,&actual,viewport);
    check(std::memcmp(&baseline,&actual,sizeof(actual))==0&&d0==d1,"flat ABI/output unchanged");
    auto curved=perspective({2800,2000,1200},{0,-1,0.2f});gmf::Settings s;
    auto view=gmf::prepare(curved.data(),s);
    gmf::Vec3 point{view.focus_x+400,9.5f,view.focus_z+80};
    auto warped=gmf::warp(point,view,s);gmf::Vec2 wanted;
    float expected_depth=FixtureProject(curved.data(),&warped.position,&wanted,viewport);
    float depth=FixtureIcon(curved.data(),&point,&actual,viewport);
    check(actual.x==wanted.x&&actual.y==wanted.y&&depth==expected_depth,"curved ABI/output matches warped anchor");
    gmf::Vec3 back{view.focus_x+s.width/2,9.5f,view.focus_z};
    FixtureIcon(curved.data(),&back,&actual,viewport);
    check(actual.x==-1000000&&actual.y==-1000000,"back hemisphere off-screen");
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
    printf("Integration checks=%d failures=%d checksum=%.2f\n",checks,failures,double(sum));
    return failures?22:0;
}
