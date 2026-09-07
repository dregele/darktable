/*
    This file is part of darktable,
    Copyright (C) 2010-2024 darktable developers.

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
#include "control/jobs/camera_jobs.h"
#include "common/darktable.h"
#include "common/collection.h"
#include "common/import_session.h"
#include "common/utility.h"
#include "common/datetime.h"
#include "control/conf.h"
#include "control/jobs/image_jobs.h"
#include "gui/gtk.h"
#include "views/view.h"

#include <glib.h>
#include <stdio.h>

typedef struct dt_camera_shared_t
{
  struct dt_import_session_t *session;
} dt_camera_shared_t;

typedef struct dt_camera_capture_t
{
  dt_camera_shared_t shared;

  /** delay between each capture, 0 no delay */
  uint32_t delay;
  /** count of images to capture, 0==1 */
  uint32_t count;
  /** bracket capture, 0=no bracket */
  uint32_t brackets;

  /** steps for each bracket, only used ig bracket capture*/
  uint32_t steps;

} dt_camera_capture_t;

typedef struct dt_camera_get_previews_t
{
  struct dt_camera_t *camera;
  uint32_t flags;
  struct dt_camctl_listener_t *listener;
  void *data;
} dt_camera_get_previews_t;

typedef struct dt_camera_import_t
{
  dt_camera_shared_t shared;

  GList *images;
  struct dt_camera_t *camera;
  dt_job_t *job;
  double fraction;
  uint32_t import_count;
} dt_camera_import_t;

typedef struct dt_camera_focus_bracket_t
{
  /** total number of frames to capture, including the first one */
  uint32_t frames;
  /** focus step magnitude, 1..7, matches the range of Sony's
   * /main/actions/manualfocus action */
  uint32_t step;
  /** move focus nearer (TRUE) between frames, or farther (FALSE) */
  gboolean near;
  /** delay in ms between the focus move and the next capture, letting
   * the lens settle before the shutter fires */
  uint32_t settle_ms;
  /** run an autofocus half-press before the first capture */
  gboolean prefocus;
  /** hold time in ms for the optional prefocus half-press */
  uint32_t af_hold_ms;
} dt_camera_focus_bracket_t;

static int32_t dt_camera_capture_job_run(dt_job_t *job)
{
  dt_camera_capture_t *params = dt_control_job_get_params(job);
  double fraction = 0;

  int total = params->brackets ? params->count * params->brackets : params->count;
  dt_control_job_set_progress_message(job,
           ngettext("capturing %d image", "capturing %d images", total), total);

  /* try to get exp program mode for nikon */
  char *expprogram = (char *)dt_camctl_camera_get_property(darktable.camctl,
                                                           NULL, "expprogram");

  /* if fail, lets try fetching mode for cannon */
  if(!expprogram)
    expprogram = (char *)dt_camctl_camera_get_property(darktable.camctl,
                                                       NULL, "autoexposuremode");

  /* Fetch all values for shutterspeed and initialize current value */
  GList *values = NULL;
  gconstpointer original_value = NULL;
  const char *cvalue = dt_camctl_camera_get_property(darktable.camctl,
                                                     NULL, "shutterspeed");
  const char *value = dt_camctl_camera_property_get_first_choice(darktable.camctl,
                                                                 NULL, "shutterspeed");

  /* get values for bracketing */
  if(params->brackets
     && expprogram
     && expprogram[0] == 'M'
     && value
     && cvalue)
  {
    do
    {
      // Add value to list
      values = g_list_prepend(values, g_strdup(value));
      // Check if current values is the same as original value, then
      // lets store item ptr
      if(strcmp(value, cvalue) == 0)
        original_value = values->data;
    } while((value = dt_camctl_camera_property_get_next_choice(darktable.camctl,
                                                               NULL, "shutterspeed"))
            != NULL);
  }
  else
  {
    /* if this was an intended bracket capture bail out */
    if(params->brackets)
    {
      dt_control_log(_("please set your camera to manual mode first!"));
      return 1;
    }
  }

  GList *current_value = g_list_find(values, original_value);
  for(uint32_t i = 0; i < params->count; i++)
  {
    // Delay if active
    if(params->delay && !params->brackets) // delay between brackets
      g_usleep(params->delay * G_USEC_PER_SEC);

    for(uint32_t b = 0; b < (params->brackets * 2) + 1; b++)
    {
      // If bracket capture, lets set change shutterspeed
      if(params->brackets)
      {
        if(b == 0)
        {
          // First bracket, step down time with (steps*brackets), also
          // check so we never set the longest shuttertime which would
          // be bulb mode
          for(uint32_t s = 0; s < (params->steps * params->brackets); s++)
            if(g_list_next(current_value) && g_list_next(g_list_next(current_value)))
              current_value = g_list_next(current_value);
        }
        else
        {
          if(params->delay) // delay after previous bracket (no delay for 1st bracket)
            g_usleep(params->delay * G_USEC_PER_SEC);

          // Step up with (steps)
          for(uint32_t s = 0; s < params->steps; s++)
            if(g_list_previous(current_value))
              current_value = g_list_previous(current_value);
        }
      }

      // set the time property for bracket capture
      if(params->brackets && current_value)
        dt_camctl_camera_set_property_string(darktable.camctl, NULL,
                                             "shutterspeed", current_value->data);

      // Capture image
      dt_camctl_camera_capture(darktable.camctl, NULL);

      fraction += 1.0 / total;
      dt_control_job_set_progress(job, fraction);
    }

    // lets reset to original value before continue
    if(params->brackets)
    {
      if(params->delay) // delay after final bracket
        g_usleep(params->delay * G_USEC_PER_SEC);

      current_value = g_list_find(values, original_value);
      dt_camctl_camera_set_property_string(darktable.camctl, NULL,
                                           "shutterspeed", current_value->data);
    }
  }

  // free values
  if(values)
  {
    g_list_free_full(values, g_free);
  }
  return 0;
}

