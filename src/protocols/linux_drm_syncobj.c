/*
 * Copyright (c) 2026 Luca Machiedo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* linux-drm-syncobj-v1: timeline points are converted to and from sync_files
 * so the existing acquire/release fence paths (EGL wait, KMS IN_FENCE_FD,
 * render/scanout release fences) do the actual synchronization. */

#include "src/core/profiling.h"
#include "linux_drm_syncobj.h"
#include "src/core/buffer.h"
#include "src/core/content_update.h"
#include "src/core/surface.h"
#include "src/core/util.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <linux-drm-syncobj-v1-server-protocol.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <xf86drm.h>

#define _SUBSYS_NAME "VT_PROTO_LINUX_DRM_SYNCOBJ"

struct vt_drm_syncobj_timeline_t {
  int      drm_fd;
  uint32_t handle;
  uint32_t refcount;

  // release points on this timeline not signaled yet
  struct wl_list releases;
};

struct vt_drm_syncobj_release_t {
  struct vt_buffer_release_t        base;
  struct vt_drm_syncobj_timeline_t *timeline;
  uint64_t                          point;
  bool                              signaled;
  struct wl_list                    link;
};

struct vt_drm_syncobj_acquire_t {
  struct vt_buffer_acquire_source_t base;
  struct vt_drm_syncobj_timeline_t *timeline;
  uint64_t                          point;
};

// holds a content update until its acquire point has been submitted
struct vt_drm_syncobj_acquire_waiter_t {
  struct vt_content_update_constraint_t constraint;
  struct vt_content_update_t           *cu;
  int                                   event_fd;
  struct wl_event_source               *source;
};

static struct {
  struct vt_compositor_t *comp;
  int                     drm_fd;

  // per-run counters, reported at teardown
  struct {
    uint64_t commits, waited, exported, exported_pending;
    uint64_t release_imported, release_signaled;
    uint64_t timelines, release_early;
  } stats;
} _proto = {.drm_fd = -1};

/* ===================================================
 * ==================== TIMELINES ====================
 * =================================================== */

static struct vt_drm_syncobj_timeline_t *
_timeline_ref(struct vt_drm_syncobj_timeline_t *tl) {
  VT_PROFILE_SCOPE();
  tl->refcount++;
  return tl;
}

static void _timeline_unref(struct vt_drm_syncobj_timeline_t **tl_ptr) {
  VT_PROFILE_SCOPE();
  struct vt_drm_syncobj_timeline_t *tl = *tl_ptr;
  *tl_ptr = NULL;
  if (!tl || --tl->refcount != 0)
    return;

  drmSyncobjDestroy(tl->drm_fd, tl->handle);
  free(tl);
}

static bool _timeline_point_available(struct vt_drm_syncobj_timeline_t *tl,
                                      uint64_t                          point) {
  VT_PROFILE_SCOPE();
  uint32_t handle = tl->handle;
  return drmSyncobjTimelineWait(tl->drm_fd, &handle, &point, 1, 0,
                                DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE,
                                NULL) == 0;
}

// absolute timeout of 0 makes this a non-blocking check
static bool _timeline_point_signaled(struct vt_drm_syncobj_timeline_t *tl,
                                     uint64_t                          point) {
  VT_PROFILE_SCOPE();
  uint32_t handle = tl->handle;
  return drmSyncobjTimelineWait(tl->drm_fd, &handle, &point, 1, 0,
                                DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
                                NULL) == 0;
}

// sync_files are what the EGL and KMS fence paths consume
static int _timeline_export_sync_file(struct vt_drm_syncobj_timeline_t *tl,
                                      uint64_t                          point) {
  VT_PROFILE_SCOPE();
  uint32_t tmp;
  if (drmSyncobjCreate(tl->drm_fd, 0, &tmp) != 0)
    return -1;

  // exports happen at draw time: a pending point stalls the composite
  _proto.stats.exported++;
  if (!_timeline_point_signaled(tl, point))
    _proto.stats.exported_pending++;
  int fd = -1;
  if (drmSyncobjTransfer(tl->drm_fd, tmp, 0, tl->handle, point, 0) != 0 ||
      drmSyncobjExportSyncFile(tl->drm_fd, tmp, &fd) != 0)
    fd = -1;

  drmSyncobjDestroy(tl->drm_fd, tmp);
  return fd;
}

