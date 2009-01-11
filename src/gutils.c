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
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "gutils.h"
#include "gnumeric-paths.h"

#include "sheet.h"
#include "ranges.h"
#include "mathfunc.h"

#include <goffice/utils/go-format.h>
#include <goffice/utils/go-locale.h>
#include <goffice/utils/go-file.h>

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <locale.h>
#include <gsf/gsf-impl-utils.h>

static char *gnumeric_lib_dir;
static char *gnumeric_data_dir;
static char *gnumeric_icon_dir;
static char *gnumeric_locale_dir;
static char *gnumeric_usr_dir;

void
gutils_init (void)
{
	char const *home_dir;
#ifdef G_OS_WIN32
	gchar *dir = g_win32_get_package_installation_directory_of_module (NULL);
	gnumeric_lib_dir = g_build_filename (dir, "lib",
					     "gnumeric", GNM_VERSION_FULL,
					     NULL);
	gnumeric_data_dir = g_build_filename (dir, "share",
					      "gnumeric", GNM_VERSION_FULL,
					      NULL);
	gnumeric_icon_dir = g_build_filename (dir, "share", "pixmaps",
					      "gnumeric", NULL);
	gnumeric_locale_dir = g_build_filename (dir, "share", "locale", NULL);
	g_free (dir);
#else
	const char *argv0 = g_get_prgname ();
	size_t l0 = argv0 ? strlen (argv0) : 0;
	char *base = argv0 ? g_path_get_basename (argv0) : NULL;
	const char *suff = "/.libs/gnumeric";
	size_t lsuff = strlen (suff);
	gboolean running_in_tree =
		(l0 > lsuff && strcmp (argv0 + l0 - lsuff, suff) == 0) ||
		(base && strncmp (base, "lt-", 3) == 0);

	g_free (base);
	if (running_in_tree) {
		char *dotlibs = g_path_get_dirname (argv0);
		char *top = g_build_filename (dotlibs, "..", "../", NULL);
		char *plugins = g_build_filename (top, PLUGIN_SUBDIR, NULL);
		if (g_file_test (plugins, G_FILE_TEST_IS_DIR))
			gnumeric_lib_dir =
				go_filename_simplify (top, GO_DOTDOT_SYNTACTIC,
						      FALSE);
		g_free (top);
		g_free (plugins);
		g_free (dotlibs);
	}

	if (!gnumeric_lib_dir)
		gnumeric_lib_dir = g_strdup (GNUMERIC_LIBDIR);
	gnumeric_data_dir = g_strdup (GNUMERIC_DATADIR);
	gnumeric_icon_dir = g_strdup (GNUMERIC_ICONDIR);
	gnumeric_locale_dir = g_strdup (GNUMERIC_LOCALEDIR);
#endif
	home_dir = g_get_home_dir ();
	gnumeric_usr_dir = (home_dir == NULL ? NULL :
	   g_build_filename (home_dir, ".gnumeric", GNM_VERSION_FULL, NULL));
}

void
gutils_shutdown (void)
{
	g_free (gnumeric_lib_dir);
	gnumeric_lib_dir = NULL;
	g_free (gnumeric_data_dir);
	gnumeric_data_dir = NULL;
	g_free (gnumeric_icon_dir);
	gnumeric_icon_dir = NULL;
	g_free (gnumeric_locale_dir);
	gnumeric_locale_dir = NULL;
	g_free (gnumeric_usr_dir);
	gnumeric_usr_dir = NULL;
}

char const *
gnm_sys_lib_dir (void)
{
	return gnumeric_lib_dir;
}

char const *
gnm_sys_data_dir (void)
{
	return gnumeric_data_dir;
}

char const *
gnm_icon_dir (void)
{
	return gnumeric_icon_dir;
}

char const *
gnm_locale_dir (void)
{
	return gnumeric_locale_dir;
}

char const *
gnm_usr_dir (void)
{
	return gnumeric_usr_dir;
}

int
gnm_regcomp_XL (GORegexp *preg, char const *pattern, int cflags)
{
	GString *res = g_string_new (NULL);
	int retval;

	while (*pattern) {
		switch (*pattern) {
		case '~':
			pattern++;
			if (*pattern == '*')
				g_string_append (res, "\\*");
			else
				g_string_append_c (res, *pattern);
			if (*pattern) pattern++;
			break;

		case '*':
			g_string_append (res, ".*");
			pattern++;
			break;

		case '?':
			g_string_append_c (res, '.');
			pattern++;
			break;

		default:
			pattern = go_regexp_quote1 (res, pattern);
		}
	}

	retval = go_regcomp (preg, res->str, cflags);
	g_string_free (res, TRUE);
	return retval;
}

#if 0
static char const *
color_to_string (PangoColor color)
{
	static char result[100];
	sprintf (result, "%04x:%04x:%04x", color.red, color.green, color.blue);
	return result;
}

static const char *
enum_name (GType typ, int i)
{
	static char result[100];
	GEnumClass *ec = g_type_class_ref (typ);

	if (ec) {
		GEnumValue *ev = g_enum_get_value (ec, i);
		g_type_class_unref (ec);

		if (ev && ev->value_nick)
			return ev->value_nick;
		if (ev && ev->value_name)
			return ev->value_name;
	}

	sprintf (result, "%d", i);
	return result;
}

