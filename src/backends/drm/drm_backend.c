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

#define _GNU_SOURCE

#include "core/core_types.h"
#include <drm/drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <inttypes.h>
#include <libinput.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <errno.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include "core/compositor.h"
#include "core/surface.h"
#include "render/renderer.h"

#include "./drm.h"
#include "./session_drm.h"

#include "core/session.h"
#include "core/util.h"
#include "input/wl_seat.h"
#include "protocols/linux_dmabuf.h"
#include "protocols/linux_explicit_sync.h"
#include "protocols/wl_output.h"
#include "protocols/wl_shm.h"
#include "render/dmabuf.h"
#include "render/drm_format.h"

#include "drm_backend.h"
#include "drm_types.h"
#include "fb.h"
#include "kms.h"
#include "prime.h"

#include "props.h"

#include <linux/input-event-codes.h>

#define _SUBSYS_NAME "DRM"

static void _drm_page_flip_handler(int fd, unsigned int frame, unsigned int sec,
                                   unsigned int usec, void *data);
static void _drm_release_all_scanout(struct vt_output_t *output);
static bool _drm_devices_equal(drmDevicePtr a, drmDevicePtr b);
static bool _drm_can_share_dmabuf(struct vt_device_t *main_dev,
                                  struct vt_device_t *dev);
static const char *_fourcc_to_str(uint32_t fmt);
static const char *_modifier_to_str(uint64_t mod, char *buf, size_t len);
static void        _log_dmabuf_tranche(struct vt_compositor_t           *comp,
                                       const struct vt_dmabuf_tranche_t *tranche,
                                       const char                       *device_path);
static bool
_drm_build_dmabuf_feedback(struct drm_backend_master_state_t *master,
                           struct vt_dmabuf_feedback_t       *feedback);
static bool _drm_suspend(struct drm_backend_state_t *backend);
static bool _drm_resume(struct drm_backend_state_t *backend);
static int  _drm_dispatch(int fd, uint32_t mask, void *data);
static bool _drm_init_for_device(struct vt_compositor_t     *comp,
                                 struct drm_backend_state_t *drm,
                                 struct vt_device_t         *dev);
static struct vt_output_mode_t *
_drm_create_output_mode(struct wl_list *list, drmModeModeInfo *mode_info);
static uint32_t    _drm_subpixel_to_wl(drmModeSubPixel subpixel);
static const char *_drm_connector_type_name(uint32_t type);
static bool _drm_create_output_for_device(struct drm_backend_state_t *drm,
                                          struct vt_output_t         *output,
                                          void                       *data);
static bool _drm_destroy_output_for_device(struct drm_backend_state_t *drm,
                                           struct vt_output_t         *output);
static bool _drm_terminate_for_device(struct drm_backend_state_t *drm);
static void _drm_on_session_terminate(struct wl_listener *listener, void *data);
static void _drm_on_seat_disable(struct wl_listener *listener, void *data);

static void _drm_keybind_switch_vt(struct vt_compositor_t *comp,
                                   void                   *user_data);
static void _drm_on_seat_enable(struct wl_listener *listener, void *data);
static bool _drm_handle_frame_for_device(struct drm_backend_state_t *drm,
                                         struct vt_output_t         *output);

static void _drm_update_tearing_cap(struct drm_backend_state_t *drm);

static bool _drm_verify_caps(struct drm_backend_state_t *drm);
static bool _drm_resources_init(struct drm_backend_state_t *drm);
static bool _drm_scan_connectors(struct drm_backend_state_t *drm);
static struct drm_crtc_t *_drm_pick_crtc(struct drm_backend_state_t *drm,
                                         drmModeConnector *conn);
static void _drm_assign_crtc_planes(struct drm_backend_state_t *drm);
static bool _drm_output_sync_refresh(struct drm_output_state_t *drm_output);
static bool _drm_plane_has_format(struct drm_plane_t *plane,
                                  uint32_t format, uint64_t modifier);
static void _drm_on_drm_change(struct wl_listener *listener, void *data);

static bool _added_global_keybinds = false;

static void _drm_scanout_finish(struct drm_output_state_t *drm_output,
                                struct drm_scanout_buffer_t *scanout,
                                int release_fence_fd) {
  if (!drm_output || !drm_output->drm_backend || !scanout)
    return;

  if (scanout->use && release_fence_fd >= 0 && scanout->use->release &&
      vt_buffer_release_needs_fence(scanout->use->release)) {
    vt_buffer_use_set_release_fence_fd(scanout->use, release_fence_fd);
  }

  drm_fb_finish(drm_output->drm_backend, &scanout->fb);

  if (scanout->bo && scanout->surface)
    gbm_surface_release_buffer(scanout->surface, scanout->bo);

  if (scanout->use)
    vt_buffer_use_unref(&scanout->use);

  memset(scanout, 0, sizeof(*scanout));
}

static void _drm_scanout_layers_finish(struct drm_output_state_t *drm_output,
                                       struct wl_array *layers,
                                       int release_fence_fd) {
  if (!drm_output || !layers)
    return;

  struct drm_scanout_layer_t *layer;
  wl_array_for_each(layer, layers) {
    _drm_scanout_finish(drm_output, &layer->scanout, release_fence_fd);
  }

  wl_array_release(layers);
  wl_array_init(layers);
}

static void _drm_complete_pending(struct drm_output_state_t *drm_output) {
  if (!drm_output || !drm_output->base || !drm_output->pending_valid)
    return;

  if (drm_output->current_valid) {
    _drm_scanout_layers_finish(drm_output, &drm_output->current_layers,
                               drm_output->pending_out_fence_fd);
  }

  drm_output->current_layers = drm_output->pending_layers;
  wl_array_init(&drm_output->pending_layers);

  drm_output->current_valid = drm_output->current_layers.size != 0;
  drm_output->pending_valid = false;
  drm_output->flip_inflight = false;
  drm_output->modeset_bootstrapped = true;

  if (drm_output->pending_out_fence_fd >= 0) {
    close(drm_output->pending_out_fence_fd);
    drm_output->pending_out_fence_fd = -1;
  }
}

static void _drm_page_flip_handler(int fd, unsigned int frame, unsigned int sec,
                                   unsigned int usec, void *data) {
  (void)fd;
  (void)frame;
  (void)sec;
  (void)usec;

  struct vt_output_t *output = (struct vt_output_t *)data;
  if (!output || !output->user_data || !output->backend ||
      !output->backend->comp) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return;
  }

  struct drm_output_state_t *drm_output =
      BACKEND_DATA(output, struct drm_output_state_t);
  if (!drm_output || !drm_output->flip_inflight ||
      !drm_output->pending_valid) {
    VT_WARN(output->backend->comp->log,
            "Ignoring page flip without pending scanout state.");
    return;
  }

  _drm_complete_pending(drm_output);

  if (!drm_output->connector_seen) {
    _drm_destroy_output_for_device(drm_output->drm_backend, output);
    return;
  }

  uint32_t t = vt_util_get_time_msec();
  vt_comp_frame_done(output->backend->comp, output, t);

  if (output->needs_repaint)
    vt_comp_schedule_repaint(output->backend->comp, output);
}

static void _drm_release_all_scanout(struct vt_output_t *output) {
  if (!output || !output->user_data)
    return;

  struct drm_output_state_t *drm_output =
      BACKEND_DATA(output, struct drm_output_state_t);
  if (!drm_output)
    return;

  if (drm_output->pending_valid) {
    _drm_scanout_layers_finish(drm_output, &drm_output->pending_layers, -1);
    drm_output->pending_valid = false;
  }

  if (drm_output->current_valid) {
    _drm_scanout_layers_finish(drm_output, &drm_output->current_layers, -1);
    drm_output->current_valid = false;
  }

  _drm_scanout_layers_finish(drm_output, &drm_output->layer_plan, -1);

  if (drm_output->pending_out_fence_fd >= 0) {
    close(drm_output->pending_out_fence_fd);
    drm_output->pending_out_fence_fd = -1;
  }
}

static bool _drm_devices_equal(drmDevicePtr a, drmDevicePtr b) {
  if (!a || !b)
    return false;
  return drmDevicesEqual(a, b) != 0;
}

static bool _drm_can_share_dmabuf(struct vt_device_t *main_dev,
                                  struct vt_device_t *dev) {
  if (!main_dev || !dev || !main_dev)
    return false;

  drmDevicePtr main_dev_drm = NULL, dev_drm = NULL;

  if (drmGetDevice(main_dev->fd, &main_dev_drm) != 0 ||
      drmGetDevice(dev->fd, &dev_drm) != 0) {
    fprintf(stderr, "drmGetDevice() failed\n");
    if (main_dev_drm)
      drmFreeDevice(&main_dev_drm);
    if (dev_drm)
      drmFreeDevice(&dev_drm);
    return false;
  }

  if (_drm_devices_equal(dev_drm, main_dev_drm)) {
    drmFreeDevice(&main_dev_drm);
    drmFreeDevice(&dev_drm);
    return true;
  }

  bool compatible = false;

  // if the devices do not have the same bustype, they cannot share memory
  if (dev_drm->bustype == main_dev_drm->bustype) {
    if (main_dev_drm->bustype == DRM_BUS_PCI) {
      if (main_dev_drm->businfo.pci && dev_drm->businfo.pci &&
          main_dev_drm->businfo.pci->domain == dev_drm->businfo.pci->domain &&
          main_dev_drm->businfo.pci->bus == dev_drm->businfo.pci->bus &&
          main_dev_drm->businfo.pci->dev == dev_drm->businfo.pci->dev &&
          main_dev_drm->businfo.pci->func == dev_drm->businfo.pci->func) {
        compatible = true;
      }
    } else if (memcmp(&main_dev_drm->businfo, &dev_drm->businfo,
                      sizeof(dev_drm->businfo)) == 0) {
      compatible = true;
    }
  }

  if (!compatible) {
    char card_main_dev[64], card_dev[64];
    // the card paths are '/dev/dri/cardX', we need to get only 'cardX':
    const char *base_main = strrchr(main_dev->path, '/');
    const char *base_other = strrchr(dev->path, '/');
    if (!base_main || !base_other) {
      compatible = false;
    } else {
      // If the devices share the same IOMMU group, they can share dmabufs
      snprintf(card_main_dev, sizeof(card_main_dev), "%s", base_main + 1);
      snprintf(card_dev, sizeof(card_dev), "%s", base_other + 1);
      char path_a[256], path_b[256];
      snprintf(path_a, sizeof(path_a), "/sys/class/drm/%s/device/iommu_group",
               card_main_dev);
      snprintf(path_b, sizeof(path_b), "/sys/class/drm/%s/device/iommu_group",
               card_dev);

      char    link_a[256], link_b[256];
      ssize_t len_a = readlink(path_a, link_a, sizeof(link_a) - 1);
      ssize_t len_b = readlink(path_b, link_b, sizeof(link_b) - 1);
      if (len_a < 0 || len_b < 0) {
        compatible = false;
      } else {
        link_a[len_a] = 0;
        link_b[len_b] = 0;
        compatible = strcmp(link_a, link_b) == 0;
      }
    }
  }

  drmFreeDevice(&main_dev_drm);
  drmFreeDevice(&dev_drm);
  return compatible;
}

static const char *_fourcc_to_str(uint32_t fmt) {
  static char str[5];
  str[0] = fmt & 0xFF;
  str[1] = (fmt >> 8) & 0xFF;
  str[2] = (fmt >> 16) & 0xFF;
  str[3] = (fmt >> 24) & 0xFF;
  str[4] = '\0';
  return str;
}

static const char *_modifier_to_str(uint64_t mod, char *buf, size_t len) {
  if (!buf || len == 0)
    return NULL;

  char *vendor = drmGetFormatModifierVendor(mod);
  char *name = drmGetFormatModifierName(mod);

  if (vendor && name) {
    snprintf(buf, len, "%s:%s (0x%016" PRIx64 ")", vendor, name, mod);
  } else if (name) {
    snprintf(buf, len, "%s (0x%016" PRIx64 ")", name, mod);
  } else if (vendor) {
    snprintf(buf, len, "%s:UNKNOWN (0x%016" PRIx64 ")", vendor, mod);
  } else {
    snprintf(buf, len, "UNKNOWN (0x%016" PRIx64 ")", mod);
  }

  free(vendor);
  free(name);

  return buf;
}

