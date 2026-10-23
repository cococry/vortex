#define _POSIX_C_SOURCE 200809L
#define VK_USE_PLATFORM_WAYLAND_KHR

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>

#include "vt-background-v1-client-protocol.h"
#include <runara/runara.h>
#include <runara/vulkan.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../subprojects/runara/examples/stb_image.h"

#define IMAGE_WIDTH 200.0f
#define IMAGE_SPEED 120.0f

struct background {
  struct wl_display             *display;
  struct wl_compositor          *compositor;
  struct wl_registry            *registry;
  struct wl_output              *output;
  uint32_t                       output_name;
  struct wl_surface             *surface;
  struct vt_background_shell_v1 *shell;
  struct vt_background_v1       *background;
  struct wl_callback            *frame;
  uint32_t                       width, height;
  struct rn_state_t             *renderer;
  struct rn_image_t              images[2];
  float                          image_heights[2];
  unsigned                       uploaded;
};

static volatile sig_atomic_t running = 1;

static void stop(int signo) {
  (void)signo;
  running = 0;
}

static void configure(void *data, struct vt_background_v1 *background,
                      uint32_t width, uint32_t height) {
  (void)background;
  struct background *app = data;
  app->width = width;
  app->height = height;
}

static const struct vt_background_v1_listener background_listener = {
    .configure = configure,
};

static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version) {
  (void)version;
  struct background *app = data;
  if (!strcmp(interface, wl_compositor_interface.name) && !app->compositor)
    app->compositor =
        wl_registry_bind(registry, name, &wl_compositor_interface, 1);
  else if (!strcmp(interface, wl_output_interface.name) && !app->output) {
    app->output = wl_registry_bind(registry, name, &wl_output_interface, 1);
    app->output_name = name;
  } else if (!strcmp(interface, vt_background_shell_v1_interface.name))
    app->shell =
        wl_registry_bind(registry, name, &vt_background_shell_v1_interface, 1);
}

static void global_remove(void *data, struct wl_registry *registry,
                          uint32_t name) {
  (void)registry;
  struct background *app = data;
  if (name == app->output_name)
    running = 0;
}

static const struct wl_registry_listener registry_listener = {
    .global = global,
    .global_remove = global_remove,
};

static const char *const *extensions(void *data, uint32_t *count) {
  (void)data;
  static const char *const names[] = {
      VK_KHR_SURFACE_EXTENSION_NAME,
      VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
  };
  *count = 2;
  return names;
}

static VkResult create_surface(void *data, VkInstance instance,
                               VkSurfaceKHR *surface) {
  struct background                  *app = data;
  const VkWaylandSurfaceCreateInfoKHR info = {
      .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
      .display = app->display,
      .surface = app->surface,
  };
  return vkCreateWaylandSurfaceKHR(instance, &info, NULL, surface);
}

static void framebuffer_size(void *data, uint32_t *width, uint32_t *height) {
  struct background *app = data;
  *width = app->width;
  *height = app->height;
}

static bool load_image(struct background *app, const char *path, unsigned i) {
  int            width, height, channels;
  unsigned char *pixels =
      stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
  if (!pixels || width <= 0 || height <= 0) {
    fprintf(stderr, "Failed to load '%s': %s\n", path, stbi_failure_reason());
    stbi_image_free(pixels);
    return false;
  }
  rn_result_t result = rn_image_dedicated(
      app->renderer, (uint32_t)width, (uint32_t)height, pixels,
      (size_t)width * (size_t)height * 4, &app->images[i]);
  stbi_image_free(pixels);
  if (result != RN_OK) {
    fprintf(stderr, "Failed to upload '%s' (Runara error %d)\n", path, result);
    return false;
  }
  app->image_heights[i] = IMAGE_WIDTH * (float)height / (float)width;
  app->uploaded++;
  return true;
}

static void frame_done(void *data, struct wl_callback *callback,
                       uint32_t time) {
  (void)time;
  struct background *app = data;
  wl_callback_destroy(callback);
  app->frame = NULL;
}

static const struct wl_callback_listener frame_listener = {.done = frame_done};

