/*
 * Copyright © 2017 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Christian J. Kellner <christian@kellner.me>
 */

#include "config.h"

#include "bolt-dbus.h"
#include "bolt-device.h"
#include "bolt-error.h"
#include "bolt-io.h"
#include "bolt-manager.h"

#include <dirent.h>
#include <libudev.h>

/* dbus method calls */
static gboolean    handle_authorize (BoltDBusDevice        *object,
                                     GDBusMethodInvocation *invocation,
                                     gpointer               user_data);

struct _BoltDevice
{
  BoltDBusDeviceSkeleton object;

  /* weak reference */
  BoltManager *mgr;

  char        *dbus_path;

  char        *uid;
  char        *name;
  char        *vendor;

  BoltStatus   status;

  /* when device is attached */
  char *syspath;
};


enum {
  PROP_0,

  PROP_UID,
  PROP_NAME,
  PROP_VENDOR,
  PROP_STATUS,
  PROP_SYSFS,

  PROP_LAST
};


G_DEFINE_TYPE (BoltDevice,
               bolt_device,
               BOLT_DBUS_TYPE_DEVICE_SKELETON)

static void
bolt_device_finalize (GObject *object)
{
  BoltDevice *dev = BOLT_DEVICE (object);

  g_free (dev->dbus_path);

  g_free (dev->uid);
  g_free (dev->name);
  g_free (dev->vendor);
  g_free (dev->syspath);

  G_OBJECT_CLASS (bolt_device_parent_class)->finalize (object);
}

static void
bolt_device_init (BoltDevice *dev)
{
  g_signal_connect (dev, "handle-authorize",
                    G_CALLBACK (handle_authorize), NULL);
}

