#include "src/core/compositor.h"

int main(int argc, char** argv) {
  struct vt_compositor_t* c = calloc(1, sizeof(*c));
  if(!vt_comp_init(c, argc, argv)) return 1;

  vt_comp_run(c);

  if(!vt_comp_terminate(c)) return 1;

  return 0;
}
