/*
 * tablet_tool.h - taiwins tablet tool interface header
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

#ifndef TW_TABLET_TOOL_H
#define TW_TABLET_TOOL_H

#include <stdint.h>
#include <wayland-server.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct tw_tablet;
struct tw_tablet_seat;
struct tw_tablet_tool;

//don't use this, do that in the function.
struct tw_tablet_tool_grab {
	void (*proximity_in)(struct tw_tablet_tool *tool,
	                     struct tw_tablet *tablet,
	                     struct wl_resource *surface);
	void (*proximity_out)(struct tw_tablet_tool *tool);
	void (*done)(struct tw_tablet_tool *tool);
	void (*up)(struct tw_tablet_tool *tool);
	void (*motion)(struct tw_tablet_tool *tool, double sx, double sy);
	void (*pressure)(struct tw_tablet_tool *tool, uint32_t unit);
	void (*distance)(struct tw_tablet_tool *tool, uint32_t unit);
	void (*tilt)(struct tw_tablet_tool *tool, double x, double y);
	void (*rotation)(struct tw_tablet_tool *tool, double degree);
	void (*slider)(struct tw_tablet_tool *tool);
	//button and then frame
};

struct tw_tablet_tool {

	struct wl_list link; /**< tw_tablet_seat:tools */
	struct tw_tablet_seat *seat;

	struct wl_resource *current_surface;
};

#ifdef  __cplusplus
}
#endif

#endif /* EOF */