static void
bolt_device_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  BoltDevice *dev = BOLT_DEVICE (object);

  switch (prop_id)
    {
    case PROP_UID:
      g_value_set_string (value, dev->uid);
      break;

    case PROP_NAME:
      g_value_set_string (value, dev->name);
      break;

    case PROP_VENDOR:
      g_value_set_string (value, dev->vendor);
      break;

    case PROP_STATUS:
      g_value_set_uint (value, dev->status);
      break;

    case PROP_SYSFS:
      g_value_set_string (value, dev->syspath);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_device_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  BoltDevice *dev = BOLT_DEVICE (object);

  switch (prop_id)
    {
    case PROP_UID:
      dev->uid = g_value_dup_string (value);
      break;

    case PROP_NAME:
      dev->name = g_value_dup_string (value);
      break;

    case PROP_VENDOR:
      dev->vendor = g_value_dup_string (value);
      break;

    case PROP_STATUS:
      dev->status = g_value_get_uint (value);
      break;

    case PROP_SYSFS:
      dev->syspath = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


static void
bolt_device_class_init (BoltDeviceClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = bolt_device_finalize;

  gobject_class->get_property = bolt_device_get_property;
  gobject_class->set_property = bolt_device_set_property;

  g_object_class_override_property (gobject_class,
                                    PROP_UID,
                                    "uid");

  g_object_class_override_property (gobject_class,
                                    PROP_NAME,
                                    "name");

  g_object_class_override_property (gobject_class,
                                    PROP_VENDOR,
                                    "vendor");

  g_object_class_override_property (gobject_class,
                                    PROP_STATUS,
                                    "status");

  g_object_class_override_property (gobject_class,
                                    PROP_SYSFS,
                                    "sysfs-path");

}

/* internal methods */

static const char *
read_sysattr_name (struct udev_device *udev, const char *attr, GError **error)
{
  g_autofree char *s = NULL;
  const char *v;

  s = g_strdup_printf ("%s_name", attr);
  v = udev_device_get_sysattr_value (udev, s);

  if (v != NULL)
    return v;

  v = udev_device_get_sysattr_value (udev, attr);

  if (v == NULL)
    g_set_error (error,
                 BOLT_ERROR, BOLT_ERROR_UDEV,
                 "failed to get sysfs attr: %s", attr);

  return v;
}

static gint
read_sysfs_attr_int (struct udev_device *device, const char *attr)
{
  const char *str;
  char *end;
  gint64 val;

  str = udev_device_get_sysattr_value (device, attr);

  if (str == NULL)
    return 0;

  val = g_ascii_strtoull (str, &end, 0);

  if (str == end)
    return 0;

  if (val > G_MAXINT || val < G_MININT)
    {
      g_warning ("value read from sysfs outside of guint's range.");
      val = 0;
    }

  return (gint) val;
}

static gboolean
string_nonzero (const char *str)
{
  return str != NULL && str[0] != '\0';
}

static BoltStatus
bolt_status_from_udev (struct udev_device *udev)
{
  gint authorized;
  const char *key;
  gboolean have_key;

  authorized = read_sysfs_attr_int (udev, "authorized");

  if (authorized == 2)
    return BOLT_STATUS_AUTHORIZED_SECURE;

  key = udev_device_get_sysattr_value (udev, "key");
  have_key = string_nonzero (key);

  if (authorized == 1)
    {
      if (have_key)
        return BOLT_STATUS_AUTHORIZED_NEWKEY;
      else
        return BOLT_STATUS_AUTHORIZED;
    }
  else if (authorized == 0 && have_key)
    {
      return BOLT_STATUS_AUTH_ERROR;
    }

  return BOLT_STATUS_CONNECTED;
}

/*  device authorization */

typedef void (*AuthCallback) (BoltDevice *dev,
                              gboolean    ok,
                              GError    **error,
                              gpointer    user_data);

typedef struct
{
  char level;

  /* the outer callback  */
  AuthCallback callback;
  gpointer     user_data;

} AuthData;

static void
auth_data_free (gpointer data)
{
  AuthData *auth = data;

  g_debug ("freeing auth data");
  g_slice_free (AuthData, auth);
}

static void
authorize_in_thread (GTask        *task,
                     gpointer      source,
                     gpointer      context,
                     GCancellable *cancellable)
{
  g_autoptr(DIR) devdir = NULL;
  g_autoptr(GError) error = NULL;
  BoltDevice *dev = source;
  AuthData *auth = context;
  gboolean ok;

  devdir = bolt_opendir (dev->syspath, &error);
  if (devdir == NULL)
    {
      g_task_return_error (task, error);
      return;
    }

  ok = bolt_verify_uid (dirfd (devdir), dev->uid, &error);
  if (!ok)
    {
      g_task_return_error (task, error);
      return;
    }

  ok = bolt_write_char_at (dirfd (devdir),
                           "authorized",
                           auth->level,
                           &error);

  if (!ok)
    {
      g_task_return_new_error (task,
                               BOLT_ERROR, BOLT_ERROR_FAILED,
                               "failed to authorize device: %s",
                               error->message);
    }
  else
    {
      g_task_return_boolean (task, TRUE);
    }
}

static void
authorize_thread_done (GObject      *object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  BoltDevice *dev = BOLT_DEVICE (object);
  GTask *task = G_TASK (res);

  g_autoptr(GError) error = NULL;
  AuthData *auth;
  gboolean ok;

  auth = g_task_get_task_data (task);

  ok = g_task_propagate_boolean (task, &error);

  if (ok)
    dev->status = BOLT_STATUS_AUTH_ERROR;
  else
    dev->status = BOLT_STATUS_AUTHORIZED;

  if (auth->callback)
    auth->callback (dev, ok, &error, auth->user_data);
}

static gboolean
bolt_device_authorize (BoltDevice  *dev,
                       AuthCallback callback,
                       gpointer     user_data,
                       GError     **error)
{
  AuthData *auth_data;
  GTask *task;

  if (dev->status != BOLT_STATUS_CONNECTED &&
      dev->status != BOLT_STATUS_AUTH_ERROR)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "wrong device state: %u", dev->status);
      return FALSE;
    }

  task = g_task_new (dev, NULL, authorize_thread_done, NULL);

  auth_data = g_slice_new (AuthData);
  auth_data->level = '1';
  auth_data->callback = callback;
  auth_data->user_data = user_data;
  g_task_set_task_data (task, auth_data, auth_data_free);

  dev->status = BOLT_STATUS_AUTHORIZING;
  g_object_notify (G_OBJECT (dev), "status");

  g_task_run_in_thread (task, authorize_in_thread);
  g_object_unref (task);

  return TRUE;
}


