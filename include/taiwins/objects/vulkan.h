/*
 * vulkan.h - taiwins vulkan renderer interface
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

#ifndef TW_VULKAN_H
#define TW_VULKAN_H

#include <stdint.h>
#include <vulkan/vulkan.h>
#include <wayland-server.h>

#include "dmabuf.h"
#include "drm_formats.h"

#ifdef  __cplusplus
extern "C" {
#endif

enum tw_vk_want_ext {
	TW_VK_WANT_VALIDATION_LAYER = (1 << 0),
	TW_VK_WANT_DIRECT_MODE_DISPLAY = (1 << 1),
};

struct tw_vk_option {
	const char *instance_name;
	uint32_t requested_exts;
	/* the udev attribute "vendor" and "device" for matching phy dev here
	 * in vulkan, for example: Intel would have 0x8086, NV has 0x10DE */
	uint64_t vendor_id, device_id;
};


struct tw_vk {
	struct wl_display *wl_display;

	VkInstance instance;
	VkPhysicalDevice phydev;
	VkDevice device;

	unsigned int internal_format;
	uint64_t min_extmem_alignment;
	struct tw_drm_formats drm_formats;
};

bool
tw_vk_init(struct tw_vk *vk, const struct tw_vk_option *opt);

void
tw_vk_fini(struct tw_vk *vk);



#ifdef  __cplusplus
}
#endif

#endif /* EOF */
