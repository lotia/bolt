/*
 * Copyright © 2018 Red Hat, Inc
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

#include "bolt-error.h"
#include "bolt-log.h"
#include "bolt-str.h"
#include "bolt-store.h"
#include "bolt-sysfs.h"

#include "bolt-list.h"
#include "bolt-domain.h"

#include <gio/gio.h>

#include <libudev.h>

struct _BoltDomain
{
  BoltExported object;

  /* internal list  */
  BoltList   domains;
  gint       sort;

  BoltStore *store;

  /* persistent */
  char *uid;

  /* sysfs */
  char        *id;
  char        *syspath;
  BoltSecurity security;
  GStrv        bootacl;
};


enum {
  PROP_0,

  /* internal properties */
  PROP_STORE,
  PROP_OBJECT_ID,

  /* exported properties */
  PROP_UID,
  PROP_ID,
  PROP_SYSPATH,
  PROP_SECURITY,
  PROP_BOOTACL,

  PROP_LAST,
  PROP_EXPORTED = PROP_UID
};

static GParamSpec *props[PROP_LAST] = { NULL, };

G_DEFINE_TYPE (BoltDomain,
               bolt_domain,
               BOLT_TYPE_EXPORTED)

static void
bolt_domain_finalize (GObject *object)
{
  BoltDomain *dom = BOLT_DOMAIN (object);

  g_clear_object (&dom->store);

  g_free (dom->uid);
  g_free (dom->id);
  g_free (dom->syspath);
  g_strfreev (dom->bootacl);

  G_OBJECT_CLASS (bolt_domain_parent_class)->finalize (object);
}

static void
bolt_domain_init (BoltDomain *dom)
{
  bolt_list_init (&dom->domains);
}

