/*
 * utils.c:  Various utility routines that do not depend on the GUI of Gnumeric
 *
 * Authors:
 *    Miguel de Icaza (miguel@gnu.org)
 *    Jukka-Pekka Iivonen (iivonen@iki.fi)
 *    Zbigniew Chyla (cyba@gnome.pl)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "gutils.h"

#include "sheet.h"
#include "ranges.h"

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-config.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <gal/util/e-util.h>
#ifdef HAVE_FLOATINGPOINT_H
#include <floatingpoint.h>
#endif

/* ------------------------------------------------------------------------- */

static GList *timers_stack = NULL;

void
gnumeric_time_counter_push (void)
{
	GTimer *timer;

	timer = g_timer_new ();
	timers_stack = g_list_prepend (timers_stack, timer);
}

gdouble
gnumeric_time_counter_pop (void)
{
	GTimer *timer;
	gdouble ret_val;

	g_assert (timers_stack != NULL);

	timer = (GTimer *) timers_stack->data;
	timers_stack = g_list_remove (timers_stack, timers_stack->data);
	ret_val = g_timer_elapsed (timer, NULL);
	g_timer_destroy (timer);

	return ret_val;
}

/**
 * gnumeric_config_get_string_list:
 * @config_path: GNOME configuration path or its prefix if
 *               @item_name_prefix != NULL.
 * @item_name_prefix: Prefix of key name when reading multiple strings from
 *                    configuration.
 *
 * Reads list of string values from GNOME configuration.
 * If @item_name_prefix == NULL it gets a vector from configuration and then
 * converts it to GList. If @item_name_prefix != NULL, it gets string values
 * with keys of the form @item_name_prefix%d.
 *
 * Return value: list of newly allocated strings which you should free after
 * use using function e_free_string_list().
 */
GList *
gnumeric_config_get_string_list (const gchar *config_path,
                                 const gchar *item_name_prefix)
{
	GList *items = NULL;
	gint i;

	if (item_name_prefix != NULL) {
		gnome_config_push_prefix (config_path);
		for (i = 0; ; i++) {
			gchar *key, *value;

			key = g_strdup_printf ("%s%d", item_name_prefix, i);
			value = gnome_config_get_string (key);
			g_free (key);
			if (value != NULL) {
				items = g_list_prepend (items, value);
			} else {
				break;
			}
		}
		gnome_config_pop_prefix ();
		items = g_list_reverse (items);
	} else {
		gchar **itemv;
		gint n_items;

		gnome_config_get_vector (config_path, &n_items, &itemv);
		for (i = 0; i < n_items; i++) {
			items = g_list_prepend (items, itemv[i]);
		}
		g_free (itemv);
	}

	return items;;
}

/**
 * gnumeric_config_set_string_list:
 * @config_path: GNOME configuration path or its prefix if
 *               @item_name_prefix != NULL.
 * @item_name_prefix: Prefix of key name when writing multiple strings to
 *                    configuration.
 *
 * Stores list of string values in GNOME configuration.
 * If @item_name_prefix == NULL it converts @items to vector and stores it in
 * configuration. If @item_name_prefix != NULL, it stores mulitple strings
 * with keys of the form @item_name_prefix%d.
 */
void
gnumeric_config_set_string_list (GList *items,
                                 const gchar *config_path,
                                 const gchar *item_name_prefix)
{
	GList *l;
	gint i;

	if (item_name_prefix != NULL) {
		gchar *key;

		gnome_config_push_prefix (config_path);
		for (l = items, i = 0; l != NULL; l = l->next, i++) {

			key = g_strdup_printf ("%s%d", item_name_prefix, i);
			gnome_config_set_string (key, (gchar *) l->data);
			g_free (key);
		}
		key = g_strdup_printf ("%s%d", item_name_prefix, i);
		gnome_config_clean_key (key);
		g_free (key);
		gnome_config_pop_prefix ();
	} else {
		const gchar **itemv;
		gint n_items;

		n_items = g_list_length (items);
		if (n_items > 0) {
			itemv = g_new (const gchar *, n_items);
			for (l = items, i = 0; l != NULL; l = l->next, i++) {
				itemv[i] = (const gchar *) l->data;
			}
			gnome_config_set_vector (config_path, n_items, itemv);
			g_free (itemv);
		} else {
			gnome_config_set_vector (config_path, 0, NULL);
		}
	}
}

void
g_ptr_array_insert (GPtrArray *array, gpointer value, int index)
{
	if ((int)array->len != index) {
		int i = array->len - 1;
		gpointer last = g_ptr_array_index (array, i);
		g_ptr_array_add (array, last);

		while (i-- > index) {
			gpointer tmp = g_ptr_array_index (array, i);
			g_ptr_array_index (array, i+1) = tmp;
		}
		g_ptr_array_index (array, index) = value;
	} else
		g_ptr_array_add (array, value);
}

/**
 * g_create_list:
 * @item1: First item.
 *
 * Creates a GList from NULL-terminated list of arguments.
 *
 * Return value: created list.
 */
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
	va_end (args);

	return g_list_reverse (list);
}

