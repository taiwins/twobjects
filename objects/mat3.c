/*
 * mat3.c - taiwins matrix implementation
 *
 * Copyright (c) 2020-2021 Xichen Zhou
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#define _USE_MATH_DEFINES
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <limits.h>
#include <wayland-server.h>
#include <pixman.h>

#include <taiwins/objects/matrix.h>

#define MAX(a, b) \
	({ __typeof__ (a) _a = (a); \
		__typeof__ (b) _b = (b); \
		_a > _b ? _a : _b; })

#define MIN(a, b) \
	({ __typeof__ (a) _a = (a); \
		__typeof__ (b) _b = (b); \
		_a < _b ? _a : _b; })

/******************************************************************************
 * 2D transformations
 *****************************************************************************/
static inline float deg2rad(float degree)
{
	return degree * M_1_PI / 180.0f;
}

/* matrix 2d indices
 * 0 3 6
 * 1 4 7
 * 2 5 8
 */

/* 2d rotation matrix
 * cos, -sin,            -- column major -->   cos, sin,
 * sin, cos                                   -sin, cos
 */
static const struct tw_mat3 transform_2ds[8] = {
	[WL_OUTPUT_TRANSFORM_NORMAL] =  {
		.d = {1.0f, 0.0f, 0.0f,
		      0.0f, 1.0f, 0.0f,
		      0.0f, 0.0f, 1.0f}
	},
	[WL_OUTPUT_TRANSFORM_90] = {
		.d = {0.0f, 1.0f, 0.0f,
		      -1.0f, 0.0f, 0.0f,
		      0.0f, 0.0f, 1.0f}
	},
	[WL_OUTPUT_TRANSFORM_180] = {
		.d = {-1.0f, 0.0f, 0.0f,
		      0.0f, -1.0f, 0.0f,
		      0.0f, 0.0f, 1.0f}
	},
	[WL_OUTPUT_TRANSFORM_270] = {
		.d = {0.0f, -1.0f, 0.0f,
		      1.0f, 0.0f, 0.0f,
		      0.0f, 0.0f, 1.0f}
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED] = {
		.d = {-1.0f, 0.0f, 0.0f,
		      0.0f,  1.0f, 0.0f,
		      0.0f, 0.0f, 1.0f}
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_90] = {
		.d ={0.0f, 1.0f, 0.0f,
		     1.0f, 0.0f, 0.0f,
		     0.0f, 0.0f, 1.0f}
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_180] = {
		.d={1.0f, 0.0f, 0.0f,
		    0.0f, -1.0f, 0.0f,
		    0.0f, 0.0f, 1.0f},
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_270] = {
		.d={0.0f, -1.0f, 0.0f,
		    -1.0f, 0.0f, 0.0f,
		    0.0f, 0.0f, 1.0f}
	},
};

static const enum wl_output_transform transform_yup_to_ydown[8] = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = WL_OUTPUT_TRANSFORM_NORMAL,
	[WL_OUTPUT_TRANSFORM_90] = WL_OUTPUT_TRANSFORM_270,
	[WL_OUTPUT_TRANSFORM_180] = WL_OUTPUT_TRANSFORM_180,
	[WL_OUTPUT_TRANSFORM_270] = WL_OUTPUT_TRANSFORM_90,
	[WL_OUTPUT_TRANSFORM_FLIPPED] = WL_OUTPUT_TRANSFORM_FLIPPED,
	[WL_OUTPUT_TRANSFORM_FLIPPED_90] = WL_OUTPUT_TRANSFORM_FLIPPED_270,
	[WL_OUTPUT_TRANSFORM_FLIPPED_180] = WL_OUTPUT_TRANSFORM_FLIPPED_180,
	[WL_OUTPUT_TRANSFORM_FLIPPED_270] = WL_OUTPUT_TRANSFORM_FLIPPED_90
};

static inline enum wl_output_transform
transform_ydown_from_yup(enum wl_output_transform t)
{
	return transform_yup_to_ydown[t];
}

WL_EXPORT void
tw_mat3_init(struct tw_mat3 *mat)
{
	static const struct tw_mat3 i = {
		.d = {1, 0, 0,
		      0, 1, 0,
		      0, 0, 1},
	};
	memcpy(mat, &i, sizeof(*mat));
}

