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
#include "workbook-view.h"
#include "workbook.h"
#include "gnumeric-util.h"
#include "main.h"
#include "sheet.h"
#include "file.h"

#include "lotus.h"
#include "lotus-types.h"
#include "plugin.h"

static char *
filename_ext (const char *filename)
{
	char *p = strrchr (g_basename (filename), '.');
	if (p == NULL)
		return NULL;
	return ++p;
}

static gboolean
lotus_probe (const char *filename)
{
	const char *ext;
	char magic[4];
	int fd, rcount;

	if (!filename)
		return FALSE;
	ext = filename_ext (filename);
	if (!ext)
		return FALSE;
	if (g_strcasecmp ("wk1", ext) && 
	    g_strcasecmp ("wks", ext))
	    return FALSE;

	/* Filename is valid.  Now test file.  */
	fd = open (filename, O_RDONLY);
	if (fd < 0) return FALSE;
	rcount = read (fd, magic, 4);
	close (fd);

	if (rcount != 4) return FALSE;

	if (magic[0] == (LOTUS_BOF & 0xff) &&
	    magic[1] == ((LOTUS_BOF >> 8) & 0xff) &&
	    magic[2] == (2 & 0xff) &&
	    magic[3] == ((2 >> 8) & 0xff))
		return TRUE;

	return FALSE;
}


static int
lotus_load (IOContext *context, WorkbookView *wb_view,
	    const char *filename)
{
	Workbook *wb = wb_view_workbook (wb_view);
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
