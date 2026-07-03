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
#version 120
#define AMBIENT_OCCLUSION_COLOR_DELTA vec3(0.7)
#define AMBIENT_OCCLUSION_STRENGTH 0.008
#define ANTIALIASING_SAMPLES 2
#define BACKGROUND_COLOR vec3(0.6,0.8,1.0)
#define COL col_scene
#define DE de_scene
#define DIFFUSE_ENABLED 0
#define DIFFUSE_ENHANCED_ENABLED 1
#define FILTERING_ENABLE 0
// FOCAL_DIST removed — computed in main() from iFov uniform
#define FOG_ENABLED 0
#define MAX_FRACTAL_ITER 48
#define FRACTAL_TYPE_SEL 0
#define LIGHT_COLOR vec3(1.0,0.95,0.8)
#define LIGHT_DIRECTION vec3(-0.36, 0.8, 0.48)
#define MAX_DIST 30.0
#define MAX_MARCHES 1000
#define MIN_DIST 1e-6
#define PI 3.14159265358979
#define SHADOWS_ENABLED 1
#define SHADOW_DARKNESS 0.7
#define SHADOW_SHARPNESS 10.0
#define SPECULAR_HIGHLIGHT 40
#define SPECULAR_MULT 0.25
#define SUN_ENABLED 1
#define SUN_SHARPNESS 2.0
#define SUN_SIZE 0.004
#define VIGNETTE_STRENGTH 0.5

uniform mat4 iMat;
uniform vec2 iResolution;
uniform vec3 iDebug;

uniform float iFracScale;
uniform float iFracAng1;
uniform float iFracAng2;
uniform vec3 iFracShift;
uniform vec3 iFracCol;
uniform vec3 iMarblePos;
uniform float iMarbleRad;
uniform float iFlagScale;
uniform vec3 iFlagPos;
uniform float iExposure;
uniform float iFov;      // vertical field of view in degrees (5-170)
uniform int iFracIter;   // fractal fold iterations (raised adaptively in free-fly)
uniform float iTime;     // seconds since start (drives 4D tesseract rotation)

float FOVperPixel;

vec3 refraction(vec3 rd, vec3 n, float p) {
	float dot_nd = dot(rd, n);
	return p * (rd - dot_nd * n) + sqrt(1.0 - (p * p) * (1.0 - dot_nd * dot_nd)) * n;
}

//##########################################
//   Space folding
//##########################################
void planeFold(inout vec4 z, vec3 n, float d) {
	z.xyz -= 2.0 * min(0.0, dot(z.xyz, n) - d) * n;
}
void sierpinskiFold(inout vec4 z) {
	z.xy -= min(z.x + z.y, 0.0);
	z.xz -= min(z.x + z.z, 0.0);
	z.yz -= min(z.y + z.z, 0.0);
}
void mengerFold(inout vec4 z) {
	float a = min(z.x - z.y, 0.0);
	z.x -= a;
	z.y += a;
	a = min(z.x - z.z, 0.0);
	z.x -= a;
	z.z += a;
	a = min(z.y - z.z, 0.0);
	z.y -= a;
	z.z += a;
}
void boxFold(inout vec4 z, vec3 r) {
	z.xyz = clamp(z.xyz, -r, r) * 2.0 - z.xyz;
}
void icosaFold(inout vec4 p) {
	const float phi = 1.6180339887;
	const float ni = 0.52573111; // 1/sqrt(1+phi^2)
	vec3 k1 = vec3(ni, phi*ni, 0.0);
	vec3 k2 = vec3(0.0, ni, phi*ni);
	vec3 k3 = vec3(phi*ni, 0.0, ni);
	float d;
	d = dot(p.xyz, k1); if (d < 0.0) p.xyz -= 2.0*d*k1;
	d = dot(p.xyz, k2); if (d < 0.0) p.xyz -= 2.0*d*k2;
	d = dot(p.xyz, k3); if (d < 0.0) p.xyz -= 2.0*d*k3;
	d = dot(p.xyz, k1); if (d < 0.0) p.xyz -= 2.0*d*k1;
	d = dot(p.xyz, k2); if (d < 0.0) p.xyz -= 2.0*d*k2;
}
void rotX(inout vec4 z, float s, float c) {
	z.yz = vec2(c*z.y + s*z.z, c*z.z - s*z.y);
}
void rotY(inout vec4 z, float s, float c) {
	z.xz = vec2(c*z.x - s*z.z, c*z.z + s*z.x);
}
void rotZ(inout vec4 z, float s, float c) {
	z.xy = vec2(c*z.x + s*z.y, c*z.y - s*z.x);
}
void rotX(inout vec4 z, float a) {
	rotX(z, sin(a), cos(a));
}
void rotY(inout vec4 z, float a) {
	rotY(z, sin(a), cos(a));
}
void rotZ(inout vec4 z, float a) {
	rotZ(z, sin(a), cos(a));
}
//4D rotations: each spins one spatial axis against the 4th (w) axis.
void rotXW(inout vec4 z, float a) {
	float s = sin(a), c = cos(a);
	z.xw = vec2(c*z.x - s*z.w, c*z.w + s*z.x);
}
void rotYW(inout vec4 z, float a) {
	float s = sin(a), c = cos(a);
	z.yw = vec2(c*z.y - s*z.w, c*z.w + s*z.y);
}
void rotZW(inout vec4 z, float a) {
	float s = sin(a), c = cos(a);
	z.zw = vec2(c*z.z - s*z.w, c*z.w + s*z.z);
}

