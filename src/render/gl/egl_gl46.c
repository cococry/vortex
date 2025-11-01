#define EGL_NO_X11
#define MESA_EGL_NO_X11_HEADERS
#define EGL_PLATFORM_GBM_KHR 1
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/eglplatform.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-util.h>

#include "src/core/compositor.h"
#include "src/core/surface.h"
#include "src/core/core_types.h"
#include "src/core/util.h"
#include "src/protocols/linux_dmabuf.h"
#include "src/render/dmabuf.h"
#include "src/render/renderer.h"


#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad.h>
#include <wayland-server.h>
#include <wayland-egl.h>

#include <runara/runara.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "egl_gl46.h"


// Minimal GBM interop
#define __vt_gbm_fourcc_code(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
  ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))


#define _VT_GBM_FORMAT_XRGB8888	__vt_gbm_fourcc_code('X', 'R', '2', '4') /* [31:0] x:R:G:B 8:8:8:8 little endian */


static PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC eglSwapBuffersWithDamageEXT_ptr = NULL;
static PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC eglSwapBuffersWithDamageKHR_ptr = NULL;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_ptr = NULL;
static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_ptr = NULL;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_ptr = NULL;

struct egl_backend_state_t {
  EGLDisplay egl_dsp;
  EGLContext egl_ctx;
  EGLConfig egl_conf;
  EGLint egl_native_vis;
  bool has_dmabuf_modifiers, has_dmabuf_support;

  RnState* render;

  struct wl_array formats;
}; 

struct egl_output_state_t {
  GLint fbo_id, fbo_tex_id; 
};

static const char*  _egl_err_str(EGLint error);
static bool         _egl_gl_import_buffer_shm(struct vt_renderer_t* r, struct vt_surface_t *surf, struct wl_shm_buffer *shm_buf);
static bool         _egl_pick_config_from_format(struct vt_compositor_t* c, struct egl_backend_state_t* egl, uint32_t format);
static bool         _egl_pick_config(struct vt_compositor_t *comp, struct egl_backend_state_t *egl, struct vt_backend_t *backend);
static bool         _egl_gl_create_output_fbo(struct vt_output_t *output); 
static bool         _egl_create_renderer(
  struct vt_renderer_t* renderer, enum vt_backend_platform_t platform, void* native_handle, 
  bool log_error); 

const char*
_egl_err_str(EGLint error) {
  switch (error) {
    case EGL_SUCCESS:                return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:         return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:              return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:               return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:           return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG:              return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT:             return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE:     return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:             return "EGL_BAD_DISPLAY";
    case EGL_BAD_SURFACE:             return "EGL_BAD_SURFACE";
    case EGL_BAD_MATCH:               return "EGL_BAD_MATCH";
    case EGL_BAD_PARAMETER:           return "EGL_BAD_PARAMETER";
    case EGL_BAD_NATIVE_PIXMAP:       return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:       return "EGL_BAD_NATIVE_WINDOW";
    case EGL_CONTEXT_LOST:            return "EGL_CONTEXT_LOST";
    default:                          return "Unknown EGL error";
  }
}

bool
_egl_gl_import_buffer_shm(struct vt_renderer_t* r, struct vt_surface_t *surf,
                          struct wl_shm_buffer *shm_buf) {
  int width = wl_shm_buffer_get_width(shm_buf);
  int height = wl_shm_buffer_get_height(shm_buf);
  int stride = wl_shm_buffer_get_stride(shm_buf);
  uint32_t fmt = wl_shm_buffer_get_format(shm_buf);

  GLenum format = GL_BGRA;
  GLenum type   = GL_UNSIGNED_INT_8_8_8_8_REV;
  GLenum internal_format = GL_RGBA8;

  switch (fmt) {
    case WL_SHM_FORMAT_ARGB8888:
    case WL_SHM_FORMAT_XRGB8888:
      break;
    default:
      VT_WARN(r->comp->log, "Unsupported wl_shm format %u", fmt);
      return false;
  }

  wl_shm_buffer_begin_access(shm_buf);
  void *data = wl_shm_buffer_get_data(shm_buf);

  if (surf->tex.width != width || surf->tex.height != height || !surf->tex.id) {
    if (!surf->tex.id)
      glGenTextures(1, &surf->tex.id);

    glBindTexture(GL_TEXTURE_2D, surf->tex.id);

    if (fmt == WL_SHM_FORMAT_XRGB8888)
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);

    glTexImage2D(GL_TEXTURE_2D, 0, internal_format,
                 width, height, 0, format, type, data);

    surf->tex.width = width;
    surf->tex.height = height;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  glBindTexture(GL_TEXTURE_2D, surf->tex.id);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);

  pixman_box32_t ext = *pixman_region32_extents(&surf->current_damage);
  glTexSubImage2D(GL_TEXTURE_2D, 0, ext.x1, ext.y1, ext.x2 - ext.x1, ext.y2 - ext.y1, format, type, data + ext.y1 * stride + ext.x1 * 4);

  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

  wl_shm_buffer_end_access(shm_buf);


  glBindTexture(GL_TEXTURE_2D, 0);

  return true;
}


