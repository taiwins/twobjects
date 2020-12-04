/*
 * tablet.c - taiwins tablet implementation
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

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <taiwins/objects/utils.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/tablet.h>
#include <wayland-util.h>
#include "wayland-tablet-server-protocol.h"

static inline void
tablet_seat_destroy(struct tw_tablet_seat *tablet_seat)
{
	struct wl_resource *res, *res_tmp;
	wl_list_remove(&tablet_seat->link);
	wl_list_remove(&tablet_seat->seat_destroy.link);

	wl_resource_for_each_safe(res, res_tmp, &tablet_seat->clients) {
		wl_list_remove(wl_resource_get_link(res));
		wl_resource_set_user_data(res, NULL);
	}

	free(tablet_seat);
}

static void
notify_tablet_seat_seat_destroy(struct wl_listener *listener, void *data)
{
	struct tw_tablet_seat *tablet_seat =
		wl_container_of(listener, tablet_seat, seat_destroy);
	tablet_seat_destroy(tablet_seat);
}

static const struct zwp_tablet_manager_v2_interface tablet_manger_impl;

static inline struct tw_tablet_manager *
tablet_manager_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
	                               &zwp_tablet_manager_v2_interface,
	                               &tablet_manger_impl));
	return wl_resource_get_user_data(resource);
}

static const struct zwp_tablet_seat_v2_interface tablet_seat_impl = {
	.destroy = tw_resource_destroy_common,
};

/* static inline struct tw_tablet_seat * */
/* tablet_seat_from_resource(struct wl_resource *resource) */
/* { */
/*	assert(wl_resource_instance_of(resource, &zwp_tablet_seat_v2_interface, */
/*	                               &tablet_seat_impl)); */
/*	return wl_resource_get_user_data(resource); */
/* } */

static void
handle_tablet_seat_resource_destroy(struct wl_resource *res)
{
	wl_list_remove(wl_resource_get_link(res));
	wl_resource_set_user_data(res, NULL);
}

static void
handle_manager_get_tablet_seat(struct wl_client *client,
				struct wl_resource *manager_resource,
				uint32_t tablet_seat_id,
				struct wl_resource *seat_resource)
{
	struct wl_resource *resource = NULL;
	struct tw_tablet_manager *manager =
		tablet_manager_from_resource(manager_resource);
	struct tw_seat *seat =
		tw_seat_from_resource(seat_resource);
	uint32_t version = wl_resource_get_version(manager_resource);
	struct tw_tablet_seat *tablet_seat =
		tw_tablet_seat_find_create(manager, seat);

	if (!tablet_seat) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	resource = wl_resource_create(client, &zwp_tablet_seat_v2_interface,
	                              version, tablet_seat_id);
	if (!resource) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	wl_resource_set_implementation(resource, &tablet_seat_impl,
	                               tablet_seat,
	                               handle_tablet_seat_resource_destroy);
	wl_list_insert(tablet_seat->clients.prev,
	               wl_resource_get_link(resource));
	//TODO we should send a set of tablet_added and tool_added events here
}

static const struct zwp_tablet_manager_v2_interface tablet_manger_impl = {
	.get_tablet_seat = handle_manager_get_tablet_seat,
	.destroy = tw_resource_destroy_common,
};

static void
bind_tablet_manager(struct wl_client *client, void *data,
                    uint32_t version, uint32_t id)
{
	struct wl_resource *res =
		wl_resource_create(client, &zwp_tablet_manager_v2_interface,
		                   version, id);
	if (!res) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(res, NULL, data, NULL);
}

static void
notify_manager_display_destroy(struct wl_listener *listener, void *data)
{
	struct tw_tablet_seat *ts, *ts_tmp;
	struct tw_tablet_manager *manager =
		wl_container_of(listener, manager, display_destroy);
	wl_list_remove(&listener->link);
	wl_global_destroy(manager->global);

	wl_list_for_each_safe(ts, ts_tmp, &manager->tablet_seats, link)
		tablet_seat_destroy(ts);
}

WL_EXPORT bool
tw_tablet_manager_init(struct tw_tablet_manager *manager,
                       struct wl_display *display)
{
	if (!(manager->global =
	    wl_global_create(display, &zwp_tablet_manager_v2_interface,
	                     1, manager, bind_tablet_manager)))

		return false;
	manager->display = display;
	tw_set_display_destroy_listener(display, &manager->display_destroy,
	                                notify_manager_display_destroy);
	wl_list_init(&manager->tablet_seats);
	return true;
}

WL_EXPORT struct tw_tablet_manager *
tw_tablet_manager_create(struct wl_display *display)
{
	static struct tw_tablet_manager s_tablet = {0};
	if (!tw_tablet_manager_init(&s_tablet, display))
		return false;
	return &s_tablet;
}

WL_EXPORT struct tw_tablet_seat *
tw_tablet_seat_find_create(struct tw_tablet_manager *manager,
                           struct tw_seat *seat)
{
	struct tw_tablet_seat *tablet_seat = NULL;
	wl_list_for_each(tablet_seat, &manager->tablet_seats, link) {
		if (tablet_seat->seat == seat)
			return tablet_seat;
	}
	tablet_seat = calloc(1, sizeof(*tablet_seat));
	if (!tablet_seat)
		return NULL;
	tablet_seat->seat = seat;
	tablet_seat->manager = manager;
	wl_list_init(&tablet_seat->clients);
	wl_list_init(&tablet_seat->link);

	tw_signal_setup_listener(&seat->destroy_signal,
	                         &tablet_seat->seat_destroy,
	                         notify_tablet_seat_seat_destroy);

	wl_list_insert(manager->tablet_seats.prev, &tablet_seat->link);
	return tablet_seat;
}