//##########################################
//   Primitive DEs
//##########################################
float de_sphere(vec4 p, float r) {
	return (length(p.xyz) - r) / p.w;
}
float de_box(vec4 p, vec3 s) {
	vec3 a = abs(p.xyz) - s;
	return (min(max(max(a.x, a.y), a.z), 0.0) + length(max(a, 0.0))) / p.w;
}
float de_tetrahedron(vec4 p, float r) {
	float md = max(max(-p.x - p.y - p.z, p.x + p.y - p.z),
				max(-p.x + p.y + p.z, p.x - p.y + p.z));
	return (md - r) / (p.w * sqrt(3.0));
}
float de_capsule(vec4 p, float h, float r) {
	p.y -= clamp(p.y, -h, h);
	return (length(p.xyz) - r) / p.w;
}
//Point-to-line-segment distance (round tube of radius r between a and b).
float de_segment(vec3 p, vec3 a, vec3 b, float r) {
	vec3 pa = p - a;
	vec3 ba = b - a;
	float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-8), 0.0, 1.0);
	return length(pa - ba*h) - r;
}
//Smooth minimum (iq): blends two surfaces over a width k.
float smin(float a, float b, float k) {
	float h = clamp(0.5 + 0.5*(b - a)/k, 0.0, 1.0);
	return mix(b, a, h) - k*h*(1.0 - h);
}

