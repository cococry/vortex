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

#define _POSIX_C_SOURCE 200809L

#include "src/core/profiling.h"
#include "src/core/buffer.h"
#include "src/core/compositor.h"
#include "src/core/output.h"
#include "src/input/backends/x11/x11_input.h"
#include "src/input/wl_seat.h"
#include "src/protocols/linux_dmabuf.h"
#include "src/protocols/linux_explicit_sync.h"
#include "src/protocols/wl_output.h"
#include "src/protocols/wl_shm.h"
#include "src/render/drm_format.h"
#include "src/render/renderer.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <X11/XKBlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <drm/drm_fourcc.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <xcb/present.h>

#define _SUBSYS_NAME "X11"

#define _X11_DEFAULT_OUTPUT_WIDTH  1280
#define _X11_DEFAULT_OUTPUT_HEIGHT 720

struct x11_backend {
  Display                *display;
  xcb_connection_t       *connection;
  bool                    has_present;
  struct vt_backend_t    *backend;
  struct wl_event_source *events;
  Atom                    wm_protocols, wm_delete;
  Cursor                  hidden_cursor;
};

struct x11_output {
  Window                  window;
  Colormap                colormap;
  struct wl_event_source *frame_timer;
  bool                    frame_pending, mapped;
  int                     pending_width, pending_height;

  /* pressed keys are per output, because x11 sends key event to specific
   * windows, which are x11 outputs */
  uint8_t pressed_keys[256];

  xcb_special_event_t *present_queue;
  xcb_present_event_t  present_event;
  uint32_t             present_serial;
  uint64_t             msc;
};

struct vt_x11_buffer_attachment_t {
  void    *pixels;
  size_t   capacity;
  uint32_t format;
  uint32_t clear_pixel;
  bool     solid;
};

static void _x11_buffer_attachment_destroy(struct vt_buffer_t *buf,
                                           const void *owner, void *ptr);

static struct vt_x11_buffer_attachment_t *
_x11_buffer_attachment_data(struct vt_buffer_t *buf);

static bool _x11_buffer_attachment_get_shm(struct vt_buffer_t   *buf,
                                           struct vt_shm_attr_t *attr);

static bool _x11_buffer_check_size(uint32_t width, uint32_t height,
                                   size_t *size);

static int _x11_parent_frame_done(void *data);

static void _x11_release_keys(struct vt_compositor_t *c, struct x11_output *xo);

static void _x11_parent_dispatch_event(struct x11_backend *x, XEvent *event);

static int _x11_parent_dispatch(int fd, uint32_t mask, void *data);

static void _x11_backend_destroy_output(struct vt_backend_t *backend,
                                        struct vt_output_t  *output);

static bool _x11_backend_create_output(struct vt_backend_t *backend,
                                       XVisualInfo *visual, unsigned id);

static bool _x11_init_dmabuf_feedback(struct vt_compositor_t *c);

static struct vt_buffer_t *
backend_allocate_buffer_x11(struct vt_backend_t *backend, uint32_t width,
                            uint32_t height, uint32_t format);

static bool backend_clear_buffer_x11(struct vt_backend_t *backend,
                                     struct vt_buffer_t *buf, uint32_t color);

static bool backend_prepare_output_frame_x11(struct vt_backend_t *backend,
                                             struct vt_output_t  *output);

static bool backend_handle_frame_x11(struct vt_backend_t *backend,
                                     struct vt_output_t  *output);

static bool backend_terminate_x11(struct vt_backend_t *backend);

static bool backend_init_x11(struct vt_backend_t *backend);

static bool backend_is_dmabuf_importable_x11(struct vt_backend_t     *backend,
                                             struct vt_dmabuf_attr_t *attr,
                                             int32_t device_fd);

static const char x11_allocator_owner;

static const struct vt_buffer_attachment_implementation_t x11_allocator_impl = {
    .destroy = _x11_buffer_attachment_destroy,
};

