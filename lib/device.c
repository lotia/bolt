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


#include <gio/gio.h>
#include <glib.h>
#include <gudev/gudev.h>

#include <stdio.h>

#include "device.h"

struct _TbDeviceClass
{
  GObjectClass parent_class;

  gpointer     padding[13];
};

enum { PROP_DEVICE_0,

       PROP_UID,

       PROP_ID,
       PROP_NAME,

       PROP_VENDOR_ID,
       PROP_VENDOR_NAME,

       PROP_SYSFS,
       PROP_AUTHORIZED,

       PROP_DEVICE_LAST };

static GParamSpec *device_props[PROP_DEVICE_LAST] = {
  NULL,
};

G_DEFINE_TYPE (TbDevice, tb_device, G_TYPE_OBJECT);

static void
tb_device_finalize (GObject *object)
{
  TbDevice *dev = TB_DEVICE (object);

  g_free (dev->uid);
  g_free (dev->vendor_name);
  g_free (dev->device_name);
  g_free (dev->sysfs);

  G_OBJECT_CLASS (tb_device_parent_class)->finalize (object);
}

static void
tb_device_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  TbDevice *dev = TB_DEVICE (object);

  switch (prop_id)
    {

    case PROP_UID:
      g_value_set_string (value, dev->uid);
      break;

    case PROP_ID:
      g_value_set_uint (value, dev->device);
      break;

    case PROP_NAME:
      g_value_set_string (value, dev->device_name);
      break;

    case PROP_VENDOR_ID:
      g_value_set_uint (value, dev->vendor);
      break;

    case PROP_VENDOR_NAME:
      g_value_set_string (value, dev->vendor_name);
      break;

    case PROP_SYSFS:
      g_value_set_string (value, dev->sysfs);
      break;

    case PROP_AUTHORIZED:
      g_value_set_int (value, dev->authorized);
      break;
    }
}

static void
tb_device_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  TbDevice *dev = TB_DEVICE (object);

  switch (prop_id)
    {
    case PROP_UID:
      dev->uid = g_value_dup_string (value);
      break;

    case PROP_ID:
      dev->device = g_value_get_uint (value);
      break;

    case PROP_NAME:
      dev->device_name = g_value_dup_string (value);
      break;

    case PROP_VENDOR_ID:
      dev->vendor = g_value_get_uint (value);
      break;

    case PROP_VENDOR_NAME:
      dev->vendor_name = g_value_dup_string (value);
      break;

    case PROP_SYSFS:
      dev->sysfs = g_value_dup_string (value);
      break;

    case PROP_AUTHORIZED:
      dev->authorized = g_value_get_int (value);
      break;
    }
}

static void
tb_device_init (TbDevice *mns)
{
}

static void
tb_device_class_init (TbDeviceClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = tb_device_finalize;

  gobject_class->get_property = tb_device_get_property;
  gobject_class->set_property = tb_device_set_property;

  device_props[PROP_UID] = g_param_spec_string ("uid",
                                                NULL,
                                                NULL,
                                                NULL,
                                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  device_props[PROP_ID] = g_param_spec_uint ("device-id",
                                             NULL,
                                             NULL,
                                             0,
                                             G_MAXUINT16,
                                             0,
                                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  device_props[PROP_NAME] =
    g_param_spec_string ("device-name",
                         NULL,
                         NULL,
                         "",
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  device_props[PROP_VENDOR_ID] =
    g_param_spec_uint ("vendor-id", NULL, NULL, 0, G_MAXUINT16, 0, G_PARAM_READABLE | G_PARAM_STATIC_NAME);

  device_props[PROP_VENDOR_NAME] =
    g_param_spec_string ("vendor-name",
                         NULL,
                         NULL,
                         "",
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  device_props[PROP_SYSFS] =
    g_param_spec_string ("sysfs", NULL, NULL, "", G_PARAM_READABLE | G_PARAM_STATIC_NAME);

  device_props[PROP_AUTHORIZED] =
    g_param_spec_int ("authorized", NULL, NULL, -1, G_MAXUINT8, -1, G_PARAM_READABLE | G_PARAM_STATIC_NAME);

  g_object_class_install_properties (gobject_class, PROP_DEVICE_LAST, device_props);
}

static int
set_string (const char *val, char **field)
{
  if (!g_strcmp0 (val, *field))
    return 0;

  g_free (*field);
  *field = g_strdup (val);

  return 1;
}

static int
set_string_from_udev_attr (GUdevDevice *udev, const char *attr, char **field)
{
  const char *val = g_udev_device_get_sysfs_attr (udev, attr);

  return set_string (val, field);
}

static int
set_uint_from_udev_attr (GUdevDevice *udev, const char *attr, guint *field)
{
  guint64 val;

  val = g_udev_device_get_sysfs_attr_as_uint64 (udev, attr);

  if (val > G_MAXUINT)
    g_warning ("value read from sysfs overflows guint field.");

  if (val == *field)
    return 0;

  *field = val;
  return 1;
}

static int
set_int_from_udev_attr (GUdevDevice *udev, const char *attr, gint *field)
{
  gint val;

  val = g_udev_device_get_sysfs_attr_as_int (udev, attr);

  if (val > G_MAXUINT)
    g_warning ("value read from sysfs overflows guint field.");

  if (val == *field)
    return 0;

  *field = val;
  return 1;
}

gboolean
tb_device_update_from_udev (TbDevice *device, GUdevDevice *udev)
{
  int res, chg = 0;
  const char *val;

  /* TODO: g_object_notify */
  val = g_udev_device_get_sysfs_path (udev);
  chg |= set_string (val, &device->sysfs);

  chg |= set_string_from_udev_attr (udev, "device_name", &device->device_name);

  chg |= set_uint_from_udev_attr (udev, "device", &device->device);
  chg |= set_string_from_udev_attr (udev, "vendor_name", &device->vendor_name);
  chg |= set_uint_from_udev_attr (udev, "vendor", &device->vendor);

  chg |= set_int_from_udev_attr (udev, "authorized", &device->authorized);

  /* FIXME: uid must not change, do something here, like warn */
  if (device->uid == NULL)
    chg |= set_string_from_udev_attr (udev, "unique_id", &device->uid);

  return chg > 0;
}

const char *
tb_device_get_uid (const TbDevice *device)
{
  return device->uid;
}

const char *
tb_device_get_name (const TbDevice *device)
{
  return device->device_name;
}

const char *
tb_device_get_vendor_name (const TbDevice *device)
{
  return device->vendor_name;
}

const char *
tb_device_get_sysfs_path (const TbDevice *device)
{
  return device->sysfs;
}

gint
tb_device_get_authorized (const TbDevice *device)
{
  return device->authorized;
}

gboolean
tb_device_in_store (const TbDevice *device)
{
  return device->db != NULL;
}

gboolean
tb_device_autoconnect (const TbDevice *device)
{
  return device->autoconnect;
}
