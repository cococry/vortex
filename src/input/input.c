/*
 * Copyright (c) 2026 Luca Machiedo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "input.h"
#include "src/input/backends/libinput/libinput.h"
#include "src/input/backends/wayland/wayland_input.h"

#define _SUBSYS_NAME "INPUT"

void vt_input_implement(struct vt_input_backend_t       *backend,
                        enum vt_input_backend_platform_t platform) {
  if (!backend)
    return;
  backend->platform = platform;

  if (platform == VT_INPUT_LIBINPUT) {
    backend->impl = (struct vt_input_backend_interface_t){
        .init = input_backend_init_li,
        .terminate = input_backend_terminate_li,
        .resume = input_backend_resume_li,
        .suspend = input_backend_suspend_li};
  } else if (platform == VT_INPUT_WAYLAND) {
    backend->impl = (struct vt_input_backend_interface_t){
        .init = input_backend_init_wl,
        .terminate = input_backend_terminate_wl,
        .resume = input_backend_resume_wl,
        .suspend = input_backend_suspend_wl};
  } else {
    VT_ERROR(backend->comp->log, "Invalid input backend.");
  }
}
