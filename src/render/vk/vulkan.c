#define _POSIX_C_SOURCE 200809L
#include "src/core/profiling.h"
#include "vulkan.h"
#include "config.h"
#include "src/core/output.h"
#include "src/core/scene.h"
#include "src/core/surface_addon.h"
#include "src/input/wl_seat.h"
#include "src/render/drm_format.h"
#include <errno.h>
#include <poll.h>
#include <runara/vulkan.h>
#include <string.h>
#if VT_HAS_X11_INPUT
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#endif
#if VT_HAS_WAYLAND_INPUT
#include <vulkan/vulkan_wayland.h>
#include <wayland-client.h>
#endif

#define _SUBSYS_NAME "VULKAN"

struct vk_texture {
  struct vt_renderer_t *renderer;
  struct rn_image_t     rn;
  bool                  registered, dmabuf, needs_upload;
};

struct vk_surface_texture {
  struct vt_surface_addon_t addon;
  struct wl_list            link;
  struct vk_texture         texture;
  bool                      valid;
};

struct vk_frame_use {
  struct vt_buffer_use_t *use;
  struct vk_texture      *texture;
};

struct vk_output {
  rn_color_t clear_color;
};

struct vt_vk_backend_state_t {
  struct wl_array      formats;
  struct wl_list       surface_textures;
  struct rn_state_t   *runara;
  void                *native_display;
  bool                 vsync, active, failed;
  struct vt_output_t  *output;
  struct vk_frame_use *uses;
  size_t               use_count, use_capacity;
  uint8_t             *pixels;
  size_t               pixel_capacity;
};

static bool _vk_runara_check(struct vt_renderer_t *r, rn_result_t result,
                             const char *operation) {
  VT_PROFILE_SCOPE();
  if (result == RN_OK)
    return true;

  VT_ERROR(r->comp->log, "%s failed: %s", operation, rn_result_to_str(result));
  return false;
}

static rn_color_t _vk_color(uint32_t col) {
  VT_PROFILE_SCOPE();
  return (rn_color_t){((col >> 16) & 255) / 255.0f,
                      ((col >> 24) & 255) / 255.0f, ((col >> 8) & 255) / 255.0f,
                      (col & 255) / 255.0f};
}

static void _vk_texture_finish(struct vk_texture *texture) {
  VT_PROFILE_SCOPE();
  struct vt_vk_backend_state_t *vk = texture->renderer->user_data;

  if (texture->registered)
    rn_texture_release(vk->runara, &texture->rn);

  struct vt_renderer_t *r = texture->renderer;
  *texture = (struct vk_texture){.renderer = r};
}

static void _vk_buffer_destroy(struct vt_buffer_t *buf, const void *owner,
                               void *data) {
  VT_PROFILE_SCOPE();
  (void)buf;
  (void)owner;
  struct vk_texture *texture = data;
  _vk_texture_finish(texture);
  free(texture);
}

static void _vk_buffer_end_use(struct vt_buffer_t *buf, const void *owner,
                               void *data) {
  VT_PROFILE_SCOPE();
  (void)buf;
  (void)owner;
  struct vk_texture *texture = data;
  texture->needs_upload = true;
}

static const struct vt_buffer_attachment_implementation_t vk_buffer_impl = {
    .destroy = _vk_buffer_destroy,
    .end_use = _vk_buffer_end_use,
};

static struct vk_texture *_vk_buffer_texture_get_or_create(struct vt_renderer_t *r,
                                             struct vt_buffer_t   *buf,
                                             bool                  create) {
  VT_PROFILE_SCOPE();
  struct vt_buffer_attachment_t *attachment =
      vt_buffer_find_attachment(buf, r, &vk_buffer_impl);

  if (attachment)
    return attachment->data;

  if (!create)
    return NULL;

  struct vk_texture *texture = calloc(1, sizeof(*texture));

  if (!texture)
    return NULL;

  texture->renderer = r;
  attachment = vt_buffer_add_attachment(buf, r, texture, &vk_buffer_impl);

  if (!attachment) {
    free(texture);
    return NULL;
  }

  wl_list_insert(&r->buffer_attachments, &attachment->link_owner);
  return texture;
}

