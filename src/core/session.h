#pragma once

#include <sys/types.h>
#include "core_types.h"

struct vt_session_t;

struct vt_device_t {
  int32_t fd, device_id;
  __dev_t dev;
  struct wl_list link;
  char path[64];

  void* user_data;
};

struct vt_session_interface_t {
  bool (*init)(struct vt_session_t* session);
  bool (*terminate)(struct vt_session_t* session);
  bool (*open_device)(struct vt_session_t* session, struct vt_device_t* dev, const char* path);
  bool (*manage_device)(struct vt_session_t* session, struct vt_device_t* dev);
  bool (*close_device)(struct vt_session_t* session, struct vt_device_t* dev); 
  bool (*unmanage_device)(struct vt_session_t* session, struct vt_device_t* dev);
  struct vt_device_t* (*device_from_fd)(struct vt_session_t* session, uint32_t fd); 
};

struct vt_session_t {
  struct vt_compositor_t* comp;
  
  struct wl_signal ev_session_terminate;

  bool active;

  struct vt_session_interface_t impl;

  void* user_data, *native_handle;
  char name[64];
};


