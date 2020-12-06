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
#include <stdlib.h>
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

static const char *basic_dev_exts[] = {
	VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
	VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
	VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
	VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,
	VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME,
};

static const char *dma_modifiers_exts[] = {
	VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,
	VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
	/* minor features that were intentionally left out in vk 1.0 */
	VK_KHR_MAINTENANCE1_EXTENSION_NAME,
	VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
	VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
	VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
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

static int
ext_names_cmp(const void *_a, const void *_b)
{
	const VkExtensionProperties *a = _a;
	const VkExtensionProperties *b = _b;
	return strcmp(a->extensionName, b->extensionName);
}

static int
is_ext_name(const void *_a, const void *_b)
{
	const char *a = _a;
	const VkExtensionProperties *b = _b;
	return strcmp(a, b->extensionName);
}

static inline bool
check_ext(const char *requested, int n_ext, VkExtensionProperties *ext_p)
{
	void *found = bsearch(requested, ext_p, n_ext,
	                      sizeof(VkExtensionProperties), is_ext_name);
	return found != 0;
}

static size_t
enum_instance_exts(char *exts[MAX_EXTS], const struct tw_vk_option *opts)
{
	uint32_t n_found_exts = 0, n_exts = 0;
	vkEnumerateInstanceExtensionProperties(NULL, &n_found_exts, NULL);
	VkExtensionProperties has_exts[n_found_exts+1];
	vkEnumerateInstanceExtensionProperties(NULL, &n_found_exts, has_exts);
	qsort(has_exts, n_found_exts, sizeof(has_exts[0]), ext_names_cmp);

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
		if (!check_ext(exts[i], n_found_exts, has_exts))
			return false;
	}
	return n_exts;
}

static VkInstance
create_instance(const struct tw_vk_option *opt)
{
	VkApplicationInfo app_info = {0};
	VkInstanceCreateInfo create_info = {0};
	char *exts[MAX_EXTS] = {0};
	size_t n_exts = 0;
	VkInstance instance = VK_NULL_HANDLE;
	const char *layers[1] = { VALIDARION_LAYER_NAME };

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

static bool
check_device_exts(const VkPhysicalDevice pdev, bool *has_modifiers)
{
	uint32_t n_exts = 0;
	bool has_dmabuf = true;

	vkEnumerateDeviceExtensionProperties(pdev, NULL, &n_exts, NULL);
	VkExtensionProperties pexts[n_exts+1];
	vkEnumerateDeviceExtensionProperties(pdev, NULL, &n_exts, pexts);
	qsort(pexts, n_exts, sizeof(pexts[0]), ext_names_cmp);

	for (unsigned i = 0; i < NUMOF(basic_dev_exts); i++) {
		has_dmabuf = has_dmabuf &&
			check_ext(basic_dev_exts[i], n_exts, pexts);
	}

	//check modifiers
	*has_modifiers = has_dmabuf;
	for (unsigned i = 0; i < NUMOF(dma_modifiers_exts); i++) {
		*has_modifiers = *has_modifiers &&
			check_ext(dma_modifiers_exts[i], n_exts, pexts);
	}
	return has_dmabuf;
}

static VkDevice
create_logical_device(const struct tw_vk_option *opt,
                      const VkInstance instance, const VkPhysicalDevice pdev)
{
	bool has_modifiers = true;
	float priority = 1.0f;
	VkDevice dev;
	VkDeviceQueueCreateInfo que_info = {0};
	VkDeviceCreateInfo info = {0};
	uint32_t n_exts = NUMOF(basic_dev_exts);
	char *exts[NUMOF(basic_dev_exts)+NUMOF(dma_modifiers_exts)];
	const char *layers[1] = { VALIDARION_LAYER_NAME };

	if (!check_device_exts(pdev, &has_modifiers))
		return VK_NULL_HANDLE;
	memcpy(exts, basic_dev_exts, sizeof(basic_dev_exts));
	if (has_modifiers) {
		memcpy(exts+n_exts, dma_modifiers_exts,
		       NUMOF(dma_modifiers_exts));
		n_exts += NUMOF(dma_modifiers_exts);
	}

	que_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	que_info.queueCount = 1;
	que_info.pQueuePriorities = &priority;

	info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	info.queueCreateInfoCount = 1;
	info.pQueueCreateInfos = &que_info;
	info.enabledExtensionCount = n_exts;
	info.ppEnabledExtensionNames = (const char * const *)exts;
	if (opt->requested_exts & TW_VK_WANT_VALIDATION_LAYER) {
		info.enabledLayerCount = NUMOF(layers);
		info.ppEnabledLayerNames = layers;
	}

	if (vkCreateDevice(pdev, &info, NULL, &dev) != VK_SUCCESS)
		return VK_NULL_HANDLE;

	return dev;
}

static inline VkCommandPool
create_cmd_pool(VkDevice dev)
{
	VkCommandPool pool = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo info = {0};

	info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	if (vkCreateCommandPool(dev, &info, NULL, &pool) != VK_SUCCESS)
		return VK_NULL_HANDLE;
	return pool;
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
	//TODO: get queue family that support graphics and transfer

	vk->device = create_logical_device(opt, vk->instance, vk->phydev);
	if (vk->device == VK_NULL_HANDLE)
		goto err_dev;

	vkGetDeviceQueue(vk->device, 0, 0, &vk->queue);
	if (vk->queue == VK_NULL_HANDLE)
		goto err_queue;

	vk->cmd_pool = create_cmd_pool(vk->device);
	if (vk->cmd_pool == VK_NULL_HANDLE)
		goto err_cmd_pool;

	return ret == VK_SUCCESS;
err_cmd_pool:
err_queue:
	vkDestroyDevice(vk->device, NULL);
err_dev:
	vkDestroyInstance(vk->instance, NULL);
	return false;
}

WL_EXPORT void
tw_vk_fini(struct tw_vk *vk)
{
	vkDestroyCommandPool(vk->device, vk->cmd_pool, NULL);
	vkDestroyDevice(vk->device, NULL);
	vkDestroyInstance(vk->instance, NULL);
}
