
#pragma once

#include "../core/core_types.h"

#include "../input/wl_seat.h"

struct vt_data_offer_t {
  struct wl_resource                    *resource;
  struct vt_data_source_t               *source;
  struct wl_listener                     source_destroy_listener;
  uint32_t                               dnd_actions;
  enum wl_data_device_manager_dnd_action preferred_dnd_action;
  bool                                   in_ask;
};

struct vt_data_source_t {
  struct wl_resource                    *resource;
  struct wl_signal                       destroy_signal;
  struct wl_array                        mime_types;
  struct vt_data_offer_t                *offer;
  struct vt_seat_t                      *seat;
  bool                                   accepted;
  bool                                   actions_set;
  bool                                   set_selection;
  uint32_t                               dnd_actions;
  enum wl_data_device_manager_dnd_action current_dnd_action;
  enum wl_data_device_manager_dnd_action compositor_action;

  void (*accept)(struct vt_data_source_t *source, uint32_t serial,
                 const char *mime_type);
  void (*send)(struct vt_data_source_t *source, const char *mime_type,
               int32_t fd);
  void (*cancel)(struct vt_data_source_t *source);
};

bool vt_proto_wl_data_device_init(struct vt_compositor_t *comp);