static gboolean
cb_gnm_pango_attr_dump (PangoAttribute *attr, gpointer user_data)
{
	g_print ("  start=%u; end=%u\n", attr->start_index, attr->end_index);
	switch (attr->klass->type) {
	case PANGO_ATTR_FAMILY:
		g_print ("    family=\"%s\"\n", ((PangoAttrString *)attr)->value);
		break;
	case PANGO_ATTR_LANGUAGE:
		g_print ("    language=\"%s\"\n", pango_language_to_string (((PangoAttrLanguage *)attr)->value));
		break;
	case PANGO_ATTR_STYLE:
		g_print ("    style=%s\n",
			 enum_name (PANGO_TYPE_STYLE, ((PangoAttrInt *)attr)->value));
		break;
	case PANGO_ATTR_WEIGHT:
		g_print ("    weight=%s\n",
			 enum_name (PANGO_TYPE_WEIGHT, ((PangoAttrInt *)attr)->value));
		break;
	case PANGO_ATTR_VARIANT:
		g_print ("    variant=%s\n",
			 enum_name (PANGO_TYPE_VARIANT, ((PangoAttrInt *)attr)->value));
		break;
	case PANGO_ATTR_STRETCH:
		g_print ("    stretch=%s\n",
			 enum_name (PANGO_TYPE_STRETCH, ((PangoAttrInt *)attr)->value));
		break;
	case PANGO_ATTR_UNDERLINE:
		g_print ("    underline=%s\n",
			 enum_name (PANGO_TYPE_UNDERLINE, ((PangoAttrInt *)attr)->value));
		break;
	case PANGO_ATTR_STRIKETHROUGH:
		g_print ("    strikethrough=%d\n", ((PangoAttrInt *)attr)->value);
		break;
	case PANGO_ATTR_RISE:
		g_print ("    rise=%d\n", ((PangoAttrInt *)attr)->value);
		break;
	case PANGO_ATTR_FALLBACK:
		g_print ("    fallback=%d\n", ((PangoAttrInt *)attr)->value);
		break;
	case PANGO_ATTR_LETTER_SPACING:
		g_print ("    letter_spacing=%d\n", ((PangoAttrInt *)attr)->value);
		break;
	case PANGO_ATTR_SIZE:
		g_print ("    size=%d%s\n",
			 ((PangoAttrSize *)attr)->size,
			 ((PangoAttrSize *)attr)->absolute ? " abs" : "");
		break;
	case PANGO_ATTR_SCALE:
		g_print ("    scale=%g\n", ((PangoAttrFloat *)attr)->value);
		break;
	case PANGO_ATTR_FOREGROUND:
		g_print ("    foreground=%s\n", color_to_string (((PangoAttrColor *)attr)->color));
		break;
	case PANGO_ATTR_BACKGROUND:
		g_print ("    background=%s\n", color_to_string (((PangoAttrColor *)attr)->color));
		break;
	case PANGO_ATTR_UNDERLINE_COLOR:
		g_print ("    underline_color=%s\n", color_to_string (((PangoAttrColor *)attr)->color));
		break;
	case PANGO_ATTR_STRIKETHROUGH_COLOR:
		g_print ("    strikethrough_color=%s\n", color_to_string (((PangoAttrColor *)attr)->color));
		break;
	case PANGO_ATTR_FONT_DESC: {
		char *desc = pango_font_description_to_string (((PangoAttrFontDesc*)attr)->desc);
		g_print  ("    font=\"%s\"\n", desc);
		g_free (desc);
		break;
	}
	default:
		g_print ("    type=%s\n", enum_name (PANGO_TYPE_ATTR_TYPE, attr->klass->type));
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

struct _GnmLocale {
	char *num_locale;
	char *monetary_locale;
};
/**
 * gnm_push_C_locale :
 *
 * Returns the current locale, and sets the locale and the value-format
 * engine's locale to 'C'.  The caller must call gnm_pop_C_locale to free the
 * result and restore the previous locale.
 **/
GnmLocale *
gnm_push_C_locale (void)
{
	GnmLocale *old = g_new0 (GnmLocale, 1);

	old->num_locale = g_strdup (go_setlocale (LC_NUMERIC, NULL));
	go_setlocale (LC_NUMERIC, "C");
	old->monetary_locale = g_strdup (go_setlocale (LC_MONETARY, NULL));
	go_setlocale (LC_MONETARY, "C");
	go_locale_untranslated_booleans ();

	return old;
}

/**
 * gnm_pop_C_locale :
 * @locale : #GnmLocale
 *
 * Frees the result of gnm_push_C_locale and restores the original locale.
 **/
void
gnm_pop_C_locale (GnmLocale *locale)
{
	/* go_setlocale restores bools to locale translation */
	go_setlocale (LC_MONETARY, locale->monetary_locale);
	g_free (locale->monetary_locale);
	go_setlocale (LC_NUMERIC, locale->num_locale);
	g_free (locale->num_locale);
	g_free (locale);
}