static bool _timeline_import_sync_file(struct vt_drm_syncobj_timeline_t *tl,
                                       uint64_t point, int sync_file_fd) {
  VT_PROFILE_SCOPE();
  uint32_t tmp;
  if (drmSyncobjCreate(tl->drm_fd, 0, &tmp) != 0)
    return false;

  _proto.stats.release_imported++;
  bool ok = drmSyncobjImportSyncFile(tl->drm_fd, tmp, sync_file_fd) == 0 &&
            drmSyncobjTransfer(tl->drm_fd, tl->handle, point, tmp, 0, 0) == 0;

  drmSyncobjDestroy(tl->drm_fd, tmp);
  return ok;
}

static void _timeline_signal(struct vt_drm_syncobj_timeline_t *tl,
                             uint64_t                          point) {
  VT_PROFILE_SCOPE();
  _proto.stats.release_signaled++;
  uint32_t handle = tl->handle;
  if (drmSyncobjTimelineSignal(tl->drm_fd, &handle, &point, 1) != 0)
    VT_ERROR(_proto.comp->log,
             "Failed to signal timeline point %" PRIu64 ": %s", point,
             strerror(errno));
}

/* ===================================================
 * ===================== RELEASE =====================
 * =================================================== */

static bool _release_needs_fence(struct vt_buffer_release_t *base) {
  VT_PROFILE_SCOPE();
  (void)base;
  return true;
}

/* Signaling a point signals every earlier one on its timeline. With a
 * timeline shared between buffers, that frees buffers still being read. */
static void _release_mark_signaled(struct vt_drm_syncobj_release_t *rel) {
  VT_PROFILE_SCOPE();
  struct vt_drm_syncobj_release_t *other;
  wl_list_for_each(other, &rel->timeline->releases, link) {
    if (other != rel && other->point < rel->point) {
      _proto.stats.release_early++;
      break;
    }
  }

  rel->signaled = true;
  wl_list_remove(&rel->link);
  wl_list_init(&rel->link);
}

static void _release_finish(struct vt_buffer_release_t *base,
                            int                         release_fence_fd) {
  VT_PROFILE_SCOPE();
  struct vt_drm_syncobj_release_t *rel = wl_container_of(base, rel, base);

  _release_mark_signaled(rel);

  if (release_fence_fd < 0) {
    _timeline_signal(rel->timeline, rel->point);
    return;
  }

  if (_timeline_import_sync_file(rel->timeline, rel->point, release_fence_fd))
    return;

  // signaling early would let the client overwrite a buffer still being read
  VT_WARN(_proto.comp->log,
          "Failed to attach release fence to point %" PRIu64
          ", waiting on the CPU.",
          rel->point);
  struct pollfd pfd = {.fd = release_fence_fd, .events = POLLIN};
  poll(&pfd, 1, 1000);
  _timeline_signal(rel->timeline, rel->point);
}

static void _release_destroy(struct vt_buffer_release_t *base) {
  VT_PROFILE_SCOPE();
  struct vt_drm_syncobj_release_t *rel = wl_container_of(base, rel, base);

  // dropped before ever being used, the buffer was never read
  if (!rel->signaled) {
    _release_mark_signaled(rel);
    _timeline_signal(rel->timeline, rel->point);
  }

  _timeline_unref(&rel->timeline);
  free(rel);
}

static const struct vt_buffer_release_implementation_t _release_impl = {
    .finish = _release_finish,
    .destroy = _release_destroy,
    .needs_release_fence = _release_needs_fence,
};

/* ===================================================
 * ===================== ACQUIRE =====================
 * =================================================== */