#define _VT_DRM_FORMAT_MOD_INVALID 0x00FFFFFFFFFFFFFF
#define _VT_DRM_FORMAT_MOD_LINEAR 0x0000000000000000

bool
_egl_gl_import_buffer_dmabuf(struct vt_renderer_t* r,
                             struct vt_linux_dmabuf_v1_buffer_t* dmabuf,
                             struct vt_surface_t* surf)
{
  struct vt_dmabuf_attr_t* a = &dmabuf->attr;
  
  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);

  if (!a->num_planes || a->width <= 0 || a->height <= 0) {
    VT_ERROR(r->comp->log, "Invalid dmabuf attributes for import.");
    return false;
  }

	if (a->mod != _VT_DRM_FORMAT_MOD_INVALID &&
			a->mod != _VT_DRM_FORMAT_MOD_LINEAR &&
			!egl->has_dmabuf_modifiers) {
    VT_ERROR(r->comp->log, "EGL: No support for DMABUF modifiers, skipping importing.");
    return false;
	}

  // https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/master/render/egl.c#L750
	unsigned int atti = 0;
	EGLint attribs[50];
	attribs[atti++] = EGL_WIDTH;
	attribs[atti++] = a->width;
	attribs[atti++] = EGL_HEIGHT;
	attribs[atti++] = a->height;
	attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
	attribs[atti++] = a->format;

	struct {
		EGLint fd;
		EGLint offset;
		EGLint pitch;
		EGLint mod_lo;
		EGLint mod_hi;
	} attr_names[VT_DMABUF_PLANES_CAP] = {
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

	for (int i = 0; i < a->num_planes; i++) {
		attribs[atti++] = attr_names[i].fd;
		attribs[atti++] = a->fds[i];
		attribs[atti++] = attr_names[i].offset;
		attribs[atti++] = a->offsets[i];
		attribs[atti++] = attr_names[i].pitch;
		attribs[atti++] = a->strides[i];
		if (egl->has_dmabuf_modifiers &&
				a->mod != _VT_DRM_FORMAT_MOD_INVALID) {
			attribs[atti++] = attr_names[i].mod_lo;
			attribs[atti++] = a->mod & 0xFFFFFFFF;
			attribs[atti++] = attr_names[i].mod_hi;
			attribs[atti++] = a->mod >> 32;
		}
	}
	attribs[atti++] = EGL_IMAGE_PRESERVED_KHR;
	attribs[atti++] = EGL_TRUE;

	attribs[atti++] = EGL_NONE;
	assert(atti <= sizeof(attribs)/sizeof(attribs[0]));

  EGLImageKHR image = eglCreateImageKHR_ptr(
      egl->egl_dsp,
      EGL_NO_CONTEXT,
      EGL_LINUX_DMA_BUF_EXT,
      NULL,
      attribs);

  if (image == EGL_NO_IMAGE_KHR) {
    EGLint err = eglGetError();
    VT_ERROR(r->comp->log,
             "Failed to import dmabuf into EGLImage: error=0x%x "
             "(format=0x%x, mod=0x%016" PRIx64 ")",
             err, a->format, a->mod);
    return false;
  }

  bool is_external_only = false;
  struct vt_dmabuf_drm_format_t* fmt = NULL;
  struct vt_dmabuf_drm_format_t* f;
  wl_array_for_each(f, &egl->formats) {
    if (f->format != a->format) continue;
    for (size_t i = 0; i < f->len; i++) {
      if (f->mods[i].mod == a->mod) {
        is_external_only = f->mods[i]._egl_ext_only;
        fmt = f;
        break;
      }
    }
    if (fmt) break;
  }

  GLenum target = is_external_only ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D;

  if (!surf->tex.id)
    glGenTextures(1, &surf->tex.id);

  glBindTexture(target, surf->tex.id);
  glEGLImageTargetTexture2DOES_ptr(target, image);

  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  surf->tex.width = a->width;
  surf->tex.height = a->height;
  surf->render_tex_handle = image; 


  VT_TRACE(r->comp->log,
           "Imported dmabuf %ux%u fmt=0x%x mod=0x%016" PRIx64 " (%s)",
           a->width, a->height, a->format, a->mod,
           is_external_only ? "external" : "2D");

  glBindTexture(target, 0);
  return true;
}


bool _egl_pick_config_from_format(struct vt_compositor_t* c,
                                  struct egl_backend_state_t* egl,
                                  uint32_t format) {
  EGLint num = 0;
  if (!eglGetConfigs(egl->egl_dsp, NULL, 0, &num) || num <= 0) {
    VT_ERROR(c->log, "EGL: eglGetConfigs() failed or returned no configs");
    return false;
  }

  EGLConfig *configs = VT_ALLOC(c, sizeof(EGLConfig) * num);
  eglGetConfigs(egl->egl_dsp, configs, num, &num);

  EGLConfig match = NULL;
  EGLint vis;
  for (int i = 0; i < num; i++) {
    eglGetConfigAttrib(egl->egl_dsp, configs[i], EGL_NATIVE_VISUAL_ID, &vis);
    if ((uint32_t)vis == format) {
      match = configs[i];
      break;
    }
  }

  if (!match) {
    VT_ERROR(
      c->log,
      "EGL: could not find config for GBM format 0x%x, falling back to first config", format);
    match = configs[0];
  }

  egl->egl_conf = match;
  eglGetConfigAttrib(egl->egl_dsp, match, EGL_NATIVE_VISUAL_ID, &vis);
  if(match) {
    VT_TRACE(c->log, "EGL: picked config with visual 0x%x", vis);
  }
  egl->egl_native_vis = vis;

  return true;
}