static void _log_dmabuf_tranche(struct vt_compositor_t           *comp,
                                const struct vt_dmabuf_tranche_t *tranche,
                                const char                       *device_path) {
  if (!comp || !tranche || !tranche->target_device || !device_path)
    return;

  char dev_path[64];
  snprintf(dev_path, sizeof(dev_path), "%s", device_path);

  VT_TRACE(
      comp->log,
      "========== Added DMABUF Tranche for device '%s'  ========== ", dev_path);

  VT_TRACE(comp->log, "      target_device: %u:%u  (dev_t: 0x%lx)",
           major(tranche->target_device->dev),
           minor(tranche->target_device->dev),
           (unsigned long)tranche->target_device->dev);

  VT_TRACE(
      comp->log, "      flags: 0x%x%s%s", tranche->flags,
      tranche->flags & VT_DMABUF_TRANCHE_FLAG_DIRECT_SCANOUT ? " DIRECT_SCANOUT"
                                                             : "",
      tranche->flags & VT_DMABUF_TRANCHE_FLAG_COMPOSITE ? " COMPOSITE" : "");

  size_t n_formats = vt_drm_format_array_count(&tranche->formats);
  VT_TRACE(comp->log, "      formats: %zu total", n_formats);

  struct vt_drm_format_t *fmt;
  wl_array_for_each(fmt, &tranche->formats) {
    const char *format_name = drmGetFormatName(fmt->format);

    VT_TRACE(comp->log, "        • %s (%4.4s), %zu modifiers:",
             format_name ? format_name : "UNKNOWN",
             _fourcc_to_str(fmt->format),
             vt_drm_format_mod_count(fmt));

    struct vt_drm_format_modifier_t *mod;
    wl_array_for_each(mod, &fmt->mods) {
      char mod_str[256];
      _modifier_to_str(mod->mod, mod_str, sizeof(mod_str));

      VT_TRACE(comp->log, "            - %s%s", mod_str,
               mod->_egl_ext_only ? " (EXT_ONLY)" : "");
    }
  }

  VT_TRACE(comp->log,
           "=========================================================== ");
}

static void _log_drm_format_array(struct vt_compositor_t *comp,
                                  const char *name,
                                  const struct wl_array *formats) {
  VT_TRACE(comp->log, "========== %s ==========", name);

  const struct vt_drm_format_t *fmt;
  wl_array_for_each(fmt, formats) {
    if (fmt->format != DRM_FORMAT_ARGB8888)
      continue;

    VT_TRACE(comp->log, "AR24: %zu modifiers",
             vt_drm_format_mod_count(fmt));

    const struct vt_drm_format_modifier_t *mod;
    wl_array_for_each(mod, &fmt->mods) {
      char mod_str[256];
      _modifier_to_str(mod->mod, mod_str, sizeof(mod_str));

      VT_TRACE(comp->log, "  - %s%s",
               mod_str,
               mod->_egl_ext_only ? " (EXT_ONLY)" : "");
    }
  }
}

static bool
_drm_build_dmabuf_feedback(struct drm_backend_master_state_t *master,
                           struct vt_dmabuf_feedback_t       *feedback) {
  if (!feedback || !master || !master->comp || !master->comp->renderer ||
      !master->main_drm || !master->main_drm->dev)
    return false;

  drmDevicePtr dev_main_drm = NULL;
  if (drmGetDevice(master->main_drm->dev->fd, &dev_main_drm) != 0 ||
      !dev_main_drm) {
    VT_ERROR(master->comp->log,
             "Cannot get DRM device pointer of device with fd=%i",
             master->main_drm->dev->fd);
    return false;
  }

  VT_TRACE(master->comp->log, "Building default DMABUF feedback...");

  enum { max_shm_formats = 256 };
  uint32_t n_shm_formats = 0;
  uint32_t shm_formats[max_shm_formats];

  uint32_t      n_devs = 0;
  drmDevicePtr *devs = calloc(master->n_drm, sizeof(*devs));
  if (master->n_drm > 0 && !devs) {
    drmFreeDevice(&dev_main_drm);
    return false;
  }

  struct drm_backend_state_t *drm;
  wl_list_for_each(drm, &master->backends, link) {
    struct vt_device_t *dev = drm->dev;
    if (!dev) {
      VT_TRACE(master->comp->log,
               "Skipping possible tranche device: No device associated.");
      continue;
    }

    VT_TRACE(master->comp->log,
             "Iterating possible DMABUF tranche device '%s' (FD: %i)...",
             dev->path, dev->fd);

    drmDevicePtr dev_drm = NULL;
    if (drmGetDevice(dev->fd, &dev_drm) != 0) {
      VT_WARN(
          master->comp->log,
          "Failed to retrieve DRM device pointer from internal DRM device '%s'",
          dev->path);
      if (dev_drm)
        drmFreeDevice(&dev_drm);
      continue;
    }

    devs[n_devs++] = dev_drm;

    if (!(dev_drm->available_nodes & (1 << DRM_NODE_RENDER))) {
      VT_TRACE(master->comp->log,
               "Skipping possible tranche device '%s': Has no available render "
               "nodes.",
               dev->path);
      continue;
    }

    if (!_drm_can_share_dmabuf(master->main_drm->dev, dev)) {
      VT_TRACE(master->comp->log,
               "Skipping possible tranche device '%s': Cannot share DMABUFs "
               "with main device '%s'.",
               dev->path, master->main_drm->dev->path);
      continue;
    }

    struct vt_dmabuf_tranche_t *main_tranche = vt_dmabuf_feedback_add_tranche(
        feedback, dev,
        _drm_devices_equal(dev_main_drm, dev_drm)
            ? VT_DMABUF_TRANCHE_FLAG_DIRECT_SCANOUT
            : VT_DMABUF_TRANCHE_FLAG_COMPOSITE);

    struct vt_renderer_t *r = master->comp->renderer;
    if (dev == master->main_drm->dev) {
      if (r->impl.query_dmabuf_formats_with_renderer) {
        if (!r->impl.query_dmabuf_formats_with_renderer(
                r, &drm->sampling_formats)) {
          VT_WARN(master->comp->log,
                  "Cannot query DMABUF formats for main device '%s' from EGL.",
                  dev->path);
          continue;
        }
      }
    } else {
      if (r->impl.query_dmabuf_formats) {
        if (!r->impl.query_dmabuf_formats(
                master->comp, drm->gbm_dev, &drm->sampling_formats)) {
          VT_WARN(
              master->comp->log,
              "Cannot query DMABUF formats for tranche device '%s' from EGL.",
              dev->path);
          continue;
        }
      }
    }

    if (!vt_drm_format_array_copy(&main_tranche->formats,
                                  &drm->sampling_formats)) {
      VT_ERROR(master->comp->log,
               "Failed to copy renderer formats into fallback tranche");
      return false;
    }

    struct vt_drm_format_t *fmt;
    wl_array_for_each(fmt, &main_tranche->formats) {
      if (!(n_shm_formats < max_shm_formats - 1)) {
        VT_WARN(master->comp->log,
                "Maximum number of SHM formats reached, not adding format %i.",
                fmt->format);
        break;
      }

      shm_formats[n_shm_formats++] = fmt->format;
    }

    _log_dmabuf_tranche(master->comp, main_tranche, dev->path);
  }

  struct vt_dmabuf_tranche_t* fallback_tranche = vt_dmabuf_feedback_add_tranche(feedback, feedback->dev_main, 0);
  wl_array_init(&fallback_tranche->formats);

  struct vt_drm_format_t fallback_fmt = {0};
  vt_drm_format_init(&fallback_fmt, DRM_FORMAT_XRGB8888);

  if (!vt_drm_format_add_mod(&fallback_fmt, DRM_FORMAT_MOD_LINEAR) ||
      !vt_drm_format_add_mod(&fallback_fmt, DRM_FORMAT_MOD_INVALID)) {
    vt_drm_format_fini(&fallback_fmt);
    goto fail;
  }

  if (!vt_drm_format_array_push(&fallback_tranche->formats, &fallback_fmt)) {
    vt_drm_format_fini(&fallback_fmt);
    goto fail;
  }

  vt_drm_format_fini(&fallback_fmt);

  drmFreeDevice(&dev_main_drm);

  for (uint32_t i = 0; i < n_devs; i++) {
    drmFreeDevice(&devs[i]);
  }
  free(devs);

  shm_formats[n_shm_formats++] = DRM_FORMAT_XRGB8888;

  if (!vt_proto_wl_shm_init(master->comp, shm_formats, n_shm_formats)) {
    VT_ERROR(master->comp->log, "Failed to initialize WL SHM protcol.\n");
    return false;
  }

  VT_TRACE(master->comp->log,
           "Added default fallback tranche (LINEAR DRM_FORMAT_XRGB8888).\n");

  return true;

fail:
  drmFreeDevice(&dev_main_drm);
  for (uint32_t i = 0; i < n_devs; i++)
    drmFreeDevice(&devs[i]);
  free(devs);
  return false;
}

static bool _drm_suspend(struct drm_backend_state_t *backend) {
  if (!backend || !backend->comp || backend->drm_fd < 0 || !backend->impl)
    return false;

  VT_TRACE(backend->comp->log, "Suspending seat session (VT switch away)...");

  struct vt_output_t *output;
  wl_list_for_each(output, &backend->outputs, link_local) {
    output->needs_repaint = false;
  }

  bool any_inflight;
  do {
    any_inflight = false;
    wl_list_for_each(output, &backend->outputs, link_local) {
      if (!output->user_data)
        continue;

      struct drm_output_state_t *drm_output =
          BACKEND_DATA(output, struct drm_output_state_t);
      if (drm_output->flip_inflight) {
        any_inflight = true;
        break;
      }
    }

    if (any_inflight && drmHandleEvent(backend->drm_fd, &backend->evctx) != 0) {
      VT_ERROR(backend->comp->log, "Failed to drain DRM events: %s",
               strerror(errno));
      return false;
    }
  } while (any_inflight);

  wl_list_for_each(output, &backend->outputs, link_local) {
    if (!output->user_data)
      continue;

    struct drm_output_state_t *drm_output =
        BACKEND_DATA(output, struct drm_output_state_t);

    if (!backend->impl->disable(backend, drm_output)) {
      VT_WARN(backend->comp->log,
              "Failed to disable connector %u while suspending.",
              drm_output->conn_id);
    }

    _drm_release_all_scanout(output);
    drm_output->needs_modeset = true;
    drm_output->flip_inflight = false;
  }

  return true;
}


static bool _drm_resume(struct drm_backend_state_t *backend) {
  if (!backend || !backend->comp || backend->drm_fd < 0)
    return false;

  VT_TRACE(backend->comp->log, "Resuming seat session (VT switch back)...");

  if (!backend->event_source) {
    backend->event_source =
        wl_event_loop_add_fd(backend->comp->wl.evloop, backend->drm_fd,
                             WL_EVENT_READABLE, _drm_dispatch, backend);
    if (!backend->event_source) {
      VT_ERROR(backend->comp->log, "Failed to restore DRM event source.");
      return false;
    }
  }

  struct vt_output_t *output;
  wl_list_for_each(output, &backend->outputs, link_local) {
    if (!output->user_data)
      continue;

    struct drm_output_state_t *drm_output =
        BACKEND_DATA(output, struct drm_output_state_t);

    drm_output->needs_modeset = true;
    drm_output->flip_inflight = false;

    pixman_region32_clear(&output->damage);
    pixman_region32_union_rect(&output->damage, &output->damage, 0, 0,
                               output->width, output->height);
    output->needs_repaint = true;
    vt_comp_schedule_repaint(backend->comp, output);
  }

  uint32_t t = vt_util_get_time_msec();
  vt_comp_frame_done_all(backend->comp, t);
  wl_display_flush_clients(backend->comp->wl.dsp);

  return true;
}


static int _drm_dispatch(int fd, uint32_t mask, void *data) {
  if (!data || fd < 0)
    return 0;
  struct drm_backend_state_t *drm = (struct drm_backend_state_t *)data;
  if (mask & (WL_EVENT_ERROR | WL_EVENT_HANGUP)) {
    VT_ERROR(drm->comp->log, "DRM event source failed.");
    return -1;
  }
  if ((mask & WL_EVENT_READABLE) && drmHandleEvent(fd, &drm->evctx) != 0) {
    VT_ERROR(drm->comp->log, "Failed to handle DRM event: %s", strerror(errno));
    return -1;
  }
  return 0;
}

