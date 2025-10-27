#pragma once

#include "backend.h"

typedef struct vt_scene_node_t vt_scene_node_t;

struct vt_scene_node_t {

  vt_scene_node_t* parent;
  vt_scene_node_t** childs;
  uint32_t child_count;
  uint32_t _child_cap;

  float x, y;
  float w, h;
};

vt_scene_node_t* vt_scene_node_create(struct vt_compositor_t* c, float x, float y, float w, float h);

bool vt_scene_node_add_child(struct vt_compositor_t* c, vt_scene_node_t* node, vt_scene_node_t* child);
