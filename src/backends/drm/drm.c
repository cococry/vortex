#define _GNU_SOURCE

#include <xf86drmMode.h>
#include "core/core_types.h"
#include <xf86drm.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <gbm.h>
#include <unistd.h>
#include <linux/kd.h>  
#include <pthread.h>  
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <drm/drm_fourcc.h>
#include <libinput.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdarg.h>
#include <string.h>

#include <errno.h>

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <pthread.h>

#include "core/compositor.h"
#include "render/renderer.h"

#include "./drm.h"
#include "./session_drm.h"

#include "core/session.h"
#include "input/wl_seat.h"
#include "protocols/linux_dmabuf.h"
#include "protocols/linux_explicit_sync.h"
#include "protocols/wl_shm.h"
#include "render/dmabuf.h"


#include <linux/input-event-codes.h>


struct drm_backend_state_t {
  int drm_fd;
  drmEventContext evctx;
  drmModeRes* res;
  struct gbm_device* gbm_dev;

  struct wl_list outputs;

  struct vt_compositor_t* comp;
  struct gbm_device* native_handle;

  struct wl_list link;


  struct vt_backend_t* root_backend;

  struct vt_device_t* dev;

  struct wl_event_source *event_source 
};

struct drm_backend_master_state_t {
  struct wl_list backends;
  uint32_t x_ptr;
  int32_t vt_fd;
  struct vt_compositor_t* comp;

  struct wl_listener session_terminate_listener,
  seat_disable_listener, seat_enable_listener;

  struct drm_backend_state_t* main_drm;
  uint32_t n_drm;
};


struct drm_output_state_t {
  struct gbm_bo *current_bo;
  struct gbm_bo *pending_bo;
  struct gbm_bo *prev_bo;
  struct gbm_bo *older_bo;
  uint32_t older_fb;   
  uint32_t current_fb;
  uint32_t pending_fb;
  uint32_t prev_fb;

  struct drm_backend_state_t* drm_backend;

  bool needs_modeset;
  bool flip_inflight;
  bool modeset_bootstrapped;

  struct gbm_surface* gbm_surf;

  drmModeModeInfo mode;
  uint32_t conn_id;
  uint32_t crtc_id;
};

static void   _drm_page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data);
static void   _drm_release_all_scanout(struct vt_output_t* output);
static bool   _drm_devices_equal(drmDevicePtr a, drmDevicePtr b);
static bool   _drm_can_share_dmabuf(struct vt_device_t* main_dev, struct vt_device_t* dev);
static bool   _drm_build_dmabuf_feedback(struct drm_backend_master_state_t* master, struct vt_dmabuf_feedback_t* feedback); 
static bool   _drm_suspend(struct drm_backend_state_t* backend);
static bool   _drm_resume(struct drm_backend_state_t* backend);
static int    _drm_dispatch(int fd, uint32_t mask, void *data);
static bool   _drm_init_for_device(struct vt_compositor_t* comp, struct drm_backend_state_t* drm, struct vt_device_t* dev); 
static bool   _drm_init_active_outputs_for_device(struct drm_backend_state_t* drm);
static bool   _drm_handle_frame_for_device(struct drm_backend_state_t* drm, struct vt_output_t* output);
static bool   _drm_create_output_for_device(struct drm_backend_state_t* drm, struct vt_output_t* output, void* data);
static bool   _drm_destroy_output_for_device(struct drm_backend_state_t* drm, struct vt_output_t* output);
static bool   _drm_terminate_for_device(struct drm_backend_state_t* drm);

static void   _drm_on_session_terminate(struct wl_listener* listener, void* data); 
static void   _drm_on_seat_enable(struct wl_listener* listener, void* data); 
static void   _drm_on_seat_disable(struct wl_listener* listener, void* data); 

static void   _drm_keybind_switch_vt(struct vt_compositor_t* comp, void* user_data); 

void 
_drm_page_flip_handler(int fd, unsigned int frame,
                       unsigned int sec, unsigned int usec, void *data) {
  struct vt_output_t* output = (struct vt_output_t*)data; 
  struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t);
  struct vt_compositor_t* comp = output->backend->comp;

  VT_TRACE(comp->log, "DRM: _drm_page_flip_handler(): Handling page flip event.")

  // Release the old, unused backbuffer
  if (drm_output->older_bo) {
    drmModeRmFB(fd, drm_output->older_fb);
    gbm_surface_release_buffer(drm_output->gbm_surf, drm_output->older_bo);
    drm_output->older_bo = NULL;
  }

  drm_output->older_bo = drm_output->prev_bo;
  drm_output->older_fb = drm_output->prev_fb;

  // Swap buffers, the previous buffer becomes the currently displayed buffer 
  // and the currently displayed buffer becomes the new, pending frame buffer 
  // (that we got from swapping buffers and is rendered to with OpenGL/Vulkan etc)
  drm_output->prev_bo = drm_output->current_bo;
  drm_output->prev_fb = drm_output->current_fb;

  drm_output->current_bo = drm_output->pending_bo;
  drm_output->current_fb = drm_output->pending_fb;

  // Reset pending buffer (so we can set it in the next frame)
  drm_output->pending_bo = NULL;
  drm_output->pending_fb = 0;

  drm_output->flip_inflight = false;
  uint32_t t = vt_util_get_time_msec(); 

  // Send the frame callbacks to all clients, establishing correct frame pacing
  vt_comp_frame_done(comp, output, t);

  if(output->needs_repaint) {
    vt_comp_schedule_repaint(comp, output);
  }
}