static bool _drm_init_for_device(struct vt_compositor_t     *comp,
                                 struct drm_backend_state_t *drm,
                                 struct vt_device_t         *dev) {
  if (!comp || !drm || !dev || dev->fd < 0 || !drm->backend)
    return false;

  drm->drm_fd = -1;
  drm->res = NULL;
  drm->gbm_dev = NULL;
  drm->native_handle = NULL;
  drm->event_source = NULL;
  drm->impl = NULL;

  wl_list_init(&drm->outputs);
  wl_array_init(&drm->crtcs);
  wl_array_init(&drm->sampling_formats);
  wl_array_init(&drm->planes);

  drm->drm_fd = dev->fd;
  drm->dev = dev;
  drm->comp = comp;

  VT_TRACE(comp->log, "Initializing DRM/KMS backend...");

  if (!(drm->gbm_dev = gbm_create_device(drm->drm_fd))) {
    VT_ERROR(comp->log, "cannot create GBM device (fd: %i)", drm->drm_fd);
    _drm_terminate_for_device(drm);
    return false;
  }

  VT_TRACE(comp->log, "Successfully created GBM device on FD: %i", drm->drm_fd);

  drm->evctx.version = DRM_EVENT_CONTEXT_VERSION;
  drm->evctx.page_flip_handler = _drm_page_flip_handler;
  drm->evctx.vblank_handler = NULL;

  drm->event_source = wl_event_loop_add_fd(
      comp->wl.evloop, drm->drm_fd, WL_EVENT_READABLE, _drm_dispatch, drm);
  if (!drm->event_source) {
    VT_ERROR(comp->log, "Failed to add DRM event source for FD %i.",
             drm->drm_fd);
    _drm_terminate_for_device(drm);
    return false;
  }

  drm->native_handle = drm->gbm_dev;

  struct drm_backend_master_state_t *drm_master =
      BACKEND_DATA(drm->backend, struct drm_backend_master_state_t);

  if (!drm_master || !comp->renderer || !comp->renderer->impl.init ||
      !comp->renderer->impl.is_handle_renderable) {
    VT_ERROR(comp->log, "Must allocate renderer before initializing backend.");
    _drm_terminate_for_device(drm);
    return false;
  }

  if (!_drm_verify_caps(drm)) {
    _drm_terminate_for_device(drm);
    return false;
  }

  drm->impl = drm->caps[VT_DRM_CAP_ATOMIC_MODESET]
                  ? &drm_kms_atomic_impl
                  : &drm_kms_legacy_impl;
  _drm_update_tearing_cap(drm);

  /* Renderer is only initialized on the main DRM device */
  if (!drm_master->main_drm &&
      comp->renderer->impl.is_handle_renderable(comp->renderer,
                                                drm->native_handle)) {
    comp->renderer->impl.init(comp->backend, comp->renderer,
                              drm->native_handle);
    drm_master->main_drm = drm;

    VT_TRACE(comp->log, "Chose DRM main device: GPU %s (FD: %i).", dev->path,
             drm->drm_fd);
  }

  VT_TRACE(comp->log, "Using %s KMS implementation for GPU %s.",
           drm->impl->name, dev->path);

  return true;
}


static bool
_drm_plane_init_cursor_sizes(struct drm_plane_t               *plane,
                             const struct drm_plane_size_hint *hints,
                             size_t                            hints_len) {
  assert(plane && hints && hints_len > 0);

  plane->cursor_sizes = calloc(hints_len, sizeof(plane->cursor_sizes[0]));
  if (plane->cursor_sizes == NULL) {
    return false;
  }
  plane->n_cursor_sizes = hints_len;

  for (size_t i = 0; i < hints_len; i++) {
    const struct drm_plane_size_hint hint = hints[i];
    plane->cursor_sizes[i] = (struct vt_output_cursor_size_t){
        .width = hint.width,
        .height = hint.height,
    };
  }

  return true;
}

static const char *_drm_plane_type_name(uint64_t type) {
  switch (type) {
  case DRM_PLANE_TYPE_PRIMARY:
    return "primary";
  case DRM_PLANE_TYPE_CURSOR:
    return "cursor";
  case DRM_PLANE_TYPE_OVERLAY:
    return "overlay";
  default:
    return "unknown";
  }
}

static bool _drm_plane_init(struct drm_backend_state_t *drm,
                            struct drm_plane_t *plane, drmModePlane *drm_plane,
                            struct wl_array *crtcs, uint32_t plane_id) {
  assert(drm && drm->comp && drm->drm_fd >= 0);
  (void)crtcs;

  if (!plane || !drm_plane)
    return false;

  int                     drm_fd = drm->drm_fd;
  struct vt_compositor_t *comp = drm->comp;

  if (!drm_kms_props_get_plane(drm_fd, plane_id, plane->props)) {
    VT_ERROR(comp->log, "Failed to get properties of plane with ID %" PRIu32,
             plane_id);
    return false;
  }

  uint64_t plane_type;
  if (!drm_kms_props_get_prop(drm_fd, plane_id, plane->props[VT_DRM_PLANE_TYPE],
                              &plane_type)) {
    VT_ERROR(comp->log,
             "Failed to get 'type' property of plane with ID %" PRIu32,
             plane_id);
    return false;
  }

  plane->id = plane_id;
  plane->id_crtc_init = drm_plane->crtc_id;
  plane->possible_crtcs = drm_plane->possible_crtcs;
  plane->type = plane_type;

  bool in_formats_supported = plane->props[VT_DRM_PLANE_IN_FORMATS] != 0 &&
                              drm->caps[VT_DRM_CAP_ADDFB2_MODIFIERS];

  VT_TRACE(comp->log,
           "DRM: discovering plane %" PRIu32 ": type=%s crtc=%" PRIu32
           " possible_crtcs=0x%" PRIx32 " legacy_formats=%u in_formats=%s.",
           plane_id, _drm_plane_type_name(plane_type), drm_plane->crtc_id,
           drm_plane->possible_crtcs, drm_plane->count_formats,
           in_formats_supported ? "yes" : "no");

  for (size_t i = 0; i < drm_plane->count_formats; ++i) {
    uint32_t format = drm_plane->formats[i];

    const char *format_name = drmGetFormatName(format);
    if (!in_formats_supported && plane_type == DRM_PLANE_TYPE_CURSOR) {

      VT_TRACE(comp->log,
               "DRM: plane %" PRIu32 " supports legacy format=%s" PRIx32
               " modifier=LINEAR.",
               plane_id, format_name);

      if (!vt_drm_format_array_push_pair(&plane->formats, format,
                                         DRM_FORMAT_MOD_LINEAR)) {
        return false;
      }
    }

    if (plane_type != DRM_PLANE_TYPE_CURSOR) {
      VT_TRACE(comp->log,
               "DRM: plane %" PRIu32 " supports legacy format=%s" PRIx32
               " modifier=INVALID.",
               plane_id, format_name);

      if (!vt_drm_format_array_push_pair(&plane->formats, format,
                                         DRM_FORMAT_MOD_INVALID)) {
        return false;
      }
    }
  }

  if (in_formats_supported) {
    uint64_t in_formats_id;

    if (!drm_kms_props_get_prop(drm_fd, plane->id,
                                plane->props[VT_DRM_PLANE_IN_FORMATS],
                                &in_formats_id) ||
        !in_formats_id) {
      VT_ERROR(comp->log,
               "Failed to get 'IN_FORMATS' property of plane with ID %" PRIu32,
               plane_id);
      return false;
    }

    VT_TRACE(comp->log,
             "DRM: plane %" PRIu32 " has IN_FORMATS blob id=%" PRIu64 ".",
             plane_id, in_formats_id);

    drmModePropertyBlobRes *blob =
        drmModeGetPropertyBlob(drm_fd, in_formats_id);

    if (!blob) {
      VT_ERROR(comp->log,
               "Failed to read 'IN_FORMATS' blob of plane with ID %" PRIu32,
               plane_id);
      return false;
    }

    size_t modifier_count = 0;

    drmModeFormatModifierIterator iter = {0};
    while (drmModeFormatModifierBlobIterNext(blob, &iter)) {
      const char *format_name = drmGetFormatName(iter.fmt);
      char        mod_str[256];
      _modifier_to_str(iter.mod, mod_str, sizeof(mod_str));
      VT_TRACE(comp->log,
               "DRM: plane %" PRIu32 " supports format=%s" PRIx32
               " modifier=%s.",
               plane_id, format_name, mod_str);

      if (!vt_drm_format_array_push_pair(&plane->formats, iter.fmt, iter.mod)) {
        VT_ERROR(comp->log,
                 "Failed to add DRM format %s"
                 " with modifier 0x%016" PRIx64 " to plane %" PRIu32
                 " format list.",
                 format_name, iter.mod, plane_id);

        drmModeFreePropertyBlob(blob);
        return false;
      }

      modifier_count++;
    }

    VT_TRACE(comp->log,
             "DRM: plane %" PRIu32
             " IN_FORMATS parsed: %zu format/modifier pairs.",
             plane_id, modifier_count);

    drmModeFreePropertyBlob(blob);
  }

  uint64_t size_hints_id = 0;
  if (plane->props[VT_DRM_PLANE_SIZE_HINTS] != 0 &&
      !drm_kms_props_get_prop(drm_fd, plane->id,
                              plane->props[VT_DRM_PLANE_SIZE_HINTS],
                              &size_hints_id)) {
    VT_ERROR(comp->log,
             "Failed to get 'SIZE_HINTS' property of plane with ID %" PRIu32,
             plane_id);
    return false;
  }

  if (size_hints_id != 0) {
    drmModePropertyBlobRes *blob =
        drmModeGetPropertyBlob(drm_fd, size_hints_id);
    if (!blob) {
      VT_ERROR(comp->log,
               "Failed to read 'SIZE_HINTS' blob of plane with ID %" PRIu32,
               plane_id);
      return false;
    }

    const struct drm_plane_size_hint *size_hints = blob->data;
    size_t size_hints_len = blob->length / sizeof(size_hints[0]);
    if (size_hints_len == 0 ||
        !_drm_plane_init_cursor_sizes(plane, size_hints, size_hints_len)) {
      drmModeFreePropertyBlob(blob);
      return false;
    }

    drmModeFreePropertyBlob(blob);
  } else if (plane->type == DRM_PLANE_TYPE_CURSOR) {
    const struct drm_plane_size_hint size_hint = {
        .width = drm->cursor_w,
        .height = drm->cursor_h,
    };
    if (!_drm_plane_init_cursor_sizes(plane, &size_hint, 1))
      return false;
  }

  return true;
}

static bool _drm_planes_init(struct drm_backend_state_t *drm) {
  assert(drm && drm->comp && drm->drm_fd >= 0);

  drmModePlaneRes *plane_res = drmModeGetPlaneResources(drm->drm_fd);

  if (!plane_res) {
    int err = errno;
    VT_ERROR(drm->comp->log, "drmModeGetPlaneResources() failed: %s (%d)",
             strerror(err), err);
    return false;
  }

  size_t plane_bytes = plane_res->count_planes * sizeof(struct drm_plane_t);

  struct drm_plane_t *planes = wl_array_add(&drm->planes, plane_bytes);

  if (!planes) {
    VT_ERROR(drm->comp->log, "Out of memory");
    goto fail_planes;
  }

  assert(drm->planes.size / sizeof(*planes) == plane_res->count_planes);

  memset(planes, 0, plane_bytes);

  for (uint32_t i = 0; i < plane_res->count_planes; i++) {
    struct drm_plane_t *plane = &planes[i];
    uint32_t            plane_id = plane_res->planes[i];
    drmModePlane       *drm_plane = drmModeGetPlane(drm->drm_fd, plane_id);

    if (!drm_plane) {
      int err = errno;
      VT_ERROR(drm->comp->log,
               "drmModeGetPlane() for plane ID %" PRIu32 " failed: %s (%d)",
               plane_id, strerror(err), err);
      goto fail_planes;
    }

    if (!_drm_plane_init(drm, plane, drm_plane, &drm->crtcs, plane_id)) {
      goto fail_this_plane;
    }

    drmModeFreePlane(drm_plane);

    continue;
  fail_this_plane:
    drmModeFreePlane(drm_plane);
    goto fail_planes;
  }

  drmModeFreePlaneResources(plane_res);
  return true;

fail_planes:
  drmModeFreePlaneResources(plane_res);
  return false;
}

