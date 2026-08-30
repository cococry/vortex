#include "wl_output.h"
#include "src/core/core_types.h"

#define _SUBSYS_NAME "WL_OUTPUT"


static void output_release(struct wl_client   *client,
                           struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static void unbind_resource(struct wl_resource *resource) {
  wl_list_remove(wl_resource_get_link(resource));
}

static const struct wl_output_interface output_interface = {
    output_release,
};

static void _bind_output(struct wl_client *client, void *data, uint32_t version,
                         uint32_t id) {

  struct wl_resource *res =
      wl_resource_create(client, &wl_output_interface, version, id);

  if (!res) {
    wl_resource_post_no_memory(res);
    return;
  }

  struct vt_output_t *output = data;

  if (!output) {
    wl_resource_set_implementation(res, &output_interface, NULL, NULL);
    return;
  }

  wl_list_insert(&output->proto.resources, wl_resource_get_link(res));

  wl_resource_set_implementation(res, &output_interface, output,
                                 unbind_resource);

  wl_output_send_geometry(res, output->x, output->y, output->physical.mm_width,
                          output->physical.mm_height, output->physical.subpixel,
                          output->physical.make, output->physical.model,
                          output->physical.transform);

  if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
    wl_output_send_scale(res, output->current_scale);

  struct vt_output_mode_t *mode;
  wl_list_for_each(mode, &output->physical.modes, link) {
    wl_output_send_mode(res, mode->flags, mode->width, mode->height,
                        mode->refresh);
  }
	if (version >= WL_OUTPUT_NAME_SINCE_VERSION)
		wl_output_send_name(res, output->physical.name);

	if (version >= WL_OUTPUT_DESCRIPTION_SINCE_VERSION)
		wl_output_send_description(res, output->physical.model);

	if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
		wl_output_send_done(res);
    VT_TRACE(
      output->backend->comp->log,
      "Binding wl_output: "
      "output=%p resource=%p client=%p id=%u version=%u "
      "pos=(%d,%d) "
      "physical=%dx%d mm "
      "subpixel=%d "
      "make=\"%s\" "
      "model=\"%s\" "
      "name=\"%s\" "
      "transform=%d "
      "scale=%d "
      "modes=%d",
      (void *)output,
      (void *)res,
      (void *)client,
      id,
      version,
      output->x,
      output->y,
      output->physical.mm_width,
      output->physical.mm_height,
      output->physical.subpixel,
      output->physical.make ? output->physical.make : "(null)",
      output->physical.model ? output->physical.model : "(null)",
      output->physical.name ? output->physical.name : "(null)",
      output->physical.transform,
      output->current_scale,
      wl_list_length(&output->physical.modes));

}

bool vt_proto_wl_output_init(struct vt_output_t *output) {
  if (!output || !output->backend)
    return false;
  output->proto.global =
      wl_global_create(output->backend->comp->wl.dsp, &wl_output_interface, 4,
                       output, _bind_output);

  return true;
}
