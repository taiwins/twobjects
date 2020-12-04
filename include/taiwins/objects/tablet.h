/*
 * tablet.h - taiwins zwp_tablet header
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

#ifndef TW_TABLET_H
#define TW_TABLET_H

#include <stdint.h>
#include <stdlib.h>
#include <wayland-server.h>

#include "seat.h"
#include "tablet_tool.h"
#include "tablet_pad.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_tablet_manager {
	struct wl_display *display;
	struct wl_global *global;

	struct wl_list tablet_seats;

	struct wl_listener display_destroy;
};

struct tw_tablet_seat {
	struct tw_seat *seat;
	struct tw_tablet_manager *manager;
	struct wl_list clients;

	struct wl_listener seat_destroy;

	struct wl_list link;
	struct wl_list tablets;
	struct wl_list tools;
};

struct tw_tablet {
	struct tw_tablet_seat *seat;
	struct wl_list link; /**< tw_tablet_seat: tablets */
	uint32_t vid, pid;
	char name[32], path[64];

	struct wl_list pads;
	struct tw_tablet_pad default_pad;
};

struct tw_tablet_manager *
tw_tablet_manager_create(struct wl_display *display);

bool
tw_tablet_manager_init(struct tw_tablet_manager *manager,
                       struct wl_display *display);
struct tw_tablet_seat *
tw_tablet_seat_find_create(struct tw_tablet_manager *manager,
                           struct tw_seat *seat);
/* add a new tablet device, additional info would be about the  */
struct tw_tablet *
tw_tablet_seat_add_device(struct tw_tablet_seat *seat,
                          const char *name, const char *path,
                          /* vid and pid is optional */
                          /* TODO additional arg for initializing default pad*/
                          uint32_t *vid, uint32_t *pid);
/* destroy the tablets and all its related tablet_pads */
void
tw_tablet_remove(struct tw_tablet *tablet);


#ifdef  __cplusplus
}
#endif

#endif /* EOF */