static bool _drm_init_crtc(struct vt_compositor_t *comp,
                           struct drm_crtc_t *crtc, int drm_fd, uint32_t id,
                           uint32_t index) {
  assert(comp);

  if (!crtc)
    return false;

  crtc->id = id;
  crtc->index = index;

  if (!drm_kms_props_get_crtc(drm_fd, id, crtc->props)) {
    memset(crtc->props, 0, sizeof(crtc->props));
    VT_WARN(comp->log,
            "Failed to populate DRM properties of CRTC with ID: %" PRIu32,
            id);
  }

  return true;
}


static bool
_drm_init_crtcs_and_planes(struct drm_backend_state_t *drm) {
  if (!drm || !drm->comp)
    return false;

  struct vt_compositor_t *comp = drm->comp;

  VT_TRACE(comp->log, "Initializing CRTCs");

  drmModeRes* res = drmModeGetResources(drm->drm_fd);

  if (!res) {
    int err = errno;
    VT_ERROR(comp->log, "drmModeGetResources() failed: %s (%d)", strerror(err),
             err);
    return false;
  }

	if (res->count_crtcs == 0) {
    VT_WARN(comp->log, "No CRTCs are available"); 
		drmModeFreeResources(res);
		return true;
	}

  size_t crtc_bytes = res->count_crtcs * sizeof(struct drm_crtc_t);

  struct drm_crtc_t *crtcs =
      wl_array_add(&drm->crtcs, crtc_bytes);

  if (!crtcs) {
    VT_ERROR(comp->log, "Out of memory");
    goto fail_resources;
  }

  assert(drm->crtcs.size / sizeof(*crtcs) == res->count_crtcs);

  memset(crtcs, 0, crtc_bytes);

  for (int i = 0; i < res->count_crtcs; ++i) {
    uint32_t crtc_id = res->crtcs[i];

    if (!_drm_init_crtc(comp, &crtcs[i], drm->drm_fd, crtc_id, i)) {
      VT_ERROR(comp->log, "Failed to initialize CRTC with ID: %" PRIu32 "\n",
               crtc_id);
      goto fail_crtc;
    }
  }

  if (!_drm_planes_init(drm)) {
    struct drm_plane_t *plane;
    wl_array_for_each(plane, &drm->planes) {
      vt_drm_format_array_free(&plane->formats);
      free(plane->cursor_sizes);
    }
    wl_array_release(&drm->planes);
    wl_array_init(&drm->planes);

    if (drm->impl && drm->impl->atomic) {
      VT_WARN(comp->log,
              "Failed to initialize DRM plane metadata, using legacy KMS.");
      drm->impl = &drm_kms_legacy_impl;
      _drm_update_tearing_cap(drm);
    }
  } else {
    _drm_assign_crtc_planes(drm);
  }

  drmModeFreeResources(res);
  return true;

fail_crtc:
  wl_array_release(&drm->crtcs);
  wl_array_init(&drm->crtcs);

fail_resources:
  drmModeFreeResources(res);
  return false;
}

static struct vt_output_mode_t *
_drm_create_output_mode(struct wl_list *list, drmModeModeInfo *mode_info) {
  if (!list || !mode_info)
    return NULL;

  struct vt_output_mode_t *mode = calloc(1, sizeof(*mode));
  if (!mode)
    return NULL;

  mode->width = mode_info->hdisplay;
  mode->height = mode_info->vdisplay;

  mode->refresh = mode_info->vrefresh * 1000;

  mode->flags = 0;

  if (mode_info->type & DRM_MODE_TYPE_PREFERRED)
    mode->flags |= WL_OUTPUT_MODE_PREFERRED;

  switch (mode_info->flags & DRM_MODE_FLAG_PIC_AR_MASK) {
  case DRM_MODE_FLAG_PIC_AR_4_3:
    mode->aspect_ratio = VT_MODE_PIC_AR_4_3;
    break;

  case DRM_MODE_FLAG_PIC_AR_16_9:
    mode->aspect_ratio = VT_MODE_PIC_AR_16_9;
    break;

  case DRM_MODE_FLAG_PIC_AR_64_27:
    mode->aspect_ratio = VT_MODE_PIC_AR_64_27;
    break;

  case DRM_MODE_FLAG_PIC_AR_256_135:
    mode->aspect_ratio = VT_MODE_PIC_AR_256_135;
    break;

  case DRM_MODE_FLAG_PIC_AR_NONE:
  default:
    mode->aspect_ratio = VT_MODE_PIC_AR_NONE;
    break;
  }

  wl_list_insert(list, &mode->link);

  return mode;
}

static uint32_t _drm_subpixel_to_wl(drmModeSubPixel subpixel) {
  switch (subpixel) {
  case DRM_MODE_SUBPIXEL_HORIZONTAL_RGB:
    return WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB;
  case DRM_MODE_SUBPIXEL_HORIZONTAL_BGR:
    return WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR;
  case DRM_MODE_SUBPIXEL_VERTICAL_RGB:
    return WL_OUTPUT_SUBPIXEL_VERTICAL_RGB;
  case DRM_MODE_SUBPIXEL_VERTICAL_BGR:
    return WL_OUTPUT_SUBPIXEL_VERTICAL_BGR;
  case DRM_MODE_SUBPIXEL_NONE:
    return WL_OUTPUT_SUBPIXEL_NONE;
  case DRM_MODE_SUBPIXEL_UNKNOWN:
  default:
    return WL_OUTPUT_SUBPIXEL_UNKNOWN;
  }
}

static const char *_drm_connector_type_name(uint32_t type) {
  switch (type) {
  case DRM_MODE_CONNECTOR_VGA:
    return "VGA";
  case DRM_MODE_CONNECTOR_DVII:
    return "DVI-I";
  case DRM_MODE_CONNECTOR_DVID:
    return "DVI-D";
  case DRM_MODE_CONNECTOR_DVIA:
    return "DVI-A";
  case DRM_MODE_CONNECTOR_Composite:
    return "Composite";
  case DRM_MODE_CONNECTOR_SVIDEO:
    return "SVIDEO";
  case DRM_MODE_CONNECTOR_LVDS:
    return "LVDS";
  case DRM_MODE_CONNECTOR_Component:
    return "Component";
  case DRM_MODE_CONNECTOR_9PinDIN:
    return "DIN";
  case DRM_MODE_CONNECTOR_DisplayPort:
    return "DP";
  case DRM_MODE_CONNECTOR_HDMIA:
    return "HDMI-A";
  case DRM_MODE_CONNECTOR_HDMIB:
    return "HDMI-B";
  case DRM_MODE_CONNECTOR_TV:
    return "TV";
  case DRM_MODE_CONNECTOR_eDP:
    return "eDP";
  case DRM_MODE_CONNECTOR_VIRTUAL:
    return "Virtual";
  case DRM_MODE_CONNECTOR_DSI:
    return "DSI";
#ifdef DRM_MODE_CONNECTOR_DPI
  case DRM_MODE_CONNECTOR_DPI:
    return "DPI";
#endif
  default:
    return "Unknown";
  }
}

static bool _drm_plane_is_assigned(struct drm_backend_state_t *drm,
                                   struct drm_plane_t *plane) {
  struct drm_crtc_t *crtc;
  wl_array_for_each(crtc, &drm->crtcs) {
    if (crtc->plane_primary == plane || crtc->plane_cursor == plane)
      return true;
  }

  return false;
}

static struct drm_plane_t *_drm_pick_plane(struct drm_backend_state_t *drm,
                                           struct drm_crtc_t *crtc,
                                           uint32_t type) {
  struct drm_plane_t *fallback = NULL;
  struct drm_plane_t *plane;
  wl_array_for_each(plane, &drm->planes) {
    if (plane->type != type ||
        !(plane->possible_crtcs & (1u << crtc->index)) ||
        _drm_plane_is_assigned(drm, plane)) {
      continue;
    }

    if (plane->id_crtc_init == crtc->id)
      return plane;

    if (!fallback)
      fallback = plane;
  }

  return fallback;
}

static void _drm_assign_crtc_planes(struct drm_backend_state_t *drm) {
  struct drm_crtc_t *crtc;
  wl_array_for_each(crtc, &drm->crtcs) {
    crtc->plane_primary = _drm_pick_plane(drm, crtc, DRM_PLANE_TYPE_PRIMARY);
    crtc->plane_cursor = _drm_pick_plane(drm, crtc, DRM_PLANE_TYPE_CURSOR);
  }
}

static struct drm_crtc_t *_drm_find_crtc(struct drm_backend_state_t *drm,
                                         uint32_t id) {
  struct drm_crtc_t *crtc;
  wl_array_for_each(crtc, &drm->crtcs) {
    if (crtc->id == id)
      return crtc;
  }

  return NULL;
}

static struct drm_crtc_t *_drm_pick_crtc(struct drm_backend_state_t *drm,
                                         drmModeConnector *conn) {
  if (!drm || !conn)
    return NULL;

  if (conn->encoder_id) {
    drmModeEncoder *encoder = drmModeGetEncoder(drm->drm_fd, conn->encoder_id);
    if (encoder) {
      struct drm_crtc_t *crtc = _drm_find_crtc(drm, encoder->crtc_id);
      if (crtc && !crtc->in_use &&
          (encoder->possible_crtcs & (1u << crtc->index))) {
        drmModeFreeEncoder(encoder);
        return crtc;
      }
      drmModeFreeEncoder(encoder);
    }
  }

  for (int i = 0; i < conn->count_encoders; i++) {
    drmModeEncoder *encoder = drmModeGetEncoder(drm->drm_fd, conn->encoders[i]);
    if (!encoder)
      continue;

    struct drm_crtc_t *crtc;
    wl_array_for_each(crtc, &drm->crtcs) {
      if (!crtc->in_use &&
          (encoder->possible_crtcs & (1u << crtc->index))) {
        drmModeFreeEncoder(encoder);
        return crtc;
      }
    }

    drmModeFreeEncoder(encoder);
  }

  return NULL;
}

static bool _drm_atomic_output_supported(struct drm_output_state_t *output) {
  if (!output || !output->crtc || !output->crtc->plane_primary)
    return false;

  struct drm_crtc_t *crtc = output->crtc;
  struct drm_plane_t *plane = crtc->plane_primary;

  return output->conn_props[VT_DRM_CONNECTOR_CRTC_ID] != 0 &&
         crtc->props[VT_DRM_CRTC_ACTIVE] != 0 &&
         crtc->props[VT_DRM_CRTC_MODE_ID] != 0 &&
         plane->props[VT_DRM_PLANE_FB_ID] != 0 &&
         plane->props[VT_DRM_PLANE_CRTC_ID] != 0 &&
         plane->props[VT_DRM_PLANE_CRTC_X] != 0 &&
         plane->props[VT_DRM_PLANE_CRTC_Y] != 0 &&
         plane->props[VT_DRM_PLANE_CRTC_W] != 0 &&
         plane->props[VT_DRM_PLANE_CRTC_H] != 0 &&
         plane->props[VT_DRM_PLANE_SRC_X] != 0 &&
         plane->props[VT_DRM_PLANE_SRC_Y] != 0 &&
         plane->props[VT_DRM_PLANE_SRC_W] != 0 &&
         plane->props[VT_DRM_PLANE_SRC_H] != 0;
}

