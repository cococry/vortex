#pragma once


typedef enum {
  VT_BACKEND_DRM_GBM = 0,
  VT_BACKEND_WAYLAND,
  VT_BACKEND_SURFACELESS,
} vt_backend_platform_t;

#include "backend.h"

struct vt_session_t;

struct vt_session_interface_t {
  bool (*init)(struct vt_session_t* session);
  bool (*terminate)(struct vt_session_t* session);
};

struct vt_session_t {
  struct vt_compositor_t* comp;
  
  struct wl_signal ev_session_terminate;

  bool active;

  struct vt_session_interface_t impl;

  void* user_data;
};