void 
_drm_release_all_scanout(struct vt_output_t* output) {
  struct drm_backend_state_t* drm = BACKEND_DATA(output->backend, struct drm_backend_state_t);
  struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t) ;

  // Basically release all used DRM scanout buffers  
  if (drm_output->pending_bo) {
    drmModeRmFB(drm->drm_fd, drm_output->pending_fb);
    gbm_surface_release_buffer(drm_output->gbm_surf, drm_output->pending_bo);
    drm_output->pending_bo = NULL; drm_output->pending_fb = 0;
  }
  if (drm_output->current_bo) {
    drmModeRmFB(drm->drm_fd, drm_output->current_fb);
    gbm_surface_release_buffer(drm_output->gbm_surf, drm_output->current_bo);
    drm_output->current_bo = NULL; drm_output->current_fb = 0;
  }
  if (drm_output->prev_bo) {
    drmModeRmFB(drm->drm_fd, drm_output->prev_fb);
    gbm_surface_release_buffer(drm_output->gbm_surf, drm_output->prev_bo);
    drm_output->prev_bo = NULL; drm_output->prev_fb = 0;
  }
}

static bool _drm_devices_equal(drmDevicePtr a, drmDevicePtr b) {
    if (!a || !b) return false;
    if (a->bustype != b->bustype) return false;
    return memcmp(&a->businfo, &b->businfo, sizeof(a->businfo)) == 0;
}

