/*
    This file is part of darktable,
    Copyright (C) 2010-2023 darktable developers.

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

#include "common/darktable.h"
#include "common/image.h"

#include <glib.h>

#if defined (_WIN32)
#ifdef interface
#undef interface
#endif
#endif //defined (_WIN32)
#include <gphoto2/gphoto2.h>
#include <gtk/gtk.h>

/** Valid step magnitude range for the "manualfocus" gphoto2 action
 * (Sony PTP_DPC_SONY_ManualFocusAdjust, exposed as
 * /main/actions/manualfocus by libgphoto2): step values are signed,
 * nonzero, with the sign giving direction (negative: nearer, positive:
 * farther) and the absolute value giving the step size, 1 (fine) to 7
 * (coarse). Zero is not a valid step and triggers a different action
 * (AFMFHold) on the camera, so callers must never send it. */
#define DT_CAMCTL_MANUALFOCUS_STEP_MIN 1
#define DT_CAMCTL_MANUALFOCUS_STEP_MAX 7

/** autofocus status reported by the camera while a half-press is held */
typedef enum dt_camctl_af_state_t
{
  DT_CAMCTL_AF_UNKNOWN,
  DT_CAMCTL_AF_SEARCHING,
  DT_CAMCTL_AF_TRACKING,
  DT_CAMCTL_AF_ACQUIRED,
  DT_CAMCTL_AF_FAILED
} dt_camctl_af_state_t;

/** A camera object used for camera actions and callbacks */
typedef struct dt_camera_t
{
  /** A pointer to the model string of camera. */
  char *model;
  /** A pointer to the port string of camera. */
  char *port;
  /** Camera summary text */
  CameraText summary;

  /** Camera configuration cache */
  CameraWidget *configuration;

  /** Registered timeout func */
  CameraTimeoutFunc timeout;

  dt_pthread_mutex_t config_lock;
  /** This camera/device can import images. */
  gboolean can_import;
  /** This camera/device can do tethered shoots. */
  gboolean can_tether;
  /** This camera/device can do live view. */
  gboolean can_live_view;
  /** This camera/device can do advanced live view things like zoom. */
  gboolean can_live_view_advanced;
  /** This camera/device can be remote controlled. */
  gboolean can_config;
  /** This camera can read file previews */
  gboolean can_file_preview;
  /** This camera has some directory support */
  gboolean can_directory;
  /** This camera has exif support */
  gboolean can_file_exif;
  /** Flag camera in tethering mode. \see dt_camera_tether_mode() */
  gboolean is_tethering;

  /** List of open gp_files to be closed when closing the camera */
  GList *open_gpfiles;

  /** A mutex lock for jobqueue */
  dt_pthread_mutex_t jobqueue_lock;
  /** The jobqueue */
  GList *jobqueue;

  struct
  {
    CameraWidget *widget;
    uint32_t index;
  } current_choice;

  /** gphoto2 camera pointer */
  Camera *gpcam;

  /** gphoto2 context */
  GPContext *gpcontext;

  /** flag to unmount */
  gboolean unmount;
  /** flag noting a runtime ptp error condition */
  gboolean ptperror;
  /** flag true while importing */
  gboolean is_importing;
  /** Live view */
  gboolean is_live_viewing;
  /** The last preview image from the camera */
  uint8_t *live_view_buffer;
  int live_view_width, live_view_height;
  //dt_colorspaces_color_profile_type_t live_view_color_space;
  /** Rotation of live view, multiples of 90° */
  int32_t live_view_rotation;
  /** Zoom level for live view */
  gboolean live_view_zoom;
  /** Pan the zoomed live view */
  gboolean live_view_pan;
  /** Position of the live view zoom region */
  gint live_view_zoom_x, live_view_zoom_y;
  /** Mirror the live view for easier self portraits */
  gboolean live_view_flip;
  /** The thread adding the live view jobs */
  pthread_t live_view_thread;
  /** A guard so that writing and reading the live view buffer don't interfere */
  dt_pthread_mutex_t live_view_buffer_mutex;
  /** A flag to tell the live view thread that the last job was completed */
  dt_pthread_mutex_t live_view_synch;
  /** requested half-press state, guarded by jobqueue_lock */
  gboolean is_af_halfpressed;
  /** actual half-press and focus state, owned by the camera worker */
  gboolean af_status_polling;
  dt_camctl_af_state_t af_state;

  /** Guards capture_done below. */
  GMutex capture_done_mutex;
  /** Signaled every time capture_done transitions to TRUE. */
  GCond capture_done_cond;
  /** FALSE from the moment a capture job is queued until the camera has been
   * observed (via gphoto2 events, see _camera_wait_until_idle()) to have gone
   * quiet again -- i.e. it is done writing/processing the shot and it is safe
   * to send the next command (next focus step, next capture, ...) without
   * risking a busy-related crash on Sony bodies during focus bracketing.
   * \see dt_camctl_camera_wait_for_capture() */
  gboolean capture_done;
} dt_camera_t;

