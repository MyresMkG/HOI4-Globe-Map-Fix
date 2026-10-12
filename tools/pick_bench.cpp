#include "test_support.hpp"
#include "../src/globe_pick.hpp"
#include <chrono>
#include <cstdio>

struct HeightContext {float altitude;unsigned long long calls=0;};
float height(void* p,float,float){auto& c=*static_cast<HeightContext*>(p);++c.calls;return c.altitude;}
int main() {
    gmf::Settings s;int failures=0;
    for(float cy:{1010.f,1050.f,1100.f,1500.f,1700.f,2400.f}) {
        auto m=perspective({2800,cy,1000},{.12f,-1,.15f});auto v=gmf::prepare(m.data(),s);
        auto q=gmf::surface(v.focus_x+100,v.focus_z+100,13,v,s),d=q-gmf::d3(v.eye);
        auto r=d*(1/std::sqrt(gmf::dot(d,d)));gmf::Vec3 direction{float(r.x),float(r.y),float(r.z)},out{};
        auto bench=[&](bool grazing) {
            HeightContext c{grazing?9.5f:13.f};int n=grazing?2000:20000,hits=0;
            auto start=std::chrono::steady_clock::now();
            for(int i=0;i<n;++i)hits+=gmf::pick(v.eye,direction,v,s,height,&c,out);
            double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/n;
            std::printf("bench %s cy=%.0f geo=%.6f mix=%.6f us/call=%.3f height_calls/call=%.1f hits=%d/%d\n",
                grazing?"grazing":"ordinary",cy,v.geo,v.mix,us,double(c.calls)/n,hits,n);
            if(hits!=n)++failures;
        };
        bench(false);
        if(v.mix!=1)continue;
        bool found=false;
        for(int i=0;i<20000;++i) {
            double x=v.focus_x+double(s.width)*(.5*i/20000.0);
            q=gmf::surface(x,v.focus_z,9.5,v,s);d=q-gmf::d3(v.eye);r=d*(1/std::sqrt(gmf::dot(d,d)));
            auto radial=q-gmf::D3{v.focus_x,s.water-v.radius,v.focus_z};
            double facing=-gmf::dot(radial,r)/std::sqrt(gmf::dot(radial,radial));
            if(facing>.05&&facing<.06){found=true;break;}
        }
        if(found){direction={float(r.x),float(r.y),float(r.z)};bench(true);}
    }
    return failures?1:0;
}
