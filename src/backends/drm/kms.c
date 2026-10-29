#include "src/core/profiling.h"
#include "kms.h"
#include "cursor.h"

void drm_kms_commit_snapshot_cursor(struct vt_drm_kms_commit_t *commit) {
  VT_PROFILE_SCOPE();
  assert(commit && commit->output);

  if (commit->cursor_submitted)
    return;

  struct vt_drm_cursor_state_t *cursor = &commit->output->cursor;

  commit->cursor_submitted = true;
  commit->cursor_x = cursor->x;
  commit->cursor_y = cursor->y;
  commit->cursor_visible = cursor->visible;

  if (cursor->visible && cursor->image)
    commit->cursor_image = drm_cursor_image_ref(cursor->image);
}

void drm_kms_commit_release_cursor(struct vt_drm_kms_commit_t *commit) {
  VT_PROFILE_SCOPE();
  if (!commit)
    return;

  if (commit->cursor_image)
    drm_cursor_image_unref(&commit->cursor_image);

  commit->cursor_submitted = false;
}