/** A dummy camera object used for unused cameras */
typedef struct dt_camera_unused_t
{
  /** A pointer to the model string of camera. */
  char *model;
  /** A pointer to the port string of camera. */
  char *port;
  /** mark the camera as auto unmounted */
  gboolean boring;
  /** mark the camera as used by another application */
  gboolean used;
  /** if true it will be removed from the list to force a reconnect */
  gboolean trymount;
} dt_camera_unused_t;

/** Camera control status.
  These enumerations are passed back to host application using
  listener interface function control_status().
*/
typedef enum dt_camctl_status_t
{
  /** Camera control is busy, operations will block . \remarks
     Technically this means that the dt_camctl_t.mutex is locked*/
  CAMERA_CONTROL_BUSY,
  /** Camera control is available. \remarks dt_camctl_t.mutex is freed */
  CAMERA_CONTROL_AVAILABLE
} dt_camctl_status_t;

/** Camera control errors.
  These enumerations are passed to the host application using
  listener interface function camera_error().
*/
typedef enum dt_camera_error_t
{
  /** Locking camera failed. \remarks This means that camera control is busy and locking failed. */
  CAMERA_LOCK_FAILED,
  /**  Camera connection is broken and unusable.  \remarks Beyond this
  message references to dt_camera_t pointer is invalid, which means
  that the host application should remove all references of camera
  pointer and disallow any operations onto it.
   */
  CAMERA_CONNECTION_BROKEN
} dt_camera_error_t;

/** Context of camera control */
typedef struct dt_camctl_t
{
  dt_pthread_mutex_t lock;
  dt_pthread_mutex_t listeners_lock;

  /** Camera event thread. */
  pthread_t camera_event_thread;
  /** List of registered listeners of camera control. \see dt_camctl_register_listener() ,
   * dt_camctl_unregister_listener() */
  GList *listeners;
  /** List of cameras found and initialized by camera control.*/
  GList *cameras;
  /** List of unused cameras found */
  GList *unused_cameras;

  /** The actual gphoto2 context */
  GPContext *gpcontext;
  /** List of gphoto2 port drivers */
  GPPortInfoList *gpports;
  /** List of gphoto2 camera drivers */
  CameraAbilitiesList *gpcams;

  /** The host application want to use this camera. \see dt_camctl_select_camera() */
  const dt_camera_t *wanted_camera;

  const dt_camera_t *active_camera;

  gboolean import_ui;
  gboolean changed_camera;
  int ticker;
  int tickmask;
} dt_camctl_t;