static void _vk_surface_texture_destroy(struct vt_surface_addon_t *addon) {
  VT_PROFILE_SCOPE();
  struct vk_surface_texture *texture = wl_container_of(addon, texture, addon);
  _vk_texture_finish(&texture->texture);
  wl_list_remove(&texture->link);
  free(texture);
}

static struct vk_surface_texture *
_vk_surface_texture(struct vt_renderer_t *r, struct vt_surface_t *surface,
                    bool create) {
  VT_PROFILE_SCOPE();
  struct vt_surface_addon_t *addon;

  wl_list_for_each(addon, &surface->addons, link) {
    if (addon->impl.destroy == _vk_surface_texture_destroy) {
      struct vk_surface_texture *texture =
          wl_container_of(addon, texture, addon);

      if (texture->texture.renderer == r)
        return texture;
    }
  }

  if (!create)
    return NULL;

  struct vk_surface_texture *texture = calloc(1, sizeof(*texture));

  if (!texture)
    return NULL;

  texture->addon.impl.name = "vulkan-surface-texture";
  texture->addon.impl.destroy = _vk_surface_texture_destroy;
  texture->texture.renderer = r;
  struct vt_vk_backend_state_t *vk = r->user_data;
  wl_list_insert(&surface->addons, &texture->addon.link);
  wl_list_insert(&vk->surface_textures, &texture->link);
  return texture;
}

static bool _vk_import_shm(struct vt_renderer_t *r, struct vk_texture *texture,
                           const struct vt_shm_attr_t *shm,
                           const pixman_region32_t    *damage) {
  VT_PROFILE_SCOPE();
  struct vt_vk_backend_state_t *vk = r->user_data;

  if (!shm->data || shm->width <= 0 || shm->height <= 0 || shm->stride <= 0 ||
      (uint64_t)shm->stride < (uint64_t)shm->width * 4 ||
      (uint64_t)(shm->height - 1) * shm->stride + (uint64_t)shm->width * 4 >
          shm->size ||
      (shm->format != WL_SHM_FORMAT_ARGB8888 &&
       shm->format != WL_SHM_FORMAT_XRGB8888))
    return false;

  bool recreate = !texture->registered || texture->dmabuf ||
                  texture->rn.width != (uint32_t)shm->width ||
                  texture->rn.height != (uint32_t)shm->height;
  pixman_box32_t box = {0, 0, shm->width, shm->height};

  if (!recreate && damage) {
    if (!pixman_region32_not_empty(damage))
      return true;

    box = *pixman_region32_extents(damage);
    box.x1 = VT_MAX(0, box.x1);
    box.y1 = VT_MAX(0, box.y1);
    box.x2 = VT_MIN(shm->width, box.x2);
    box.y2 = VT_MIN(shm->height, box.y2);
  }

  if (box.x2 <= box.x1 || box.y2 <= box.y1)
    return true;

  size_t stride = (size_t)(box.x2 - box.x1) * 4;
  size_t size = stride * (box.y2 - box.y1);

  if (size > vk->pixel_capacity) {
    uint8_t *pixels = realloc(vk->pixels, size);

    if (!pixels)
      return false;

    vk->pixels = pixels;
    vk->pixel_capacity = size;
  }

  /* convert native argb/xrgb rows into runara's packed rgba8 upload format */
  for (int y = box.y1; y < box.y2; ++y) {
    const uint8_t *src = (const uint8_t *)shm->data + (size_t)y * shm->stride +
                         (size_t)box.x1 * 4;
    uint8_t *dst = vk->pixels + (size_t)(y - box.y1) * stride;

    for (int x = box.x1; x < box.x2; ++x) {
      uint32_t pixel;
      memcpy(&pixel, src, sizeof(pixel));
      dst[0] = pixel >> 16;
      dst[1] = pixel >> 8;
      dst[2] = pixel;
      dst[3] = shm->format == WL_SHM_FORMAT_XRGB8888 ? 255 : pixel >> 24;
      src += 4;
      dst += 4;
    }
  }

  rn_result_t result;

  if (recreate) {
    _vk_texture_finish(texture);
    result = rn_image_dedicated(vk->runara, shm->width, shm->height, vk->pixels,
                                size, &texture->rn);
  } else {
    const struct rn_box_u32_t region = {box.x1, box.y1, box.x2 - box.x1,
                                        box.y2 - box.y1};
    result =
        rn_image_update(vk->runara, &texture->rn, &region, vk->pixels, size);
  }

  if (!_vk_runara_check(r, result,
                        recreate ? "rn_image_dedicated" : "rn_image_update"))
    return false;

  texture->registered = true;
  texture->rn.premultiplied = true;
  return true;
}