static int _acquire_export(struct vt_buffer_acquire_source_t *base) {
  VT_PROFILE_SCOPE();
  struct vt_drm_syncobj_acquire_t *acq = wl_container_of(base, acq, base);

  int fd = _timeline_export_sync_file(acq->timeline, acq->point);
  if (fd < 0)
    VT_ERROR(_proto.comp->log,
             "Failed to export acquire point %" PRIu64 " as sync_file.",
             acq->point);
  return fd;
}

static void _acquire_destroy(struct vt_buffer_acquire_source_t *base) {
  VT_PROFILE_SCOPE();
  struct vt_drm_syncobj_acquire_t *acq = wl_container_of(base, acq, base);
  _timeline_unref(&acq->timeline);
  free(acq);
}

static void _waiter_finish(struct vt_drm_syncobj_acquire_waiter_t *w) {
  VT_PROFILE_SCOPE();
  if (w->source)
    wl_event_source_remove(w->source);
  if (w->event_fd >= 0)
    close(w->event_fd);
  free(w);
}

static void
_waiter_constraint_destroy(struct vt_content_update_constraint_t *c) {
  VT_PROFILE_SCOPE();
  struct vt_drm_syncobj_acquire_waiter_t *w = wl_container_of(c, w, constraint);
  _waiter_finish(w);
}

static int _waiter_handle_ready(int fd, uint32_t mask, void *data) {
  VT_PROFILE_SCOPE();
  (void)fd;
  (void)mask;
  struct vt_drm_syncobj_acquire_waiter_t *w = data;

  // detached before resolving, so cu teardown no longer frees the waiter
  vt_content_update_resolve_constraint(w->cu, &w->constraint);
  _waiter_finish(w);
  return 0;
}

/* Hold the update only until the client has submitted its GPU work, the
 * fence wait (EGL or KMS IN_FENCE_FD) covers the rest. Holding until
 * signaled starves fast clients of buffers: releases queue behind the held
 * commits. The fence itself is only exported if the buffer gets used. */
static bool _acquire_attach(struct vt_content_update_t       *cu,
                            struct vt_drm_syncobj_timeline_t *tl,
                            uint64_t                          point) {
  VT_PROFILE_SCOPE();
  struct vt_drm_syncobj_acquire_t *acq = calloc(1, sizeof(*acq));
  if (!acq)
    return false;

  acq->base.export_fence = _acquire_export;
  acq->base.destroy = _acquire_destroy;
  acq->timeline = _timeline_ref(tl);
  acq->point = point;

  if (cu->acquire_source)
    cu->acquire_source->destroy(cu->acquire_source);
  cu->acquire_source = &acq->base;

  if (_timeline_point_available(tl, point))
    return true;

  struct vt_drm_syncobj_acquire_waiter_t *w = calloc(1, sizeof(*w));
  if (!w)
    return false;

  _proto.stats.waited++;
  w->cu = cu;
  w->constraint.destroy = _waiter_constraint_destroy;
  w->event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

  if (w->event_fd < 0 ||
      drmSyncobjEventfd(tl->drm_fd, tl->handle, point, w->event_fd,
                        DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE) != 0) {
    VT_ERROR(_proto.comp->log, "Failed to wait on acquire point: %s",
             strerror(errno));
    _waiter_finish(w);
    return false;
  }

  w->source = wl_event_loop_add_fd(_proto.comp->wl.evloop, w->event_fd,
                                   WL_EVENT_READABLE, _waiter_handle_ready, w);
  if (!w->source) {
    _waiter_finish(w);
    return false;
  }

  vt_content_update_add_constraint(cu, &w->constraint);
  return true;
}

/* ===================================================
 * ================== SURFACE ADDON ==================
 * =================================================== */

static void
_state_clear_points(struct vt_linux_drm_syncobj_v1_surface_state_t *s) {
  VT_PROFILE_SCOPE();
  _timeline_unref(&s->acquire_timeline);
  _timeline_unref(&s->release_timeline);
}

