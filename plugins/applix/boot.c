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
#include "workbook.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>

static char const *
filename_ext (const char *filename)
{
	char const *p;
	if (filename == NULL || (p = strrchr (filename, '.')) == NULL)
		return NULL;
	return ++p;
}

static gboolean
applix_probe (const char *filename)
{
	gboolean res;
	FILE *file;
	char const *ext = filename_ext (filename);
	if (ext == NULL || g_strcasecmp ("as", ext))
		return FALSE;

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
applix_load (CommandContext *context, Workbook *wb, const char *filename)
{
	int res;
	FILE *file = gnumeric_fopen (context, filename, "r");
	if (file == NULL)
		return -1;

	res = applix_read (context, wb, file);
	fclose (file);

	if (res == 0)
		workbook_set_saveinfo (wb, filename, FILE_FL_MANUAL, NULL);

	return res;
}

static int
applix_can_unload (PluginData *pd)
{
	return TRUE;
}

static void
applix_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (applix_probe, applix_load);
}

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	file_format_register_open (100, 
				   _("Applix (*.as) file format",
				   &applix_probe, &applix_load);

	if (plugin_data_init (pd, &applix_can_unload, &applix_cleanup_plugin,
			      _("Applix"),
			      _("Imports version 4.[234] spreadsheets")))
	        return PLUGIN_OK;
	return PLUGIN_ERROR;
}