static bool _vk_query_formats(struct vt_renderer_t *r) {
  struct vt_vk_backend_state_t       *vk = r->user_data;
  const struct rn_vk_dmabuf_format_t *formats;
  size_t                              count;

  if (!_vk_runara_check(r, rn_vk_dmabuf_formats(vk->runara, &formats, &count),
                        "rn_vk_dmabuf_formats"))
    return false;

  for (size_t i = 0; i < count; ++i) {
    if (!vt_drm_format_array_push_pair(&vk->formats, formats[i].format,
                                       formats[i].modifier))
      return false;
  }

  return true;
}

static bool _vk_import_dmabuf(struct vt_renderer_t          *r,
                              struct vk_texture             *texture,
                              const struct vt_dmabuf_attr_t *attr,
                              int                            acquire_fd) {
  VT_PROFILE_SCOPE();
  if (attr->width <= 0 || attr->height <= 0 || attr->num_planes <= 0 ||
      attr->num_planes > RN_VK_DMABUF_MAX_PLANES)
    return false;

  if (texture->registered && !texture->dmabuf)
    return false;

  struct vt_vk_backend_state_t *vk = r->user_data;
  struct rn_vk_dmabuf_desc_t    desc = {
         .width = attr->width,
         .height = attr->height,
         .format = attr->format,
         .modifier = attr->mod,
         .plane_count = attr->num_planes,
         .acquire_fence_fd = acquire_fd,
         .premultiplied = true,
  };

  for (int i = 0; i < attr->num_planes; ++i)
    desc.planes[i] = (struct rn_vk_dmabuf_plane_t){
        attr->fds[i], attr->offsets[i], attr->strides[i]};

  if (!_vk_runara_check(
          r, rn_vk_image_from_dmabuf(vk->runara, &desc, &texture->rn),
          "rn_vk_image_from_dmabuf"))
    return false;

  texture->registered = true;
  texture->dmabuf = true;
  return true;
}

static bool _vk_wait_fd(struct vt_renderer_t *r, int fd) {
  VT_PROFILE_SCOPE();
  struct pollfd wait = {.fd = fd, .events = POLLIN};
  int           result;

  do {
    result = poll(&wait, 1, -1);
  } while (result < 0 && errno == EINTR);

  if (result > 0 && (wait.revents & POLLIN))
    return true;

  VT_ERROR(r->comp->log, "Failed to wait for buffer acquire fence.");
  return false;
}

static void _vk_frame_uses_finish(struct vt_vk_backend_state_t *vk) {
  VT_PROFILE_SCOPE();
  for (size_t i = 0; i < vk->use_count; ++i)
    vt_buffer_use_unref(&vk->uses[i].use);

  vk->use_count = 0;
}