static bool
_post_surface_error(struct vt_linux_drm_syncobj_v1_surface_state_t *s,
                    uint32_t code, const char *msg) {
  VT_PROFILE_SCOPE();
  wl_resource_post_error(s->res, code, "%s", msg);
  return false;
}

static bool _addon_validate_commit(struct vt_surface_t *surf) {
  VT_PROFILE_SCOPE();
  struct vt_linux_drm_syncobj_v1_surface_state_t *s =
      surf->proto_state.linux_drm_syncobj_v1;
  if (!s || !s->res)
    return true;

  bool has_buffer = surf->pending.buffer_attached && surf->pending.buf;
  bool has_points = s->acquire_timeline || s->release_timeline;

  if (!has_buffer) {
    if (has_points)
      return _post_surface_error(
          s, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_BUFFER,
          "timeline points set without a buffer");
    return true;
  }

  if (!s->acquire_timeline)
    return _post_surface_error(
        s, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_ACQUIRE_POINT,
        "buffer attached without an acquire point");
  if (!s->release_timeline)
    return _post_surface_error(
        s, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_RELEASE_POINT,
        "buffer attached without a release point");

  if (s->acquire_timeline == s->release_timeline &&
      s->acquire_point >= s->release_point)
    return _post_surface_error(
        s, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_CONFLICTING_POINTS,
        "acquire point must be lower than release point on one timeline");

  struct vt_dmabuf_attr_t attr;
  if (!vt_buffer_get_dmabuf(surf->pending.buf, &attr))
    return _post_surface_error(
        s, WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_UNSUPPORTED_BUFFER,
        "explicit sync needs a dmabuf buffer");

  return true;
}

static bool _addon_commit(struct vt_surface_t        *surf,
                          struct vt_content_update_t *cu) {
  VT_PROFILE_SCOPE();
  struct vt_linux_drm_syncobj_v1_surface_state_t *s =
      surf->proto_state.linux_drm_syncobj_v1;
  if (!s || !s->acquire_timeline)
    return true;

  // validated: both points are set and a dmabuf is attached
  _proto.stats.commits++;
  struct vt_drm_syncobj_release_t *rel = calloc(1, sizeof(*rel));
  if (!rel) {
    _state_clear_points(s);
    return false;
  }

  vt_buffer_release_init_and_ref(&rel->base, surf->comp, &_release_impl);
  rel->timeline = _timeline_ref(s->release_timeline);
  rel->point = s->release_point;
  wl_list_insert(&rel->timeline->releases, &rel->link);

  if (cu->state.buffer_release)
    vt_buffer_release_unref(&cu->state.buffer_release);
  cu->state.buffer_release = &rel->base;

  bool ok = _acquire_attach(cu, s->acquire_timeline, s->acquire_point);
  if (!ok)
    VT_ERROR(surf->comp->log, "Failed to attach acquire point to commit.");

  _state_clear_points(s);
  return ok;
}

static void _addon_destroy(struct vt_surface_addon_t *addon) {
  VT_PROFILE_SCOPE();
  struct vt_linux_drm_syncobj_v1_surface_state_t *s =
      wl_container_of(addon, s, addon);

  _state_clear_points(s);

  if (s->res)
    wl_resource_set_user_data(s->res, NULL);

  if (s->surf)
    s->surf->proto_state.linux_drm_syncobj_v1 = NULL;

  free(s);
}

static const struct vt_surface_addon_impl_t _addon_impl = {
    .name = "linux-drm-syncobj-v1",
    .destroy = _addon_destroy,
    .validate_commit = _addon_validate_commit,
    .commit = _addon_commit,
};

/* ===================================================
 * =================== REQUESTS ======================
 * =================================================== */

static void _surface_destroy(struct wl_client   *client,
                             struct wl_resource *resource) {
  VT_PROFILE_SCOPE();
  (void)client;
  wl_resource_destroy(resource);
}

