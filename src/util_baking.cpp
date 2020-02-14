/*
* Copyright 2011-2013 Blender Foundation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "pr_cycles/util_baking.hpp"
#include "pr_cycles/mesh.hpp"
#include "pr_cycles/object.hpp"
#include <render/mesh.h>

using namespace pragma::modules;

#define FILTER_MASK_MARGIN 1
#define FILTER_MASK_USED 2

#pragma optimize("",off)
typedef struct ZSpan {
	int rectx, recty; /* range for clipping */

	int miny1, maxy1, miny2, maxy2;             /* actual filled in range */
	const float *minp1, *maxp1, *minp2, *maxp2; /* vertex pointers detect min/max range in */
	std::vector<float> span1, span2;
} ZSpan;

typedef struct BakeDataZSpan {
	cycles::baking::BakePixel *pixel_array;
	int primitive_id;
	int object_id;
	//BakeImage *bk_image;
	uint32_t bakeImageWidth;
	uint32_t bakeImageHeight;
	std::vector<ZSpan> zspan;
	float du_dx, du_dy;
	float dv_dx, dv_dy;
} BakeDataZSpan;

void cycles::baking::populate_bake_data(ccl::BakeData *data,
	const int object_id,
	BakePixel *pixel_array,
	const int num_pixels)
{
	BakePixel *bp = pixel_array;

	int i;
	for (i = 0; i < num_pixels; i++) {
		if (bp->object_id == object_id) {
			data->set(i, bp->primitive_id, bp->uv, bp->du_dx, bp->du_dy, bp->dv_dx, bp->dv_dy);
		}
		else {
			data->set_null(i);
		}
		++bp;
	}
}

static void bake_differentials(BakeDataZSpan *bd,
	const float *uv1,
	const float *uv2,
	const float *uv3)
{
	float A;

	/* assumes dPdu = P1 - P3 and dPdv = P2 - P3 */
	A = (uv2[0] - uv1[0]) * (uv3[1] - uv1[1]) - (uv3[0] - uv1[0]) * (uv2[1] - uv1[1]);

	if (fabsf(A) > FLT_EPSILON) {
		A = 0.5f / A;

		bd->du_dx = (uv2[1] - uv3[1]) * A;
		bd->dv_dx = (uv3[1] - uv1[1]) * A;

		bd->du_dy = (uv3[0] - uv2[0]) * A;
		bd->dv_dy = (uv1[0] - uv3[0]) * A;
	}
	else {
		bd->du_dx = bd->du_dy = 0.0f;
		bd->dv_dx = bd->dv_dy = 0.0f;
	}
}

static void zbuf_init_span(ZSpan *zspan)
{
	zspan->miny1 = zspan->miny2 = zspan->recty + 1;
	zspan->maxy1 = zspan->maxy2 = -1;
	zspan->minp1 = zspan->maxp1 = zspan->minp2 = zspan->maxp2 = NULL;
}

float min_ff(float a, float b)
{
	return (a < b) ? a : b;
}
int min_ii(int a, int b)
{
	return (a < b) ? a : b;
}
int max_ii(int a, int b)
{
	return (b < a) ? a : b;
}
float max_ff(float a, float b)
{
	return (a > b) ? a : b;
}
static void zbuf_add_to_span(ZSpan *zspan, const float v1[2], const float v2[2])
{
	const float *minv, *maxv;
	float *span;
	float xx1, dx0, xs0;
	int y, my0, my2;

	if (v1[1] < v2[1]) {
		minv = v1;
		maxv = v2;
	}
	else {
		minv = v2;
		maxv = v1;
	}

	my0 = ceil(minv[1]);
	my2 = floor(maxv[1]);

	if (my2 < 0 || my0 >= zspan->recty) {
		return;
	}

	/* clip top */
	if (my2 >= zspan->recty) {
		my2 = zspan->recty - 1;
	}
	/* clip bottom */
	if (my0 < 0) {
		my0 = 0;
	}

	if (my0 > my2) {
		return;
	}
	/* if (my0>my2) should still fill in, that way we get spans that skip nicely */

	xx1 = maxv[1] - minv[1];
	if (xx1 > FLT_EPSILON) {
		dx0 = (minv[0] - maxv[0]) / xx1;
		xs0 = dx0 * (minv[1] - my2) + minv[0];
	}
	else {
		dx0 = 0.0f;
		xs0 = min_ff(minv[0], maxv[0]);
	}

	/* empty span */
	if (zspan->maxp1 == NULL) {
		span = zspan->span1.data();
	}
	else { /* does it complete left span? */
		if (maxv == zspan->minp1 || minv == zspan->maxp1) {
			span = zspan->span1.data();
		}
		else {
			span = zspan->span2.data();
		}
	}

	if (span == zspan->span1.data()) {
		//      printf("left span my0 %d my2 %d\n", my0, my2);
		if (zspan->minp1 == NULL || zspan->minp1[1] > minv[1]) {
			zspan->minp1 = minv;
		}
		if (zspan->maxp1 == NULL || zspan->maxp1[1] < maxv[1]) {
			zspan->maxp1 = maxv;
		}
		if (my0 < zspan->miny1) {
			zspan->miny1 = my0;
		}
		if (my2 > zspan->maxy1) {
			zspan->maxy1 = my2;
		}
	}
	else {
		//      printf("right span my0 %d my2 %d\n", my0, my2);
		if (zspan->minp2 == NULL || zspan->minp2[1] > minv[1]) {
			zspan->minp2 = minv;
		}
		if (zspan->maxp2 == NULL || zspan->maxp2[1] < maxv[1]) {
			zspan->maxp2 = maxv;
		}
		if (my0 < zspan->miny2) {
			zspan->miny2 = my0;
		}
		if (my2 > zspan->maxy2) {
			zspan->maxy2 = my2;
		}
	}

	for (y = my2; y >= my0; y--, xs0 += dx0) {
		/* xs0 is the xcoord! */
		span[y] = xs0;
	}
}