static bool _vk_prepare_use(struct vt_renderer_t   *r,
                            struct vt_buffer_use_t *use,
                            struct vt_surface_t    *surface) {
  VT_PROFILE_SCOPE();
  struct vt_vk_backend_state_t *vk = r->user_data;

  if (!use || !use->buf)
    return false;

  for (size_t i = 0; i < vk->use_count; ++i) {
    if (vk->uses[i].use == use)
      return true;
  }

  if (vk->use_count == vk->use_capacity) {
    size_t capacity = vk->use_capacity ? vk->use_capacity * 2 : 32;
    struct vk_frame_use *uses = realloc(vk->uses, capacity * sizeof(*uses));

    if (!uses)
      return false;

    vk->uses = uses;
    vk->use_capacity = capacity;
  }

  int                     fd = vt_buffer_use_acquire_fence(use);
  struct vk_texture      *texture;
  struct vt_dmabuf_attr_t dmabuf;
  struct vt_shm_attr_t    shm;

  if (vt_buffer_get_dmabuf(use->buf, &dmabuf)) {
    texture = _vk_buffer_texture_get_or_create(r, use->buf, true);

    if (!texture || !_vk_import_dmabuf(r, texture, &dmabuf, fd))
      return false;

    if (surface) {
      struct vk_surface_texture *cached =
          _vk_surface_texture(r, surface, false);

      if (cached)
        cached->valid = false;
    }
  } else if (vt_buffer_get_shm(use->buf, &shm)) {
    if (fd >= 0 && !_vk_wait_fd(r, fd))
      return false;

    if (surface) {
      struct vk_surface_texture *cached = _vk_surface_texture(r, surface, true);

      if (!cached ||
          !_vk_import_shm(r, &cached->texture, &shm,
                          cached->valid ? &surface->applied.damage : NULL))
        return false;

      cached->valid = true;
      texture = &cached->texture;
    } else {
      texture = _vk_buffer_texture_get_or_create(r, use->buf, true);

      if (!texture)
        return false;

      /* unchanged buffers stay uploaded for the duration of their use */
      if ((!texture->registered || texture->needs_upload) &&
          !_vk_import_shm(r, texture, &shm, NULL))
        return false;

      texture->needs_upload = false;
    }
  } else {
    return false;
  }

  vk->uses[vk->use_count++] =
      (struct vk_frame_use){vt_buffer_use_ref(use), texture};

  if (surface)
    pixman_region32_clear(&surface->applied.damage);

  return true;
}

static const char *const *_vk_instance_extensions(void *user, uint32_t *count) {
  struct vt_renderer_t    *r = user;
  static const char *const x11[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                    "VK_KHR_xlib_surface"};
  static const char *const wayland[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                        "VK_KHR_wayland_surface"};
  *count = 2;
  return r->backend->platform == VT_BACKEND_X11 ? x11 : wayland;
}

static VkResult _vk_create_surface(void *user, VkInstance instance,
                                   VkSurfaceKHR *surface) {
  struct vt_output_t   *output = user;
  struct vt_renderer_t *r = output->backend->comp->renderer;

  struct vt_vk_backend_state_t *vk = r->user_data;

  switch (r->backend->platform) {
#if VT_HAS_X11_INPUT

  case VT_BACKEND_X11: {
    VkXlibSurfaceCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = vk->native_display,
        .window = (Window)(uintptr_t)output->native_window,
    };

    return vkCreateXlibSurfaceKHR(instance, &info, NULL, surface);
  }
#endif
#if VT_HAS_WAYLAND_INPUT

  case VT_BACKEND_WAYLAND: {
    VkWaylandSurfaceCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = vk->native_display,
        .surface = output->native_window,
    };

    return vkCreateWaylandSurfaceKHR(instance, &info, NULL, surface);
  }
#endif
  default:
    return VK_ERROR_INITIALIZATION_FAILED;
  }
}

static void _vk_framebuffer_size(void *user, uint32_t *width,
                                 uint32_t *height) {
  struct vt_output_t *output = user;
  *width = output->width;
  *height = output->height;
}

static bool _vk_create_renderer(struct vt_renderer_t      *r,
                                enum vt_backend_platform_t platform,
                                void                      *native_handle) {
  if (platform != VT_BACKEND_X11 && platform != VT_BACKEND_WAYLAND) {
    VT_ERROR(r->comp->log,
             "Vulkan presentation currently requires X11 or Wayland.");
    return false;
  }

  struct vt_vk_backend_state_t *vk = calloc(1, sizeof(*vk));

  if (!vk)
    return false;

  r->user_data = vk;
  vk->native_display = native_handle;
  vk->vsync = platform == VT_BACKEND_WAYLAND;
  wl_array_init(&vk->formats);
  wl_list_init(&vk->surface_textures);
  struct rn_vk_init_options_t options = {
      .mode = RN_VK_TARGET_MANAGED,
      .user = r,
      .instance_extensions = _vk_instance_extensions,
      .enable_dmabuf_import = true,
  };

  struct rn_init_options_t init = {
      .render_backend = RN_RENDER_BACKEND_VULKAN,
      .vk_options = &options,
      .log = {.verbose = r->comp->log.verbose,
              .quiet = r->comp->log.quiet,
              .log_stream = r->comp->log.stream},
  };

  vk->runara = rn_state_init(&init);

  if (!vk->runara) {
    free(vk);
    r->user_data = NULL;
    return false;
  }

  if (!_vk_query_formats(r))
    goto fail;

  VT_INFO(r->comp->log, "Initialized Runara Vulkan renderer.");
  return true;

fail:
  renderer_destroy_vk(r);
  return false;
}

