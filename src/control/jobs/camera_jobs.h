/*
    This file is part of darktable,
    Copyright (C) 2010-2021 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "common/camera_control.h"
#include "common/film.h"
#include "common/variables.h"
#include "control/control.h"
#include <inttypes.h>

/** Camera capture job */
dt_job_t *dt_camera_capture_job_create(const char *jobcode, uint32_t delay, uint32_t count, uint32_t brackets,
                                       uint32_t steps);

/** Camera import job */
dt_job_t *dt_camera_import_job_create(GList *images, struct dt_camera_t *camera,
                                      const char *time_override);

/** Focus bracketing job.
 * Drives the camera's manual focus stepping action (Sony:
 * /main/actions/manualfocus) between captures, to build a focus stack.
 * \param frames total number of frames to capture, including the first
 *   one; clamped up to a minimum of 2
 * \param step focus step magnitude, clamped to DT_CAMCTL_MANUALFOCUS_STEP_MIN..MAX
 * \param near TRUE moves focus nearer between frames, FALSE moves it
 *   farther (mirrors the sign of the manualfocus action: near is negative,
 *   far is positive)
 * \param settle_ms delay in milliseconds between the focus move and the next capture
 * \param prefocus TRUE to run an autofocus half-press before the first capture
 */
dt_job_t *dt_camera_focus_bracket_job_create(const uint32_t frames,
                                             const uint32_t step,
                                             const gboolean near,
                                             const uint32_t settle_ms,
                                             const gboolean prefocus);

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on