static bool _drm_create_output_for_device(struct drm_backend_state_t *drm,
                                          struct vt_output_t         *output,
                                          void                       *data) {
  if (!drm || !drm->comp || !drm->res || !output || !data)
    return false;

  struct vt_compositor_t *comp = drm->comp;
  drmModeConnector       *conn = data;

  VT_TRACE(comp->log, "Creating DRM internal output.");

  if (!(output->user_data =
            VT_ALLOC(drm->comp, sizeof(struct drm_output_state_t)))) {
    return false;
  }
  memset(output->user_data, 0, sizeof(struct drm_output_state_t));

  struct drm_output_state_t *drm_output =
      BACKEND_DATA(output, struct drm_output_state_t);
  wl_array_init(&drm_output->current_layers);
  wl_array_init(&drm_output->pending_layers);
  wl_array_init(&drm_output->layer_plan);
  drm_output->pending_out_fence_fd = -1;

  wl_list_init(&output->physical.modes);
  wl_list_init(&output->proto.resources);
  wl_list_init(&output->presented_surfaces);

  struct vt_output_mode_t *fallback = NULL;
  struct vt_output_mode_t *selected = NULL;
  drmModeModeInfo         *selected_drm = NULL;

  for (int j = 0; j < conn->count_modes; j++) {
    drmModeModeInfo *drm_mode = &conn->modes[j];
    struct vt_output_mode_t *mode =
        _drm_create_output_mode(&output->physical.modes, drm_mode);

    if (!mode)
      return false;

    if (!fallback)
      fallback = mode;

    if (!selected && (drm_mode->type & DRM_MODE_TYPE_PREFERRED)) {
      selected = mode;
      selected_drm = drm_mode;
    }
  }

  if (!fallback) {
    VT_ERROR(comp->log, "Connector %u has no modes", conn->connector_id);
    return false;
  }

  if (!selected) {
    selected = fallback;
    selected_drm = &conn->modes[0];
  }

  selected->flags |= WL_OUTPUT_MODE_CURRENT;

  drm_output->base = output;
  drm_output->drm_backend = drm;
  drm_output->conn_id = conn->connector_id;
  drm_output->mode = *selected_drm;
  drm_output->needs_modeset = true;
  drm_output->connector_seen = true;

  drm_output->crtc = _drm_pick_crtc(drm, conn);
  if (!drm_output->crtc) {
    VT_ERROR(comp->log, "Failed to find CRTC for connector %u",
             drm_output->conn_id);
    return false;
  }
  drm_output->crtc->in_use = true;

  if (drm->impl->atomic) {
    if (!drm_kms_props_get_connector(drm->drm_fd, drm_output->conn_id,
                                     drm_output->conn_props) ||
        !_drm_atomic_output_supported(drm_output)) {
      VT_WARN(comp->log,
              "Atomic properties are incomplete for connector %u, using legacy KMS.",
              drm_output->conn_id);
      drm->impl = &drm_kms_legacy_impl;
      _drm_update_tearing_cap(drm);
    }
  }

  struct drm_backend_master_state_t *drm_master =
      BACKEND_DATA(output->backend, struct drm_backend_master_state_t);
  if (!drm_master || !drm_master->main_drm || !drm_master->main_drm->gbm_dev)
    return false;

  uint32_t usage = GBM_BO_USE_RENDERING;
  if (drm_master->main_drm == drm)
    usage |= GBM_BO_USE_SCANOUT;
  else
    usage |= GBM_BO_USE_LINEAR;

  const uint32_t desired_format = comp->renderer->_desired_render_buffer_format;

  /* We create all GBM surfaces with the main DRM device, we do not support
   * setups where multiple GPUs render to separate outputs */
  drm_output->gbm_surf = gbm_surface_create(
      drm_master->main_drm->gbm_dev, drm_output->mode.hdisplay,
      drm_output->mode.vdisplay, desired_format, usage);

  if (!drm_output->gbm_surf) {
    VT_ERROR(comp->log,
             "cannot create GBM surface (%ux%u@%u) for output on connector %i "
             "for rendering.",
             drm_output->mode.hdisplay, drm_output->mode.vdisplay,
             drm_output->mode.vrefresh, drm_output->conn_id);
    return false;
  }

  VT_TRACE(comp->log,
           "Acknowledged connector: %u, CRTC %u, mode %ux%u@%u (%p)",
           drm_output->conn_id, drm_output->crtc->id,
           drm_output->mode.hdisplay, drm_output->mode.vdisplay,
           drm_output->mode.vrefresh, (void *)output);

  output->needs_repaint = true;
  output->width = (uint32_t)selected->width;
  output->height = (uint32_t)selected->height;
  output->refresh_rate = selected->refresh;
  output->x = drm_master->x_ptr;
  output->y = 0;
  output->native_window = drm_output->gbm_surf;
  output->format = desired_format;
  output->id = drm_output->conn_id;

  output->physical.mm_width = conn->mmWidth;
  output->physical.mm_height = conn->mmHeight;
  output->physical.subpixel = _drm_subpixel_to_wl(conn->subpixel);
  output->physical.transform = WL_OUTPUT_TRANSFORM_NORMAL;
  output->current_scale = 1;

  output->physical.make = strdup("Unknown");
  output->physical.model = strdup("Unknown");
  output->physical.serial_number = NULL;

  const char *type_name = _drm_connector_type_name(conn->connector_type);
  char        name[64];
  snprintf(name, sizeof(name), "%s-%u", type_name, conn->connector_type_id);
  output->physical.name = strdup(name);

  if (!output->physical.make || !output->physical.model ||
      !output->physical.name) {
    VT_ERROR(comp->log, "Failed to allocate DRM output metadata.");
    return false;
  }

  wl_list_insert(&drm->outputs, &output->link_local);
  wl_list_insert(&drm->comp->outputs, &output->link_global);

  if (!vt_proto_wl_output_init(output)) {
    VT_ERROR(comp->log, "Failed to create wl_output global for output.");
    return false;
  }

  /* TODO: Monitor position system */
  drm_master->x_ptr += output->width;

  return true;
}


static bool _drm_resources_init(struct drm_backend_state_t *drm) {
  if (!drm || !drm->comp || drm->drm_fd < 0)
    return false;

  struct vt_compositor_t *comp = drm->comp;

  drm->res = drmModeGetResources(drm->drm_fd);
  if (!drm->res) {
    VT_ERROR(comp->log, "Failed to get DRM resources: %s", strerror(errno));
    return false;
  }

  if (!_drm_init_crtcs_and_planes(drm)) {
    if (drm->impl->atomic) {
      VT_WARN(comp->log,
              "Failed to initialize atomic plane metadata, using legacy KMS.");
      drm->impl = &drm_kms_legacy_impl;
      _drm_update_tearing_cap(drm);
    }

    struct drm_plane_t *plane;
    wl_array_for_each(plane, &drm->planes) {
      vt_drm_format_array_free(&plane->formats);
      free(plane->cursor_sizes);
    }
    wl_array_release(&drm->planes);
    wl_array_init(&drm->planes);

    if (drm->crtcs.size == 0)
      return false;
  }

  return _drm_scan_connectors(drm);
}


static struct vt_output_t *_drm_find_output(struct drm_backend_state_t *drm,
                                            uint32_t conn_id) {
  struct vt_output_t *output;
  wl_list_for_each(output, &drm->outputs, link_local) {
    if (!output->user_data)
      continue;

    struct drm_output_state_t *drm_output =
        BACKEND_DATA(output, struct drm_output_state_t);
    if (drm_output->conn_id == conn_id)
      return output;
  }

  return NULL;
}

static bool _drm_scan_connectors(struct drm_backend_state_t *drm) {
  if (!drm || !drm->comp)
    return false;

  drmModeRes *res = drmModeGetResources(drm->drm_fd);
  if (!res) {
    VT_ERROR(drm->comp->log, "Failed to get DRM resources: %s",
             strerror(errno));
    return false;
  }

  if (drm->res)
    drmModeFreeResources(drm->res);
  drm->res = res;

  struct vt_output_t *output;
  wl_list_for_each(output, &drm->outputs, link_local) {
    if (!output->user_data)
      continue;
    BACKEND_DATA(output, struct drm_output_state_t)->connector_seen = false;
  }

  for (int i = 0; i < drm->res->count_connectors; i++) {
    uint32_t connector_id = drm->res->connectors[i];
    drmModeConnector *conn = drmModeGetConnector(drm->drm_fd, connector_id);
    if (!conn)
      continue;

    if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
      drmModeFreeConnector(conn);
      continue;
    }

    output = _drm_find_output(drm, connector_id);
    if (output) {
      BACKEND_DATA(output, struct drm_output_state_t)->connector_seen = true;
      drmModeFreeConnector(conn);
      continue;
    }

    output = VT_ALLOC(drm->comp, sizeof(*output));
    if (!output) {
      drmModeFreeConnector(conn);
      continue;
    }

    memset(output, 0, sizeof(*output));
    output->backend = drm->backend;
    pixman_region32_init(&output->damage);

    if (!_drm_create_output_for_device(drm, output, conn)) {
      VT_ERROR(drm->comp->log, "Failed to initialize connector %u.",
               connector_id);
      drmModeFreeConnector(conn);
      continue;
    }

    struct drm_output_state_t *drm_output =
        BACKEND_DATA(output, struct drm_output_state_t);

    if (!drm->comp->renderer->impl.setup_renderable_output ||
        !drm->comp->renderer->impl.setup_renderable_output(
            drm->comp->renderer, output)) {
      VT_ERROR(drm->comp->log, "Failed to setup renderable DRM output %p.",
               (void *)output);
      drmModeFreeConnector(conn);
      _drm_destroy_output_for_device(drm, output);
      continue;
    }

    drm_output->renderable_setup = true;
    drmModeFreeConnector(conn);
  }

  struct vt_output_t *tmp;
  wl_list_for_each_safe(output, tmp, &drm->outputs, link_local) {
    if (!output->user_data)
      continue;

    struct drm_output_state_t *drm_output =
        BACKEND_DATA(output, struct drm_output_state_t);
    if (drm_output->connector_seen)
      continue;

    output->needs_repaint = false;
    if (drm_output->flip_inflight)
      continue;

    _drm_destroy_output_for_device(drm, output);
  }

  return true;
}

static void _drm_on_drm_change(struct wl_listener *listener, void *data) {
  if (!listener || !data)
    return;

  struct drm_backend_master_state_t *master =
      wl_container_of(listener, master, drm_change_listener);
  struct vt_session_drm_event_t *event = data;

  struct drm_backend_state_t *drm;
  wl_list_for_each(drm, &master->backends, link) {
    if (!drm->dev || !drm->dev->path || !event->device_node_name)
      continue;

    const char *node = strrchr(drm->dev->path, '/');
    node = node ? node + 1 : drm->dev->path;
    if (strcmp(node, event->device_node_name) != 0)
      continue;

    VT_TRACE(master->comp->log, "Rescanning DRM connectors on %s.",
             drm->dev->path);
    _drm_scan_connectors(drm);
    break;
  }
}

static bool _drm_destroy_output_for_device(struct drm_backend_state_t *drm,
                                           struct vt_output_t         *output) {
  if (!drm || !drm->comp)
    return false;
  if (!output || !output->user_data)
    return false;

  VT_TRACE(drm->comp->log, "Destroying output %p.\n", (void *)output);

  struct drm_output_state_t *drm_output =
      BACKEND_DATA(output, struct drm_output_state_t);

  if (!drm->comp->suspended && drm->impl && drm_output->crtc)
    drm->impl->disable(drm, drm_output);

  _drm_release_all_scanout(output);

  if (drm_output->crtc)
    drm_output->crtc->in_use = false;

  if (drm_output->gbm_surf) {
    if (drm_output->renderable_setup && drm->comp->renderer &&
        drm->comp->renderer->impl.destroy_renderable_output &&
        !drm->comp->renderer->impl.destroy_renderable_output(
            drm->comp->renderer, output)) {
      VT_WARN(drm->comp->log, "Failed to destroy renderable DRM output %p.",
              (void *)output);
    }
    drm_output->renderable_setup = false;

    gbm_surface_destroy(drm_output->gbm_surf);
    drm_output->gbm_surf = NULL;
  }

  output->user_data = NULL;

  free(output->physical.make);
  free(output->physical.model);
  free(output->physical.name);
  free(output->physical.serial_number);

  struct vt_output_mode_t *mode, *tmp_mode;
  wl_list_for_each_safe(mode, tmp_mode, &output->physical.modes, link) {
    wl_list_remove(&mode->link);
    free(mode);
  }

  pixman_region32_fini(&output->damage);

  wl_list_remove(&output->link_local);
  wl_list_remove(&output->link_global);

  return true;
}