bool 
_egl_pick_config(struct vt_compositor_t *comp, struct egl_backend_state_t *egl, struct vt_backend_t *backend) {
  EGLint attribs[32];
  int i = 0;

  switch (backend->platform) {
    case VT_BACKEND_DRM_GBM: {
      // DRM/GBM backend: use DRM fourcc to pick config
      uint32_t fmt = _VT_GBM_FORMAT_XRGB8888;
      return _egl_pick_config_from_format(comp, egl, fmt);
    }

    case VT_BACKEND_WAYLAND: {
      // Wayland backend: use standard window attributes
      EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_ALPHA_SIZE, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
      };
      EGLint n = 0;
      if (!eglChooseConfig(egl->egl_dsp, attrs, &egl->egl_conf, 1, &n) || n == 0) {
        VT_ERROR(comp->log, "EGL: no valid configs for Wayland backend");
        return false;
      }
      eglGetConfigAttrib(egl->egl_dsp, egl->egl_conf, EGL_NATIVE_VISUAL_ID, &egl->egl_native_vis);
      return true;
    }

    case VT_BACKEND_SURFACELESS: {
      EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
      };
      EGLint n = 0;
      if (!eglChooseConfig(egl->egl_dsp, attrs, &egl->egl_conf, 1, &n) || n == 0)
        return false;
      eglGetConfigAttrib(egl->egl_dsp, egl->egl_conf, EGL_NATIVE_VISUAL_ID, &egl->egl_native_vis);
      return true;
    }

    default:
      VT_ERROR(comp->log, "EGL: unsupported backend platform");
      return false;
  }
}

bool
_egl_gl_create_output_fbo(struct vt_output_t *output) {
  if(!output || !output->user_data_render) return false;

  struct egl_output_state_t* egl_output =  (struct egl_output_state_t*)output->user_data_render; 

  if (egl_output->fbo_tex_id) glDeleteTextures(1, &egl_output->fbo_tex_id);
  if (egl_output->fbo_id) glDeleteFramebuffers(1, &egl_output->fbo_id);

  glGenFramebuffers(1, &egl_output->fbo_id);
  glBindFramebuffer(GL_FRAMEBUFFER, egl_output->fbo_id);

  glGenTextures(1, &egl_output->fbo_tex_id);
  glBindTexture(GL_TEXTURE_2D, egl_output->fbo_tex_id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, output->width, output->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  // For crisp image during resize
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, egl_output->fbo_tex_id, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    VT_ERROR(output->backend->comp->log, "EGL: FBO creation for output %p (%u%u) failed.\n", output, output->width, output->height);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return false;
  }

  // clean up
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  return true;
}