static const struct vt_buffer_implementation_t x11_buffer_impl = {
    .get_shm = _x11_buffer_attachment_get_shm,
};

static struct vt_output_implementation_t output_impl = {0};

static void _x11_buffer_attachment_destroy(struct vt_buffer_t *buf,
                                           const void *owner, void *ptr) {
  VT_PROFILE_SCOPE();
  (void)buf;
  (void)owner;
  struct vt_x11_buffer_attachment_t *data = ptr;
  free(data->pixels);
  free(data);
}

static struct vt_x11_buffer_attachment_t *
_x11_buffer_attachment_data(struct vt_buffer_t *buf) {
  VT_PROFILE_SCOPE();
  struct vt_buffer_attachment_t *a =
      vt_buffer_find_attachment(buf, &x11_allocator_owner, &x11_allocator_impl);
  return a ? a->data : NULL;
}

static bool _x11_buffer_attachment_get_shm(struct vt_buffer_t   *buf,
                                           struct vt_shm_attr_t *attr) {
  VT_PROFILE_SCOPE();
  struct vt_x11_buffer_attachment_t *data = _x11_buffer_attachment_data(buf);
  if (!data || !attr)
    return false;
  /* this just returns allocated, compositor owned pixel memory created by
   * backend->impl->allocate_buffer. the compositor-owned buffers that implement
   * get_shm are not really returning real shm that was retrieved by some
   * external source, but the CPU-backed pixel buffers that got created by us.
   */
  *attr = (struct vt_shm_attr_t){
      .data = data->pixels,
      .size = data->capacity,
      .width = buf->width,
      .height = buf->height,
      .stride = buf->width * 4,
      .format = data->format == DRM_FORMAT_XRGB8888 ? WL_SHM_FORMAT_XRGB8888
                                                    : WL_SHM_FORMAT_ARGB8888,
  };
  return true;
}

static bool _x11_buffer_check_size(uint32_t width, uint32_t height,
                                   size_t *size) {
  VT_PROFILE_SCOPE();
  if (!width || !height || width > INT32_MAX / 4 || height > INT32_MAX ||
      height > SIZE_MAX / ((size_t)width * 4))
    return false;
  *size = (size_t)width * height * 4;
  return true;
}

static int _x11_parent_frame_done(void *data) {
  VT_PROFILE_SCOPE();
  struct vt_output_t *output = data;
  struct x11_output  *xo = output->user_data;

  xo->frame_pending = false;
  vt_comp_frame_done(output->backend->comp, output, vt_util_get_time_msec());

  if (xo->mapped && output->needs_repaint)
    vt_comp_schedule_repaint(output->backend->comp, output);
  return 0;
}

static void _x11_release_keys(struct vt_compositor_t *c,
                              struct x11_output      *xo) {
  for (unsigned i = 8; i < sizeof(xo->pressed_keys); ++i) {
    if (xo->pressed_keys[i]) {
      vt_seat_handle_key(c->seat, i, VT_KEY_STATE_RELEASED,
                         vt_util_get_time_msec());
      xo->pressed_keys[i] = 0;
    }
  }
}

