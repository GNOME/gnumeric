/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * utils.c:  Various utility routines that do not depend on the GUI of Gnumeric
 *
 * Authors:
 *    Miguel de Icaza (miguel@gnu.org)
 *    Jukka-Pekka Iivonen (iivonen@iki.fi)
 *    Zbigniew Chyla (cyba@gnome.pl)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "gutils.h"

#include "sheet.h"
#include "ranges.h"
#include "mathfunc.h"
#include "libgnumeric.h"

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <gsf/gsf-impl-utils.h>
#ifdef HAVE_FLOATINGPOINT_H
#include <floatingpoint.h>
#endif

char *
gnm_sys_data_dir (char const *subdir)
{
	if (subdir == NULL)
		return (char *)gnumeric_data_dir;
	return g_build_filename (gnumeric_data_dir, subdir, NULL);
}

char *
gnm_sys_lib_dir (char const *subdir)
{
	return g_build_filename (gnumeric_lib_dir, subdir, NULL);
}

#define GLADE_SUFFIX	"glade"
#define PLUGIN_SUFFIX	"plugins"

char *
gnm_sys_glade_dir (void)
{
	return gnm_sys_data_dir (GLADE_SUFFIX);
}

char *
gnm_sys_plugin_dir (void)
{
	return gnm_sys_lib_dir (PLUGIN_SUFFIX);
}

char *
gnm_usr_dir (char const *subdir)
{
	char const *home_dir = g_get_home_dir ();

	if (!home_dir)
		return NULL;

	return g_build_filename (home_dir, ".gnumeric",
				 GNUMERIC_VERSION, subdir,
				 NULL);
}

char *
gnm_usr_plugin_dir (void)
{
	return gnm_usr_dir (PLUGIN_SUFFIX);
}