typedef struct dt_camctl_listener_t
{
  void *data;
  /** Invoked when a image is downloaded while in tethered mode or by
      import. \see dt_camctl_status_t */
  void (*control_status)(dt_camctl_status_t status, void *data);

  /** Invoked before images are fetched from camera and when tethered
   * capture fetching an image. \note That only one listener should
   * implement this at time... */
  const char *(*request_image_path)(const dt_camera_t *camera,
                                    const dt_image_basic_exif_t *basic_exif,
                                    void *data);

  /** Invoked before images are fetched from camera and when tethered
   * capture fetching an image. \note That only one listener should
   * implement this at time... */
  const char *(*request_image_filename)(const dt_camera_t *camera,
                                        const char *filename,
                                        const dt_image_basic_exif_t *basic_exif,
                                        void *data);

  /** Invoked when a image is downloaded while in tethered mode or by import */
  void (*image_downloaded)(const dt_camera_t *camera,
                           const char *in_path,
                           const char *in_filename,
                           const char *filename,
                           void *data);

  /** Invoked when a image is found on storage.. such as from
   * dt_camctl_get_previews(), if 0 is returned the recurse is
   * stopped.. */
  int (*camera_storage_image_filename)(const dt_camera_t *camera,
                                       const char *filename,
                                       CameraFile *preview,
                                       void *data);

  /** Invoked when a value of a property is changed. */
  void (*camera_property_value_changed)(const dt_camera_t *camera,
                                        const char *name,
                                        const char *value,
                                        void *data);
  /** Invoked when accessibility of a property is changed. */
  void (*camera_property_accessibility_changed)(const dt_camera_t *camera,
                                                const char *name,
                                                gboolean read_only,
                                                void *data);

  /** Invoked from dt_camctl_detect_cameras() when a new camera is connected */
  void (*camera_connected)(const dt_camera_t *camera, void *data);
  /** Invoked from dt_camctl_detect_cameras() when a new camera is
   * disconnected, or when connection is broken and camera is
   * unusable */
  void (*camera_disconnected)(const dt_camera_t *camera, void *data);
  /** Invoked when a error occurred \see dt_camera_error_t */
  void (*camera_error)(const dt_camera_t *camera, dt_camera_error_t error, void *data);
  /** invoked on the camera worker when autofocus status changes
   * callbacks must not unregister listeners or wait for camera jobs */
  void (*camera_autofocus_status_changed)(const dt_camera_t *camera,
                                          dt_camctl_af_state_t state,
                                          void *data);
} dt_camctl_listener_t;


typedef enum dt_camera_preview_flags_t
{
  /** No image data */
  CAMCTL_IMAGE_NO_DATA = 0,
  /**Get an image preview. */
  CAMCTL_IMAGE_PREVIEW_DATA = 1,
} dt_camera_preview_flags_t;

/** camera file info */
typedef struct dt_camera_files_t
{
  /** file name */
  char *filename;
  /** timestamp */
  time_t timestamp;
} dt_camera_files_t;

/** Initializes the gphoto and cam control, returns NULL if failed */
dt_camctl_t *dt_camctl_new();
/** Destroys the camera control */
void dt_camctl_destroy(dt_camctl_t *c);
/** Registers a listener of camera control */
void dt_camctl_register_listener(const dt_camctl_t *c, dt_camctl_listener_t *listener);
/** Unregisters a listener of camera control */
void dt_camctl_unregister_listener(const dt_camctl_t *c, dt_camctl_listener_t *listener);
/** Check if there is any camera connected */
gboolean dt_camctl_have_cameras(const dt_camctl_t *c);
/** Check if there is any camera known but unused  */
gboolean dt_camctl_have_unused_cameras(const dt_camctl_t *c);
/** Selects a camera to be used by cam control, this camera is selected if NULL is passed as camera*/
void dt_camctl_select_camera(const dt_camctl_t *c, const dt_camera_t *cam);
/** Can tether...*/
int dt_camctl_can_enter_tether_mode(const dt_camctl_t *c, const dt_camera_t *cam);
/** Enables/Disables the tether mode on camera. */
void dt_camctl_tether_mode(const dt_camctl_t *c, const dt_camera_t *cam, gboolean enable);
/** Imports the images in list from specified camera */
void dt_camctl_import(const dt_camctl_t *c, const dt_camera_t *cam, GList *images);
/** return the timestamp for file from camera CAUTION camera mutex already own*/
time_t dt_camctl_get_image_file_timestamp(const dt_camctl_t *c, const char *in_path,
                                          const char *in_filename);
/** return the list of images from camera */
GList *dt_camctl_get_images_list(const dt_camctl_t *c, dt_camera_t *cam);
/** return the thumbnail of a camera image */
GdkPixbuf *dt_camctl_get_thumbnail(const dt_camctl_t *c, dt_camera_t *cam, const gchar *filename);
/** Execute remote capture of camera.*/
void dt_camctl_camera_capture(const dt_camctl_t *c, const dt_camera_t *cam);

