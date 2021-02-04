#include <taiwins/objects/vulkan.h>
#include <vulkan/vulkan_core.h>

int main(int argc, char *argv[])
{
	struct tw_vk vk;
	struct tw_vk_option opt = {
		.requested_exts = TW_VK_WANT_VALIDATION_LAYER,
		.instance_name = "test-vulkan",
		.vendor_id = 0x8086,
		.device_id = 0x3e9b,
	};

	tw_vk_init(&vk, &opt);
	tw_vk_fini(&vk);
	return 0;
}
