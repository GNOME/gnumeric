/**
 * boot.c: Lotus 123 support for Gnumeric
 *
 * Author:
 *    See: README
 *    Michael Meeks <mmeeks@gnu.org>
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
#include "workbook.h"
#include "gnumeric-util.h"
#include "main.h"
#include "sheet.h"
#include "file.h"

#include "lotus.h"
#include "plugin.h"

static char *
filename_ext (const char *filename)
{
	char *p = strrchr (filename, '.');
	if (p == NULL)
		return NULL;
	return ++p;
}

static gboolean
lotus_probe (const char *filename)
{
	char *ext;

	if (!filename)
		return FALSE;
	ext = filename_ext (filename);
	if (!ext)
		return FALSE;
	if (!g_strcasecmp ("wk1", ext) ||
	    !g_strcasecmp ("wks", ext))
	    return TRUE;
	return FALSE;
}


static int
lotus_load (CommandContext *context, Workbook *wb, const char *filename)
{
	int ret;

	ret = lotus_read (context, wb, filename);

	if (ret == 0)
		workbook_set_saveinfo (wb, filename, FILE_FL_MANUAL, NULL);

	return ret;
}

static int
lotus_can_unload (PluginData *pd)
{
	return TRUE;
}

static void
lotus_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (lotus_probe, lotus_load);
}

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	file_format_register_open (50,
				   _("Lotus file format (*.wk1)"),
				   &lotus_probe, &lotus_load);

	if (plugin_data_init (pd, &lotus_can_unload, &lotus_cleanup_plugin,
			      _("Lotus 123"),
			      _("Imports Lotus 123 files")))
	        return PLUGIN_OK;
	else
	        return PLUGIN_ERROR;

}
