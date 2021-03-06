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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Christian J. Kellner <christian@kellner.me>
 */

#include "config.h"

#include "bolt-enums.h"
#include "bolt-error.h"

#include "bolt-config.h"

#define DAEMON_GROUP "config"
#define CFG_VERSION 1

#define DEFAULT_POLICY_KEY "DefaultPolicy"
#define AUTH_MODE_KEY "AuthMode"

GKeyFile *
bolt_config_user_init (void)
{
  GKeyFile *cfg;

  cfg = g_key_file_new ();

  g_key_file_set_comment (cfg, NULL, NULL,
                          " Generated by boltd - do not edit",
                          NULL);

  g_key_file_set_uint64 (cfg, DAEMON_GROUP, "version", CFG_VERSION);

  return cfg;
}

BoltTri
bolt_config_load_default_policy (GKeyFile   *cfg,
                                 BoltPolicy *policy,
                                 GError    **error)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *str = NULL;
  BoltPolicy p;

  g_return_val_if_fail (error == NULL || *error == NULL, TRI_NO);
  g_return_val_if_fail (policy != NULL, TRI_NO);

  if (cfg == NULL)
    return TRI_NO;

  str = g_key_file_get_string (cfg, DAEMON_GROUP, DEFAULT_POLICY_KEY, &err);
  if (str == NULL)
    {
      int res = bolt_err_notfound (err) ? TRI_NO : TRI_ERROR;

      if (res == TRI_ERROR)
        bolt_error_propagate (error, &err);

      return res;
    }

  p = bolt_policy_from_string (str);
  if (!bolt_policy_validate (p))
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_CFG,
                   "invalid policy: %s", str);
      return TRI_ERROR;
    }

  *policy = p;
  return TRI_YES;
}

BoltTri
bolt_config_load_auth_mode (GKeyFile     *cfg,
                            BoltAuthMode *authmode,
                            GError      **error)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *str = NULL;
  guint flags = 0;
  gboolean ok;

  if (cfg == NULL)
    return TRI_NO;

  g_return_val_if_fail (error == NULL || *error == NULL, TRI_NO);

  str = g_key_file_get_string (cfg, DAEMON_GROUP, AUTH_MODE_KEY, &err);
  if (str == NULL)
    {
      int res = bolt_err_notfound (err) ? TRI_NO : TRI_ERROR;

      if (res == TRI_ERROR)
        bolt_error_propagate (error, &err);

      return res;
    }

  ok = bolt_flags_from_string (BOLT_TYPE_AUTH_MODE, str, &flags, error);
  if (!ok)
    return TRI_ERROR;

  if (authmode)
    *authmode = flags;

  return TRI_YES;
}

void
bolt_config_set_auth_mode (GKeyFile   *cfg,
                           const char *authmode)
{
  g_return_if_fail (cfg != NULL);

  g_key_file_set_string (cfg, DAEMON_GROUP, AUTH_MODE_KEY, authmode);
}
