// Included inside namespace gmf, after chart/surface helpers.
// Conservative interval search of the near-side height shell. A bilinear
// patch is bounded by its corners; no later root is accepted while an earlier
// interval remains unresolved. Scratch samples live for one query only.
struct HeightSamples {
    struct Entry { int x=-1,z=-1; float h=0; };
    Entry entries[256];
    const Settings& s; Height height; void* context; bool valid=true;
    float sample(int x,int z) {
        x%=int(s.width);if(x<0)x+=int(s.width);
        z=std::clamp(z,0,int(s.height)-1);
        unsigned slot=(unsigned(x)*73856093u^unsigned(z)*19349663u)&255u;
        auto& e=entries[slot];
        if(e.x!=x||e.z!=z) {
            e={x,z,height(context,float(x),float(z))};
            if(!std::isfinite(e.h)||e.h<0||e.h>25.501f)valid=false;
        }
        return e.h;
    }
    double at(double x,double z) {
        x-=std::floor(x/s.width)*s.width;
        z=std::clamp(z,0.0,double(s.height-1));
        int ix=int(std::floor(x)),iz=int(std::floor(z));
        double fx=x-ix,fz=z-iz;
        double a=sample(ix,iz),b=sample(ix+1,iz),c=sample(ix,iz+1),d=sample(ix+1,iz+1);
        return std::max(double(s.water),(a+(b-a)*fx)*(1-fz)+(c+(d-c)*fx)*fz);
    }
};
struct SphereTrace {
    struct Point { double t,r,longitude,target,row; };
    enum Result { empty,found,unresolved };
    D3 eye,ray,o;
    const View& v; const Settings& s;
    HeightSamples samples;
    double b,extremum=0; bool has_extremum=false;
    int budget=8192;
    Vec3& out;
    SphereTrace(D3 e,D3 d,const View& view,const Settings& settings,Height h,void* context,Vec3& result):
        eye(e),ray(d),o(e-D3{view.focus_x,settings.water-view.radius,view.focus_z}),v(view),s(settings),
        samples{{},settings,h,context,true},b(dot(o,d)),out(result) {
        double py=o.y*v.sin_focus+o.z*v.cos_focus;
        double dy=ray.y*v.sin_focus+ray.z*v.cos_focus;
        double denominator=dy*b-py;
        if(std::abs(denominator)>1e-12) {
            extremum=(py*b-dy*dot(o,o))/denominator;has_extremum=true;
        }
    }
    Point point(double t) const {
        D3 p=o+ray*t;double r=std::sqrt(dot(p,p));
        double py=p.y*v.sin_focus+p.z*v.cos_focus,pz=p.y*v.cos_focus-p.z*v.sin_focus;
        double target=(std::asin(std::clamp(py/r,-1.0,1.0))-v.focus_lat)/v.scale;
        return {t,r,std::atan2(p.x,pz),target,inverse_row(target,v,s)};
    }
    bool coordinate(const Point& p,double& x,double& z) const {
        if(std::abs(p.longitude)>3.14159265*v.scale+1e-7||
           p.target<v.chart_lower-1e-7||p.target>v.chart_upper+1e-7)return false;
        x=v.focus_x+(p.longitude/v.scale)*v.radius_full;
        z=p.row;return true;
    }
    bool finish(const Point& p) {
        double x,z;if(!coordinate(p,x,z)||z<0||z>=s.height)return false;
        double h=samples.at(x,z);
        if(!samples.valid||std::abs(p.r-v.radius+s.water-h)>2e-5)return false;
        auto warped=warp({float(x),float(h),float(z)},v,s);
        if(!warped.visible)return false;
        x-=std::floor(x/s.width)*s.width;
        out={float(x),float(h),float(z)};if(out.x>=s.width)out.x=0;
        return finite(out);
    }
    double slope_bound(const Point& a,const Point& c,double zlo,double zhi,double gx,double gz) const {
        double pz=o.y*v.cos_focus-o.z*v.sin_focus,dz=ray.y*v.cos_focus-ray.z*v.sin_focus;
        double py=o.y*v.sin_focus+o.z*v.cos_focus,dy=ray.y*v.sin_focus+ray.z*v.cos_focus;
        double norm=ray.x*ray.x+dz*dz;
        double closest=norm>1e-15?std::clamp(-(o.x*ray.x+pz*dz)/norm,a.t,c.t):a.t;
        double xx=o.x+ray.x*closest,zz=pz+dz*closest,plane=xx*xx+zz*zz;
        if(plane<1e-15)return INFINITY;
        double dx_bound=std::abs(ray.x*pz-o.x*dz)*v.radius_full/(v.scale*plane);
        double k=dy*dot(o,o)-py*b,l=dy*b-py;
        double phi_bound=std::max(std::abs(k+l*a.t),std::abs(k+l*c.t))/(c.r*c.r*std::sqrt(plane));
        double pmax=.8*std::max(std::abs(zlo-(672.0/2048)*s.height),std::abs(zhi-(672.0/2048)*s.height))/v.radius_full;
        double row_derivative=((1-v.geo)+v.geo/std::cosh(pmax))/v.radius_full;
        double dz_bound=phi_bound/(v.scale*row_derivative);
        return gx*dx_bound+gz*dz_bound;
    }
    Result monotone_root(Point a,Point c,double fa,double fc) {
        if(fa<-1e-8||fc>1e-8)return empty;
        for(int i=0;i<40;++i) {
            double t=(a.t*fc-c.t*fa)/(fc-fa);
            if(!std::isfinite(t)||t<=a.t||t>=c.t)t=(a.t+c.t)*.5;
            Point p=point(t);double x,z;if(!coordinate(p,x,z))return unresolved;
            double f=p.r-v.radius+s.water-samples.at(x,z);
            if(!samples.valid)return unresolved;
            if(std::abs(f)<1e-8||c.t-a.t<1e-6)return finish(p)?found:unresolved;
            if(f>0){a=p;fa=f;}else{c=p;fc=f;}
        }
        return unresolved;
    }
    Result search(const Point& a,const Point& c,int depth=0) {
        if(--budget<0||depth>48)return unresolved;
        double lon_lo=std::min(a.longitude,c.longitude),lon_hi=std::max(a.longitude,c.longitude);
        bool seam=lon_hi-lon_lo>3.14159265;
        double lower=std::min(a.target,c.target),upper=std::max(a.target,c.target);
        double zlo=std::min(a.row,c.row),zhi=std::max(a.row,c.row);
        // Latitude along a line has at most one extremum. Longitude is
        // monotone unless crossing atan2's branch cut, which is subdivided.
        if(has_extremum&&extremum>a.t&&extremum<c.t) {
            auto p=point(extremum);lower=std::min(lower,p.target);upper=std::max(upper,p.target);
            zlo=std::min(zlo,p.row);zhi=std::max(zhi,p.row);
        }
        if(!seam&&(lon_hi<-3.14159265*v.scale||lon_lo>3.14159265*v.scale))return empty;
        if(upper<v.chart_lower||lower>v.chart_upper)return empty;
        double xlo=v.focus_x+std::max(lon_lo,-3.14159265*v.scale)/v.scale*v.radius_full;
        double xhi=v.focus_x+std::min(lon_hi,3.14159265*v.scale)/v.scale*v.radius_full;
        double tile=std::floor(xlo/s.width)*s.width;xlo-=tile;xhi-=tile;
        // Raw corner bounds over a small rectangle; trim them to the actual
        // rectangle once it lies within a single bilinear patch.
        int ix=int(std::floor(xlo)),ex=int(std::floor(xhi))+1;
        int iz=int(std::floor(zlo)),ez=int(std::floor(zhi))+1;
        double max_height=25.501,min_height=s.water,gx=25.501,gz=25.501;
        if(!seam&&std::int64_t(ex-ix+1)*(ez-iz+1)<=32) {
            max_height=s.water;min_height=25.501;
            if(ex==ix+1&&ez==iz+1) {
                for(double x:{xlo,xhi})for(double z:{zlo,zhi}) {
                    double h=samples.at(x,z);max_height=std::max(max_height,h);min_height=std::min(min_height,h);
                }
                gx=std::max(std::abs(samples.sample(ix+1,iz)-samples.sample(ix,iz)),
                            std::abs(samples.sample(ix+1,iz+1)-samples.sample(ix,iz+1)));
                gz=std::max(std::abs(samples.sample(ix,iz+1)-samples.sample(ix,iz)),
                            std::abs(samples.sample(ix+1,iz+1)-samples.sample(ix+1,iz)));
            }else {
                gx=0;gz=0;
                for(int x=ix;x<=ex;++x)for(int z=iz;z<=ez;++z) {
                    double h=std::max(double(s.water),double(samples.sample(x,z)));
                    max_height=std::max(max_height,h);min_height=std::min(min_height,h);
                    if(x<ex)gx=std::max(gx,double(std::abs(samples.sample(x+1,z)-samples.sample(x,z))));
                    if(z<ez)gz=std::max(gz,double(std::abs(samples.sample(x,z+1)-samples.sample(x,z))));
                }
            }
            if(!samples.valid)return unresolved;
        }
        double low_alt=c.r-v.radius+s.water,high_alt=a.r-v.radius+s.water;
        if(low_alt>max_height+1e-8||high_alt<min_height-1e-8)return empty;
        double x,z;
        if(coordinate(a,x,z)) {
            double f=high_alt-samples.at(x,z);
            if(!samples.valid)return unresolved;
            if(std::abs(f)<1e-8&&finish(a))return found;
        }
        if(!seam&&lower>=v.chart_lower&&upper<=v.chart_upper&&
           lon_lo>=-3.14159265*v.scale&&lon_hi<=3.14159265*v.scale) {
            if(max_height-min_height<1e-10) {
                double radius=v.radius+max_height-s.water,disc=b*b-dot(o,o)+radius*radius;
                if(disc>=0) {
                    double t=-b-std::sqrt(disc);
                    if(t>=a.t-1e-8&&t<=c.t+1e-8&&finish(point(t)))return found;
                }
                return empty;
            }
            double inward=-(b+c.t)/c.r;
            if(inward>0&&slope_bound(a,c,zlo,zhi,gx,gz)<inward*.999) {
                double xa,za,xc,zc;
                if(coordinate(a,xa,za)&&coordinate(c,xc,zc)) {
                    double fa=high_alt-samples.at(xa,za),fc=low_alt-samples.at(xc,zc);
                    if(!samples.valid)return unresolved;
                    return monotone_root(a,c,fa,fc);
                }
            }
        }
        if(c.t-a.t<1e-6) {
            if(finish(a)||finish(point((a.t+c.t)*.5))||finish(c))return found;
            return empty;
        }
        Point mid=point((a.t+c.t)*.5);
        auto first=search(a,mid,depth+1);
        return first==empty?search(mid,c,depth+1):first;
    }
    bool run() {
        if(!v.chart_valid)return false;
        double outer=v.radius+25.5-s.water;
        double disc=b*b-dot(o,o)+outer*outer;
        if(disc<0)return false;
        double begin=-b-std::sqrt(disc),end=-b;
        double sea=b*b-dot(o,o)+double(v.radius)*v.radius;
        if(sea>=0)end=-b-std::sqrt(sea);
        if(begin<=0||end<begin-1e-8)return false;
        if(std::abs(end-begin)<1e-8)return finish(point(begin));
        return search(point(begin),point(end))==found;
    }
};
inline bool sphere_pick(D3 eye,D3 ray,const View& v,const Settings& s,Height height,void* context,Vec3& out) {
    SphereTrace trace(eye,ray,v,s,height,context,out);return trace.run();
}
