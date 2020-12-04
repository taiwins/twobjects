/*
 * tablet_pad.h - taiwins tablet pad interface header
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

#ifndef TW_TABLET_PAD_H
#define TW_TABLET_PAD_H

#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

/* the tablet can actually used as remote controller if we want */

struct tw_tablet;
struct tw_tablet_seat;
struct tw_tablet_pad;
struct tw_tablet_pad_ring;
struct tw_tablet_pad_strip;
struct tw_tablet_pad_group;

/** group would be annouced during the pad initialization, the group contains
 * the rings, strip and buttons
 */
struct tw_tablet_pad_group {
	uint32_t idx;

	uint32_t n_buttons, n_strip, n_rings;

	struct wl_list clients;

	struct wl_list strips;
	struct wl_list rings;
};

/**
 * unlike tablet_tool, a tablet_pad is usually associate with a tablet.
 */
struct tw_tablet_pad {
	struct tw_tablet_seat *seat;

	/* a tablet pad has number of groups, it usually has at least one
	 * group */
	struct wl_list groups;

	struct tw_tablet_pad_group default_group;
};


#ifdef  __cplusplus
}
#endif


#endif /* EOF */