WL_EXPORT void
tw_mat3_transpose(struct tw_mat3 *dst, const struct tw_mat3 *src)
{
	struct tw_mat3 tmp;
	const unsigned idx[9] = {0, 3, 6, 1, 4, 7, 2, 5, 8};
	for (unsigned i = 0; i < 9; i++)
		tmp.d[i] = src->d[idx[i]];
	memcpy(dst, &tmp, sizeof(tmp));
}

WL_EXPORT void
tw_mat3_multiply(struct tw_mat3 *dst, const struct tw_mat3 *a,
                 const struct tw_mat3 *b)
{
	struct tw_mat3 tmp;

	tmp.d[0] = a->d[0] * b->d[0] + a->d[3] * b->d[1] + a->d[6] * b->d[2];
	tmp.d[1] = a->d[1] * b->d[0] + a->d[4] * b->d[1] + a->d[7] * b->d[2];
	tmp.d[2] = a->d[2] * b->d[0] + a->d[5] * b->d[1] + a->d[8] * b->d[2];

	tmp.d[3] = a->d[0] * b->d[3] + a->d[3] * b->d[4] + a->d[6] * b->d[5];
	tmp.d[4] = a->d[1] * b->d[3] + a->d[4] * b->d[4] + a->d[7] * b->d[5];
	tmp.d[5] = a->d[2] * b->d[3] + a->d[5] * b->d[4] + a->d[8] * b->d[5];

	tmp.d[6] = a->d[0] * b->d[6] + a->d[3] * b->d[7] + a->d[6] * b->d[8];
	tmp.d[7] = a->d[1] * b->d[6] + a->d[4] * b->d[7] + a->d[7] * b->d[8];
	tmp.d[8] = a->d[2] * b->d[6] + a->d[5] * b->d[7] + a->d[8] * b->d[8];

	memcpy(dst, &tmp, sizeof(*dst));
}

WL_EXPORT void
tw_mat3_vec_transform(const struct tw_mat3 *mat, float x, float y,
                      float *rx, float *ry)
{
	*rx = mat->d[0] * x + mat->d[3] * y + mat->d[6];
	*ry = mat->d[1] * x + mat->d[4] * y + mat->d[7];
}

WL_EXPORT void
tw_mat3_box_transform(const struct tw_mat3 *mat,
                      pixman_box32_t *dst, const pixman_box32_t *src)
{
	float corners[4][2] = {
		{src->x1, src->y1}, {src->x1, src->y2},
		{src->x2, src->y1}, {src->x2, src->y2},
	};
	pixman_box32_t box = {INT_MAX, INT_MAX, INT_MIN, INT_MIN};

	for (int i = 0; i < 4; i++) {
		tw_mat3_vec_transform(mat, corners[i][0], corners[i][1],
		                      &corners[i][0], &corners[i][1]);
		box.x1 = MIN(box.x1, (int32_t)corners[i][0]);
		box.y1 = MIN(box.y1, (int32_t)corners[i][1]);
		box.x2 = MAX(box.x2, (int32_t)corners[i][0]);
		box.y2 = MAX(box.y2, (int32_t)corners[i][1]);
	}
	*dst = box;
}

WL_EXPORT void
tw_mat3_region_transform(const struct tw_mat3 *mat,
                         pixman_region32_t *dst, pixman_region32_t *src)
{
	int n = 0;
	pixman_box32_t *src_rects = NULL;
	pixman_box32_t *dst_rects = NULL;

	src_rects = pixman_region32_rectangles(src, &n);
	dst_rects = malloc(n * sizeof(pixman_box32_t));
	if (!dst_rects)
		return;

	for (int i = 0; i < n; i++)
		tw_mat3_box_transform(mat, &dst_rects[i], &src_rects[i]);
	pixman_region32_fini(dst);
	pixman_region32_init_rects(dst, dst_rects, n);
	free(dst_rects);
}

WL_EXPORT void
tw_mat3_translate(struct tw_mat3 *mat, float x, float y)
{
	tw_mat3_init(mat);
	mat->d[6] = x;
	mat->d[7] = y;
}

WL_EXPORT void
tw_mat3_scale(struct tw_mat3 *dst, float x, float y)
{
	tw_mat3_init(dst);
	dst->d[0] = x;
	dst->d[4] = y;
}

/* rotate ccw */
WL_EXPORT void
tw_mat3_rotate(struct tw_mat3 *mat, float degree, bool yup)
{
	float rad = deg2rad(degree);
	tw_mat3_init(mat);
	mat->d[0] = cos(rad);
	mat->d[1] = (yup) ? sin(rad) : -sin(rad);
	mat->d[3] = (yup) ? -sin(rad) : sin(rad);
	mat->d[4] = cos(rad);
}

