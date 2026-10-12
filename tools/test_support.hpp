#pragma once
#include "../src/globe_math.hpp"
#include <array>
#include <cmath>

inline std::array<float,16> perspective(gmf::Vec3 eye,gmf::Vec3 forward) {
    double norm=std::sqrt(double(forward.x)*forward.x+double(forward.y)*forward.y+double(forward.z)*forward.z);
    forward={float(forward.x/norm),float(forward.y/norm),float(forward.z/norm)};
    gmf::Vec3 right{forward.z,0,-forward.x};
    float r=std::sqrt(right.x*right.x+right.z*right.z);
    if(r<1e-6f)right={1,0,0};else right={right.x/r,0,right.z/r};
    gmf::Vec3 up{forward.y*right.z,forward.z*right.x-forward.x*right.z,-forward.y*right.x};
    std::array<float,16> m{};
    float q=100000.0f/99999.0f;
    const float ep[3]={eye.x,eye.y,eye.z},fp[3]={forward.x,forward.y,forward.z};
    const float rp[3]={right.x,right.y,right.z},upv[3]={up.x,up.y,up.z};
    for(int i=0;i<3;++i) {
        float e=ep[i],f=fp[i],x=rp[i],y=upv[i];
        m[4*i]=x*1.4f;m[4*i+1]=y*2.1f;m[4*i+2]=f*q;m[4*i+3]=f;
        m[12]-=x*1.4f*e;m[13]-=y*2.1f*e;m[14]-=f*q*e;m[15]-=f*e;
    }
    m[14]-=q;return m;
}
inline float project_reference(const float* m,const gmf::Vec3* p,gmf::Vec2* out,const int* viewport) {
    float w=m[3]*p->x+m[7]*p->y+m[11]*p->z+m[15];
    out->x=((m[0]*p->x+m[4]*p->y+m[8]*p->z+m[12])/w*0.5f+0.5f)*viewport[0];
    out->y=((m[1]*p->x+m[5]*p->y+m[9]*p->z+m[13])/w*-0.5f+0.5f)*viewport[1];
    return (m[2]*p->x+m[6]*p->y+m[10]*p->z+m[14])/w;
}
