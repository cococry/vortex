#define _GNU_SOURCE
#include <string.h>
#include "session_drm.h"

#include <xf86drmMode.h>
#include <xf86drm.h>
#include <unistd.h>

#include <errno.h>

#include <sys/ioctl.h>
#include <sys/stat.h>


#include "../../core/core_types.h"

#define _VT_DRM_PRIMARY_MINOR_NAME  "card"
#define _SUBSYS_NAME "SESSION"

static void                   _vt_session_seat_log_func(enum libseat_log_level level, const char* format, va_list args);
static void                   _vt_session_seat_enable(struct libseat *seat, void* userdata);
static void                   _vt_session_seat_disable(struct libseat *seat, void* userdata);
static int32_t                _vt_session_seat_dispatch(int fd, uint32_t mask, void* data);

static bool                   _vt_session_drm_card_valid(const char* sysname);
static int32_t                _vt_session_drm_udev_dispatch(int fd, uint32_t mask, void *data);
static struct udev_enumerate* _vt_session_drm_udev_enumerate_cards(struct vt_session_t* session);
static struct vt_device_t*    _vt_session_drm_open_device_if_kms(struct vt_session_t* session, const char* path);
/*                            The two functions below are equivalent to drmIsKMS() and drmIoctl(), 
 *                            we are self implementing them as to not bring in libdrm as a hard dep.*/
static bool                   _vt_session_drm_card_is_kms(int32_t fd);
static int                    _vt_session_drm_ioctl(int fd, unsigned long request, void *arg);


// Taken from https://chromium.googlesource.com/chromiumos/third_party/libdrm/+/df21b293e9cf550ec8d6a3e49461350dbdf14260/xf86drm.c, line 174
int 
_vt_session_drm_ioctl(int fd, unsigned long request, void *arg) {
  int	ret;
  do {
    ret = ioctl(fd, request, arg);
  } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
  return ret;
}

bool 
_vt_session_drm_card_is_kms(int32_t fd)  {
  struct drm_mode_card_res res = {0};
  // If we can get DRM resoruces from the FD, it's a DRM card
  if (_vt_session_drm_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == 0)
    return true;

  if (errno == ENOTTY || errno == EOPNOTSUPP || errno == EINVAL)
    return false;

  return false;
}


struct wait_for_gpu_handler_t {
  bool found;
  struct wl_listener listener;
};



static struct vt_compositor_t* _log_comp_ptr = NULL;

static const struct libseat_seat_listener _seat_listener = {
  .disable_seat = _vt_session_seat_disable,
  .enable_seat = _vt_session_seat_enable 
};

void _vt_session_seat_log_func(enum libseat_log_level level, const char* format, va_list args) {
  if (!_log_comp_ptr) return; 
  char buf[512];
  vsnprintf(buf, sizeof(buf), format, args);

  switch (level) {
    case LIBSEAT_LOG_LEVEL_ERROR:
      VT_ERROR(_log_comp_ptr->log, "libseat: %s", buf);
      break;
    case LIBSEAT_LOG_LEVEL_INFO:
    case LIBSEAT_LOG_LEVEL_DEBUG:
      VT_TRACE(_log_comp_ptr->log, "libseat: %s", buf);
      break;
    default:
      VT_TRACE(_log_comp_ptr->log, "libseat (unknown log level): %s", buf);
      break;
  }
}

void 
_vt_session_seat_enable(struct libseat *seat, void* userdata) {
  if(!userdata) return;
  struct vt_session_t* session = (struct vt_session_t*)userdata;
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  if(!session_drm->_first_libseat_enable) {
    vt_util_emit_signal(&session_drm->ev_seat_enable, session);
  }
  session_drm->_first_libseat_enable = false;

  session->active = true;
  VT_TRACE(session->comp->log, "Seat enabled by libseat");
}

void 
_vt_session_seat_disable(struct libseat *seat, void* userdata) {
  if(!userdata) return;
  struct vt_session_t* session = (struct vt_session_t*)userdata;
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  vt_util_emit_signal(&session_drm->ev_seat_disable, session);
  
  if (libseat_disable_seat(seat) < 0) {
    VT_ERROR(session->comp->log, "Failed to disable seat %s.", session_drm->seat_name);
  }
  session->active = false;
  VT_TRACE(session->comp->log, "Seat disabled by libseat");
}

