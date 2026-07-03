/* This file is part of the Marble Marcher (https://github.com/HackerPoet/MarbleMarcher).
* Copyright(C) 2018 CodeParade
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.If not, see <http://www.gnu.org/licenses/>.
*/
#include "Explorer2D.h"
#include "Fractals.h"
#include "Res.h"
#include <cmath>
#include <cstring>
#include <algorithm>

//##########################################
//   BigFixed: 384-bit signed fixed point
//##########################################
static const double LIMB_BASE = 4294967296.0;  // 2^32

BigFixed::BigFixed() : sign(0) {
  std::memset(m, 0, sizeof(m));
}

BigFixed BigFixed::FromDouble(double v) {
  BigFixed r;
  if (v == 0.0) { return r; }
  r.sign = (v < 0.0) ? -1 : 1;
  double x = std::fabs(v);
  // m[11] is the integer part (value = magnitude / 2^352)
  for (int i = LIMBS - 1; i >= 0; --i) {
    const double ip = std::floor(x);
    r.m[i] = (std::uint32_t)ip;
    x = (x - ip) * LIMB_BASE;
  }
  return r;
}

double BigFixed::ToDouble() const {
  double r = 0.0;
  for (int i = 0; i < LIMBS; ++i) {
    r = (r + (double)m[i]) / LIMB_BASE;
  }
  return r * LIMB_BASE * (double)sign;
}

BigFixed BigFixed::Neg() const {
  BigFixed r = *this;
  r.sign = -r.sign;
  return r;
}

static int CmpMag(const BigFixed& a, const BigFixed& b) {
  for (int i = BigFixed::LIMBS - 1; i >= 0; --i) {
    if (a.m[i] != b.m[i]) { return (a.m[i] > b.m[i]) ? 1 : -1; }
  }
  return 0;
}

BigFixed BigFixed::Add(const BigFixed& a, const BigFixed& b) {
  if (a.sign == 0) { return b; }
  if (b.sign == 0) { return a; }
  BigFixed r;
  if (a.sign == b.sign) {
    r.sign = a.sign;
    std::uint64_t carry = 0;
    for (int i = 0; i < LIMBS; ++i) {
      const std::uint64_t t = (std::uint64_t)a.m[i] + b.m[i] + carry;
      r.m[i] = (std::uint32_t)t;
      carry = t >> 32;
    }
    // Coordinates stay tiny compared to the 32 integer bits; no overflow.
  } else {
    const int c = CmpMag(a, b);
    if (c == 0) { return r; }  // exact zero
    const BigFixed& big = (c > 0) ? a : b;
    const BigFixed& sml = (c > 0) ? b : a;
    r.sign = (c > 0) ? a.sign : b.sign;
    std::uint32_t borrow = 0;
    for (int i = 0; i < LIMBS; ++i) {
      const std::uint64_t sub = (std::uint64_t)sml.m[i] + borrow;
      const std::uint64_t bigv = big.m[i];
      if (bigv >= sub) {
        r.m[i] = (std::uint32_t)(bigv - sub);
        borrow = 0;
      } else {
        r.m[i] = (std::uint32_t)(bigv + 0x100000000ULL - sub);
        borrow = 1;
      }
    }
  }
  return r;
}

BigFixed BigFixed::Mul(const BigFixed& a, const BigFixed& b) {
  BigFixed r;
  if (a.sign == 0 || b.sign == 0) { return r; }
  r.sign = a.sign * b.sign;
  std::uint32_t prod[2 * LIMBS] = { 0 };
  for (int i = 0; i < LIMBS; ++i) {
    std::uint64_t carry = 0;
    for (int j = 0; j < LIMBS; ++j) {
      const std::uint64_t t = (std::uint64_t)a.m[i] * b.m[j] + prod[i + j] + carry;
      prod[i + j] = (std::uint32_t)t;
      carry = t >> 32;
    }
    int k = i + LIMBS;
    while (carry != 0 && k < 2 * LIMBS) {
      const std::uint64_t t = (std::uint64_t)prod[k] + carry;
      prod[k] = (std::uint32_t)t;
      carry = t >> 32;
      ++k;
    }
  }
  // (A/2^352)*(B/2^352) = AB/2^704 -> result limbs are AB >> 352 (11 limbs)
  for (int i = 0; i < LIMBS; ++i) {
    r.m[i] = prod[i + LIMBS - 1];
  }
  bool nonzero = false;
  for (int i = 0; i < LIMBS; ++i) {
    if (r.m[i] != 0) { nonzero = true; break; }
  }
  if (!nonzero) { r.sign = 0; }
  return r;
}