static dt_camera_capture_t *dt_camera_capture_alloc()
{
  dt_camera_capture_t *params = calloc(1, sizeof(dt_camera_capture_t));
  if(!params)
    return NULL;

  // FIXME: unused
  params->shared.session = dt_import_session_new();

  return params;
}

static void dt_camera_capture_cleanup(void *p)
{
  dt_camera_capture_t *params = p;

  dt_import_session_destroy(params->shared.session);

  free(params);
}

dt_job_t *dt_camera_capture_job_create(const char *jobcode,
                                       const uint32_t delay,
                                       const uint32_t count,
                                       const uint32_t brackets,
                                       const uint32_t steps)
{
  dt_job_t *job = dt_control_job_create(&dt_camera_capture_job_run,
                                        "remote capture of image(s)");
  if(!job)
    return NULL;

  dt_camera_capture_t *params = dt_camera_capture_alloc();
  if(!params)
  {
    dt_control_job_dispose(job);
    return NULL;
  }
  dt_control_job_add_progress(job, _("capture images"), FALSE);
  dt_control_job_set_params(job, params, dt_camera_capture_cleanup);

  dt_import_session_set_name(params->shared.session, jobcode);

  params->delay = delay;
  params->count = count;
  params->brackets = brackets;
  params->steps = steps;
  return job;
}

/** Sleep for \p ms milliseconds in small slices, checking \p job's
 * cancellation state between slices so a cancel request (e.g. from the
 * darktable progress bar) takes effect promptly instead of only being
 * noticed after a multi-second sleep. \return TRUE if the job was
 * cancelled before the full delay elapsed. */
#define DT_CAMERA_FOCUS_BRACKET_SLEEP_SLICE_MS 50
static gboolean _sleep_cancellable(dt_job_t *job, uint32_t ms)
{
  while(ms > 0)
  {
    if(dt_control_job_get_state(job) == DT_JOB_STATE_CANCELLED)
      return TRUE;
    const uint32_t slice = MIN(ms, DT_CAMERA_FOCUS_BRACKET_SLEEP_SLICE_MS);
    g_usleep(slice * 1000);
    ms -= slice;
  }
  return dt_control_job_get_state(job) == DT_JOB_STATE_CANCELLED;
}

// settle time after releasing an autofocus half-press, before the lens
// is considered focused and it's safe to move on
#define DT_CAMERA_AF_HALFPRESS_SETTLE_MS 300

/** Focus bracketing: repeatedly nudge the lens focus with the camera's
 * manual focus stepping action and capture a frame after each move.
 * \see dev-doc / PTP_DPC_SONY_ManualFocusAdjust, exposed by libgphoto2
 * as the "/main/actions/manualfocus" action (nonzero values -7..-1 and
 * 1..7, sign is direction, magnitude is step size). */