static void _x11_parent_dispatch_event(struct x11_backend *x, XEvent *event) {
  VT_PROFILE_SCOPE();
  struct vt_compositor_t *c = x->backend->comp;
  if (input_backend_handle_event_x11(c->input_backend, event))
    return;
  struct vt_output_t *output;
  wl_list_for_each(output, &c->outputs, link_global) {
    struct x11_output *xo = output->user_data;
    if (xo->window != event->xany.window)
      continue;
    switch (event->type) {
    case ClientMessage:
      if (event->xclient.message_type == x->wm_protocols &&
          event->xclient.format == 32 &&
          (Atom)event->xclient.data.l[0] == x->wm_delete)
        c->running = false;
      break;
    case ConfigureNotify:
      xo->pending_width = event->xconfigure.width;
      xo->pending_height = event->xconfigure.height;
      vt_comp_schedule_repaint(c, output);
      break;
    case MapNotify:
      xo->mapped = true;
      vt_output_damage_whole(output);
      vt_comp_schedule_repaint(c, output);
      break;
    case UnmapNotify:
      xo->mapped = false;
      break;
    case DestroyNotify:
      xo->window = 0;
      xo->mapped = false;
      c->running = false;
      break;
    case Expose:
      vt_output_damage_whole(output);
      vt_comp_schedule_repaint(c, output);
      break;
    case EnterNotify:
      vt_seat_handle_pointer_motion(c->seat, output->x + event->xcrossing.x,
                                    output->y + event->xcrossing.y,
                                    event->xcrossing.time);
      break;
    case LeaveNotify:
      if (event->xcrossing.mode == NotifyNormal)
        vt_seat_set_pointer_focus(c->seat, NULL, 0, 0);
      break;
    case MotionNotify:
      vt_seat_handle_pointer_motion(c->seat, output->x + event->xmotion.x,
                                    output->y + event->xmotion.y,
                                    event->xmotion.time);
      break;
    case ButtonPress:
    case ButtonRelease: {
      unsigned button = event->xbutton.button;
      bool     pressed = event->type == ButtonPress;
      if (button >= 4 && button <= 7) {
        if (!pressed)
          break;
        unsigned axis = button <= 5 ? WL_POINTER_AXIS_VERTICAL_SCROLL
                                    : WL_POINTER_AXIS_HORIZONTAL_SCROLL;

        int direction = (button == 4 || button == 6) ? -1 : 1;
        struct vt_pointer_axis_event_t scroll = {
            .source = WL_POINTER_AXIS_SOURCE_WHEEL, .has_source = true};
        scroll.axes[axis] =
            (struct vt_pointer_axis_t){.time = event->xbutton.time,
                                       .value = direction * 10.0,
                                       .discrete = direction,
                                       .value120 = direction * 120,
                                       .has_value = true,
                                       .has_discrete = true,
                                       .has_value120 = true};
        vt_seat_handle_pointer_axis(c->seat, &scroll);
      } else {
        uint32_t code = button == 1   ? BTN_LEFT
                        : button == 2 ? BTN_MIDDLE
                        : button == 3 ? BTN_RIGHT
                        : button >= 8 ? BTN_SIDE + button - 8
                                      : 0;
        if (code)
          vt_seat_handle_pointer_button(c->seat, code, pressed,
                                        event->xbutton.time);
      }
      break;
    }
    case KeyPress:
    case KeyRelease: {
      unsigned key = event->xkey.keycode;
      if (key >= sizeof(xo->pressed_keys))
        break;
      bool pressed = event->type == KeyPress;
      /* Detectable autorepeat generates extra presses; Wayland clients repeat.
       */
      if (xo->pressed_keys[key] == pressed)
        break;
      xo->pressed_keys[key] = pressed;
      vt_seat_handle_key(c->seat, key,
                         pressed ? VT_KEY_STATE_PRESSED : VT_KEY_STATE_RELEASED,
                         event->xkey.time);
      break;
    }
    case FocusIn:
      if (event->xfocus.mode == NotifyNormal)
        input_backend_sync_x11(c->input_backend);
      break;
    case FocusOut:
      if (event->xfocus.mode == NotifyNormal)
        _x11_release_keys(c, xo);
      break;
    default:
      break;
    }
    return;
  }
}