bool renderer_init_vk(struct vt_backend_t *backend, struct vt_renderer_t *r,
                      void *native_handle) {
  if (!backend || !r || !native_handle)
    return false;

  r->backend = backend;
  r->rendering_backend = VT_RENDERING_BACKEND_VULKAN;

  if (!r->user_data &&
      !_vk_create_renderer(r, backend->platform, native_handle))
    return false;

#if VT_HAS_X11_INPUT

  if (backend->platform == VT_BACKEND_X11) {
    Display *display = native_handle;
    r->_desired_render_buffer_format =
        XVisualIDFromVisual(DefaultVisual(display, DefaultScreen(display)));
  }
#endif

  return true;
}

bool renderer_is_handle_renderable_vk(struct vt_renderer_t *r,
                                      void                 *native_handle) {
  return r && r->user_data && native_handle;
}

bool renderer_query_dmabuf_formats_vk(struct vt_compositor_t *comp,
                                      void                   *native_handle,
                                      struct wl_array        *formats) {
  return comp && native_handle &&
         renderer_query_dmabuf_formats_with_renderer_vk(comp->renderer,
                                                        formats);
}

bool renderer_query_dmabuf_formats_with_renderer_vk(struct vt_renderer_t *r,
                                                    struct wl_array *formats) {
  if (!r || !r->user_data || !formats)
    return false;

  struct vt_vk_backend_state_t *vk = r->user_data;
  return vt_drm_format_array_copy(formats, &vk->formats);
}

bool renderer_setup_renderable_output_vk(struct vt_renderer_t *r,
                                         struct vt_output_t   *output) {
  if (!r || !r->user_data || !output || output->backend != r->backend ||
      !output->native_window)
    return false;

  if (r->backend->platform != VT_BACKEND_X11 &&
      r->backend->platform != VT_BACKEND_WAYLAND)
    return false;

  if (output->render_surface)
    return true;

  struct vt_vk_backend_state_t *vk = r->user_data;
  struct vk_output             *state = calloc(1, sizeof(*state));

  if (!state)
    return false;

  state->clear_color = (rn_color_t){0, 0, 0, 1};
  output->user_data_render = state;
  struct rn_vk_surface_impl_t impl = {
      .user = output,
      .create_vk_surface = _vk_create_surface,
      .framebuffer_size = _vk_framebuffer_size,
      .disable_vsync = !vk->vsync,
      .allow_vsync_fallback = true,
  };

  struct rn_vk_managed_surface_t *surface = NULL;
  rn_result_t result = rn_vk_surface_create(vk->runara, &impl, &surface);

  if (!_vk_runara_check(r, result, "rn_vk_surface_create")) {
    output->user_data_render = NULL;
    free(state);
    return false;
  }

  output->render_surface = surface;
  output->user_data_render = state;
  vt_output_damage_whole(output);
  return true;
}

bool renderer_resize_renderable_output_vk(struct vt_renderer_t *r,
                                          struct vt_output_t *output, int32_t w,
                                          int32_t h) {
  if (!r || !r->user_data || !output || !output->render_surface || w <= 0 ||
      h <= 0)
    return false;

  /* runara observes framebuffer dimensions and recreates the swapchain */
  output->width = w;
  output->height = h;
  output->needs_damage_rebuild = true;
  vt_output_damage_whole(output);
  return true;
}

bool renderer_destroy_renderable_output_vk(struct vt_renderer_t *r,
                                           struct vt_output_t   *output) {
  if (!r || !r->user_data || !output)
    return false;

  if (!output->render_surface)
    return true;

  struct vt_vk_backend_state_t *vk = r->user_data;

  if (!_vk_runara_check(
          r, rn_vk_surface_destroy(vk->runara, output->render_surface),
          "rn_vk_surface_destroy"))
    return false;

  output->render_surface = NULL;
  free(output->user_data_render);
  output->user_data_render = NULL;
  return true;
}