EGLDisplay _egl_create_display(
  struct vt_compositor_t* comp,
  enum vt_backend_platform_t platform,
  void *native_handle,
  bool log_error
) {
  PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
    (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (!eglGetPlatformDisplayEXT) {
    if(log_error) VT_ERROR(comp->log, "EGL_EXT_platform_base not supported.");
    return NULL;
  }

  int32_t egl_platform = -1;
  switch(platform) {
    case VT_BACKEND_DRM_GBM: egl_platform = EGL_PLATFORM_GBM_KHR; break;
    case VT_BACKEND_WAYLAND: egl_platform = EGL_PLATFORM_WAYLAND_KHR; break;
    case VT_BACKEND_SURFACELESS: egl_platform = EGL_PLATFORM_SURFACELESS_MESA; break;
    default: {
      log_fatal(comp->log, "Using invalid compositor backend.");
      return NULL;
    }
  }
  EGLDisplay dsp = eglGetPlatformDisplayEXT(egl_platform, native_handle, NULL);
  if (dsp == EGL_NO_DISPLAY) {
    if(log_error) {
      EGLint err = eglGetError();
      VT_ERROR(comp->log, "eglGetPlatformDisplayEXT failed: 0x%04x (%s)", err, _egl_err_str(err));
    }
    return NULL;
  }

  if (!eglInitialize(dsp, NULL, NULL)) {
    EGLint err = eglGetError();
    if(log_error) VT_ERROR(comp->log, "eglInitialize failed: 0x%04x (%s)", err, _egl_err_str(err));
    return NULL;
  }

  return dsp;
}

bool 
_egl_create_renderer(
  struct vt_renderer_t* renderer, enum vt_backend_platform_t platform,
  void* native_handle, bool log_error) {
  if(!native_handle) return false;

  renderer->user_data = VT_ALLOC(renderer->comp, sizeof(struct egl_backend_state_t));
  struct egl_backend_state_t* egl = BACKEND_DATA(renderer, struct egl_backend_state_t);

  if(!(egl->egl_dsp = _egl_create_display(renderer->comp, platform, native_handle, log_error))) {
    VT_ERROR(renderer->comp->log, "EGL: Failed to create EGL display.");
    return false;
  }
  return true;
}


// ===================================================
// =================== PUBLIC API ====================
// ===================================================

bool
renderer_init_egl(struct vt_backend_t* backend, struct vt_renderer_t *r, void* native_handle) {
  if (!r || !native_handle) return false;

  r->backend = backend;
  r->rendering_backend = VT_RENDERING_BACKEND_EGL_OPENGL;

  if(!r->user_data) {
    if(!_egl_create_renderer(r, backend->platform, native_handle, true)) return false;
  }
    
  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);

  eglBindAPI(EGL_OPENGL_API);

  if(!_egl_pick_config(backend->comp, egl, backend)) return false;

  r->_desired_render_buffer_format = egl->egl_native_vis;

  EGLint ctx_attr[] = {
    EGL_CONTEXT_MAJOR_VERSION, 4,
    EGL_CONTEXT_MINOR_VERSION, 5,
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
    EGL_NONE
  };

  egl->egl_ctx = eglCreateContext(egl->egl_dsp, egl->egl_conf, EGL_NO_CONTEXT, ctx_attr);
  if (egl->egl_ctx == EGL_NO_CONTEXT) {
    EGLint err = eglGetError();
    VT_ERROR(r->comp->log, "eglCreateContext failed: 0x%04x (%s)", err, _egl_err_str(err));
    return false;
  }


  const char* vendor  = eglQueryString(egl->egl_dsp, EGL_VENDOR);
  const char* version = eglQueryString(egl->egl_dsp, EGL_VERSION);

  const char* exts = eglQueryString(egl->egl_dsp, EGL_EXTENSIONS);
  if(strstr(exts, "EGL_EXT_swap_buffers_with_damage")) {
    eglSwapBuffersWithDamageEXT_ptr =
      (PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC)
      eglGetProcAddress("eglSwapBuffersWithDamageEXT");
  }
  if(strstr(exts, "EGL_KHR_swap_buffers_with_damage")) {
    eglSwapBuffersWithDamageKHR_ptr =
      (PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC)
      eglGetProcAddress("eglSwapBuffersWithDamageKHR");
  }

  egl->has_dmabuf_support = true;
  egl->has_dmabuf_modifiers = true;

  glEGLImageTargetTexture2DOES_ptr =
    (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
    eglGetProcAddress("glEGLImageTargetTexture2DOES");

  if (!glEGLImageTargetTexture2DOES_ptr) {
    VT_ERROR(backend->comp->log,
             "EGL: Failed to load glEGLImageTargetTexture2DOES (GL_OES_EGL_image), DMABUF imports will not be supported.");
    egl->has_dmabuf_support = false;
  }

  if (strstr(exts, "EGL_KHR_image_base")) {
    eglCreateImageKHR_ptr =
      (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR_ptr =
      (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
  } else {
    VT_ERROR(backend->comp->log, "EGL: EGL_KHR_image_base extension not supported, DMABUF imports will not be supported.");
    egl->has_dmabuf_support = false;
  }
  VT_TRACE(r->comp->log, "EGL extensions: %s", exts);
  VT_TRACE(r->comp->log, "EGL: initialized (vendor=%s, version=%s)", vendor, version);

  return true;
}

bool 
renderer_is_handle_renderable_egl(struct vt_renderer_t* renderer, void* native_handle) {
  if(!renderer || !renderer->comp || !renderer->comp->backend || !native_handle) return false; 
  return _egl_create_renderer(renderer, renderer->comp->backend->platform, native_handle, false);
}

bool 
renderer_query_dmabuf_formats_egl(struct vt_compositor_t* comp, void* native_handle, struct wl_array* formats) {
  if(!comp || !native_handle || !formats) return false; 
    PFNEGLQUERYDMABUFFORMATSEXTPROC eglQueryDmaBufFormatsEXT =
        (void*) eglGetProcAddress("eglQueryDmaBufFormatsEXT");
    PFNEGLQUERYDMABUFMODIFIERSEXTPROC eglQueryDmaBufModifiersEXT =
        (void*) eglGetProcAddress("eglQueryDmaBufModifiersEXT");

  if (!eglQueryDmaBufFormatsEXT || !eglQueryDmaBufModifiersEXT) {
    return false;
  }

  EGLDisplay egl_dsp;
  if(!(egl_dsp = _egl_create_display(comp, comp->backend->platform, native_handle, false))) {
    VT_ERROR(comp->log, "EGL: Failed to create EGL display.");
    return false;
  }

  wl_array_init(formats);

  // Query the available DMABUF formats
  EGLint n_formats = 0;
  /*
    /* we set max_formats to 0 to count the formats without retrieving them: 
   *      If <max_formats> is 0, no formats are returned, but the total number
          of formats is returned in <num_formats>, and no error is generated.
    */
  eglQueryDmaBufFormatsEXT(egl_dsp, 0 /*max_formats*/, NULL, &n_formats);
  if (n_formats <= 0) {
    VT_ERROR(comp->log, "EGL: No DMABUF formats available, falling back to SHM.\n");
    eglTerminate(egl_dsp);
    return false;
  }

  EGLint* dmabuf_formats = calloc(n_formats, sizeof(EGLint));
  eglQueryDmaBufFormatsEXT(egl_dsp, n_formats, dmabuf_formats, &n_formats);

  // query the available modifiers of each format
  for (uint32_t i = 0; i < n_formats; i++) {
    EGLint n_mods = 0;
    /* we set max_modifiers to 0 to count the modifiers without retrieving them: 
     *    If <max_modifiers> is 0, no modifiers are returned, but the total
          number of modifiers is returned in <num_modifiers>, and no error is
          generated. */
    eglQueryDmaBufModifiersEXT(egl_dsp, dmabuf_formats[i], 0 /*max_modifiers*/, NULL, NULL, &n_mods);
    if (n_mods <= 0) continue;

    EGLuint64KHR* format_mods = calloc(n_mods, sizeof(EGLuint64KHR));
    // We need to store ext_only per modifier to know if 
    // the requested format-modifier combination is only 
    // supported for use with the GL_TEXTURE_EXTERNAL_OES flag when importing 
    // the DMABUF into a GL texture later.
    EGLBoolean* ext_only = calloc(n_mods, sizeof(EGLBoolean));
    eglQueryDmaBufModifiersEXT(egl_dsp, dmabuf_formats[i],
                               n_mods, format_mods, ext_only, &n_mods);

    // add the format to the array of available formats
    struct vt_dmabuf_drm_format_t* fmt = wl_array_add(formats, sizeof(*fmt));
    fmt->format = dmabuf_formats[i];
    fmt->len = n_mods;
    fmt->mods = malloc(sizeof(struct vt_dmabuf_format_modifier_t) * n_mods);

    // populate the modifiers 
    for (uint32_t j = 0; j < n_mods; j++) {
      fmt->mods[j].mod = format_mods[j];
      fmt->mods[j]._egl_ext_only = ext_only[j] == EGL_TRUE;
    }

    free(format_mods);
    free(ext_only);
  }

  free(dmabuf_formats);

  eglTerminate(egl_dsp);
  return true;

}

bool 
renderer_query_dmabuf_formats_with_renderer_egl(struct vt_renderer_t* renderer, struct wl_array* formats) {
  if(!renderer || !renderer->user_data || !formats) return false; 
  
  struct egl_backend_state_t* egl = BACKEND_DATA(renderer, struct egl_backend_state_t);
  wl_array_init(&egl->formats);

    PFNEGLQUERYDMABUFFORMATSEXTPROC eglQueryDmaBufFormatsEXT =
        (void*) eglGetProcAddress("eglQueryDmaBufFormatsEXT");
    PFNEGLQUERYDMABUFMODIFIERSEXTPROC eglQueryDmaBufModifiersEXT =
        (void*) eglGetProcAddress("eglQueryDmaBufModifiersEXT");

  if (!eglQueryDmaBufFormatsEXT || !eglQueryDmaBufModifiersEXT) {
    VT_ERROR(renderer->comp->log, "EGL: DMABUF extensions not supported, falling back to SHM.\n");
    egl->has_dmabuf_modifiers = true;
    return false;
  }
  

  wl_array_init(formats);

  // Query the available DMABUF formats
  EGLint n_formats = 0;
  /*
    /* we set max_formats to 0 to count the formats without retrieving them: 
   *      If <max_formats> is 0, no formats are returned, but the total number
          of formats is returned in <num_formats>, and no error is generated.
    */
  eglQueryDmaBufFormatsEXT(egl->egl_dsp, 0 /*max_formats*/, NULL, &n_formats);
  if (n_formats <= 0) {
    VT_ERROR(renderer->comp->log, "EGL: No DMABUF formats available, falling back to SHM.\n");
    return false;
  }

  EGLint* dmabuf_formats = calloc(n_formats, sizeof(EGLint));
  eglQueryDmaBufFormatsEXT(egl->egl_dsp, n_formats, dmabuf_formats, &n_formats);

  // query the available modifiers of each format
  for (uint32_t i = 0; i < n_formats; i++) {
    EGLint n_mods = 0;
    /* we set max_modifiers to 0 to count the modifiers without retrieving them: 
     *    If <max_modifiers> is 0, no modifiers are returned, but the total
          number of modifiers is returned in <num_modifiers>, and no error is
          generated. */
    eglQueryDmaBufModifiersEXT(egl->egl_dsp, dmabuf_formats[i], 0 /*max_modifiers*/, NULL, NULL, &n_mods);
    if (n_mods <= 0) continue;

    EGLuint64KHR* format_mods = calloc(n_mods, sizeof(EGLuint64KHR));
    // We need to store ext_only per modifier to know if 
    // the requested format-modifier combination is only 
    // supported for use with the GL_TEXTURE_EXTERNAL_OES flag when importing 
    // the DMABUF into a GL texture later.
    EGLBoolean* ext_only = calloc(n_mods, sizeof(EGLBoolean));
    eglQueryDmaBufModifiersEXT(egl->egl_dsp, dmabuf_formats[i],
                               n_mods, format_mods, ext_only, &n_mods);

    // add the format to the array of available formats
    struct vt_dmabuf_drm_format_t* fmt = wl_array_add(formats, sizeof(*fmt));
    fmt->format = dmabuf_formats[i];
    fmt->len = n_mods;
    fmt->mods = malloc(sizeof(struct vt_dmabuf_format_modifier_t) * n_mods);

    // populate the modifiers 
    for (uint32_t j = 0; j < n_mods; j++) {
      fmt->mods[j].mod = format_mods[j];
      fmt->mods[j]._egl_ext_only = ext_only[j] == EGL_TRUE;
    }

    free(format_mods);
    free(ext_only);
  }


  struct vt_dmabuf_drm_format_t* fmt;
  wl_array_for_each(fmt, formats) {
    if(!fmt) continue;
    struct vt_dmabuf_drm_format_t* fmt_add = wl_array_add(&egl->formats, sizeof(*fmt_add));
    if(!fmt_add) {
      return false;
    }
    // deep copy
    fmt_add->format = fmt->format;
    fmt_add->len = fmt->len;

    if (fmt->len > 0 && fmt->mods) {
      fmt_add->mods = calloc(fmt->len,
                             sizeof(*fmt_add->mods));
      if (!fmt_add->mods) {
        // Rollback allocation
        wl_array_release(&egl->formats);
        if (n_formats)
          return false;
      }
      memcpy(fmt_add->mods, fmt->mods,
             fmt->len * sizeof(*fmt->mods));
    } else {
      fmt_add->mods = NULL;
    }
  }

  free(dmabuf_formats);
  return true;
}


bool
renderer_setup_renderable_output_egl(struct vt_renderer_t *r, struct vt_output_t* output) {
  if (!r || !output || !r->user_data || !output->native_window) return false;
  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);

  output->user_data_render = VT_ALLOC(r->comp, sizeof(*output->user_data_render));

  EGLint fbo_id, fbo_tex_id; 

 
  // If we're running the wayland sink backend, we create the egl_window 
  // handle and use it as the native window handle to create the EGL 
  // surface as the wayland sink backend does not assign output->native_window.
  if(r->backend->platform == VT_BACKEND_WAYLAND) {
    struct wl_egl_window* egl_win = wl_egl_window_create(output->native_window, output->width, output->height);
    if(!egl_win) {
      VT_ERROR(r->backend->comp->log, "EGL: Wayland Backend: Failed to create EGL window.");
    }
    output->native_window = egl_win; 
  }

  // Creating the EGL surface for the output
  EGLSurface egl_surf =
    eglCreateWindowSurface(egl->egl_dsp, egl->egl_conf,
                           (EGLNativeWindowType)output->native_window, NULL);
  if (egl_surf == EGL_NO_SURFACE) {
    EGLint err = eglGetError();
    VT_ERROR(r->comp->log, "EGL: eglCreateWindowSurface failed: 0x%04x (%s)", err, _egl_err_str(err));
    return false;
  }

  // Set the EGL context to correctly initialize resources for the batch renderer (runara)
  if (!eglMakeCurrent(egl->egl_dsp, egl_surf, egl_surf, egl->egl_ctx)) {
    EGLint err = eglGetError();
    VT_ERROR(r->comp->log, "eglMakeCurrent failed: 0x%04x (%s)", err, _egl_err_str(err));
    eglDestroySurface(egl->egl_dsp, egl_surf);
    return false;
  }

  if (!egl->render) {
    egl->render = rn_init(0, 0, (RnGLLoader)eglGetProcAddress);
    if (!egl->render) {
      VT_ERROR(r->comp->log, "EGL: Failed to initialize runara rendering backend.");
      return false;
    } else {
      VT_TRACE(r->comp->log, "EGL: Initialized runara rendering backend.");
    }
  }


  output->render_surface = (void*)egl_surf;

  pixman_region32_union_rect(
    &output->damage, &output->damage,
    0, 0, output->width, output->height);
  
  // Create EGL FBO for output
  _egl_gl_create_output_fbo(output);
  glEnable(GL_STENCIL_TEST);

  vt_comp_schedule_repaint(r->comp, output);

  VT_TRACE(r->comp->log, "EGL: Created surface %p (%ux%u)", egl_surf, output->width, output->height);
  return true;
}

bool 
renderer_resize_renderable_output_egl(struct vt_renderer_t* r, struct vt_output_t* output, int32_t w, int32_t h) {
  if(r->backend->platform != VT_BACKEND_WAYLAND) return true;
  if(!r || !output || !output->native_window || w == 0 || h == 0) return false;

  struct wl_egl_window* egl_win = (struct wl_egl_window*)output->native_window; 
  if(!egl_win) return false;

  wl_egl_window_resize(egl_win, w, h, 0, 0);
  _egl_gl_create_output_fbo(output);

  pixman_region32_union_rect(
    &output->damage, &output->damage,
    0, 0, w, h);

  return true;
}


bool 
renderer_destroy_renderable_output_egl(struct vt_renderer_t *r, struct vt_output_t* output) {
  if (!r || !r->user_data || !output->render_surface ||
    r->backend->platform == VT_BACKEND_SURFACELESS) return false;

  if(r->rendering_backend != VT_RENDERING_BACKEND_EGL_OPENGL) return false;

  struct egl_backend_state_t *egl = BACKEND_DATA(r, struct egl_backend_state_t);

  if(r->backend->platform == VT_BACKEND_WAYLAND) {
    struct wl_egl_window* egl_win = (struct wl_egl_window*)output->native_window; 
    wl_egl_window_destroy(egl_win);
  }


  if(!eglDestroySurface(egl->egl_dsp, (EGLSurface)output->render_surface)) {
    int32_t err = eglGetError();
    VT_ERROR(r->comp->log, "eglDestroySurface() failed: 0x%04x (%s)\n", err, _egl_err_str(err));
    return false;
  }
  output->render_surface = NULL;

  VT_TRACE(r->comp->log, "EGL: Destroyed render surface."); 
  return true;
}

bool renderer_import_buffer_egl(
  struct vt_renderer_t *r, struct vt_surface_t *surf,
  struct wl_resource *buffer_resource) {
  struct wl_shm_buffer* shmbuf = wl_shm_buffer_get(buffer_resource);
  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);

  VT_TRACE(r->comp->log, "Importing buffer for surface %p", surf);
  if(shmbuf) {
    return _egl_gl_import_buffer_shm(r, surf, shmbuf); 
  }

  struct vt_linux_dmabuf_v1_buffer_t* dmabuf = vt_proto_linux_dmabuf_v1_from_buffer_res(buffer_resource);

  if(dmabuf && egl->has_dmabuf_support) {
    // import dmabuf
    return _egl_gl_import_buffer_dmabuf(r, dmabuf, surf);
  }

  VT_WARN(r->comp->log, "Unknown buffer type for surface %p", surf);
  return false;
}
  
bool renderer_destroy_surface_texture_egl(struct vt_renderer_t* r, struct vt_surface_t* surf) {
  if(!surf || !r) return false;
  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);
  glBindTexture(GL_TEXTURE_2D, 0);
  if(surf->tex.id)
    glDeleteTextures(1, &surf->tex.id);

  if (surf->render_tex_handle && surf->render_tex_handle != EGL_NO_IMAGE_KHR) {
    eglDestroyImageKHR_ptr(egl->egl_dsp, (EGLImageKHR)surf->render_tex_handle);
    surf->render_tex_handle = EGL_NO_IMAGE_KHR;
  }
  surf->tex.id = 0;
  surf->tex.width = 0;
  surf->tex.height = 0;
  return true;
}

