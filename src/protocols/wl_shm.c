#include <linux/types.h>

#include <stdint.h>
#include "wl_shm.h"
#include "src/core/util.h"

#define fourcc_code(a, b, c, d) ((__u32)(a) | ((__u32)(b) << 8) | \
				 ((__u32)(c) << 16) | ((__u32)(d) << 24))

#define _VT_DRM_FORMAT_XRGB8888	fourcc_code('X', 'R', '2', '4') /* [31:0] x:R:G:B 8:8:8:8 little endian */
#define _VT_DRM_FORMAT_ARGB8888	fourcc_code('A', 'R', '2', '4') /* [31:0] A:R:G:B 8:8:8:8 little endian */

bool vt_proto_wl_shm_init(
    struct vt_compositor_t* comp,
    uint32_t* drm_formats, uint32_t n_formats) {
  if(!comp || !comp->wl.dsp || !n_formats) return false;
  
  bool has_argb = false, has_xrgb = false;
  for (size_t i = 0; i < n_formats; i++) {
    if(drm_formats[i] == _VT_DRM_FORMAT_ARGB8888) has_argb = true;
    if(drm_formats[i] == _VT_DRM_FORMAT_XRGB8888) has_xrgb = true;
  }
  if(!(has_argb && has_xrgb)) {
    log_fatal(comp->log, "VT_PROTO_WL_SHM: We do not have the minimal required protocols ARGB8888 and XRGB8888.");
    return false; // cosmetic 
  }

  wl_display_init_shm(comp->wl.dsp);

  for(uint32_t i = 0; i < n_formats; i++) {
    wl_display_add_shm_format(comp->wl.dsp, vt_util_convert_drm_format_to_wl_shm(drm_formats[i]));
  }

  return true;
}
