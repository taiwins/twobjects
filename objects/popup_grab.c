/*
 * popup_grab.c - taiwins server common interface for popup windows
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

#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#include <taiwins/objects/utils.h>
#include <taiwins/objects/seat.h>
#include <taiwins/objects/popup_grab.h>

static const struct tw_pointer_grab_interface popup_pointer_grab_impl;
static const struct tw_touch_grab_interface popup_touch_grab_impl;

static inline bool
tw_popup_grab_is_current(struct tw_seat *seat)
{
	return (seat->pointer.grab &&
	        seat->pointer.grab->impl == &popup_pointer_grab_impl) ||
		(seat->touch.grab &&
		 seat->touch.grab->impl == &popup_touch_grab_impl);
}

static void tw_popup_grab_close(struct tw_popup_grab *grab);

static void
popup_pointer_grab_button(struct tw_seat_pointer_grab *grab,
                          uint32_t time_msec, uint32_t button,
                          enum wl_pointer_button_state state)
{
	struct tw_pointer *pointer = &grab->seat->pointer;
	struct wl_resource *wl_surface = grab->data;
	bool on_popup = (wl_surface == pointer->focused_surface);
	struct tw_popup_grab *popup_grab =
		wl_container_of(grab, popup_grab, pointer_grab);
	pointer->default_grab.impl->button(&pointer->default_grab,
	                                   time_msec, button, state);
	//TODO it is either we have pointer to be able to focus on popup before
	//this or defer the grab after the release button.
	if (!on_popup)
		tw_popup_grab_close(popup_grab);
}

static void
popup_pointer_grab_enter(struct tw_seat_pointer_grab *grab,
                         struct wl_resource *surface, double sx, double sy)
{
	struct tw_pointer *pointer = &grab->seat->pointer;
	pointer->default_grab.impl->enter(&pointer->default_grab, surface,
	                                  sx, sy);
}

static void
popup_pointer_grab_motion(struct tw_seat_pointer_grab *grab,
                          uint32_t time_msec,
                          double sx, double sy)
{
	struct tw_pointer *pointer = &grab->seat->pointer;
	pointer->default_grab.impl->motion(&pointer->default_grab, time_msec,
	                                   sx, sy);
}

static void
popup_pointer_grab_axis(struct tw_seat_pointer_grab *grab, uint32_t time_msec,
                        enum wl_pointer_axis orientation, double value,
                        int32_t value_discrete,
                        enum wl_pointer_axis_source source)
{
	struct tw_pointer *pointer = &grab->seat->pointer;
	pointer->default_grab.impl->axis(&pointer->default_grab, time_msec,
	                                 orientation, value, value_discrete,
	                                 source);
}

static void
popup_pointer_grab_frame(struct tw_seat_pointer_grab *grab)
{
	struct tw_pointer *pointer = &grab->seat->pointer;
	pointer->default_grab.impl->frame(&pointer->default_grab);
}

static void
popup_pointer_grab_cancel(struct tw_seat_pointer_grab *grab)
{
}

static const struct tw_pointer_grab_interface popup_pointer_grab_impl = {
	.enter = popup_pointer_grab_enter,
	.motion = popup_pointer_grab_motion,
	.button = popup_pointer_grab_button,
	.axis = popup_pointer_grab_axis,
	.frame = popup_pointer_grab_frame,
	.cancel = popup_pointer_grab_cancel,
};


static void
popup_touch_grab_down(struct tw_seat_touch_grab *grab, uint32_t time_msec,
                      uint32_t touch_id, double sx, double sy)
{
	struct tw_touch *touch = &grab->seat->touch;
	struct wl_resource *wl_surface = grab->data;
	struct tw_popup_grab *popup_grab =
		wl_container_of(grab, popup_grab, touch_grab);
	bool on_popup = (wl_surface == touch->focused_surface);

	touch->default_grab.impl->down(&touch->default_grab,
	                               time_msec, touch_id,
	                               sx, sy);
	if (!on_popup)
		tw_popup_grab_close(popup_grab);
}

static void
popup_touch_grab_up(struct tw_seat_touch_grab *grab, uint32_t time_msec,
                    uint32_t touch_id)
{
	struct tw_touch *touch = &grab->seat->touch;
	touch->default_grab.impl->up(&touch->default_grab, time_msec,
	                             touch_id);
}

static void
popup_touch_grab_motion(struct tw_seat_touch_grab *grab, uint32_t time_msec,
                        uint32_t touch_id, double sx, double sy)
{
	struct tw_touch *touch = &grab->seat->touch;
	touch->default_grab.impl->motion(&touch->default_grab, time_msec,
	                                 touch_id, sx, sy);
}

static void
popup_touch_grab_enter(struct tw_seat_touch_grab *grab,
                       struct wl_resource *surface, double sx, double sy)
{
	struct tw_touch *touch = &grab->seat->touch;
	touch->default_grab.impl->enter(&touch->default_grab, surface, sx, sy);
}

static void
popup_touch_grab_touch_cancel(struct tw_seat_touch_grab *grab)
{
	struct tw_touch *touch = &grab->seat->touch;
	touch->default_grab.impl->touch_cancel(&touch->default_grab);
}

static void
popup_touch_grab_cancel(struct tw_seat_touch_grab *grab)
{
}

static const struct tw_touch_grab_interface popup_touch_grab_impl = {
	.down = popup_touch_grab_down,
	.up = popup_touch_grab_up,
	.motion = popup_touch_grab_motion,
	.enter = popup_touch_grab_enter,
	.touch_cancel = popup_touch_grab_touch_cancel,
	.cancel = popup_touch_grab_cancel,
};

void
tw_popup_grab_close(struct tw_popup_grab *grab)
{
	struct tw_seat *seat = grab->pointer_grab.seat;
	struct tw_pointer *pointer = &seat->pointer;
	struct tw_touch *touch = &seat->touch;

	//we need to end grab unconditionally since we could be nested.
	tw_pointer_end_grab(pointer);
	tw_touch_end_grab(touch);
	wl_signal_emit(&grab->close, grab);

	tw_reset_wl_list(&grab->parent_destroy.link);
	tw_reset_wl_list(&grab->resource_destroy.link);

	//also, if there is a parent, we switch to parent
	if (grab->parent_grab)
		tw_popup_grab_start(grab->parent_grab, seat);
}

static void
notify_resource_destroy(struct wl_listener *listener, void *userdata)
{
	struct tw_popup_grab *grab =
		wl_container_of(listener, grab, resource_destroy);
	tw_popup_grab_close(grab);
}

static void
notify_parent_destroy(struct wl_listener *listener, void *userdata)
{
	struct tw_popup_grab *parent = userdata;
	struct tw_popup_grab *grab =
		wl_container_of(listener, grab, parent_destroy);

        tw_reset_wl_list(&grab->parent_destroy.link);
	if (parent->parent_grab && grab != parent->parent_grab) {
		grab->parent_grab = parent->parent_grab;
		tw_signal_setup_listener(&parent->parent_grab->close,
		                         &grab->parent_destroy,
		                         notify_parent_destroy);
	} else {
		grab->parent_grab = NULL;
	}
}

WL_EXPORT void
tw_popup_grab_start(struct tw_popup_grab *grab, struct tw_seat *seat)
{
	grab->seat = seat;
	if (tw_popup_grab_is_current(seat)) {
		grab->parent_grab =
			wl_container_of(seat->pointer.grab,
			                grab->parent_grab, pointer_grab);
		tw_signal_setup_listener(&grab->parent_grab->close,
		                         &grab->parent_destroy,
		                         notify_parent_destroy);
	}

	grab->pointer_grab.seat = seat;
	grab->touch_grab.seat = seat;
	if (seat->capabilities & WL_SEAT_CAPABILITY_POINTER) {
		tw_pointer_start_grab(&seat->pointer, &grab->pointer_grab);
		//TODO: this is a hack, should work most of the time
		tw_pointer_notify_enter(&seat->pointer, grab->focus, 0, 0);
	}
	if (seat->capabilities & WL_SEAT_CAPABILITY_TOUCH) {
		tw_touch_start_grab(&seat->touch, &grab->touch_grab);
		tw_touch_notify_enter(&seat->touch, grab->focus, 0, 0);
	}
}

WL_EXPORT void
tw_popup_grab_init(struct tw_popup_grab *grab, struct tw_surface *surface,
                   struct wl_resource *obj)
{
	struct wl_resource *wl_surface = surface->resource;
	grab->pointer_grab.data = wl_surface;
	grab->pointer_grab.impl = &popup_pointer_grab_impl;

	grab->touch_grab.data = wl_surface;
	grab->touch_grab.impl = &popup_touch_grab_impl;

	grab->focus = wl_surface;
	grab->interface = obj;
	tw_set_resource_destroy_listener(obj,
	                                 &grab->resource_destroy,
	                                 notify_resource_destroy);
	wl_list_init(&grab->parent_destroy.link);
	wl_signal_init(&grab->close);
}