bool 
renderer_drop_context_egl(struct vt_renderer_t* r) {
  if (!r || !r->impl.drop_context || !r->user_data) {
    VT_ERROR(r->comp->log, "EGL: Renderer backend not initialized before dropping context.");
    return false;
  }
  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);
  if(!eglMakeCurrent(egl->egl_dsp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
    VT_ERROR(r->comp->log, "EGL: Cannot drop context: eglMakeCurrent() failed: %s", _egl_err_str(eglGetError()));
    return false;
  }
  return true;
}

void 
renderer_set_vsync_egl(struct vt_renderer_t* r, bool vsync) {
  if (!r || !r->impl.set_vsync || !r->user_data) {
    VT_ERROR(r->comp->log, "EGL: Renderer backend not initialized before setting vsync.");
    return;
  }
  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);
  eglSwapInterval(egl->egl_dsp, (EGLint)vsync);
}

void 
renderer_set_clear_color_egl(struct vt_renderer_t* r, struct vt_output_t* output, uint32_t col) {
  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);
  rn_rect_render(egl->render, (vec2s){0, 0}, (vec2s){output->width, output->height}, RN_WHITE);
}

void 
renderer_stencil_damage_pass_egl(struct vt_renderer_t* r, struct vt_output_t* output) {
  glClear(GL_STENCIL_BUFFER_BIT);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glDepthMask(GL_FALSE);
  glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
  glStencilFunc(GL_ALWAYS, 1, 0xFF);

}