//##########################################
//   Main DEs
//##########################################
float de_fractal(vec4 p) {
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p.xyz = abs(p.xyz);
		rotZ(p, iFracAng1);
		mengerFold(p);
		rotX(p, iFracAng2);
		p *= iFracScale;
		p.xyz += iFracShift;
	}
	return de_box(p, vec3(6.0));
}
vec4 col_fractal(vec4 p) {
	vec3 orbit = vec3(0.0);
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p.xyz = abs(p.xyz);
		rotZ(p, iFracAng1);
		mengerFold(p);
		rotX(p, iFracAng2);
		p *= iFracScale;
		p.xyz += iFracShift;
		orbit = max(orbit, p.xyz*iFracCol);
	}
	return vec4(orbit, de_box(p, vec3(6.0)));
}
float de_mandelbox(vec4 p) {
	float s = iFracScale;
	vec3 c = p.xyz + iFracShift;
	float dr = 1.0;
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p.xyz = clamp(p.xyz, -1.0, 1.0) * 2.0 - p.xyz;
		float r2 = dot(p.xyz, p.xyz);
		if (r2 < 0.25) { p.xyz *= 4.0; dr *= 4.0; }
		else if (r2 < 1.0) { float k = 1.0/r2; p.xyz *= k; dr *= k; }
		p.xyz = p.xyz * s + c;
		dr = dr * abs(s) + 1.0;
	}
	return (length(p.xyz) - abs(abs(s) - 1.0)) / dr;
}
vec4 col_mandelbox(vec4 p) {
	float s = iFracScale;
	vec3 c = p.xyz + iFracShift;
	float dr = 1.0;
	vec3 orbit = vec3(0.0);
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p.xyz = clamp(p.xyz, -1.0, 1.0) * 2.0 - p.xyz;
		float r2 = dot(p.xyz, p.xyz);
		if (r2 < 0.25) { p.xyz *= 4.0; dr *= 4.0; }
		else if (r2 < 1.0) { float k = 1.0/r2; p.xyz *= k; dr *= k; }
		p.xyz = p.xyz * s + c;
		dr = dr * abs(s) + 1.0;
		orbit = max(orbit, abs(p.xyz) * iFracCol);
	}
	return vec4(orbit, (length(p.xyz) - abs(abs(s) - 1.0)) / dr);
}
float de_octahedral(vec4 p) {
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p.xyz = abs(p.xyz);
		planeFold(p, vec3(0.7071, 0.7071, 0.0), 0.0);
		planeFold(p, vec3(0.7071, 0.0, 0.7071), 0.0);
		planeFold(p, vec3(0.0, 0.7071, 0.7071), 0.0);
		p *= iFracScale;
		p.xyz += iFracShift;
	}
	return de_tetrahedron(p, 1.0);
}
vec4 col_octahedral(vec4 p) {
	vec3 orbit = vec3(0.0);
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p.xyz = abs(p.xyz);
		planeFold(p, vec3(0.7071, 0.7071, 0.0), 0.0);
		planeFold(p, vec3(0.7071, 0.0, 0.7071), 0.0);
		planeFold(p, vec3(0.0, 0.7071, 0.7071), 0.0);
		p *= iFracScale;
		p.xyz += iFracShift;
		orbit = max(orbit, abs(p.xyz) * iFracCol);
	}
	return vec4(orbit, de_tetrahedron(p, 1.0));
}
float de_sierpinski_tet(vec4 p) {
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		sierpinskiFold(p);
		p *= iFracScale;
		p.xyz += iFracShift;
	}
	return de_tetrahedron(p, 1.0);
}
vec4 col_sierpinski_tet(vec4 p) {
	vec3 orbit = vec3(0.0);
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		sierpinskiFold(p);
		p *= iFracScale;
		p.xyz += iFracShift;
		orbit = max(orbit, abs(p.xyz) * iFracCol);
	}
	return vec4(orbit, de_tetrahedron(p, 1.0));
}
float de_marble(vec4 p) {
	return de_sphere(p - vec4(iMarblePos, 0), iMarbleRad);
}
vec4 col_marble(vec4 p) {
	return vec4(0, 0, 0, de_sphere(p - vec4(iMarblePos, 0), iMarbleRad));
}
float de_flag(vec4 p) {
	vec3 f_pos = iFlagPos + vec3(1.5, 4, 0)*iFlagScale;
	float d = de_box(p - vec4(f_pos, 0), vec3(1.5, 0.8, 0.08)*iMarbleRad);
	d = min(d, de_capsule(p - vec4(iFlagPos + vec3(0, iFlagScale*2.4, 0), 0), iMarbleRad*2.4, iMarbleRad*0.18));
	return d;
}
vec4 col_flag(vec4 p) {
	vec3 f_pos = iFlagPos + vec3(1.5, 4, 0)*iFlagScale;
	float d1 = de_box(p - vec4(f_pos, 0), vec3(1.5, 0.8, 0.08)*iMarbleRad);
	float d2 = de_capsule(p - vec4(iFlagPos + vec3(0, iFlagScale*2.4, 0), 0), iMarbleRad*2.4, iMarbleRad*0.18);
	if (d1 < d2) {
		return vec4(1.0, 0.2, 0.1, d1);
	} else {
		return vec4(0.9, 0.9, 0.1, d2);
	}
}
float de_mandelbulb(vec4 p) {
	float n = iFracScale;
	vec3 z = p.xyz;
	float dr = 1.0, r = 0.0;
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		r = length(z);
		if (r > 2.0) break;
		float theta = acos(clamp(z.z/r, -1.0, 1.0));
		float phi = atan(z.y, z.x);
		dr = pow(r, n-1.0) * n * dr + 1.0;
		float zr = pow(r, n);
		theta *= n; phi *= n;
		z = zr * vec3(sin(theta)*cos(phi), sin(phi)*sin(theta), cos(theta));
		z += p.xyz;
	}
	r = length(z);
	return 0.5 * log(r) * r / dr;
}
vec4 col_mandelbulb(vec4 p) {
	float n = iFracScale;
	vec3 z = p.xyz;
	float dr = 1.0, r = 0.0;
	vec3 orbit = vec3(0.0);
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		r = length(z);
		if (r > 2.0) break;
		float theta = acos(clamp(z.z/r, -1.0, 1.0));
		float phi = atan(z.y, z.x);
		dr = pow(r, n-1.0) * n * dr + 1.0;
		float zr = pow(r, n);
		theta *= n; phi *= n;
		z = zr * vec3(sin(theta)*cos(phi), sin(phi)*sin(theta), cos(theta));
		z += p.xyz;
		orbit = max(orbit, abs(z) * iFracCol);
	}
	r = length(z);
	return vec4(orbit, 0.5 * log(r) * r / dr);
}
float de_icosahedral(vec4 p) {
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p.xyz = abs(p.xyz);
		icosaFold(p);
		p *= iFracScale;
		p.xyz += iFracShift;
	}
	return de_sphere(p, 1.0);
}
vec4 col_icosahedral(vec4 p) {
	vec3 orbit = vec3(0.0);
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p.xyz = abs(p.xyz);
		icosaFold(p);
		p *= iFracScale;
		p.xyz += iFracShift;
		orbit = max(orbit, abs(p.xyz) * iFracCol);
	}
	return vec4(orbit, de_sphere(p, 1.0));
}
// Quaternion multiply: a * b
vec4 qmul(vec4 a, vec4 b) {
	return vec4(
		a.x*b.x - a.y*b.y - a.z*b.z - a.w*b.w,
		a.x*b.y + a.y*b.x + a.z*b.w - a.w*b.z,
		a.x*b.z - a.y*b.w + a.z*b.x + a.w*b.y,
		a.x*b.w + a.y*b.z - a.z*b.y + a.w*b.x
	);
}
float de_quaternion_julia(vec4 p) {
	vec4 z = vec4(p.xyz, 0.0);
	vec4 c = vec4(iFracShift, iFracAng1);
	vec4 dz = vec4(1.0, 0.0, 0.0, 0.0);
	float r2 = 1.0;
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		dz = 2.0 * qmul(z, dz);
		z = vec4(z.x*z.x - z.y*z.y - z.z*z.z - z.w*z.w,
		         2.0*z.x*z.y, 2.0*z.x*z.z, 2.0*z.x*z.w) + c;
		r2 = dot(z, z);
		if (r2 > 4.0) break;
	}
	float dr2 = max(dot(dz, dz), 1e-12);
	return 0.5 * sqrt(r2 / dr2) * log(max(r2, 1.001));
}
vec4 col_quaternion_julia(vec4 p) {
	vec4 z = vec4(p.xyz, 0.0);
	vec4 c = vec4(iFracShift, iFracAng1);
	vec4 dz = vec4(1.0, 0.0, 0.0, 0.0);
	vec3 orbit = vec3(0.0);
	float r2 = 1.0;
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		dz = 2.0 * qmul(z, dz);
		z = vec4(z.x*z.x - z.y*z.y - z.z*z.z - z.w*z.w,
		         2.0*z.x*z.y, 2.0*z.x*z.z, 2.0*z.x*z.w) + c;
		r2 = dot(z, z);
		orbit = max(orbit, abs(z.xyz) * iFracCol);
		if (r2 > 4.0) break;
	}
	float dr2 = max(dot(dz, dz), 1e-12);
	float d = 0.5 * sqrt(r2 / dr2) * log(max(r2, 1.001));
	return vec4(orbit, d);
}
float de_mandelbox_julia(vec4 p) {
	float s = iFracScale;
	vec3 c = iFracShift;
	float dr = 1.0;
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p.xyz = clamp(p.xyz, -1.0, 1.0) * 2.0 - p.xyz;
		float r2 = dot(p.xyz, p.xyz);
		if (r2 < 0.25) { p.xyz *= 4.0; dr *= 4.0; }
		else if (r2 < 1.0) { float k = 1.0/r2; p.xyz *= k; dr *= k; }
		p.xyz = p.xyz * s + c;
		dr = dr * abs(s) + 1.0;
	}
	return (length(p.xyz) - abs(abs(s) - 1.0)) / dr;
}
vec4 col_mandelbox_julia(vec4 p) {
	float s = iFracScale;
	vec3 c = iFracShift;
	float dr = 1.0;
	vec3 orbit = vec3(0.0);
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p.xyz = clamp(p.xyz, -1.0, 1.0) * 2.0 - p.xyz;
		float r2 = dot(p.xyz, p.xyz);
		if (r2 < 0.25) { p.xyz *= 4.0; dr *= 4.0; }
		else if (r2 < 1.0) { float k = 1.0/r2; p.xyz *= k; dr *= k; }
		p.xyz = p.xyz * s + c;
		dr = dr * abs(s) + 1.0;
		orbit = max(orbit, abs(p.xyz) * iFracCol);
	}
	return vec4(orbit, (length(p.xyz) - abs(abs(s) - 1.0)) / dr);
}
float de_apollonian(vec4 q) {
	vec3 p = q.xyz;
	float scale = 1.0;
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p = -1.0 + 2.0*fract(0.5*p + 0.5);
		float r2 = dot(p, p);
		float k = iFracScale/r2;
		p *= k;
		scale *= k;
	}
	return 0.25*abs(p.y)/scale;
}
vec4 col_apollonian(vec4 q) {
	vec3 p = q.xyz;
	float scale = 1.0;
	float trap = 1e10;
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p = -1.0 + 2.0*fract(0.5*p + 0.5);
		float r2 = dot(p, p);
		float k = iFracScale/r2;
		p *= k;
		scale *= k;
		trap = min(trap, r2);
	}
	vec3 orbit = iFracCol * (0.35 + 2.5*trap);
	return vec4(orbit, 0.25*abs(p.y)/scale);
}
float de_pseudo_kleinian(vec4 q) {
	vec3 csize = abs(iFracShift);
	vec3 p = q.xyz;
	float scale = 1.0;
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p = 2.0*clamp(p, -csize, csize) - p;
		float r2 = dot(p, p);
		float k = max(iFracScale/r2, 1.0);
		p *= k;
		scale *= k;
	}
	return 0.45*abs(p.y)/scale;
}
vec4 col_pseudo_kleinian(vec4 q) {
	vec3 csize = abs(iFracShift);
	vec3 p = q.xyz;
	float scale = 1.0;
	float trap = 1e10;
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p = 2.0*clamp(p, -csize, csize) - p;
		float r2 = dot(p, p);
		float k = max(iFracScale/r2, 1.0);
		p *= k;
		scale *= k;
		trap = min(trap, r2);
	}
	vec3 orbit = iFracCol * (0.25 + 1.2*trap);
	return vec4(orbit, 0.45*abs(p.y)/scale);
}
float de_juliabulb(vec4 p) {
	float n = iFracScale;
	vec3 z = p.xyz;
	float dr = 1.0, r = length(z);
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		r = length(z);
		if (r > 2.0) break;
		float theta = acos(clamp(z.z/r, -1.0, 1.0));
		float phi = atan(z.y, z.x);
		dr = pow(r, n-1.0) * n * dr;
		float zr = pow(r, n);
		theta *= n; phi *= n;
		z = zr * vec3(sin(theta)*cos(phi), sin(phi)*sin(theta), cos(theta));
		z += iFracShift;
	}
	r = length(z);
	return 0.5 * log(max(r, 1e-6)) * r / max(dr, 1e-9);
}
vec4 col_juliabulb(vec4 p) {
	float n = iFracScale;
	vec3 z = p.xyz;
	float dr = 1.0, r = length(z);
	vec3 orbit = vec3(0.0);
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		r = length(z);
		if (r > 2.0) break;
		float theta = acos(clamp(z.z/r, -1.0, 1.0));
		float phi = atan(z.y, z.x);
		dr = pow(r, n-1.0) * n * dr;
		float zr = pow(r, n);
		theta *= n; phi *= n;
		z = zr * vec3(sin(theta)*cos(phi), sin(phi)*sin(theta), cos(theta));
		z += iFracShift;
		orbit = max(orbit, abs(z) * iFracCol);
	}
	r = length(z);
	return vec4(orbit, 0.5 * log(max(r, 1e-6)) * r / max(dr, 1e-9));
}
float de_menger_mandelbox(vec4 p) {
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p.xyz = abs(p.xyz);
		mengerFold(p);
		boxFold(p, vec3(1.0));
		float r2 = dot(p.xyz, p.xyz);
		if (r2 < 0.25) { p *= 4.0; }
		else if (r2 < 1.0) { p /= r2; }
		p *= iFracScale;
		p.xyz += iFracShift;
	}
	return de_box(p, vec3(6.0));
}
vec4 col_menger_mandelbox(vec4 p) {
	vec3 orbit = vec3(0.0);
	for (int i = 0; i < MAX_FRACTAL_ITER; ++i) { if (i >= iFracIter) break;
		p.xyz = abs(p.xyz);
		mengerFold(p);
		boxFold(p, vec3(1.0));
		float r2 = dot(p.xyz, p.xyz);
		if (r2 < 0.25) { p *= 4.0; }
		else if (r2 < 1.0) { p /= r2; }
		p *= iFracScale;
		p.xyz += iFracShift;
		orbit = max(orbit, abs(p.xyz) * iFracCol);
	}
	return vec4(orbit, de_box(p, vec3(6.0)));
}

