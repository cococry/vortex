#pragma once

#include <stdbool.h>

#include <pixman.h>
#include <wayland-util.h>

#include "../input/input.h"
#include "../input/wl_seat.h"
#include "session.h"
#include "util.h"

#include "../render/dmabuf_attr.h"

#define BACKEND_DATA(b, type) ((type *)((b)->user_data))
#define VT_MAX_DAMAGE_RECTS   64

#define VT_ALLOC(c, size)       vt_util_alloc(&(c)->arena, (size))
#define VT_ALLOC_FRAME(c, size) vt_util_alloc(&(c)->frame_arena, (size))

struct vt_renderer_t;
struct vt_surface_t;
struct vt_backend_t;
struct vt_output_t;

struct log_state_t {
  FILE *stream;
  bool  verbose, quiet;
};

struct wl_state_t {
  struct wl_display    *dsp;
  struct wl_event_loop *evloop;
  struct wl_compositor *compositor;
};

enum vt_backend_platform_t {
  VT_BACKEND_DRM_GBM = 0,
  VT_BACKEND_WAYLAND,
  VT_BACKEND_SURFACELESS,
};

struct vt_region_t {
  pixman_region32_t       region;
  struct vt_compositor_t *comp;
};

struct vt_backend_interface_t {
  bool (*init)(struct vt_backend_t *backend);
  bool (*is_dmabuf_importable)(struct vt_backend_t     *backend,
                               struct vt_dmabuf_attr_t *attr,
                               int32_t                  device_fd);
  bool (*handle_frame)(struct vt_backend_t *backend,
                       struct vt_output_t  *output);
  bool (*prepare_output_frame)(struct vt_backend_t *backend,
                               struct vt_output_t  *output);
  bool (*terminate)(struct vt_backend_t *backend);
};

struct vt_backend_t {
  void (*on_output_change)(struct vt_backend_t *backend,
                           struct vt_output_t  *changed);
  void                         *user_data;
  struct vt_backend_interface_t impl;

  struct vt_compositor_t *comp;

  enum vt_backend_platform_t platform;
};

enum vt_output_mode_aspect_ratio_t {
  WESTON_MODE_PIC_AR_NONE = 0,    /* DRM_MODE_PICTURE_ASPECT_NONE */
  WESTON_MODE_PIC_AR_4_3 = 1,     /* DRM_MODE_PICTURE_ASPECT_4_3 */
  WESTON_MODE_PIC_AR_16_9 = 2,    /* DRM_MODE_PICTURE_ASPECT_16_9 */
  WESTON_MODE_PIC_AR_64_27 = 3,   /* DRM_MODE_PICTURE_ASPECT_64_27 */
  WESTON_MODE_PIC_AR_256_135 = 4, /* DRM_MODE_PICTURE_ASPECT_256_135*/
};

struct vt_output_mode_t {
  uint32_t flags;
  /** Picture aspect ratio.*/
  enum vt_output_mode_aspect_ratio_t aspect_ratio;
  int32_t                            width;   /**< Width in pixels. */
  int32_t                            height;  /**< Height in pixels. */
  uint32_t                           refresh; /**< Refresh rate in mHz. */
  struct wl_list                     link; /**< in weston_output::mode_list */
};

struct vt_output_t {
  struct wl_list       link_local, link_global;
  struct vt_backend_t *backend;
  void                *native_window;
  void                *render_surface;

  uint32_t width, height;
  int32_t  x, y;
  float    refresh_rate;
  uint32_t format, id;

  bool needs_repaint, repaint_pending, resize_pending;

  void *user_data, *user_data_render;

  struct wl_event_source *repaint_source;

  pixman_region32_t damage;

  pixman_box32_t cached_damage[VT_MAX_DAMAGE_RECTS];
  int32_t        n_damage_boxes;
  bool           needs_damage_rebuild;

  struct {
    struct wl_global *global;
    struct wl_list resources;
  } proto;

  uint32_t transform;
  int32_t  native_scale;
  int32_t  current_scale;
  int32_t  original_scale;

  struct {
    int32_t mm_width;
    int32_t mm_height;

    // WL_OUTPUT_TRANSFORM 
    uint32_t transform;

    char    *make;
    char    *model;
    char    *name;

    char    *serial_number; 
    uint32_t subpixel;

    struct wl_list modes;
  } physical;
};

struct vt_compositor_t {
  struct vt_arena_t arena, frame_arena;

  struct wl_state_t     wl;
  struct vt_backend_t  *backend;
  struct vt_renderer_t *renderer;
  struct log_state_t    log;

  struct wl_list surfaces;

  bool running, suspended;
  bool sent_frame_cbs, any_frame_cb_pending;

  uint32_t    n_virtual_outputs;
  const char *_cmd_line_backend_path;

  struct wl_list outputs;

  struct vt_session_t       *session;
  struct vt_seat_t          *seat;
  struct vt_input_backend_t *input_backend;

  bool have_proto_dmabuf, have_proto_dmabuf_explicit_sync;

  struct vt_surface_t *root_cursor;

  struct vt_scene_node_t *root_node;
};

typedef bool (*backend_implement_func_t)(struct vt_compositor_t *comp);