static int _x11_parent_dispatch(int fd, uint32_t mask, void *data) {
  VT_PROFILE_SCOPE();
  (void)fd;
  struct x11_backend *x = data;
  if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
    x->backend->comp->running = false;
    return 0;
  }
  while (XPending(x->display)) {
    XEvent event;
    XNextEvent(x->display, &event);
    _x11_parent_dispatch_event(x, &event);
    if (!x->backend->comp->running)
      return 0;
  }

  /* check present queue event streams for NotifyMSC from Present, so we can do
   * frame pacing*/
  struct vt_output_t *output;
  wl_list_for_each(output, &x->backend->comp->outputs, link_global) {
    struct x11_output *xo = output->user_data;
    if (!xo->present_queue)
      continue;
    xcb_generic_event_t *event;
    /* poll for special events (from Present) */
    while ((event =
                xcb_poll_for_special_event(x->connection, xo->present_queue))) {
      xcb_present_complete_notify_event_t *complete = (void *)event;

      if ((event->response_type & 0x7f) == XCB_GE_GENERIC &&
          complete->event_type == XCB_PRESENT_COMPLETE_NOTIFY &&
          complete->kind == XCB_PRESENT_COMPLETE_KIND_NOTIFY_MSC) {
        /* event is NotifyMSC: call frame_done and unarm the watchdog timer
         * watching if no NotifyMSC arrived. */
        xo->msc = complete->msc;
        if (xo->frame_pending && complete->serial == xo->present_serial) {
          wl_event_source_timer_update(xo->frame_timer, 0);
          _x11_parent_frame_done(output);
        }
      }
      free(event);
    }
  }
  return 0;
}

static void _x11_backend_destroy_output(struct vt_backend_t *backend,
                                        struct vt_output_t  *output) {
  struct x11_backend *x = backend->user_data;
  struct x11_output  *xo = output->user_data;
  if (output->commit_source) {
    wl_event_source_remove(output->commit_source);
    output->commit_source = NULL;
  }
  if (xo->frame_timer)
    wl_event_source_remove(xo->frame_timer);
  if (xo->present_queue) {
    if (xo->window)
      xcb_present_select_input(x->connection, xo->present_event, xo->window,
                               XCB_PRESENT_EVENT_MASK_NO_EVENT);
    xcb_unregister_for_special_event(x->connection, xo->present_queue);
  }
  if (output->render_surface)
    backend->comp->renderer->impl.destroy_renderable_output(
        backend->comp->renderer, output);
  if (output->proto.global)
    wl_global_destroy(output->proto.global);
  if (xo->window)
    XDestroyWindow(x->display, xo->window);
  if (xo->colormap)
    XFreeColormap(x->display, xo->colormap);
  output->native_window = NULL;
  vt_output_fini(output);
}

