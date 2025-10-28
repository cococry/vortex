#pragma once

#include <libseat.h>
#include <libudev.h>
#include <libinput.h>
#include <wayland-server-core.h>

#include "../../core/session.h"

struct vt_session_drm_t {
  struct libseat* seat;
  struct udev* udev;
  char seat_name[64];
  struct udev_monitor* udev_mon;

  struct wl_signal ev_drm_add_card,
                   ev_drm_change_card, ev_drm_remove_card;
  
  struct wl_signal ev_seat_enable,
                   ev_seat_disable;
  bool _first_libseat_enable;
  
  struct wl_list devices;
};

struct vt_session_drm_event_t {
  const char* device_node_name;
};

bool vt_session_init_drm(struct vt_session_t* session);

bool vt_session_terminate_drm(struct vt_session_t* session);

bool vt_session_open_device_drm(struct vt_session_t* session, struct vt_device_t* dev, const char* path);

bool vt_session_manage_device_drm(struct vt_session_t* session, struct vt_device_t* dev);

bool vt_session_close_device_drm(struct vt_session_t* session, struct vt_device_t* dev); 

bool vt_session_unmanage_device_drm(struct vt_session_t* session, struct vt_device_t* dev);

struct vt_device_t* vt_session_device_from_fd_drm(struct vt_session_t* session, uint32_t fd); 

uint32_t vt_session_enumerate_cards_drm(struct vt_session_t* session, struct vt_device_t** devs, const uint32_t max_devs);

bool vt_session_switch_vt_drm(struct vt_session_t* session, uint32_t vt); 

