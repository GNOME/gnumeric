/* vim: set sw=8:
 * $Id$
 */

/*
 * boot.c : Installation and bootstraping routines to
 *          register the applix plugin.
 *
 * Copyright (C) 2000 Jody Goldberg (jgoldberg@home.com)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include "config.h"
#include "gnumeric.h"
#include "plugin.h"
#include "plugin-util.h"
#include "applix.h"
#include "workbook-view.h"
#include "workbook.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>

gchar gnumeric_plugin_version[] = GNUMERIC_VERSION;

static FileOpenerId applix_opener_id;

static gboolean
applix_probe (const char *filename, gpointer user_data)
{
	gboolean res;
	FILE *file;

	if (g_strcasecmp ("as", g_extension_pointer (filename)) != 0) {
		return FALSE;
	}

	/* Use fopen rather than gnumeric_fopen because we do not want
	 * to report errors.
	 */
	file = fopen (filename, "r");
	if (file == NULL)
		return FALSE;

	res = applix_read_header (file);
	fclose (file);

	return res;
}

static int
applix_load (IOContext *context, WorkbookView *wb_view,
             const char *filename, gpointer user_data)
{
	int res;
	FILE *file = gnumeric_fopen (context, filename, "r");
	if (file == NULL)
		return -1;

	res = applix_read (context, wb_view, file);
	fclose (file);

	if (res == 0)
		workbook_set_saveinfo (wb_view_workbook (wb_view),
                               filename, FILE_FL_MANUAL, FILE_SAVER_ID_INVAID);

	return res;
}

gboolean
can_deactivate_plugin (PluginInfo *pinfo)
{
	return TRUE;
}

gboolean
cleanup_plugin (PluginInfo *pinfo)
{
	file_format_unregister_open (applix_opener_id);
	return TRUE;
}

gboolean
init_plugin (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	applix_opener_id = file_format_register_open (
	                   100, _("Applix (*.as) file format"),
	                   &applix_probe, &applix_load, NULL);

	return TRUE;
}
