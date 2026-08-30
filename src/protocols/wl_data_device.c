#define _POSIX_C_SOURCE 200809L
#include "wl_data_device.h"
#include <string.h>
#include <unistd.h>

#define ALL_ACTIONS                                                            \
  (WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |                                    \
   WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE |                                    \
   WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)

static void _create_data_source(struct wl_client   *client,
                                struct wl_resource *resource, uint32_t id);

static void _get_data_device(struct wl_client   *client,
                             struct wl_resource *manager_resource, uint32_t id,
                             struct wl_resource *seat_resource);

static void _client_source_accept(struct vt_data_source_t *source,
                                  uint32_t serial, const char *mime_type);
static void _client_source_send(struct vt_data_source_t *source,
                                const char *mime_type, int32_t fd);

static void _client_source_cancel(struct vt_data_source_t *source);
static void _destroy_data_source(struct wl_resource *resource);

static const struct wl_data_device_manager_interface manager_interface = {
    .create_data_source =  _create_data_source,
    .get_data_device = _get_data_device,
};

static void _data_source_offer(struct wl_client   *client,
                               struct wl_resource *resource, const char *type);
static void _data_source_destroy(struct wl_client   *client,
                                 struct wl_resource *resource);

static void _data_source_set_actions(struct wl_client   *client,
                                     struct wl_resource *resource,
                                     uint32_t            dnd_actions);

static const struct wl_data_source_interface data_source_interface = {
    _data_source_offer, _data_source_destroy, _data_source_set_actions};

static void _destroy_data_source(struct wl_resource *resource) {
  struct vt_data_source_t *source = wl_resource_get_user_data(resource);
  char                   **p;

  wl_signal_emit(&source->destroy_signal, source);

  wl_array_for_each(p, &source->mime_types) free(*p);

  wl_array_release(&source->mime_types);

  free(source);
}

static void _create_data_source(struct wl_client   *client,
                                struct wl_resource *resource, uint32_t id) {
  struct vt_data_source_t *source;

  source = malloc(sizeof(*source));
  if (source == NULL) {
    wl_resource_post_no_memory(resource);
    return;
  }

  source->resource = wl_resource_create(client, &wl_data_source_interface,
                                        wl_resource_get_version(resource), id);
  if (source->resource == NULL) {
    free(source);
    wl_resource_post_no_memory(resource);
    return;
  }

  wl_signal_init(&source->destroy_signal);
  source->accept = _client_source_accept;
  source->send = _client_source_send;
  source->cancel = _client_source_cancel;
  source->offer = NULL;
  source->accepted = false;
  source->seat = NULL;
  source->actions_set = false;
  source->dnd_actions = 0;
  source->current_dnd_action = WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
  source->compositor_action = WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
  source->set_selection = false;

  wl_array_init(&source->mime_types);

  wl_resource_set_implementation(source->resource, &data_source_interface,
                                 source, _destroy_data_source);
}


static void _unbind_data_device(struct wl_resource *resource) {
  wl_list_remove(wl_resource_get_link(resource));
}

static void data_device_start_drag(struct wl_client   *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *source,
                                   struct wl_resource *origin,
                                   struct wl_resource *icon, uint32_t serial) {
  (void)client;
  (void)resource;
  (void)source;
  (void)origin;
  (void)icon;
  (void)serial;
}

static void data_device_set_selection(struct wl_client   *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *source_resource,
                                      uint32_t            serial) {
  (void)client;
  (void)resource;
  (void)source_resource;
  (void)serial;
}

static void data_device_release(struct wl_client   *client,
                                struct wl_resource *resource) {
  (void)client;
  wl_resource_destroy(resource);
}

static const struct wl_data_device_interface data_device_interface = {
    .start_drag = data_device_start_drag,
    .set_selection = data_device_set_selection,
    .release = data_device_release,
};

static void _get_data_device(struct wl_client   *client,
                             struct wl_resource *manager_resource, uint32_t id,
                             struct wl_resource *seat_resource) {
  struct vt_seat_t   *seat = wl_resource_get_user_data(seat_resource);
  struct wl_resource *resource;

  resource = wl_resource_create(client, &wl_data_device_interface,
                                wl_resource_get_version(manager_resource), id);
  if (resource == NULL) {
    wl_resource_post_no_memory(manager_resource);
    return;
  }

  if (seat) {
    wl_list_insert(&seat->drag_resources, wl_resource_get_link(resource));
  } else {
    wl_list_init(wl_resource_get_link(resource));
  }

  wl_resource_set_implementation(resource, &data_device_interface, seat,
                                 _unbind_data_device);
}

static void _client_source_accept(struct vt_data_source_t *source,
                                  uint32_t serial, const char *mime_type) {
  wl_data_source_send_target(source->resource, mime_type);
}

static void _client_source_send(struct vt_data_source_t *source,
                                const char *mime_type, int32_t fd) {
  wl_data_source_send_send(source->resource, mime_type, fd);
  close(fd);
}

static void _client_source_cancel(struct vt_data_source_t *source) {
  wl_data_source_send_cancelled(source->resource);
}

static void _data_source_offer(struct wl_client   *client,
                               struct wl_resource *resource, const char *type) {
  struct vt_data_source_t *source = wl_resource_get_user_data(resource);
  char                   **p;

  p = wl_array_add(&source->mime_types, sizeof *p);
  if (p)
    *p = strdup(type);
  if (!p || !*p)
    wl_resource_post_no_memory(resource);
}

static void _data_source_destroy(struct wl_client   *client,
                                 struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static void _data_source_set_actions(struct wl_client   *client,
                                     struct wl_resource *resource,
                                     uint32_t            dnd_actions) {
  struct vt_data_source_t *source = wl_resource_get_user_data(resource);

  if (source->actions_set) {
    wl_resource_post_error(source->resource,
                           WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
                           "cannot set actions more than once");
    return;
  }

  if (dnd_actions & ~ALL_ACTIONS) {
    wl_resource_post_error(source->resource,
                           WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
                           "invalid action mask %x", dnd_actions);
    return;
  }

  if (source->seat) {
    wl_resource_post_error(source->resource,
                           WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
                           "invalid action change after "
                           "wl_data_device.start_drag");
    return;
  }

  source->dnd_actions = dnd_actions;
  source->actions_set = true;
}

static void _manager_bind(struct wl_client *client, void *data,
                          uint32_t version, uint32_t id) {
  struct wl_resource *resource;

  resource = wl_resource_create(client, &wl_data_device_manager_interface,
                                version, id);
  if (resource == NULL) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(resource, &manager_interface, NULL, NULL);
}
bool vt_proto_wl_data_device_init(struct vt_compositor_t *comp) {
  if (!wl_global_create(comp->wl.dsp, &wl_data_device_manager_interface, 3, NULL,
                       _manager_bind)) {
    return false;
  }

  return true;
}
