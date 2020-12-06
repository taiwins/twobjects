/*
 * context.c - taiwins vulkan renderer context
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

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <taiwins/objects/vulkan.h>
#include <vulkan/vulkan_core.h>

#define MAX_EXTS 128
#define VALIDARION_LAYER_NAME "VK_LAYER_KHRONOS_validation"
#define EXTERN_MEM_HOST_PROP_TYPE \
	VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT


#ifndef NUMOF
#define NUMOF(x) (sizeof(x) / sizeof(*x))
#endif

static const char *basic_vk_exts[] = {
	VK_KHR_SURFACE_EXTENSION_NAME,
	/* required for wayland display */
	VK_KHR_DISPLAY_EXTENSION_NAME,
	/* the external_* exts are required to build a vulkan WSI for
	 * wayland compositor */
	VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
	VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,
	VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
	/* need for the external exts above */
	VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
};

static bool
layer_supported(const char *layer_name)
{
	uint32_t n_layers = 0;
        vkEnumerateInstanceLayerProperties(&n_layers, NULL);

        VkLayerProperties layers[n_layers+1];
        vkEnumerateInstanceLayerProperties(&n_layers, layers);

        for (unsigned i = 0; i < n_layers; i++) {
	        if (strcmp(layers[i].layerName, layer_name) == 0)
		        return true;
        }
        return false;
}

static size_t
enum_instance_exts(char *exts[MAX_EXTS], const struct tw_vk_option *opts)
{
	uint32_t n_found_exts = 0, n_exts = 0;
	vkEnumerateInstanceExtensionProperties(NULL, &n_found_exts, NULL);
	VkExtensionProperties has_exts[n_found_exts+1];
	vkEnumerateInstanceExtensionProperties(NULL, &n_found_exts, has_exts);

	//basic exts need to be supported
	memcpy(exts, basic_vk_exts, sizeof(basic_vk_exts));
	n_exts = NUMOF(basic_vk_exts);

	if ((n_exts < MAX_EXTS) &&
	    opts->requested_exts & TW_VK_WANT_DIRECT_MODE_DISPLAY) {
		exts[n_exts] = VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME;
		n_exts++;
	}
	//verify the exts
	for (unsigned i = 0; i < n_exts; i++) {
		bool found = false;
		for (unsigned j = 0; j < n_found_exts; j++) {
			if (strcmp(has_exts[j].extensionName, exts[i]) == 0) {
				found = true;
				break;
			}
		}
		if (!found)
			return 0;
	}
	return n_exts;
}

static uint64_t
get_extern_mem_min_alignment(VkPhysicalDeviceProperties2 *prop)
{
	VkPhysicalDeviceExternalMemoryHostPropertiesEXT *mem_prop =
		prop->pNext;

	while (mem_prop) {
		if (mem_prop->sType == EXTERN_MEM_HOST_PROP_TYPE)
			return mem_prop->minImportedHostPointerAlignment;
		mem_prop = mem_prop->pNext;
	}
	return 0;
}

static VkPhysicalDevice
find_phy_dev(uint64_t *alignment, const struct tw_vk_option *opt,
             const VkInstance instance)
{
	uint32_t n_phy_devs = 0;
	VkPhysicalDevice phy_dev = VK_NULL_HANDLE;
	bool any_device = opt->device_id == 0 && opt->vendor_id == 0;
	bool device_match = false;

	vkEnumeratePhysicalDevices(instance, &n_phy_devs, NULL);
	if (!n_phy_devs)
		return VK_NULL_HANDLE;

	VkPhysicalDevice devs[n_phy_devs+1];
	vkEnumeratePhysicalDevices(instance, &n_phy_devs, devs);

	for (unsigned i = 0; i < n_phy_devs; i++) {
		VkPhysicalDeviceProperties2 props2 = {0};
		VkPhysicalDeviceFeatures feats = {0};
		VkPhysicalDeviceProperties *props = &props2.properties;

		props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

		//from this properties2, we need
                vkGetPhysicalDeviceProperties2(devs[i], &props2);
                vkGetPhysicalDeviceFeatures(devs[i], &feats);
                *alignment = get_extern_mem_min_alignment(&props2);
                device_match = (opt->vendor_id == props->vendorID &&
                                opt->device_id == props->deviceID);
                //check for device
                if (device_match || any_device) {
	                phy_dev = devs[i];
	                break;
                }
	}
	//we are not sure if we can get to use
	return phy_dev;
}

static VkInstance
create_instance(const struct tw_vk_option *opt)
{
	VkApplicationInfo app_info = {0};
	VkInstanceCreateInfo create_info = {0};
	char *exts[MAX_EXTS] = {0};
	size_t n_exts = 0;
	VkInstance instance = VK_NULL_HANDLE;

	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = opt->instance_name;
	app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.pEngineName = "No Engine";
	app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.apiVersion = VK_API_VERSION_1_1; //need for external_* exts

	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &app_info;
	create_info.enabledLayerCount = 0;
	if (opt->requested_exts & TW_VK_WANT_VALIDATION_LAYER) {
		const char *layers[1] = { VALIDARION_LAYER_NAME };

		create_info.enabledLayerCount = NUMOF(layers);
		create_info.ppEnabledLayerNames = layers;
	}
	if ((n_exts = enum_instance_exts(exts, opt)) == 0)
	    return false;
	create_info.enabledExtensionCount = n_exts;
	create_info.ppEnabledExtensionNames = (const char * const *)exts;

	vkCreateInstance(&create_info, NULL, &instance);

	return instance;
}

WL_EXPORT bool
tw_vk_init(struct tw_vk *vk, const struct tw_vk_option *opt)
{
	VkResult ret = VK_SUCCESS;

	if ((opt->requested_exts & TW_VK_WANT_VALIDATION_LAYER) &&
	    !layer_supported(VALIDARION_LAYER_NAME))
		return false;

	vk->instance = create_instance(opt);
	if (!vk->instance)
		return false;

	vk->phydev = find_phy_dev(&vk->min_extmem_alignment, opt,
	                          vk->instance);
	if (vk->phydev == VK_NULL_HANDLE)
		goto err_dev;

	return ret == VK_SUCCESS;
err_dev:
	vkDestroyInstance(vk->instance, NULL);
	return false;
}

WL_EXPORT void
tw_vk_fini(struct tw_vk *vk)
{
	vkDestroyInstance(vk->instance, NULL);
}
