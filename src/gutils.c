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

#include <goffice/goffice.h>

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <locale.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-timestamp.h>

static char *gnumeric_lib_dir;
static char *gnumeric_data_dir;
static char *gnumeric_icon_dir;
static char *gnumeric_locale_dir;
static char *gnumeric_usr_dir;
static char *gnumeric_usr_dir_unversioned;
static char *gnumeric_extern_plugin_dir;

static gboolean
running_in_tree (void)
{
	const char *argv0 = g_get_prgname ();

	if (!argv0)
		return FALSE;

	/* Sometime we see, e.g., "lt-gnumeric" as basename.  */
	{
		char *base = g_path_get_basename (argv0);
		gboolean has_lt_prefix = (strncmp (base, "lt-", 3) == 0);
		g_free (base);
		if (has_lt_prefix)
			return TRUE;
	}

	/* Look for ".libs" as final path element.  */
	{
		const char *dotlibs = strstr (argv0, ".libs/");
		if (dotlibs &&
		    (dotlibs == argv0 || G_IS_DIR_SEPARATOR (dotlibs[-1])) &&
		    strchr (dotlibs + 6, G_DIR_SEPARATOR) == NULL)
			return TRUE;
	}

	return FALSE;
}

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
	gnumeric_extern_plugin_dir = g_build_filename
		(dir, "lib", "gnumeric", GNM_API_VERSION, "plugins",
		 NULL);
	g_free (dir);
#else
	if (running_in_tree ()) {
		const char *argv0 = g_get_prgname ();
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
		if (0) g_printerr ("Running in-tree\n");
	}

	if (!gnumeric_lib_dir)
		gnumeric_lib_dir = g_strdup (GNUMERIC_LIBDIR);
	gnumeric_data_dir = g_strdup (GNUMERIC_DATADIR);
	gnumeric_icon_dir = g_strdup (GNUMERIC_ICONDIR);
	gnumeric_locale_dir = g_strdup (GNUMERIC_LOCALEDIR);
	gnumeric_extern_plugin_dir = g_strdup (GNUMERIC_EXTERNPLUGINDIR);