static void _surface_set_point(struct wl_resource *resource,
                               struct wl_resource *timeline_res, uint32_t hi,
                               uint32_t lo, bool acquire) {
  VT_PROFILE_SCOPE();
  struct vt_linux_drm_syncobj_v1_surface_state_t *s =
      wl_resource_get_user_data(resource);
  if (!s) {
    wl_resource_post_error(resource,
                           WP_LINUX_DRM_SYNCOBJ_SURFACE_V1_ERROR_NO_SURFACE,
                           "the wl_surface was destroyed");
    return;
  }

  struct vt_drm_syncobj_timeline_t *tl =
      wl_resource_get_user_data(timeline_res);
  struct vt_drm_syncobj_timeline_t **slot =
      acquire ? &s->acquire_timeline : &s->release_timeline;

  _timeline_unref(slot);
  *slot = _timeline_ref(tl);

  uint64_t point = ((uint64_t)hi << 32) | lo;
  if (acquire)
    s->acquire_point = point;
  else
    s->release_point = point;
}

static void _surface_set_acquire_point(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       struct wl_resource *timeline,
                                       uint32_t point_hi, uint32_t point_lo) {
  VT_PROFILE_SCOPE();
  (void)client;
  _surface_set_point(resource, timeline, point_hi, point_lo, true);
}

static void _surface_set_release_point(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       struct wl_resource *timeline,
                                       uint32_t point_hi, uint32_t point_lo) {
  VT_PROFILE_SCOPE();
  (void)client;
  _surface_set_point(resource, timeline, point_hi, point_lo, false);
}

static const struct wp_linux_drm_syncobj_surface_v1_interface _surface_impl = {
    .destroy = _surface_destroy,
    .set_acquire_point = _surface_set_acquire_point,
    .set_release_point = _surface_set_release_point,
};

static void _surface_handle_resource_destroy(struct wl_resource *resource) {
  VT_PROFILE_SCOPE();
  struct vt_linux_drm_syncobj_v1_surface_state_t *s =
      wl_resource_get_user_data(resource);
  if (!s)
    return;

  // points set since the last commit may be discarded
  s->res = NULL;
  vt_surface_addon_destroy(&s->addon);
}

static void _timeline_destroy(struct wl_client   *client,
                              struct wl_resource *resource) {
  VT_PROFILE_SCOPE();
  (void)client;
  wl_resource_destroy(resource);
}

static const struct wp_linux_drm_syncobj_timeline_v1_interface _timeline_impl =
    {
        .destroy = _timeline_destroy,
};

// points already set keep their own reference
static void _timeline_handle_resource_destroy(struct wl_resource *resource) {
  VT_PROFILE_SCOPE();
  struct vt_drm_syncobj_timeline_t *tl = wl_resource_get_user_data(resource);
  _timeline_unref(&tl);
}

static void _manager_destroy(struct wl_client   *client,
                             struct wl_resource *resource) {
  VT_PROFILE_SCOPE();
  (void)client;
  wl_resource_destroy(resource);
}