/** Blocks the calling thread until the camera can be confirmed, via gphoto2
 * events, to have gone idle again after the most recently queued capture --
 * i.e. it is done writing/transferring the image and it is safe to send the
 * next command. This is the robust replacement for sleeping a fixed delay or
 * relying solely on a capped GP_ERROR_CAMERA_BUSY retry between focus steps
 * of a focus-bracketing sequence: it does not return "done" until the camera
 * itself says so, and only falls back to a generous safety-net timeout if
 * the camera never reports readiness (logged, since that indicates a genuine
 * problem rather than "wait longer").
 * \param cam Pointer to dt_camera_t if NULL the camctl->active_camera is used.
 * \param timeout_ms hard upper bound on how long to wait.
 * \return TRUE if the camera confirmed idle, FALSE if timeout_ms elapsed first. */
gboolean dt_camctl_camera_wait_for_capture(const dt_camctl_t *c,
                                           const dt_camera_t *cam,
                                           const int timeout_ms);
/** Start live view of camera.*/
gboolean dt_camctl_camera_start_live_view(const dt_camctl_t *c);
/** Stop live view of camera.*/
void dt_camctl_camera_stop_live_view(const dt_camctl_t *c);
/** Returns a model string of camera.*/
const char *dt_camctl_camera_get_model(const dt_camctl_t *c, const dt_camera_t *cam);

/** Set a property value \param cam Pointer to dt_camera_t if NULL the camctl->active_camera is used. */
void dt_camctl_camera_set_property_string(const dt_camctl_t *c,
                                          const dt_camera_t *cam,
                                          const char *property_name,
                                          const char *value);
void dt_camctl_camera_set_property_toggle(const dt_camctl_t *c,
                                          const dt_camera_t *cam,
                                          const char *property_name);
void dt_camctl_camera_set_property_choice(const dt_camctl_t *c,
                                          const dt_camera_t *cam,
                                          const char *property_name,
                                          const int value);
void dt_camctl_camera_set_property_int(const dt_camctl_t *c,
                                       const dt_camera_t *cam,
                                       const char *property_name,
                                       const int value);
void dt_camctl_camera_set_property_float(const dt_camctl_t *c,
                                         const dt_camera_t *cam,
                                         const char *property_name,
                                         const float value);
/** start autofocus using shutter half-press emulation and hold it until
 * dt_camctl_camera_release_af_halfpress() is called
 * NULL selects the active camera; the camera must be tethering
 * TRUE means the request was queued, not that focus was acquired
 * status callbacks report camera results asynchronously, with unsupported
 * or unrecognized focus indication remaining unknown */
gboolean dt_camctl_camera_start_af_halfpress(const dt_camctl_t *c,
                                             dt_camera_t *cam);
/** queue release of a previously started autofocus half-press
 * NULL selects the active camera; safe if no half-press is held */
void dt_camctl_camera_release_af_halfpress(const dt_camctl_t *c,
                                           dt_camera_t *cam);
/** Get a property value from cached configuration. \param cam Pointer to dt_camera_t if NULL the
 * camctl->active_camera is used. */
const char *dt_camctl_camera_get_property(const dt_camctl_t *c,
                                          const dt_camera_t *cam,
                                          const char *property_name);
/** Check if property exists. */
int dt_camctl_camera_property_exists(const dt_camctl_t *c,
                                     const dt_camera_t *cam,
                                     const char *property_name);

/**
 * @param cam the camera to check property type for
 * @param property_name the property check type for
 * @return the type of camera widget, NULL on failure
 */
int dt_camctl_camera_get_property_type(const dt_camctl_t *c,
                                       const dt_camera_t *cam,
                                       const char *property_name,
                                       CameraWidgetType *widget_type);

/** Get first choice available for named property. */
const char *dt_camctl_camera_property_get_first_choice(const dt_camctl_t *c,
                                                       const dt_camera_t *cam,
                                                       const char *property_name);
/** Get next choice available for named property. */
const char *dt_camctl_camera_property_get_next_choice(const dt_camctl_t *c,
                                                      const dt_camera_t *cam,
                                                      const char *property_name);

/** build a popup menu with all properties available */
void dt_camctl_camera_build_property_menu(const dt_camctl_t *c,
                                          const dt_camera_t *cam,
                                          GMenu **menu,
                                          GCallback item_activate,
                                          gpointer user_data);

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