#endif
	home_dir = g_get_home_dir ();
	gnumeric_usr_dir_unversioned = home_dir
		? g_build_filename (home_dir, ".gnumeric", NULL)
		: NULL;
	gnumeric_usr_dir = gnumeric_usr_dir_unversioned
		? g_build_filename (gnumeric_usr_dir_unversioned, GNM_VERSION_FULL, NULL)
		: NULL;
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
	g_free (gnumeric_usr_dir_unversioned);
	gnumeric_usr_dir_unversioned = NULL;
	g_free (gnumeric_extern_plugin_dir);
	gnumeric_extern_plugin_dir = NULL;
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
gnm_sys_extern_plugin_dir (void)
{
	return gnumeric_extern_plugin_dir;
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
gnm_usr_dir (gboolean versioned)
{
	return versioned ? gnumeric_usr_dir : gnumeric_usr_dir_unversioned;
}


static gboolean
all_ascii (const char *s)
{
	while ((guchar)*s < 0x7f) {
		if (*s)
			s++;
		else
			return TRUE;
	}
	return FALSE;
}

/*
 * Like strto[ld], but...
 * 1. handles non-ascii characters
 * 2. disallows 0x000.0p+00 and 0.0d+00
 * 3. ensures sane errno on exit
 */
gnm_float
gnm_utf8_strto (const char *s, char **end)
{
	const char *p;
	int sign;
	char *dummy_end;
	GString *ascii;
	GString const *decimal = go_locale_get_decimal ();
	gboolean seen_decimal = FALSE;
	gboolean seen_digit = FALSE;
	size_t spaces = 0;
	gnm_float res;
	int save_errno;

	if (all_ascii (s))
		return gnm_strto (s, end);

	ascii = g_string_sized_new (100);

	if (!end)
		end = &dummy_end;

	p = s;
	while (g_unichar_isspace (g_utf8_get_char (p))) {
		p = g_utf8_next_char (p);
		spaces++;
	}

	sign = go_unichar_issign (g_utf8_get_char (p));
	if (sign) {
		g_string_append_c (ascii, "-/+"[sign + 1]);
		p = g_utf8_next_char (p);
	}

	do {
		if (strncmp (p, decimal->str, decimal->len) == 0) {
			if (seen_decimal)
				break;
			seen_decimal = TRUE;
			go_string_append_gstring (ascii, decimal);
			p += decimal->len;
		} else if (g_unichar_isdigit (g_utf8_get_char (p))) {
			g_string_append_c (ascii, '0' + g_unichar_digit_value (g_utf8_get_char (p)));
			p = g_utf8_next_char (p);
			seen_digit = TRUE;
		} else
			break;
	} while (1);

	if (!seen_digit) {
		/* No conversion, bail to gnm_strto for nan etc. */
		g_string_free (ascii, TRUE);
		return gnm_strto (s, end);
	}

	if (*p == 'e' || *p == 'E') {
		int sign;

		g_string_append_c (ascii, 'e');
		p = g_utf8_next_char (p);

		sign = go_unichar_issign (g_utf8_get_char (p));
		if (sign) {
			g_string_append_c (ascii, "-/+"[sign + 1]);
			p = g_utf8_next_char (p);
		}
		while (g_unichar_isdigit (g_utf8_get_char (p))) {
			g_string_append_c (ascii, '0' + g_unichar_digit_value (g_utf8_get_char (p)));
			p = g_utf8_next_char (p);
		}
	}

	res = gnm_strto (ascii->str, end);
	save_errno = errno;
	*end = g_utf8_offset_to_pointer
		(s, spaces + g_utf8_pointer_to_offset (ascii->str, *end));
	g_string_free (ascii, TRUE);

	errno = save_errno;
	return res;
}

/*
 * Like strtol, but...
 * 1. handles non-ascii characters
 * 2. assumes base==10
 * 3. ensures sane errno on exit
 */
long
gnm_utf8_strtol (const char *s, char **end)
{
	const char *p;
	int sign;
	char *dummy_end;
	unsigned long res = 0, lim, limd;

	if (!end)
		end = &dummy_end;

	p = s;
	while (g_unichar_isspace (g_utf8_get_char (p)))
		p = g_utf8_next_char (p);

	sign = go_unichar_issign (g_utf8_get_char (p));
	if (sign)
		p = g_utf8_next_char (p);
	if (sign < 0) {
		lim = (-(unsigned long)LONG_MIN) / 10u;
		limd = (-(unsigned long)LONG_MIN) % 10u;
	} else {
		lim = (unsigned long)LONG_MAX / 10u;
		limd = (unsigned long)LONG_MAX % 10u;
	}

	if (!g_unichar_isdigit (g_utf8_get_char (p))) {
		errno = 0;
		*end = (char *)s;
		return 0;
	}

	while (g_unichar_isdigit (g_utf8_get_char (p))) {
		guint8 dig = g_unichar_digit_value (g_utf8_get_char (p));
		p = g_utf8_next_char (p);

		if (res > lim || (res == lim && dig > limd)) {
			/* Overflow */
			while (g_unichar_isdigit (g_utf8_get_char (p)))
				p = g_utf8_next_char (p);
			*end = (char *)p;
			errno = ERANGE;
			return sign < 0 ? LONG_MIN : LONG_MAX;
		}

		res = res * 10u + dig;
	}
	*end = (char *)p;
	errno = 0;
	return sign < 0 ? (long)-res : (long)res;
}


int
gnm_regcomp_XL (GORegexp *preg, char const *pattern, int cflags,
		gboolean anchor_start, gboolean anchor_end)
{
	GString *res = g_string_new (NULL);
	int retval;

	if (anchor_start)
		g_string_append_c (res, '^');

	while (*pattern) {
		switch (*pattern) {
		case '*':
			g_string_append (res, ".*");
			pattern++;
			break;

		case '?':
			g_string_append_c (res, '.');
			pattern++;
			break;

		case '~':
			if (pattern[1] == '*' ||
			    pattern[1] == '?' ||
			    pattern[1] == '~')
				pattern++;
			/* Fall through */
		default:
			pattern = go_regexp_quote1 (res, pattern);
		}
	}

	if (anchor_end)
		g_string_append_c (res, '$');

	retval = go_regcomp (preg, res->str, cflags);
	g_string_free (res, TRUE);
	return retval;
}

/**
 * gnm_excel_search_impl:
 * @needle: the pattern to search for, see gnm_regcomp_XL.
 * @haystack: the string to search in.
 * @skip: zero-based search start point in characters.
 *
 * Returns: -1 for a non-match, or zero-based location in
 * characters.
 *
 * The is the implementation of Excel's SEARCH function.
 * However, note that @skip and return value are zero-based.
 */
int
gnm_excel_search_impl (const char *needle, const char *haystack,
		       size_t skip)
{
	const char *hay2;
	size_t i;
	GORegexp r;

	for (i = skip, hay2 = haystack; i > 0; i--) {
		if (*hay2 == 0)
			return -1;
		hay2 = g_utf8_next_char (hay2);
	}

	if (gnm_regcomp_XL (&r, needle, GO_REG_ICASE, FALSE, FALSE) == GO_REG_OK) {
		GORegmatch rm;

		switch (go_regexec (&r, hay2, 1, &rm, 0)) {
		case GO_REG_NOMATCH:
			break;
		case GO_REG_OK:
			go_regfree (&r);
			return skip +
				g_utf8_pointer_to_offset (hay2, hay2 + rm.rm_so);
		default:
			g_warning ("Unexpected go_regexec result");
		}
		go_regfree (&r);
	} else {
		g_warning ("Unexpected regcomp result");
	}

	return -1;
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


static gboolean
cb_gnm_pango_attr_list_equal (PangoAttribute *a, gpointer _sl)
{
	GSList **sl = _sl;
	*sl = g_slist_prepend (*sl, a);
	return FALSE;
}

/*
 * This is a bit of a hack.  It might claim a difference even when things
 * actually are equal.  But not the other way around.
 */
gboolean
gnm_pango_attr_list_equal (PangoAttrList const *l1, PangoAttrList const *l2)
{
	if (l1 == l2)
		return TRUE;
	else if (l1 == NULL || l2 == NULL)
		return FALSE;
	else {
		gboolean res;
		GSList *sl1 = NULL, *sl2 = NULL;
		(void)pango_attr_list_filter ((PangoAttrList *)l1,
					      cb_gnm_pango_attr_list_equal,
					      &sl1);
		(void)pango_attr_list_filter ((PangoAttrList *)l2,
					      cb_gnm_pango_attr_list_equal,
					      &sl2);

		while (sl1 && sl2) {
			const PangoAttribute *a1 = sl1->data;
			const PangoAttribute *a2 = sl2->data;
			if (a1->start_index != a2->start_index ||
			    a1->end_index != a2->end_index ||
			    !pango_attribute_equal (a1, a2))
				break;
			sl1 = g_slist_delete_link (sl1, sl1);
			sl2 = g_slist_delete_link (sl2, sl2);
		}

		res = (sl1 == sl2);
		g_slist_free (sl1);
		g_slist_free (sl2);
		return res;
	}
}

/* ------------------------------------------------------------------------- */

struct _GnmLocale {
	char *num_locale;
	char *monetary_locale;
};
/**
 * gnm_push_C_locale: (skip)
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
 * gnm_pop_C_locale: (skip)
 * @locale: #GnmLocale
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

/* ------------------------------------------------------------------------- */

gboolean
gnm_debug_flag (const char *flag)
{
	GDebugKey key;
	key.key = (char *)flag;
	key.value = 1;

	return g_parse_debug_string (g_getenv ("GNM_DEBUG"), &key, 1) != 0;
}

/* ------------------------------------------------------------------------- */

void
gnm_string_add_number (GString *buf, gnm_float d)
{
	size_t old_len = buf->len;
	double d2;
	static int digits;

	if (digits == 0) {
		gnm_float l10 = gnm_log10 (FLT_RADIX);
		digits = (int)gnm_ceil (GNM_MANT_DIG * l10) +
			(l10 == (int)l10 ? 0 : 1);
	}

	g_string_append_printf (buf, "%.*" GNM_FORMAT_g, digits - 1, d);
	d2 = gnm_strto (buf->str + old_len, NULL);

	if (d != d2) {
		g_string_truncate (buf, old_len);
		g_string_append_printf (buf, "%.*" GNM_FORMAT_g, digits, d);
	}
}

/* ------------------------------------------------------------------------- */

void
gnm_insert_meta_date (GODoc *doc, char const *name)
{
	GValue *value = g_new0 (GValue, 1);
	GTimeVal tm;
	GsfTimestamp *ts = gsf_timestamp_new ();

	g_get_current_time (&tm);
	tm.tv_usec = 0L;

	gsf_timestamp_set_time (ts, tm.tv_sec);
	g_value_init (value, GSF_TIMESTAMP_TYPE);
	gsf_timestamp_to_value (ts, value);
	gsf_timestamp_free (ts);

	gsf_doc_meta_data_insert (go_doc_get_meta_data (doc),
				  g_strdup (name),
				  value);
}

/* ------------------------------------------------------------------------- */

gboolean
gnm_object_get_bool (gpointer o, const char *name)
{
	gboolean b;
	g_object_get (o, name, &b, NULL);
	return b;
}

gboolean
gnm_object_has_readable_prop (gconstpointer obj, const char *property,
			      GType typ, gpointer pres)
{
	GObjectClass *klass;
	GParamSpec *spec;

	if (!obj)
		return FALSE;

	klass =  G_OBJECT_GET_CLASS (G_OBJECT (obj));
	spec = g_object_class_find_property (klass, property);
	if (!spec ||
	    !(G_PARAM_READABLE & spec->flags) ||
	    (typ != G_TYPE_NONE && spec->value_type != typ))
		return FALSE;

	if (pres)
		g_object_get (G_OBJECT (obj), property, pres, NULL);
	return TRUE;
}




gint
gnm_float_equal (gnm_float const *a, const gnm_float *b)
{
	return (*a == *b);
}

/* ------------------------------------------------------------------------- */

guint
gnm_float_hash (gnm_float const *d)
{
	int expt;
	gnm_float mant = gnm_frexp (gnm_abs (*d), &expt);
	guint h = ((guint)(0x80000000u * mant)) ^ expt;
	if (*d >= 0)
		h ^= 0x55555555;
	return h;
}

/* ------------------------------------------------------------------------- */

struct cb_compare {
	GnmHashTableOrder order;
	gpointer user;
};

static gint
cb_compare (gconstpointer a_, gconstpointer b_, gpointer user_data)
{
	struct cb_compare *user = user_data;
	gpointer *a = (gpointer )a_;
	gpointer *b = (gpointer )b_;

	return user->order (a[0], a[1], b[0], b[1], user->user);
}


/**
 * gnm_hash_table_foreach_ordered:
 * @h: Hash table
 * @callback: (scope async): #GHFunc
 * @order: (scope async): Ordering function
 * @user: user data for callback and order
 *
 * Like g_hash_table_foreach, but with an ordering imposed.
 **/
void
gnm_hash_table_foreach_ordered (GHashTable *h,
				GHFunc callback,
				GnmHashTableOrder order,
				gpointer user)
{
	unsigned ui;
	GPtrArray *data;
	struct cb_compare u;
	GHashTableIter hiter;
	gpointer key, value;

	/* Gather all key-value pairs */
	data = g_ptr_array_new ();
	g_hash_table_iter_init (&hiter, h);
	while (g_hash_table_iter_next (&hiter, &key, &value)) {
		g_ptr_array_add (data, key);
		g_ptr_array_add (data, value);
	}

	/* Sort according to given ordering */
	u.order = order;
	u.user = user;
	g_qsort_with_data (data->pdata,
			   data->len / 2, 2 * sizeof (gpointer),
			   cb_compare,
			   &u);

	/* Call user callback with all pairs */
	for (ui = 0; ui < data->len; ui += 2)
		callback (g_ptr_array_index (data, ui),
			  g_ptr_array_index (data, ui + 1),
			  user);

	/* Clean up */
	g_ptr_array_free (data, TRUE);
}
