/**
 * boot.c: Lotus 123 support for Gnumeric
 *
 * Author:
 *    See: README
 *    Michael Meeks <michael@imagiantor.com>
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

#include "lotus.h"
#include "plugin.h"

static char *
filename_ext(const char *filename)
{
	char *p = strrchr (filename, '.');
	if (p==NULL)
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


static Workbook *
lotus_load (const char *filename)
{
	char *name, *p;
	Workbook *wb;
	
	wb = lotus_read (filename);

	if (wb) {
		if ((p = filename_ext (name)) != NULL)
			*p = '\0'; /* remove "wk1" */
		name = g_strconcat (p, "gnumeric", NULL);
		g_free (p);
		workbook_set_filename (wb, name);
		g_free (name);
	}

	return wb;
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

int
init_plugin (PluginData *pd)
{
	char *descr  = _("Lotus (*.wk1) file format");

	file_format_register_open (50, descr, lotus_probe, lotus_load);

	pd->can_unload     = lotus_can_unload;
	pd->cleanup_plugin = lotus_cleanup_plugin;
	pd->title = g_strdup (_("Lotus 123 file import/export plugin"));

	return 0;
}
