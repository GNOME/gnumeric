/*
 * gnm-rsm.c: Resource manager for Gnumeric.
 *
 * Copyright (C) 2011 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <gnumeric-config.h>
#include <goffice/goffice.h>
#include "gnm-rsm.h"

void
gnm_rsm_register_file (const char *id, gconstpointer data, size_t len)
{
  char *id2;

  g_return_if_fail (id != NULL);

  id2 = g_strconcat ("gnm:", id, NULL);
  go_rsm_register_file (id2, data, len);
  g_free (id2);
}