//##########################################
//   Explorer2D
//##########################################

// Reference orbit texture: two RGBA8 texels encode one (x, y) float pair.
// Mandelbrot uses the whole texture for one orbit; Julia splits it into the
// center orbit (entries 0..9215) and the critical orbit (from entry 9216,
// must match REF2_ENTRY_OFF in frag2d.glsl).
static const int REF_W = 2048;
static const int REF_H = 20;
static const int REF_MAX_ORBIT = (REF_W * REF_H) / 2;
static const int REF2_ENTRY_OFF = 9216;

// Below this zoom Mandelbrot and Julia switch to perturbation rendering.
// This is intentionally early (right where plain floats run out): the
// perturbation path uses only ordinary float math plus exponent tracking,
// so it cannot be broken by aggressive shader-compiler optimizations like
// the emulated double-single path can.
static const double perturb_threshold = 1e-4;

// Zoom limits per kind:
//  - Mandelbrot / Sierpinski triangle / carpet: ~1e-95 (384-bit CPU precision)
//  - Julia: ~1e-60 (limited by reference orbit slots in the texture)
//  - Burning Ship / Tricorn: ~1e-13 (emulated double precision)
//  - Newton / Koch: ~2e-5 (plain float)
static double MinZoom(int kind) {
  if (kind == KIND_MANDELBROT || kind == KIND_SIERPINSKI_TRI || kind == KIND_CARPET) { return 1e-95; }
  if (kind == KIND_JULIA) { return 1e-60; }
  if (kind == KIND_NEWTON || kind == KIND_KOCH) { return 2e-5; }
  return 1e-13;
}
static const double max_zoom = 4.0;

// 1/3 with all 384 bits set correctly (binary 0.010101...)
static BigFixed MakeThird() {
  BigFixed r;
  r.sign = 1;
  for (int i = 0; i < BigFixed::LIMBS - 1; ++i) { r.m[i] = 0x55555555u; }
  r.m[BigFixed::LIMBS - 1] = 0;
  return r;
}

// 1/sqrt(3) to full precision via Newton iteration y' = y*(1.5 - 1.5*y^2)
// (only needs Mul/Add; converges quadratically from the double seed)
static BigFixed MakeInvSqrt3() {
  BigFixed y = BigFixed::FromDouble(0.5773502691896258);
  const BigFixed c15 = BigFixed::FromDouble(1.5);
  for (int it = 0; it < 4; ++it) {
    const BigFixed y2 = BigFixed::Mul(y, y);
    const BigFixed t = BigFixed::Add(c15, BigFixed::Mul(c15, y2).Neg());
    y = BigFixed::Mul(y, t);
  }
  return y;
}

Explorer2D::Explorer2D() :
  kind(KIND_MANDELBROT),
  palette(0),
  cx(-0.6), cy(0.0),
  zoom(1.4), base_zoom(1.4),
  julia_live(true),
  jc_x(-0.7f), jc_y(0.27015f),
  have_prev(false),
  prev_ux(0.0), prev_uy(0.0),
  res_x(1280.0f), res_y(720.0f),
  ref_dirty(true),
  ref_count(0),
  ref_count2(0),
  ifs_sx(0.0), ifs_sy(0.0),
  ifs_scale(1.0),
  ifs_skip(0) {
  hp_cx = BigFixed::FromDouble(cx);
  hp_cy = BigFixed::FromDouble(cy);
}

