#pragma once

#include "core_types.h"

enum vt_scene_node_type_t {
  VT_SCENE_NODE_ROOT = 0,
  VT_SCENE_NODE_SURFACE = 0,
  VT_SCENE_NODE_RECT
};


struct vt_scene_node_t {

  struct vt_scene_node_t* parent;
  struct vt_scene_node_t** childs;
  uint32_t child_count;
  uint32_t _child_cap;

  float x, y;
  float w, h;
  uint32_t color;

  struct vt_surface_t* surf;

  enum vt_scene_node_type_t type;
};

typedef bool (*vt_scene_node_filter_func_t)(struct vt_scene_node_t* node);


struct vt_scene_node_t* vt_scene_node_create(struct vt_compositor_t* c, float x, float y, float w, float h,
    enum vt_scene_node_type_t type, struct vt_surface_t* surf);

bool vt_scene_node_add_child(struct vt_compositor_t* c, struct vt_scene_node_t* node, struct vt_scene_node_t* child);

struct vt_renderer_t;
struct vt_output_t;
void vt_scene_node_render(struct vt_renderer_t* renderer,  struct vt_output_t* output, struct vt_scene_node_t* node, bool care_for_damage, vt_scene_node_filter_func_t filter); 

void vt_scene_render(struct vt_renderer_t* renderer,  struct vt_output_t* output, struct vt_scene_node_t* root); 
