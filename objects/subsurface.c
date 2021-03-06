/*
 * subsurface.c - taiwins wl_subsurface implementation
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

#include <limits.h>
#include <assert.h>
#include <math.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/surface.h>


#define SUBSURFACE_VERSION 1

static const struct wl_subsurface_interface subsurface_impl;

void
subsurface_commit_for_parent(struct tw_subsurface *subsurface, bool sync);

static void subsurface_commit_role(struct tw_surface *surf) {
	struct tw_subsurface *sub = surf->role.commit_private;
	struct tw_surface *parent = sub->parent;
	// surface has moved, or parent has moved. We would need to dirty the
	// geometry now.
	if (surf->geometry.xywh.x != sub->sx + parent->geometry.xywh.x ||
	    surf->geometry.xywh.y != sub->sy + parent->geometry.xywh.y)
		tw_surface_set_position(surf, parent->geometry.xywh.x + sub->sx,
		                        parent->geometry.xywh.y + sub->sy);
}

WL_EXPORT bool
tw_surface_is_subsurface(struct tw_surface *surf)
{
	return surf->role.commit == subsurface_commit_role;
}

WL_EXPORT struct tw_subsurface *
tw_surface_get_subsurface(struct tw_surface *surf)
{
	return (tw_surface_is_subsurface(surf)) ?
		surf->role.commit_private :
		NULL;
}

static struct tw_subsurface *
tw_subsurface_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource, &wl_subsurface_interface,
	                               &subsurface_impl));
	return wl_resource_get_user_data(resource);
}

static struct tw_subsurface *
find_sibling_subsurface(struct tw_subsurface *subsurface,
                        struct tw_surface *surface)
{
	struct tw_surface *parent = subsurface->parent;
	struct tw_subsurface *sibling;
	wl_list_for_each(sibling, &parent->subsurfaces, parent_link) {
		if (sibling->surface == surface && sibling != subsurface)
			return sibling;
	}
	wl_list_for_each(sibling, &parent->subsurfaces_pending,
	                 parent_pending_link) {
		if (sibling->surface == surface && sibling != subsurface)
			return sibling;
	}
	return NULL;
}

static void
subsurface_set_position(struct wl_client *client,
                        struct wl_resource *resource,
                        int32_t x,
                        int32_t y)
{
	struct tw_subsurface *subsurf =
		tw_subsurface_from_resource(resource);
	tw_subsurface_update_pos(subsurf, x, y);
}

static void
subsurface_place_above(struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *sibling)
{
	struct tw_subsurface *subsurface =
		tw_subsurface_from_resource(resource);
	struct tw_surface *sibling_surface =
		tw_surface_from_resource(sibling);
	struct tw_subsurface *sibling_subsurface =
		find_sibling_subsurface(subsurface, sibling_surface);
	if (!sibling_subsurface) {
		wl_resource_post_error(
			resource,
			WL_SUBSURFACE_ERROR_BAD_SURFACE,
			"wl_surface@%d is not sibling to "
			"wl_surface@%d",
			wl_resource_get_id(sibling_surface->resource),
			wl_resource_get_id(subsurface->surface->resource));
	} else {
		wl_list_remove(&subsurface->parent_pending_link);
		wl_list_insert(&sibling_subsurface->parent_pending_link,
		               &subsurface->parent_pending_link);
	}
}

static void
subsurface_place_below(struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *sibling)
{
	struct tw_subsurface *subsurface =
		tw_subsurface_from_resource(resource);
	struct tw_surface *sibling_surface =
		tw_surface_from_resource(sibling);
	struct tw_subsurface *sibling_subsurface =
		find_sibling_subsurface(subsurface, sibling_surface);
	if (!sibling_subsurface) {
		wl_resource_post_error(
			resource,
			WL_SUBSURFACE_ERROR_BAD_SURFACE,
			"wl_surface@%d is not sibling to "
			"wl_surface@%d",
			wl_resource_get_id(sibling_surface->resource),
			wl_resource_get_id(subsurface->surface->resource));
	} else {
		wl_list_remove(&subsurface->parent_pending_link);
		wl_list_insert(sibling_subsurface->parent_pending_link.prev,
		               &subsurface->parent_pending_link);
	}
}

static void
subsurface_set_sync(struct wl_client *client, struct wl_resource *resource)
{
	struct tw_subsurface *subsurface =
		tw_subsurface_from_resource(resource);
	subsurface->sync = true;
}

static void
subsurface_set_desync(struct wl_client *client, struct wl_resource *resource)
{
	struct tw_subsurface *subsurface =
		tw_subsurface_from_resource(resource);
	if (subsurface->sync) {
		subsurface->sync = false;
		if (!tw_subsurface_is_synched(subsurface))
			subsurface_commit_for_parent(subsurface, true);
	}
}

static const struct wl_subsurface_interface subsurface_impl = {
	.destroy = tw_resource_destroy_common,
	.set_position = subsurface_set_position,
	.place_above = subsurface_place_above,
	.place_below = subsurface_place_below,
	.set_sync = subsurface_set_sync,
	.set_desync = subsurface_set_desync,
};

static inline void
subsurface_set_role(struct tw_subsurface *subsurface, struct tw_surface *surf)
{
	surf->role.commit_private = subsurface;
        surf->role.commit = subsurface_commit_role;
        surf->role.name = "subsurface";
}

static inline void
subsurface_unset_role(struct tw_subsurface *subsurface)
{
	subsurface->surface->role.commit_private = NULL;
	subsurface->surface->role.commit = NULL;
	subsurface->surface->role.name  = NULL;
}

static void
subsurface_destroy(struct tw_subsurface *subsurface)
{
	if (!subsurface)
		return;

	wl_signal_emit(&subsurface->destroy, subsurface);

	wl_list_remove(&subsurface->surface_destroyed.link);
	if (subsurface->parent) {
		wl_list_remove(&subsurface->parent_link);
		wl_list_remove(&subsurface->parent_pending_link);
	}
	wl_resource_set_user_data(subsurface->resource, NULL);
	if (subsurface->surface)
		subsurface_unset_role(subsurface);

	subsurface->parent = NULL;
	free(subsurface);
}

static void
subsurface_destroy_resource(struct wl_resource *resource)
{
	struct tw_subsurface *subsurface =
		tw_subsurface_from_resource(resource);
	subsurface_destroy(subsurface);
}

static void
notify_subsurface_surface_destroy(struct wl_listener *listener, void *data)
{
	struct tw_subsurface *subsurface =
		wl_container_of(listener, subsurface, surface_destroyed);

	subsurface_destroy(subsurface);
}

/*******************************************************************************
 * tw_subsurface API
 ******************************************************************************/

