/* Query the compositor's advertised current output modes without a GUI. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
struct output { struct wl_output *proxy; int width, height, refresh, scale, transform; };
static struct output outputs[16];
static int count, watching;
static void geometry(void *d, struct wl_output *o, int32_t x, int32_t y,
 int32_t w, int32_t h, int32_t sub, const char *make, const char *model, int32_t t) {
 (void)o;(void)x;(void)y;(void)w;(void)h;(void)sub;(void)make;(void)model;
 ((struct output *)d)->transform=t;
}
static void mode(void *d, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t r) {
 (void)o; struct output *v=d;
 if (flags & WL_OUTPUT_MODE_CURRENT) { v->width=w; v->height=h; v->refresh=r; }
}
static void done(void *d, struct wl_output *o) {
 (void)o; struct output *v=d;
 if (watching && v->width && v->height) {
  printf("{\"width\":%d,\"height\":%d,\"scale\":%d,\"transform\":%d}\n",v->width,v->height,v->scale,v->transform);
  fflush(stdout);
 }
}
static void scale(void *d, struct wl_output *o, int32_t s) { (void)o; ((struct output *)d)->scale=s; }
static const struct wl_output_listener listener = {.geometry=geometry,.mode=mode,.done=done,.scale=scale};
static void global(void *d, struct wl_registry *registry, uint32_t id, const char *name, uint32_t version) {
 (void)d;
 if (strcmp(name, "wl_output") || count==16) return;
 struct output *v=&outputs[count++]; v->scale=1;
 v->proxy=wl_registry_bind(registry,id,&wl_output_interface,version<2?version:2);
 wl_output_add_listener(v->proxy,&listener,v);
}
static void removed(void *d, struct wl_registry *r, uint32_t id) { (void)d;(void)r;(void)id; }
static const struct wl_registry_listener registry_listener={global,removed};
int main(int argc, char **argv) {
 if (argc>1) { if (argc!=2 || strcmp(argv[1],"--watch")) return 4; watching=1; }
 struct wl_display *d=wl_display_connect(NULL); if (!d) return 1;
 struct wl_registry *r=wl_display_get_registry(d); wl_registry_add_listener(r,&registry_listener,NULL);
 if (wl_display_roundtrip(d)<0 || wl_display_roundtrip(d)<0 || !count) return 2;
 if (watching) { while (wl_display_dispatch(d)>=0) {} return 0; }
 printf("[");
 for (int i=0;i<count;i++) {
  if (!outputs[i].width || !outputs[i].height) return 3;
  printf("%s{\"width\":%d,\"height\":%d,\"refresh_mhz\":%d,\"scale\":%d,\"transform\":%d}",i?",":"",outputs[i].width,outputs[i].height,outputs[i].refresh,outputs[i].scale,outputs[i].transform);
  wl_output_destroy(outputs[i].proxy);
 }
 puts("]"); wl_registry_destroy(r); wl_display_disconnect(d); return 0;
}