WL_EXPORT void
tw_mat3_wl_transform(struct tw_mat3 *dst,
                     enum wl_output_transform transform, bool yup)
{
	transform = (yup) ? transform : transform_ydown_from_yup(transform);
	memcpy(dst, &transform_2ds[transform], sizeof(*dst));
}

WL_EXPORT void
tw_mat3_flip_y(struct tw_mat3 *dst, float h)
{
	dst->d[0] = 1.0; dst->d[1] =  0.0; dst->d[2] = 0.0;
	dst->d[3] = 0.0; dst->d[4] = -1.0; dst->d[5] = 0.0;
	dst->d[6] = 0.0; dst->d[7] =  h;   dst->d[8] = 1.0;
}

WL_EXPORT void
tw_mat3_transform_rect(struct tw_mat3 *dst, bool yup,
                       enum wl_output_transform transform,
                       uint32_t width, uint32_t height, uint32_t scale)
{
	struct tw_mat3 tmp;

	transform = (yup) ? transform : transform_ydown_from_yup(transform);
	tw_mat3_init(dst);
	tw_mat3_wl_transform(&tmp, transform, true);
	tw_mat3_multiply(dst, &tmp, dst);
	tw_mat3_scale(&tmp, scale, scale);

	switch (transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
		break;
	case WL_OUTPUT_TRANSFORM_90:
		tw_mat3_translate(&tmp, height, 0.0);
		tw_mat3_multiply(dst, &tmp, dst);
		break;
	case WL_OUTPUT_TRANSFORM_180:
		tw_mat3_translate(&tmp, width, height);
		tw_mat3_multiply(dst, &tmp, dst);
		break;
	case WL_OUTPUT_TRANSFORM_270:
		tw_mat3_translate(&tmp, 0.0, width);
		tw_mat3_multiply(dst, &tmp, dst);
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED:
		tw_mat3_translate(&tmp, width, 0.0);
		tw_mat3_multiply(dst, &tmp, dst);
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		tw_mat3_translate(&tmp, 0.0, height);
		tw_mat3_multiply(dst, &tmp, dst);
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		tw_mat3_translate(&tmp, height, width);
		tw_mat3_multiply(dst, &tmp, dst);
		break;
	}
}

WL_EXPORT void
tw_mat3_ortho_proj(struct tw_mat3 *dst, uint32_t width,
                   uint32_t height)
{
	float x = 2.0f / width;
	float y = 2.0f / height;

	//xp = 2*xe/width - 1.0
	//yp = (2 - 2*ye/height) + 1.0
	tw_mat3_init(dst);
	dst->d[0] = x;
	dst->d[4] = y;
	dst->d[6] = -1.0f;
	dst->d[7] = -1.0f;
	dst->d[8] = 1.0f;
}

static float
det_mat3(const struct tw_mat3 *mat)
{
	return  mat->d[0] * (mat->d[4] * mat->d[8] - mat->d[5] * mat->d[7]) -
		mat->d[3] * (mat->d[1] * mat->d[8] - mat->d[2] * mat->d[7]) +
		mat->d[6] * (mat->d[1] * mat->d[5] - mat->d[2] * mat->d[4]);
}

WL_EXPORT bool
tw_mat3_inverse(struct tw_mat3 *dst, const struct tw_mat3 *src)
{
	float sign = 1.0, det = 0.0;
	struct tw_mat3 tmp;
	static const unsigned idx[9][4] = {
		{4, 8, 7, 5},
		{3, 8, 6, 5},
		{3, 7, 6, 4},
		{1, 8, 7, 2},
		{0, 8, 6, 2},
		{0, 7, 6, 1},
		{1, 5, 4, 2},
		{0, 5, 3, 2},
		{0, 4, 3, 1},
	};

	det = det_mat3(src);
	if (fabs(0.0 - det) <= TW_EPSILON)
		return false;
	tw_mat3_transpose(&tmp, src);

	for (unsigned i = 0; i < 9; i++) {
		dst->d[i] = (sign/det) * (tmp.d[idx[i][0]] * tmp.d[idx[i][1]] -
		                          tmp.d[idx[i][2]] * tmp.d[idx[i][3]]);
		sign *= -1.0f;
	}
	return true;
}