static int32_t dt_camera_focus_bracket_job_run(dt_job_t *job)
{
  dt_camera_focus_bracket_t *params = dt_control_job_get_params(job);

  if(!dt_camctl_camera_property_exists(darktable.camctl, NULL, "manualfocus"))
  {
    dt_control_log(_("camera does not support focus stepping, "
                     "can't run focus bracketing"));
    return 1;
  }

  const int total = MAX(1, (int)params->frames);
  dt_control_job_set_progress_message(job,
           ngettext("focus bracketing: capturing %d frame",
                    "focus bracketing: capturing %d frames", total), total);

  if(params->prefocus
     && dt_camctl_camera_property_exists(darktable.camctl, NULL, "autofocus"))
  {
    // shutter half-press begin/end: this is the only autofocus trigger
    // strategy that works reliably across Sony bodies
    dt_camctl_camera_set_property_int(darktable.camctl, NULL, "autofocus", 1);
    if(_sleep_cancellable(job, params->af_hold_ms))
      return 0;
    dt_camctl_camera_set_property_int(darktable.camctl, NULL, "autofocus", 0);
    if(_sleep_cancellable(job, DT_CAMERA_AF_HALFPRESS_SETTLE_MS))
      return 0;
  }

  const int step = CLAMP((int)params->step, 1, 7);
  const int signed_step = params->near ? -step : step;

  double fraction = 0;
  for(uint32_t i = 0; i < params->frames; i++)
  {
    if(dt_control_job_get_state(job) == DT_JOB_STATE_CANCELLED)
      break;

    if(i > 0)
    {
      dt_camctl_camera_set_property_float(darktable.camctl, NULL,
                                          "manualfocus", signed_step);
      if(_sleep_cancellable(job, params->settle_ms))
        break;
    }

    if(dt_control_job_get_state(job) == DT_JOB_STATE_CANCELLED)
      break;

    // capture, reusing the regular tethered capture/download path so
    // that already captured frames stay imported even if the sequence
    // is cancelled or fails part-way through. dt_camctl_camera_capture()
    // only enqueues the capture on the camera's tether thread; a hard
    // camera-side failure is surfaced asynchronously to the user through
    // the usual camera_error listener callback/dt_control_log, the same
    // as for the regular (non-bracket) capture job above
    dt_camctl_camera_capture(darktable.camctl, NULL);

    fraction += 1.0 / total;
    dt_control_job_set_progress(job, fraction);
  }

  return 0;
}

dt_job_t *dt_camera_focus_bracket_job_create(const uint32_t frames,
                                             const uint32_t step,
                                             const gboolean near,
                                             const uint32_t settle_ms,
                                             const gboolean prefocus,
                                             const uint32_t af_hold_ms)
{
  dt_job_t *job = dt_control_job_create(&dt_camera_focus_bracket_job_run,
                                        "focus bracket capture of image(s)");
  if(!job)
    return NULL;

  dt_camera_focus_bracket_t *params = calloc(1, sizeof(dt_camera_focus_bracket_t));
  if(!params)
  {
    dt_control_job_dispose(job);
    return NULL;
  }
  dt_control_job_add_progress(job, _("focus bracket"), TRUE);
  dt_control_job_set_params(job, params, free);

  params->frames = MAX(2u, frames);
  params->step = CLAMP(step, 1u, 7u);
  params->near = near;
  params->settle_ms = settle_ms;
  params->prefocus = prefocus;
  params->af_hold_ms = af_hold_ms;
  return job;
}

/** Listener interface for import job */
void _camera_import_image_downloaded(const dt_camera_t *camera,
                                     const char *in_path,
                                     const char *in_filename,
                                     const char *filename,
                                     void *data)
{
  // Import downloaded image to import filmroll
  dt_camera_import_t *t = (dt_camera_import_t *)data;
  const dt_imgid_t imgid =
    dt_image_import(dt_import_session_film_id(t->shared.session), filename, FALSE, TRUE);

  const time_t timestamp = (!in_path || !in_filename) ? 0 :
               dt_camctl_get_image_file_timestamp(darktable.camctl, in_path, in_filename);
  if(timestamp
     && dt_is_valid_imgid(imgid))
  {
    char dt_txt[DT_DATETIME_EXIF_LENGTH];
    dt_datetime_unix_to_exif(dt_txt, sizeof(dt_txt), &timestamp);
    gchar *id = g_strconcat(in_filename, "-", dt_txt, NULL);
    dt_metadata_set(imgid, "Xmp.darktable.image_id", id, FALSE);
    g_free(id);
  }

  dt_control_queue_redraw_center();
  gchar *basename = g_path_get_basename(filename);
  const int num_images = g_list_length(t->images);
  dt_control_log(ngettext("%d/%d imported to %s", "%d/%d imported to %s",
                          t->import_count + 1),
                 t->import_count + 1, num_images, basename);
  g_free(basename);

  t->fraction += 1.0 / num_images;

  dt_control_job_set_progress(t->job, t->fraction);

  if((imgid & 3) == 3)
  {
    dt_collection_update_query(darktable.collection,
                               DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_UNDEF, NULL);
  }

  if(t->import_count + 1 == num_images)
  {
    // only redraw at the end, to not spam the cpu with exposure events
    dt_control_queue_redraw_center();
    DT_CONTROL_SIGNAL_RAISE(DT_SIGNAL_TAG_CHANGED);

    DT_CONTROL_SIGNAL_RAISE(DT_SIGNAL_FILMROLLS_IMPORTED,
                            dt_import_session_film_id(t->shared.session));
  }
  t->import_count++;
}