bool Explorer2D::Load(float rx, float ry) {
  res_x = rx; res_y = ry;
  ref_pixels.assign(REF_W * REF_H * 4, 0);
  if (!ref_tex.resize({ (unsigned)REF_W, (unsigned)REF_H })) { return false; }
  ref_tex.setSmooth(false);
  return shader.loadFromFile(vert_glsl, frag2d_glsl);
}

void Explorer2D::SetKind(int k) {
  kind = k;
  ResetView();
}

void Explorer2D::SetCenter(double x, double y) {
  hp_cx = BigFixed::FromDouble(x);
  hp_cy = BigFixed::FromDouble(y);
  cx = x; cy = y;
  ref_dirty = true;
}

void Explorer2D::MoveCenter(double dx, double dy) {
  if (dx == 0.0 && dy == 0.0) { return; }
  // The center lives in 384-bit fixed point; the deltas are small enough
  // for doubles at any zoom depth, so add-only navigation stays exact.
  hp_cx = BigFixed::Add(hp_cx, BigFixed::FromDouble(dx));
  hp_cy = BigFixed::Add(hp_cy, BigFixed::FromDouble(dy));
  cx = hp_cx.ToDouble();
  cy = hp_cy.ToDouble();
  ref_dirty = true;
}

void Explorer2D::ResetView() {
  double x = -0.6, y = 0.0;
  switch (kind) {
  case KIND_BURNING_SHIP:   x = -0.4;  y = 0.4;  zoom = 1.4;  break;
  case KIND_TRICORN:        x = 0.0;   y = 0.0;  zoom = 1.7;  break;
  case KIND_NEWTON:         x = 0.0;   y = 0.0;  zoom = 1.7;  break;
  case KIND_JULIA:          x = 0.0;   y = 0.0;  zoom = 1.6;  julia_live = true; break;
  case KIND_SIERPINSKI_TRI: x = 0.0;   y = 0.15; zoom = 1.3;  break;
  case KIND_CARPET:         x = 0.0;   y = 0.0;  zoom = 0.75; break;
  case KIND_KOCH:           x = 0.0;   y = 0.0;  zoom = 1.1;  break;
  default:                  x = -0.6;  y = 0.0;  zoom = 1.4;  break;
  }
  SetCenter(x, y);
  base_zoom = zoom;
  have_prev = false;
}

void Explorer2D::Update(float mx_frac, float my_frac, bool lmb, float wheel,
                        float pan_x, float pan_y, float zoom_dir) {
  // Mouse in shader uv coords (y up, x scaled by aspect)
  const double ar = (double)res_x / (double)res_y;
  const double ux = (2.0*(double)mx_frac - 1.0) * ar;
  const double uy = 1.0 - 2.0*(double)my_frac;

  // Zoom toward the cursor: the fractal point under the mouse stays fixed.
  // Written in delta form so it works at any depth: c += u*(zoom_old-zoom_new)
  if (wheel != 0.0f) {
    const double old_zoom = zoom;
    zoom *= std::pow(0.85, (double)wheel);
    zoom = std::max(MinZoom(kind), std::min(zoom, max_zoom));
    MoveCenter(ux * (old_zoom - zoom), uy * (old_zoom - zoom));
    ref_dirty = true;  // iteration count depends on zoom
  }

  // Smooth keyboard zoom (E in, Q out) toward the screen center
  if (zoom_dir != 0.0f) {
    zoom *= std::pow(0.92, (double)zoom_dir);
    zoom = std::max(MinZoom(kind), std::min(zoom, max_zoom));
    ref_dirty = true;
  }

  // Drag to pan
  if (lmb && have_prev) {
    MoveCenter(-(ux - prev_ux)*zoom, -(uy - prev_uy)*zoom);
  }
  prev_ux = ux; prev_uy = uy;
  have_prev = true;

  // WASD pan
  if (pan_x != 0.0f || pan_y != 0.0f) {
    MoveCenter((double)pan_x * zoom * 0.02, (double)pan_y * zoom * 0.02);
  }

  // Live Julia c: screen position maps to a fixed c range (independent of zoom)
  if (kind == KIND_JULIA && julia_live) {
    jc_x = (float)(ux * 1.2);
    jc_y = (float)(uy * 1.2);
  }
}

