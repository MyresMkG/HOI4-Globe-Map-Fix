#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>

namespace gmf {
struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };
struct Settings { float width = 5632, height = 2048, water = 9.5f; };
inline bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline float smooth(float lo, float hi, float x) {
    float t = std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline float latitude(float row, const Settings& s, float radius) {
    float p = (std::clamp(row, 0.0f, s.height) - (672.0f / 2048.0f) * s.height) / radius;
    return 2.5f * std::atan(std::exp(0.8f * p)) - 0.625f * 3.14159265f;
}
inline double latitude_precise(double row, const Settings& s, double radius) {
    return 2.5*std::atan(std::exp(.8*(std::clamp(row,0.0,double(s.height))-(672.0/2048)*s.height)/radius))-.625*3.14159265;
}

// Clip X, Y and W are all zero at the eye of a perspective camera.
// Solving those three planes avoids reading any version-specific camera fields.
inline bool camera(const float* m, Vec3& eye, Vec3& direction) {
    for (int i = 0; i < 16; ++i) if (!std::isfinite(m[i])) return false;
    double a[3][4] = {{m[0], m[4], m[8], -m[12]},
                      {m[1], m[5], m[9], -m[13]},
                      {m[3], m[7], m[11], -m[15]}};
    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 3; ++r)
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) pivot = r;
        if (std::abs(a[pivot][col]) < 1e-10) return false;
        for (int k = col; k < 4; ++k) std::swap(a[col][k], a[pivot][k]);
        double div = a[col][col];
        for (int k = col; k < 4; ++k) a[col][k] /= div;
        for (int r = 0; r < 3; ++r) if (r != col) {
            double v = a[r][col];
            for (int k = col; k < 4; ++k) a[r][k] -= v * a[col][k];
        }
    }
    eye = {float(a[0][3]), float(a[1][3]), float(a[2][3])};
    double norm = std::sqrt(double(m[3])*m[3] + double(m[7])*m[7] + double(m[11])*m[11]);
    if (norm < 1e-8 || !finite(eye)) return false;
    direction = {float(m[3]/norm), float(m[7]/norm), float(m[11]/norm)};
    // GlobeFocus requires a downward-looking perspective camera.
    return finite(direction) && direction.y < -0.001f && eye.y >= 0 && eye.y < 100000;
}

struct View {
    Vec3 eye{};
    float focus_x = 0, focus_z = 0, radius_full = 0, radius = 0;
    float blend = 0, scale = 0, geo = 0, mix = 0;
    float focus_lat = 0, sin_focus = 0, cos_focus = 1, row_lat = 0;
    double chart_lower = 0, chart_upper = 0;
    bool chart_valid = false;
    bool valid = false;
};
inline View prepare(const float* m, const Settings& s) {
    View v;
    Vec3 dir;
    if (!camera(m, v.eye, dir)) return v;
    v.valid = true;
    v.blend = smooth(1000.0f, 1800.0f, v.eye.y);
    if (v.blend == 0) return v;
    float t = std::max(0.0f, v.eye.y - s.water) / std::max(0.05f, -dir.y);
    v.focus_x = v.eye.x + dir.x * t;
    v.focus_z = std::clamp(v.eye.z + dir.z * t, 0.0f, s.height);
    v.radius_full = s.width / (2.0f * 3.14159265f);
    v.scale = std::max(v.blend, 0.02f);
    v.radius = v.radius_full / v.scale;
    v.geo = smooth(0.5f, 1.0f, v.blend);
    v.mix = std::clamp(v.blend / 0.02f, 0.0f, 1.0f);
    v.row_lat = latitude(v.focus_z, s, v.radius_full);
    v.focus_lat = v.row_lat * v.geo;
    v.sin_focus = std::sin(v.focus_lat);
    v.cos_focus = std::cos(v.focus_lat);
    v.chart_lower=(1-v.geo)*(0-v.focus_z)/v.radius_full+v.geo*(latitude_precise(0,s,v.radius_full)-v.row_lat);
    v.chart_upper=(1-v.geo)*(s.height-v.focus_z)/v.radius_full+v.geo*(latitude_precise(s.height,s,v.radius_full)-v.row_lat);
    v.chart_valid=v.focus_lat+v.chart_lower*v.scale>-1.57079632&&v.focus_lat+v.chart_upper*v.scale<1.57079632;
    return v;
}
struct Warp { Vec3 position; bool visible; };
inline Warp warp(Vec3 p, const View& v, const Settings& s) {
    if (!v.valid || v.blend == 0) return {p, true};
    if (!finite(p)) return {p, false};
    float lon = (p.x - v.focus_x) / v.radius_full;
    lon -= std::floor(lon / (2.0f * 3.14159265f) + 0.5f) * (2.0f * 3.14159265f);
    lon *= v.scale;
    float dlat = (p.z - v.focus_z) / v.radius_full;
    if (v.geo > 0) dlat += (latitude(p.z, s, v.radius_full) - v.row_lat - dlat) * v.geo;
    float lat = v.focus_lat + dlat * v.scale;
    float sin_lat = std::sin(lat), cos_lat = std::cos(lat);
    float pz = cos_lat * std::cos(lon);
    Vec3 n{cos_lat * std::sin(lon), sin_lat*v.sin_focus + pz*v.cos_focus,
           sin_lat*v.cos_focus - pz*v.sin_focus};
    float length = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
    n.x /= length; n.y /= length; n.z /= length;
    float alt = p.y - s.water;
    Vec3 q{v.focus_x + n.x*v.radius + n.x*alt,
           s.water + (n.y-1.0f)*v.radius + n.y*alt,
           v.focus_z + n.z*v.radius + n.z*alt};
    q = {p.x+(q.x-p.x)*v.mix, p.y+(q.y-p.y)*v.mix, p.z+(q.z-p.z)*v.mix};
    // Only the sign matters for visibility; no normalization of eye-q needed.
    float facing = n.x*(v.eye.x-q.x) + n.y*(v.eye.y-q.y) + n.z*(v.eye.z-q.z);
    bool visible = finite(q) && facing >= 0 && p.z >= -2.0f && p.z <= s.height+2.0f;
    return {q, visible};
}
struct ViewCache {
    float matrix[16]{};
    bool populated = false;
    View view{};
    const View& get(const float* m, const Settings& s) {
        if (!populated || std::memcmp(matrix, m, sizeof(matrix)) != 0) {
            std::memcpy(matrix, m, sizeof(matrix));
            view = prepare(m, s);
            populated = true;
        }
        return view;
    }
};
}