static const char *_camera_request_image_filename(const dt_camera_t *camera,
                                                  const char *filename,
                                                  const dt_image_basic_exif_t *basic_exif,
                                                  void *data)
{
  const gchar *file;
  struct dt_camera_shared_t *shared;
  shared = (dt_camera_shared_t *)data;
  const gboolean use_filename = dt_conf_get_bool("session/use_filename");

  dt_import_session_set_filename(shared->session, filename);
  dt_import_session_set_exif_basic_info(shared->session, basic_exif);
  file = dt_import_session_filename(shared->session, use_filename);

  if(file == NULL) return NULL;

  return g_strdup(file);
}

static const char *_camera_request_image_path(const dt_camera_t *camera,
                                              const dt_image_basic_exif_t *basic_exif,
                                              void *data)
{
  struct dt_camera_shared_t *shared;
  shared = (struct dt_camera_shared_t *)data;
  dt_import_session_set_exif_basic_info(shared->session, basic_exif);
  return dt_import_session_path(shared->session, FALSE);
}

static int32_t dt_camera_import_job_run(dt_job_t *job)
{
  dt_camera_import_t *params = dt_control_job_get_params(job);
  dt_control_log(_("starting to import images from camera"));

  guint total = g_list_length(params->images);
  dt_control_job_set_progress_message(job,
           ngettext("importing %d image from camera",
                    "importing %d images from camera", total), total);

  // Switch to new filmroll
  dt_film_open(dt_import_session_film_id(params->shared.session));
  dt_ctl_switch_mode_to("lighttable");

  // register listener
  dt_camctl_listener_t listener = { 0 };
  listener.data = params;
  listener.image_downloaded = _camera_import_image_downloaded;
  listener.request_image_path = _camera_request_image_path;
  listener.request_image_filename = _camera_request_image_filename;

  // start download of images
  dt_camctl_register_listener(darktable.camctl, &listener);
  dt_camctl_import(darktable.camctl, params->camera, params->images);
  dt_camctl_unregister_listener(darktable.camctl, &listener);

  // notify the user via the window manager
  dt_ui_notify_user();

  return 0;
}

static dt_camera_import_t *dt_camera_import_alloc()
{
  dt_camera_import_t *params = calloc(1, sizeof(dt_camera_import_t));
  if(!params)
    return NULL;

  params->shared.session = dt_import_session_new();

  return params;
}

static void dt_camera_import_cleanup(void *p)
{
  dt_camera_import_t *params = p;

  // Free the dynamically allocated filename strings in the list, then the list itself.
  // These strings were allocated by gtk_tree_model_get in import.c:_import_from_dialog_run.
  g_list_free_full(params->images, g_free);

  dt_import_session_destroy(params->shared.session);

  params->camera->is_importing = FALSE;
  free(params);
}

dt_job_t *dt_camera_import_job_create(GList *images,
                                      struct dt_camera_t *camera,
                                      const char *time_override)
{
  dt_job_t *job = dt_control_job_create(&dt_camera_import_job_run,
                                        "import selected images from camera");
  if(!job)
    return NULL;

  dt_camera_import_t *params = dt_camera_import_alloc();
  if(!params)
  {
    dt_control_job_dispose(job);
    return NULL;
  }
  camera->is_importing = TRUE;
  dt_control_job_add_progress(job, _("import images from camera"), FALSE);
  dt_control_job_set_params(job, params, dt_camera_import_cleanup);

  /* initialize import session for camera import job */
  if(time_override && time_override[0])
    dt_import_session_set_time(params->shared.session, time_override);
  const char *jobcode = dt_conf_get_string_const("ui_last/import_jobcode");
  dt_import_session_set_name(params->shared.session, jobcode);

  params->fraction = 0;
  params->images = images;
  params->camera = camera;
  params->import_count = 0;
  params->job = job;
  return job;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
