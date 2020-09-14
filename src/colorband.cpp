/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2005 Blender Foundation.
 * All rights reserved.
 */

// Source: blender/blenkernel/intern/colorband.c

// TODO: Implement this properly for the rgb ramp node
#if 0
#include <mathutil/umath.h>
#include <cassert>
#include <render/scene.h>

/* colormode */
enum {
  COLBAND_BLEND_RGB = 0,
  COLBAND_BLEND_HSV = 1,
  COLBAND_BLEND_HSL = 2,
};

/* interpolation */
enum {
  COLBAND_INTERP_LINEAR = 0,
  COLBAND_INTERP_EASE = 1,
  COLBAND_INTERP_B_SPLINE = 2,
  COLBAND_INTERP_CARDINAL = 3,
  COLBAND_INTERP_CONSTANT = 4,
};

/* color interpolation */
enum {
  COLBAND_HUE_NEAR = 0,
  COLBAND_HUE_FAR = 1,
  COLBAND_HUE_CW = 2,
  COLBAND_HUE_CCW = 3,
};

struct CBData
{
	float r,g,b,a;
	float pos;
};
struct ColorBand
{
	CBData *data;
	char color_mode;
	char ipotype;
	short tot;
	char ipotype_hue;
};

enum {
  KEY_LINEAR = 0,
  KEY_CARDINAL = 1,
  KEY_BSPLINE = 2,
  KEY_CATMULL_ROM = 3,
};

static float colorband_hue_interp(
    const int ipotype_hue, const float mfac, const float fac, float h1, float h2)
{
  float h_interp;
  int mode = 0;

#define HUE_INTERP(h_a, h_b) ((mfac * (h_a)) + (fac * (h_b)))
#define HUE_MOD(h) (((h) < 1.0f) ? (h) : (h)-1.0f)

  h1 = HUE_MOD(h1);
  h2 = HUE_MOD(h2);

  assert(h1 >= 0.0f && h1 < 1.0f);
  assert(h2 >= 0.0f && h2 < 1.0f);

  switch (ipotype_hue) {
    case COLBAND_HUE_NEAR: {
      if ((h1 < h2) && (h2 - h1) > +0.5f) {
        mode = 1;
      }
      else if ((h1 > h2) && (h2 - h1) < -0.5f) {
        mode = 2;
      }
      else {
        mode = 0;
      }
      break;
    }
    case COLBAND_HUE_FAR: {
      /* Do full loop in Hue space in case both stops are the same... */
      if (h1 == h2) {
        mode = 1;
      }
      else if ((h1 < h2) && (h2 - h1) < +0.5f) {
        mode = 1;
      }
      else if ((h1 > h2) && (h2 - h1) > -0.5f) {
        mode = 2;
      }
      else {
        mode = 0;
      }
      break;
    }
    case COLBAND_HUE_CCW: {
      if (h1 > h2) {
        mode = 2;
      }
      else {
        mode = 0;
      }
      break;
    }
    case COLBAND_HUE_CW: {
      if (h1 < h2) {
        mode = 1;
      }
      else {
        mode = 0;
      }
      break;
    }
  }

  switch (mode) {
    case 0:
      h_interp = HUE_INTERP(h1, h2);
      break;
    case 1:
      h_interp = HUE_INTERP(h1 + 1.0f, h2);
      h_interp = HUE_MOD(h_interp);
      break;
    case 2:
      h_interp = HUE_INTERP(h1, h2 + 1.0f);
      h_interp = HUE_MOD(h_interp);
      break;
  }

  assert(h_interp >= 0.0f && h_interp < 1.0f);

#undef HUE_INTERP
#undef HUE_MOD

  return h_interp;
}

