/* Marble Marcher — 2D fractal explorer shader.
* Flat escape-time rendering: no ray marching, one fractal evaluation per pixel.
*
* Deep zoom: coordinates use emulated double precision ("double-single":
* each number is the unevaluated sum of two floats, ~48 bit mantissa).
* This pushes the zoom limit from ~1e-5 down to ~1e-13.
*/
#version 120
#pragma optionNV(fastmath off)
#pragma optionNV(fastprecision off)

uniform vec2 iResolution;
uniform vec2 iCenterX;   // view center x as double-single (hi, lo)
uniform vec2 iCenterY;   // view center y as double-single (hi, lo)
uniform float iZoom;     // fractal units per half screen-height
uniform int iKind;       // Explorer2DKind (see Fractals.h)
uniform int iPalette;    // 0 rainbow, 1 fire, 2 ocean, 3 gold
uniform vec2 iJuliaC;
uniform int iIters;      // raised automatically while zooming in
uniform int iDeep;       // 1 = use emulated double precision (deep zoom)
uniform vec4 iFracMat;   // view->fractal delta transform (Sierpinski triangle)

// Perturbation deep zoom (Mandelbrot & Julia): reference orbits computed on
// the CPU in 384-bit precision, stored as raw float bits in an RGBA8 texture.
uniform int iPerturb;      // 1 = perturbation path active
uniform sampler2D iRefTex; // reference orbits, two texels per (x,y) pair
uniform int iRefCount;     // entries in the primary orbit (view center)
uniform int iRefCount2;    // entries in the secondary orbit (Julia: critical orbit)
uniform float iZetaMan;    // zoom = iZetaMan * 2^iZetaExp
uniform int iZetaExp;

// IFS deep zoom (Sierpinski triangle & carpet): the CPU pre-applies the first
// iIfsSkip fold iterations to the view center in 384-bit precision.
uniform vec2 iIfsStart;    // center after the skipped iterations
uniform float iIfsScale;   // zoom scaled up by the skipped iterations
uniform int iIfsSkip;      // number of skipped iterations (for band coloring)

#define MAX_ITER_2D 2048
#define MAX_ITER_IFS 96
#define MAX_ITER_KOCH 48
#define MAX_PERTURB_ITER 20000
#define REF_W 2048.0
#define REF_H 20.0
#define REF2_ENTRY_OFF 9216

vec2 cmul(vec2 a, vec2 b) { return vec2(a.x*b.x - a.y*b.y, a.x*b.y + a.y*b.x); }

vec3 pal_col(float t) {
	if (iPalette == 0) { return 0.5 + 0.5*cos(6.28318*(t + vec3(0.00, 0.33, 0.67))); }
	if (iPalette == 1) { return 0.5 + 0.5*cos(6.28318*(vec3(1.0, 1.0, 0.5)*t + vec3(0.00, 0.25, 0.50))); }
	if (iPalette == 2) { return 0.5 + 0.5*cos(6.28318*(vec3(0.8, 0.9, 1.0)*t + vec3(0.50, 0.45, 0.40))); }
	float g = 0.5 + 0.5*cos(6.28318*t);
	return vec3(g, g*0.85, g*0.55);
}

//##########################################
//   Double-single arithmetic (Dekker/Knuth)
//##########################################
vec2 ds(float a) { return vec2(a, 0.0); }
vec2 ds_neg(vec2 a) { return vec2(-a.x, -a.y); }
vec2 ds_two(vec2 a) { return vec2(2.0*a.x, 2.0*a.y); }  // exact in binary fp

//iOne is always 1.0, but the compiler cannot know that: multiplying key
//intermediates by it blocks algebraic simplification of the error terms
//(otherwise "(a+b)-a" folds to "b" and the extra precision silently dies).
uniform float iOne;