//##########################################
//   Non-fractal math objects
//##########################################

//Rotate one tesseract vertex through 4D and project it to 3D. All 16 vertices
//share this transform, so it depends only on iTime/params, not on the query
//point — cheap to recompute per DE call.
vec3 tessVert(vec4 v) {
	float t = iTime;
	rotXW(v, t * 0.37 * iFracAng1);
	rotYW(v, t * 0.29 * iFracAng2);
	rotZW(v, t * 0.21);
	rotZ(v,  t * 0.15);
	float wd = max(3.0 + iFracShift.x, 2.2);   //4D camera distance (>2 avoids singularity)
	float pr = wd / (wd - v.w);                //perspective projection to 3D
	return v.xyz * pr;
}
float de_tesseract(vec4 p) {
	float sc = iFracScale;
	vec3 q = p.xyz / sc;
	vec3 V[16];
	V[0]  = tessVert(vec4(-1.0,-1.0,-1.0,-1.0));
	V[1]  = tessVert(vec4( 1.0,-1.0,-1.0,-1.0));
	V[2]  = tessVert(vec4(-1.0, 1.0,-1.0,-1.0));
	V[3]  = tessVert(vec4( 1.0, 1.0,-1.0,-1.0));
	V[4]  = tessVert(vec4(-1.0,-1.0, 1.0,-1.0));
	V[5]  = tessVert(vec4( 1.0,-1.0, 1.0,-1.0));
	V[6]  = tessVert(vec4(-1.0, 1.0, 1.0,-1.0));
	V[7]  = tessVert(vec4( 1.0, 1.0, 1.0,-1.0));
	V[8]  = tessVert(vec4(-1.0,-1.0,-1.0, 1.0));
	V[9]  = tessVert(vec4( 1.0,-1.0,-1.0, 1.0));
	V[10] = tessVert(vec4(-1.0, 1.0,-1.0, 1.0));
	V[11] = tessVert(vec4( 1.0, 1.0,-1.0, 1.0));
	V[12] = tessVert(vec4(-1.0,-1.0, 1.0, 1.0));
	V[13] = tessVert(vec4( 1.0,-1.0, 1.0, 1.0));
	V[14] = tessVert(vec4(-1.0, 1.0, 1.0, 1.0));
	V[15] = tessVert(vec4( 1.0, 1.0, 1.0, 1.0));
	float r = 0.03;
	//32 edges: each connects two vertices differing in exactly one bit
	float d = de_segment(q, V[0], V[1], r);
	d = min(d, de_segment(q, V[2], V[3], r));
	d = min(d, de_segment(q, V[4], V[5], r));
	d = min(d, de_segment(q, V[6], V[7], r));
	d = min(d, de_segment(q, V[8], V[9], r));
	d = min(d, de_segment(q, V[10], V[11], r));
	d = min(d, de_segment(q, V[12], V[13], r));
	d = min(d, de_segment(q, V[14], V[15], r));
	d = min(d, de_segment(q, V[0], V[2], r));
	d = min(d, de_segment(q, V[1], V[3], r));
	d = min(d, de_segment(q, V[4], V[6], r));
	d = min(d, de_segment(q, V[5], V[7], r));
	d = min(d, de_segment(q, V[8], V[10], r));
	d = min(d, de_segment(q, V[9], V[11], r));
	d = min(d, de_segment(q, V[12], V[14], r));
	d = min(d, de_segment(q, V[13], V[15], r));
	d = min(d, de_segment(q, V[0], V[4], r));
	d = min(d, de_segment(q, V[1], V[5], r));
	d = min(d, de_segment(q, V[2], V[6], r));
	d = min(d, de_segment(q, V[3], V[7], r));
	d = min(d, de_segment(q, V[8], V[12], r));
	d = min(d, de_segment(q, V[9], V[13], r));
	d = min(d, de_segment(q, V[10], V[14], r));
	d = min(d, de_segment(q, V[11], V[15], r));
	d = min(d, de_segment(q, V[0], V[8], r));
	d = min(d, de_segment(q, V[1], V[9], r));
	d = min(d, de_segment(q, V[2], V[10], r));
	d = min(d, de_segment(q, V[3], V[11], r));
	d = min(d, de_segment(q, V[4], V[12], r));
	d = min(d, de_segment(q, V[5], V[13], r));
	d = min(d, de_segment(q, V[6], V[14], r));
	d = min(d, de_segment(q, V[7], V[15], r));
	return d * sc;
}
vec4 col_tesseract(vec4 p) {
	return vec4(iFracCol, de_tesseract(p));
}