/* dbus methods */

static void
handle_authorize_done (BoltDevice *dev,
                       gboolean    ok,
                       GError    **error,
                       gpointer    user_data)
{
  GDBusMethodInvocation *invocation = user_data;

  if (ok)
    {
      bolt_dbus_device_complete_authorize (BOLT_DBUS_DEVICE (dev),
                                           invocation);
    }
  else
    {
      g_dbus_method_invocation_take_error (invocation, *error);
      error = NULL;
    }
}

static gboolean
handle_authorize (BoltDBusDevice        *object,
                  GDBusMethodInvocation *invocation,
                  gpointer               user_data)
{
  BoltDevice *dev = BOLT_DEVICE (object);
  GError *error = NULL;
  gboolean ok;

  ok = bolt_device_authorize (dev,
                              handle_authorize_done,
                              invocation,
                              &error);

  if (!ok)
    g_dbus_method_invocation_take_error (invocation, error);

  return TRUE;
}

/* public methods */

BoltDevice *
bolt_device_new_for_udev (BoltManager        *mgr,
                          struct udev_device *udev,
                          GError            **error)
{
  const char *uid;
  const char *name;
  const char *vendor;
  const char *syspath;
  BoltDevice *dev;


  uid = udev_device_get_sysattr_value (udev, "unique_id");
  if (udev == NULL)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                   "could not get unique_id for udev");
      return NULL;
    }

  syspath = udev_device_get_syspath (udev);
  g_return_val_if_fail (syspath != NULL, NULL);

  name = read_sysattr_name (udev, "device", error);
  if (name == NULL)
    return NULL;

  vendor = read_sysattr_name (udev, "vendor", error);
  if (vendor == NULL)
    return NULL;

  dev = g_object_new (BOLT_TYPE_DEVICE,
                      "uid", uid,
                      "name", name,
                      "vendor", vendor,
                      "sysfs-path", syspath,
                      NULL);

  dev->status = bolt_status_from_udev (udev);

  g_object_add_weak_pointer (G_OBJECT (mgr),
                             (gpointer *) &dev->mgr);

  return dev;
}

const char *
bolt_device_export (BoltDevice      *device,
                    GDBusConnection *connection,
                    GError         **error)
{
  const char *path;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_DEVICE (device), FALSE);

  path = bolt_device_get_object_path (device);

  g_debug ("Exporting device at: %s", path);

  ok = g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (device),
                                         connection,
                                         path,
                                         error);
  return ok ? path : NULL;
}

void
bolt_device_unexport (BoltDevice *device)
{
  const char *path;

  path = bolt_device_get_object_path (device);

  g_debug ("Unexporting device at: %s", path);
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (device));
}

const char *
bolt_device_get_object_path (BoltDevice *device)
{
  g_return_val_if_fail (BOLT_IS_DEVICE (device), FALSE);

  if (device->dbus_path == NULL)
    {
      char *path = NULL;

      path = g_strdup_printf ("/org/freedesktop/Bolt/devices/%s", device->uid);
      g_strdelimit (path, "-", '_');

      device->dbus_path = path;
    }

  return device->dbus_path;
}

const char *
bolt_device_get_uid (BoltDevice *dev)
{
  return dev->uid;
}

const char *
bolt_device_get_syspath (BoltDevice *dev)
{
  return dev->syspath;
}