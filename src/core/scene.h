#pragma once

#include "core_types.h"

struct vt_scene_node_t {

  struct vt_scene_node_t* parent;
  struct vt_scene_node_t** childs;
  uint32_t child_count;
  uint32_t _child_cap;

  float x, y;
  float w, h;
};

struct vt_scene_node_t* vt_scene_node_create(struct vt_compositor_t* c, float x, float y, float w, float h);

bool vt_scene_node_add_child(struct vt_compositor_t* c, struct vt_scene_node_t* node, struct vt_scene_node_t* child);