static bool _x11_backend_create_output(struct vt_backend_t *backend,
                                       XVisualInfo *visual, unsigned id) {

  assert(backend && visual);

  struct x11_backend     *x = backend->user_data;
  struct vt_compositor_t *c = backend->comp;
  struct vt_output_t     *output = vt_output_init(backend, &output_impl);
  if (!output)
    return false;

  struct x11_output *xo = VT_ALLOC(c, sizeof(*xo));
  if (!xo)
    return false;
  output->user_data = xo;

  pixman_region32_init(&output->damage);
  wl_list_init(&output->physical.modes);
  wl_list_init(&output->presented_surfaces);
  wl_list_init(&output->proto.resources);
  output->id = id;
  output->refresh_rate = 60;
  output->cursor_mode = VT_CURSOR_MODE_SOFTWARE;
  output->current_scale = output->native_scale = output->original_scale = 1;
  output->physical.make = strdup("Vortex");
  output->physical.model = strdup("X11 Nested Output");
  char name[64];
  snprintf(name, sizeof(name), "VORTEX-X11-%u", id);
  output->physical.name = strdup(name);
  struct vt_output_mode_t *mode = calloc(1, sizeof(*mode));
  if (!mode)
    goto fail;
  if (!output->physical.make || !output->physical.model ||
      !output->physical.name) {
    free(mode);
    goto fail;
  }
  mode->width = _X11_DEFAULT_OUTPUT_WIDTH;
  mode->height = _X11_DEFAULT_OUTPUT_HEIGHT;
  mode->refresh = 60000;
  mode->flags = WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;
  wl_list_insert(&output->physical.modes, &mode->link);

  /* create empty colormap for window */
  xo->colormap =
      XCreateColormap(x->display, RootWindow(x->display, visual->screen),
                      visual->visual, AllocNone);

  /* create X11 window */
  XSetWindowAttributes attrs = {
      .colormap = xo->colormap,
      .border_pixel = 0,
      .event_mask = ExposureMask | StructureNotifyMask | FocusChangeMask |
                    PointerMotionMask | EnterWindowMask | LeaveWindowMask |
                    ButtonPressMask | ButtonReleaseMask | KeyPressMask |
                    KeyReleaseMask};
  xo->window =
      XCreateWindow(x->display, RootWindow(x->display, visual->screen), 0, 0,
                    _X11_DEFAULT_OUTPUT_WIDTH, _X11_DEFAULT_OUTPUT_HEIGHT, 0,
                    visual->depth, InputOutput, visual->visual,
                    CWColormap | CWBorderPixel | CWEventMask, &attrs);
  if (!xo->window)
    goto fail;

  output->native_window = (void *)(uintptr_t)xo->window;

  if (x->has_present) {
    /* generate present event id */
    xo->present_event = xcb_generate_id(x->connection);
    /* registert present queue */
    xo->present_queue = xcb_register_for_special_xge(
        x->connection, &xcb_present_id, xo->present_event, NULL);
    if (!xo->present_queue)
      goto fail;

    /* select to listen for NotifyMSC from X11Present */
    xcb_generic_error_t *error = xcb_request_check(
        x->connection, xcb_present_select_input_checked(
                           x->connection, xo->present_event, xo->window,
                           XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY));
    if (error) {
      free(error);
      goto fail;
    }
  }

  snprintf(name, sizeof(name), "Vortex Nested X11 (%u)", id);
  XStoreName(x->display, xo->window, name);
  XSetWMProtocols(x->display, xo->window, &x->wm_delete, 1);
  XClassHint hint = {.res_name = "vortex", .res_class = "Vortex"};
  XSetClassHint(x->display, xo->window, &hint);
  XDefineCursor(x->display, xo->window, x->hidden_cursor);

  /* fallback timer for when Present is not available */
  xo->frame_timer =
      wl_event_loop_add_timer(c->wl.evloop, _x11_parent_frame_done, output);

  if (!xo->frame_timer ||
      !vt_output_init_geom(output, _X11_DEFAULT_OUTPUT_WIDTH,
                           _X11_DEFAULT_OUTPUT_HEIGHT) ||
      !vt_proto_wl_output_init(output))
    goto fail;

  wl_list_insert(c->outputs.prev, &output->link_global);

  /* setup renderable output, set vsync to false because we handle frame timing
   * ourselves with NotifyMSC or the fallback ~58fps timer. */
  if (!c->renderer->impl.setup_renderable_output(c->renderer, output))
    goto fail;

  c->renderer->impl.set_vsync(c->renderer, false);
  output->resize_pending = true;

  /* map the window */
  XMapWindow(x->display, xo->window);

  return true;
fail:
  _x11_backend_destroy_output(backend, output);
  return false;
}

static bool _x11_init_dmabuf_feedback(struct vt_compositor_t *c) {
  struct vt_dmabuf_feedback_t *fb = vt_dmabuf_feedback_create(c, NULL);
  if (!fb)
    return false;
  bool               ok = false;
  struct vt_device_t device = {.fd = -1};
  struct stat        st;
  if (stat("/dev/dri/renderD128", &st) == 0 || stat("/dev/dri/card0", &st) == 0)
    device.dev = st.st_rdev;
  else {
    device.dev = makedev(0, 0);
    VT_WARN(c->log,
            "Cannot stat() /dev/dri/card0 and /dev/dri/renderD128, falling "
            "back to not providing a main device for DMABUF feedback.");
  }
  fb->dev_main = &device;
  struct vt_dmabuf_tranche_t *tranche =
      vt_dmabuf_feedback_add_tranche(fb, fb->dev_main, 0);
  if (!tranche)
    goto done;

  if (c->renderer->rendering_backend == VT_RENDERING_BACKEND_VULKAN) {
    if (!c->renderer->impl.query_dmabuf_formats_with_renderer(
            c->renderer, &tranche->formats) ||
        !vt_drm_format_array_count(&tranche->formats))
      goto done;
  } else {
    /* Nested sessions must not depend on querying the host GPU's EGL format
     * table. Offer only the basic RGB formats with linear or implicit layout.
     */
    const uint32_t formats[] = {DRM_FORMAT_XRGB8888, DRM_FORMAT_ARGB8888};
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
      if (!vt_drm_format_array_push_pair(&tranche->formats, formats[i],
                                         DRM_FORMAT_MOD_LINEAR) ||
          !vt_drm_format_array_push_pair(&tranche->formats, formats[i],
                                         DRM_FORMAT_MOD_INVALID))
        goto done;
    }
  }

  ok = vt_proto_linux_dmabuf_v1_init(c, fb, 4);