static void
bolt_domain_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  BoltDomain *dom = BOLT_DOMAIN (object);

  switch (prop_id)
    {
    case PROP_STORE:
      g_value_set_object (value, dom->store);
      break;

    case PROP_OBJECT_ID:
    case PROP_UID:
      g_value_set_string (value, dom->uid);
      break;

    case PROP_ID:
      g_value_set_string (value, dom->id);
      break;

    case PROP_SYSPATH:
      g_value_set_string (value, dom->syspath);
      break;

    case PROP_SECURITY:
      g_value_set_enum (value, dom->security);
      break;

    case PROP_BOOTACL:
      g_value_set_boxed (value, dom->bootacl);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_domain_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  BoltDomain *dom = BOLT_DOMAIN (object);

  switch (prop_id)
    {
    case PROP_STORE:
      if (dom->store == g_value_get_object (value))
        return;

      g_clear_object (&dom->store);
      dom->store = g_value_dup_object (value);
      break;

    case PROP_UID:
      dom->uid = g_value_dup_string (value);
      break;

    case PROP_ID:
      dom->id = g_value_dup_string (value);
      break;

    case PROP_SYSPATH:
      g_clear_pointer (&dom->syspath, g_free);
      dom->syspath = g_value_dup_string (value);
      break;

    case PROP_SECURITY:
      dom->security = g_value_get_enum (value);
      break;

    case PROP_BOOTACL:
      g_strfreev (dom->bootacl);
      dom->bootacl = g_value_dup_boxed (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_domain_class_init (BoltDomainClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  BoltExportedClass *exported_class = BOLT_EXPORTED_CLASS (klass);

  gobject_class->finalize = bolt_domain_finalize;

  gobject_class->get_property = bolt_domain_get_property;
  gobject_class->set_property = bolt_domain_set_property;

  props[PROP_STORE] =
    g_param_spec_object ("store",
                         NULL, NULL,
                         BOLT_TYPE_STORE,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_OBJECT_ID] =
    bolt_param_spec_override (gobject_class, "object-id");

  props[PROP_UID] =
    g_param_spec_string ("uid",
                         "Uid", NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_ID] =
    g_param_spec_string ("id",
                         "Id", NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_SYSPATH] =
    g_param_spec_string ("syspath",
                         "SysfsPath", NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_SECURITY] =
    g_param_spec_enum ("security",
                       "SecurityLevel", NULL,
                       BOLT_TYPE_SECURITY,
                       BOLT_SECURITY_NONE,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_BOOTACL] =
    g_param_spec_boxed ("bootacl",
                        "BootACL", NULL,
                        G_TYPE_STRV,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

  bolt_exported_class_set_interface_info (exported_class,
                                          BOLT_DBUS_DOMAIN_INTERFACE,
                                          "/boltd/org.freedesktop.bolt.xml");

  bolt_exported_class_set_object_path (exported_class, BOLT_DBUS_PATH_DOMAINS);

  bolt_exported_class_export_properties (exported_class,
                                         PROP_EXPORTED,
                                         PROP_LAST,
                                         props);

}

/*  */
BoltDomain *
bolt_domain_new_for_udev (struct udev_device *udev,
                          const char         *uid,
                          GError            **error)
{
  g_autoptr(GError) err = NULL;
  g_auto(GStrv) acl = NULL;
  BoltDomain *dom = NULL;
  BoltSecurity security = BOLT_SECURITY_UNKNOWN;
  const char *syspath;
  const char *sysname;
  gint sort = -1;

  g_return_val_if_fail (udev != NULL, NULL);
  g_return_val_if_fail (uid != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (!bolt_sysfs_device_is_domain (udev, error))
    return NULL;

  syspath = udev_device_get_syspath (udev);
  sysname = udev_device_get_sysname (udev);

  if (sysname == NULL)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                   "could not get domain id from udev");
      return NULL;
    }

  if (g_str_has_prefix (sysname, "domain"))
    {
      const char *ptr = sysname + strlen ("domain");
      bolt_str_parse_as_int (ptr, &sort);
    }

  security = bolt_sysfs_security_for_device (udev, error);

  if (security == BOLT_SECURITY_UNKNOWN)
    return NULL;

  acl = bolt_sysfs_read_boot_acl (udev, &err);
  if (acl == NULL && !bolt_err_notfound (err))
    bolt_warn_err (err, "failed to get boot_acl");

  dom = g_object_new (BOLT_TYPE_DOMAIN,
                      "uid", uid,
                      "id", sysname,
                      "syspath", syspath,
                      "security", security,
                      "bootacl", acl,
                      NULL);

  return dom;
}

const char *
bolt_domain_get_uid (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  return domain->uid;
}

const char *
bolt_domain_get_id (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  return domain->id;
}

const char *
bolt_domain_get_syspath (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  return domain->syspath;
}

BoltSecurity
bolt_domain_get_security (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), BOLT_SECURITY_UNKNOWN);

  return domain->security;
}

GStrv
bolt_domain_get_bootacl (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  return domain->bootacl;
}

gboolean
bolt_domain_is_stored (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), FALSE);

  return domain->store != NULL;
}

void
bolt_domain_export (BoltDomain      *domain,
                    GDBusConnection *bus)
{
  g_autoptr(GError) err  = NULL;
  BoltExported *exported;
  const char *opath;
  gboolean ok;

  exported = BOLT_EXPORTED (domain);
  ok = bolt_exported_export (exported, bus, NULL, &err);

  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("dbus"),
                     "error exporting a domain");
      return;
    }

  opath = bolt_exported_get_object_path (exported);
  bolt_info (LOG_TOPIC ("dbus"), "exported domain at %s", opath);
}

void
bolt_domain_update_from_udev (BoltDomain         *domain,
                              struct udev_device *udev)
{
  g_autoptr(GError) err = NULL;
  g_auto(GStrv) acl = NULL;
  gboolean same;

  acl = bolt_sysfs_read_boot_acl (udev, &err);
  if (acl == NULL && !bolt_err_notfound (err))
    {
      bolt_warn_err (err, "failed to get boot_acl");
      return;
    }

  same = bolt_strv_equal (domain->bootacl, acl);
  if (same)
    return;

  bolt_swap (domain->bootacl, acl);

  g_object_notify_by_pspec (G_OBJECT (domain), props[PROP_BOOTACL]);
}

gboolean
bolt_domain_supports_bootacl (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), FALSE);

  return domain->bootacl != NULL;
}

