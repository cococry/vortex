#include "backend.h"

int main(int argc, char** argv) {
  vt_compositor_t* c = calloc(1, sizeof(*c));
  if(!comp_init(c, argc, argv)) return 1;

  comp_run(c);

  if(!comp_terminate(c)) return 1;

  return 0;
}