gint
g_list_index_custom (GList *list, gpointer data, GCompareFunc cmp_func)
{
	GList *l;
	gint i;

	for (l = list, i = 0; l != NULL; l = l->next, i++) {
		if (cmp_func (l->data, data) == 0) {
			return i;
		}
	}

	return -1;
}

/**
 * g_list_free_custom:
 * @list: list of some items
 * @free_func: function freeing list item
 *
 * Clears a list, calling @free_func for each list item.
 *
 */
void
g_list_free_custom (GList *list, GFreeFunc free_func)
{
	GList *l;

	for (l = list; l != NULL; l = l->next) {
		free_func (l->data);
	}
	g_list_free (list);
}

/**
 * g_string_list_copy:
 * @list: List of strings.
 *
 * Creates a copy of the given string list (strings are also copied using
 * g_strdup).
 *
 * Return value: new copy of the list which you should free after use using
 *               function e_free_string_list()
 */
GList *
g_string_list_copy (GList *list)
{
	GList *list_copy = NULL, *l;

	for (l = list; l != NULL; l = l->next) {
		list_copy = g_list_prepend (list_copy, g_strdup ((gchar *) l->data));
	}
	list_copy = g_list_reverse (list_copy);

	return list_copy;
}

/**
 * g_strsplit_to_list:
 * @string: String to split
 * @delimiter: Token delimiter
 *
 * Splits up string into tokens at delim and returns a string list.
 *
 * Return value: string list which you should free after use using function
 * e_free_string_list().
 *
 */
GList *
g_strsplit_to_list (const gchar *string, const gchar *delimiter)
{
	gchar **token_v;
	GList *string_list = NULL;
	gint i;

	token_v = g_strsplit (string, delimiter, 0);
	if (token_v != NULL) {
		for (i = 0; token_v[i] != NULL; i++) {
			string_list = g_list_prepend (string_list, token_v[i]);
		}
		string_list = g_list_reverse (string_list);
		g_free (token_v);
	}

	return string_list;
}

/**
 * g_slist_free_custom:
 * @list: list of some items
 * @free_func: function freeing list item
 *
 * Clears a list, calling g_free() for each list item.
 *
 */
void
g_slist_free_custom (GSList *list, GFreeFunc free_func)
{
	GSList *l;

	for (l = list; l != NULL; l = l->next) {
		free_func (l->data);
	}
	g_slist_free (list);
}

/**
 * g_lang_score_in_lang_list:
 *
 */
gint
g_lang_score_in_lang_list (gchar *lang, GList *lang_list)
{
	if (lang_list == NULL)
		lang_list = (GList *)gnome_i18n_get_language_list ("LC_MESSAGES");

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

extern char *gnumeric_data_dir;
char *
gnumeric_sys_data_dir (const char *subdir)
{
	return g_strconcat (gnumeric_data_dir, G_DIR_SEPARATOR_S,
			    subdir, G_DIR_SEPARATOR_S, NULL);
}

extern char *gnumeric_lib_dir;
char *
gnumeric_sys_lib_dir (const char *subdir)
{
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

/*
 * Escapes all backslashes and quotes in a string. It is based on glib's
 * g_strescape.
 *
 * Also adds quotes around the result.
 */
char *
gnumeric_strescape (const char *string)
{
	char *q, *escaped;
	int escapechars = 0;
	const char *p;

	g_return_val_if_fail (string != NULL, NULL);

	for (p = string; *p; p++)
		if (*p == '\\' || *p == '\"')
			escapechars++;

	q = escaped = g_new (char, strlen (string) + escapechars + 3);
	*q++ = '\"';
	for (p = string; *p; p++) {
		if (*p == '\\' || *p == '\"')
			*q++ = '\\';
		*q++ = *p;
	}
	*q++ = '\"';
	*q = '\000';

	return escaped;
}

/* ------------------------------------------------------------------------- */

#ifdef NEED_FAKE_MODFL
gnum_float
fake_modfl (gnum_float x, gnum_float *iptr)
{
	double di;
	gnum_float res;

	res = modf (x, &di);
	*iptr = di;
	return res;
}
#endif

/* ------------------------------------------------------------------------- */

#if defined(WITH_LONG_DOUBLE) && !defined(HAVE_STRTOLD)

gnum_float
strtognum (const char *str, char **end)
{
	gnum_float res;
#if defined(HAVE_STRING_TO_DECIMAL) && defined(HAVE_DECIMAL_TO_QUADRUPLE)
	decimal_record dr;
	enum decimal_string_form form;
	decimal_mode dm;
	fp_exception_field_type excp;
	char *echar;

	string_to_decimal ((char **)&str, strlen (str),
			   0, &dr, &form, &echar);
	if (end) *end = (char *)str;

	if (form == invalid_form) {
		errno = EINVAL;
		return 0.0;
	}

	dm.rd = fp_nearest;
	dm.df = floating_form;
	dm.ndigits = GNUM_DIG;
	decimal_to_quadruple (&res, &dm, &dr, &excp);
#else
	static gboolean warned = FALSE;
	if (!warned) {
		warned = TRUE;
		g_warning (_("This version of Gnumeric has been compiled with inadequate precision in strtognum."));
	}

	res = strtod (str, end);
#endif
	return res;
}

#endif

/* ------------------------------------------------------------------------- */