bool renderer_import_buffer_vk(struct vt_renderer_t *r, struct vt_buffer_t *buf,
                               const pixman_region32_t *damage) {
  VT_PROFILE_SCOPE();
  if (!r || !r->user_data || !buf)
    return false;

  struct vt_vk_backend_state_t *vk = r->user_data;

  if (vk->active)
    return false;

  struct vk_texture *texture = _vk_buffer_texture_get_or_create(r, buf, true);

  if (!texture)
    return false;

  struct vt_dmabuf_attr_t dmabuf;
  struct vt_shm_attr_t    shm;

  if (vt_buffer_get_dmabuf(buf, &dmabuf))
    return _vk_import_dmabuf(r, texture, &dmabuf, -1);

  if (vt_buffer_get_shm(buf, &shm)) {
    bool imported = _vk_import_shm(r, texture, &shm, damage);

    if (imported)
      texture->needs_upload = false;

    return imported;
  }

  return false;
}

bool renderer_is_dmabuf_importable_vk(struct vt_renderer_t          *r,
                                      const struct vt_dmabuf_attr_t *attr) {
  VT_PROFILE_SCOPE();
  if (!r || !r->user_data || !attr)
    return false;

  struct vt_vk_backend_state_t *vk = r->user_data;

  if (vk->active)
    return false;

  struct vk_texture texture = {.renderer = r};
  bool              imported = _vk_import_dmabuf(r, &texture, attr, -1);
  _vk_texture_finish(&texture);
  return imported;
}

bool renderer_drop_context_vk(struct vt_renderer_t *r) {
  if (!r || !r->user_data)
    return false;

  struct vt_vk_backend_state_t *vk = r->user_data;

  if (vk->active)
    return false;

  rn_vk_surface_set_current(vk->runara, NULL);
  return true;
}

bool renderer_supports_explicit_sync_vk(struct vt_renderer_t *r) {
  VT_PROFILE_SCOPE();
  /* acquire fences are waited on before sampling, releases follow gpu
   * completion */
  return r && r->user_data;
}

void renderer_set_vsync_vk(struct vt_renderer_t *r, bool vsync) {
  if (!r || !r->user_data)
    return;

  struct vt_vk_backend_state_t *vk = r->user_data;

  if (vk->vsync == vsync || vk->active)
    return;

  vk->vsync = vsync;
  struct vt_output_t *output;

  /* runara fixes present mode at surface creation */
  wl_list_for_each(output, &r->comp->outputs, link_global) {
    if (output->backend != r->backend || !output->render_surface)
      continue;

    struct vk_output state = *(struct vk_output *)output->user_data_render;

    if (!renderer_destroy_renderable_output_vk(r, output) ||
        !renderer_setup_renderable_output_vk(r, output)) {
      VT_ERROR(r->comp->log, "Failed to change Vulkan presentation mode.");
      continue;
    }

    *(struct vk_output *)output->user_data_render = state;
  }
}

void renderer_set_clear_color_vk(struct vt_renderer_t *r,
                                 struct vt_output_t *output, uint32_t col) {
  VT_PROFILE_SCOPE();
  if (!r || !r->user_data || !output || !output->user_data_render)
    return;

  struct vk_output *state = output->user_data_render;
  state->clear_color = _vk_color(col);
}

void renderer_clear_color_buffer_vk(struct vt_renderer_t *r,
                                    struct vt_output_t   *output) {
  VT_PROFILE_SCOPE();
  if (!r || !r->user_data)
    return;

  struct vt_vk_backend_state_t *vk = r->user_data;

  if (vk->active && vk->output == output)
    _vk_runara_check(
        r,
        rn_clear_color(
            vk->runara,
            ((struct vk_output *)output->user_data_render)->clear_color),
        "rn_clear_color");
}