static bool _drm_can_share_dmabuf(struct vt_device_t* main_dev, struct vt_device_t* dev) {
    if (!main_dev || !dev)
        return false;

  drmDevicePtr main_dev_drm = NULL, dev_drm = NULL;

  if (drmGetDevice(main_dev->fd, &main_dev_drm) != 0 ||
    drmGetDevice(dev->fd, &dev_drm) != 0) {
    fprintf(stderr, "drmGetDevice() failed\n");
    if (main_dev_drm) drmFreeDevice(&main_dev_drm);
    if (dev_drm) drmFreeDevice(&dev_drm);
    return false;
  }

  if(_drm_devices_equal(dev_drm, main_dev_drm)) {
    drmFreeDevice(&main_dev_drm);
    drmFreeDevice(&dev_drm);
    return true;
  }

  bool compatible = false;

  // if the devices do not have the same bustype, they cannot share memory 
  if (dev_drm->bustype == main_dev_drm->bustype) {
    if (main_dev_drm->bustype == DRM_BUS_PCI) {
      if(
        main_dev_drm->businfo.pci && dev_drm->businfo.pci &&
        main_dev_drm->businfo.pci->domain == dev_drm->businfo.pci->domain &&
        main_dev_drm->businfo.pci->bus == dev_drm->businfo.pci->bus &&
        main_dev_drm->businfo.pci->dev == dev_drm->businfo.pci->dev &&
        main_dev_drm->businfo.pci->func == dev_drm->businfo.pci->func) {
        compatible = true;
      }
    } else if(memcmp(&main_dev_drm->businfo, &dev_drm->businfo, sizeof(dev_drm->businfo)) == 0) { 
      compatible = true;
    }
  } 


  if(!compatible) {
    char card_main_dev[64], card_dev[64];
    // the card paths are '/dev/dri/cardX', we need to get only 'cardX':
    const char* base_main = strrchr(main_dev->path, '/');
    const char* base_other = strrchr(dev->path, '/');
    if (!base_main || !base_other) {
      compatible = false;
    } else {
      // If the devices share the same IOMMU group, they can share dmabufs 
      snprintf(card_main_dev, sizeof(card_main_dev), "%s", base_main + 1);
      snprintf(card_dev, sizeof(card_dev), "%s", base_other + 1);
      char path_a[256], path_b[256];
      snprintf(path_a, sizeof(path_a), "/sys/class/drm/%s/device/iommu_group", card_main_dev);
      snprintf(path_b, sizeof(path_b), "/sys/class/drm/%s/device/iommu_group", card_dev);

      char link_a[256], link_b[256];
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

static const char* 
_fourcc_to_str(uint32_t fmt) {
  static char str[5];
  str[0] = fmt & 0xFF;
  str[1] = (fmt >> 8) & 0xFF;
  str[2] = (fmt >> 16) & 0xFF;
  str[3] = (fmt >> 24) & 0xFF;
  str[4] = '\0';
  return str;
}

static const char* 
_modifier_to_str(uint64_t mod, char* buf, size_t len) {
  if (mod == DRM_FORMAT_MOD_INVALID)
    snprintf(buf, len, "INVALID");
  else if (mod == DRM_FORMAT_MOD_LINEAR)
    snprintf(buf, len, "LINEAR");
  else
    snprintf(buf, len, "0x%016" PRIx64, mod);
  return buf;
}

void 
_log_dmabuf_tranche(struct vt_compositor_t *comp,
                         const struct vt_dmabuf_tranche_t *tranche,
                         const char *device_path) {
  char dev_path[64];
  snprintf(dev_path, sizeof(dev_path), "%s", device_path);

  VT_TRACE(comp->log,
           "DRM: ========== Added DMABUF Tranche for device '%s'  ========== ", dev_path);

  VT_TRACE(comp->log,
           "      target_device: %u:%u  (dev_t: 0x%lx)",
           major(tranche->target_device->dev),
           minor(tranche->target_device->dev),
           (unsigned long)tranche->target_device);

  VT_TRACE(comp->log, "      flags: 0x%x%s",
           tranche->flags,
           tranche->flags ? " (preferred/scanout)" : "");

  size_t n_formats = tranche->formats.size / sizeof(struct vt_dmabuf_drm_format_t);
  VT_TRACE(comp->log, "      formats: %zu total", n_formats);

  struct vt_dmabuf_drm_format_t *fmt;
  wl_array_for_each(fmt, &tranche->formats) {
    VT_TRACE(comp->log, "        • %s (%4.4s), %zu modifiers:",
             drmGetFormatName(fmt->format), _fourcc_to_str(fmt->format), fmt->len);

    for (size_t j = 0; j < fmt->len; j++) {
      char mod_str[32];
      _modifier_to_str(fmt->mods[j].mod, mod_str, sizeof(mod_str));
      VT_TRACE(comp->log, "            - %s%s",
               mod_str,
               fmt->mods[j]._egl_ext_only ? " (EXT_ONLY)" : "");
    }
  }

  VT_TRACE(comp->log, "DRM: =========================================================== ", dev_path);
}

bool 
_drm_build_dmabuf_feedback(
  struct drm_backend_master_state_t* master,
  struct vt_dmabuf_feedback_t* feedback
) {
  if(!feedback || !master || !master->comp || !master->comp->renderer) return false; 
  drmDevicePtr dev_main_drm = NULL;
  drmGetDevice(master->main_drm->dev->fd, &dev_main_drm); 

  feedback->dev_main = master->main_drm->dev;
  wl_array_init(&feedback->tranches);

  VT_TRACE(master->comp->log, "DRM: Building default DMABUF feedback...");

  uint32_t max_shm_formats = 256, n_shm_formats = 0;
  uint32_t shm_formats[max_shm_formats];

  uint32_t n_devs = 0;
  drmDevicePtr devs[master->n_drm];
  // add the tranches
  struct drm_backend_state_t* drm;
  wl_list_for_each(drm, &master->backends, link) {
    struct vt_device_t* dev = drm->dev;
    VT_TRACE(master->comp->log, "DRM: Iterating possible DMABUF tranche device '%s' (FD: %i)...", dev->path, dev->fd);
    if(!drm->dev) {
      VT_TRACE(master->comp->log, "DRM: Skipping possible tranche device '%s': No device associated.", dev->path);
      continue;
    }
    drmDevicePtr dev_drm = NULL;
    if (drmGetDevice(dev->fd, &dev_drm) != 0) {
      VT_WARN(master->comp->log, "DRM: Failed to retrieve DRM device pointer from internal DRM device '%s'", dev->path);
      if (dev_drm) drmFreeDevice(&dev_drm);
      continue;
    }
    devs[n_devs++] = dev_drm;

    // skip devices without any render nodes
    if (!(dev_drm->available_nodes & (1 << DRM_NODE_RENDER))) {
      VT_TRACE(master->comp->log, "DRM: Skipping possible tranche device '%s': Has no available render nodes.", dev->path);
      continue;
    }
    // skip non-shareable GPUs
    if (!_drm_can_share_dmabuf(master->main_drm->dev, dev)) {
      VT_TRACE(
        master->comp->log, "DRM: Skipping possible tranche device '%s': Cannot share DMABUFs with main device '%s'.",
        dev->path, master->main_drm->dev->path);
      continue;  
    }

    struct vt_dmabuf_tranche_t* tranche = wl_array_add(&feedback->tranches, sizeof(*tranche));
    tranche->target_device = dev;

    tranche->flags = _drm_devices_equal(dev_main_drm, dev_drm)
      ? VT_DMABUF_TRANCHE_FLAG_DIRECT_SCANOUT 
      : 0;

    // Add the formats that the device supports, as we got told by the 
    // renderer, to the tranche
    struct vt_renderer_t* r = master->comp->renderer;
    if(dev == master->main_drm->dev) { 
      if(r->impl.query_dmabuf_formats_with_renderer) {
        if(!master->comp->renderer->impl.query_dmabuf_formats_with_renderer(r, &tranche->formats)) {
          VT_WARN(master->comp->log, "DRM: Cannot query DMABUF formats for main device '%s' from EGL.", dev->path);
          wl_array_init(&tranche->formats);
          continue;
        }
      }
    } else { 
      if(r->impl.query_dmabuf_formats) {
        if(!master->comp->renderer->impl.query_dmabuf_formats(master->comp, drm->gbm_dev,  &tranche->formats)) {
          VT_WARN(master->comp->log, "DRM: Cannot query DMABUF formats for tranche device '%s' from EGL.", dev->path);
          wl_array_init(&tranche->formats);
          continue;
        }
      }
    }
    struct vt_dmabuf_drm_format_t* formats = tranche->formats.data;
    for(uint32_t i = 0; i < tranche->formats.size; i++) {
      if(!(n_shm_formats < max_shm_formats - 1)) {
        VT_WARN(master->comp->log, "DRM: Maximum number of SHM formats reached, not adding format %i.", formats[i].format);
        break; 
      }
      shm_formats[n_shm_formats++] = formats[i].format; 
    }
    _log_dmabuf_tranche(master->comp, tranche, dev->path);

  }
  // Adding a generic fallback tranche (LINEAR DRM_FORMAT_ARGB8888) 
  struct vt_dmabuf_tranche_t* fallback = wl_array_add(&feedback->tranches, sizeof(*fallback));

  fallback->target_device = feedback->dev_main;
  fallback->flags = 0;
  wl_array_init(&fallback->formats);

  struct vt_dmabuf_drm_format_t* fmt = wl_array_add(&fallback->formats, sizeof(*fmt));
  fmt->format = DRM_FORMAT_XRGB8888;
  fmt->len = 2;
  fmt->mods = calloc(fmt->len, sizeof(*fmt->mods));
  fmt->mods[0].mod = DRM_FORMAT_MOD_LINEAR;
  fmt->mods[0]._egl_ext_only = false;
  fmt->mods[1].mod = DRM_FORMAT_MOD_INVALID;
  fmt->mods[1]._egl_ext_only = false;

  drmFreeDevice(&dev_main_drm);
  for (int i = 0; i < n_devs; i++) {
    drmFreeDevice(&devs[i]);
  }

  shm_formats[n_shm_formats++] = fmt->format;

  if(!vt_proto_wl_shm_init(master->comp, shm_formats, n_shm_formats)) {
    VT_ERROR(master->comp->log, "DRM: Failed to initialize WL SHM protcol.\n");
    return false;
  }

  VT_TRACE(master->comp->log, "DRM: Added default fallback tranche (LINEAR DRM_FORMAT_ARGB8888).\n");

  return true;
}

bool 
_drm_suspend(struct drm_backend_state_t* backend) {
  if (!backend)
    return false;
  if (backend->comp->suspended)
    return true;

  backend->comp->suspended = true;
  VT_TRACE(backend->comp->log, "DRM: Suspending seat session (VT switch away)...");

  // we stop submitting new flips immediately
  struct vt_output_t* output;
  wl_list_for_each(output, &backend->outputs, link_local) {
    output->needs_repaint = false;
  }

  // Drain any in-flight page flips 
  bool any_inflight;
  do {
    any_inflight = false;
    wl_list_for_each(output, &backend->outputs, link_local) {
      if (!output->user_data)
        continue;
      struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t);
      if (drm_output->flip_inflight) {
        any_inflight = true;
        break;
      }
    }
    if (any_inflight)
      drmHandleEvent(backend->drm_fd, &backend->evctx);
  } while (any_inflight);

  // disable scanout on all outputs (causes ~1 frame of black screen but safe)
  wl_list_for_each(output, &backend->outputs, link_local) {
    if (!output->user_data)
      continue;

    struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t);

    VT_TRACE(backend->comp->log, 
             "DRM: Disabling CRTC %u for connector %u.", 
             drm_output->crtc_id, drm_output->conn_id);

    drmModeSetCrtc(backend->drm_fd, drm_output->crtc_id,
                   0, 0, 0, NULL, 0, NULL);
  }

  return true;
}


bool 
_drm_resume(struct drm_backend_state_t* backend) {
  if (!backend)
    return false;
  if (!backend->comp->suspended)
    return true;

  backend->comp->suspended = false;
  VT_TRACE(backend->comp->log, "DRM: Resuming seat session (VT switch back)...");


  struct vt_output_t* output;
  wl_list_for_each(output, &backend->outputs, link_local) {
    if (!output->user_data)
      continue;

    struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t);
    if (!drm_output->current_bo || drm_output->current_fb == 0)
      continue;

    VT_TRACE(backend->comp->log,
             "DRM: Re-enabling CRTC %u with existing framebuffer %u for connector %u.",
             drm_output->crtc_id, drm_output->current_fb, drm_output->conn_id);

    // libseat has already called drmSetMaster() for us.
    // we can safely re-enable CRTCs and resume rendering.
    if (drmModeSetCrtc(backend->drm_fd, drm_output->crtc_id,
                       drm_output->current_fb, 0, 0,
                       &drm_output->conn_id, 1, &drm_output->mode) != 0) {
      VT_ERROR(backend->comp->log,
               "DRM: Failed to restore CRTC %u: %s",
               drm_output->crtc_id, strerror(errno));
      drm_output->needs_modeset = true;
    } else {
      drm_output->needs_modeset = false;
    }

    // mark the output for repaint on the next frame
    output->needs_repaint = true;
    drm_output->flip_inflight = false;
  }

  if (!backend->event_source) {
    backend->event_source = wl_event_loop_add_fd(
      backend->comp->wl.evloop,
      backend->drm_fd,
      WL_EVENT_READABLE,
      _drm_dispatch,
      backend);
  }

  wl_list_for_each(output, &backend->outputs, link_local) {
    pixman_region32_clear(&output->damage);
    pixman_region32_union_rect(&output->damage, &output->damage,
                               0, 0, output->width, output->height);
    vt_comp_schedule_repaint(backend->comp, output);
  }

  uint32_t t = vt_util_get_time_msec();
  vt_comp_frame_done_all(backend->comp, t);
  wl_display_flush_clients(backend->comp->wl.dsp);

  return true;
}

