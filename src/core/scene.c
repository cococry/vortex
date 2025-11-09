#include "scene.h"
#include "src/core/util.h"

#define _SCENE_CHILD_CAP_INIT 4

#define _SUBSYS_NAME "SCENE"

struct vt_scene_node_t*
vt_scene_node_create(struct vt_compositor_t* c, float x, float y, float w, float h) {
  struct vt_scene_node_t* n = VT_ALLOC(c, sizeof(*n));
  if(!n) {
    VT_ERROR(c->log, "Failed to allocate scene node.");
    return NULL;
  }

  n->x = x; n->y = y;
  n->w = x; n->h = h;

  return n;
}

bool 
vt_scene_node_add_child(struct vt_compositor_t* c, struct vt_scene_node_t* node, struct vt_scene_node_t* child) {
  if(!c || !node || !child) {
    VT_ERROR(c->log, "One or more parameters of vt_scene_node_add_child() are invalid, cannot add child.");
    return false;
  }
  if(node->child_count >= node->_child_cap) {
    node->_child_cap = !node->_child_cap ? _SCENE_CHILD_CAP_INIT : node->_child_cap * 2;
    node->childs = realloc(
      node->childs,
      sizeof(struct vt_scene_node_t*) * node->_child_cap);
  }

  node->childs[node->child_count++] = child;

  return true;
}