done:
  vt_dmabuf_feedback_fini(fb);
  return ok;
}

// ===================================================
// =================== PUBLIC API ====================
// ===================================================
static struct vt_buffer_t *
backend_allocate_buffer_x11(struct vt_backend_t *backend, uint32_t width,
                            uint32_t height, uint32_t format) {
  VT_PROFILE_SCOPE();
  size_t size;
  if (!_x11_buffer_check_size(width, height, &size) ||
      (format != DRM_FORMAT_XRGB8888 && format != DRM_FORMAT_ARGB8888))
    return NULL;

  struct vt_x11_buffer_attachment_t *data = calloc(1, sizeof(*data));
  if (!data)
    return NULL;
  data->pixels = calloc(1, size);
  data->capacity = size;
  data->format = format;

  /* create buffer */
  struct vt_buffer_t *buf =
      data->pixels
          ? vt_buffer_create(backend->comp, width, height, &x11_buffer_impl)
          : NULL;

  /* add x11 attachment */
  if (!buf || !vt_buffer_add_attachment(buf, &x11_allocator_owner, data,
                                        &x11_allocator_impl)) {
    if (buf)
      vt_buffer_unref(&buf);
    _x11_buffer_attachment_destroy(NULL, NULL, data);
    return NULL;
  }
  return buf;
}

static bool backend_clear_buffer_x11(struct vt_backend_t *backend,
                                     struct vt_buffer_t *buf, uint32_t color) {
  VT_PROFILE_SCOPE();
  (void)backend;
  struct vt_x11_buffer_attachment_t *data =
      buf ? _x11_buffer_attachment_data(buf) : NULL;
  if (!data || buf->uses)
    return false;
  uint32_t *pixels = data->pixels;
  size_t    count = data->capacity / sizeof(*pixels);
  /* RGBA */
  uint32_t pixel = 0xff000000u | (color & 0xffffffu);
  if (data->solid && data->clear_pixel == pixel)
    return true;
  for (size_t i = 0; i < count; ++i)
    pixels[i] = pixel;
  data->solid = true;
  data->clear_pixel = pixel;
  return true;
}