void key_curve_position_weights(float t, float data[4], int type)
{
  float t2, t3, fc;

  if (type == KEY_LINEAR) {
    data[0] = 0.0f;
    data[1] = -t + 1.0f;
    data[2] = t;
    data[3] = 0.0f;
  }
  else if (type == KEY_CARDINAL) {
    t2 = t * t;
    t3 = t2 * t;
    fc = 0.71f;

    data[0] = -fc * t3 + 2.0f * fc * t2 - fc * t;
    data[1] = (2.0f - fc) * t3 + (fc - 3.0f) * t2 + 1.0f;
    data[2] = (fc - 2.0f) * t3 + (3.0f - 2.0f * fc) * t2 + fc * t;
    data[3] = fc * t3 - fc * t2;
  }
  else if (type == KEY_BSPLINE) {
    t2 = t * t;
    t3 = t2 * t;

    data[0] = -0.16666666f * t3 + 0.5f * t2 - 0.5f * t + 0.16666666f;
    data[1] = 0.5f * t3 - t2 + 0.66666666f;
    data[2] = -0.5f * t3 + 0.5f * t2 + 0.5f * t + 0.16666666f;
    data[3] = 0.16666666f * t3;
  }
  else if (type == KEY_CATMULL_ROM) {
    t2 = t * t;
    t3 = t2 * t;
    fc = 0.5f;

    data[0] = -fc * t3 + 2.0f * fc * t2 - fc * t;
    data[1] = (2.0f - fc) * t3 + (fc - 3.0f) * t2 + 1.0f;
    data[2] = (fc - 2.0f) * t3 + (3.0f - 2.0f * fc) * t2 + fc * t;
    data[3] = fc * t3 - fc * t2;
  }
}
float min_ff(float a, float b)
{
  return (a < b) ? a : b;
}
void rgb_to_hsv(float r, float g, float b, float *lh, float *ls, float *lv)
{
  float k = 0.0f;
  float chroma;
  float min_gb;

  if (g < b) {
    umath::swap(g, b);
    k = -1.0f;
  }
  min_gb = b;
  if (r < g) {
    umath::swap(r, g);
    k = -2.0f / 6.0f - k;
    min_gb = min_ff(g, b);
  }

  chroma = r - min_gb;

  *lh = fabsf(k + (g - b) / (6.0f * chroma + 1e-20f));
  *ls = chroma / (r + 1e-20f);
  *lv = r;
}
void rgb_to_hsv_v(const float rgb[3], float r_hsv[3])
{
  rgb_to_hsv(rgb[0], rgb[1], rgb[2], &r_hsv[0], &r_hsv[1], &r_hsv[2]);
}
float max_ff(float a, float b)
{
  return (a > b) ? a : b;
}
float max_fff(float a, float b, float c)
{
  return max_ff(max_ff(a, b), c);
}
float min_ff(float a, float b)
{
  return (a < b) ? a : b;
}
float min_fff(float a, float b, float c)
{
  return min_ff(min_ff(a, b), c);
}

