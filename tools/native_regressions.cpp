#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <vector>
#include <cstdio>
int protect_calls=0,flush_calls=0,fail_after=0;
bool fail_flush=false;
std::vector<int> failures;
BOOL test_protect(LPVOID p,SIZE_T n,DWORD flags,PDWORD old) {
    ++protect_calls;
    if(fail_after&&protect_calls>=fail_after)return FALSE;
    for(int call:failures)if(call==protect_calls)return FALSE;
    return VirtualProtect(p,n,flags,old);
}
BOOL test_flush(HANDLE process,LPCVOID p,SIZE_T n) {
    ++flush_calls;if(fail_flush)return FALSE;
    return FlushInstructionCache(process,p,n);
}
#define VirtualProtect test_protect
#define FlushInstructionCache test_flush
#include "../src/dllmain.cpp"
#undef VirtualProtect
#undef FlushInstructionCache

int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 20;
    int checks=0,failed=0;
    auto check=[&](bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL %s\n",why);}};
    std::string shader;
    check(read(argv[1],shader)&&terrain_shader_contract(shader),"installed terrain pipeline accepted");
    check(!terrain_shader_contract("// \"globe.fxh\"\n// GlobeWarp(\nCode [[ float3 Flat(float3 p){return p;} ]]"),"comment-only false enable rejected");
    auto changed=shader;auto at=changed.find("GlobeWarp( VertexOut.prepos )");
    check(at!=changed.npos,"real vertex transform fixture exists");
    if(at!=changed.npos) {
        changed.replace(at,std::strlen("GlobeWarp( VertexOut.prepos )"),"VertexOut.prepos");
        changed+="\n// GlobeWarp( VertexOut.prepos )\n";
        check(!terrain_shader_contract(changed),"flat vertex plus old comment rejected");
        changed+="\nCode [[ float3 Unused(float3 p){return GlobeWarp(p);} ]]\n";
        check(!terrain_shader_contract(changed),"unused helper cannot enable pipeline");
        changed=shader;changed.insert(at,"/* comment before real call */ ");
        check(terrain_shader_contract(changed+"\n// extra comment\n"),"whitespace and comments preserve contract");
        changed=shader;changed.insert(at,"\n#if 0\n");
        check(!terrain_shader_contract(changed),"conditional-code changes rejected");
    }
    check(compact("A \"a // b\" B")=="A\"a // b\"B","string contents survive normalization");
    check(compact("/* unterminated").empty(),"unterminated comment rejected");
    // Real transactions on review-process memory only. No test page is called.
    auto exercise=[&](bool shared,std::vector<int> faults,int persistent,bool flush,PatchResult wanted) {
        auto a=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!a){check(false,"test allocation");return;}
        auto b=a+(shared?64:4096);
        std::memset(a,0x90,7);std::memset(b,0x90,7);DWORD ignored;
        VirtualProtect(a,8192,PAGE_EXECUTE_READ,&ignored);
        Patch p[2]={{a,5,{},{},0},{b,7,{},{},0}};
        std::memset(p[0].expected,0x90,5);std::memset(p[1].expected,0x90,7);
        std::memset(p[0].replacement,0xcc,5);std::memset(p[1].replacement,0xcc,7);
        failures=std::move(faults);fail_after=persistent;fail_flush=flush;protect_calls=0;flush_calls=0;
        auto result=patch_all(p,2);
        check(result==wanted,"transaction reports correct recovery state");
        bool committed=result==PatchResult::installed||result==PatchResult::degraded;
        check(a[0]==(committed?0xcc:0x90)&&b[0]==a[0],"both byte ranges always agree");
        if(result==PatchResult::installed||result==PatchResult::retryable) {
            MEMORY_BASIC_INFORMATION ma{},mb{};VirtualQuery(a,&ma,sizeof(ma));VirtualQuery(b,&mb,sizeof(mb));
            check(ma.Protect==PAGE_EXECUTE_READ&&mb.Protect==PAGE_EXECUTE_READ,"healthy transaction restores RX including shared page");
        }
        std::printf("patch shared=%d state=%d bytes=%02x/%02x protect_calls=%d flush_calls=%d\n",shared,int(result),a[0],b[0],protect_calls,flush_calls);
        failures.clear();fail_after=0;fail_flush=false;
        VirtualFree(a,0,MEM_RELEASE);
    };
    for(bool shared:{false,true}) {
        exercise(shared,{},0,false,PatchResult::installed);
        exercise(shared,{1},0,false,PatchResult::retryable);
        exercise(shared,{2,3},0,false,PatchResult::retryable);
        exercise(shared,{3,5},0,false,PatchResult::installed); // original review failure
        exercise(shared,{3,4,5,6,7,8},0,false,PatchResult::degraded);
        exercise(shared,{},3,false,PatchResult::degraded);
        exercise(shared,{2},3,false,PatchResult::protection_failure);
        exercise(shared,{},0,true,PatchResult::degraded);
    }
    std::printf("Native regressions checks=%d failures=%d\n",checks,failed);
    return failed?1:0;
}
