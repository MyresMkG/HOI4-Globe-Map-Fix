#include "test_support.hpp"
#include "../src/globe_pick.hpp"
#include <cstdio>
#include <random>

struct Ridge { double center,width; };
float ridge(void* p,float x,float) {
    auto& r=*static_cast<Ridge*>(p);return std::abs(x-r.center)<r.width?25.5f:9.5f;
}
float constant(void* p,float,float){return *static_cast<float*>(p);}

// Independent dense near-to-far oracle. Brackets by direct radial residual;
// it does not use SphereTrace bounds, local slope proofs or its search logic.
bool oracle(gmf::D3 eye,gmf::D3 ray,const gmf::View& v,const gmf::Settings& s,gmf::Height height,void* ctx,double& time) {
    auto o=eye-gmf::D3{v.focus_x,s.water-v.radius,v.focus_z};double b=gmf::dot(o,ray);
    double r=v.radius+25.5-s.water,disc=b*b-gmf::dot(o,o)+r*r;
    if(disc<0)return false;
    double begin=-b-std::sqrt(disc),end=-b;
    double sea=b*b-gmf::dot(o,o)+double(v.radius)*v.radius;
    if(sea>=0)end=-b-std::sqrt(sea);
    auto f=[&](double t) {
        auto p=o+ray*t;double length=std::sqrt(gmf::dot(p,p)),x,z;
        if(!gmf::chart(p*(1/length),v,s,x,z))return double(NAN);
        return length-v.radius+s.water-gmf::terrain(x,z,s,height,ctx);
    };
    double previous=f(begin),t0=begin;
    if(std::abs(previous)<1e-8){time=begin;return true;}
    for(int i=1;i<=20000;++i) {
        double t=begin+(end-begin)*i/20000,value=f(t);
        if(std::isfinite(previous)&&previous>=0&&value<=0) {
            double lo=t0,hi=t;
            for(int j=0;j<40;++j) {double m=(lo+hi)*.5;if(f(m)>0)lo=m;else hi=m;}
            time=(lo+hi)*.5;return true;
        }
        previous=value;t0=t;
    }
    return false;
}
int main() {
    int checks=0,failures=0;
    auto check=[&](bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}};
    gmf::Settings s;
    for(float water:{9.5f,25.5f})for(float h:{9.5f,25.5f}) {
        s.water=water;auto m=perspective({2800,2400,1000},{0,-1,.1f});auto v=gmf::prepare(m.data(),s);
        int targets=0,misses=0;
        for(int i=0;i<20000;++i) {
            double x=v.focus_x+double(s.width)*(.5*i/20000.0),z=v.focus_z,altitude=std::max(water,h);
            auto q=gmf::surface(x,z,altitude,v,s),d=q-gmf::d3(v.eye),ray=d*(1/std::sqrt(gmf::dot(d,d)));
            auto radial=q-gmf::D3{v.focus_x,s.water-v.radius,v.focus_z};
            double facing=-gmf::dot(radial,ray)/std::sqrt(gmf::dot(radial,radial));
            if(facing<.03||facing>.08)continue;
            ++targets;gmf::Vec3 hit{};bool ok=gmf::pick(v.eye,{float(ray.x),float(ray.y),float(ray.z)},v,s,constant,&h,hit);
            if(!ok)++misses;
            check(ok&&std::abs(hit.x-x)<.01&&std::abs(hit.z-z)<.01,"visible max-height/degenerate-shell roundtrip");
        }
        check(targets>300,"endpoint regression covers rim targets");
        std::printf("endpoint water=%.1f raw_height=%.1f targets=%d misses=%d\n",water,h,targets,misses);
    }
    s.water=9.5f;
    for(float cy:{1500.f,1700.f,2400.f})for(double width:{.6,1.5,5.0})for(int sign:{-1,1}) {
        auto m=perspective({2800,cy,1000},{0,-1,.1f});auto v=gmf::prepare(m.data(),s);
        double x=v.focus_x,z=v.focus_z;gmf::D3 ray{};
        for(int i=0;i<20000;++i) {
            x=v.focus_x+sign*double(s.width)*(.5*i/20000.0);
            auto q=gmf::surface(x,z,9.5,v,s),d=q-gmf::d3(v.eye);ray=d*(1/std::sqrt(gmf::dot(d,d)));
            auto n=q-gmf::D3{v.focus_x,s.water-v.radius,v.focus_z};double facing=-gmf::dot(n,ray)/std::sqrt(gmf::dot(n,n));
            if(facing>.25&&facing<.26)break;
        }
        double rx=0,rz=0;check(gmf::sphere_seed(gmf::d3(v.eye),ray,17.5,v,s,rx,rz),"foreground ridge seed");
        Ridge r{rx,width};gmf::Vec3 dir{float(ray.x),float(ray.y),float(ray.z)},hit{};
        ray=gmf::d3(dir);ray=ray*(1/std::sqrt(gmf::dot(ray,ray)));
        double wanted=0;bool reference=oracle(gmf::d3(v.eye),ray,v,s,ridge,&r,wanted);
        bool ok=gmf::pick(v.eye,dir,v,s,ridge,&r,hit);
        auto q=gmf::surface(hit.x,hit.z,hit.y,v,s),d=q-gmf::d3(v.eye);double t=gmf::dot(d,ray);
        auto residual=d-ray*t;
        check(reference&&ok&&std::abs(t-wanted)<.002&&std::sqrt(gmf::dot(residual,residual))<.001,"nearest narrow-ridge hit matches dense oracle");
    }
    // Check the optimized inverse against independent 48-step bisection,
    // including custom map ratios and rows very close to their edges.
    std::mt19937 rng(111);
    auto jagged=[](void*,float x,float z)->float {
        std::uint32_t h=std::uint32_t(x)*73856093u^std::uint32_t(z)*19349663u;
        h^=h>>13;h*=1274126177u;return float(h&255u)*.1f;
    };
    int jagged_hits=0;
    for(float cy:{1500.f,1700.f,2400.f})for(int i=0;i<60;++i) {
        auto m=perspective({2800,cy,1000},{.12f,-1,.15f});auto v=gmf::prepare(m.data(),s);
        double x=v.focus_x+std::uniform_real_distribution<double>(-1500,1500)(rng);
        double z=std::uniform_real_distribution<double>(100,1900)(rng);
        double h=gmf::terrain(x,z,s,jagged,nullptr);
        auto q=gmf::surface(x,z,h,v,s),d=q-gmf::d3(v.eye),ray=d*(1/std::sqrt(gmf::dot(d,d)));
        if(!gmf::warp({float(x),float(h),float(z)},v,s).visible)continue;
        gmf::Vec3 dir{float(ray.x),float(ray.y),float(ray.z)},hit{};
        ray=gmf::d3(dir);ray=ray*(1/std::sqrt(gmf::dot(ray,ray)));
        double wanted=0;bool reference=oracle(gmf::d3(v.eye),ray,v,s,jagged,nullptr,wanted);
        bool ok=gmf::pick(v.eye,dir,v,s,jagged,nullptr,hit);
        q=gmf::surface(hit.x,hit.z,hit.y,v,s);d=q-gmf::d3(v.eye);double t=gmf::dot(d,ray);
        check(reference&&ok&&std::abs(t-wanted)<.002,"jagged bilinear terrain earliest hit matches dense oracle");
        ++jagged_hits;
    }
    check(jagged_hits>140,"jagged oracle covers all three camera heights");
    std::printf("Jagged earliest-hit oracle comparisons=%d\n",jagged_hits);
    for(float width:{512.f,5632.f,10000.f,100000.f})for(float cy:{1401.f,1500.f,1700.f,1799.f}) {
        s.width=width;
        auto m=perspective({width*.5f,cy,1000},{0,-1,.1f});auto v=gmf::prepare(m.data(),s);
        if(!v.chart_valid)continue;
        for(int i=0;i<250;++i) {
            double target=v.chart_lower+(v.chart_upper-v.chart_lower)*std::generate_canonical<double,32>(rng);
            double lo=0,hi=s.height;
            for(int j=0;j<48;++j) {
                double mid=(lo+hi)*.5,value=(1-v.geo)*(mid-v.focus_z)/v.radius_full+v.geo*(gmf::lat(mid,s,v)-v.row_lat);
                if(value<target)lo=mid;else hi=mid;
            }
            check(std::abs(gmf::inverse_row(target,v,s)-(lo+hi)*.5)<1e-6,"safeguarded latitude inverse matches independent bisection");
        }
    }
    s={};auto m=perspective({2800,2400,1000},{0,-1,.1f});auto v=gmf::prepare(m.data(),s);
    auto q=gmf::surface(v.focus_x+100,v.focus_z+100,13,v,s),d=q-gmf::d3(v.eye),ray=d*(1/std::sqrt(gmf::dot(d,d)));
    gmf::Vec3 dir{float(ray.x),float(ray.y),float(ray.z)},hit{};float h=13;
    check(gmf::pick(v.eye,dir,v,s,constant,&h,hit)&&hit.y==13,"first live height sample");
    h=18;check(gmf::pick(v.eye,dir,v,s,constant,&h,hit)&&hit.y==18,"next pick observes changed terrain with same view");
    for(float invalid:{-1.f,25.6f,float(NAN)}) {
        h=invalid;check(!gmf::pick(v.eye,dir,v,s,constant,&h,hit),"invalid height produces miss");
    }
    std::printf("Pick regressions checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