int32_t
_vt_session_seat_dispatch(int fd, uint32_t mask, void* data) {
  if(!data) return 0;
  struct vt_session_t* session = data;
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  int ret = libseat_dispatch(session_drm->seat, 0);
  if (ret < 0) {
    VT_ERROR(session->comp->log, "libseat_dispatch() failed: %s\n", strerror(errno));
  }
  return 0;
}


// Taken from lé wlroots
bool 
_vt_session_drm_card_valid(const char* sysname) {
  const char prefix[] = _VT_DRM_PRIMARY_MINOR_NAME;
  // Check if the sysname starts with "card" (_VT_DRM_PRIMARY_MINOR_NAME)
  if (strncmp(sysname, prefix, strlen(prefix)) != 0) {
    return false;
  }
  // Check if it's a primary card (card0-9)
  for (size_t i = strlen(prefix); sysname[i] != '\0'; i++) {
    if (sysname[i] < '0' || sysname[i] > '9') {
      return false;
    }
  }
  return true;
}

struct udev_enumerate* 
_vt_session_drm_udev_enumerate_cards(struct vt_session_t* session) {
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  struct udev_enumerate* enumerate = udev_enumerate_new(session_drm->udev);
  if (!enumerate) {
    VT_ERROR(session->comp->log, "Cannot enumerate DRM devices with udev.");
    return NULL;
  }

  // We tell udev that we want to enumerate all primary DRM cards (card0-9), no other devices.
  udev_enumerate_add_match_subsystem(enumerate, "drm");
  udev_enumerate_add_match_sysname(enumerate, _VT_DRM_PRIMARY_MINOR_NAME "[0-9]*");

  if (udev_enumerate_scan_devices(enumerate) != 0) {
    VT_TRACE(session->comp->log, "Cannot scan enumerted udev devices.");
    udev_enumerate_unref(enumerate);
    return NULL;
  }

  // We have to udev_enumerate_unref the enumerate outside 
  // of the function body after using it to not leak any memory
  return enumerate;
}

struct vt_device_t* 
_vt_session_drm_open_device_if_kms(struct vt_session_t* session, const char* path) {
  if(!path) return NULL;
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  struct vt_device_t* dev = VT_ALLOC(session->comp, sizeof(*dev));
  if(!vt_session_open_device_drm(session, dev, path) || !_vt_session_drm_card_is_kms(dev->fd)) {
    VT_TRACE(session->comp->log, "Not using non-KMS device '%s'.\n", path); 
    return NULL;
  }

  wl_list_insert(&session_drm->devices, &dev->link);
  return dev;
}

int32_t
_vt_session_drm_udev_dispatch(int fd, uint32_t mask, void *data) {
  if(!data) return 1;
  struct vt_session_t* session = data;
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  struct udev_device *udev_dev = udev_monitor_receive_device(session_drm->udev_mon);
  if (!udev_dev) return 1;

  const char* name = udev_device_get_sysname(udev_dev);
  const char* node = udev_device_get_devnode(udev_dev);
  const char* action = udev_device_get_action(udev_dev);
  VT_TRACE(session->comp->log, "Received udev event for device %s (%s)", name, action);

  if (!_vt_session_drm_card_valid(name) || !action || !action) { 
    udev_device_unref(udev_dev);
    return 1;
  }

  const char *seat = udev_device_get_property_value(udev_dev, "ID_SEAT");
  if (!seat) {
    seat = "seat0";
  }
  if (session_drm->seat_name[0] != '\0' && strcmp(session_drm->seat_name, seat) != 0) {
    udev_device_unref(udev_dev);
    return 1;
  }

  if (strcmp(action, "add") == 0) {
    VT_TRACE(session->comp->log, "Device %s added", name);
    struct vt_session_drm_event_t ev = { .device_node_name = name };
    vt_util_emit_signal(&session_drm->ev_drm_add_card, &ev);
  } else if (strcmp(action, "change") == 0 || strcmp(action, "remove") == 0) {
    dev_t devnum = udev_device_get_devnum(udev_dev);
    struct vt_device_t* dev;
    wl_list_for_each(dev, &session_drm->devices, link) {
      if (dev->dev != devnum) {
        continue;
      }

      if (strcmp(action, "change") == 0) {
        VT_TRACE(session->comp->log, "Device %s changed", name);
        struct vt_session_drm_event_t ev = { .device_node_name = name };
        vt_util_emit_signal(&session_drm->ev_drm_change_card, &ev);
      } else if (strcmp(action, "remove") == 0) {
        VT_TRACE(session->comp->log, "Device %s removed", name);
        struct vt_session_drm_event_t ev = { .device_node_name = name };
        vt_util_emit_signal(&session_drm->ev_drm_remove_card, &ev);
      } 
      break;
    }
  } else {
    VT_WARN(session->comp->log, "Got unknown udev action for device %s", name);
  }

  return 0;
}