int Explorer2D::PrecisionMode() const {
  if (kind == KIND_NEWTON || kind == KIND_KOCH) { return 0; }
  if (zoom >= perturb_threshold) { return 0; }
  if (kind == KIND_BURNING_SHIP || kind == KIND_TRICORN) { return 1; }
  return 2;  // mandelbrot/julia perturbation, sierpinski/carpet CPU-assisted
}

int Explorer2D::Iters() const {
  const double depth = std::log10(std::max(base_zoom / zoom, 1.0));
  switch (kind) {
  case KIND_NEWTON:          return 64;
  case KIND_SIERPINSKI_TRI:  return std::min(360, 14 + (int)(depth * 3.4));  // features halve per iter
  case KIND_CARPET:          return std::min(240, 10 + (int)(depth * 2.2));  // features third per iter
  case KIND_KOCH:            return std::min(44, 6 + (int)(depth * 2.2));
  case KIND_MANDELBROT:      return std::min(20000, 96 + (int)(depth * 130.0));
  case KIND_JULIA:           return std::min(9000, 96 + (int)(depth * 130.0));
  default:                   return std::min(2000, 96 + (int)(depth * 130.0));
  }
}

// Iterate the reference orbit(s) for z^2+c in 384-bit precision and store
// them as raw float bits in an RGBA8 texture (decoded in the shader).
// Mandelbrot: one orbit starting at 0, c = view center.
// Julia: primary orbit starting at the view center, plus the critical orbit
// (starting at 0) which the shader rebases onto.
void Explorer2D::ComputeReference() {
  auto pack_float = [&](int texel, float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, 4);
    std::uint8_t* p = &ref_pixels[(size_t)texel * 4];
    p[0] = (std::uint8_t)(bits & 0xFF);
    p[1] = (std::uint8_t)((bits >> 8) & 0xFF);
    p[2] = (std::uint8_t)((bits >> 16) & 0xFF);
    p[3] = (std::uint8_t)((bits >> 24) & 0xFF);
  };
  const bool is_julia = (kind == KIND_JULIA);
  const BigFixed bcx = is_julia ? BigFixed::FromDouble((double)jc_x) : hp_cx;
  const BigFixed bcy = is_julia ? BigFixed::FromDouble((double)jc_y) : hp_cy;

  // Primary orbit: starts at 0 (Mandelbrot) or at the view center (Julia)
  BigFixed zx, zy;
  if (is_julia) { zx = hp_cx; zy = hp_cy; }
  const int max_n = std::min(Iters(), is_julia ? (REF2_ENTRY_OFF - 2) : (REF_MAX_ORBIT - 2));
  int count = 0;
  for (int n = 0; n < max_n; ++n) {
    const double dx = zx.ToDouble();
    const double dy = zy.ToDouble();
    pack_float(2*n,     (float)dx);
    pack_float(2*n + 1, (float)dy);
    count = n + 1;
    if (dx*dx + dy*dy > 1e6) { break; }  // reference escaped
    const BigFixed x2 = BigFixed::Mul(zx, zx);
    const BigFixed y2 = BigFixed::Mul(zy, zy);
    const BigFixed xy = BigFixed::Mul(zx, zy);
    zx = BigFixed::Add(BigFixed::Add(x2, y2.Neg()), bcx);
    zy = BigFixed::Add(BigFixed::Add(xy, xy), bcy);
  }
  ref_count = count;

  // Julia secondary orbit: the critical orbit (starts at 0). Doubles are
  // plenty here — its values only feed float texels and it does not depend
  // on the view center.
  ref_count2 = 0;
  if (is_julia) {
    double kx = 0.0, ky = 0.0;
    const int max_b = REF_MAX_ORBIT - REF2_ENTRY_OFF - 2;
    for (int n = 0; n < max_b; ++n) {
      pack_float(2*(REF2_ENTRY_OFF + n),     (float)kx);
      pack_float(2*(REF2_ENTRY_OFF + n) + 1, (float)ky);
      ref_count2 = n + 1;
      if (kx*kx + ky*ky > 1e6) { break; }
      const double tx = kx*kx - ky*ky + (double)jc_x;
      ky = 2.0*kx*ky + (double)jc_y;
      kx = tx;
    }
  }
  ref_tex.update(ref_pixels.data());
}