vec2 ds_add(vec2 a, vec2 b) {
	float s = (a.x + b.x) * iOne;
	float v = s - a.x;
	float e = (b.x - v) + (a.x - (s - v));
	e += a.y + b.y;
	float hi = (s + e) * iOne;
	return vec2(hi, e - (hi - s));
}
vec2 ds_mul(vec2 a, vec2 b) {
	const float SPLIT = 4097.0;  // 2^12 + 1 (Dekker split for 24-bit floats)
	float ca = (SPLIT * a.x) * iOne;
	float ahi = ca - (ca - a.x);
	float alo = a.x - ahi;
	float cb = (SPLIT * b.x) * iOne;
	float bhi = cb - (cb - b.x);
	float blo = b.x - bhi;
	float p = (a.x * b.x) * iOne;
	float e = ((ahi*bhi - p) + ahi*blo + alo*bhi) + alo*blo;
	e += a.x*b.y + a.y*b.x;
	float hi = (p + e) * iOne;
	return vec2(hi, e - (hi - p));
}
vec2 ds_three(vec2 a) { return ds_add(ds_add(a, a), a); }

//##########################################
//   Reference orbit access (perturbation)
//##########################################
//Reconstruct an IEEE754 float from 4 bytes stored in an RGBA8 texel
float decode_f(vec4 t) {
	vec4 b = floor(t*255.0 + 0.5);
	float sgn = (b.a >= 128.0) ? -1.0 : 1.0;
	float ea = b.a - ((sgn < 0.0) ? 128.0 : 0.0);
	float eb = (b.b >= 128.0) ? 1.0 : 0.0;
	float ex = ea*2.0 + eb;
	float mant = (b.b - eb*128.0)*65536.0 + b.g*256.0 + b.r;
	if (ex < 0.5) { return sgn * mant * exp2(-149.0); }  // denormal
	return sgn * (1.0 + mant*1.1920929e-7) * exp2(ex - 127.0);
}
vec2 fetch_ref(int mi) {
	float fi = float(mi) * 2.0;
	float ty = floor(fi / REF_W);
	float tx = fi - ty * REF_W;   // always even, so tx+1 stays in the same row
	float v = (ty + 0.5) / REF_H;
	return vec2(
		decode_f(texture2D(iRefTex, vec2((tx + 0.5) / REF_W, v))),
		decode_f(texture2D(iRefTex, vec2((tx + 1.5) / REF_W, v))));
}