void 
renderer_composite_pass_egl(struct vt_renderer_t* r, struct vt_output_t* output) {
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glStencilFunc(GL_EQUAL, 1, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

}

void 
renderer_begin_scene_egl(struct vt_renderer_t *r, struct vt_output_t *output) {
  if(!output);
  if (!r || !r->impl.begin_scene || !r->user_data) {
    VT_ERROR(r->comp->log, "EGL: Renderer backend not initialized before beginning frame.");
    return;
  }
  if(!output->width || !output->height) {
    VT_WARN(r->comp->log, "EGL: Trying to render on invalid output region (%ix%i).", output->width, output->height);
    return;
  }

  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);

  rn_begin(egl->render);

}

void 
renderer_begin_frame_egl(struct vt_renderer_t *r, struct vt_output_t *output) {
  if(!output);
  if (!r || !r->impl.begin_frame|| !r->user_data) {
    VT_ERROR(r->comp->log, "EGL: Renderer backend not initialized before beginning frame.");
    return;
  }

  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);

  EGLSurface surface = (EGLSurface)output->render_surface;
  if (!eglMakeCurrent(egl->egl_dsp, surface, surface, egl->egl_ctx)) {
    EGLint err = eglGetError();
    VT_ERROR(r->comp->log, "eglMakeCurrent() failed (renderer_begin_frame_egl): 0x%04x (%s)",
             err, _egl_err_str(err));
    return;
  }

  // Resize the render display if the output size changed
  if(egl->render->render.render_w != output->width || egl->render->render.render_h != output->height) {
    rn_resize_display(egl->render, output->width, output->height);
  }
  
  struct egl_output_state_t* egl_output = (struct egl_output_state_t*)output->user_data_render; 
  glBindFramebuffer(GL_FRAMEBUFFER, egl_output->fbo_id);
}