static bool _drm_terminate_for_device(struct drm_backend_state_t *drm) {
  if (!drm || !drm->comp)
    return false;
  struct vt_compositor_t *comp = drm->comp;

  if (!comp->suspended && drm->drm_fd >= 0) {
    bool any_inflight;
    do {
      any_inflight = false;
      struct vt_output_t *output;
      wl_list_for_each(output, &drm->outputs, link_local) {
        if (output->user_data &&
            BACKEND_DATA(output, struct drm_output_state_t)->flip_inflight) {
          any_inflight = true;
          break;
        }
      }
      if (any_inflight && drmHandleEvent(drm->drm_fd, &drm->evctx) != 0) {
        VT_WARN(comp->log, "Failed to drain DRM events during teardown: %s",
                strerror(errno));
        break;
      }
    } while (any_inflight);
  }

  if (drm->res)
    drmModeFreeResources(drm->res);
  drm->res = NULL;

  struct vt_output_t *output, *tmp;
  wl_list_for_each_safe(output, tmp, &drm->outputs, link_local) {
    _drm_destroy_output_for_device(drm, output);
  }

  struct drm_plane_t *plane;
  wl_array_for_each(plane, &drm->planes) {
    vt_drm_format_array_free(&plane->formats);
    free(plane->cursor_sizes);
  }
  wl_array_release(&drm->planes);
  wl_array_release(&drm->crtcs);

  struct drm_backend_master_state_t *drm_master = NULL;
  if (drm->backend)
    drm_master =
        BACKEND_DATA(drm->backend, struct drm_backend_master_state_t);

  if (drm_master && drm_master->main_drm == drm && comp->renderer &&
      comp->renderer->impl.destroy) {
    comp->renderer->impl.destroy(comp->renderer);
    drm_master->main_drm = NULL;
  }

  if (drm->event_source) {
    wl_event_source_remove(drm->event_source);
    drm->event_source = NULL;
  }

  if (drm->gbm_dev) {
    gbm_device_destroy(drm->gbm_dev);
    drm->gbm_dev = NULL;
  }

  if (drm->drm_fd >= 0)
    drmDropMaster(drm->drm_fd);

  if (drm->drm_fd >= 0) {
    close(drm->drm_fd);
    drm->drm_fd = -1;
  }

  return true;
}

static void _drm_on_session_terminate(struct wl_listener *listener,
                                      void               *data) {
  if (!listener || !data)
    return;
  struct vt_session_t     *session = (struct vt_session_t *)data;
  struct vt_session_drm_t *session_drm =
      BACKEND_DATA(session, struct vt_session_drm_t);
  if (!session_drm)
    return;

  struct vt_device_t *dev, *tmp_dev;
  wl_list_for_each_safe(dev, tmp_dev, &session_drm->devices, link) {
    vt_session_close_device_drm(session, dev);
  }

  // Stop listening
  wl_list_remove(&listener->link);
  wl_list_init(&listener->link);
}

static void _drm_on_seat_disable(struct wl_listener *listener, void *data) {
  (void)listener;

  if (!data)
    return;

  struct vt_session_t *session = (struct vt_session_t *)data;
  if (!session->comp || !session->comp->backend)
    return;

  struct drm_backend_master_state_t *drm_master =
      BACKEND_DATA(session->comp->backend, struct drm_backend_master_state_t);
  if (!drm_master || session->comp->suspended)
    return;

  VT_TRACE(session->comp->log, "Seat disable event (VT switch away)");

  session->comp->suspended = true;

  struct drm_backend_state_t *drm;
  wl_list_for_each(drm, &drm_master->backends, link) { _drm_suspend(drm); }

  struct vt_input_backend_t *input = session->comp->input_backend;
  if (input && input->impl.suspend)
    input->impl.suspend(input);

  VT_TRACE(session->comp->log,
           "Seat disable complete (devices paused, not closed).");
}

static void _drm_keybind_switch_vt(struct vt_compositor_t *comp,
                                   void                   *user_data) {
  if (!comp || !comp->session || !user_data)
    return;
  uint32_t vt = *(uint32_t *)user_data;
  vt_session_switch_vt_drm(comp->session, vt);
}

static void _drm_on_seat_enable(struct wl_listener *listener, void *data) {
  (void)listener;

  if (!data)
    return;

  struct vt_session_t *session = (struct vt_session_t *)data;
  if (!session->comp || !session->comp->backend)
    return;
  struct drm_backend_master_state_t *drm_master =
      BACKEND_DATA(session->comp->backend, struct drm_backend_master_state_t);
  if (!drm_master || !session->comp->suspended)
    return;

  VT_TRACE(session->comp->log, "Seat enable event (VT switch back)");

  session->comp->suspended = false;

  struct drm_backend_state_t *drm;
  wl_list_for_each(drm, &drm_master->backends, link) { _drm_resume(drm); }

  struct vt_input_backend_t *input = session->comp->input_backend;
  if (input && input->impl.resume)
    input->impl.resume(input);

  VT_TRACE(session->comp->log, "Seat enable complete (devices resumed).");
}

static bool _drm_output_sync_refresh(struct drm_output_state_t *drm_output) {
  if (!drm_output || !drm_output->base || !drm_output->drm_backend)
    return false;

  struct vt_output_t *output = drm_output->base;
  uint32_t refresh = output->refresh_rate;

  if (drm_output->mode.hdisplay != output->width ||
      drm_output->mode.vdisplay != output->height)
    return false;

  if (drm_output->mode.vrefresh * 1000 == refresh)
    return true;

  drmModeConnector *conn = drmModeGetConnector(
      drm_output->drm_backend->drm_fd, drm_output->conn_id);
  if (!conn)
    return false;

  drmModeModeInfo *match = NULL;
  for (int i = 0; i < conn->count_modes; i++) {
    drmModeModeInfo *mode = &conn->modes[i];
    if (mode->hdisplay == output->width && mode->vdisplay == output->height &&
        mode->vrefresh * 1000 == refresh) {
      match = mode;
      break;
    }
  }

  if (!match) {
    drmModeFreeConnector(conn);
    return false;
  }

  drm_output->mode = *match;
  drm_output->needs_modeset = true;

  struct vt_output_mode_t *mode;
  wl_list_for_each(mode, &output->physical.modes, link) {
    mode->flags &= ~WL_OUTPUT_MODE_CURRENT;
    if (mode->width == output->width && mode->height == output->height &&
        mode->refresh == refresh) {
      mode->flags |= WL_OUTPUT_MODE_CURRENT;
    }
  }

  drmModeFreeConnector(conn);
  return true;
}

static bool _drm_plane_has_format(struct drm_plane_t *plane,
                                  uint32_t format, uint64_t modifier) {
  if (!plane)
    return false;

  struct vt_drm_format_t *fmt;
  wl_array_for_each(fmt, &plane->formats) {
    if (fmt->format != format)
      continue;

    if (vt_drm_format_get_mod(fmt, modifier))
      return true;
  }

  return false;
}

static bool _drm_handle_frame_for_device(struct drm_backend_state_t *drm,
                                         struct vt_output_t         *output) {
  if (!drm || !drm->comp || !output || !output->user_data || !drm->impl)
    return false;

  struct vt_compositor_t *comp = drm->comp;
  if (!comp->renderer) {
    VT_ERROR(comp->log,
             "Renderer backend not initialized before handling frame.");
    return false;
  }

  struct drm_output_state_t *drm_output =
      BACKEND_DATA(output, struct drm_output_state_t);
  if (!drm_output || drm_output->drm_backend != drm || !drm_output->gbm_surf ||
      drm_output->flip_inflight || drm_output->pending_valid) {
    return false;
  }

  if (!_drm_output_sync_refresh(drm_output)) {
    VT_WARN(comp->log,
            "Requested DRM output refresh rate is not available on connector %u.",
            drm_output->conn_id);
  }

  VT_TRACE(comp->log, "Handling frame...");

  drm_output->pending_layers = drm_output->layer_plan;
  wl_array_init(&drm_output->layer_plan);

  bool primary_assigned = false;
  struct drm_scanout_layer_t *scanout_layer;
  wl_array_for_each(scanout_layer, &drm_output->pending_layers) {
    if (drm_output->crtc &&
        scanout_layer->plane == drm_output->crtc->plane_primary) {
      primary_assigned = true;
      break;
    }
  }

  if (!primary_assigned) {
    struct gbm_bo *bo = gbm_surface_lock_front_buffer(drm_output->gbm_surf);
    if (!bo) {
      VT_WARN(comp->log,
              "Failed to get the GBM front buffer for frame in output %p.",
              (void *)output);
      _drm_scanout_layers_finish(drm_output, &drm_output->pending_layers, -1);
      output->needs_repaint = true;
      return true;
    }

    scanout_layer = wl_array_add(&drm_output->pending_layers,
                                 sizeof(*scanout_layer));
    if (!scanout_layer) {
      gbm_surface_release_buffer(drm_output->gbm_surf, bo);
      _drm_scanout_layers_finish(drm_output, &drm_output->pending_layers, -1);
      output->needs_repaint = true;
      return false;
    }

    memset(scanout_layer, 0, sizeof(*scanout_layer));
    scanout_layer->plane = drm_output->crtc
                               ? drm_output->crtc->plane_primary
                               : NULL;
    scanout_layer->scanout.bo = bo;
    scanout_layer->scanout.surface = drm_output->gbm_surf;
    scanout_layer->src = (struct vt_box_t){
        .x = 0,
        .y = 0,
        .width = gbm_bo_get_width(bo),
        .height = gbm_bo_get_height(bo),
    };
    scanout_layer->dst = (struct vt_box_t){
        .x = 0,
        .y = 0,
        .width = output->width,
        .height = output->height,
    };

    if (!drm_fb_init_from_gbm(drm, &scanout_layer->scanout.fb, bo)) {
      _drm_scanout_layers_finish(drm_output, &drm_output->pending_layers, -1);
      output->needs_repaint = true;
      VT_ERROR(comp->log,
               "Failed to import GBM buffer for connector %u into DRM.",
               drm_output->conn_id);
      return false;
    }
  }

  size_t plane_count = drm_output->pending_layers.size /
                       sizeof(struct drm_scanout_layer_t);
  if (plane_count == 0) {
    output->needs_repaint = true;
    return false;
  }

  struct drm_kms_plane_state_t *plane_states =
      calloc(plane_count, sizeof(*plane_states));
  if (!plane_states) {
    _drm_scanout_layers_finish(drm_output, &drm_output->pending_layers, -1);
    output->needs_repaint = true;
    return false;
  }

  size_t plane_index = 0;
  wl_array_for_each(scanout_layer, &drm_output->pending_layers) {
    plane_states[plane_index++] = (struct drm_kms_plane_state_t){
        .plane = scanout_layer->plane,
        .fb = &scanout_layer->scanout.fb,
        .src = scanout_layer->src,
        .dst = scanout_layer->dst,
        .acquire_fence_fd = scanout_layer->scanout.use
                                ? scanout_layer->scanout.use->acquire_fence_fd
                                : -1,
    };
  }

  struct drm_kms_commit_t commit = {
      .output = drm_output,
      .planes = plane_states,
      .plane_count = plane_count,
      .active = true,
      .modeset = drm_output->needs_modeset,
      .test_only = false,
      .async = drm_output->allow_tearing &&
               drm->caps[VT_DRM_CAP_TEARING_PAGE_FLIPS] &&
               !drm_output->needs_modeset,
      .out_fence_fd = -1,
  };

  bool was_bootstrapped = drm_output->modeset_bootstrapped;
  if (!drm->impl->commit(drm, &commit)) {
    free(plane_states);
    _drm_scanout_layers_finish(drm_output, &drm_output->pending_layers, -1);
    drm_output->pending_valid = false;
    output->needs_repaint = true;
    return false;
  }
  free(plane_states);

  struct drm_scanout_layer_t *layer;

  wl_array_for_each(layer, &drm_output->pending_layers) {
    if (!layer->surface || !layer->scanout.use)
      continue;

    if (!vt_output_track_presented_surface(output, layer->surface)) {
      VT_ERROR(comp->log, "Failed to track directly scanned-out surface %p.",
               layer->surface);
      continue;
    }

    VT_TRACE(comp->log, "OUTPUT: tracking presented surface=%p output=%p",
             layer->surface, output);
  }

  VT_TRACE(comp->log,
           "DRM: real commit succeeded output=%p event_pending=%d layers=%zu",
           output, commit.event_pending,
           drm_output->pending_layers.size /
               sizeof(struct drm_scanout_layer_t));

  drm_output->pending_valid = true;
  drm_output->needs_modeset = false;
  drm_output->pending_out_fence_fd = commit.out_fence_fd;

  if (commit.event_pending) {
    drm_output->flip_inflight = true;
  } else {
    _drm_complete_pending(drm_output);

    uint32_t t = vt_util_get_time_msec();
    if (!was_bootstrapped) {
      vt_comp_frame_done_all(comp, t);
      wl_display_flush_clients(comp->wl.dsp);
    } else {
      vt_comp_frame_done(comp, output, t);
    }
  }

  output->needs_repaint = false;
  return true;
}


