#include "test_support.hpp"
#include "../src/json.hpp"
#include "../src/locator.hpp"
#include "../src/globe_pick.hpp"
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
    double max_pick_error=0;int roundtrips=0,occluded_by_relief=0;
    auto land=[](void* ctx,float x,float row)->float {
        return ctx?float(13+3*std::sin(x*.004)+2*std::cos(row*.006)):9.5f;
    };
    for(float altitude:{1001.f,1010.f,1030.f,1050.f,1070.f,1100.f,1300.f,1500.f,1700.f,1800.f,2400.f})
    for(int terrain_mode=0;terrain_mode<2;++terrain_mode)for(int i=0;i<350;++i) {
        auto matrix=perspective({2800,altitude,1000},{.12f,-1,.15f});auto v=gmf::prepare(matrix.data(),settings);
        float x=v.focus_x+std::uniform_real_distribution<float>(-2400,2400)(rng),row=z(rng);
        void* ctx=terrain_mode?static_cast<void*>(&checks):nullptr;
        double h=gmf::terrain(x,row,settings,land,ctx);
        auto warped=gmf::warp({x,float(h),row},v,settings);
        if(!warped.visible)continue;
        // Exclude numerical instability at the exact tangent from the success
        // requirement; it must still never return a back-hemisphere point.
        auto q=gmf::surface(x,row,h,v,settings),delta=q-gmf::d3(v.eye);
        auto ray=delta*(1/std::sqrt(gmf::dot(delta,delta)));gmf::Vec3 hit{};
        bool success=gmf::pick(v.eye,{float(ray.x),float(ray.y),float(ray.z)},v,settings,land,ctx,hit);
        double nx=x-std::floor(x/settings.width)*settings.width;
        double ex=std::abs(hit.x-nx);ex=std::min(ex,double(settings.width)-ex);
        double error=success?std::max(ex,std::abs(double(hit.z)-row)):1e6;
        bool closer_relief=false;
        if(success&&terrain_mode&&error>=.4) {
            auto qq=gmf::surface(hit.x,hit.z,hit.y,v,settings),dd=qq-gmf::d3(v.eye);
            auto orthogonal=dd-ray*gmf::dot(dd,ray);
            closer_relief=gmf::dot(orthogonal,orthogonal)<1e-6&&gmf::dot(dd,ray)<gmf::dot(delta,ray);
            if(closer_relief)++occluded_by_relief;
        }
        if(!closer_relief&&error>max_pick_error)max_pick_error=error;
        ++roundtrips;check(success&&(error<.4||closer_relief),"terrain inverse finds original point or nearer occluding relief");
    }
    std::cout<<"Inverse roundtrips="<<roundtrips<<" max unobscured map error="<<max_pick_error<<" nearer relief intersections="<<occluded_by_relief<<'\n';
    for(float cx:{-5640.f,-10.f,10.f,5620.f,5640.f,11270.f})
    for(float cy:{1000.1f,1010.f,1050.f,1070.f,1400.f,1700.f,2400.f}) {
        auto matrix=perspective({cx,cy,800},{.3f,-1,.2f});auto v=gmf::prepare(matrix.data(),settings);
        for(float dx:{-600.f,-10.f,10.f,600.f}) {
            double xx=v.focus_x+dx,zz=v.focus_z+100;
            auto q=gmf::surface(xx,zz,9.5,v,settings),delta=q-gmf::d3(v.eye);
            auto ray=delta*(1/std::sqrt(gmf::dot(delta,delta)));gmf::Vec3 hit{};
            bool ok=gmf::pick(v.eye,{float(ray.x),float(ray.y),float(ray.z)},v,settings,land,nullptr,hit);
            double wanted=xx-std::floor(xx/settings.width)*settings.width;
            check(ok&&std::abs(hit.x-wanted)<.15&&std::abs(hit.z-zz)<.15,"inverse seam/camera-wrap/initial-blend roundtrip");
        }
    }
    {
        auto matrix=perspective({2800,2400,1000},{0,-1,.1f});auto v=gmf::prepare(matrix.data(),settings);gmf::Vec3 hit;
        check(!gmf::pick(v.eye,{0,1,0},v,settings,land,nullptr,hit),"sky miss");
        check(!gmf::pick(v.eye,{NAN,-1,0},v,settings,land,nullptr,hit),"invalid ray miss");
        for(int i=0;i<1000;++i) {
            gmf::Vec3 p{v.focus_x+settings.width*.5f,9.5f,z(rng)};
            check(!gmf::warp(p,v,settings).visible,"back hemisphere occluded across latitude");
        }
    }
    if(argc==2) {
        std::ifstream f(argv[1],std::ios::binary);std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(f)),{});
        auto location=gmf::locate(data.data(),data.size());
        auto picking=gmf::locate_pick(data.data(),data.size());
        check(picking.matches==1&&picking.height_matches==1,"unique installed picker/ray/height contracts");
        check(picking.map_camera==32&&picking.primary_camera==16&&location.matrix_offset==144,"decoded camera offsets");
        std::cout<<"Pick offset: 0x"<<std::hex<<picking.entry<<"; ray: 0x"<<picking.ray<<"; height: 0x"<<picking.height<<std::dec<<'\n';
        check(location.matches==1,"installed exe has exactly one signature candidate");
        std::cout<<"Signature file offset: 0x"<<std::hex<<location.call<<"; projection file offset: 0x"<<location.projection<<std::dec<<'\n';
        if(location.matches==1) {
            auto broken=data;broken[picking.entry]=0x90;
            check(gmf::locate_pick(broken.data(),broken.size()).matches==0,"modified picker entry rejected");
            broken=data;broken[picking.ray]=0x90;
            check(gmf::locate_pick(broken.data(),broken.size()).matches==0,"unexpected ray target rejected");
            broken=data;broken[picking.height]=0x90;
            check(gmf::locate_pick(broken.data(),broken.size()).height_matches==0,"modified terrain accessor rejected");
            auto copy=data;copy[location.call]=0x90;check(gmf::locate(copy.data(),copy.size()).matches==0,"modified call rejected");
            copy=data;copy[location.projection]=0x90;check(gmf::locate(copy.data(),copy.size()).matches==0,"unexpected target rejected");
            auto doubled=data;doubled.insert(doubled.end(),data.begin(),data.end());
            check(gmf::locate(doubled.data(),doubled.size()).matches==2,"ambiguous signatures detected");
            check(gmf::locate(data.data(),30).matches==0,"short range safe");
        }
    }
    std::cout<<"Unit checks: "<<checks<<"; failures: "<<failures<<'\n';return failures?1:0;
}