static double monotonic_seconds(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static bool draw(struct background *app, double elapsed) {
  const struct rn_box_u32_t bounds = {
      .width = app->width,
      .height = app->height,
  };
  rn_canvas_set_bounds(app->renderer, &bounds);
  rn_result_t result = rn_begin(app->renderer);
  if (result != RN_OK)
    goto fail;

  result = rn_draw_rect(
      app->renderer,
      (struct rn_rect_t){0, 0, (float)app->width, (float)app->height},
      (struct rn_color_t){0.08f, 0.09f, 0.12f, 1.0f});
  float x =
      (float)fmod(elapsed * IMAGE_SPEED, (double)app->width + IMAGE_WIDTH) -
      IMAGE_WIDTH;
  for (unsigned i = 0; i < 10 && result == RN_OK; i++) {
    unsigned         image = i % 2;
    struct rn_rect_t rect = {
        .x = x + i * 20.0f,
        .y =
            ((float)app->height - app->image_heights[image]) * 0.5f + i * 20.0f,
        .width = IMAGE_WIDTH,
        .height = app->image_heights[image],
    };
    result = rn_draw_texture(app->renderer, &app->images[image], rect,
                             (struct rn_color_t){1, 1, 1, 0.8f});
  }
  if (result != RN_OK)
    goto fail;

  /* Request pacing on the same surface that Vulkan commits in rn_end().
   * The WSI swapchain owns buffer allocation, synchronization and release. */
  app->frame = wl_surface_frame(app->surface);
  if (!app->frame)
    return false;
  wl_callback_add_listener(app->frame, &frame_listener, app);
  result = rn_end(app->renderer);
  if (result == RN_OK)
    return true;
fail:
  fprintf(stderr, "Runara Vulkan drawing failed (error %d)\n", result);
  return false;
}

int main(int argc, char **argv) {
  if (argc > 3 || (argc > 1 && !strcmp(argv[1], "--help"))) {
    fprintf(
        argc > 3 ? stderr : stdout,
        "Usage: %s [image.png [image2.jpg]]\n"
        "Animates a Vulkan background on the first advertised Vortex output.\n"
        "Stop with Ctrl-C.\n",
        argv[0]);
    return argc > 3 ? EXIT_FAILURE : EXIT_SUCCESS;
  }
  const char *paths[] = {
      argc > 1 ? argv[1] : BACKGROUND_IMAGE_DIR "/image.png",
      argc > 2 ? argv[2] : BACKGROUND_IMAGE_DIR "/image2.jpg",
  };
  struct background app = {0};
  int               status = EXIT_FAILURE;
  struct sigaction  action = {.sa_handler = stop};
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGTERM, &action, NULL);

  app.display = wl_display_connect(NULL);
  if (!app.display) {
    fprintf(
        stderr,
        "Cannot connect to Wayland; set WAYLAND_DISPLAY to Vortex's socket.\n");
    goto cleanup;
  }
  app.registry = wl_display_get_registry(app.display);
  if (!app.registry)
    goto cleanup;
  wl_registry_add_listener(app.registry, &registry_listener, &app);
  if (wl_display_roundtrip(app.display) < 0)
    goto cleanup;
  if (!app.compositor || !app.output || !app.shell) {
    fprintf(stderr, "Requires wl_compositor, wl_output and "
                    "vt_background_shell_v1; run inside Vortex.\n");
    goto cleanup;
  }
  app.surface = wl_compositor_create_surface(app.compositor);
  if (!app.surface)
    goto cleanup;
  app.background =
      vt_background_shell_v1_get_background(app.shell, app.surface, app.output);
  if (!app.background)
    goto cleanup;
  vt_background_v1_add_listener(app.background, &background_listener, &app);
  if (wl_display_roundtrip(app.display) < 0 || !app.width || !app.height) {
    fprintf(stderr, "Background configuration failed.\n");
    goto cleanup;
  }

  const struct rn_vk_init_options_t vk = {
      .mode = RN_VK_TARGET_MANAGED,
      .platform =
          {
              .user = &app,
              .instance_extensions = extensions,
              .create_surface = create_surface,
              .framebuffer_size = framebuffer_size,
          },
  };
  const struct rn_init_options_t options = {
      .render_backend = RN_RENDER_BACKEND_VULKAN,
      .vk_options = &vk,
  };
  app.renderer = rn_state_init(&options, app.width, app.height);
  if (!app.renderer) {
    fprintf(stderr, "Failed to initialize Runara Vulkan.\n");
    goto cleanup;
  }
  for (unsigned i = 0; i < 2; i++)
    if (!load_image(&app, paths[i], i))
      goto cleanup;

  double start = monotonic_seconds();
  while (running) {
    if (wl_display_dispatch_pending(app.display) < 0)
      goto cleanup;
    if (!running)
      break;
    if (!app.frame && app.width && app.height &&
        !draw(&app, monotonic_seconds() - start))
      goto cleanup;
    if (wl_display_dispatch(app.display) < 0) {
      if (errno == EINTR)
        continue;
      fprintf(stderr, "Wayland connection failed.\n");
      goto cleanup;
    }
  }
  status = EXIT_SUCCESS;

cleanup:
  if (app.renderer) {
    for (unsigned i = 0; i < app.uploaded; i++)
      rn_texture_release(app.renderer, &app.images[i]);
    rn_state_fini(app.renderer);
  }
  if (app.frame)
    wl_callback_destroy(app.frame);
  if (app.background)
    vt_background_v1_destroy(app.background);
  if (app.surface)
    wl_surface_destroy(app.surface);
  if (app.shell)
    vt_background_shell_v1_destroy(app.shell);
  if (app.output)
    wl_output_destroy(app.output);
  if (app.compositor)
    wl_compositor_destroy(app.compositor);
  if (app.registry)
    wl_registry_destroy(app.registry);
  if (app.display) {
    wl_display_flush(app.display);
    wl_display_disconnect(app.display);
  }
  return status;
}