void zspan_scanconvert(ZSpan *zspan,
	void *handle,
	float *v1,
	float *v2,
	float *v3,
	void (*func)(void *, int, int, float, float))
{
	float x0, y0, x1, y1, x2, y2, z0, z1, z2;
	float u, v, uxd, uyd, vxd, vyd, uy0, vy0, xx1;
	const float *span1, *span2;
	int i, j, x, y, sn1, sn2, rectx = zspan->rectx, my0, my2;

	/* init */
	zbuf_init_span(zspan);

	/* set spans */
	zbuf_add_to_span(zspan, v1, v2);
	zbuf_add_to_span(zspan, v2, v3);
	zbuf_add_to_span(zspan, v3, v1);

	/* clipped */
	if (zspan->minp2 == NULL || zspan->maxp2 == NULL) {
		return;
	}

	my0 = max_ii(zspan->miny1, zspan->miny2);
	my2 = min_ii(zspan->maxy1, zspan->maxy2);

	//  printf("my %d %d\n", my0, my2);
	if (my2 < my0) {
		return;
	}

	/* ZBUF DX DY, in floats still */
	x1 = v1[0] - v2[0];
	x2 = v2[0] - v3[0];
	y1 = v1[1] - v2[1];
	y2 = v2[1] - v3[1];

	z1 = 1.0f; /* (u1 - u2) */
	z2 = 0.0f; /* (u2 - u3) */

	x0 = y1 * z2 - z1 * y2;
	y0 = z1 * x2 - x1 * z2;
	z0 = x1 * y2 - y1 * x2;

	if (z0 == 0.0f) {
		return;
	}

	xx1 = (x0 * v1[0] + y0 * v1[1]) / z0 + 1.0f;
	uxd = -(double)x0 / (double)z0;
	uyd = -(double)y0 / (double)z0;
	uy0 = ((double)my2) * uyd + (double)xx1;

	z1 = -1.0f; /* (v1 - v2) */
	z2 = 1.0f;  /* (v2 - v3) */

	x0 = y1 * z2 - z1 * y2;
	y0 = z1 * x2 - x1 * z2;

	xx1 = (x0 * v1[0] + y0 * v1[1]) / z0;
	vxd = -(double)x0 / (double)z0;
	vyd = -(double)y0 / (double)z0;
	vy0 = ((double)my2) * vyd + (double)xx1;

	/* correct span */
	span1 = zspan->span1.data() + my2;
	span2 = zspan->span2.data() + my2;

	for (i = 0, y = my2; y >= my0; i++, y--, span1--, span2--) {

		sn1 = floor(min_ff(*span1, *span2));
		sn2 = floor(max_ff(*span1, *span2));
		sn1++;

		if (sn2 >= rectx) {
			sn2 = rectx - 1;
		}
		if (sn1 < 0) {
			sn1 = 0;
		}

		u = (((double)sn1 * uxd) + uy0) - (i * uyd);
		v = (((double)sn1 * vxd) + vy0) - (i * vyd);

		for (j = 0, x = sn1; x <= sn2; j++, x++) {
			func(handle, x, y, u + (j * uxd), v + (j * vxd));
		}
	}
}

void copy_v2_fl2(float v[2], float x, float y)
{
	v[0] = x;
	v[1] = y;
}