void rgb_to_hsl(float r, float g, float b, float *lh, float *ls, float *ll)
{
  const float cmax = max_fff(r, g, b);
  const float cmin = min_fff(r, g, b);
  float h, s, l = min_ff(1.0, (cmax + cmin) / 2.0f);

  if (cmax == cmin) {
    h = s = 0.0f;  // achromatic
  }
  else {
    float d = cmax - cmin;
    s = l > 0.5f ? d / (2.0f - cmax - cmin) : d / (cmax + cmin);
    if (cmax == r) {
      h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    }
    else if (cmax == g) {
      h = (b - r) / d + 2.0f;
    }
    else {
      h = (r - g) / d + 4.0f;
    }
  }
  h /= 6.0f;

  *lh = h;
  *ls = s;
  *ll = l;
}
void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b)
{
  float nr, ng, nb;

  nr = fabsf(h * 6.0f - 3.0f) - 1.0f;
  ng = 2.0f - fabsf(h * 6.0f - 2.0f);
  nb = 2.0f - fabsf(h * 6.0f - 4.0f);

  nr = umath::clamp(nr, 0.0f, 1.0f);
  nb = umath::clamp(nb, 0.0f, 1.0f);
  ng = umath::clamp(ng, 0.0f, 1.0f);

  *r = ((nr - 1.0f) * s + 1.0f) * v;
  *g = ((ng - 1.0f) * s + 1.0f) * v;
  *b = ((nb - 1.0f) * s + 1.0f) * v;
}
void hsv_to_rgb_v(const float hsv[3], float r_rgb[3])
{
  hsv_to_rgb(hsv[0], hsv[1], hsv[2], &r_rgb[0], &r_rgb[1], &r_rgb[2]);
}
void rgb_to_hsl_v(const float rgb[3], float r_hsl[3])
{
  rgb_to_hsl(rgb[0], rgb[1], rgb[2], &r_hsl[0], &r_hsl[1], &r_hsl[2]);
}
void hsl_to_rgb(float h, float s, float l, float *r, float *g, float *b)
{
  float nr, ng, nb, chroma;

  nr = fabsf(h * 6.0f - 3.0f) - 1.0f;
  ng = 2.0f - fabsf(h * 6.0f - 2.0f);
  nb = 2.0f - fabsf(h * 6.0f - 4.0f);

  nr = umath::clamp(nr, 0.0f, 1.0f);
  nb = umath::clamp(nb, 0.0f, 1.0f);
  ng = umath::clamp(ng, 0.0f, 1.0f);

  chroma = (1.0f - fabsf(2.0f * l - 1.0f)) * s;

  *r = (nr - 0.5f) * chroma + l;
  *g = (ng - 0.5f) * chroma + l;
  *b = (nb - 0.5f) * chroma + l;
}
void hsl_to_rgb_v(const float hsl[3], float r_rgb[3])
{
  hsl_to_rgb(hsl[0], hsl[1], hsl[2], &r_rgb[0], &r_rgb[1], &r_rgb[2]);
}
bool BKE_colorband_evaluate(const ColorBand *coba, float in, float out[4])
{
  const CBData *cbd1, *cbd2, *cbd0, *cbd3;
  float fac;
  int ipotype;
  int a;

  if (coba == NULL || coba->tot == 0) {
    return false;
  }

  cbd1 = coba->data;

  /* Note: when ipotype >= COLBAND_INTERP_B_SPLINE,
   * we cannot do early-out with a constant color before first color stop and after last one,
   * because interpolation starts before and ends after those... */
  ipotype = (coba->color_mode == COLBAND_BLEND_RGB) ? coba->ipotype : COLBAND_INTERP_LINEAR;

  if (coba->tot == 1) {
    out[0] = cbd1->r;
    out[1] = cbd1->g;
    out[2] = cbd1->b;
    out[3] = cbd1->a;
  }
  else if ((in <= cbd1->pos) &&
           (ipotype == COLBAND_INTERP_LINEAR || ipotype == COLBAND_INTERP_EASE || ipotype == COLBAND_INTERP_CONSTANT)) {
    /* We are before first color stop. */
    out[0] = cbd1->r;
    out[1] = cbd1->g;
    out[2] = cbd1->b;
    out[3] = cbd1->a;
  }
  else {
    CBData left, right;

    /* we're looking for first pos > in */
    for (a = 0; a < coba->tot; a++, cbd1++) {
      if (cbd1->pos > in) {
        break;
      }
    }

    if (a == coba->tot) {
      cbd2 = cbd1 - 1;
      right = *cbd2;
      right.pos = 1.0f;
      cbd1 = &right;
    }
    else if (a == 0) {
      left = *cbd1;
      left.pos = 0.0f;
      cbd2 = &left;
    }
    else {
      cbd2 = cbd1 - 1;
    }

    if ((a == coba->tot) &&
        (ipotype == COLBAND_INTERP_LINEAR || ipotype == COLBAND_INTERP_EASE || ipotype == COLBAND_INTERP_CONSTANT)) {
      /* We are after last color stop. */
      out[0] = cbd2->r;
      out[1] = cbd2->g;
      out[2] = cbd2->b;
      out[3] = cbd2->a;
    }
    else if (ipotype == COLBAND_INTERP_CONSTANT) {
      /* constant */
      out[0] = cbd2->r;
      out[1] = cbd2->g;
      out[2] = cbd2->b;
      out[3] = cbd2->a;
    }
    else {
      if (cbd2->pos != cbd1->pos) {
        fac = (in - cbd1->pos) / (cbd2->pos - cbd1->pos);
      }
      else {
        /* was setting to 0.0 in 2.56 & previous, but this
         * is incorrect for the last element, see [#26732] */
        fac = (a != coba->tot) ? 0.0f : 1.0f;
      }

      if ((ipotype == COLBAND_INTERP_B_SPLINE || ipotype == COLBAND_INTERP_CARDINAL)) {
        /* ipo from right to left: 3 2 1 0 */
        float t[4];

        if (a >= coba->tot - 1) {
          cbd0 = cbd1;
        }
        else {
          cbd0 = cbd1 + 1;
        }
        if (a < 2) {
          cbd3 = cbd2;
        }
        else {
          cbd3 = cbd2 - 1;
        }

        fac = umath::clamp(fac, 0.0f, 1.0f);

        if (ipotype == COLBAND_INTERP_CARDINAL) {
          key_curve_position_weights(fac, t, KEY_CARDINAL);
        }
        else {
          key_curve_position_weights(fac, t, KEY_BSPLINE);
        }

        out[0] = t[3] * cbd3->r + t[2] * cbd2->r + t[1] * cbd1->r + t[0] * cbd0->r;
        out[1] = t[3] * cbd3->g + t[2] * cbd2->g + t[1] * cbd1->g + t[0] * cbd0->g;
        out[2] = t[3] * cbd3->b + t[2] * cbd2->b + t[1] * cbd1->b + t[0] * cbd0->b;
        out[3] = t[3] * cbd3->a + t[2] * cbd2->a + t[1] * cbd1->a + t[0] * cbd0->a;
        out[0] = umath::clamp(out[0], 0.0f, 1.0f);
        out[1] = umath::clamp(out[1], 0.0f, 1.0f);
        out[2] = umath::clamp(out[2], 0.0f, 1.0f);
        out[3] = umath::clamp(out[3], 0.0f, 1.0f);
      }
      else {
        if (ipotype == COLBAND_INTERP_EASE) {
          const float fac2 = fac * fac;
          fac = 3.0f * fac2 - 2.0f * fac2 * fac;
        }
        const float mfac = 1.0f - fac;

        if (UNLIKELY(coba->color_mode == COLBAND_BLEND_HSV)) {
          float col1[3], col2[3];

          rgb_to_hsv_v(&cbd1->r, col1);
          rgb_to_hsv_v(&cbd2->r, col2);

          out[0] = colorband_hue_interp(coba->ipotype_hue, mfac, fac, col1[0], col2[0]);
          out[1] = mfac * col1[1] + fac * col2[1];
          out[2] = mfac * col1[2] + fac * col2[2];
          out[3] = mfac * cbd1->a + fac * cbd2->a;

          hsv_to_rgb_v(out, out);
        }
        else if (UNLIKELY(coba->color_mode == COLBAND_BLEND_HSL)) {
          float col1[3], col2[3];

          rgb_to_hsl_v(&cbd1->r, col1);
          rgb_to_hsl_v(&cbd2->r, col2);

          out[0] = colorband_hue_interp(coba->ipotype_hue, mfac, fac, col1[0], col2[0]);
          out[1] = mfac * col1[1] + fac * col2[1];
          out[2] = mfac * col1[2] + fac * col2[2];
          out[3] = mfac * cbd1->a + fac * cbd2->a;

          hsl_to_rgb_v(out, out);
        }
        else {
          /* COLBAND_BLEND_RGB */
          out[0] = mfac * cbd1->r + fac * cbd2->r;
          out[1] = mfac * cbd1->g + fac * cbd2->g;
          out[2] = mfac * cbd1->b + fac * cbd2->b;
          out[3] = mfac * cbd1->a + fac * cbd2->a;
        }
      }
    }
  }

  return true; /* OK */
}
#endif
