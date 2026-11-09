#pragma once

#ifdef TRACY_ENABLE
#include <time.h>
#include <tracy/TracyC.h>

static inline void vt_profile_scope_end(TracyCZoneCtx *zone) {
  TracyCZoneEnd(*zone);
}

#define VT_PROFILE_CONCAT_IMPL(a, b) a##b
#define VT_PROFILE_CONCAT(a, b)      VT_PROFILE_CONCAT_IMPL(a, b)
#define VT_PROFILE_SCOPE_IMPL(id, name)                                        \
  static const struct ___tracy_source_location_data VT_PROFILE_CONCAT(         \
      vt_profile_location_, id) = {name, __func__, __FILE__, __LINE__, 0};     \
  TracyCZoneCtx VT_PROFILE_CONCAT(vt_profile_zone_, id)                        \
      __attribute__((cleanup(vt_profile_scope_end), unused)) =                 \
          ___tracy_emit_zone_begin(                                            \
              &VT_PROFILE_CONCAT(vt_profile_location_, id), 1)

#ifndef VT_PROFILE_COMPARISON
#define VT_PROFILE_SCOPE()           VT_PROFILE_SCOPE_IMPL(__COUNTER__, NULL)
#define VT_PROFILE_SCOPE_NAMED(name) VT_PROFILE_SCOPE_IMPL(__COUNTER__, name)

#define VT_PROFILE_BEGIN(zone) TracyCZone(zone, 1)
#define VT_PROFILE_END(zone)   TracyCZoneEnd(zone)

#else
#define VT_PROFILE_SCOPE()
#define VT_PROFILE_SCOPE_NAMED(name)
#define VT_PROFILE_BEGIN(zone)
#define VT_PROFILE_END(zone)
#endif

#ifdef VT_PROFILE_COMPARISON
/* Align coarse zones with the external workload's CLOCK_MONOTONIC window.
 * The same timestamp event is emitted by both compositors. */
#define VT_PROFILE_COMPARE_IMPL(id, name)                                      \
  VT_PROFILE_SCOPE_IMPL(id, name);                                             \
  struct timespec VT_PROFILE_CONCAT(vt_profile_clock_, id);                    \
  clock_gettime(CLOCK_MONOTONIC, &VT_PROFILE_CONCAT(vt_profile_clock_, id));   \
  TracyCZoneValue(                                                             \
      VT_PROFILE_CONCAT(vt_profile_zone_, id),                                 \
      (uint64_t)VT_PROFILE_CONCAT(vt_profile_clock_, id)                       \
              .tv_sec *UINT64_C(1000000000) +                                  \
          (uint64_t)VT_PROFILE_CONCAT(vt_profile_clock_, id).tv_nsec)
#define VT_PROFILE_COMPARE(name) VT_PROFILE_COMPARE_IMPL(__COUNTER__, name)
#else
#define VT_PROFILE_COMPARE(name)
#endif

#else

#define VT_PROFILE_SCOPE()
#define VT_PROFILE_SCOPE_NAMED(name)
#define VT_PROFILE_BEGIN(zone)
#define VT_PROFILE_END(zone)

#define VT_PROFILE_COMPARE(name)

#endif
