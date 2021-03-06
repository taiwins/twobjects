/*
 * surface.h - taiwins wl_surface headers
 *
 * Copyright (c) 2020 Xichen Zhou
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

#ifndef TW_SURFACE_H
#define TW_SURFACE_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <pixman.h>

#include "matrix.h"
#include "plane.h"
#include "utils.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define MAX_VIEW_LINKS 5

enum tw_surface_state {
	TW_SURFACE_ATTACHED = (1 << 0),
	TW_SURFACE_DAMAGED = (1 << 1),
	TW_SURFACE_BUFFER_TRANSFORM = (1 << 2),
	TW_SURFACE_BUFFER_DAMAGED = (1 << 3),
	TW_SURFACE_BUFFER_SCALED = (1 << 4),
	TW_SURFACE_OPAQUE_REGION = (1 << 5),
	TW_SURFACE_INPUT_REGION = (1 << 6),
	/** frame is released in frame, not in commit. If surface never has any
	 * thinng to commit, we should not care about frame. */
	//TW_SURFACE_FRAME_REQUESTED = (1 << 9),
};

struct tw_surface;
struct tw_surface_buffer;

struct tw_event_buffer_uploading {
	struct tw_surface_buffer *buffer;
	pixman_region32_t *damages;
	struct wl_resource *wl_buffer;
	bool new_upload;
};

typedef void (*tw_surface_commit_cb_t)(struct tw_surface *surface);
/**
 * @brief tw_surface_buffer represents a buffer texture for the surface.
 *
 * On server side, a surface shall only need one buffer(texture) to present on
 * the output. Buffer uploading happens at commit, server also releases the
 * previous committed buffer if not released.
 *
 * The texture is staying with the surface until
 */
struct tw_surface_buffer {
	/* can be a wl_shm_buffer or egl buffer or dma buffer */
	struct wl_resource *resource;
	int width, height, stride;
	enum wl_shm_format format;
	union {
		uint32_t id;
		void *ptr;
	} handle;
	/* if there is a texture with the surface, the listener should be
	 * used at surface destruction. */
	struct wl_listener surface_destroy_listener;
	struct {
		bool (*buffer_import)(struct tw_event_buffer_uploading *event,
		                      void *callback);
		void *callback;
	} buffer_import;
};


struct tw_event_surface_frame {
	struct tw_surface *surface;
	uint32_t frame_time;
};

struct tw_view {
	struct tw_surface *surface;
	uint32_t commit_state;
	int32_t dx, dy; /**< wl_surface_attach, pending */
	int32_t buffer_scale;
	enum wl_output_transform transform;
	struct {
		int32_t x, y, w, h;
	} crop;
	struct {
		uint32_t w, h;
	} surface_scale;

	struct tw_mat3 surface_to_buffer;

	struct tw_plane *plane;
	struct wl_resource *buffer_resource;

	pixman_region32_t surface_damage, buffer_damage;
	pixman_region32_t opaque_region, input_region;
};

struct tw_subsurface;
struct tw_surface {
	struct wl_resource *resource;
	const struct tw_allocator *alloc;
	/** the tw_surface::buffer is used to present; should stay available
         * from imported to destroy of the surface
         */
	struct tw_surface_buffer buffer;

        /* current: commited;
         * pending: attached, without commit;
         * previous: last commit
         * rotating towards left.
         */
	struct tw_view *pending, *current, *previous;
	struct tw_view surface_states[3];

	/**
	 * many types may need to use one of the links, eg: backend_ouput,
	 * layer, compositor, input. Plane.
	 */
	struct wl_list links[MAX_VIEW_LINKS];
        struct wl_list layer_link;

	struct wl_list frame_callbacks;
	/* there is also the presentation feedback, later */
	struct wl_list subsurfaces;
	/* subsurface changes on commit  */
	struct wl_list subsurfaces_pending;

	bool is_mapped;