int  
_drm_dispatch(int fd, uint32_t mask, void *data) {
  if(!data) return 0;
  struct drm_backend_state_t* drm = (struct drm_backend_state_t*)data;
  drmHandleEvent(fd, &drm->evctx);
  return 0;
}

bool _drm_init_for_device(struct vt_compositor_t* comp, struct drm_backend_state_t* drm, struct vt_device_t* dev) {
  if(!drm) return false;
  wl_list_init(&drm->outputs);

  drm->drm_fd = dev->fd;
  drm->dev = dev;
  drm->comp = comp;

  VT_TRACE(comp->log, "DRM: Initializing DRM/KMS backend...");


  if(!(drm->gbm_dev = gbm_create_device(drm->drm_fd))) {
    VT_ERROR(comp->log, "DRM: cannot create GBM device (fd: %i)", drm->drm_fd);
    _drm_terminate_for_device(drm);
    return false;
  }

  VT_TRACE(comp->log, "DRM: Successfully created GBM device on FD: %i", drm->drm_fd);  

  drm->evctx.version = DRM_EVENT_CONTEXT_VERSION;
  drm->evctx.page_flip_handler = _drm_page_flip_handler;
  drm->evctx.vblank_handler = NULL;

  drm->event_source = wl_event_loop_add_fd(comp->wl.evloop, drm->drm_fd, WL_EVENT_READABLE, _drm_dispatch, drm);

  drm->native_handle = drm->gbm_dev;

  struct drm_backend_master_state_t* drm_master = BACKEND_DATA(drm->root_backend, struct drm_backend_master_state_t);

  if(!comp->renderer) {
    log_fatal(comp->log, "DRM: Must allocate renderer before initializing backend.");
  }
  if(!drm_master->main_drm && comp->renderer->impl.is_handle_renderable(comp->renderer, drm->native_handle)) {
    comp->renderer->impl.init(comp->backend, comp->renderer, drm->native_handle);
    drm_master->main_drm = drm;
  }

  _drm_init_active_outputs_for_device(drm);

  VT_TRACE(comp->log, "DRM: Successfully initialized DRM/KMS backend for GPU %i.", drm->drm_fd);

  return true;
}