void 
renderer_draw_surface_egl(struct vt_renderer_t* r, struct vt_surface_t* surface, float x, float y) {
  if(!surface) return;
  if(!surface->tex.id) return;
  if (!r || !r->impl.draw_surface || !r->user_data) {
    VT_ERROR(r->comp->log, "EGL: Renderer backend not initialized before rendering surface.");
    return;
  }
  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);

  rn_image_render(egl->render, (vec2s){x,y}, RN_WHITE, surface->tex);
}

void 
renderer_draw_rect_egl(struct vt_renderer_t* r, float x, float y, float w, float h, uint32_t col) {
  if (!r || !r->impl.draw_rect || !r->user_data) {
    VT_ERROR(r->comp->log, "EGL: Renderer backend not initialized before rendering rectangle.");
    return;
  }

  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);
  rn_rect_render(egl->render, (vec2s){x,y}, (vec2s){w,h}, rn_color_from_hex(col)); 
}

void 
renderer_end_scene_egl(struct vt_renderer_t *r, struct vt_output_t *output) {
  if (!r || !r->impl.end_scene || !r->user_data) {
    VT_ERROR(r->comp->log, "EGL: Renderer backend not initialized before ending frame.");
    return;
  }
  
  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);

  rn_end(egl->render);

}

void
renderer_end_frame_egl(struct vt_renderer_t *r, struct vt_output_t *output,  const pixman_box32_t* damaged, int32_t n_damaged) {
  if (!r || !r->impl.end_frame || !r->user_data) {
    VT_ERROR(r->comp->log, "EGL: Renderer backend not initialized before ending frame.");
    return;
  }

  if (!r || !r->impl.end_frame || !r->user_data) {
    VT_ERROR(r->comp->log, "EGL: Renderer backend not initialized before ending frame.");
    return;
  }
  
  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);
  struct egl_output_state_t* egl_output = (struct egl_output_state_t*)output->user_data_render; 

  if(!egl || !egl_output) return;

  glBindFramebuffer(GL_READ_FRAMEBUFFER, egl_output->fbo_id);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBlitFramebuffer(0, 0, output->width, output->height,
                    0, 0, output->width, output->height, 
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);


  if (n_damaged > 0 && eglSwapBuffersWithDamageEXT_ptr) {
    EGLint egl_rects[n_damaged * 4];
    for (int i = 0; i < n_damaged; i++) {
      egl_rects[i * 4 + 0] = damaged[i].x1;
      egl_rects[i * 4 + 1] = output->height - damaged[i].y2;
      egl_rects[i * 4 + 2] = damaged[i].x2 - damaged[i].x1;
      egl_rects[i * 4 + 3] = damaged[i].y2 - damaged[i].y1;
    }

    eglSwapBuffersWithDamageEXT_ptr(
      egl->egl_dsp, output->render_surface, egl_rects, n_damaged);
  } else if (n_damaged > 0 && eglSwapBuffersWithDamageKHR_ptr) {
    EGLint egl_rects[n_damaged * 4];
    for (int i = 0; i < n_damaged; i++) {
      egl_rects[i * 4 + 0] = damaged[i].x1;
      egl_rects[i * 4 + 1] = output->height - damaged[i].y2;
      egl_rects[i * 4 + 2] = damaged[i].x2 - damaged[i].x1;
      egl_rects[i * 4 + 3] = damaged[i].y2 - damaged[i].y1;
    }
    eglSwapBuffersWithDamageKHR_ptr(
      egl->egl_dsp, output->render_surface, egl_rects, n_damaged);
  } else {
    eglSwapBuffers(egl->egl_dsp, output->render_surface);
  }

}

bool
renderer_destroy_egl(struct vt_renderer_t* r) {
  if (!r || !r->impl.destroy || !r->user_data) {
    VT_ERROR(r->comp->log, "EGL: Renderer backend not initialized before destroying backend.");
    return false;
  }
  struct egl_backend_state_t* egl = BACKEND_DATA(r, struct egl_backend_state_t);
  eglMakeCurrent(egl->egl_dsp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

  if (egl->egl_ctx != EGL_NO_CONTEXT) {
    if(!eglDestroyContext(egl->egl_dsp, egl->egl_ctx)) {
      VT_ERROR(r->comp->log, "eglDestroyContext() failed: %s", _egl_err_str(eglGetError()));
      return false;
    }
    egl->egl_ctx = EGL_NO_CONTEXT;
  }

  if(!eglTerminate(egl->egl_dsp)) {
    VT_ERROR(r->comp->log, "EGL: eglTerminate() failed: %s", _egl_err_str(eglGetError()));
    return false;
  }

  r->user_data = NULL;

  return true;
}

