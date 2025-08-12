# README.md

## Kaboom — Ray-Marched Fireball (C++)

A tiny, dependency-free C++ renderer that uses **sphere tracing** (ray marching) over a **signed distance field (SDF)** to render a bumpy, animated “fireball.” Frames are written as `.ppm` images that you can stitch into a GIF.

[https://github.com/yourname/kaboom](https://github.com/yourname/kaboom)

---

## What you get

* CPU path, no GPU/GL required
* Clean 3-vector math (`geometry.h`)
* Animated SDF with sinusoidal + fBM displacement
* Simple Lambert + rim lighting with a **fire color palette**
* Outputs `out_0000.ppm … out_0NNN.ppm` (ready for GIF/MP4)

---

## File layout

```
.
├── main.cpp       // renderer, animation loop, sphere tracing
└── geometry.h     // minimal Vec3 math (x,y,z, dot, normalize, etc.)
```

---

## Build & run

### Linux / macOS (Clang/GCC)

```bash
g++ -O2 -fopenmp main.cpp -o render      # drop -fopenmp if not available
./render
```

### Windows (MSYS2/MinGW or WSL)

```bash
g++ -O2 -fopenmp main.cpp -o render.exe  # omit -fopenmp if your toolchain lacks it
./render.exe
```

The program writes frames in the current directory:

```
out_0000.ppm, out_0001.ppm, … out_0119.ppm
```

View a PPM with your image viewer, or convert to PNG/GIF:

```bash
# Install ImageMagick if needed
# Ubuntu/Debian: sudo apt-get install imagemagick

# Make a GIF (≈24 fps; tune delay)
magick convert -delay 5 -loop 0 out_*.ppm kaboom.gif
# or (older IM installs):
convert -delay 5 -loop 0 out_*.ppm kaboom.gif
```

(You can also do `ffmpeg -framerate 24 -pattern_type glob -i 'out_*.ppm' -pix_fmt yuv420p kaboom.mp4` for MP4.)

---

## How it works (concepts & code map)

### 1) Signed Distance Field (SDF)

We render an **implicit surface** defined by a function $d(\mathbf{p})$ that returns the signed distance from point $\mathbf{p}$ to the surface. For a perfect sphere of radius $R$ at the origin:

$$
d(\mathbf{p}) = \|\mathbf{p}\| - R
$$

We **displace** that radius with animated noise:

```cpp
float signed_distance(const Vec3f &p) {
    float r = sphere_radius + 0.25f * sinf(g_time * 2.0f);

    float phase = g_time * 6.0f;
    float sin_disp = sinf(16*p.x + phase) * sinf(16*p.y + phase) * sinf(16*p.z + phase);
    float fbm = fractal_brownian_motion(p*2.0f + Vec3f(g_time, g_time*0.7f, g_time*1.3f));

    float displacement = noise_amplitude * (0.6f*sin_disp + 0.8f*(fbm - 0.5f));
    return p.norm() - (r + displacement);
}
```

### 2) Sphere Tracing (Ray Marching)

Cast a ray from the camera through each pixel. At a current position $\mathbf{x}$, evaluate $d(\mathbf{x})$. **Step forward by (a fraction of) that distance**—you’re guaranteed not to hit the surface before that step if your SDF is conservative.

```cpp
bool sphere_trace(const Vec3f &orig, const Vec3f &dir, Vec3f &pos) {
    pos = orig;
    for (int i=0; i<max_steps; ++i) {
        float d = signed_distance(pos);
        if (d < 0.0f) return true;                      // hit
        pos = pos + dir * std::max(d*0.1f, min_step);   // advance
    }
    return false;                                       // miss
}
```

* `max_steps` caps marching work.
* `min_step` prevents tiny steps from stalling.
* `0.1f` is a safety scale to avoid skipping thin features.

### 3) Normals via SDF Gradient

Approximate the surface normal by finite differences (sample SDF in ±x/±y/±z):

```cpp
Vec3f distance_field_normal(const Vec3f &pos) {
    const float eps = 0.05f;
    float d  = signed_distance(pos);
    float nx = signed_distance(pos + Vec3f(eps, 0, 0)) - d;
    float ny = signed_distance(pos + Vec3f(0, eps, 0)) - d;
    float nz = signed_distance(pos + Vec3f(0, 0, eps)) - d;
    return Vec3f(nx, ny, nz).normalize();
}
```

### 4) Shading & Fire Palette

* Diffuse (Lambert): `max(0, dot(N, L))`
* Ambient floor for visibility
* Rim light to highlight the silhouette
* Color comes from **fire palette** driven by animated fBM:

```cpp
float fval = fractal_brownian_motion(hit*2.5f + Vec3f(0,0,g_time*1.2f));
Vec3f fire = palette_fire(fval);
float light_intensity = ambient + 0.9f*lambert + 0.4f*rim;
color = fire * light_intensity;
```

### 5) Animation

A global `g_time` advances per frame:

```cpp
for (int f = 0; f < nframes; ++f) {
    g_time = f / fps;
    // render one frame → out_XXXX.ppm
}
```

---

## Key functions (quick map)

* `lerp(a,b,t)`: linear interpolation, clamped
* `hash()`, `noise()`: value noise building block
* `rotate(v)`: mixes coordinates for richer fBM
* `fractal_brownian_motion(x)`: layered noise (octaves)
* `palette_fire(d)`: maps \[0,1] → gray→red→orange→yellow
* `signed_distance(p)`: animated displaced sphere
* `sphere_trace(o, d, pos)`: ray marcher (SDF)
* `distance_field_normal(p)`: gradient-based normal
* `write_ppm(name, fb)`: writes `P6` binary PPM

---

## Parameters (tweak me in `main.cpp`)

```cpp
const int   width = 640, height = 480;
const float fov = M_PI/3.0f;
const int   max_steps = 128;
const float min_step  = 0.01f;

const float sphere_radius   = 1.5f;
const float noise_amplitude = 1.0f;

const int   nframes = 120;   // total frames
const float fps     = 24.0f; // frames per second
```

Ideas:

* **Bigger fireball:** increase `sphere_radius`
* **More turbulence:** increase `noise_amplitude`
* **Longer clip:** increase `nframes` or `fps`
* **Sharper details:** reduce `min_step`, increase `max_steps` (slower)

---

## Suggestions & next steps

### Image quality

* **Normals**: Use *central differences* (sample ±eps) for smoother lighting; smaller `eps` improves detail.
* **Tone mapping**: Replace hard clamp with Reinhard or filmic curve; add **gamma correction** (`pow(color, 1/2.2)`).
* **Soft shadows**: March a shadow ray toward the light, accumulate occlusion.
* **Ambient occlusion**: Sample SDF along the normal to estimate cavity darkening.

### Performance

* **Adaptive step scaling**: Increase the step multiplier where the field is smooth; reduce near high curvature.
* **Early bailout**: Stop if distance grows beyond a scene bound.
* **Parallelism**: Keep `-fopenmp`; consider **std::execution** parallel algorithms or **TBB**.

### Features

* **CSG modeling**: Combine primitives using min/max (union, intersection, subtraction).
* **Repetition tiling**: Mod the space for repeating patterns.
* **Reflections/refractions**: Secondary rays for mirror/glass.
* **Motion blur**: Time-jitter samples per pixel.
* **Camera controls**: CLI args for FOV, camera pos, frames, output pattern.
* **PNG output**: Use `stb_image_write.h` to write PNGs directly (no PPM/GIF step).

### Packaging & DX

* **CMake** project
* **Config file / CLI** for parameters (`--frames 300 --fps 30 --size 800x800`)
* **Real-time preview**: Render to a window via SDL2/GLFW, hot-reload parameters

---

## Troubleshooting

* **`Vec3f does not name a type`**
  Ensure the header guard in `geometry.h` is *not* using double underscores. Use:

  ```cpp
  #ifndef GEOMETRY_H_
  #define GEOMETRY_H_
  // ...
  #endif
  ```
* **OpenMP errors**
  Remove `-fopenmp` if your compiler/lib lacks it. It only affects speed.
* **ImageMagick “policy” errors**
  Try `magick convert` instead of `convert`, or export to PNG first:
  `magick mogrify -format png out_*.ppm`
* **No viewer for PPM**
  Convert: `magick convert out_0000.ppm out_0000.png`

---

## License

Add a license to your repository (MIT is a good default for sample code).

---

## Acknowledgements

Classic SDF and sphere-tracing techniques have been popularized by the demoscene and shader authors; this project is a minimal C++ take on those ideas.

---

**Enjoy making it go 💥.** If you want a version that writes a GIF directly or renders in a live window, open an issue or PR.
