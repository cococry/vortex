#include "scene.h"
#include "src/core/util.h"

#define _SCENE_CHILD_CAP_INIT 4

vt_scene_node_t*
vt_scene_node_create(struct vt_compositor_t* c, float x, float y, float w, float h) {
  vt_scene_node_t* n = VT_ALLOC(c, sizeof(*n));
  if(!n) {
    VT_ERROR(c->log, "Scene: Failed to allocate scene node.");
    return NULL;
  }

  n->x = x; n->y = y;
  n->w = x; n->h = h;

  return n;
}

bool 
vt_scene_node_add_child(struct vt_compositor_t* c, vt_scene_node_t* node, vt_scene_node_t* child) {
  if(!c || !node || !child) {
    VT_ERROR(c->log, "Scene: One or more parameters of vt_scene_node_add_child() are invalid, cannot add child.");
    return false;
  }
  if(node->child_count >= node->_child_cap) {
    node->_child_cap = !node->_child_cap ? _SCENE_CHILD_CAP_INIT : node->_child_cap * 2;
    node->childs = realloc(
      node->childs,
      sizeof(vt_scene_node_t*) * node->_child_cap);
  }

  node->childs[node->child_count++] = child;

  return true;
}