// Sierpinski triangle & carpet deep zoom: while the whole screen falls into
// the same fold branch as the view center, the iteration is identical for
// every pixel — so pre-apply those iterations to the center in 384-bit
// precision here and let the shader iterate only the visible remainder.
void Explorer2D::ComputeIfsSkip() {
  const auto sub_d = [](const BigFixed& a, double b) {
    return BigFixed::Add(a, BigFixed::FromDouble(-b)).ToDouble();
  };
  const double ar = (double)res_x / (double)res_y;
  const double diag = std::sqrt(1.0 + ar*ar);
  double r = zoom * diag;  // screen half-diagonal in fractal units
  int K = 0;

  if (kind == KIND_SIERPINSKI_TRI) {
    // View space (equilateral) -> exact unit right-triangle space, in BigFixed
    static const BigFixed third = MakeThird();
    static const BigFixed inv_s3 = MakeInvSqrt3();
    BigFixed fx = BigFixed::Add(BigFixed::Add(BigFixed::Mul(inv_s3, hp_cx),
                                              BigFixed::Mul(third, hp_cy).Neg()), third);
    BigFixed fy = BigFixed::Add(BigFixed::Add(BigFixed::Mul(third, hp_cy),
                                              BigFixed::Mul(third, hp_cy)), third);
    while (K < 340) {
      // Signed distances to all branch/escape boundaries (BigFixed diff keeps
      // full precision even when the center is extremely close to one)
      const double dx0 = fx.ToDouble();
      const double dy0 = fy.ToDouble();
      const double dx5 = sub_d(fx, 0.5);
      const double dy5 = sub_d(fy, 0.5);
      const double e   = BigFixed::Add(BigFixed::Add(fx, fy), BigFixed::FromDouble(-1.0)).ToDouble();
      if (dx0 < 0.0 || dy0 < 0.0 || e > 0.0) { break; }  // center escaped
      const double d = std::min(std::min(std::fabs(dx0), std::fabs(dy0)),
                                std::min(std::min(std::fabs(dx5), std::fabs(dy5)),
                                         std::fabs(e) * 0.7071));
      if (r >= d * 0.45) { break; }  // screen spans a boundary: stop skipping
      if (dx5 >= 0.0) {
        fx = BigFixed::Add(BigFixed::Add(fx, fx), BigFixed::FromDouble(-1.0));
        fy = BigFixed::Add(fy, fy);
      } else if (dy5 >= 0.0) {
        fy = BigFixed::Add(BigFixed::Add(fy, fy), BigFixed::FromDouble(-1.0));
        fx = BigFixed::Add(fx, fx);
      } else {
        fx = BigFixed::Add(fx, fx);
        fy = BigFixed::Add(fy, fy);
      }
      r *= 2.0;
      ++K;
    }
    ifs_sx = fx.ToDouble();
    ifs_sy = fy.ToDouble();
  } else {  // KIND_CARPET
    BigFixed fx = hp_cx, fy = hp_cy;
    while (K < 230) {
      if (std::fabs(fx.ToDouble()) > 0.5 || std::fabs(fy.ToDouble()) > 0.5) { break; }
      const BigFixed px = BigFixed::Add(BigFixed::Add(fx, fx), fx);  // 3x
      const BigFixed py = BigFixed::Add(BigFixed::Add(fy, fy), fy);
      // Distance to the nearest cell boundary (half-integer lines) in 3x coords
      double d = 1e9;
      static const double bounds[4] = { -1.5, -0.5, 0.5, 1.5 };
      for (int b = 0; b < 4; ++b) {
        d = std::min(d, std::fabs(sub_d(px, bounds[b])));
        d = std::min(d, std::fabs(sub_d(py, bounds[b])));
      }
      if (3.0*r >= d * 0.45) { break; }  // screen spans a boundary
      const int cx3 = (sub_d(px, 0.5) >= 0.0) ? 1 : ((sub_d(px, -0.5) < 0.0) ? -1 : 0);
      const int cy3 = (sub_d(py, 0.5) >= 0.0) ? 1 : ((sub_d(py, -0.5) < 0.0) ? -1 : 0);
      if (cx3 == 0 && cy3 == 0) { break; }  // center in the hole: GPU redoes this step
      fx = BigFixed::Add(px, BigFixed::FromDouble(-(double)cx3));
      fy = BigFixed::Add(py, BigFixed::FromDouble(-(double)cy3));
      r *= 3.0;
      ++K;
    }
    ifs_sx = fx.ToDouble();
    ifs_sy = fy.ToDouble();
  }
  ifs_scale = r / diag;  // = zoom * 2^K (or 3^K)
  ifs_skip = K;
}