bool 
_drm_init_active_outputs_for_device(struct drm_backend_state_t* drm) {
  if(!drm) return false;
  struct vt_compositor_t* comp = drm->comp;

  VT_TRACE(comp->log, "DRM: Initializing active outputs.");

  if (!comp->renderer || !comp->renderer->impl.setup_renderable_output) {
    VT_ERROR(comp->log, "DRM: Renderer backend not initialized before output setup.");
    return false;
  }


  drm->res = drmModeGetResources(drm->drm_fd);
  if (!drm->res) {
    VT_ERROR(comp->log, "DRM: drmModeGetResources() failed: %s", strerror(errno));
    return false;
  }

  for (int i = 0; i < drm->res->count_connectors; i++) {
    drmModeConnector *conn = drmModeGetConnector(drm->drm_fd, drm->res->connectors[i]);
    if (!conn) continue;
    if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
      struct vt_output_t* output = VT_ALLOC(comp, sizeof(struct vt_output_t));
      output->needs_damage_rebuild = true;
      wl_list_init(&output->link_local);
      wl_list_init(&output->link_global);
      if (!output) {
        VT_ERROR(comp->log, "DRM: allocation failed for output.");
        drmModeFreeConnector(conn);
        continue;
      }
      pixman_region32_init(&output->damage);
      output->backend = comp->backend;
      if (!_drm_create_output_for_device(drm, output, conn)) {
        VT_ERROR(comp->log, "DRM: Failed to setup internal DRM output output.");
        _drm_destroy_output_for_device(drm, output);
        drmModeFreeConnector(conn);
        continue;
      } 
      if(!comp->renderer->impl.setup_renderable_output(comp->renderer, output)) {
        VT_ERROR(comp->log, "DRM: Failed to setup renderable output for DRM output (%ix%i@%.2f)",
                 output->width, output->height, output->refresh_rate);
        _drm_destroy_output_for_device(drm, output);
        drmModeFreeConnector(conn);
        continue;
      }
    }
    drmModeFreeConnector(conn);
  }

  if (wl_list_empty(&drm->outputs)) {
    VT_ERROR(comp->log, "DRM: No connected connector found");
    drmModeFreeResources(drm->res);
    return false;
  }

  return true;
}

bool 
_drm_create_output_for_device(struct drm_backend_state_t* drm, struct vt_output_t* output, void* data) {
  if(!drm || !output || !data) return false;

  VT_TRACE(drm->comp->log, "DMR: Creating DRM internal output.");
  if(!(output->user_data = VT_ALLOC(drm->comp, sizeof(struct drm_output_state_t)))) {
    return false;
  }

  drmModeConnector* conn = (drmModeConnector*)data;
  drmModeModeInfo mode;
  bool found = false;
  for (int j = 0; j < conn->count_modes; j++) {
    if (conn->modes[j].type & DRM_MODE_TYPE_PREFERRED) {
      mode = conn->modes[j];
      found = true;
      break;
    }
  }
  if (!found) {
    // Fallback to the first mode if none marked preferred
    mode = conn->modes[0];
  }
  struct drm_output_state_t* drm_output  = BACKEND_DATA(output, struct drm_output_state_t);
  drmModeModeInfo preferred_mode  = mode; 
  struct vt_compositor_t* comp    = drm->comp; 

  output->needs_repaint = true;

  drm_output->mode = preferred_mode;
  drm_output->needs_modeset = true;
  drm_output->conn_id = conn->connector_id;
  drm_output->drm_backend = drm;

  drmModeEncoder *enc = NULL;

  // Try to find a usable encoder
  if (conn->encoder_id)
    enc = drmModeGetEncoder(drm->drm_fd, conn->encoder_id);

  if (enc) {
    drm_output->crtc_id = enc->crtc_id;
    drmModeFreeEncoder(enc);
  } else if (drm->res->count_encoders > 0) {
    // fallback: try all encoders for this connector
    // (the voices are getting too loud)
    for (int i = 0; i < conn->count_encoders; i++) {
      enc = drmModeGetEncoder(drm->drm_fd, conn->encoders[i]);
      if (!enc) continue;
      for (int j = 0; j < drm->res->count_crtcs; j++) {
        if (enc->possible_crtcs & (1 << j)) {
          drm_output->crtc_id = drm->res->crtcs[j];
          break;
        }
      }
      drmModeFreeEncoder(enc);
      if (drm_output->crtc_id) break;
    }
  }

  if (!drm_output->crtc_id) {
    VT_ERROR(comp->log, "DRM: Failed to find CRTC for connector %u", drm_output->conn_id);
    drmModeFreeConnector(conn);
    return false;
  }

  const uint32_t desired_format = comp->renderer->_desired_render_buffer_format;
  if(!(drm_output->gbm_surf = gbm_surface_create(
    drm->gbm_dev,
    drm_output->mode.hdisplay, drm_output->mode.vdisplay,
    desired_format,
    GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
  ))) {
    VT_ERROR(comp->log, "DRM: cannot create GBM surface (%ux%u@%u) for output on connector %i for rendering.", 
             drm_output->mode.hdisplay, drm_output->mode.vdisplay, drm_output->mode.vrefresh, drm_output->conn_id);
    return false;
  } 


  VT_TRACE(comp->log, "DRM: Acknowledged connector: %u, CRTC %u, mode %ux%u@%u (%p)",
           drm_output->conn_id, drm_output->crtc_id,
           drm_output->mode.hdisplay, drm_output->mode.vdisplay, drm_output->mode.vrefresh, output);

  struct drm_backend_master_state_t* drm_master = BACKEND_DATA(output->backend, struct drm_backend_master_state_t); 
  // TODO: Implement correct output layouting e.g vertical monitors
  output->width = (uint32_t)drm_output->mode.hdisplay; 
  output->height = (uint32_t)drm_output->mode.vdisplay; 
  output->x = drm_master->x_ptr;
  output->y = 0; 
  output->height = (uint32_t)drm_output->mode.vdisplay; 
  output->refresh_rate = (uint32_t)drm_output->mode.vrefresh; 
  output->native_window = drm_output->gbm_surf;
  output->format = desired_format; 
  output->id = drm_output->conn_id;

  drm_master->x_ptr += output->width;

  wl_list_insert(&drm->outputs, &output->link_local);
  wl_list_insert(&drm->comp->outputs, &output->link_global);


  return true;
}