static void store_bake_pixel(void *handle, int x, int y, float u, float v)
{
	BakeDataZSpan *bd = (BakeDataZSpan *)handle;
	cycles::baking::BakePixel *pixel;

	const int width = bd->bakeImageWidth;
	const size_t offset = 0;
	const int i = offset + y * width + x;

	pixel = &bd->pixel_array[i];
	pixel->primitive_id = bd->primitive_id;

	copy_v2_fl2(pixel->uv, u, v);

	pixel->du_dx = bd->du_dx;
	pixel->du_dy = bd->du_dy;
	pixel->dv_dx = bd->dv_dx;
	pixel->dv_dy = bd->dv_dy;
	pixel->object_id = bd->object_id;
}

void cycles::baking::prepare_bake_data(cycles::Object &o,BakePixel *pixelArray,uint32_t numPixels,uint32_t imgWidth,uint32_t imgHeight,bool useLightmapUvs)
{
	/* initialize all pixel arrays so we know which ones are 'blank' */
	for(auto i=decltype(numPixels){0u};i<numPixels;++i)
	{
		pixelArray[i].primitive_id = -1;
		pixelArray[i].object_id = -1;
	}


	BakeDataZSpan bd;
	bd.pixel_array = pixelArray;
	uint32_t numBakeImages = 1u;
	bd.zspan.resize(numBakeImages);

	for(auto &zspan : bd.zspan)
	{
		zspan.rectx = imgWidth;
		zspan.recty = imgHeight;

		zspan.span1.resize(zspan.recty);
		zspan.span2.resize(zspan.recty);
	}

	auto &mesh = o.GetMesh();
	auto *uvs = useLightmapUvs ? mesh.GetLightmapUVs() : mesh.GetUVs();
	if(uvs == nullptr)
		return;
	bd.object_id = o.GetId();
	auto *cclMesh = *mesh;
	auto numTris = cclMesh->triangles.size() /3;
	for(auto i=decltype(numTris){0u};i<numTris;++i)
	{
		int32_t imageId = 0;
		bd.bakeImageWidth = imgWidth;
		bd.bakeImageHeight = imgHeight;
		bd.primitive_id = i;

		float vec[3][2];
		auto *tri = &cclMesh->triangles[i *3];
		for(uint8_t j=0;j<3;++j)
		{
			const float *uv = reinterpret_cast<const float*>(&uvs[i *3 +j]);

			/* Note, workaround for pixel aligned UVs which are common and can screw up our
			* intersection tests where a pixel gets in between 2 faces or the middle of a quad,
			* camera aligned quads also have this problem but they are less common.
			* Add a small offset to the UVs, fixes bug #18685 - Campbell */
			vec[j][0] = uv[0] * (float)bd.bakeImageWidth - (0.5f + 0.001f);
			vec[j][1] = uv[1] * (float)bd.bakeImageHeight - (0.5f + 0.002f);
		}

		bake_differentials(&bd, vec[0], vec[1], vec[2]);
		zspan_scanconvert(&bd.zspan[imageId], (void *)&bd, vec[0], vec[1], vec[2], store_bake_pixel);
	}
}

// Source: blender/blenlib/intern/math_base_inline.c
unsigned char cycles::baking::unit_float_to_uchar_clamp(float val)
{
	return (unsigned char)((
		(val <= 0.0f) ? 0 : ((val > (1.0f - 0.5f / 255.0f)) ? 255 : ((255.0f * val) + 0.5f))));
}

unsigned short cycles::baking::unit_float_to_ushort_clamp(float val)
{
	return (unsigned short)((val >= 1.0f - 0.5f / 65535) ?
		65535 :
		(val <= 0.0f) ? 0 : (val * 65535.0f + 0.5f));
}

static int filter_make_index(const int x, const int y, const int w, const int h)
{
	if (x < 0 || x >= w || y < 0 || y >= h) {
		return -1; /* return bad index */
	}
	else {
		return y * w + x;
	}
}

static int check_pixel_assigned(
	const void *buffer, const char *mask, const int index, const int depth, const bool is_float)
{
	int res = 0;

	if (index >= 0) {
		const int alpha_index = depth * index + (depth - 1);

		if (mask != NULL) {
			res = mask[index] != 0 ? 1 : 0;
		}
		else if ((is_float && ((const float *)buffer)[alpha_index] != 0.0f) ||
			(!is_float && ((const unsigned char *)buffer)[alpha_index] != 0)) {
			res = 1;
		}
	}

	return res;
}

