#include "test_support.hpp"
#include "../src/json.hpp"
#include "../src/locator.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

int main(int argc,char** argv) {
    int checks=0,failures=0;
    auto check=[&](bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<why<<'\n';}};
    std::vector<std::string> mods;
    check(gmf::Json(R"({"enabled_mods":["mod/a.mod","mod/\u4e2d\u6587.mod"],"other":{"x":[true,-1.4e+2,null]}})").enabled(mods)&&mods.size()==2,"JSON escaped UTF8/unknown fields");
    check(gmf::Json(R"({"enabled_mods":[]})").enabled(mods)&&mods.empty(),"empty active mod list");
    check(!gmf::Json("{\"enabled_mods\":[],\"other\":\"\xc0\xaf\"}").enabled(mods),"overlong UTF8 rejected");
    check(!gmf::Json("{\"enabled_mods\":[\"\xed\xa0\x80\"]}").enabled(mods),"UTF8 surrogate rejected");
    check(gmf::Json(R"({"enabled_mods":["mod/\uD83D\uDE00.mod"]})").enabled(mods),"Unicode surrogate pair accepted");
    for(auto malformed:{R"({"enabled_mods":["x",]})",R"({"enabled_mods":[],"enabled_mods":[]})",R"({"enabled_mods":[]}garbage)",R"({"enabled_mods":["\uD800"]})",R"({"enabled_mods":["\u0000"]})",R"({"enabled_mods":[],"bad":01})",R"({"enabled_mods":[],"bad":1.})"})
        check(!gmf::Json(malformed).enabled(mods),"malformed JSON rejected");
    gmf::Settings settings;std::mt19937 rng(928451);std::uniform_real_distribution<float> lon(-12000,12000),z(0,2048),height(50,2400),angle(-0.8f,0.8f);
    for(int i=0;i<5000;++i) {
        gmf::Vec3 eye{lon(rng),height(rng),z(rng)},forward{angle(rng),-1,angle(rng)};
        auto matrix=perspective(eye,forward);gmf::Vec3 recovered,dir;
        bool ok=gmf::camera(matrix.data(),recovered,dir);
        float error=std::max({std::abs(eye.x-recovered.x),std::abs(eye.y-recovered.y),std::abs(eye.z-recovered.z)});
        check(ok&&error<0.005f,"perspective eye recovery");
        auto view=gmf::prepare(matrix.data(),settings);
        gmf::Vec3 p{lon(rng),9.5f,z(rng)};
        auto warped=gmf::warp(p,view,settings);
        check(gmf::finite(warped.position),"finite globe transition");
        if(eye.y<999.9f)check(std::memcmp(&p,&warped.position,sizeof(p))==0,"flat coordinates bitwise unchanged");
        if(view.blend>0) {
            gmf::Vec3 center{view.focus_x,settings.water,view.focus_z};auto c=gmf::warp(center,view,settings);
            check(std::abs(c.position.x-center.x)<0.02f&&std::abs(c.position.y-center.y)<0.02f&&std::abs(c.position.z-center.z)<0.02f,"focus sea-level anchor conserved");
            auto shifted=p;shifted.x+=settings.width;auto b=gmf::warp(shifted,view,settings);
            // Initial blend mixes an intentionally unwrapped flat coordinate.
            if(view.mix==1)check(std::abs(b.position.x-warped.position.x)<0.02f&&std::abs(b.position.y-warped.position.y)<0.02f&&std::abs(b.position.z-warped.position.z)<0.02f,"longitude periodic after initial blend");
        }
    }
    std::array<float,16> invalid{};gmf::Vec3 eye,dir;
    check(!gmf::camera(invalid.data(),eye,dir),"singular matrix rejected");
    invalid[0]=std::numeric_limits<float>::quiet_NaN();check(!gmf::camera(invalid.data(),eye,dir),"NaN matrix rejected");
    if(argc==2) {
        std::ifstream f(argv[1],std::ios::binary);std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(f)),{});
        auto location=gmf::locate(data.data(),data.size());
        check(location.matches==1,"installed exe has exactly one signature candidate");
        std::cout<<"Signature file offset: 0x"<<std::hex<<location.call<<"; projection file offset: 0x"<<location.projection<<std::dec<<'\n';
        if(location.matches==1) {
            auto copy=data;copy[location.call]=0x90;check(gmf::locate(copy.data(),copy.size()).matches==0,"modified call rejected");
            copy=data;copy[location.projection]=0x90;check(gmf::locate(copy.data(),copy.size()).matches==0,"unexpected target rejected");
            auto doubled=data;doubled.insert(doubled.end(),data.begin(),data.end());
            check(gmf::locate(doubled.data(),doubled.size()).matches==2,"ambiguous signatures detected");
            check(gmf::locate(data.data(),30).matches==0,"short range safe");
        }
    }
    std::cout<<"Unit checks: "<<checks<<"; failures: "<<failures<<'\n';return failures?1:0;
}