//Möbius strip: undo the half-turn twist that accumulates around the loop,
//then measure a flat 2D band cross-section. One trip around = one half flip,
//which is exactly the Möbius identification.
float de_mobius(vec4 p) {
	float sc = iFracScale;
	vec3 q = p.xyz / sc;
	float ang = atan(q.y, q.x);
	float rad = length(q.xy) - 1.0;
	float ca = cos(ang*0.5), sa = sin(ang*0.5);
	float u =  ca*rad + sa*q.z;   //across the band width
	float v = -sa*rad + ca*q.z;   //through the band thickness
	vec2 d2 = abs(vec2(u, v)) - vec2(iFracAng1, iFracAng2);
	float d = length(max(d2, 0.0)) + min(max(d2.x, d2.y), 0.0);
	return d * sc * 0.7;   //0.7: safety factor against the twist's stretch
}
vec4 col_mobius(vec4 p) {
	vec3 q = p.xyz / iFracScale;
	float ang = atan(q.y, q.x);
	return vec4(iFracCol * (0.6 + 0.4*sin(ang*3.0)), de_mobius(p));
}

//Klein bottle (classic "bottle" immersion): a hollow bulb plus a neck tube
//that hooks over and plunges back through the wall into the interior. The
//self-intersection is unavoidable in 3D and mathematically correct.
float de_klein(vec4 p) {
	float sc = iFracScale;
	vec3 q = p.xyz / sc;
	//Bulb: slightly squashed hollow sphere shell
	vec3 bp = q - vec3(0.0, -0.2, 0.0);
	bp.y *= 0.85;
	float bulb = abs(length(bp) - 0.9) - 0.05;
	//Neck: a hooked tube from the top, over, down, and back into the bulb
	float tr = iFracAng2;     //tube radius
	float hk = iFracAng1;     //how far the hook reaches out
	vec3 a = vec3(0.0,       0.62, 0.0);
	vec3 b = vec3(0.0,       1.50, 0.0);
	vec3 c = vec3(hk,        1.70, 0.0);
	vec3 e = vec3(hk,        0.20, 0.0);
	vec3 f = vec3(hk*0.2,   -0.55, 0.0);
	float neck = de_segment(q, a, b, tr);
	neck = min(neck, de_segment(q, b, c, tr));
	neck = min(neck, de_segment(q, c, e, tr));
	neck = min(neck, de_segment(q, e, f, tr));
	return smin(bulb, neck, 0.08) * sc;
}
vec4 col_klein(vec4 p) {
	vec3 q = p.xyz / iFracScale;
	return vec4(iFracCol * (0.55 + 0.45*clamp(q.y*0.5 + 0.5, 0.0, 1.0)), de_klein(p));
}

