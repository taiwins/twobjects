/*
 * egl.c - taiwins backend egl renderer interface
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

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <wayland-client-protocol.h>
#ifdef HAVE_EGLMESAEXT
#include <EGL/eglmesaext.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <wayland-server.h>
#include <drm_fourcc.h>
#include <pixman.h>

#include <taiwins/objects/logger.h>
#include <taiwins/objects/dmabuf.h>
#include <taiwins/objects/drm_formats.h>
#include <taiwins/objects/egl.h>
#include <taiwins/objects/matrix.h>

//we need EGL context here mostly just for import buffer and export buffers.
static PFNEGLGETPLATFORMDISPLAYEXTPROC _get_platform_display = NULL;
static PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC _create_window_surface = NULL;
static PFNEGLCREATEIMAGEKHRPROC _create_egl_image = NULL;
static PFNEGLDESTROYIMAGEKHRPROC _destroy_egl_image = NULL;
static PFNEGLQUERYWAYLANDBUFFERWL _query_wl_buffer = NULL;
static PFNEGLBINDWAYLANDDISPLAYWL _bind_wl_display = NULL;
static PFNEGLUNBINDWAYLANDDISPLAYWL _unbind_wl_display = NULL;
static PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC _swap_buffer_with_damage = NULL;
static PFNEGLQUERYDMABUFFORMATSEXTPROC _query_dmabuf_formats = NULL;
static PFNEGLQUERYDMABUFMODIFIERSEXTPROC _query_dmabuf_modifiers = NULL;
static PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC _export_dmabuf_image_query = NULL;
static PFNEGLEXPORTDMABUFIMAGEMESAPROC _export_dmabuf_image = NULL;

const char *
platform_to_extension(EGLenum platform)
{
	switch (platform) {
	case EGL_PLATFORM_GBM_KHR:
		return "gbm";
	case EGL_PLATFORM_WAYLAND_KHR:
		return "wayland";
	case EGL_PLATFORM_X11_KHR:
		return "x11";
	case EGL_PLATFORM_SURFACELESS_MESA:
		return "surfaceless";
	case EGL_PLATFORM_DEVICE_EXT:
		return "device";
	default:
		assert(0 && "bad EGL platform enum");
		return "";
	}
}

static inline bool
check_egl_ext(const char *exts, const char *ext, bool required)
{
	if (strstr(exts, ext) == NULL) {
		tw_logl_level(required ? TW_LOG_ERRO : TW_LOG_WARN,
		              "EGL extension %s not found", ext);
		return false;
	}
	return true;
}

static bool
get_egl_proc(void *addr, const char *name)
{
	void *proc = (void *)eglGetProcAddress(name);
	if (!proc) {
		tw_logl_level(TW_LOG_ERRO, "function %s not found", name);
		return false;
	}
	*(void **)addr = proc;
	return true;
}

static bool
setup_egl_basic_exts(struct tw_egl *egl)
{
	const char *exts_str = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);

	if (!exts_str) {
		tw_logl_level(TW_LOG_ERRO, "Failed to query EGL extensions");
		return false;
	}
	//platform display
	if (!check_egl_ext(exts_str, "EGL_EXT_platform_base", true))
		return false;
	if (!get_egl_proc(&_get_platform_display,
	                  "eglGetPlatformDisplayEXT"))
		return false;
	if (!get_egl_proc(&_create_window_surface,
	                  "eglCreatePlatformWindowSurfaceEXT"))
		return false;
	return true;
}

static bool
setup_egl_display(struct tw_egl *egl, const struct tw_egl_options *opts)
{
	EGLint major, minor;

	egl->display = _get_platform_display(opts->platform,
	                                     opts->native_display,
	                                     opts->platform_attribs);
	if (egl->display == EGL_NO_DISPLAY) {
		tw_logl_level(TW_LOG_ERRO, "Failed to create EGL display");
		return false;
	}
	egl->platform = opts->platform;

	if (!eglInitialize(egl->display, &major, &minor)) {
		tw_logl_level(TW_LOG_ERRO, "Failed to initialize EGL");
		return false;
	}

	return true;
}

static bool
setup_egl_client_extensions(struct tw_egl *egl)
{
	const char *exts_str = eglQueryString(egl->display, EGL_EXTENSIONS);
	if (!exts_str) {
		tw_logl_level(TW_LOG_ERRO, "Failed to query EGL extensions");
		return false;
	}
	//create/destroy EGLImage
	if (check_egl_ext(exts_str, "EGL_KHR_image_base", false)) {
		egl->image_base_khr = true;
		if (!get_egl_proc(&_create_egl_image,
		                  "eglCreateImageKHR"))
			return false;
		if (!get_egl_proc(&_destroy_egl_image,
		                  "eglDestroyImageKHR"))
			return false;
	}
	//buffer age
	if (check_egl_ext(exts_str, "EGL_EXT_buffer_age", false))
		egl->query_buffer_age = true;
	//swap buffer with damage
	if (check_egl_ext(exts_str, "EGL_KHR_swap_buffers_with_damage",
	                  false)) {
		if (!get_egl_proc(&_swap_buffer_with_damage,
		                  "eglSwapBuffersWithDamageKHR"))
			return false;
	} else if (check_egl_ext(exts_str, "EGL_EXT_swap_buffers_with_damage",
	                         false)) {
		if (!get_egl_proc(&_swap_buffer_with_damage,
		                  "eglSwapBuffersWithDamageEXT"))
			return false;
	}
	//dmabuf import
	if (check_egl_ext(exts_str, "EGL_EXT_image_dma_buf_import", false) &&
	    check_egl_ext(exts_str, "EGL_EXT_image_dma_buf_import_modifiers",
	                  false)) {
		egl->import_dmabuf = true;
		egl->import_dmabuf_modifiers = true;
		if (!get_egl_proc(&_query_dmabuf_formats,
		                  "eglQueryDmaBufFormatsEXT"))
			return false;
		if (!get_egl_proc(&_query_dmabuf_modifiers,
		                  "eglQueryDmaBufModifiersEXT"))
			return false;
	}
	//dmabuf export
	if (check_egl_ext(exts_str, "EGL_MESA_image_dma_buf_export", false)) {
		if (!get_egl_proc(&_export_dmabuf_image_query,
		                  "eglExportDMABUFImageQueryMESA"))
			return false;
		if (!get_egl_proc(&_export_dmabuf_image,
		                  "eglExportDMABUFImageMESA"))
			return false;
	}
	//bind wayland display
	if (check_egl_ext(exts_str, "EGL_WL_bind_wayland_display", false)) {
		if (!get_egl_proc(&_bind_wl_display,
			    "eglBindWaylandDisplayWL"))
			return false;
		if (!get_egl_proc(&_unbind_wl_display,
			    "eglUnbindWaylandDisplayWL"))
			return false;
		if (!get_egl_proc(&_query_wl_buffer,
		                  "eglQueryWaylandBufferWL"))
			return false;
	}

	return true;

}

static EGLConfig
choose_egl_config(EGLDisplay display, EGLConfig *configs, int count,
                  const struct tw_egl_options *opts)
{
	if (!opts->visual_id)
		return configs[0];

	for (int i = 0; i < count; i++) {
		EGLint visual_id;
		if (!eglGetConfigAttrib(display, configs[i],
		                        EGL_NATIVE_VISUAL_ID, &visual_id))
			continue;
		if (opts->visual_id == visual_id)
			return configs[i];
	}
	return EGL_NO_CONFIG_KHR;
}

static bool
setup_egl_config(struct tw_egl *egl, const struct tw_egl_options *opts)
{
	EGLint count = 0, matched = 0, ret = 0;

	ret = eglGetConfigs(egl->display, NULL, 0, &count);
	if (ret == EGL_FALSE || count == 0) {
		tw_logl_level(TW_LOG_ERRO, "eglGetConfigs failed");
		return false;
	}

	EGLConfig configs[count];

	if(eglChooseConfig(egl->display, opts->context_attribs, configs,
	                   count, &matched) == EGL_FALSE) {
		tw_logl_level(TW_LOG_ERRO, "eglChooseConfig failed");
		return false;
	}
	egl->config = choose_egl_config(egl->display, configs, matched, opts);
	if (egl->config == EGL_NO_CONFIG_KHR)
		return false;
	//store the surface type for future queries
	eglGetConfigAttrib(egl->display, egl->config,
	                   EGL_SURFACE_TYPE, &egl->surface_type);

	return true;
}

static bool
setup_egl_context(struct tw_egl *egl)
{
	EGLint attrs[16] = {EGL_CONTEXT_CLIENT_VERSION, 0};
	unsigned nattr = 2;

	const char *exts_str = eglQueryString(egl->display, EGL_EXTENSIONS);
	bool ext_context_priority =
		check_egl_ext(exts_str, "EGL_IMG_context_priority", false);

	if (ext_context_priority) {
		attrs[nattr++] = EGL_CONTEXT_PRIORITY_LEVEL_IMG;
		attrs[nattr++] = EGL_CONTEXT_PRIORITY_HIGH_IMG;
	}
	attrs[nattr] = EGL_NONE;
	//try GLES 3 first
	attrs[1] = 3;
	egl->context = eglCreateContext(egl->display, egl->config,
	                                EGL_NO_CONTEXT, attrs);
	if (egl->context == EGL_NO_CONTEXT) {
		attrs[1] = 2;
		egl->context = eglCreateContext(egl->display, egl->config,
		                                EGL_NO_CONTEXT, attrs);
		if (egl->context == EGL_NO_CONTEXT) {
			tw_logl_level(TW_LOG_ERRO, "eglCreateContext failed");
			return false;
		}
	}

	if (ext_context_priority) {
		EGLint level = EGL_CONTEXT_PRIORITY_LOW_IMG;
		eglQueryContext(egl->display, egl->context,
		                EGL_CONTEXT_PRIORITY_LEVEL_IMG, &level);
		if (level != EGL_CONTEXT_PRIORITY_HIGH_IMG)
			tw_logl_level(TW_LOG_WARN, "failed to obtain the "
			              "high priority context");
	}
	if (!eglMakeCurrent(egl->display, EGL_NO_SURFACE,
	                    EGL_NO_SURFACE, egl->context)) {
		tw_logl_level(TW_LOG_ERRO, "eglMakeCurrent failed");
		eglDestroyContext(egl->display, egl->context);
		egl->context = EGL_NO_CONTEXT;
		return false;
	}
	if (tw_egl_check_gl_ext(egl, "GL_OES_rgb8_rgba8") ||
	    tw_egl_check_gl_ext(egl, "GL_OES_required_internalformat") ||
	    tw_egl_check_gl_ext(egl, "GL_ARM_rgba8")) {
		egl->internal_format = GL_RGBA8_OES;
	} else {
		tw_logl("GL_RGBA8_OES not supported, performance may be "
		        "affected");
		egl->internal_format = GL_RGBA4;
	}

	return true;
}

static int
get_dmabuf_formats(struct tw_egl *egl, int *formats)
{
	int num = -1;

	if (!egl->import_dmabuf || !_query_dmabuf_formats)
		return -1;
	//using DRM_FORMAT_ARGB8888 and DRM_FORMAT_XRGB8888 for backup
	if (!egl->import_dmabuf_modifiers) {
		const int fallbacks[2] = {
			DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888
		};
		if (formats)
			memcpy(formats, fallbacks, sizeof(fallbacks));
		return 2;
	}
	if (!_query_dmabuf_formats(egl->display, 0, NULL, &num)) {
		tw_logl_level(TW_LOG_WARN, "Failed to query number of dmabuf"
		              " formats");
		return -1;
	}

	if (formats)
		_query_dmabuf_formats(egl->display, num, formats, &num);
	return num;
}

static int
get_dmabuf_modifiers(struct tw_egl *egl, int fmt, uint64_t *modifiers,
                     bool *external_only)
{
	EGLint num;

	if (!_query_dmabuf_modifiers)
		return 0;
	if (!_query_dmabuf_modifiers(egl->display, fmt, 0, NULL, NULL, &num))
		return -1;
	if (num == 0)
		return 0;
	if (modifiers && external_only) {
		EGLBoolean _external_only[num];
		_query_dmabuf_modifiers(egl->display, fmt, num, modifiers,
		                        _external_only, &num);
		for (int i = 0; i < num; i++)
			external_only[i] = _external_only[i] == EGL_TRUE;
	}
	return num;
}

static void
init_egl_dma_formats(struct tw_egl *egl)
{
	int n_formats = get_dmabuf_formats(egl, NULL);

	tw_drm_formats_init(&egl->drm_formats);
	if (n_formats <= 0)
		return;

	int formats[n_formats];
	memset(formats, 0, sizeof(int)*n_formats);
	get_dmabuf_formats(egl, formats);

	for (int i = 0; i < n_formats; i++) {
		uint32_t fmt = formats[i];
		int n_modifiers =
			get_dmabuf_modifiers(egl, fmt, NULL, NULL);

		if (n_modifiers < 0)
			continue;
		if (n_modifiers == 0) {
			uint64_t invalid_modifier = DRM_FORMAT_INVALID;
			bool no = false;
			tw_drm_formats_add_format(&egl->drm_formats, fmt,
			                          1, &invalid_modifier, &no);
			continue;
		}

		uint64_t modifiers[n_modifiers];
		bool external_only[n_modifiers];

		get_dmabuf_modifiers(egl, fmt, modifiers, external_only);

		if (!tw_drm_formats_add_format(&egl->drm_formats, fmt,
		                               n_modifiers, modifiers,
		                               external_only))
			continue;
	}

	char str_formats[n_formats * 5 + 1];
	for (int i = 0; i < n_formats; i++)
		snprintf(&str_formats[i*5], 6, "%.4s ", (char *)&formats[i]);
	tw_logl("EGL Supported dmabuf formats: %s", str_formats);
}

static void
print_egl_info(struct tw_egl *egl)
{
	EGLint major, minor;
	char *ext = NULL, *exts_str = NULL;

	eglQueryContext(egl->display, egl->context, EGL_CONTEXT_MAJOR_VERSION,
	                &major);
	eglQueryContext(egl->display, egl->context, EGL_CONTEXT_MINOR_VERSION,
	                &minor);
	tw_logl("EGL: current EGL vendor: %s", eglQueryString(egl->display,
	                                                      EGL_VENDOR));
	tw_logl("EGL: current EGL version: %s", eglQueryString(egl->display,
	                                                       EGL_VERSION));
	tw_logl("EGL: using GLES %d.%d", major, minor);
	exts_str = strdup(eglQueryString(egl->display, EGL_EXTENSIONS));
	tw_logl("EGL extension:");
	for (ext = strtok(exts_str, " "); ext != NULL;
	     ext = strtok(NULL, " "))
		tw_logl("\t%s", ext);
	free(exts_str);
}

WL_EXPORT bool
tw_egl_init(struct tw_egl *egl, const struct tw_egl_options *opts)
{
	if (!setup_egl_basic_exts(egl))
		return false;
	if (!setup_egl_display(egl, opts))
		return false;
	if (!setup_egl_client_extensions(egl))
		return false;
	if (!setup_egl_config(egl, opts))
		goto error;
	if (!eglBindAPI(EGL_OPENGL_ES_API))
		goto error;
	//TODO setup dmabuf formats
	if (!setup_egl_context(egl))
		goto error;
	init_egl_dma_formats(egl);
	print_egl_info(egl);
	return true;
error:
	egl->config = EGL_NO_CONFIG_KHR;
	eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
	               EGL_NO_CONTEXT);
	if (egl->display)
		eglTerminate(egl->display);

	eglReleaseThread();
	return false;
}

WL_EXPORT void
tw_egl_fini(struct tw_egl *egl)
{
	if (!egl)
		return;

	tw_drm_formats_fini(&egl->drm_formats);
	eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
	               EGL_NO_CONTEXT);
	if (egl->wl_display) {
		assert(_unbind_wl_display);
		_unbind_wl_display(egl->display, egl->wl_display);
	}
	if (!eglDestroyContext(egl->display, egl->context))
		tw_logl_level(TW_LOG_ERRO, "failed to destroy EGL context");
	if (!eglTerminate(egl->display))
		tw_logl_level(TW_LOG_ERRO, "failed to termiate EGL display");
	eglReleaseThread();
}

WL_EXPORT bool
tw_egl_make_current(struct tw_egl *egl, EGLSurface surface)
{
	if (!eglMakeCurrent(egl->display, surface, surface, egl->context)) {
		tw_logl_level(TW_LOG_ERRO, "eglMakeCurrent failed");
		return false;
	}
	return true;
}

WL_EXPORT bool
tw_egl_unset_current(struct tw_egl *egl)
{
	if (!eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
	                    egl->context)) {
		tw_logl_level(TW_LOG_ERRO, "eglMakeCurrent failed");
		return false;
	}
	return true;
}

WL_EXPORT int
tw_egl_buffer_age(struct tw_egl *egl, EGLSurface surface)
{
	EGLint buffer_age;

	if (!tw_egl_make_current(egl, surface))
		return -1;
	if (!egl->query_buffer_age)
		return -1;
	if (!eglQuerySurface(egl->display, surface,
	                     EGL_BUFFER_AGE_EXT, &buffer_age))
		return -1;
	return (int)buffer_age;
}

WL_EXPORT bool
tw_egl_check_egl_ext(struct tw_egl *egl, const char *ext)
{
	const char *exts_str = NULL;
	exts_str = eglQueryString(egl->display, EGL_EXTENSIONS);

	if (exts_str)
		return check_egl_ext(exts_str, ext, false);
	return false;
}

WL_EXPORT bool
tw_egl_check_gl_ext(struct tw_egl *egl, const char *ext)
{
	const char *exts_str = NULL;
	bool ret = false;

	tw_egl_make_current(egl, EGL_NO_SURFACE);
	exts_str = (const char *)glGetString(GL_EXTENSIONS);
	if (!exts_str)
		tw_logl_level(TW_LOG_ERRO, "Failed to get GL_EXTENSION");
	else
		ret = check_egl_ext(exts_str, ext, false);

	//we would still be in the context
	tw_egl_unset_current(egl);

	return ret;
}

WL_EXPORT bool
tw_egl_bind_wl_display(struct tw_egl *egl, struct wl_display *display)
{
	if (!_bind_wl_display)
		return false;
	if (_bind_wl_display(egl->display, display)) {
		egl->wl_display = display;
		return true;
	}
	return false;
}

WL_EXPORT bool
tw_egl_destroy_image(struct tw_egl *egl, EGLImageKHR image)
{
	if (!_destroy_egl_image)
		return false;
	if (image == EGL_NO_IMAGE)
		return true;
	return _destroy_egl_image(egl->display, image);
}

WL_EXPORT bool
tw_egl_query_wl_buffer(struct tw_egl *egl, struct wl_resource *buffer,
                       EGLint attribute, EGLint *value)
{
	if (!_query_wl_buffer)
		return false;
	else
		return _query_wl_buffer(egl->display, buffer, attribute,
		                        value);
}

WL_EXPORT EGLSurface
tw_egl_create_window_surface(struct tw_egl *egl, void *native_surface,
                             EGLint const * attrib_list)
{
	assert(_create_window_surface);
	return _create_window_surface(egl->display, egl->config,
	                              native_surface, attrib_list);
}

WL_EXPORT bool
tw_egl_swap_buffer(struct tw_egl *egl, EGLSurface surface,
                   pixman_region32_t *damages)
{
	EGLBoolean ret;
	//never block when swapping buffers on Wayland
	if (egl->platform == EGL_PLATFORM_WAYLAND_EXT)
		eglSwapInterval(egl->display, 0);

	if (damages && _swap_buffer_with_damage) {
		EGLint width = 0, height = 0;
		int nrects = 0;
		struct tw_mat3 flip;
		pixman_box32_t rect;
		pixman_box32_t *rects = (damages) ?
			pixman_region32_rectangles(damages, &nrects) : NULL;
		EGLint egl_damage[4 * nrects + 1];

		eglQuerySurface(egl->display, surface, EGL_WIDTH, &width);
		eglQuerySurface(egl->display, surface, EGL_HEIGHT, &height);
		tw_mat3_flip_y(&flip, height);

		for (int i = 0; i < nrects; i++) {
			tw_mat3_box_transform(&flip, &rect, &rects[i]);
			egl_damage[4 * i] = rect.x1;
			egl_damage[4 * i + 1] = rect.y1;
			egl_damage[4 * i + 2] = rect.x2 - rect.x1;
			egl_damage[4 * i + 3] = rect.y2 - rect.y1;
		}
		if (nrects == 0)
			ret = eglSwapBuffers(egl->display, surface);
		else
			ret = _swap_buffer_with_damage(egl->display, surface,
			                               egl_damage, nrects);
	} else {
		ret = eglSwapBuffers(egl->display, surface);
	}
	return ret == EGL_TRUE;
}

WL_EXPORT EGLImageKHR
tw_egl_import_wl_drm_image(struct tw_egl *egl, struct wl_resource *data,
                           EGLint *fmt, int *width, int *height,
                           bool *y_inverted)
{
	EGLint _y_inverted;
	const EGLint attribs[] = {EGL_WAYLAND_PLANE_WL, 0, EGL_NONE};

	if (!_bind_wl_display || !_create_egl_image)
		return NULL;

	if (!_query_wl_buffer(egl->display, data, EGL_TEXTURE_FORMAT, fmt))
		return NULL;

	_query_wl_buffer(egl->display, data, EGL_WIDTH, width);
	_query_wl_buffer(egl->display, data, EGL_HEIGHT, height);

	if (_query_wl_buffer(egl->display, data, EGL_WAYLAND_Y_INVERTED_WL,
	                     &_y_inverted))
		*y_inverted = _y_inverted == EGL_TRUE;
	else
		*y_inverted = false;

	return _create_egl_image(egl->display, egl->context,
	                         EGL_WAYLAND_BUFFER_WL, data, attribs);
}

static void
prepare_egl_dmabuf_attributes(EGLint eglattrs[50],
                              struct tw_dmabuf_attributes *attrs,
                              bool has_modifiers)
{
	unsigned int atti = 0;
	struct {
		EGLint fd;
		EGLint offset;
		EGLint pitch;
		EGLint mod_lo;
		EGLint mod_hi;
	} attr_names[TW_DMA_MAX_PLANES] = {
		{
			EGL_DMA_BUF_PLANE0_FD_EXT,
			EGL_DMA_BUF_PLANE0_OFFSET_EXT,
			EGL_DMA_BUF_PLANE0_PITCH_EXT,
			EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
			EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT
		}, {
			EGL_DMA_BUF_PLANE1_FD_EXT,
			EGL_DMA_BUF_PLANE1_OFFSET_EXT,
			EGL_DMA_BUF_PLANE1_PITCH_EXT,
			EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
			EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT
		}, {
			EGL_DMA_BUF_PLANE2_FD_EXT,
			EGL_DMA_BUF_PLANE2_OFFSET_EXT,
			EGL_DMA_BUF_PLANE2_PITCH_EXT,
			EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
			EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT
		}, {
			EGL_DMA_BUF_PLANE3_FD_EXT,
			EGL_DMA_BUF_PLANE3_OFFSET_EXT,
			EGL_DMA_BUF_PLANE3_PITCH_EXT,
			EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT,
			EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT
		}
	};

	eglattrs[atti++] = EGL_WIDTH;
	eglattrs[atti++] = attrs->width;
	eglattrs[atti++] = EGL_HEIGHT;
	eglattrs[atti++] = attrs->height;
	eglattrs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
	eglattrs[atti++] = attrs->format;

	for (int i = 0; i < attrs->n_planes; i++) {
		eglattrs[atti++] = attr_names[i].fd;
		eglattrs[atti++] = attrs->fds[i];
		eglattrs[atti++] = attr_names[i].offset;
		eglattrs[atti++] = attrs->offsets[i];
		eglattrs[atti++] = attr_names[i].pitch;
		eglattrs[atti++] = attrs->strides[i];
		if (has_modifiers) {
			eglattrs[atti++] = attr_names[i].mod_lo;
			eglattrs[atti++] = attrs->modifier & 0xFFFFFFFF;
			eglattrs[atti++] = attr_names[i].mod_hi;
			eglattrs[atti++] = attrs->modifier >> 32;
		}
	}
	eglattrs[atti++] = EGL_NONE;
	assert(atti < 50);
}


WL_EXPORT EGLImageKHR
tw_egl_import_dmabuf_image(struct tw_egl *egl,
                           struct tw_dmabuf_attributes *attrs, bool *external)
{
	bool has_modifier = false;
	EGLint egl_attrs[50];
	EGLImageKHR image = EGL_NO_IMAGE;

	if (!egl->image_base_khr || !egl->import_dmabuf) {
		tw_logl_level(TW_LOG_WARN, "no dmabuf import extension");
		return EGL_NO_IMAGE;
	}

	if (attrs->modifier != DRM_FORMAT_MOD_INVALID &&
	    attrs->modifier != DRM_FORMAT_MOD_LINEAR) {
		if (!egl->import_dmabuf_modifiers) {
			tw_logl_level(TW_LOG_WARN,
			              "no dmabuf import modifiers externsion");
			return EGL_NO_IMAGE;
		}
		has_modifier = true;
	}


        prepare_egl_dmabuf_attributes(egl_attrs, attrs, has_modifier);

        image =  _create_egl_image(egl->display, EGL_NO_CONTEXT,
                                   EGL_LINUX_DMA_BUF_EXT, NULL, egl_attrs);
        if (external)
	        *external = tw_drm_formats_is_modifier_external(
		        &egl->drm_formats, attrs->format, attrs->modifier);
        return image;
}


WL_EXPORT bool
tw_egl_image_export_dmabuf(struct tw_egl *egl, EGLImage image,
                           int width, int height, uint32_t flags,
                           struct tw_dmabuf_attributes *attrs)
{
	memset(attrs, 0, sizeof(*attrs));

	if (!_export_dmabuf_image ||
	    !_export_dmabuf_image_query)
		return false;
	if (!_export_dmabuf_image_query(egl->display, image,
	                                (int *)&attrs->format,
	                                &attrs->n_planes, &attrs->modifier))
		return false;
	if (attrs->n_planes > TW_DMA_MAX_PLANES) {
		tw_logl_level(TW_LOG_WARN, "exceed max DMA-buf planes");
		return false;
	}
	if (!_export_dmabuf_image(egl->display, image, attrs->fds,
	                          (EGLint *)attrs->strides,
	                          (EGLint *)attrs->offsets))
		return false;
	attrs->width = width;
	attrs->height = height;
	attrs->flags = flags;
	return true;

}

static void
egl_dma_format_request(struct tw_linux_dmabuf *dmabuf,
                       void *callback, int *formats,
                       size_t *nformats)
{
	struct tw_egl *egl = callback;

	*nformats = tw_drm_formats_count(&egl->drm_formats);
	if (formats) {
		struct tw_drm_format *format;
		int i = 0;
		wl_array_for_each(format, &egl->drm_formats.formats) {
			formats[i++] = format->fmt;
		}
	}
}

static void
egl_dma_modifiers_request(struct tw_linux_dmabuf *dmabuf,
                          void *callback, int fmt,
                          uint64_t *modifiers,
                          size_t *n_modifiers)
{
	struct tw_egl *egl = callback;
	struct tw_drm_format *format, *target = NULL;

	wl_array_for_each(format, &egl->drm_formats.formats) {
		if ((int)format->fmt == fmt) {
			target = format;
			break;
		}
	}
	if (!target)
		goto no_modifiers;
	*n_modifiers = format->len;

	if (modifiers && *n_modifiers) {
		struct tw_drm_modifier *modifier =
			egl->drm_formats.modifiers.data;
		modifier += format->cursor;

		for (int i = 0; i < format->len; i++)
			modifiers[i] = (modifier + i)->modifier;
	}
	return;
no_modifiers:
	*n_modifiers = 0;
	return;
}

static bool
egl_dma_test_import_buffer(struct tw_dmabuf_attributes *attrs, void *callback)
{
	struct tw_egl *egl = callback;
	bool external;
	EGLImage image = EGL_NO_IMAGE;

	if (!egl->image_base_khr || !egl->import_dmabuf || !_destroy_egl_image)
		return false;

	image = tw_egl_import_dmabuf_image(egl, attrs, &external);

	if (image != EGL_NO_IMAGE) {
		_destroy_egl_image(egl->display, image);
		return true;
	} else {
		return false;
	}
}

static const struct tw_linux_dmabuf_impl dmabuf_impl = {
	.format_request = egl_dma_format_request,
	.modifiers_request = egl_dma_modifiers_request,
	.test_import = egl_dma_test_import_buffer,
};

WL_EXPORT void
tw_egl_impl_linux_dmabuf(struct tw_egl *egl, struct tw_linux_dmabuf *dma)
{
	dma->impl = &dmabuf_impl;
	dma->impl_userdata = egl;
}


WL_EXPORT EGLDeviceEXT
tw_egl_device_from_path(const char *path)
{
	int ndevs;
	const char *extensions, *pt;
	PFNEGLQUERYDEVICESEXTPROC query_devices;
	PFNEGLQUERYDEVICESTRINGEXTPROC query_device_string;
	EGLDeviceEXT devices[16];

	//get EGL device
	extensions = (const char *)eglQueryString(EGL_NO_DISPLAY,
	                                          EGL_EXTENSIONS);
	if (!extensions) {
		tw_logl_level(TW_LOG_WARN, "Failed to query egl externsion");
		return EGL_NO_DEVICE_EXT;
	}

	if (!check_egl_ext(extensions, "EGL_EXT_device_base", false) &&
	    (!check_egl_ext(extensions, "EGL_EXT_device_query", false) ||
	     !check_egl_ext(extensions, "EGL_EXT_device_enumeration", false))){
		tw_logl_level(TW_LOG_WARN, "no EGL_EXT_device_base");
		return EGL_NO_DEVICE_EXT;
	}

	if (!check_egl_ext(extensions, "EGL_EXT_device_base", false) &&
	    (!check_egl_ext(extensions, "EGL_EXT_device_query", false) ||
	     !check_egl_ext(extensions, "EGL_EXT_device_enumeration", false))){
		tw_logl_level(TW_LOG_WARN, "no EGL_EXT_device_base");
		return EGL_NO_DEVICE_EXT;
	}

	if (!get_egl_proc(&query_devices, "eglQueryDevicesEXT") ||
	    !get_egl_proc(&query_device_string, "eglQueryDeviceStringEXT"))
		return EGL_NO_DEVICE_EXT;

	if (query_devices(16, devices, &ndevs) != EGL_TRUE) {
		tw_logl_level(TW_LOG_WARN, "Failed to query EGL Devices\n");
		return EGL_NO_DEVICE_EXT;
	}
	for (int i = 0; i < ndevs; i++) {
		extensions = query_device_string(devices[i], EGL_EXTENSIONS);
		pt = query_device_string(devices[i], EGL_DRM_DEVICE_FILE_EXT);

		if (!extensions || !pt ||
		    !check_egl_ext(extensions, "EGL_EXT_device_drm", false))
			continue;
		if (strcmp(pt, path) != 0)
			continue;
		return devices[i];
	}
	return EGL_NO_DEVICE_EXT;
}