void renderer_stencil_damage_pass_vk(struct vt_renderer_t *r,
                                     struct vt_output_t   *output) {
  VT_PROFILE_SCOPE();
  (void)r;
  /* swapchain images have no preserved contents, so composite the entire scene
   */
  output->cached_damage[0] =
      (pixman_box32_t){0, 0, output->width, output->height};
  output->n_damage_boxes = 1;
}

void renderer_composite_pass_vk(struct vt_renderer_t *r,
                                struct vt_output_t   *output) {
  VT_PROFILE_SCOPE();
  (void)r;
  (void)output;
  /* runara owns the render pass and blending state */
}

void renderer_begin_frame_vk(struct vt_renderer_t *r,
                             struct vt_output_t   *output) {
  VT_PROFILE_SCOPE();
  if (!r || !r->user_data || !output || !output->render_surface ||
      !output->width || !output->height)
    return;

  struct vt_vk_backend_state_t *vk = r->user_data;

  if (vk->active || vk->failed)
    return;

  vk->output = output;
  rn_vk_surface_set_current(vk->runara, output->render_surface);
  const struct rn_box_u32_t canvas = {0, 0, output->width, output->height};
  rn_canvas_set_bounds(vk->runara, &canvas);
  /* imports and uploads must precede rn_begin, which opens the render pass */
  struct vt_scene_t *scene = r->comp->scene;

  for (size_t i = 0; scene && i < scene->n_layers; ++i) {
    struct vt_output_layer_state_t *layer = &scene->layers[i];

    if (layer->accepted)
      continue;

    struct vt_surface_t *surface =
        layer->type == VT_OUTPUT_LAYER_SURFACE ? layer->surface : NULL;
    struct vt_buffer_use_t *use =
        surface ? surface->current_buf_use : layer->use;

    if (!_vk_prepare_use(r, use, surface))
      VT_ERROR(r->comp->log, "Failed to prepare Vulkan scene buffer.");
  }

  struct vt_seat_cursor_image_t cursor =
      vt_seat_get_cursor_image(r->comp->seat);

  if (output->cursor_mode != VT_CURSOR_MODE_HARDWARE && cursor.surf &&
      cursor.surf->mapped)
    _vk_prepare_use(r, cursor.surf->current_buf_use, cursor.surf);
}

void renderer_begin_scene_vk(struct vt_renderer_t *r,
                             struct vt_output_t   *output) {
  VT_PROFILE_SCOPE();
  if (!r || !r->user_data)
    return;

  struct vt_vk_backend_state_t *vk = r->user_data;

  if (vk->output != output || vk->active || vk->failed)
    return;

  vk->active = _vk_runara_check(r, rn_begin(vk->runara), "rn_begin");
}

static bool _vk_draw_use(struct vt_renderer_t *r, struct vt_output_t *output,
                         struct vt_buffer_use_t *use, uint32_t width,
                         uint32_t height, struct vt_box_t *src,
                         struct vt_box_t *dst) {
  VT_PROFILE_SCOPE();
  if (!r || !r->user_data || !use || !src || !dst || !width || !height)
    return false;

  struct vt_vk_backend_state_t *vk = r->user_data;

  if (!vk->active || vk->output != output)
    return false;

  struct vk_texture *texture = NULL;

  for (size_t i = 0; i < vk->use_count; ++i) {
    if (vk->uses[i].use == use) {
      texture = vk->uses[i].texture;
      break;
    }
  }

  if (!texture || !texture->registered)
    return false;

  struct rn_image_t image = texture->rn;
  image.uv[0] = (float)src->x / width;
  image.uv[1] = (float)src->y / height;
  image.uv[2] = (float)(src->x + src->width) / width;
  image.uv[3] = (float)(src->y + src->height) / height;
  struct rn_rect_t rect = {dst->x, dst->y, dst->width, dst->height};
  rn_color_t       tint = {1, 1, 1, 1};
  return _vk_runara_check(r, rn_draw_texture(vk->runara, &image, rect, tint),
                          "rn_draw_texture");
}

void renderer_draw_buffer_vk(struct vt_renderer_t   *r,
                             struct vt_output_t     *output,
                             struct vt_buffer_use_t *use, struct vt_box_t *src,
                             struct vt_box_t *dst) {
  VT_PROFILE_SCOPE();
  if (use && use->buf)
    _vk_draw_use(r, output, use, use->buf->width, use->buf->height, src, dst);
}

