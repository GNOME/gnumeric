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

#if 0
static const char *
color_to_string (PangoColor color)
{
	static char result[100];
	sprintf (result, "%04x:%04x:%04x", color.red, color.green, color.blue);
	return result;
}

static gboolean
cb_gnm_pango_attr_dump (PangoAttribute *attr, gpointer user_data)
{
	g_print ("  start=%u; end=%u\n", attr->start_index, attr->end_index);
	switch (attr->klass->type) {
	case PANGO_ATTR_FAMILY: g_print ("    family=\"%s\"\n", ((PangoAttrString *)attr)->value); break;
	case PANGO_ATTR_LANGUAGE: g_print ("    language=\"%s\"\n", pango_language_to_string (((PangoAttrLanguage *)attr)->value)); break;
	case PANGO_ATTR_STYLE: g_print ("    style=%d\n", ((PangoAttrInt *)attr)->value); break;
	case PANGO_ATTR_WEIGHT: g_print ("    weight=%d\n", ((PangoAttrInt *)attr)->value); break;
	case PANGO_ATTR_VARIANT: g_print ("    variant=%d\n", ((PangoAttrInt *)attr)->value); break;
	case PANGO_ATTR_STRETCH: g_print ("    stretch=%d\n", ((PangoAttrInt *)attr)->value); break;
	case PANGO_ATTR_UNDERLINE: g_print ("    underline=%d\n", ((PangoAttrInt *)attr)->value); break;
	case PANGO_ATTR_STRIKETHROUGH: g_print ("    strikethrough=%d\n", ((PangoAttrInt *)attr)->value); break;
	case PANGO_ATTR_RISE: g_print ("    rise=%d\n", ((PangoAttrInt *)attr)->value); break;
	case PANGO_ATTR_FALLBACK: g_print ("    fallback=%d\n", ((PangoAttrInt *)attr)->value); break;
	case PANGO_ATTR_LETTER_SPACING: g_print ("    letter_spacing=%d\n", ((PangoAttrInt *)attr)->value); break;
	case PANGO_ATTR_SIZE: g_print ("    size=%d%s\n",
				       ((PangoAttrSize *)attr)->size,
				       ((PangoAttrSize *)attr)->absolute ? " abs" : ""); break;
	case PANGO_ATTR_SCALE: g_print ("    scale=%g\n", ((PangoAttrFloat *)attr)->value); break;
	case PANGO_ATTR_FOREGROUND: g_print ("    foreground=%s\n", color_to_string (((PangoAttrColor *)attr)->color)); break;
	case PANGO_ATTR_BACKGROUND: g_print ("    background=%s\n", color_to_string (((PangoAttrColor *)attr)->color)); break;
	case PANGO_ATTR_UNDERLINE_COLOR: g_print ("    underline_color=%s\n", color_to_string (((PangoAttrColor *)attr)->color)); break;
	case PANGO_ATTR_STRIKETHROUGH_COLOR: g_print ("    strikethrough_color=%s\n", color_to_string (((PangoAttrColor *)attr)->color)); break;
	default: g_print ("    type=%d\n", attr->klass->type);
	}

	return FALSE;
}

void
gnm_pango_attr_dump (PangoAttrList *list)
{
	g_print ("PangoAttrList at %p\n", list);
	pango_attr_list_filter (list, cb_gnm_pango_attr_dump, NULL);
}
#endif

/* ------------------------------------------------------------------------- */