// ===================================================
// =================== PUBLIC API ====================
// ===================================================
bool 
vt_session_init_drm(struct vt_session_t* session) {
  if(!session || !session->comp) return false;

  if(!(session->user_data = VT_ALLOC(session->comp, sizeof(struct vt_session_drm_t)))) {
    return false;
  }
  
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  _log_comp_ptr = session->comp;

  libseat_set_log_level(LIBSEAT_LOG_LEVEL_DEBUG);
  libseat_set_log_handler(_vt_session_seat_log_func);

  setenv("XDG_SESSION_TYPE", "wayland", 1);
  setenv("LIBSEAT_BACKEND", "seatd", 1);

  wl_list_init(&session_drm->devices);
  wl_signal_init(&session_drm->ev_drm_add_card);
  wl_signal_init(&session_drm->ev_drm_change_card);
  wl_signal_init(&session_drm->ev_drm_remove_card);

  wl_signal_init(&session->ev_session_terminate);

  wl_signal_init(&session_drm->ev_seat_enable);
  wl_signal_init(&session_drm->ev_seat_disable);


  session_drm->seat = libseat_open_seat(&_seat_listener, session);
  if(!session_drm->seat) {
    VT_ERROR(session->comp->log, "Cannot open seat.");
    return false;
  }
  const char *seat_name = libseat_seat_name(session_drm->seat);
  if (seat_name == NULL) {
    VT_ERROR(session->comp->log, "Unable to get seat name");
    vt_session_terminate_drm(session);
    return false;
  }
  snprintf(session_drm->seat_name, sizeof(session_drm->seat_name), "%s", seat_name);

  // After we opened lé chair, we make sure that lé wayland event loop knows about it
  int32_t fd = libseat_get_fd(session_drm->seat);
  VT_TRACE(session->comp->log, "Initialized seat with libseat fd = %d\n", fd);
  wl_event_loop_add_fd(session->comp->wl.evloop, fd, WL_EVENT_READABLE, _vt_session_seat_dispatch, session);

  if (libseat_dispatch(session_drm->seat, 0) == -1) {
    VT_ERROR(session->comp->log, "Cannot dispatch seat event on seat '%s'.", session_drm->seat_name);
    vt_session_terminate_drm(session);
    return false;
  }

  VT_TRACE(session->comp->log, "DRM: Opened Seat '%s'.", session_drm->seat_name);

  session_drm->udev = udev_new();
  if(!session_drm->udev) {
    VT_ERROR(session->comp->log, "Cannot initialize udev.");
    vt_session_terminate_drm(session);
    return false;
  }

  // populate the backend specific state for other subsystems like the input
  // system to be used later 
  session->native_handle = session_drm->udev; 
  snprintf(session->name, sizeof(session->name), "%s", session_drm->seat_name);

  // drinking seed oils to this (edgy comment) 
  session_drm->udev_mon = udev_monitor_new_from_netlink(session_drm->udev, "udev");
  if(!session_drm->udev_mon) {
    VT_ERROR(session->comp->log, "Cannot get udev monitor from netlink.");
    vt_session_terminate_drm(session);
    return false;
  }

  udev_monitor_filter_add_match_subsystem_devtype(session_drm->udev_mon, "drm", NULL);
  udev_monitor_enable_receiving(session_drm->udev_mon);

  // now after we created lé udev, we make sure that we also integrate
  // lé udev events into lé wayland ev loop.
  int32_t udev_fd = udev_monitor_get_fd(session_drm->udev_mon);
  wl_event_loop_add_fd(
    session->comp->wl.evloop, 
    udev_fd, WL_EVENT_READABLE, _vt_session_drm_udev_dispatch, session);

  session->active = true;

  return true;
}
bool 
vt_session_terminate_drm(struct vt_session_t* session) {
  if(!session || !session->comp) return false;
  
  vt_util_emit_signal(&session->ev_session_terminate, session);
  
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  if(session_drm->seat) {
    if(libseat_close_seat(session_drm->seat) < 0) {
      VT_ERROR(session->comp->log, "Cannot close seat.");
    }
    session_drm->seat = NULL;
  }
  if(session_drm->udev) {
    udev_unref(session_drm->udev);
    session_drm->udev = NULL;
  }
  if(session_drm->udev_mon) {
    udev_monitor_unref(session_drm->udev_mon);
    session_drm->udev_mon = NULL;
  }

  return true;
}

