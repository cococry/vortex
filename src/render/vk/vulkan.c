#include "vulkan.h"
#include "core/core_types.h"
#include "core/util.h"
#include "runara/runara.h"
#include "subprojects/runara/include/runara/runara.h"
#include "subprojects/runara/include/runara/vulkan.h"
#include <vulkan/vulkan_core.h>
#include <wayland-util.h>

#define _SUBSYS_NAME "VULKAN"

struct vt_vk_instance_t {
  VkInstance inst;
};

struct vt_vk_backend_state_t {
  struct wl_array formats;
  struct wl_list  surface_textures;

  struct rn_state_t *runara;
};

static bool _vk_create_renderer(struct vt_renderer_t      *r,
                                enum vt_backend_platform_t backend_platform,
                                void                      *native_handle) {

  (void)backend_platform;
  (void)native_handle;

  assert(r);

  r->user_data = VT_ALLOC(r->comp, sizeof(struct vt_vk_backend_state_t));
  if (!r->user_data)
    return false;

  struct vt_vk_backend_state_t *vk =
      BACKEND_DATA(r, struct vt_vk_backend_state_t);

  wl_array_init(&vk->formats);
  wl_list_init(&vk->surface_textures);

  if (!vk->runara) {
    struct rn_vk_init_options_t vk_opts = {
        .mode = RN_VK_TARGET_EXTERNAL,
        .platform = {
            .create_surface = r->backend->impl.vk_create_surface,
            .instance_extensions = r->backend->impl.vk_get_instance_extensions,
        }};

    struct rn_init_options_t opts = {.log = {.verbose = r->comp->log.verbose,
                                             .log_stream = r->comp->log.stream},
                                     .vk_options = &vk_opts,
                                     .render_backend =
                                         RN_RENDER_BACKEND_VULKAN};
    vk->runara = rn_state_init(&opts, 0, 0);

    if (!vk->runara) {
      VT_ERROR(r->comp->log, "Failed to initialize Runara rendering engine.");
      return false;
    } else {
      VT_TRACE(r->comp->log, "Initialized Runara rendering engine.");
    }
  }

  return true;
}

bool renderer_init_vk(struct vt_backend_t *backend, struct vt_renderer_t *r,
                      void *native_handle) {
  assert(backend && r && native_handle);

  r->backend = backend;
  r->rendering_backend = VT_RENDERING_BACKEND_VULKAN;

  if (!r->user_data) {
    if (!_vk_create_renderer(r, backend->platform, native_handle))
      return false;
  }

  return true;
}

bool renderer_is_handle_renderable_vk(struct vt_renderer_t *renderer,
                                      void                 *native_handle) {
  (void)renderer;
  (void)native_handle;
  return true;
}

bool renderer_query_dmabuf_formats_vk(struct vt_compositor_t *comp,
                                      void                   *native_handle,
                                      struct wl_array        *formats) {
  (void)comp;
  (void)native_handle;
  (void)formats;
  return true;
}

bool renderer_query_dmabuf_formats_with_renderer_vk(
    struct vt_renderer_t *renderer, struct wl_array *formats) {
  (void)renderer;
  (void)formats;
  return true;
}

bool renderer_setup_renderable_output_vk(struct vt_renderer_t *r,
                                         struct vt_output_t   *output) {
  (void)r;
  (void)output;
  return true;
}

bool renderer_resize_renderable_output_vk(struct vt_renderer_t *r,
                                          struct vt_output_t *output, int32_t w,
                                          int32_t h) {
  (void)r;
  (void)output;
  (void)w;
  (void)h;
  return true;
}

bool renderer_destroy_renderable_output_vk(struct vt_renderer_t *r,
                                           struct vt_output_t   *output) {
  (void)r;
  (void)output;
  return true;
}

bool renderer_import_buffer_vk(struct vt_renderer_t *r, struct vt_buffer_t *buf,
                               const pixman_region32_t *damage) {
  (void)r;
  (void)buf;
  (void)damage;
  return true;
}

bool renderer_drop_context_vk(struct vt_renderer_t *r) {
  (void)r;
  return true;
}

bool renderer_supports_explicit_sync_vk(struct vt_renderer_t *r) {
  (void)r;
  return true;
}

void renderer_set_vsync_vk(struct vt_renderer_t *r, bool vsync) {
  (void)r;
  (void)vsync;
}

void renderer_set_clear_color_vk(struct vt_renderer_t *r,
                                 struct vt_output_t *output, uint32_t col) {
  (void)r;
  (void)output;
  (void)col;
}

void renderer_clear_color_buffer_vk(struct vt_renderer_t *r,
                                    struct vt_output_t   *output) {
  (void)r;
  (void)output;
}

void renderer_stencil_damage_pass_vk(struct vt_renderer_t *r,
                                     struct vt_output_t   *output) {
  (void)r;
  (void)output;
}

void renderer_composite_pass_vk(struct vt_renderer_t *r,
                                struct vt_output_t   *output) {
  (void)r;
  (void)output;
}

void renderer_begin_frame_vk(struct vt_renderer_t *r,
                             struct vt_output_t   *output) {
  (void)r;
  (void)output;
}

void renderer_begin_scene_vk(struct vt_renderer_t *r,
                             struct vt_output_t   *output) {
  (void)r;
  (void)output;
}

void renderer_draw_buffer_vk(struct vt_renderer_t   *r,
                             struct vt_output_t     *output,
                             struct vt_buffer_use_t *use,
                             struct vt_box_t        *src_box,
                             struct vt_box_t        *dst_box) {
  (void)r;
  (void)output;
  (void)use;
  (void)src_box;
  (void)dst_box;
}

void renderer_draw_surface_vk(struct vt_renderer_t *r,
                              struct vt_output_t   *output,
                              struct vt_surface_t  *surface,
                              struct vt_box_t      *src_box,
                              struct vt_box_t      *dst_box) {
  (void)r;
  (void)output;
  (void)surface;
  (void)src_box;
  (void)dst_box;
}

void renderer_draw_surface_simple_vk(struct vt_renderer_t *r,
                                     struct vt_output_t   *output,
                                     struct vt_surface_t *surface, float x,
                                     float y) {
  (void)r;
  (void)output;
  (void)surface;
  (void)x;
  (void)y;
}

void renderer_draw_rect_vk(struct vt_renderer_t *r, float x, float y, float w,
                           float h, uint32_t col) {
  (void)r;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)col;
}

void renderer_end_scene_vk(struct vt_renderer_t *r,
                           struct vt_output_t   *output) {
  (void)r;
  (void)output;
}

void renderer_end_frame_vk(struct vt_renderer_t *r, struct vt_output_t *output,
                           const pixman_box32_t *damaged, int32_t n_damaged) {
  (void)r;
  (void)output;
  (void)damaged;
  (void)n_damaged;
}

bool renderer_destroy_vk(struct vt_renderer_t *r) {
  (void)r;
  return true;
}