static void _drm_update_tearing_cap(struct drm_backend_state_t *drm) {
  if (!drm || !drm->impl)
    return;

  uint64_t cap = 0;
  drm->caps[VT_DRM_CAP_TEARING_PAGE_FLIPS] = false;

  if (drm->impl->atomic) {
#ifdef DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP
    if (drmGetCap(drm->drm_fd, DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP, &cap) == 0 &&
        cap != 0) {
      drm->caps[VT_DRM_CAP_TEARING_PAGE_FLIPS] = true;
    }
#endif
  } else {
#ifdef DRM_CAP_ASYNC_PAGE_FLIP
    if (drmGetCap(drm->drm_fd, DRM_CAP_ASYNC_PAGE_FLIP, &cap) == 0 && cap != 0)
      drm->caps[VT_DRM_CAP_TEARING_PAGE_FLIPS] = true;
#endif
  }

  VT_TRACE(drm->comp->log, "Tearing page flips %s",
           drm->caps[VT_DRM_CAP_TEARING_PAGE_FLIPS] ? "supported"
                                                    : "unsupported");
}

static bool _drm_verify_caps(struct drm_backend_state_t *drm) {
  if (!drm || !drm->comp)
    return false;

  if (drmGetCap(drm->drm_fd, DRM_CAP_CURSOR_WIDTH, &drm->cursor_w))
    drm->cursor_w = 64;
  if (drmGetCap(drm->drm_fd, DRM_CAP_CURSOR_HEIGHT, &drm->cursor_h))
    drm->cursor_h = 64;

  uint64_t cap = 0;
  if (drmGetCap(drm->drm_fd, DRM_CAP_PRIME, &cap) != 0 ||
      !(cap & DRM_PRIME_CAP_IMPORT)) {
    VT_WARN(drm->comp->log,
            "PRIME buffer import is unsupported on DRM device %s.",
            drm->dev ? drm->dev->path : "unknown");
  }

  if (drmSetClientCap(drm->drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0)
    VT_WARN(drm->comp->log, "DRM universal planes unsupported");

  drm->caps[VT_DRM_CAP_ATOMIC_MODESET] =
      drmSetClientCap(drm->drm_fd, DRM_CLIENT_CAP_ATOMIC, 1) == 0;

  if (!drm->caps[VT_DRM_CAP_ATOMIC_MODESET])
    VT_WARN(drm->comp->log, "Atomic modesetting unsupported, using legacy KMS");

  int ret = drmGetCap(drm->drm_fd, DRM_CAP_ADDFB2_MODIFIERS, &cap);
  drm->caps[VT_DRM_CAP_ADDFB2_MODIFIERS] = ret == 0 && cap == 1;

  if (drmGetCap(drm->drm_fd, DRM_CAP_CRTC_IN_VBLANK_EVENT, &cap) != 0 || !cap)
    VT_WARN(drm->comp->log, "DRM_CRTC_IN_VBLANK_EVENT unsupported");

  if (drmGetCap(drm->drm_fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap) != 0 || !cap)
    VT_WARN(drm->comp->log, "DRM_CAP_TIMESTAMP_MONOTONIC unsupported");

  VT_TRACE(drm->comp->log, "ADDFB2 modifiers %s",
           drm->caps[VT_DRM_CAP_ADDFB2_MODIFIERS] ? "supported"
                                                  : "unsupported");
  return true;
}


// ===================================================
// =================== PUBLIC API ====================
// ===================================================
bool backend_init_drm(struct vt_backend_t *backend) {
  if (!backend || !backend->comp || !backend->comp->session)
    return false;

  if (!(backend->user_data = VT_ALLOC(
            backend->comp, sizeof(struct drm_backend_master_state_t)))) {
    return false;
  }
  memset(backend->user_data, 0, sizeof(struct drm_backend_master_state_t));

  struct drm_backend_master_state_t *drm_master =
      BACKEND_DATA(backend, struct drm_backend_master_state_t);

  assert(drm_master);

  drm_master->comp = backend->comp;

  wl_list_init(&drm_master->backends);
  wl_list_init(&drm_master->session_terminate_listener.link);
  wl_list_init(&drm_master->seat_enable_listener.link);
  wl_list_init(&drm_master->seat_disable_listener.link);
  wl_list_init(&drm_master->drm_change_listener.link);

  drm_master->session_terminate_listener.notify = _drm_on_session_terminate;
  wl_signal_add(&backend->comp->session->ev_session_terminate,
                &drm_master->session_terminate_listener);

  struct vt_session_drm_t *session_drm =
      BACKEND_DATA(backend->comp->session, struct vt_session_drm_t);
  if (!session_drm) {
    VT_ERROR(backend->comp->log, "DRM session is not initialized.");
    goto fail;
  }

  drm_master->seat_enable_listener.notify = _drm_on_seat_enable;
  wl_signal_add(&session_drm->ev_seat_enable,
                &drm_master->seat_enable_listener);

  drm_master->seat_disable_listener.notify = _drm_on_seat_disable;
  wl_signal_add(&session_drm->ev_seat_disable,
                &drm_master->seat_disable_listener);

  drm_master->drm_change_listener.notify = _drm_on_drm_change;
  wl_signal_add(&session_drm->ev_drm_change_card,
                &drm_master->drm_change_listener);

  enum { max_gpus = 16 };
  struct vt_device_t *gpus[max_gpus];
  drm_master->n_drm =
      vt_session_enumerate_cards_drm(backend->comp->session, gpus, max_gpus);
  if (drm_master->n_drm == 0) {
    VT_ERROR(backend->comp->log, "No DRM devices were found.");
    goto fail;
  }

  for (uint32_t i = 0; i < drm_master->n_drm; i++) {
    if (!gpus[i]) {
      VT_ERROR(backend->comp->log, "DRM device %u is invalid.", i);
      goto fail;
    }

    struct drm_backend_state_t *drm_backend =
        VT_ALLOC(backend->comp, sizeof(struct drm_backend_state_t));
    if (!drm_backend) {
      VT_ERROR(backend->comp->log,
               "Failed to allocate DRM backend for GPU (%i).", gpus[i]->fd);
      goto fail;
    }

    memset(drm_backend, 0, sizeof(*drm_backend));
    drm_backend->backend = backend;

    if (!_drm_init_for_device(backend->comp, drm_backend, gpus[i])) {
      VT_ERROR(backend->comp->log,
               "Failed to initialize DRM backend for GPU (%i).", gpus[i]->fd);
      goto fail;
    }

    wl_list_insert(&drm_master->backends, &drm_backend->link);
  }

  if (!drm_master->main_drm) {
    VT_ERROR(drm_master->comp->log, "Failed to find renderable DRM device.");
    goto fail;
  }

  struct drm_backend_state_t *drm;
  wl_list_for_each(drm, &drm_master->backends, link) {
    drm->main_drm = drm == drm_master->main_drm ? NULL : drm_master->main_drm;

    if (!_drm_resources_init(drm)) {
      VT_WARN(backend->comp->log,
              "Resource initialization failed for GPU %s (FD: %i)",
              drm->dev->path, drm->drm_fd);
    }
  }

  if (wl_list_empty(&backend->comp->outputs)) {
    VT_ERROR(backend->comp->log, "No active DRM outputs were initialized.");
    goto fail;
  }

  if (backend->comp->have_proto_dmabuf) {
    struct vt_dmabuf_feedback_t *default_feedback =
        vt_dmabuf_feedback_create(drm_master->comp, drm_master->main_drm->dev);

    if (!default_feedback) {
      VT_ERROR(backend->comp->log, "Failed to create default DMABUF feedback.");
      goto fail;
    }

    default_feedback->comp = drm_master->comp;

    if (!_drm_build_dmabuf_feedback(drm_master, default_feedback)) {
      VT_ERROR(backend->comp->log, "Failed to build default DMABUF feedback.");
    } else {
      const uint32_t dmabuf_ver = 4;
      if (!vt_proto_linux_dmabuf_v1_init(backend->comp, default_feedback,
                                         dmabuf_ver)) {
        VT_ERROR(backend->comp->log,
                 "Failed to initialize DMABUF protocol version %i.",
                 dmabuf_ver);
      } else {
        VT_TRACE(backend->comp->log,
                 "Successfully initialized DMABUF protocol version %i.",
                 dmabuf_ver);
      }
    }

    vt_dmabuf_feedback_fini(default_feedback);
  }

  if (backend->comp->have_proto_dmabuf_explicit_sync) {
    const uint32_t dmabuf_explicit_sync_ver = 2;
    if (!vt_proto_linux_explicit_sync_v1_init(backend->comp,
                                              dmabuf_explicit_sync_ver)) {
      VT_ERROR(backend->comp->log,
               "Failed to initialize DMABUF explicit sync protocol version %i.",
               dmabuf_explicit_sync_ver);
    } else {
      VT_TRACE(
          backend->comp->log,
          "Successfully initialized DMABUF explicit sync protocol version %i.",
          dmabuf_explicit_sync_ver);
    }
  }

  VT_TRACE(backend->comp->log, "Successfully initialized DRM backend.");

  return true;

fail:
  backend_terminate_drm(backend);
  return false;
}


bool backend_handle_frame_drm(struct vt_backend_t *backend,
                              struct vt_output_t  *output) {
  if (!backend || !backend->comp || !backend->user_data || !output ||
      !output->user_data)
    return false;

  if (!_added_global_keybinds && backend->comp->input_backend &&
      backend->comp->seat) {
    // Keybinds for VT switching
    struct vt_kb_modifiers_t mods = backend->comp->input_backend->mods;
    for (uint32_t i = 0; i < 12; i++) {
      uint32_t *vt = VT_ALLOC(backend->comp, sizeof(*vt));
      if (!vt)
        return false;
      *vt = i + 1;
      vt_seat_add_global_keybind(
          backend->comp->seat, XKB_KEY_XF86Switch_VT_1 + i,
          mods.ctrl | mods.alt, _drm_keybind_switch_vt, vt);
    }
    _added_global_keybinds = true;
  }

  struct drm_output_state_t *drm_output =
      BACKEND_DATA(output, struct drm_output_state_t);
  if (!drm_output || !drm_output->drm_backend) {
    VT_ERROR(backend->comp->log, "No DRM device available for frame handling.");
    return false;
  }
  return _drm_handle_frame_for_device(drm_output->drm_backend, output);
}

bool backend_terminate_drm(struct vt_backend_t *backend) {
  if (!backend || !backend->comp || !backend->user_data)
    return false;

  struct drm_backend_master_state_t *drm_master =
      BACKEND_DATA(backend, struct drm_backend_master_state_t);
  if (!drm_master) {
    VT_PARAM_CHECK_FAIL(backend->comp);
    return false;
  }
  struct drm_backend_state_t *main_drm = drm_master->main_drm;
  struct drm_backend_state_t *drm, *tmp;
  wl_list_for_each_safe(drm, tmp, &drm_master->backends, link) {
    if (drm == main_drm)
      continue;
    _drm_terminate_for_device(drm);
    wl_list_remove(&drm->link);
  }

  if (main_drm) {
    _drm_terminate_for_device(main_drm);
    wl_list_remove(&main_drm->link);
  }

  if (!wl_list_empty(&drm_master->seat_enable_listener.link))
    wl_list_remove(&drm_master->seat_enable_listener.link);
  if (!wl_list_empty(&drm_master->seat_disable_listener.link))
    wl_list_remove(&drm_master->seat_disable_listener.link);
  if (!wl_list_empty(&drm_master->session_terminate_listener.link))
    wl_list_remove(&drm_master->session_terminate_listener.link);
  if (!wl_list_empty(&drm_master->drm_change_listener.link))
    wl_list_remove(&drm_master->drm_change_listener.link);

  if (backend->user_data) {
    backend->user_data = NULL;
  }

  _added_global_keybinds = false;

  return true;
}

bool backend_is_dmabuf_importable_drm(struct vt_backend_t     *backend,
                                      struct vt_dmabuf_attr_t *attr,
                                      int32_t                  device_fd) {
  if (!backend || !backend->comp || !attr)
    return false;

  if (attr->num_planes == 0 || attr->num_planes > 4)
    return false;

  if (device_fd >= 0)
    return drm_prime_test_import(device_fd, attr);

  struct drm_backend_master_state_t *master =
      BACKEND_DATA(backend, struct drm_backend_master_state_t);
  if (!master || !master->main_drm)
    return false;

  return drm_prime_test_import(master->main_drm->drm_fd, attr);
}

bool backend_test_output_layers_drm(
    struct vt_backend_t *backend, struct vt_output_t *output,
    struct vt_output_layer_state_t *layers, size_t layer_count) {
  if (!backend || !backend->comp || !output || !output->user_data ||
      (layer_count > 0 && !layers)) {
    return false;
  }

  for (size_t i = 0; i < layer_count; i++)
    layers[i].accepted = false;

  struct drm_output_state_t *drm_output =
      BACKEND_DATA(output, struct drm_output_state_t);
  struct drm_backend_state_t *drm = drm_output->drm_backend;
  if (!drm || !drm->impl)
    return false;

  _drm_scanout_layers_finish(drm_output, &drm_output->layer_plan, -1);

  if (layer_count != 1) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p: "
             "expected exactly one layer, got %zu.",
             output, layer_count);
    return false;
  }
  
  struct vt_output_layer_state_t *layer = &layers[0];

  if (!drm->impl->atomic) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p: "
             "atomic modesetting is unavailable.",
             output);
    goto reject;
  }

  if (drm_output->flip_inflight) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p: "
             "page flip is still in flight.",
             output);
    goto reject;
  }

  

  if (!drm_output->crtc) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p: "
             "output has no assigned CRTC.",
             output);
    goto reject;
  }

  if (!drm_output->crtc->plane_primary) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p: "
             "CRTC has no primary plane.",
             output);
    goto reject;
  }


  if (!layer->surface) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p: "
             "layer has no surface.",
             output);
    goto reject;
  }

  if (!layer->surface->current_buf_use) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p surface %p: "
             "surface has no current buffer use.",
             output, layer->surface);
    goto reject;
  }

  if (!layer->surface->current_buf_use->buf) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p surface %p: "
             "current buffer use has no buffer.",
             output, layer->surface);
    goto reject;
  }

  if (layer->dst.x != 0 || layer->dst.y != 0 ||
      layer->dst.width != output->width ||
      layer->dst.height != output->height) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p surface %p: "
             "destination does not cover the full output. "
             "dst=[x=%d y=%d w=%u h=%u], output=[w=%u h=%u].",
             output, layer->surface,
             layer->dst.x, layer->dst.y,
             layer->dst.width, layer->dst.height,
             output->width, output->height);
    goto reject;
  }

  if (layer->src.x < 0 || layer->src.y < 0 ||
      layer->src.width <= 0 || layer->src.height <= 0) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p surface %p: "
             "invalid source rectangle src=[x=%d y=%d w=%u h=%u].",
             output, layer->surface,
             layer->src.x, layer->src.y,
             layer->src.width, layer->src.height);
    goto reject;
  }

  struct vt_buffer_use_t *use = layer->surface->current_buf_use;

  struct vt_dmabuf_attr_t attr = {0};
  if (!vt_buffer_get_dmabuf(use->buf, &attr)) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p surface %p: "
             "buffer %p is not a DMABUF.",
             output, layer->surface, use->buf);
    goto reject;
  }

    const char *format_name = drmGetFormatName(attr.format);

  VT_TRACE(backend->comp->log,
           "DRM: direct scanout candidate: "
           "surface=%p buffer=%p format=%s modifier=0x%016" PRIx64
           " size=%dx%d planes=%u",
           layer->surface, use->buf, format_name, attr.mod, attr.width,
           attr.height, attr.num_planes);

  if ((uint64_t)layer->src.x + (uint64_t)layer->src.width >
          (uint64_t)attr.width ||
      (uint64_t)layer->src.y + (uint64_t)layer->src.height >
          (uint64_t)attr.height) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p surface %p: "
             "source rectangle exceeds DMABUF bounds. "
             "src=[x=%d y=%d w=%u h=%u], buffer=[w=%d h=%d].",
             output, layer->surface,
             layer->src.x, layer->src.y,
             layer->src.width, layer->src.height,
             attr.width, attr.height);
    goto reject;
  }

  struct drm_plane_t *plane = drm_output->crtc->plane_primary;

  if (!_drm_plane_has_format(plane, attr.format, attr.mod)) {

    VT_TRACE(
        backend->comp->log,
        "DRM: direct scanout rejected for output %p surface %p: "
        "primary plane %u does not support format=%s modifier=0x%016" PRIx64
        ".",
        output, layer->surface, plane->id,
        format_name ? format_name : "UNKNOWN", attr.mod);
    goto reject;
  }

  if (use->acquire_fence_fd >= 0 &&
      plane->props[VT_DRM_PLANE_IN_FENCE_FD] == 0) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p surface %p: "
             "buffer has acquire fence fd=%d but primary plane %u has no "
             "IN_FENCE_FD property.",
             output, layer->surface,
             use->acquire_fence_fd, plane->id);
    goto reject;
  }

  if (use->release && vt_buffer_release_needs_fence(use->release) &&
      drm_output->crtc->props[VT_DRM_CRTC_OUT_FENCE_PTR] == 0) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p surface %p: "
             "buffer release requires explicit fence but CRTC %u has no "
             "OUT_FENCE_PTR property.",
             output, layer->surface, drm_output->crtc->id);
    goto reject;
  }

  struct drm_scanout_layer_t plan = {
      .surface = layer->surface,
      .plane = plane,
      .src = layer->src,
      .dst = layer->dst,
  };

  if (!drm_fb_init_from_buffer(drm, &plan.scanout.fb, use->buf)) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p surface %p: "
             "failed to create DRM framebuffer from buffer %p.",
             output, layer->surface, use->buf);
    goto reject;
  }

  plan.scanout.use = vt_buffer_use_ref(use);

  struct drm_kms_plane_state_t plane_state = {
      .plane = plane,
      .fb = &plan.scanout.fb,
      .src = plan.src,
      .dst = plan.dst,
      .acquire_fence_fd = use->acquire_fence_fd,
  };

  struct drm_kms_commit_t commit = {
      .output = drm_output,
      .planes = &plane_state,
      .plane_count = 1,
      .active = true,
      .modeset = drm_output->needs_modeset,
      .test_only = true,
      .async = false,
      .out_fence_fd = -1,
  };

  if (!drm->impl->commit(drm, &commit)) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout rejected for output %p surface %p: "
             "atomic TEST_ONLY commit failed. "
             "plane=%u src=[x=%d y=%d w=%u h=%u] "
             "dst=[x=%d y=%d w=%u h=%u].",
             output, layer->surface, plane->id, layer->src.x, layer->src.y,
             layer->src.width, layer->src.height, layer->dst.x, layer->dst.y,
             layer->dst.width, layer->dst.height);

    _drm_scanout_finish(drm_output, &plan.scanout, -1);
    goto reject;
  }

  struct drm_scanout_layer_t *stored =
      wl_array_add(&drm_output->layer_plan, sizeof(*stored));

  if (!stored) {
    VT_TRACE(backend->comp->log,
             "DRM: direct scanout preparation failed for output %p surface %p: "
             "could not store validated layer plan.",
             output, layer->surface);

    _drm_scanout_finish(drm_output, &plan.scanout, -1);
    goto reject;
  }

  *stored = plan;
  layer->accepted = true;

  VT_TRACE(backend->comp->log,
           "DRM: direct scanout accepted for output %p surface %p: "
           "plane=%u buffer=%p format=%s modifier=0x%016" PRIx64 " "
           "src=[x=%d y=%d w=%u h=%u] "
           "dst=[x=%d y=%d w=%u h=%u].",
           output, layer->surface, plane->id, use->buf, format_name, attr.mod,
           layer->src.x, layer->src.y, layer->src.width, layer->src.height,
           layer->dst.x, layer->dst.y, layer->dst.width, layer->dst.height);

  return true;

