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


static char *
oleo_load (Workbook *wb, const char *filename)
{
	char *name, *p;
	char * ret;

	ret = oleo_read (wb, filename);

	if (ret == NULL) {
		if ((p = filename_ext (filename)) != NULL)
			*p = '\0'; /* remove "oleo" */
		name = g_strconcat (p, "gnumeric", NULL);
		workbook_set_filename (wb, name);
		g_free (name);
	}

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
	g_free (pd->title);
}

PluginInitResult
init_plugin (CmdContext *context, PluginData *pd)
{
	char *descr  = _("GNU Oleo (*.oleo) file format");

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	file_format_register_open (100, descr, oleo_probe, oleo_load);

	pd->can_unload     = oleo_can_unload;
	pd->cleanup_plugin = oleo_cleanup_plugin;
	pd->title = g_strdup (_("GNU Oleo import plugin"));

	return PLUGIN_OK;
}