void main() {
	vec2 uv = (2.0*gl_FragCoord.xy - iResolution) / iResolution.y;
	vec2 dxy = uv * iZoom;                                  // small view-space offset
	vec2 pos = vec2(iCenterX.x, iCenterY.x) + dxy;          // float approximation
	vec3 col = vec3(0.0);

	if (iKind == 3) {
		//Newton fractal for z^3 - 1: color by root basin, shade by convergence speed
		vec2 z = pos;
		float n = 0.0;
		int root = -1;
		for (int i = 0; i < 64; ++i) {
			vec2 z2 = cmul(z, z);
			float dl = 9.0*dot(z2, z2);
			if (dl < 1e-18) { break; }
			vec2 num = cmul(z2, z) - vec2(1.0, 0.0);
			vec2 den = 3.0*z2;
			z -= vec2(dot(num, den), num.y*den.x - num.x*den.y) / dl;
			float da = length(z - vec2( 1.0, 0.0));
			float db = length(z - vec2(-0.5, 0.8660254));
			float dc = length(z - vec2(-0.5,-0.8660254));
			if (min(da, min(db, dc)) < 1e-4) {
				root = (da < db && da < dc) ? 0 : ((db < dc) ? 1 : 2);
				n = float(i);
				break;
			}
		}
		if (root < 0) { col = vec3(0.0); }
		else { col = pal_col(float(root)*0.3333 + 0.1) * clamp(1.1 - n*0.035, 0.15, 1.0); }

	} else if (iKind == 5) {
		//Sierpinski triangle in unit right-triangle space. The CPU already
		//applied the first iIfsSkip doublings to the view center in 384-bit
		//precision (all pixels share those branches); only the visible
		//remainder is iterated here — plain floats, no precision limit.
		vec2 dv = uv * iIfsScale;
		vec2 p = iIfsStart + vec2(iFracMat.x*dv.x + iFracMat.y*dv.y,
		                          iFracMat.z*dv.x + iFracMat.w*dv.y);
		float n = -1.0;
		for (int i = 0; i < MAX_ITER_IFS; ++i) {
			if (i >= iIters) { break; }
			if (p.x < 0.0 || p.y < 0.0 || p.x + p.y > 1.0) { n = float(iIfsSkip + i); break; }
			if (p.x >= 0.5)      { p = vec2(2.0*p.x - 1.0, 2.0*p.y); }
			else if (p.y >= 0.5) { p = vec2(2.0*p.x, 2.0*p.y - 1.0); }
			else                 { p *= 2.0; }
		}
		col = (n < 0.0) ? vec3(0.95) : pal_col(n*0.045 + 0.06);

	} else if (iKind == 6) {
		//Sierpinski carpet: same CPU-assisted skip scheme on the unit square
		vec2 p = iIfsStart + uv * iIfsScale;
		float n = -1.0;
		for (int i = 0; i < MAX_ITER_IFS; ++i) {
			if (i >= iIters) { break; }
			if (abs(p.x) > 0.5 || abs(p.y) > 0.5) { n = float(iIfsSkip + i); break; }
			p *= 3.0;
			vec2 cell = clamp(floor(p + 0.5), -1.0, 1.0);
			if (cell.x == 0.0 && cell.y == 0.0) { n = float(iIfsSkip + i) + 1.0; break; }
			p -= cell;
		}
		col = (n < 0.0) ? vec3(0.95) : pal_col(n*0.045 + 0.06);

	} else if (iKind == 7) {
		//Koch snowflake: distance field via edge folds (float precision)
		vec2 p = pos;
		float scale = 1.0;
		p.x = abs(p.x);
		vec2 nrm = vec2(0.5, -0.8660254);          // fold into snowflake wedge
		p.y -= 0.28867513;
		float dd = dot(p - vec2(0.5, 0.0), nrm);
		p -= nrm * max(0.0, dd) * 2.0;
		nrm = vec2(0.8660254, -0.5);
		p.x += 0.5;
		for (int i = 0; i < MAX_ITER_KOCH; ++i) {
			if (i >= iIters) { break; }
			p *= 3.0; scale *= 3.0;
			p.x -= 1.5;
			p.x = abs(p.x) - 0.5;
			p -= nrm * min(0.0, dot(p, nrm)) * 2.0;
		}
		float d = length(p - vec2(clamp(p.x, -1.0, 1.0), 0.0)) / scale;
		float pxs = 2.0*iZoom / iResolution.y;
		col = mix(vec3(1.0), pal_col(log(d + 1e-20)*0.10 + 0.6)*0.8,
		          smoothstep(0.6*pxs, 1.8*pxs, d));

	} else if (iPerturb == 1) {
		//Deep zoom via perturbation (Mandelbrot & Julia): the CPU iterates a
		//reference orbit in 384-bit precision, each pixel iterates only its
		//tiny delta to that orbit. The delta is kept as u * 2^E (scaled
		//float), so any zoom depth fits into 32-bit floats.
		//Mandelbrot: delta' = (2Z+delta)*delta + delta_c, rebases onto its own
		//orbit (which starts at 0). Julia: delta' = (2Z+delta)*delta (c is the
		//same for all pixels), rebases onto the critical orbit (starts at 0).
		bool is_julia = (iKind == 4);
		vec2 dc = uv;             // true delta_c = uv * zeta, zeta = iZetaMan * 2^iZetaExp
		vec2 u = is_julia ? dc * iZetaMan : vec2(0.0);
		int E = iZetaExp;
		int mi = 0;
		int obase = 0;
		int ocount = iRefCount;
		float sn = -1.0;
		for (int i = 0; i < MAX_PERTURB_ITER; ++i) {
			if (i >= iIters) { break; }
			vec2 Zm = fetch_ref(obase + mi);
			float e2 = exp2(float(E));
			vec2 df = u * e2;
			vec2 z = Zm + df;
			float r2 = dot(z, z);
			if (r2 > 256.0) { sn = float(i) - log2(0.5*log2(r2)) + 4.0; break; }
			//Rebase (Zhuoran): when the full value is smaller than the delta,
			//or the reference orbit is about to run out (it escaped), restart
			//at the current full value on an orbit that begins at 0
			if (r2 < dot(df, df) || mi + 1 >= ocount) {
				u = z;
				E = 0;
				mi = 0;
				Zm = vec2(0.0);
				e2 = 1.0;
				if (is_julia) { obase = REF2_ENTRY_OFF; ocount = iRefCount2; }
			}
			//delta' = (2*Z + delta)*delta [+ delta_c]   (all divided by 2^E)
			vec2 w = 2.0*Zm + u*e2;
			u = cmul(w, u);
			if (!is_julia) { u += dc * (iZetaMan * exp2(float(iZetaExp - E))); }
			mi += 1;
			//Renormalize u into a safe range, tracking the exponent
			float L = max(abs(u.x), abs(u.y));
			if (L > 0.0) {
				float sh = floor(log2(L));
				u *= exp2(-sh);
				E += int(sh);
			}
		}
		col = (sn < 0.0) ? vec3(0.0) : pal_col(sn*0.017 + 0.15);

	} else if (iDeep == 1) {
		//Escape-time family, deep-zoom path with emulated double precision
		vec2 cxd = ds_add(iCenterX, ds(dxy.x));
		vec2 cyd = ds_add(iCenterY, ds(dxy.y));
		if (iKind == 1) { cyd = ds_neg(cyd); }
		vec2 zx, zy;
		if (iKind == 4) { zx = cxd; zy = cyd; cxd = ds(iJuliaC.x); cyd = ds(iJuliaC.y); }
		else { zx = ds(0.0); zy = ds(0.0); }
		float sn = -1.0;
		float r2 = 0.0;
		for (int i = 0; i < MAX_ITER_2D; ++i) {
			if (i >= iIters) { break; }
			if (iKind == 1) {
				if (zx.x < 0.0) { zx = ds_neg(zx); }
				if (zy.x < 0.0) { zy = ds_neg(zy); }
			} else if (iKind == 2) {
				zy = ds_neg(zy);
			}
			vec2 x2 = ds_mul(zx, zx);
			vec2 y2 = ds_mul(zy, zy);
			vec2 xy = ds_mul(zx, zy);
			zx = ds_add(ds_add(x2, ds_neg(y2)), cxd);
			zy = ds_add(ds_two(xy), cyd);
			r2 = zx.x*zx.x + zy.x*zy.x;
			if (r2 > 256.0) { sn = float(i) - log2(0.5*log2(r2)) + 4.0; break; }
		}
		col = (sn < 0.0) ? vec3(0.0) : pal_col(sn*0.017 + 0.15);

	} else {
		//Escape-time family, fast float path for shallow zooms
		if (iKind == 1) { pos.y = -pos.y; }
		vec2 z, c;
		if (iKind == 4) { z = pos; c = iJuliaC; }
		else { z = vec2(0.0); c = pos; }
		float sn = -1.0;
		float r2 = 0.0;
		for (int i = 0; i < MAX_ITER_2D; ++i) {
			if (i >= iIters) { break; }
			if (iKind == 1) { z = abs(z); }
			else if (iKind == 2) { z.y = -z.y; }
			z = cmul(z, z) + c;
			r2 = dot(z, z);
			if (r2 > 256.0) { sn = float(i) - log2(0.5*log2(r2)) + 4.0; break; }
		}
		col = (sn < 0.0) ? vec3(0.0) : pal_col(sn*0.017 + 0.15);
	}

	gl_FragColor = vec4(col, 1.0);
}