static void _manager_get_surface(struct wl_client   *client,
                                 struct wl_resource *resource, uint32_t id,
                                 struct wl_resource *surface_res) {
  VT_PROFILE_SCOPE();
  struct vt_surface_t *surf = wl_resource_get_user_data(surface_res);

  if (surf->proto_state.linux_drm_syncobj_v1) {
    wl_resource_post_error(
        resource, WP_LINUX_DRM_SYNCOBJ_MANAGER_V1_ERROR_SURFACE_EXISTS,
        "wl_surface@%" PRIu32 " already has a syncobj surface",
        wl_resource_get_id(surface_res));
    return;
  }

  struct vt_linux_drm_syncobj_v1_surface_state_t *s = calloc(1, sizeof(*s));
  struct wl_resource                             *res =
      wl_resource_create(client, &wp_linux_drm_syncobj_surface_v1_interface,
                         wl_resource_get_version(resource), id);
  if (!s || !res) {
    free(s);
    if (res)
      wl_resource_destroy(res);
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  s->surf = surf;
  s->res = res;
  s->addon.impl = _addon_impl;
  wl_list_insert(&surf->addons, &s->addon.link);
  surf->proto_state.linux_drm_syncobj_v1 = s;

  wl_resource_set_implementation(res, &_surface_impl, s,
                                 _surface_handle_resource_destroy);
}

static void _manager_import_timeline(struct wl_client   *client,
                                     struct wl_resource *resource, uint32_t id,
                                     int32_t fd) {
  VT_PROFILE_SCOPE();
  struct vt_drm_syncobj_timeline_t *tl = calloc(1, sizeof(*tl));
  if (!tl) {
    close(fd);
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  tl->drm_fd = _proto.drm_fd;
  tl->refcount = 1;
  wl_list_init(&tl->releases);
  _proto.stats.timelines++;
  int ret = drmSyncobjFDToHandle(tl->drm_fd, fd, &tl->handle);
  close(fd);

  if (ret != 0) {
    free(tl);
    wl_resource_post_error(
        resource, WP_LINUX_DRM_SYNCOBJ_MANAGER_V1_ERROR_INVALID_TIMELINE,
        "cannot import DRM syncobj: %s", strerror(errno));
    return;
  }

  struct wl_resource *res =
      wl_resource_create(client, &wp_linux_drm_syncobj_timeline_v1_interface,
                         wl_resource_get_version(resource), id);
  if (!res) {
    _timeline_unref(&tl);
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  wl_resource_set_implementation(res, &_timeline_impl, tl,
                                 _timeline_handle_resource_destroy);
}

static const struct wp_linux_drm_syncobj_manager_v1_interface _manager_impl = {
    .destroy = _manager_destroy,
    .get_surface = _manager_get_surface,
    .import_timeline = _manager_import_timeline,
};

static void _manager_bind(struct wl_client *client, void *data,
                          uint32_t version, uint32_t id) {
  VT_PROFILE_SCOPE();
  (void)data;
  struct wl_resource *res = wl_resource_create(
      client, &wp_linux_drm_syncobj_manager_v1_interface, version, id);
  if (!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  wl_resource_set_implementation(res, &_manager_impl, NULL, NULL);
  VT_TRACE(_proto.comp->log, "linux_drm_syncobj.bind: client bound version %u.",
           version);
}

/* ===================================================
 * =================== PUBLIC API ====================
 * =================================================== */

bool vt_proto_linux_drm_syncobj_v1_init(struct vt_compositor_t *comp,
                                        int                     drm_fd) {
  VT_PROFILE_SCOPE();
  uint64_t cap = 0;
  if (drmGetCap(drm_fd, DRM_CAP_SYNCOBJ_TIMELINE, &cap) != 0 || !cap) {
    VT_TRACE(comp->log, "DRM device lacks timeline syncobjs, not advertising "
                        "linux-drm-syncobj-v1.");
    return false;
  }

  _proto.comp = comp;
  _proto.drm_fd = drm_fd;

  if (!wl_global_create(comp->wl.dsp,
                        &wp_linux_drm_syncobj_manager_v1_interface, 1, NULL,
                        _manager_bind)) {
    VT_ERROR(comp->log, "Cannot create linux-drm-syncobj-v1 global.");
    return false;
  }

  VT_TRACE(comp->log, "Initialized linux-drm-syncobj-v1.");
  return true;
}

void vt_proto_linux_drm_syncobj_v1_log_stats(void) {
  VT_PROFILE_SCOPE();
  if (!_proto.comp)
    return;

  VT_INFO(_proto.comp->log,
          "syncobj: %" PRIu64 " commits, %" PRIu64 " held, %" PRIu64
          " acquire exports (%" PRIu64 " still pending), %" PRIu64
          " release imports, %" PRIu64 " release signals, %" PRIu64
          " timelines, %" PRIu64 " releases past a lower pending point.",
          _proto.stats.commits, _proto.stats.waited, _proto.stats.exported,
          _proto.stats.exported_pending, _proto.stats.release_imported,
          _proto.stats.release_signaled, _proto.stats.timelines,
          _proto.stats.release_early);
}