static void IMB_filter_extend(struct cycles::baking::ImBuf *ibuf, std::vector<uint8_t> &vmask, int filter)
{
	auto *mask = reinterpret_cast<char*>(vmask.data());

	const int width = ibuf->x;
	const int height = ibuf->y;
	const int depth = 4; /* always 4 channels */
	const bool is_float = ibuf->rect->IsFloatFormat();
	const int chsize = is_float ? sizeof(float) : sizeof(unsigned char);
	const size_t bsize = ((size_t)width) * height * depth * chsize;

	std::vector<uint8_t> vdstbuf;
	auto *data = ibuf->rect->GetData();
	vdstbuf.resize(ibuf->rect->GetSize());
	memcpy(vdstbuf.data(),data,vdstbuf.size() *sizeof(vdstbuf.front()));

	void *dstbuf = vdstbuf.data();
	auto vdstmask = vmask;
	char *dstmask = reinterpret_cast<char*>(vdstmask.data());
	void *srcbuf = data;
	char *srcmask = mask;
	int cannot_early_out = 1, r, n, k, i, j, c;
	float weight[25];

	/* build a weights buffer */
	n = 1;

#if 0
	k = 0;
	for (i = -n; i <= n; i++) {
		for (j = -n; j <= n; j++) {
			weight[k++] = sqrt((float)i * i + j * j);
		}
	}
#endif

	weight[0] = 1;
	weight[1] = 2;
	weight[2] = 1;
	weight[3] = 2;
	weight[4] = 0;
	weight[5] = 2;
	weight[6] = 1;
	weight[7] = 2;
	weight[8] = 1;

	/* run passes */
	for (r = 0; cannot_early_out == 1 && r < filter; r++) {
		int x, y;
		cannot_early_out = 0;

		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				const int index = filter_make_index(x, y, width, height);

				/* only update unassigned pixels */
				if (!check_pixel_assigned(srcbuf, srcmask, index, depth, is_float)) {
					float tmp[4];
					float wsum = 0;
					float acc[4] = {0, 0, 0, 0};
					k = 0;

					if (check_pixel_assigned(
						srcbuf, srcmask, filter_make_index(x - 1, y, width, height), depth, is_float) ||
						check_pixel_assigned(
							srcbuf, srcmask, filter_make_index(x + 1, y, width, height), depth, is_float) ||
						check_pixel_assigned(
							srcbuf, srcmask, filter_make_index(x, y - 1, width, height), depth, is_float) ||
						check_pixel_assigned(
							srcbuf, srcmask, filter_make_index(x, y + 1, width, height), depth, is_float)) {
						for (i = -n; i <= n; i++) {
							for (j = -n; j <= n; j++) {
								if (i != 0 || j != 0) {
									const int tmpindex = filter_make_index(x + i, y + j, width, height);

									if (check_pixel_assigned(srcbuf, srcmask, tmpindex, depth, is_float)) {
										if (is_float) {
											for (c = 0; c < depth; c++) {
												tmp[c] = ((const float *)srcbuf)[depth * tmpindex + c];
											}
										}
										else {
											for (c = 0; c < depth; c++) {
												tmp[c] = (float)((const unsigned char *)srcbuf)[depth * tmpindex + c];
											}
										}

										wsum += weight[k];

										for (c = 0; c < depth; c++) {
											acc[c] += weight[k] * tmp[c];
										}
									}
								}
								k++;
							}
						}

						if (wsum != 0) {
							for (c = 0; c < depth; c++) {
								acc[c] /= wsum;
							}

							if (is_float) {
								for (c = 0; c < depth; c++) {
									((float *)dstbuf)[depth * index + c] = acc[c];
								}
							}
							else {
								for (c = 0; c < depth; c++) {
									((unsigned char *)dstbuf)[depth * index + c] =
										acc[c] > 255 ? 255 : (acc[c] < 0 ? 0 : ((unsigned char)(acc[c] + 0.5f)));
								}
							}

							if (dstmask != NULL) {
								dstmask[index] = FILTER_MASK_MARGIN; /* assigned */
							}
							cannot_early_out = 1;
						}
					}
				}
			}
		}

		/* keep the original buffer up to date. */
		memcpy(srcbuf, dstbuf, bsize);
		if (dstmask != NULL) {
			memcpy(srcmask, dstmask, ((size_t)width) * height);
		}
	}
}

void cycles::baking::RE_bake_margin(ImBuf *ibuf, std::vector<uint8_t> &mask, const int margin)
{
	/* margin */
	IMB_filter_extend(ibuf, mask, margin);
}

void cycles::baking::RE_bake_mask_fill(const std::vector<BakePixel> pixel_array, const size_t num_pixels, char *mask)
{
	size_t i;
	if (!mask) {
		return;
	}

	/* only extend to pixels outside the mask area */
	for (i = 0; i < num_pixels; i++) {
		if (pixel_array[i].primitive_id != -1) {
			mask[i] = FILTER_MASK_USED;
		}
	}
}
#pragma optimize("",on)