bool   
_drm_destroy_output_for_device(struct drm_backend_state_t* drm, struct vt_output_t* output) {
  if(!drm) return false;
  if(!output || !output->user_data) return false;

  VT_TRACE(drm->comp->log, "DRM: Destroying output %p.\n", output);

  _drm_release_all_scanout(output);
  struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t);
  if (drm_output->gbm_surf) {
    gbm_surface_destroy(drm_output->gbm_surf);
    drm_output->gbm_surf = NULL;
  
    if(!drm->comp->renderer->impl.destroy_renderable_output(drm->comp->renderer, output)) return false;
  }

  output->user_data = NULL;

  wl_list_remove(&output->link_local);

  output = NULL;

  return true;
}

static bool 
_drm_terminate_for_device(struct drm_backend_state_t* drm) {
  if(!drm) return false;
  struct vt_compositor_t* comp = drm->comp; 

  if (!comp->suspended && drm->drm_fd > 0) {
    bool any_inflight;
    do {
      any_inflight = false;
      struct vt_output_t* output;
      wl_list_for_each(output, &drm->outputs, link_local) {
        if (BACKEND_DATA(output, struct drm_output_state_t)->flip_inflight) {
          any_inflight = true;
          break;
        }
      }
      if (any_inflight)
        drmHandleEvent(drm->drm_fd, &drm->evctx);
    } while (any_inflight);
  }

  if(drm->res)
    drmModeFreeResources(drm->res);


  struct vt_output_t *output, *tmp;
  wl_list_for_each_safe(output, tmp, &drm->outputs, link_local) {
    _drm_destroy_output_for_device(drm, output);
  }

  comp->renderer->impl.destroy(comp->renderer);

  if (drm->gbm_dev) {
    gbm_device_destroy(drm->gbm_dev);
    drm->gbm_dev = NULL;
  }


  if (drm->drm_fd > 0) drmDropMaster(drm->drm_fd);

  if (drm->drm_fd > 0) {
    close(drm->drm_fd);
    drm->drm_fd = -1;
  }


  return true;
}

void 
_drm_on_session_terminate(struct wl_listener* listener, void* data) {
  if(!data) return;
  struct vt_session_t* session = (struct vt_session_t*)data;
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  struct vt_device_t* dev, *tmp_dev;
  wl_list_for_each_safe(dev, tmp_dev, &session_drm->devices, link) {
    vt_session_close_device_drm(session, dev);
  }

  // Stop listening
  wl_list_remove(&listener->link);
}

void _drm_on_seat_disable(struct wl_listener* listener, void* data) {
  if (!data) return;

  struct vt_session_t* session = (struct vt_session_t*)data;

  struct drm_backend_master_state_t* drm_master =
    BACKEND_DATA(session->comp->backend, struct drm_backend_master_state_t);

  VT_TRACE(session->comp->log, "DRM: Seat disable event (VT switch away)");

  struct drm_backend_state_t* drm;
  wl_list_for_each(drm, &drm_master->backends, link) {
    _drm_suspend(drm);
  }

  struct vt_input_backend_t* input = session->comp->input_backend;
  if(input && input->impl.suspend)
    input->impl.suspend(input);

  wl_list_init(&session->comp->seat->keyboards);

  VT_TRACE(session->comp->log, "DRM: Seat disable complete (devices paused, not closed).");
}

void   
_drm_keybind_switch_vt(struct vt_compositor_t* comp, void* user_data) { 
  if(!comp || !user_data) return;
  uint32_t vt = *(uint32_t*)user_data;
  vt_session_switch_vt_drm(comp->session, vt);
}


void _drm_on_seat_enable(struct wl_listener *listener, void *data) {
  if (!data) return;

  struct vt_session_t* session = (struct vt_session_t*)data;
  struct drm_backend_master_state_t* drm_master =
    BACKEND_DATA(session->comp->backend, struct drm_backend_master_state_t);

  VT_TRACE(session->comp->log, "DRM: Seat enable event (VT switch back)");

  struct drm_backend_state_t* drm;
  wl_list_for_each(drm, &drm_master->backends, link) {
    _drm_resume(drm);
  }
  
  struct vt_input_backend_t* input = session->comp->input_backend;
  if(input && input->impl.resume)
    input->impl.resume(input);

  VT_TRACE(session->comp->log, "DRM: Seat enable complete (devices resumed).");
}