//The active DE is selected at compile time. FRACTAL_TYPE_SEL is injected by
//the game when the shader is built; unused formulas are dead-code-stripped.
float de_scene(vec4 p) {
#if FRACTAL_TYPE_SEL == 1
	float d = de_sierpinski_tet(p);
#elif FRACTAL_TYPE_SEL == 2
	float d = de_mandelbox(p);
#elif FRACTAL_TYPE_SEL == 3
	float d = de_octahedral(p);
#elif FRACTAL_TYPE_SEL == 4
	float d = de_quaternion_julia(p);
#elif FRACTAL_TYPE_SEL == 5
	float d = de_mandelbox_julia(p);
#elif FRACTAL_TYPE_SEL == 6
	float d = de_mandelbulb(p);
#elif FRACTAL_TYPE_SEL == 7
	float d = de_icosahedral(p);
#elif FRACTAL_TYPE_SEL == 8
	float d = de_apollonian(p);
#elif FRACTAL_TYPE_SEL == 9
	float d = de_pseudo_kleinian(p);
#elif FRACTAL_TYPE_SEL == 10
	float d = de_juliabulb(p);
#elif FRACTAL_TYPE_SEL == 11
	float d = de_menger_mandelbox(p);
#elif FRACTAL_TYPE_SEL == 12
	float d = de_tesseract(p);
#elif FRACTAL_TYPE_SEL == 13
	float d = de_mobius(p);
#elif FRACTAL_TYPE_SEL == 14
	float d = de_klein(p);
#else
	float d = de_fractal(p);
#endif
	d = min(d, de_marble(p));
	d = min(d, de_flag(p));
	return d;
}
vec4 col_scene(vec4 p) {
#if FRACTAL_TYPE_SEL == 1
	vec4 col = col_sierpinski_tet(p);
#elif FRACTAL_TYPE_SEL == 2
	vec4 col = col_mandelbox(p);
#elif FRACTAL_TYPE_SEL == 3
	vec4 col = col_octahedral(p);
#elif FRACTAL_TYPE_SEL == 4
	vec4 col = col_quaternion_julia(p);
#elif FRACTAL_TYPE_SEL == 5
	vec4 col = col_mandelbox_julia(p);
#elif FRACTAL_TYPE_SEL == 6
	vec4 col = col_mandelbulb(p);
#elif FRACTAL_TYPE_SEL == 7
	vec4 col = col_icosahedral(p);
#elif FRACTAL_TYPE_SEL == 8
	vec4 col = col_apollonian(p);
#elif FRACTAL_TYPE_SEL == 9
	vec4 col = col_pseudo_kleinian(p);
#elif FRACTAL_TYPE_SEL == 10
	vec4 col = col_juliabulb(p);
#elif FRACTAL_TYPE_SEL == 11
	vec4 col = col_menger_mandelbox(p);
#elif FRACTAL_TYPE_SEL == 12
	vec4 col = col_tesseract(p);
#elif FRACTAL_TYPE_SEL == 13
	vec4 col = col_mobius(p);
#elif FRACTAL_TYPE_SEL == 14
	vec4 col = col_klein(p);
#else
	vec4 col = col_fractal(p);
#endif
	vec4 col_f = col_flag(p);
	if (col_f.w < col.w) { col = col_f; }
	vec4 col_m = col_marble(p);
	if (col_m.w < col.w) {
		return vec4(col_m.xyz, 1.0);
	}
	return vec4(col.xyz, 0.0);
}

