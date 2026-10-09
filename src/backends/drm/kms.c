#include "kms.h"
#include "cursor.h"

void drm_kms_commit_snapshot_cursor(struct drm_kms_commit_t *commit) {
  assert(commit && commit->output);

  if (commit->cursor_submitted)
    return;

  struct drm_cursor_state_t *cursor = &commit->output->cursor;

  commit->cursor_submitted = true;
  commit->cursor_x = cursor->x;
  commit->cursor_y = cursor->y;
  commit->cursor_visible = cursor->visible;

  if (cursor->visible && cursor->image)
    commit->cursor_image = drm_cursor_image_ref(cursor->image);
}

void drm_kms_commit_release_cursor(struct drm_kms_commit_t *commit) {
  if (!commit)
    return;

  if (commit->cursor_image)
    drm_cursor_image_unref(&commit->cursor_image);

  commit->cursor_submitted = false;
}