void Explorer2D::Write() {
  const bool perturb = ((kind == KIND_MANDELBROT || kind == KIND_JULIA) &&
                        zoom < perturb_threshold);
  const bool ifs_assist = (kind == KIND_SIERPINSKI_TRI || kind == KIND_CARPET);
  if (ref_dirty && perturb) {
    ComputeReference();
    ref_dirty = false;
  }
  if (ref_dirty && ifs_assist) {
    ComputeIfsSkip();
    ref_dirty = false;
  }

  // The view center is kept in high precision on the CPU and sent as a
  // double-single pair (hi + lo float); the shader adds only the tiny
  // per-pixel offset. (Used by the float and double-single paths.)
  const float hx = (float)cx, hy = (float)cy;
  shader.setUniform("iResolution", sf::Glsl::Vec2(res_x, res_y));
  shader.setUniform("iCenterX", sf::Glsl::Vec2(hx, (float)(cx - (double)hx)));
  shader.setUniform("iCenterY", sf::Glsl::Vec2(hy, (float)(cy - (double)hy)));
  shader.setUniform("iZoom", (float)zoom);
  shader.setUniform("iKind", kind);
  shader.setUniform("iPalette", palette);
  shader.setUniform("iJuliaC", sf::Glsl::Vec2(jc_x, jc_y));
  shader.setUniform("iOne", 1.0f);
  shader.setUniform("iDeep", (!perturb && zoom < 1e-4) ? 1 : 0);
  shader.setUniform("iPerturb", perturb ? 1 : 0);

  int iters_uniform = Iters();
  if (ifs_assist) {
    // The shader only iterates what the CPU skip left over
    iters_uniform = std::min(std::max(iters_uniform - ifs_skip, 6), 90);
    shader.setUniform("iIfsStart", sf::Glsl::Vec2((float)ifs_sx, (float)ifs_sy));
    shader.setUniform("iIfsScale", (float)ifs_scale);
    shader.setUniform("iIfsSkip", ifs_skip);
  }
  shader.setUniform("iIters", iters_uniform);

  if (perturb) {
    int ze = 0;
    const double zman = std::frexp(zoom, &ze);
    shader.setUniform("iZetaMan", (float)zman);
    shader.setUniform("iZetaExp", ze);
    shader.setUniform("iRefCount", ref_count);
    shader.setUniform("iRefCount2", ref_count2);
    shader.setUniform("iRefTex", ref_tex);
  }
  // View->fractal delta transform for the Sierpinski triangle (float is fine
  // for the small on-screen offsets)
  shader.setUniform("iFracMat", sf::Glsl::Vec4(0.57735027f, -0.33333334f, 0.0f, 0.66666667f));
}
