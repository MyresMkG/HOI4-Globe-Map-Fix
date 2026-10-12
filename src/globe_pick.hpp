#pragma once
#include "globe_math.hpp"
#include <cstdint>

namespace gmf {
using Height = float (*)(void*,float,float);
struct D3 { double x,y,z; };
inline D3 operator+(D3 a,D3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline D3 operator-(D3 a,D3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline D3 operator*(D3 a,double k){return {a.x*k,a.y*k,a.z*k};}
inline double dot(D3 a,D3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline D3 cross(D3 a,D3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline D3 d3(Vec3 a){return {a.x,a.y,a.z};}
inline double lat(double z,const Settings& s,const View& v) {
    return latitude_precise(z,s,v.radius_full);
}
// Bilinear interpolation of the game's own integer height samples. No image
// copy, disk read or persistent terrain cache: terrain edits remain observable.
inline double terrain(double x,double z,const Settings& s,Height height,void* context) {
    x-=std::floor(x/s.width)*s.width;
    z=std::clamp(z,0.0,double(s.height-1));
    double ix=std::floor(x),iz=std::floor(z),fx=x-ix,fz=z-iz;
    float nx=float(ix+1<s.width?ix+1:0);
    double a=height(context,float(ix),float(iz)),b=height(context,nx,float(iz));
    double c=height(context,float(ix),float(std::min(iz+1,double(s.height-1))));
    double d=height(context,nx,float(std::min(iz+1,double(s.height-1))));
    if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(c)||!std::isfinite(d)||
       std::min({a,b,c,d})<0||std::max({a,b,c,d})>25.501)return NAN;
    return std::max(double(s.water),(a+(b-a)*fx)*(1-fz)+(c+(d-c)*fx)*fz);
}
inline D3 surface(double x,double z,double altitude,const View& v,const Settings& s) {
    double lon=(x-v.focus_x)/v.radius_full;
    lon-=std::floor(lon/(2*3.14159265)+.5)*(2*3.14159265);
    lon*=v.scale;
    double dlat=(z-v.focus_z)/v.radius_full;
    if(v.geo>0)dlat+=(lat(z,s,v)-v.row_lat-dlat)*v.geo;
    double la=v.focus_lat+dlat*v.scale,sl=std::sin(la),cl=std::cos(la),pz=cl*std::cos(lon);
    D3 n{cl*std::sin(lon),sl*v.sin_focus+pz*v.cos_focus,sl*v.cos_focus-pz*v.sin_focus};
    n=n*(1/std::sqrt(dot(n,n)));
    D3 p{x,altitude,z},q=D3{v.focus_x,s.water-v.radius,v.focus_z}+n*(v.radius+altitude-s.water);
    return p+(q-p)*v.mix;
}
inline double inverse_row(double target,const View& v,const Settings& s) {
    target=std::clamp(target,v.chart_lower,v.chart_upper);
    if(v.geo==0)return std::clamp(v.focus_z+target*v.radius_full,0.0,double(s.height));
    if(v.geo==1) {
        double angle=(target+v.row_lat+.625*3.14159265)/2.5;
        return std::clamp((672.0/2048)*s.height+(v.radius_full/.8)*std::log(std::tan(angle)),0.0,double(s.height));
    }
    double lower=0,upper=s.height;
    double row=s.height*(target-v.chart_lower)/(v.chart_upper-v.chart_lower);
    // Safeguarded Newton: derivative is positive throughout the supported
    // chart. Keep a bracket and fall back to bisection on an unsafe step.
    for(int i=0;i<32;++i) {
        double p=.8*(row-(672.0/2048)*s.height)/v.radius_full;
        double e=std::exp(p);
        double value=(1-v.geo)*(row-v.focus_z)/v.radius_full+v.geo*(2.5*std::atan(e)-.625*3.14159265-v.row_lat);
        double error=value-target;
        if(std::abs(error)<1e-12)return row;
        if(error<0)lower=row;else upper=row;
        double derivative=((1-v.geo)+v.geo*2*e/(1+e*e))/v.radius_full;
        double next=row-error/derivative;
        row=next>lower&&next<upper?next:(lower+upper)*.5;
    }
    return row;
}
inline bool chart(D3 n,const View& v,const Settings& s,double& x,double& z,bool clamp_seed=false) {
    if(!v.chart_valid)return false;
    double py=n.y*v.sin_focus+n.z*v.cos_focus,pz=n.y*v.cos_focus-n.z*v.sin_focus;
    double longitude=std::atan2(n.x,pz)/v.scale;
    if(std::abs(longitude)>3.14159265+1e-5)return false;
    x=v.focus_x+longitude*v.radius_full;
    double target=(std::asin(std::clamp(py,-1.0,1.0))-v.focus_lat)/v.scale;
    // An elevated coast can enter the ray before the sea-level sphere enters
    // the chart. Seed at the closest edge; the final solve still enforces the
    // exact map bounds and residual instead of accepting this clamped seed.
    if(!clamp_seed&&(target<v.chart_lower-1e-7||target>v.chart_upper+1e-7))return false;
    z=inverse_row(target,v,s);
    return std::isfinite(x)&&std::isfinite(z);
}
inline bool sphere_seed(D3 eye,D3 ray,double altitude,const View& v,const Settings& s,double& x,double& z) {
    D3 center{v.focus_x,s.water-v.radius,v.focus_z},o=eye-center;
    double b=dot(o,ray),r=v.radius+altitude-s.water,disc=b*b-dot(o,o)+r*r;
    if(disc<0)return false;
    double t=-b-std::sqrt(disc);
    if(t<=0)return false;
    D3 n=(o+ray*t)*(1/r);
    return chart(n,v,s,x,z,true);
}
#include "sphere_trace.inl"
// Compatibility helper for the review fixture; all sphere angles now use the
// same conservative near-side traversal.
inline bool grazing(D3 eye,D3 ray,const View& v,const Settings& s,Height height,void* context,Vec3& out) {
    return sphere_pick(eye,ray,v,s,height,context,out);
}
// Returns an original, unwarped map coordinate. A miss never falls through to
// the planar picker. The bounded Newton solve also handles 0 < blend < .02,
// where the shader mixes a plane and a sphere instead of drawing a sphere.
inline bool pick(Vec3 origin,Vec3 direction,const View& v,const Settings& s,Height height,void* context,Vec3& out) {
    if(!v.valid||v.blend<=0||!finite(origin)||!finite(direction)||!height)return false;
    D3 eye=d3(origin),ray=d3(direction);double length=std::sqrt(dot(ray,ray));
    if(length<.5||length>2)return false;
    ray=ray*(1/length);
    if(v.mix==1)return sphere_pick(eye,ray,v,s,height,context,out);
    double x=0,z=0;
    {
        if(ray.y>=-1e-6)return false;
        double t=(s.water-eye.y)/ray.y;if(t<=0)return false;
        x=eye.x+ray.x*t;z=eye.z+ray.z*t;
        // Sea-plane parallax can start outside the coast although the actual
        // elevated terrain hit is inside. Only the seed is clamped.
        z=std::clamp(z,0.0,double(s.height));
        x=std::clamp(x,double(v.focus_x-s.width*.5f+.01f),double(v.focus_x+s.width*.5f-.3f));
    }
    D3 u=cross(ray,std::abs(ray.y)<.9?D3{0,1,0}:D3{1,0,0});u=u*(1/std::sqrt(dot(u,u)));
    D3 w=cross(ray,u);
    auto residual=[&](double xx,double zz,double& a,double& b,D3& q)->bool {
        if(zz<-2||zz>s.height+2||std::abs(xx-v.focus_x)>s.width*.5+.01)return false;
        double h=terrain(xx,zz,s,height,context);if(!std::isfinite(h))return false;
        q=surface(xx,zz,h,v,s);a=dot(q-eye,u);b=dot(q-eye,w);
        return std::isfinite(a)&&std::isfinite(b);
    };
    for(int i=0;i<20;++i) {
        double a,b;D3 q;if(!residual(x,z,a,b,q))return false;
        double error=a*a+b*b;
        if(error<1e-8) {
            double h=terrain(x,z,s,height,context);
            // The same facing test as CPU icons and GlobeFacing in the shader.
            auto result=warp({float(x),float(h),float(z)},v,s);
            if(z<0||z>=s.height||!result.visible||dot(q-eye,ray)<=0)return false;
            x-=std::floor(x/s.width)*s.width;
            out={float(x),float(h),float(z)};
            if(out.x>=s.width)out.x=0;
            return finite(out);
        }
        constexpr double step=.25;
        double ax,bx,az,bz;D3 unused;
        if(!residual(x+step,z,ax,bx,unused)||!residual(x,z+step,az,bz,unused))return false;
        double j00=(ax-a)/step,j10=(bx-b)/step,j01=(az-a)/step,j11=(bz-b)/step;
        double det=j00*j11-j01*j10;
        if(std::abs(det)<1e-8)return false;
        double dx=(j11*a-j01*b)/det,dz=(-j10*a+j00*b)/det;
        if(!std::isfinite(dx)||!std::isfinite(dz))return false;
        double factor=1;bool improved=false;
        for(int trial=0;trial<8;++trial,factor*=.5) {
            double aa,bb;D3 qq;
            if(residual(x-dx*factor,z-dz*factor,aa,bb,qq)&&aa*aa+bb*bb<error) {x-=dx*factor;z-=dz*factor;improved=true;break;}
        }
        if(!improved)return false;
    }
    return false;
}
}
