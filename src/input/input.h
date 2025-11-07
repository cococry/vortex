#pragma once 

#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>

#include "../core/core_types.h"

enum vt_input_backend_platform_t {
  VT_INPUT_LIBINPUT = 0,
  VT_INPUT_WAYLAND,
	VT_INPUT_UNKNOWN
};

enum vt_input_key_state_t {
	VT_KEY_STATE_RELEASED = 0,
	VT_KEY_STATE_PRESSED = 1,
	VT_KEY_STATE_INVALID = 2
};

enum vt_input_pointer_state_t {
	VT_POINTER_STATE_RELEASED = 0,
	VT_POINTER_STATE_PRESSED = 1,
	VT_POINTER_STATE_INVALID = 2
};

struct vt_kb_modifiers_t {
  uint32_t shift;
  uint32_t ctrl;
  uint32_t alt;
  uint32_t super;
  uint32_t caps;
};

struct vt_input_backend_t;

struct vt_input_backend_interface_t {
  bool (*init)(struct vt_input_backend_t* backend, void* native_handle);
  bool (*terminate)(struct vt_input_backend_t* backend); 
  bool (*suspend)(struct vt_input_backend_t* backend);
  bool (*resume)(struct vt_input_backend_t* backend); 
};

struct vt_input_backend_t {
  enum vt_input_backend_platform_t platform;
  struct vt_input_backend_interface_t impl;

  struct vt_compositor_t* comp;
  void* user_data;

  struct vt_kb_modifiers_t mods;
  struct xkb_context* kb_context;
  struct xkb_keymap* keymap;
  struct xkb_state* kb_state;
};

void vt_input_implement(struct vt_input_backend_t* backend, enum vt_input_backend_platform_t platform);
