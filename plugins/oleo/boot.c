/**
 * boot.c: Oleo support for Gnumeric
 *
 * Author:
 *    Robert Brady <rwb197@ecs.soton.ac.uk>
 *
 * (this file adapted from lotus-123/boot.c)
 **/
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <config.h>
#include <stdio.h>
#include <ctype.h>
#include <gnome.h>
#include "gnumeric.h"
#include "workbook-view.h"
#include "workbook.h"
#include "gnumeric-util.h"
#include "main.h"
#include "sheet.h"
#include "file.h"

#include "oleo.h"
#include "plugin.h"
#include "plugin-util.h"

gchar gnumeric_plugin_version[] = GNUMERIC_VERSION;

static FileOpenerId oleo_opener_id;

static gboolean
oleo_probe (const char *filename, gpointer user_data)
{
	return filename != NULL &&
	       g_strcasecmp ("oleo", g_extension_pointer (filename)) == 0;
}

static int
oleo_load (IOContext *context, WorkbookView *wb_view,
           const char *filename, gpointer user_data)
{
	Workbook *wb = wb_view_workbook (wb_view);
	int ret;

	ret = oleo_read (context, wb, filename);

	if (ret == 0)
		workbook_set_saveinfo (wb, filename, FILE_FL_MANUAL, FILE_SAVER_ID_INVAID);

	return ret;
}

gboolean
can_deactivate_plugin (PluginInfo *pinfo)
{
	return TRUE;
}

gboolean
cleanup_plugin (PluginInfo *pinfo)
{
	file_format_unregister_open (oleo_opener_id);
	return TRUE;
}

gboolean
init_plugin (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	oleo_opener_id = file_format_register_open (
	                 100, _("GNU Oleo (*.oleo) file format"),
	                 &oleo_probe, &oleo_load, NULL);

	return TRUE;
}