reject:
  vt_proto_linux_dmabuf_v1_set_surface_feedback(layer->surface);
  return true;
}

bool backend_build_surface_feedback(struct vt_backend_t         *backend,
                                    struct vt_surface_t         *surface,
                                    struct vt_output_t          *output,
                                    struct vt_dmabuf_feedback_t *feedback) {

  assert(backend && surface && output && feedback);

  struct drm_backend_master_state_t *drm_master =
      BACKEND_DATA(backend, struct drm_backend_master_state_t);

  struct drm_output_state_t *drm_output =
      BACKEND_DATA(output, struct drm_output_state_t);

  assert(drm_master && drm_output && drm_master->main_drm &&
         drm_master->main_drm->dev);

  if (!drm_output->crtc || !drm_output->crtc->plane_primary) {
    VT_ERROR(drm_master->comp->log,
             "Cannot build surface feedback for surface %p, output %p; CRTC or"
             "primary plane is not available",
             surface, output);
    return false;
  }

  feedback->dev_main = drm_master->main_drm->dev;

  struct vt_dmabuf_tranche_t *scanout_tranche =
      vt_dmabuf_feedback_add_tranche(feedback, drm_master->main_drm->dev,
                                     VT_DMABUF_TRANCHE_FLAG_DIRECT_SCANOUT);

  if (!scanout_tranche) {
    VT_ERROR(drm_master->comp->log,
             "Failed to create direct-scanout tranche for surface feedback of "
             "surface %p, output %p",
             surface, output);
    return false;
  }

  _log_drm_format_array(drm_master->comp, "Renderer sampling formats",
                        &drm_master->main_drm->sampling_formats);

  _log_drm_format_array(drm_master->comp, "Primary plane formats",
                        &drm_output->crtc->plane_primary->formats);

  vt_drm_format_array_intersect(&scanout_tranche->formats,
                                &drm_master->main_drm->sampling_formats,
                                &drm_output->crtc->plane_primary->formats);

  VT_TRACE(drm_master->comp->log,
           "Surface feedback for surface=%p output=%p: direct scanout tranche",
           surface, output);

  _log_dmabuf_tranche(drm_master->comp, scanout_tranche,
                      drm_master->main_drm->dev->path);

  struct vt_dmabuf_tranche_t *fallback_tranche = vt_dmabuf_feedback_add_tranche(
      feedback, drm_master->main_drm->dev, VT_DMABUF_TRANCHE_FLAG_COMPOSITE);

  if (!fallback_tranche) {
    VT_ERROR(drm_master->comp->log,
             "Failed to create fallback tranche for surface feedback of "
             "surface %p, output %p",
             surface, output);
    return false;
  }

  if (!vt_drm_format_array_copy(&fallback_tranche->formats,
                                &drm_master->main_drm->sampling_formats)) {
    VT_ERROR(drm_master->comp->log,
             "Failed to copy renderer formats into fallback tranche");
    return false;
  }

  VT_TRACE(drm_master->comp->log,
           "Surface feedback for surface=%p output=%p: composite tranche",
           surface, output);

  _log_dmabuf_tranche(drm_master->comp, fallback_tranche,
                      drm_master->main_drm->dev->path);

  return true;
}

bool backend_prepare_output_frame_drm(struct vt_backend_t *backend,
                                      struct vt_output_t  *output) {
  if (!backend || !backend->comp || backend->comp->suspended || !output ||
      !output->user_data)
    return false;

  struct drm_output_state_t *drm_output =
      BACKEND_DATA(output, struct drm_output_state_t);
  if (!drm_output)
    return false;
  if (drm_output->flip_inflight || drm_output->pending_valid)
    return false;
  if (!output->needs_repaint && drm_output->modeset_bootstrapped)
    return false;

  return true;
}


bool backend_implement_drm(struct vt_compositor_t *comp) {
  if (!comp || !comp->backend || !comp->session)
    return false;

  VT_TRACE(comp->log, "Implementing backend...");

  comp->backend->platform = VT_BACKEND_DRM_GBM;

  comp->backend->impl = (struct vt_backend_interface_t){
      .init = backend_init_drm,
      .is_dmabuf_importable = backend_is_dmabuf_importable_drm,
      .handle_frame = backend_handle_frame_drm,
      .terminate = backend_terminate_drm,
      .prepare_output_frame = backend_prepare_output_frame_drm,
      .test_output_layers = backend_test_output_layers_drm,
      .build_surface_feedback = backend_build_surface_feedback,
  };

  comp->session->impl = (struct vt_session_interface_t){
      .init = vt_session_init_drm,
      .close_device = vt_session_close_device_drm,
      .open_device = vt_session_open_device_drm,
      .manage_device = vt_session_manage_device_drm,
      .unmanage_device = vt_session_unmanage_device_drm,
      .device_from_fd = vt_session_device_from_fd_drm,
      .get_native_handle = vt_session_get_native_handle_drm,
      .finish_native_handle = vt_session_finish_native_handle_drm,
      .get_native_handle_render_node = vt_session_get_native_handle_render_node,
      .terminate = vt_session_terminate_drm,
  };

  return true;
}


