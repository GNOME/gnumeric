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

static char *
filename_ext(const char *filename)
{
	char *p = strrchr (filename, '.');
	if (p == NULL)
		return NULL;
	return ++p;
}

static gboolean
oleo_probe (const char *filename)
{
	char *ext;

	if (!filename)
		return FALSE;
	ext = filename_ext (filename);
	if (!ext)
		return FALSE;
	if (!g_strcasecmp ("oleo", ext))
	    return TRUE;
	return FALSE;
}


static int
oleo_load (IOContext *context, WorkbookView *wb_view,
	   const char *filename)
{
	Workbook *wb = wb_view_workbook (wb_view);
	int ret;

	ret = oleo_read (context, wb, filename);

	if (ret == 0)
		workbook_set_saveinfo (wb, filename, FILE_FL_MANUAL, NULL);

	return ret;
}

static int
oleo_can_unload (PluginData *pd)
{
	return TRUE;
}

static void
oleo_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (oleo_probe, oleo_load);
}

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	file_format_register_open (100, 
				   _("GNU Oleo (*.oleo) file format"),
				   &oleo_probe, &oleo_load);

	if (plugin_data_init (pd, &oleo_can_unload, &oleo_cleanup_plugin,
			      _("GNU Oleo"),
			      _("Imports GNU Oleo documents")))
	        return PLUGIN_OK;
	else
	        return PLUGIN_ERROR;

}