void renderer_draw_surface_vk(struct vt_renderer_t *r,
                              struct vt_output_t   *output,
                              struct vt_surface_t  *surface,
                              struct vt_box_t *src, struct vt_box_t *dst) {
  VT_PROFILE_SCOPE();
  if (!surface ||
      !_vk_draw_use(r, output, surface->current_buf_use, surface->applied.width,
                    surface->applied.height, src, dst))
    return;

  vt_output_track_presented_surface(output, surface);
  surface->damaged = false;
}

void renderer_draw_surface_simple_vk(struct vt_renderer_t *r,
                                     struct vt_output_t   *output,
                                     struct vt_surface_t *surface, float x,
                                     float y) {
  VT_PROFILE_SCOPE();
  if (!surface)
    return;

  struct vt_box_t src = {.width = surface->applied.width,
                         .height = surface->applied.height};
  struct vt_box_t dst = {
      .x = x, .y = y, .width = src.width, .height = src.height};
  renderer_draw_surface_vk(r, output, surface, &src, &dst);
}

void renderer_draw_rect_vk(struct vt_renderer_t *r, float x, float y, float w,
                           float h, uint32_t col) {
  VT_PROFILE_SCOPE();
  if (!r || !r->user_data)
    return;

  struct vt_vk_backend_state_t *vk = r->user_data;

  if (vk->active)
    _vk_runara_check(r,
                     rn_draw_rect(vk->runara, (struct rn_rect_t){x, y, w, h},
                                  _vk_color(col)),
                     "rn_draw_rect");
}

void renderer_end_scene_vk(struct vt_renderer_t *r,
                           struct vt_output_t   *output) {
  VT_PROFILE_SCOPE();
  if (!r || !r->user_data)
    return;

  struct vt_vk_backend_state_t *vk = r->user_data;

  if (!vk->active || vk->output != output)
    return;

  bool ended = _vk_runara_check(r, rn_end(vk->runara), "rn_end");
  vk->active = false;

  if (!ended)
    vk->failed = true;
}

void renderer_end_frame_vk(struct vt_renderer_t *r, struct vt_output_t *output,
                           const pixman_box32_t *damaged, int32_t n_damaged) {
  VT_PROFILE_SCOPE();
  (void)damaged;
  (void)n_damaged;

  if (!r || !r->user_data)
    return;

  struct vt_vk_backend_state_t *vk = r->user_data;

  if (vk->output != output)
    return;

  /* runara has no submission fence export, retain client storage until
   * completion */

  if (!_vk_runara_check(r, rn_vk_wait_for_render(vk->runara),
                        "rn_vk_wait_for_render")) {
    vk->failed = true;
    r->comp->running = false;
    return;
  }

  _vk_frame_uses_finish(vk);
  vk->output = NULL;
}

bool renderer_destroy_vk(struct vt_renderer_t *r) {
  if (!r)
    return false;

  struct vt_vk_backend_state_t *vk = r->user_data;

  if (!vk)
    return true;

  rn_vk_wait_for_render(vk->runara);
  _vk_frame_uses_finish(vk);
  struct vt_output_t *output;

  wl_list_for_each(output, &r->comp->outputs, link_global) {
    if (output->backend == r->backend &&
        !renderer_destroy_renderable_output_vk(r, output))
      return false;
  }

  struct vk_surface_texture *texture, *tmp;

  wl_list_for_each_safe(texture, tmp, &vk->surface_textures, link)
      vt_surface_addon_destroy(&texture->addon);

  struct vt_buffer_attachment_t *attachment, *next;

  wl_list_for_each_safe(attachment, next, &r->buffer_attachments, link_owner) {
    wl_list_remove(&attachment->link_owner);
    wl_list_remove(&attachment->link_buf);
    _vk_buffer_destroy(attachment->buf, r, attachment->data);
    free(attachment);
  }

  rn_state_fini(vk->runara);
  vt_drm_format_array_free(&vk->formats);
  free(vk->pixels);
  free(vk->uses);
  free(vk);
  r->user_data = NULL;
  return true;
}
