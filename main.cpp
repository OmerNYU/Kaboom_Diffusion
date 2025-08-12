#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>
#include <limits>
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include "geometry.h"

// ----------------- Params you can tweak -----------------
const int   width    = 640;
const int   height   = 480;
const float fov      = M_PI/3.0f;
const int   max_steps = 128;
const float min_step = 0.01f;

const float sphere_radius   = 1.5f;
const float noise_amplitude = 1.0f;

const int   nframes  = 120;   // total frames
const float fps      = 24.0f; // frames per second
// --------------------------------------------------------

// Global time (advanced per-frame)
static float g_time = 0.0f;

// ----------------- Math / utility -----------------
template <typename T> inline T lerp(const T &v0, const T &v1, float t) {
    return v0 + (v1-v0)*std::max(0.f, std::min(1.f, t));
}

float hash(const float n) {
    float x = sinf(n)*43758.5453f;
    return x - floorf(x);
}

float noise(const Vec3f &x) {
    Vec3f p(floorf(x.x), floorf(x.y), floorf(x.z));
    Vec3f f(x.x-p.x, x.y-p.y, x.z-p.z);
    f = f*(f*(Vec3f(3.f,3.f,3.f)-f*2.f));
    float n = p*Vec3f(1.f,57.f,113.f);
    return lerp( lerp( lerp(hash(n+  0.f), hash(n+  1.f), f.x),
                       lerp(hash(n+ 57.f), hash(n+ 58.f), f.x), f.y),
                 lerp( lerp(hash(n+113.f), hash(n+114.f), f.x),
                       lerp(hash(n+170.f), hash(n+171.f), f.x), f.y), f.z);
}

Vec3f rotate(const Vec3f &v) {
    // fixed rotation matrix (columns)
    return Vec3f(Vec3f( 0.00,  0.80,  0.60)*v,
                 Vec3f(-0.80,  0.36, -0.48)*v,
                 Vec3f(-0.60, -0.48,  0.64)*v);
}

float fractal_brownian_motion(const Vec3f &x) {
    Vec3f p = rotate(x);
    float f = 0.f;
    f += 0.5000f*noise(p); p = p*2.32f;
    f += 0.2500f*noise(p); p = p*3.03f;
    f += 0.1250f*noise(p); p = p*2.61f;
    f += 0.0625f*noise(p);
    return f/0.9375f;
}

Vec3f palette_fire(const float d) {
    const Vec3f   yellow(1.7f, 1.3f, 1.0f); // "hot": components can exceed 1
    const Vec3f   orange(1.0f, 0.6f, 0.0f);
    const Vec3f      red(1.0f, 0.0f, 0.0f);
    const Vec3f darkgray(0.2f, 0.2f, 0.2f);
    const Vec3f     gray(0.4f, 0.4f, 0.4f);

    float x = std::max(0.f, std::min(1.f, d));
    if (x<.25f)       return lerp(gray,     darkgray, x*4.f);
    else if (x<.5f)   return lerp(darkgray, red,      x*4.f-1.f);
    else if (x<.75f)  return lerp(red,      orange,   x*4.f-2.f);
    else              return lerp(orange,   yellow,   x*4.f-3.f);
}

// ----------------- Distance field (animated) -----------------
float signed_distance(const Vec3f &p) {
    // base radius breathes slightly over time
    float r = sphere_radius + 0.25f * sinf(g_time * 2.0f);

    // time-scrolled displacement (mix of sin product and fBM)
    float phase = g_time * 6.0f;
    float sin_disp = sinf(16.0f*p.x + phase) * sinf(16.0f*p.y + phase) * sinf(16.0f*p.z + phase);
    float fbm = fractal_brownian_motion(p*2.0f + Vec3f(g_time, g_time*0.7f, g_time*1.3f));

    float displacement = noise_amplitude * (0.6f*sin_disp + 0.8f*(fbm - 0.5f));
    return p.norm() - (r + displacement);
}

// ----------------- Ray marching -----------------
bool sphere_trace(const Vec3f &orig, const Vec3f &dir, Vec3f &pos) {
    pos = orig;
    for (int i=0; i<max_steps; ++i) {
        float d = signed_distance(pos);
        if (d < 0.0f) return true;                       // hit surface
        pos = pos + dir * std::max(d*0.1f, min_step);    // conservative step
    }
    return false; // no hit
}

Vec3f distance_field_normal(const Vec3f &pos) {
    const float eps = 0.05f;
    float d  = signed_distance(pos);
    float nx = signed_distance(pos + Vec3f(eps, 0, 0)) - d;
    float ny = signed_distance(pos + Vec3f(0, eps, 0)) - d;
    float nz = signed_distance(pos + Vec3f(0, 0, eps)) - d;
    return Vec3f(nx, ny, nz).normalize();
}

// ----------------- Frame writer -----------------
void write_ppm(const std::string &filename, const std::vector<Vec3f> &fb) {
    std::ofstream ofs(filename, std::ios::binary);
    ofs << "P6\n" << width << " " << height << "\n255\n";
    for (size_t i=0; i<fb.size(); ++i) {
        // tone-map the “hot” colors a bit (simple clamp)
        float r = std::min(1.0f, fb[i][0]);
        float g = std::min(1.0f, fb[i][1]);
        float b = std::min(1.0f, fb[i][2]);
        unsigned char R = (unsigned char)(255 * std::max(0.f, r));
        unsigned char G = (unsigned char)(255 * std::max(0.f, g));
        unsigned char B = (unsigned char)(255 * std::max(0.f, b));
        ofs.put(R); ofs.put(G); ofs.put(B);
    }
}

// ----------------- Main render -----------------
int main() {
    // Camera and light
    const Vec3f cam_pos(0, 0, 3);
    const Vec3f light_pos(10, 10, 10);

    for (int f = 0; f < nframes; ++f) {
        g_time = f / fps; // seconds
        std::vector<Vec3f> framebuffer(width*height);

        #pragma omp parallel for
        for (int j = 0; j < height; ++j) {
            for (int i = 0; i < width; ++i) {
                float dir_x =  (i + 0.5f) -  width/2.0f;
                float dir_y = -(j + 0.5f) + height/2.0f; // flip Y
                float dir_z = -height / (2.0f * tanf(fov/2.0f));
                Vec3f ray_dir = Vec3f(dir_x, dir_y, dir_z).normalize();

                Vec3f hit;
                Vec3f color(0.2f, 0.7f, 0.8f); // sky

                if (sphere_trace(cam_pos, ray_dir, hit)) {
                    // shading
                    Vec3f N = distance_field_normal(hit);
                    Vec3f L = (light_pos - hit).normalize();
                    float lambert = std::max(0.0f, N*L);
                    float ambient = 0.25f;

                    // fire palette driven by animated fBM at the surface
                    float fval = fractal_brownian_motion(hit*2.5f + Vec3f(0,0,g_time*1.2f));
                    Vec3f fire = palette_fire(fval);

                    // mix: fire color * light, with a little rim from view angle
                    Vec3f V = (cam_pos - hit).normalize();
                    float rim = powf(std::max(0.0f, 1.0f - std::max(0.0f, N*V)), 2.0f);

                    float light_intensity = ambient + 0.9f*lambert + 0.4f*rim;
                    color = fire * light_intensity;
                }

                framebuffer[i + j*width] = color;
            }
        }

        std::ostringstream name;
        name << "out_" << std::setfill('0') << std::setw(4) << f << ".ppm";
        write_ppm(name.str(), framebuffer);
        std::cerr << "Wrote " << name.str() << "\n";
    }

    return 0;
}