bool 
_drm_handle_frame_for_device(struct drm_backend_state_t* drm, struct vt_output_t* output) {
  if(!drm || !output) return false;
  struct vt_compositor_t* comp = drm->comp;

  VT_TRACE(comp->log, "DRM: Handling frame...");
  vt_comp_repaint_scene(comp, output);

  if (!comp->renderer) {
    VT_ERROR(comp->log, "DRM: Renderer backend not initialized before handling frame.");
    return false;
  }

  struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t); 

  // Retrieve the front buffer that we rendered to with the renderer 
  struct gbm_bo *bo = gbm_surface_lock_front_buffer(drm_output->gbm_surf);
  // If we could not get the front buffer, we'll try again next frame.
  if (!bo) {
    VT_WARN(comp->log, "DRM: Failed to get the GBM front buffer for frame in output %p.", output);
    output->needs_repaint = true; 
    return true;
  }

  uint32_t w = gbm_bo_get_width(bo);
  uint32_t h = gbm_bo_get_height(bo);
  uint32_t fmt = gbm_bo_get_format(bo);
  uint32_t handle = gbm_bo_get_handle(bo).u32;
  uint32_t stride = gbm_bo_get_stride(bo);
  uint32_t fb;
  uint32_t handles[4] = { gbm_bo_get_handle(bo).u32 };
  uint32_t strides[4] = { gbm_bo_get_stride(bo) };
  uint32_t offsets[4] = { 0 };

  uint64_t modifier = gbm_bo_get_modifier(bo);

  int ret;
  // If the BO wants modifiers, add the FB with modifier arguments
  if (modifier != DRM_FORMAT_MOD_INVALID) {
    uint64_t mods[4] = { modifier, 0, 0, 0 };
    ret = drmModeAddFB2WithModifiers(drm->drm_fd, w, h, fmt,
                                     handles, strides, offsets, mods, &fb,
                                     DRM_MODE_FB_MODIFIERS);
  } else {
    ret = drmModeAddFB2(drm->drm_fd, w, h, fmt,
                        handles, strides, offsets, &fb, 0);
  }

  // If we could not create a DRM frame buffer...
  if (ret != 0) {
    gbm_surface_release_buffer(drm_output->gbm_surf, bo);
    // Try again next farme
    output->needs_repaint = true;
    VT_ERROR(comp->log, "DRM: canot create DRM frame buffer for output %p: drmModeAddFB2(%ux%u, fmt=0x%08x) failed: %s",
             output, w, h, fmt, strerror(errno));
    return false;
  }

  if (drm_output->needs_modeset) {
    if (drmModeSetCrtc(drm->drm_fd, drm_output->crtc_id, fb,
                       0, 0, &drm_output->conn_id, 1, &drm_output->mode) != 0) {
      drmModeRmFB(drm->drm_fd, fb);
      gbm_surface_release_buffer(drm_output->gbm_surf, bo);
      // Try again next farme
      output->needs_repaint = true;
      VT_ERROR(comp->log, "DRM: cannot set CRTC mode for output %p: drmModeSetCrtc() failed: %s",
               output, strerror(errno));
      return false;
    }
    VT_TRACE(comp->log, "DRM: Successfully performed DRM CRTC mode set for output %p (%ux%u@%.2f, ID: %i)", output,
             output->width, output->height, output->refresh_rate, drm_output->conn_id);

    // Release the old buffer 
    if (drm_output->current_bo) {
      drmModeRmFB(drm->drm_fd, drm_output->current_fb);
      gbm_surface_release_buffer(drm_output->gbm_surf, drm_output->current_bo);
    }

    // Assign the newly rendered buffer
    drm_output->current_bo = bo;
    drm_output->current_fb = fb;
    drm_output->needs_modeset = false;

    // If we did not yet bootstrap (we just applied the first CRTC), we need to 
    // manually send the frame callbacks to the clients to kick off
    // the client rendering loop, because this path does not invoke page_flip_handler(),
    // which would normally send the frame callbacks.
    if(!drm_output->modeset_bootstrapped) {
      uint32_t t = vt_util_get_time_msec();
      vt_comp_frame_done_all(comp, t);
      wl_display_flush_clients(comp->wl.dsp);
      output->needs_repaint = false;
      drm_output->modeset_bootstrapped = true;
      VT_TRACE(comp->log, "DRM: Successfully bootstrapped the first frame for output %p.", output);
    }
    return true;
  }

  // Set pending buffer and eventually assign to 
  // current in _drm_page_flip_handler() (after flip completes)
  drm_output->pending_bo = bo;
  drm_output->pending_fb = fb;

  if( 
    drmModePageFlip(drm->drm_fd, drm_output->crtc_id, fb,
                    DRM_MODE_PAGE_FLIP_EVENT, output) != 0) {
    // Flip refused; free pending and try again later
    drmModeRmFB(drm->drm_fd, drm_output->pending_fb);
    gbm_surface_release_buffer(drm_output->gbm_surf, drm_output->pending_bo);
    drm_output->pending_bo = NULL; drm_output->pending_fb = 0;
    output->needs_repaint = true;
    VT_ERROR(comp->log, "DRM: cannot do a page flip: drmModePageFlip() failed: %s",
             strerror(errno));
    return false;
  }
  VT_TRACE(comp->log, "DRM: Successfully performed drmModePageFlip() call.");

  drm_output->flip_inflight = true;

  // we’ve submitted a frame so clear the desire until something else changes
  output->needs_repaint = false;
}

