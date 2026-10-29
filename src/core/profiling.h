#pragma once

#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>

static inline void vt_profile_scope_end(TracyCZoneCtx *zone) {
  TracyCZoneEnd(*zone);
}

#define VT_PROFILE_CONCAT_IMPL(a, b) a##b
#define VT_PROFILE_CONCAT(a, b) VT_PROFILE_CONCAT_IMPL(a, b)
#define VT_PROFILE_SCOPE_IMPL(id, name)                                      \
  static const struct ___tracy_source_location_data                         \
      VT_PROFILE_CONCAT(vt_profile_location_, id) = {                       \
          name, __func__, __FILE__, __LINE__, 0};                           \
  TracyCZoneCtx VT_PROFILE_CONCAT(vt_profile_zone_, id)                       \
      __attribute__((cleanup(vt_profile_scope_end), unused)) =              \
          ___tracy_emit_zone_begin(                                        \
              &VT_PROFILE_CONCAT(vt_profile_location_, id), 1)

#define VT_PROFILE_SCOPE() VT_PROFILE_SCOPE_IMPL(__COUNTER__, NULL)
#define VT_PROFILE_SCOPE_NAMED(name) VT_PROFILE_SCOPE_IMPL(__COUNTER__, name)

#define VT_PROFILE_BEGIN(zone) TracyCZone(zone, 1)
#define VT_PROFILE_END(zone) TracyCZoneEnd(zone)

#else

#define VT_PROFILE_SCOPE()
#define VT_PROFILE_SCOPE_NAMED(name)
#define VT_PROFILE_BEGIN(zone)
#define VT_PROFILE_END(zone)

#endif