static bool backend_prepare_output_frame_x11(struct vt_backend_t *backend,
                                             struct vt_output_t  *output) {
  VT_PROFILE_SCOPE();
  struct x11_output *xo = output->user_data;
  if (xo->frame_pending || !xo->mapped)
    return false;

  if (xo->pending_width > 0 && xo->pending_height > 0) {

    int w = xo->pending_width;
    int h = xo->pending_height;

    xo->pending_width = 0;
    xo->pending_height = 0;

    if (w != (int)output->width || h != (int)output->height) {

      struct vt_renderer_t *r = backend->comp->renderer;
      /* resize the render output backed by a framebuffer */
      if (!r->impl.resize_renderable_output(r, output, w, h))
        return false;

      output->width = w;
      output->height = h;

      /* update the synthetic physical mode of the virtual output */
      struct vt_output_mode_t *mode =
          wl_container_of(output->physical.modes.next, mode, link);
      mode->width = w;
      mode->height = h;
      output->resize_pending = true;

      int32_t             x = 0;
      struct vt_output_t *other;
      wl_list_for_each(other, &backend->comp->outputs, link_global) {
        /* update x of other outputs according to new width of this output
         * (outputs are logically ordered like the linked list is) */
        other->x = x;
        x += other->width;

        /* send new geometry to wl_output */
        struct wl_resource *res;
        wl_resource_for_each(res, &other->proto.resources) {
          wl_output_send_geometry(
              res, other->x, other->y, other->physical.mm_width,
              other->physical.mm_height, other->physical.subpixel,
              other->physical.make, other->physical.model,
              other->physical.transform);
          if (other == output)
            wl_output_send_mode(res, mode->flags, w, h, mode->refresh);
          if (wl_resource_get_version(res) >= WL_OUTPUT_DONE_SINCE_VERSION)
            wl_output_send_done(res);
        }

        /* damage the output, call the change callback */
        vt_output_damage_whole(other);
        if (backend->on_output_change)
          backend->on_output_change(backend, other);

        /* schedule a repaint */
        vt_comp_schedule_repaint(backend->comp, other);
      }
    }
  }
  return true;
}

static bool backend_handle_frame_x11(struct vt_backend_t *backend,
                                     struct vt_output_t  *output) {
  VT_PROFILE_SCOPE();
  struct x11_backend *x = backend->user_data;
  struct x11_output  *xo = output->user_data;
  XFlush(x->display);
  xo->frame_pending = true;

  /* fallback timeout for when X11 Present is not available */
  int timeout_ms = 17;
  if (xo->present_queue) {
    xcb_present_notify_msc(x->connection, xo->window, ++xo->present_serial,
                           xo->msc ? xo->msc + 1 : 0, 0, 0);
    xcb_flush(x->connection);
    timeout_ms = 100;
  }
  return wl_event_source_timer_update(xo->frame_timer, timeout_ms) == 0;
}

static bool backend_terminate_x11(struct vt_backend_t *backend) {
  struct x11_backend *x = backend->user_data;
  if (!x)
    return true;
  if (x->events)
    wl_event_source_remove(x->events);
  struct vt_output_t *output, *tmp;
  wl_list_for_each_safe(output, tmp, &backend->comp->outputs, link_global)
      _x11_backend_destroy_output(backend, output);
  if (backend->comp->renderer->user_data)
    backend->comp->renderer->impl.destroy(backend->comp->renderer);
  if (x->display) {
    if (x->hidden_cursor)
      XFreeCursor(x->display, x->hidden_cursor);
    XCloseDisplay(x->display);
  }
  backend->comp->session->native_handle = NULL;
  backend->user_data = NULL;
  return true;
}

