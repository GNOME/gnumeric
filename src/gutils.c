/*
 * utils.c:  Various utility routines that do not depend on the GUI of Gnumeric
 *
 * Author:
 *    Miguel de Icaza (miguel@gnu.org)
 *    Jukka-Pekka Iivonen (iivonen@iki.fi)
 */
#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <gnome.h>
#include <gal/util/e-util.h>
#include "numbers.h"
#include "gutils.h"
#include "gnumeric.h"
#include "sheet.h"
#include "ranges.h"

GList *
g_create_list (gpointer item1, ...)
{
	va_list args;
	GList *list = NULL;
	gpointer item;

	va_start (args, item1);
	for (item = item1; item != NULL; item = va_arg (args, gpointer)) {
		list = g_list_prepend (list, item);
	}		

	return g_list_reverse (list);
}

gint
g_lang_score_in_lang_list (gchar *lang, GList *lang_list)
{
	if (lang_list == NULL) {
		lang_list = gnome_i18n_get_language_list ("LC_MESSAGES");
	}
	if (lang == NULL) {
		return g_list_length (lang_list);
	} else {
		GList *lang_item;

		lang_item = g_list_find_custom (lang_list, lang, g_str_compare);
		if (lang_item != NULL) {
			return g_list_position (lang_list, lang_item);
		} else {
			return G_MAXINT;
		}
	}
}

gint
gnumeric_strcase_equal (gconstpointer v, gconstpointer v2)
{
	return g_strcasecmp ((const gchar*) v, (const gchar*)v2) == 0;
}

/* a char* hash function from ASU */
guint
gnumeric_strcase_hash (gconstpointer v)
{
	const unsigned char *s = (const unsigned char *)v;
	const unsigned char *p;
	guint h = 0, g;

	for(p = s; *p != '\0'; p += 1) {
		h = ( h << 4 ) + tolower (*p);
		if ( ( g = h & 0xf0000000 ) ) {
			h = h ^ (g >> 24);
			h = h ^ g;
		}
	}

	return h /* % M */;
}

char *
gnumeric_sys_data_dir (const char *subdir)
{
	extern char *gnumeric_data_dir;

	return g_strconcat (gnumeric_data_dir, G_DIR_SEPARATOR_S,
			    subdir, G_DIR_SEPARATOR_S, NULL);
}

char *
gnumeric_sys_lib_dir (const char *subdir)
{
	extern char *gnumeric_lib_dir;

	return g_strconcat (gnumeric_lib_dir, G_DIR_SEPARATOR_S,
			    subdir, G_DIR_SEPARATOR_S, NULL);
}

#define GLADE_SUFFIX	"glade"
#define PLUGIN_SUFFIX	"plugins"

char *
gnumeric_sys_glade_dir (void)
{
	return gnumeric_sys_data_dir (GLADE_SUFFIX);
}

char *
gnumeric_sys_plugin_dir (void)
{
	return gnumeric_sys_lib_dir (PLUGIN_SUFFIX);
}

char *
gnumeric_usr_dir (const char *subdir)
{
	const char *home_dir = g_get_home_dir ();

	if (home_dir != NULL) {
		gboolean has_slash = (home_dir[strlen (home_dir) - 1] == G_DIR_SEPARATOR);
		return g_strconcat (home_dir, (has_slash ? "" : G_DIR_SEPARATOR_S),
				    ".gnumeric" G_DIR_SEPARATOR_S GNUMERIC_VERSION G_DIR_SEPARATOR_S,
				    subdir, G_DIR_SEPARATOR_S,
				    NULL);
	}
	return NULL;
}

char *
gnumeric_usr_plugin_dir (void)
{
	return gnumeric_usr_dir (PLUGIN_SUFFIX);
}

/* ------------------------------------------------------------------------- */
/*
 * Note: the code below might look awful, but fixed-sized memcpy ought to
 * produce reasonable code.
 */

gint16
gnumeric_get_le_int16 (const void *p)
{
	gint16 data;
	memcpy (&data, p, sizeof (data));
	return GINT16_FROM_LE (data);
}

guint16
gnumeric_get_le_uint16 (const void *p)
{
	guint16 data;
	memcpy (&data, p, sizeof (data));
	return GUINT16_FROM_LE (data);
}

gint32
gnumeric_get_le_int32 (const void *p)
{
	gint32 data;
	memcpy (&data, p, sizeof (data));
	return GINT32_FROM_LE (data);
}

guint32
gnumeric_get_le_uint32 (const void *p)
{
	guint32 data;
	memcpy (&data, p, sizeof (data));
	return GUINT32_FROM_LE (data);
}

double
gnumeric_get_le_double (const void *p)
{
#if G_BYTE_ORDER == G_BIG_ENDIAN
	if (sizeof (double) == 8) {
		double  d;
		int     i;
		guint8 *t  = (guint8 *)&d;
		guint8 *p2 = (guint8 *)p;
		int     sd = sizeof (d);

		for (i = 0; i < sd; i++)
			t[i] = p2[sd - 1 - i];

		return d;
	} else {
		g_error ("Big endian machine, but weird size of doubles");
	}
#elif G_BYTE_ORDER == G_LITTLE_ENDIAN
	if (sizeof (double) == 8) {
		/*
		 * On i86, we could access directly, but Alphas require
		 * aligned access.
		 */
		double data;
		memcpy (&data, p, sizeof (data));
		return data;
	} else {
		g_error ("Little endian machine, but weird size of doubles");
	}
#else
#error "Byte order not recognised -- out of luck"
#endif
}


void
gnumeric_set_le_double (void *p, double d)
{
#if G_BYTE_ORDER == G_BIG_ENDIAN
	if (sizeof (double) == 8) {
		int     i;
		guint8 *t  = (guint8 *)&d;
		guint8 *p2 = (guint8 *)p;
		int     sd = sizeof (d);

		for (i = 0; i < sd; i++)
			p2[sd - 1 - i] = t[i];
	} else {
		g_error ("Big endian machine, but weird size of doubles");
	}
#elif G_BYTE_ORDER == G_LITTLE_ENDIAN
	if (sizeof (double) == 8) {
		/*
		 * On i86, we could access directly, but Alphas require
		 * aligned access.
		 */
		memcpy (p, &d, sizeof (d));
	} else {
		g_error ("Little endian machine, but weird size of doubles");
	}
#else
#error "Byte order not recognised -- out of luck"
#endif
}

/* ------------------------------------------------------------------------- */