WL_EXPORT struct tw_subsurface *
tw_subsurface_create(struct wl_client *client, uint32_t ver, uint32_t id,
                     struct tw_surface *surface, struct tw_surface *parent,
                     const struct tw_allocator *alloc)
{
	struct tw_subsurface *subsurface = NULL;
	struct wl_resource *resource = NULL;

	if (!tw_alloc_wl_resource_for_obj(resource, subsurface, client, id,ver,
	                                  wl_subsurface_interface, alloc)) {
		wl_client_post_no_memory(client);
		return NULL;
	}
	wl_resource_set_implementation(resource, &subsurface_impl,
	                               subsurface,
	                               subsurface_destroy_resource);
	subsurface->resource = resource;
	subsurface->surface = surface;
	subsurface->parent = parent;
	subsurface_set_role(subsurface, surface);
	// stacking order
	wl_signal_init(&subsurface->destroy);
	wl_list_init(&subsurface->parent_link);
	wl_list_init(&subsurface->parent_pending_link);
	wl_list_insert(parent->subsurfaces_pending.prev,
	               &subsurface->parent_pending_link);
	// add listeners
	wl_list_init(&subsurface->surface_destroyed.link);
	subsurface->surface_destroyed.notify =
		notify_subsurface_surface_destroy;
	wl_signal_add(&surface->signals.destroy,
	              &subsurface->surface_destroyed);

	return subsurface;
}

WL_EXPORT void
tw_subsurface_update_pos(struct tw_subsurface *sub,
                         int32_t sx, int32_t sy)
{
	struct tw_surface *surface = sub->surface;
	struct tw_surface *parent = sub->parent;

	sub->sx = sx;
	sub->sy = sy;
	tw_surface_set_position(surface, parent->geometry.x + sx,
	                        parent->geometry.y + sy);
}

WL_EXPORT bool
tw_subsurface_is_synched(struct tw_subsurface *subsurface)
{
	while (subsurface != NULL) {
		if (subsurface->sync)
			return true;
		if (!subsurface->parent)
			return false;
		subsurface = tw_surface_get_subsurface(subsurface->parent);
	}

	return false;
}