static bool backend_init_x11(struct vt_backend_t *backend) {
  struct vt_compositor_t *c = backend->comp;
  struct x11_backend     *x = VT_ALLOC(c, sizeof(*x));
  if (!x)
    return false;
  backend->user_data = x;
  x->backend = backend;
  x->display = XOpenDisplay(NULL);
  if (!x->display) {
    VT_ERROR(c->log, "Cannot open X11 display.");
    goto fail;
  }

  c->session->native_handle = x->display;
  snprintf(c->session->name, sizeof(c->session->name), "x11");
  x->connection = XGetXCBConnection(x->display);

  const xcb_query_extension_reply_t *present =
      xcb_get_extension_data(x->connection, &xcb_present_id);

  /* see if this server supports the Present X11 extention. This lets us pace
   * frames using the X server's refresh inteval. */
  if (present && present->present) {
    xcb_present_query_version_reply_t *version =
        xcb_present_query_version_reply(
            x->connection, xcb_present_query_version(x->connection, 1, 0),
            NULL);
    x->has_present = version != NULL;
    free(version);
  }

  VT_INFO(c->log, "Method for frame pacing: %s",
          x->has_present ? "Present MSC" : "Fallback 60 Hz timer");

  x->wm_protocols = XInternAtom(x->display, "WM_PROTOCOLS", False);
  x->wm_delete = XInternAtom(x->display, "WM_DELETE_WINDOW", False);
  Bool detectable = False;
  if (!XkbSetDetectableAutoRepeat(x->display, True, &detectable) ||
      !detectable) {
    VT_ERROR(c->log,
             "X11 server does not support detectable keyboard autorepeat.");
    goto fail;
  }
  char pixel = 0;

  Pixmap bitmap = XCreateBitmapFromData(
      x->display, DefaultRootWindow(x->display), &pixel, 1, 1);
  XColor color = {0};
  /* create hidden cursor to hide the host cursor inside of virtual outputs */
  x->hidden_cursor =
      XCreatePixmapCursor(x->display, bitmap, bitmap, &color, &color, 0, 0);
  XFreePixmap(x->display, bitmap);

  if (!c->renderer->impl.init(backend, c->renderer, x->display))
    goto fail;

  /* get appropriate visual desired by the renderer */
  XVisualInfo *visual;
  XVisualInfo template = {.visualid =
                              c->renderer->_desired_render_buffer_format};
  int count;
  visual = XGetVisualInfo(x->display, VisualIDMask, &template, &count);
  if (!visual || !count) {
    if (visual)
      XFree(visual);
    goto fail;
  }

  bool outputs_ok = true;
  /* create virtual outputs */
  for (uint32_t i = 0; i < c->n_virtual_outputs; ++i) {
    if (!_x11_backend_create_output(backend, visual, i)) {
      outputs_ok = false;
      break;
    }
  }
  XFree(visual);
  if (!outputs_ok)
    goto fail;

  /* Timer pacing avoids blocking all outputs in eglSwapBuffers. */
  c->renderer->impl.set_vsync(c->renderer, false);
  uint32_t formats[] = {DRM_FORMAT_XRGB8888, DRM_FORMAT_ARGB8888};

  if (!vt_proto_wl_shm_init(c, formats, 2))
    goto fail;

  if (c->have_proto_dmabuf && !_x11_init_dmabuf_feedback(c))
    VT_WARN(c->log, "X11 renderer has no DMA-BUF feedback; using SHM clients.");

  if (c->have_proto_dmabuf_explicit_sync &&
      !vt_proto_linux_explicit_sync_v1_init(c, 2))
    goto fail;

  x->events = wl_event_loop_add_fd(c->wl.evloop, ConnectionNumber(x->display),
                                   WL_EVENT_READABLE, _x11_parent_dispatch, x);
  if (!x->events)
    goto fail;

  wl_event_source_check(x->events);

  XFlush(x->display);
  return true;
fail:
  backend_terminate_x11(backend);
  return false;
}

static bool backend_is_dmabuf_importable_x11(struct vt_backend_t     *backend,
                                             struct vt_dmabuf_attr_t *attr,
                                             int32_t device_fd) {
  VT_PROFILE_SCOPE();
  (void)device_fd;
  struct vt_renderer_t *r = backend->comp->renderer;
  return !r->impl.is_dmabuf_importable || r->impl.is_dmabuf_importable(r, attr);
}

bool backend_implement_x11(struct vt_compositor_t *comp) {
  if (!comp || !comp->backend)
    return false;
  comp->backend->platform = VT_BACKEND_X11;
  comp->backend->impl = (struct vt_backend_interface_t){
      .init = backend_init_x11,
      .terminate = backend_terminate_x11,
      .allocate_buffer = backend_allocate_buffer_x11,
      .clear_buffer = backend_clear_buffer_x11,
      .is_dmabuf_importable = backend_is_dmabuf_importable_x11,
      .prepare_output_frame = backend_prepare_output_frame_x11,
      .handle_frame = backend_handle_frame_x11};
  memset(&comp->session->impl, 0, sizeof(comp->session->impl));
  return true;
}