bool 
vt_session_open_device_drm(struct vt_session_t* session, struct vt_device_t* dev, const char* path) {
  if(!dev || !session || !session->comp) return false;
  
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  snprintf(dev->path, sizeof(dev->path), "%s", path);

  int32_t device_fd, device_id;
  if((device_id = libseat_open_device(session_drm->seat, dev->path, &device_fd)) < 0) {
    VT_ERROR(session->comp->log, "Cannot open device '%s'.\n", dev->path);
    return false;
  }

  struct stat st; 
  if(fstat(device_fd, &st) < 0) {
    VT_ERROR(session->comp->log, "Failed to stat device (FD: %i): %s'.\n", device_fd, strerror(errno));
    vt_session_close_device_drm(session, dev);
    return false;
  }

  dev->fd = device_fd;
  dev->device_id = device_id;
  dev->dev = st.st_rdev;
  
  VT_TRACE(session->comp->log, "Opening device %s, device ID: %i", path, dev->dev);

  return true;
}

bool 
vt_session_manage_device_drm(struct vt_session_t* session, struct vt_device_t* dev) {
  if(!dev || !session || !session->user_data) return false;
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 
  wl_list_insert(&session_drm->devices, &dev->link);
  return true;
}

bool 
vt_session_close_device_drm(struct vt_session_t* session, struct vt_device_t* dev) {
  if(!session || !dev) return false;
  
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  // Just close lé device
  if(libseat_close_device(session_drm->seat, dev->device_id) < 0) {
    VT_ERROR(session->comp->log, "Failed to close device (ID: %i)'.\n", dev->device_id);
    return false;
  }
  // ah and the fd aswell
  close(dev->fd);

  return true;
}

bool 
vt_session_unmanage_device_drm(struct vt_session_t* session, struct vt_device_t* dev) {
  if(!dev || !session || !session->user_data) return false;
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 
  wl_list_remove(&dev->link);
  return true;
}

void*
vt_session_get_native_handle_drm(struct vt_session_t* session, struct vt_device_t* dev) {
  if(!session || !dev) return NULL;
  drmDevice *device = NULL;
  if (drmGetDeviceFromDevId(dev->dev, 0, &device) != 0) {
    VT_ERROR(session->comp->log, "Failed to get native handle of device '%s': drmGetDeviceFromDevId failed.", dev->path);
    return NULL;
  }
  return device;
}

bool 
vt_session_finish_native_handle_drm(struct vt_session_t* session, void* handle) {
  if(!session || !handle) return false;
  drmFreeDevice((drmDevice **)&handle);
  return true;
}

const char* 
vt_session_get_native_handle_render_node(struct vt_session_t* session, void* handle) {
  if(!session || !handle) return NULL;
  drmDevice* native = (drmDevice*)handle;
  if(!(native->available_nodes & (1 << DRM_NODE_RENDER))) {
    return NULL;
  }
  const char* name = native->nodes[DRM_NODE_RENDER];
  return name;
}

struct vt_device_t* 
vt_session_device_from_fd_drm(struct vt_session_t* session, uint32_t fd) {
  if(!session || !session->user_data) return NULL;
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 
  struct vt_device_t* dev;
  wl_list_for_each(dev, &session_drm->devices, link) {
    if (dev->fd != fd) continue;
    return dev;
  }
  return NULL;
}