	/** transform of the view */
	struct {
		float x, y;
                /* xywh and prev_xywh are tbe bbox, in terms of damage
                 * collection, xywh and prev_xywh are only useful to a 2D damage
                 * tracker. It would not be very useful in other cases.
                 */
		pixman_rectangle32_t xywh;
		pixman_region32_t dirty;
                /**
                 * map from (-1,-1,1,1) to global coordinates in a Y-down
                 * coordinate system
                 */
		struct tw_mat3 transform;
		struct tw_mat3 inverse_transform;

	} geometry;

	struct {
		const char *name;
		void *commit_private;
		tw_surface_commit_cb_t commit;
	} role;

	struct {
		struct wl_signal frame;
		struct wl_signal commit;
		struct wl_signal destroy;
		struct wl_signal dirty;
	} signals;

	void *user_data;
};

/** a good reference about subsurface is here
 * :https://ppaalanen.blogspot.com/2013/11/sub-surfaces-now.html
 */
struct tw_subsurface {
	struct wl_resource *resource;
	struct tw_surface *surface;
	struct tw_surface *parent;
	struct wl_list parent_link; /**< reflects subsurface stacking order */
	struct wl_list parent_pending_link; /* accummulated stacking order */
	struct wl_signal destroy;
	struct wl_listener surface_destroyed;
	int32_t sx, sy;
	bool sync;
	const struct tw_allocator *alloc;
};

struct tw_region {
	struct wl_resource *resource;
	pixman_region32_t region;
	struct wl_signal destroy;
	const struct tw_allocator *alloc;
};

struct tw_surface*
tw_surface_create(struct wl_client *client, uint32_t version, uint32_t id,
                  const struct tw_allocator *alloc);

struct tw_surface *
tw_surface_from_resource(struct wl_resource *wl_surface);

bool
tw_surface_has_texture(struct tw_surface *surface);

bool
tw_surface_has_role(struct tw_surface *surface);

bool
tw_surface_assign_role(struct tw_surface *surface, tw_surface_commit_cb_t cmt,
                       void *user_data, const char *name);

/**
 * @brief dirty the geometry of the surface and subsurfaces.
 *
 * Compositor should accumulate all the damage of the surface when geometry is
 * dirty.
 */
void
tw_surface_set_position(struct tw_surface *surface, float x, float y);

void
tw_surface_to_local_pos(struct tw_surface *surface, float x, float y,
                        float *sx, float *sy);
void
tw_surface_to_global_pos(struct tw_surface *surface, float sx, float sy,
                        float *gx, float *gy);
bool
tw_surface_has_point(struct tw_surface *surface, float x, float y);

bool
tw_surface_has_input_point(struct tw_surface *surface, float x, float y);

/**
 * @brief force dirting the geometry, it would damages all its clip region for
 * the outputs.
 */
void
tw_surface_dirty_geometry(struct tw_surface *surface);

/**
 * @brief flushing the view state, clean up the damage and also calls frame
 * signal
 */
void
tw_surface_flush_frame(struct tw_surface *surface, uint32_t time_msec);

bool
tw_surface_is_subsurface(struct tw_surface *surf);

bool
tw_subsurface_is_synched(struct tw_subsurface *sub);

struct tw_subsurface *
tw_surface_get_subsurface(struct tw_surface *surf);

struct tw_subsurface *
tw_subsurface_create(struct wl_client *client, uint32_t version, uint32_t id,
                     struct tw_surface *surface, struct tw_surface *parent,
                     const struct tw_allocator *alloc);
void
tw_subsurface_update_pos(struct tw_subsurface *sub,
                         int32_t sx, int32_t sy);
struct tw_region *
tw_region_create(struct wl_client *client, uint32_t version, uint32_t id,
                 const struct tw_allocator *alloc);

struct tw_region *
tw_region_from_resource(struct wl_resource *wl_region);

void
tw_surface_buffer_release(struct tw_surface_buffer *buffer);

bool
tw_surface_buffer_update(struct tw_surface_buffer *buffer,
                         struct wl_resource *resource,
                         pixman_region32_t *damage);
void
tw_surface_buffer_new(struct tw_surface_buffer *buffer,
                      struct wl_resource *resource);

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