//##########################################
//   Main code
//##########################################

//A faster formula to find the gradient/normal direction of the DE(the w component is the average DE)
//credit to http://www.iquilezles.org/www/articles/normalsSDF/normalsSDF.htm
vec3 calcNormal(vec4 p, float dx) {
	const vec3 k = vec3(1,-1,0);
	return normalize(k.xyy*DE(p + k.xyyz*dx) +
					 k.yyx*DE(p + k.yyxz*dx) +
					 k.yxy*DE(p + k.yxyz*dx) +
					 k.xxx*DE(p + k.xxxz*dx));
}

//find the average color of the fractal in a radius dx in plane s1-s2
vec4 smoothColor(vec4 p, vec3 s1, vec3 s2, float dx) {
	return (COL(p + vec4(s1,0)*dx) +
			COL(p - vec4(s1,0)*dx) +
			COL(p + vec4(s2,0)*dx) +
			COL(p - vec4(s2,0)*dx))/4;
}

vec4 ray_march(inout vec4 p, vec4 ray, float sharpness) {
	//March the ray
	float d = DE(p);
	if (d < 0.0 && sharpness == 1.0) {
		vec3 v;
		if (abs(iMarblePos.x) >= 999.0f) {
			v = (-20.0 * iMarbleRad) * iMat[2].xyz;
		} else {
			v = iMarblePos.xyz - iMat[3].xyz;
		}
		d = dot(v, v) / dot(v, ray.xyz) - iMarbleRad;
	}
	float s = 0.0;
	float td = 0.0;
	float min_d = 1.0;
	for (; s < MAX_MARCHES; s += 1.0) {
		//if the distance from the surface is less than the distance per pixel we stop
		float min_dist = max(FOVperPixel*td, MIN_DIST);
		if (d < min_dist) {
			s += d / min_dist;
			break;
		} else if (td > MAX_DIST) {
			break;
		}
		td += d;
		p += ray * d;
		min_d = min(min_d, sharpness * d / td);
		d = DE(p);
	}
	return vec4(d, s, td, min_d);
}