static void on_found_gpu(struct wl_listener *listener, void *data) {
  struct wait_for_gpu_handler_t* handler = wl_container_of(listener, handler, listener);
  handler->found = true;
}

// Largely inspired from lé wlroots backends/session/session.c
uint32_t 
vt_session_enumerate_cards_drm(struct vt_session_t* session, struct vt_device_t** devs, const uint32_t max_devs) {
  if(!session) return false;
  
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  struct udev_enumerate* enumerate;
  if(!(enumerate = _vt_session_drm_udev_enumerate_cards(session))) return false;

  // If no cards were enumerated, we wait 10s for primary cards being added to the udev table. 
  // (e.g the user happend to plug in his GPU within the next ten seconds after starting the compositor.)
  if(!udev_enumerate_get_list_entry(enumerate)) {
    udev_enumerate_unref(enumerate);
    VT_TRACE(session->comp->log, "No DRM devices were found, we now wait for a DRM card device...");

    // Waiting for the "add" signal 
    struct wait_for_gpu_handler_t handler;
    handler.listener.notify = on_found_gpu;
    wl_signal_add(&session_drm->ev_drm_add_card, &handler.listener);

    uint64_t start_time = vt_util_get_time_msec();
    int32_t max_wait_time = 10000; // ms
    while(!handler.found) {
      if(wl_event_loop_dispatch(session->comp->wl.evloop, max_wait_time) < 0) {
        VT_ERROR(session->comp->log, "Failed to wait for a DRM card device.");
        return false;
      }
      uint64_t now = vt_util_get_time_msec();
      if (now >= start_time + max_wait_time) break;
      max_wait_time = (int32_t)(start_time + max_wait_time - now);
    }

    // Not waiting for the "add" signal anymore
    wl_list_remove(&handler.listener.link);

    // If there were no GPUs found even after the 10s, then why should we care anymore
    if(!(enumerate = _vt_session_drm_udev_enumerate_cards(session))) return false;
  }
  struct udev_list_entry *entry;
  uint32_t i = 0;

  udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
    if (i == max_devs) break;

    bool is_boot_vga = false;

    const char* path = udev_list_entry_get_name(entry);
    struct udev_device* udev_dev = udev_device_new_from_syspath(session_drm->udev, path);
    if (!udev_dev) continue;

    const char *seat = udev_device_get_property_value(udev_dev, "ID_SEAT");
    if (!seat) seat = "seat0";

    // If the card is associated with a different seat, we will not use it
    if (session_drm->seat_name[0] && strcmp(session_drm->seat_name, seat) != 0) {
      udev_device_unref(udev_dev);
      continue;
    }

    // Check if the card we found is a boot VGA card 
    // (it's basically the GPU the firmware chose as the primary display device when the system started).
    // So we should prioritise it.
    struct udev_device* udev_dev_pci = udev_device_get_parent_with_subsystem_devtype(udev_dev, "pci", NULL);
    if (udev_dev_pci) {
      const char *id = udev_device_get_sysattr_value(udev_dev_pci, "boot_vga");
      if (id && strcmp(id, "1") == 0) {
        is_boot_vga = true;
      }
    }

    // Check if the device supports KMS and open it  
    struct vt_device_t* dev = _vt_session_drm_open_device_if_kms(session, udev_device_get_devnode(udev_dev));
    if (!dev) {
      udev_device_unref(udev_dev);
      continue;
    }

    udev_device_unref(udev_dev);

    // If the card is valid and passed all the above checks, we 
    // add it to the list of devices.
    devs[i] = dev;

    // If the card is the boot VGA card, we insert it as the 
    // first card in our list (to show our love for it).
    if (is_boot_vga) {
      struct vt_device_t* tmp = devs[0];
      devs[0] = devs[i];
      devs[i] = tmp;
    }

    ++i;
  }
  udev_enumerate_unref(enumerate);

  return i;

}

bool
vt_session_switch_vt_drm(struct vt_session_t* session, uint32_t vt) {
  if(!session) return false;
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  if(libseat_switch_session(session_drm->seat, vt) < 0) {
    VT_ERROR(session->comp->log, "Failed to switch to VT session %i.", vt);
    return false;
  }

  return true;
}