guint
bolt_domain_bootacl_slots (BoltDomain *domain,
                           guint      *n_free)
{
  guint slots = 0;
  guint unused = 0;

  if (domain->bootacl == NULL)
    {
      if (n_free)
        *n_free = 0;

      return 0;
    }

  for (char **iter = domain->bootacl; *iter; iter++)
    {
      slots++;

      if (bolt_strzero (*iter))
        unused++;
    }

  if (n_free)
    *n_free = unused;

  return slots;
}

gboolean
bolt_domain_bootacl_contains (BoltDomain *domain,
                              const char *uuid)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), FALSE);
  g_return_val_if_fail (uuid != NULL, FALSE);

  return domain->bootacl != NULL &&
         g_strv_contains ((char const * const *) domain->bootacl, uuid);
}

/* domain list management */
BoltDomain *
bolt_domain_insert (BoltDomain *list, BoltDomain *domain)
{
  BoltList iter;
  BoltList *n;

  g_return_val_if_fail (domain != NULL, list);

  /* the list as a whole takes one reference */
  g_object_ref (domain);

  if (list == NULL)
    return domain;

  bolt_nhlist_iter_init (&iter, &list->domains);
  while ((n = bolt_nhlist_iter_next (&iter)))
    {
      BoltDomain *d = bolt_list_entry (n, BoltDomain, domains);

      if (domain->sort > d->sort)
        break;
    }

  /* all existing domains are sorted before,
   * so add to the end of the list */
  if (n == NULL)
    n = list->domains.prev;

  bolt_list_add_after (n, &domain->domains);

  return list;
}

BoltDomain *
bolt_domain_remove (BoltDomain *list, BoltDomain *domain)
{
  BoltList *head;

  g_return_val_if_fail (list != NULL, NULL);
  g_return_val_if_fail (domain != NULL, list);

  head = bolt_nhlist_del (&list->domains, &domain->domains);

  /* the list as a whole has one reference, release it */
  g_object_unref (domain);

  if (head == NULL)
    return NULL;

  return bolt_list_entry (head, BoltDomain, domains);
}

const char **
bolt_domain_bootacl_get_used (BoltDomain *domain,
                              guint      *n_used)
{
  GPtrArray *res;
  guint used = 0;

  res = g_ptr_array_new ();

  for (char **iter = domain->bootacl; iter && *iter; iter++)
    {
      if (strlen (*iter))
        {
          g_ptr_array_add (res, *iter);
          used++;
        }
    }

  g_ptr_array_add (res, NULL);

  if (n_used != NULL)
    *n_used = used;

  return (const char **) g_ptr_array_free (res, FALSE);
}


BoltDomain *
bolt_domain_next (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  return bolt_list_entry (domain->domains.next, BoltDomain, domains);
}

BoltDomain *
bolt_domain_prev (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  return bolt_list_entry (domain->domains.prev, BoltDomain, domains);
}

guint
bolt_domain_count (BoltDomain *domain)
{
  if (domain == NULL)
    return 0;

  return bolt_nhlist_len (&domain->domains);
}

void
bolt_domain_foreach (BoltDomain *list,
                     GFunc       func,
                     gpointer    data)
{
  BoltList iter;
  BoltList *n;

  if (list == NULL)
    return;

  bolt_nhlist_iter_init (&iter, &list->domains);
  while ((n = bolt_nhlist_iter_next (&iter)))
    {
      BoltDomain *d = bolt_list_entry (n, BoltDomain, domains);
      func ((gpointer) d, data);
    }
}

BoltDomain *
bolt_domain_find_id (BoltDomain *list,
                     const char *id,
                     GError    **error)
{
  BoltList iter;
  BoltList *n;

  g_return_val_if_fail (id != NULL, NULL);

  if (list == NULL)
    goto notfound;

  bolt_nhlist_iter_init (&iter, &list->domains);
  while ((n = bolt_nhlist_iter_next (&iter)))
    {
      BoltDomain *d = bolt_list_entry (n, BoltDomain, domains);
      if (bolt_streq (d->id, id))
        return d;
    }

notfound:
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
               "domain with id '%s' could not be found.",
               id);

  return NULL;
}

void
bolt_domain_clear (BoltDomain **list)
{
  BoltDomain *iter;

  g_return_if_fail (list != NULL);

  iter = *list;

  while (bolt_domain_count (iter))
    iter = bolt_domain_remove (iter, iter);

  *list = iter;
}
