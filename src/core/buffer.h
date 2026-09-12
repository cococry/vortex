#pragma once

#include "core_types.h"
#include "pixman.h"
#include "../render/renderer.h"
#include <runara/runara.h>

struct vt_buffer_t {
  struct wl_resource *res;
  struct wl_listener  destroy;
  bool                destroy_linked;

  RnTexture tex;
  void     *render_tex_handle;

  uint32_t refcount;

  struct vt_renderer_t* renderer;
};

struct vt_linux_explicit_sync_v1_buffer_release_t {
  struct wl_resource* res;
  struct vt_buffer_release_t* release;
};

struct vt_buffer_release_t {
  uint32_t refcount;

  struct wl_list callbacks;

  struct vt_linux_explicit_sync_v1_buffer_release_t *explicit;

  int fence_fd;
};

bool vt_buffer_import(struct vt_buffer_t *buf, const pixman_region32_t *damage);

struct vt_buffer_t* vt_buffer_from_resource(struct vt_renderer_t* renderer, struct wl_resource* res);

void vt_buffer_drop_resource(struct vt_buffer_t *buf);

void vt_buffer_destroy(struct vt_buffer_t *buf);

struct vt_buffer_t* vt_buffer_ref(struct vt_buffer_t *buf);

void vt_buffer_unref(struct vt_buffer_t **buf);

struct vt_buffer_t *vt_buffer_release_ref(struct vt_buffer_release_t *release);

void vt_buffer_release_unref(struct vt_buffer_release_t **release);
