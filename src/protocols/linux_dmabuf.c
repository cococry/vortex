#include "linux_dmabuf.h"

#include <linux-dmabuf-v1-server-protocol.h>
#include <wayland-util.h>

struct vt_proto_linux_dmabuf_v1_t {
  struct wl_global* global;

  struct wl_listener dsp_destroy;
};

static struct vt_proto_linux_dmabuf_v1_t* proto;

static void _linux_dmabuf_v1_bind(struct wl_client *client, void *data,
  uint32_t version, uint32_t id);

static void _linux_dmabuf_v1_destroy(struct vt_proto_linux_dmabuf_v1_t* dmabuf);

static void _linux_dmabuf_v1_handle_dsp_destroy(struct wl_listener* listener, void* data);

void 
_linux_dmabuf_v1_bind(struct wl_client *client, void *data,
  uint32_t version, uint32_t id) {

}

void 
_linux_dmabuf_v1_destroy(struct vt_proto_linux_dmabuf_v1_t* dmabuf) {
  // TODO
}

void 
_linux_dmabuf_v1_handle_dsp_destroy(struct wl_listener* listener, void* data) {
  struct vt_proto_linux_dmabuf_v1_t* dmabuf = wl_container_of(listener, dmabuf, dsp_destroy);
  _linux_dmabuf_v1_destroy(dmabuf);
}


bool 
vt_proto_linux_dmabuf_v1_init(struct vt_compositor_t* comp, uint32_t version) {
  if(!(proto = VT_ALLOC(comp, sizeof(*proto)))) return false;

  if(!(proto->global = wl_global_create(comp->wl.dsp, &zwp_linux_dmabuf_v1_interface,
     4, proto, _linux_dmabuf_v1_bind))) return false;

  proto->dsp_destroy.notify = _linux_dmabuf_v1_handle_dsp_destroy;
  wl_display_add_destroy_listener(comp->wl.dsp, &proto->dsp_destroy);

}