// ===================================================
// =================== PUBLIC API ====================
// ===================================================
bool
backend_init_drm(struct vt_backend_t* backend) {
  if(!backend || !backend->comp || !backend->comp->session) {
    return false;
  }
  if(!(backend->user_data = VT_ALLOC(backend->comp, sizeof(struct drm_backend_master_state_t))))  {
    return false;
  }

  struct drm_backend_master_state_t* drm_master = BACKEND_DATA(backend, struct drm_backend_master_state_t); 
  drm_master->comp = backend->comp;

  wl_list_init(&drm_master->backends);

  // Listen for the session terminate signal so that 
  // we can call our backend-specific handler. 
  drm_master->session_terminate_listener.notify = _drm_on_session_terminate; 
  wl_signal_add(&backend->comp->session->ev_session_terminate, &drm_master->session_terminate_listener);

  // Listen for seat enable/disable signals for correct VT switching 
  // functionality
  struct vt_session_drm_t* session_drm = BACKEND_DATA(backend->comp->session, struct vt_session_drm_t); 
  drm_master->seat_enable_listener.notify = _drm_on_seat_enable; 
  wl_signal_add(&session_drm->ev_seat_enable, &drm_master->seat_enable_listener);

  drm_master->seat_disable_listener.notify = _drm_on_seat_disable; 
  wl_signal_add(&session_drm->ev_seat_disable, &drm_master->seat_disable_listener);

  // Create a DRM backend state for all the enumerated GPUs
  const uint8_t max_gpus = 16;
  struct vt_device_t* gpus[max_gpus];
  drm_master->n_drm = vt_session_enumerate_cards_drm(backend->comp->session, gpus, max_gpus);

  for(uint32_t i = 0; i < drm_master->n_drm; i++) {
    struct drm_backend_state_t* drm_backend = VT_ALLOC(backend->comp, sizeof(struct drm_backend_state_t));
    drm_backend->root_backend = backend;
    if(!_drm_init_for_device(backend->comp, drm_backend, gpus[i])) {
      VT_ERROR(backend->comp->log, "DRM: Failed to initialize DRM backend for GPU (%i).", gpus[i]->fd);
      return false;
    }
    wl_list_insert(&drm_master->backends, &drm_backend->link);
  }

  if(!drm_master->main_drm) {
    log_fatal(drm_master->comp->log, "DRM: Failed to find renderable DRM device.");
  }

  if(backend->comp->have_proto_dmabuf) {
    // initialize the dmabuf protocol with default feedback
    struct vt_dmabuf_feedback_t* default_feedback = calloc(1, sizeof(*default_feedback));
    default_feedback->comp = drm_master->comp;

    if(!(_drm_build_dmabuf_feedback(drm_master, default_feedback))) {
      VT_ERROR(backend->comp->log, "DRM: Failed to build default DMABUF feedback.");
      free(default_feedback);
    } else {
      const uint32_t dmabuf_ver = 4;
      if(!vt_proto_linux_dmabuf_v1_init(backend->comp, default_feedback, dmabuf_ver)) {
        VT_ERROR(backend->comp->log, "DRM: Failed to initialize DMABUF protocol version %i.", dmabuf_ver);
      } else {
        VT_TRACE(backend->comp->log, "DRM: Successfully initialized DMABUF protocol version %i.", dmabuf_ver);
      }
    }

    // cleanup the feedback
    struct vt_dmabuf_tranche_t* tranche;
    wl_array_for_each(tranche, &default_feedback->tranches) {
      struct vt_dmabuf_drm_format_t* fmt;
      wl_array_for_each(fmt, &tranche->formats) {
        free(fmt->mods);
      }
      wl_array_release(&tranche->formats);
    }
    wl_array_release(&default_feedback->tranches);

    free(default_feedback);
  }

  // init explicit sync
  if(backend->comp->have_proto_dmabuf_explicit_sync)  {
    const uint32_t dmabuf_explicit_sync_ver = 2;
    if(!vt_proto_linux_explicit_sync_v1_init(backend->comp, dmabuf_explicit_sync_ver)) {
      VT_ERROR(backend->comp->log, "DRM: Failed to initialize DMABUF explicit sync protocol version %i.", dmabuf_explicit_sync_ver);
    } else {
      VT_TRACE(backend->comp->log, "DRM: Successfully initialized DMABUF explicit sync protocol version %i.", dmabuf_explicit_sync_ver);
    }
  }

  VT_TRACE(backend->comp->log, "DRM: Successfully initialized DRM backend.");

  return true;
}

static bool _added_global_keybinds = false; 

bool 
backend_handle_frame_drm(struct vt_backend_t* backend, struct vt_output_t* output) {
  if(!backend || !backend->user_data) return false;

  if(!_added_global_keybinds) {
    // Keybinds for VT switching
    struct vt_kb_modifiers_t mods = backend->comp->input_backend->mods;
    for(uint32_t i = 0; i < 12; i++) {
      uint32_t* vt = VT_ALLOC(backend->comp, sizeof(*vt));
      *vt = i + 1;
      vt_seat_add_global_keybind(backend->comp->seat, 
                                 XKB_KEY_XF86Switch_VT_1 + i,mods.ctrl|mods.alt, 
                                 _drm_keybind_switch_vt, 
                                 vt);
    }
    _added_global_keybinds = true;
  }

  struct drm_backend_master_state_t* drm_master = BACKEND_DATA(backend, struct drm_backend_master_state_t); 

  if (!drm_master->main_drm) {
    VT_ERROR(backend->comp->log, "DRM: No main render device available for frame handling.");
    return false;
  }
  return _drm_handle_frame_for_device(drm_master->main_drm, output);
}


bool
backend_terminate_drm(struct vt_backend_t* backend) {
  struct vt_device_t* dev, *tmp_dev;
  struct drm_backend_master_state_t* drm_master = BACKEND_DATA(backend, struct drm_backend_master_state_t); 
  struct drm_backend_state_t* drm;
  wl_list_for_each(drm, &drm_master->backends, link) {
    _drm_terminate_for_device(drm);
  }

  if(backend->user_data) {
    backend->user_data = NULL;
  }
}

bool 
backend_is_dmabuf_importable_drm(struct vt_backend_t* backend, struct vt_dmabuf_attr_t* attr, int32_t device_fd) {
  if (device_fd < 0) return true;

  for (uint32_t i = 0; i < attr->num_planes; i++) {
    uint32_t handle = 0;
    if (drmPrimeFDToHandle(device_fd, attr->fds[i], &handle) != 0) {
      VT_ERROR(backend->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Failed to import DMA-BUF FD for plane %i", i);
      return false;
    }
    if (drmCloseBufferHandle(device_fd, handle) != 0) {
      VT_ERROR(backend->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Failed to closse buffer handle for plane %i", i);
      return false;
    }
  }
  return true;
}


bool 
backend_prepare_output_frame_drm(struct vt_backend_t* backend, struct vt_output_t* output) {
  (void)backend;
  struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t);
  if(drm_output->flip_inflight) return false;
  if(!output->needs_repaint && drm_output->modeset_bootstrapped) return false;

  return true;
}

bool 
backend_implement_drm(struct vt_compositor_t* comp) {
  if(!comp || !comp->backend) return false;

  VT_TRACE(comp->log, "DRM: Implementing backend...");

  comp->backend->platform = VT_BACKEND_DRM_GBM; 

  comp->backend->impl = (struct vt_backend_interface_t){
    .init = backend_init_drm,
    .is_dmabuf_importable = backend_is_dmabuf_importable_drm,
    .handle_frame = backend_handle_frame_drm,
    .terminate = backend_terminate_drm,
    .prepare_output_frame = backend_prepare_output_frame_drm,
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