vec4 scene(inout vec4 p, inout vec4 ray, float vignette) {
	//Trace the ray
	vec4 d_s_td_m = ray_march(p, ray, 1.0f);
	float d = d_s_td_m.x;
	float s = d_s_td_m.y;
	float td = d_s_td_m.z;

	//Determine the color for this pixel
	vec4 col = vec4(0.0);
	float min_dist = max(FOVperPixel*td, MIN_DIST);
	if (d < min_dist) {
		//Get the surface normal
		vec3 n = calcNormal(p, min_dist*0.5);
		
		//find closest surface point, without this we get weird coloring artifacts
		p.xyz -= n*d;

		//Get coloring
		#if FILTERING_ENABLE
			//sample direction 1, the cross product between the ray and the surface normal, should be parallel to the surface
			vec3 s1 = normalize(cross(ray.xyz, n));
			//sample direction 2, the cross product between s1 and the surface normal
			vec3 s2 = cross(s1, n);
			//get filtered color
			vec4 orig_col = clamp(smoothColor(p, s1, s2, min_dist*0.5), 0.0, 1.0);
		#else
			vec4 orig_col = clamp(COL(p), 0.0, 1.0);
		#endif
		col.w = orig_col.w;

		//Get if this point is in shadow
		float k = 1.0;
		#if SHADOWS_ENABLED
			vec4 light_pt = p;
			light_pt.xyz += n * MIN_DIST * 100;
			vec4 rm = ray_march(light_pt, vec4(LIGHT_DIRECTION, 0.0), SHADOW_SHARPNESS);
			k = rm.w * min(rm.z, 1.0);
		#endif

		//Get specular
		#if SPECULAR_HIGHLIGHT > 0
			vec3 reflected = ray.xyz - 2.0*dot(ray.xyz, n) * n;
			float specular = max(dot(reflected, LIGHT_DIRECTION), 0.0);
			specular = pow(specular, SPECULAR_HIGHLIGHT);
			col.xyz += specular * LIGHT_COLOR * (k * SPECULAR_MULT);
		#endif

		//Get diffuse lighting
		#if DIFFUSE_ENHANCED_ENABLED
			k = min(k, SHADOW_DARKNESS * 0.5 * (dot(n, LIGHT_DIRECTION) - 1.0) + 1.0);
		#elif DIFFUSE_ENABLED
			k = min(k, dot(n, LIGHT_DIRECTION));
		#endif

		//Don't make shadows entirely dark
		k = max(k, 1.0 - SHADOW_DARKNESS);
		col.xyz += orig_col.xyz * LIGHT_COLOR * k;

		//Add small amount of ambient occlusion
		float a = 1.0 / (1.0 + s * AMBIENT_OCCLUSION_STRENGTH);
		col.xyz += (1.0 - a) * AMBIENT_OCCLUSION_COLOR_DELTA;

		//Add fog effects
		#if FOG_ENABLED
			a = td / MAX_DIST;
			col.xyz = (1.0 - a) * col.xyz + a * BACKGROUND_COLOR;
		#endif

		//Return normal through ray
		ray = vec4(n, 0.0);
	} else {
		//Ray missed, start with solid background color
		col.xyz += BACKGROUND_COLOR;

		col.xyz *= vignette;
		//Background specular
		#if SUN_ENABLED
			float sun_spec = dot(ray.xyz, LIGHT_DIRECTION) - 1.0 + SUN_SIZE;
			sun_spec = min(exp(sun_spec * SUN_SHARPNESS / SUN_SIZE), 1.0);
			col.xyz += LIGHT_COLOR * sun_spec;
		#endif
	}

	return col;
}

void main() {
	// Focal distance from vertical FOV (degrees)
	float focal_dist = 1.0 / tan(iFov * (PI / 360.0));

	// Angle per pixel — scales with FOV so ray march quality adapts
	FOVperPixel = tan(iFov * (PI / 360.0)) / max(iResolution.x, 900.0);

	vec3 col = vec3(0.0);
	for (int i = 0; i < ANTIALIASING_SAMPLES; ++i) {
		for (int j = 0; j < ANTIALIASING_SAMPLES; ++j) {
			//Get normalized screen coordinate
			vec2 delta = vec2(i, j) / ANTIALIASING_SAMPLES;
			vec2 screen_pos = (gl_FragCoord.xy + delta) / iResolution.xy;

			vec2 uv = 2*screen_pos - 1;
			uv.x *= iResolution.x / iResolution.y;

			//Convert screen coordinate to 3d ray
			vec4 ray = iMat * normalize(vec4(uv.x, uv.y, -focal_dist, 0.0));
			vec4 p = iMat[3];

			//Reflect light if needed
			float vignette = 1.0 - VIGNETTE_STRENGTH * length(screen_pos - 0.5);
			vec3 r = ray.xyz;
			vec4 col_r = scene(p, ray, vignette);

			//Check if this is the glass marble
			if (col_r.w > 0.5) {
				//Calculate refraction
				vec3 n = normalize(iMarblePos - p.xyz);
				vec3 q = refraction(r, n, 1.0 / 1.5);
				vec3 p2 = p.xyz + (dot(q, n) * 2.0 * iMarbleRad) * q;
				n = normalize(p2 - iMarblePos);
				q = (dot(q, r) * 2.0) * q - r;
				vec4 p_temp = vec4(p2 + n * (MIN_DIST * 10), 1.0);
				vec4 r_temp = vec4(q, 0.0);
				vec3 refr = scene(p_temp, r_temp, 0.8).xyz;

				//Calculate refraction
				n = normalize(p.xyz - iMarblePos);
				q = r - n*(2*dot(r,n));
				p_temp = vec4(p.xyz + n * (MIN_DIST * 10), 1.0);
				r_temp = vec4(q, 0.0);
				vec3 refl = scene(p_temp, r_temp, 0.8).xyz;

				//Combine for final marble color
				col += refr * 0.6f + refl * 0.4f + col_r.xyz;
			} else {
				col += col_r.xyz;
			}
		}
	}

	col *= iExposure / (ANTIALIASING_SAMPLES * ANTIALIASING_SAMPLES);
	gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
