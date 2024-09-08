/*
 * gutils.c: Various utility routines that do not depend on the GUI of Gnumeric
 *
 * Authors:
 *    Miguel de Icaza (miguel@gnu.org)
 *    Jukka-Pekka Iivonen (iivonen@iki.fi)
 *    Zbigniew Chyla (cyba@gnome.pl)
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <gutils.h>
#include <gnumeric-paths.h>

#include <sheet.h>
#include <ranges.h>
#include <mathfunc.h>
#include <workbook-view.h>
#include <workbook.h>
#include <workbook-priv.h>

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

#define SHEET_SELECTION_KEY "sheet-selection"
#define SSCONVERT_SHEET_SET_KEY "ssconvert-sheets"

static char *gnumeric_lib_dir;
static char *gnumeric_data_dir;
static char *gnumeric_locale_dir;
static char *gnumeric_usr_dir;
static char *gnumeric_usr_dir_unversioned;
static char *gnumeric_extern_plugin_dir;
static GSList *gutils_xml_in_docs;

static char *
running_in_tree (void)
{
	const char *argv0 = gnm_get_argv0 ();

	if (!argv0)
		return NULL;

	/* Look for ".libs" as final path element.  */
	{
		const char *dotlibs = strstr (argv0, ".libs/");
		if (dotlibs &&
		    (dotlibs == argv0 || G_IS_DIR_SEPARATOR (dotlibs[-1])) &&
		    strchr (dotlibs + 6, G_DIR_SEPARATOR) == NULL) {
			size_t l = dotlibs - argv0;
			char *res = g_strndup (argv0, l);

			while (l > 0 && G_IS_DIR_SEPARATOR (res[l - 1]))
				res[--l] = 0;
			while (l > 0 && !G_IS_DIR_SEPARATOR (res[l - 1]))
				res[--l] = 0;
			while (l > 0 && G_IS_DIR_SEPARATOR (res[l - 1]))
				res[--l] = 0;

			return res;
		}
	}

	{
		const char *builddir = g_getenv ("GNM_TEST_TOP_BUILDDIR");
		if (builddir)
			return g_strdup (builddir);
	}

	return NULL;
}

static gboolean gutils_inited = FALSE;

void
gutils_init (void)
{
	char const *home_dir;
	char *top_builddir;

	// This function will end up being called twice in normal operation:
	// once from gnm_pre_parse_init and once from gnm_init.  Introspection
	// will not get the first.
	if (gutils_inited)
		return;

#ifdef G_OS_WIN32
	gchar *dir = g_win32_get_package_installation_directory_of_module (NULL);
	gnumeric_lib_dir = g_build_filename (dir, "lib",
					     "gnumeric", GNM_VERSION_FULL,
					     NULL);
	gnumeric_data_dir = g_build_filename (dir, "share",
					      "gnumeric", GNM_VERSION_FULL,
					      NULL);
	gnumeric_locale_dir = g_build_filename (dir, "share", "locale", NULL);
	gnumeric_extern_plugin_dir = g_build_filename
		(dir, "lib", "gnumeric", GNM_API_VERSION, "plugins",
		 NULL);
	g_free (dir);
#else
	top_builddir = running_in_tree ();
	if (top_builddir) {
		gnumeric_lib_dir =
			go_filename_simplify (top_builddir, GO_DOTDOT_SYNTACTIC,
					      FALSE);
		if (gnm_debug_flag ("in-tree"))
			g_printerr ("Running in-tree [%s]\n", top_builddir);
		g_free (top_builddir);
	}

	if (!gnumeric_lib_dir)
		gnumeric_lib_dir = g_strdup (GNUMERIC_LIBDIR);
	gnumeric_data_dir = g_strdup (GNUMERIC_DATADIR);
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

	gutils_inited = TRUE;
}

void
gutils_shutdown (void)
{
	GSList *l;

	g_free (gnumeric_lib_dir);
	gnumeric_lib_dir = NULL;
	g_free (gnumeric_data_dir);
	gnumeric_data_dir = NULL;
	g_free (gnumeric_locale_dir);
	gnumeric_locale_dir = NULL;
	g_free (gnumeric_usr_dir);
	gnumeric_usr_dir = NULL;
	g_free (gnumeric_usr_dir_unversioned);
	gnumeric_usr_dir_unversioned = NULL;
	g_free (gnumeric_extern_plugin_dir);
	gnumeric_extern_plugin_dir = NULL;

	for (l = gutils_xml_in_docs; l; l = l->next) {
		GsfXMLInDoc **pdoc = l->data;
		gsf_xml_in_doc_free (*pdoc);
		*pdoc = NULL;
	}
	g_slist_free (gutils_xml_in_docs);
	gutils_xml_in_docs = NULL;
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

	if (all_ascii (s)) {
		res = gnm_strto (s, end);
		goto handle_denormal;
	}

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

handle_denormal:
	save_errno = errno;
	if (res != 0 && gnm_abs (res) <= GNM_MIN)
		errno = 0;
	else
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
	go_dtoa (buf, "!" GNM_FORMAT_g, d);
}

/* ------------------------------------------------------------------------- */

void
gnm_insert_meta_date (GODoc *doc, char const *name)
{
	GValue *value = g_new0 (GValue, 1);
	GsfTimestamp *ts = gsf_timestamp_new ();

	gsf_timestamp_set_time (ts, g_get_real_time () / 1000000);
	g_value_init (value, GSF_TIMESTAMP_TYPE);
	gsf_timestamp_to_value (ts, value);
	gsf_timestamp_free (ts);

	gsf_doc_meta_data_insert (go_doc_get_meta_data (doc),
				  g_strdup (name),
				  value);
}

/* ------------------------------------------------------------------------- */

/**
 * gnm_object_get_bool:
 * @o: #GObject
 * @name: property name
 *
 * Returns: the value of @o's boolean property @name.
 */
gboolean
gnm_object_get_bool (gpointer o, const char *name)
{
	gboolean b;
	g_object_get (o, name, &b, NULL);
	return b;
}

/**
 * gnm_object_has_readable_prop:
 * @obj: #GObject
 * @property: property name
 * @typ: property's type or %G_TYPE_NONE.  (Exact type, not is-a.)
 * @pres: (out) (optional): location to store property value.
 *
 * Returns: %TRUE if @obj has a readable property named @property
 * of type @typ.
 */
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

/* ------------------------------------------------------------------------- */

gint
gnm_float_equal (gnm_float const *a, const gnm_float *b)
{
	return (*a == *b);
}

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
 * @callback: (scope call): #GHFunc
 * @order: (scope call): Ordering function
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

/* ------------------------------------------------------------------------- */

void
gnm_xml_in_doc_dispose_on_exit (GsfXMLInDoc **pdoc)
{
	gutils_xml_in_docs = g_slist_prepend (gutils_xml_in_docs, pdoc);
}

/**
 * gnm_xml_out_end_element_check:
 * @xout: #GsfXMLOut sink
 * @id: expected tag being closed
 *
 * Closes an xml tag, expected it to be @id.  If it is not, tags will
 * continue to be closed until the expected one is found in the hope
 * that getting back to sync will make the result less corrupted.
 */
void
gnm_xml_out_end_element_check (GsfXMLOut *xout, char const *id)
{
	while (TRUE) {
		const char *cid = gsf_xml_out_end_element (xout);
		if (!cid)
			return;
		if (g_str_equal (cid, id))
			return;
		g_critical ("Unbalanced xml tags while writing, please report");
	}
}


/* ------------------------------------------------------------------------- */

/**
 * gnm_file_saver_get_sheet:
 * @fs: #GOFileSaver
 * @wbv: #WorkbookView
 *
 * For a single-sheet saver, this function determines what sheet to save.
 *
 * Returns: (transfer none): the sheet to export
 */
Sheet *
gnm_file_saver_get_sheet (GOFileSaver const *fs, WorkbookView const *wbv)
{
	Workbook *wb;
	GPtrArray *sel;

	g_return_val_if_fail (GO_IS_FILE_SAVER (fs), NULL);
	g_return_val_if_fail (go_file_saver_get_save_scope (fs) ==
			      GO_FILE_SAVE_SHEET, NULL);
	g_return_val_if_fail (GNM_IS_WORKBOOK_VIEW (wbv), NULL);

	wb = wb_view_get_workbook (wbv);

	sel = g_object_get_data (G_OBJECT (wb), SHEET_SELECTION_KEY);
	if (sel) {
		if (sel->len == 1)
			return g_ptr_array_index (sel, 0);
		g_critical ("Someone messed up sheet selection");
	}

	return wb_view_cur_sheet (wbv);
}

/**
 * gnm_file_saver_get_sheets:
 * @fs: #GOFileSaver
 * @wbv: #WorkbookView
 * @default_all: If %TRUE, all sheets will be selected by default; if %FALSE,
 * this function will return %NULL if no sheets were explicitly selected.
 *
 * This function determines what sheets to save.
 *
 * Returns: (transfer container) (element-type Sheet): the sheets to export
 *
 * Note: the return value should be unreffed, not freed.
 */
GPtrArray *
gnm_file_saver_get_sheets (GOFileSaver const *fs,
			   WorkbookView const *wbv,
			   gboolean default_all)
{
	Workbook *wb;
	GPtrArray *sel, *sheets;
	GOFileSaveScope save_scope;

	g_return_val_if_fail (GO_IS_FILE_SAVER (fs), NULL);
	g_return_val_if_fail (GNM_IS_WORKBOOK_VIEW (wbv), NULL);

	save_scope = go_file_saver_get_save_scope (fs);
	wb = wb_view_get_workbook (wbv);
	sel = g_object_get_data (G_OBJECT (wb), SHEET_SELECTION_KEY);
	sheets = g_object_get_data (G_OBJECT (wb), SSCONVERT_SHEET_SET_KEY);
	if (sel)
		g_ptr_array_ref (sel);
	else if (sheets)
		sel = g_ptr_array_ref (sheets);
	else if (save_scope != GO_FILE_SAVE_WORKBOOK) {
		sel = g_ptr_array_new ();
		g_ptr_array_add (sel, wb_view_cur_sheet (wbv));
	} else if (default_all) {
		int i;
		sel = g_ptr_array_new ();
		for (i = 0; i < workbook_sheet_count (wb); i++) {
			Sheet *sheet = workbook_sheet_by_index (wb, i);
			g_ptr_array_add (sel, sheet);
		}
	}

	return sel;
}

gboolean
gnm_file_saver_common_export_option (GOFileSaver const *fs,
				     Workbook const *wb,
				     const char *key, const char *value,
				     GError **err)
{
	if (err)
		*err = NULL;

	g_return_val_if_fail (GO_IS_FILE_SAVER (fs), FALSE);
	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	if (strcmp (key, "sheet") == 0 ||
	    strcmp (key, "active-sheet") == 0) {
		GPtrArray *sheets;
		Sheet *sheet = NULL;

		if (key[0] == 'a') {
			// Not ideal -- we lack a view here
			WORKBOOK_FOREACH_VIEW (wb, wbv, {
					sheet = wb_view_cur_sheet (wbv);
				});
		} else {
			sheet = workbook_sheet_by_name (wb, value);
		}

		if (!sheet) {
			if (err)
				*err = g_error_new (go_error_invalid (), 0,
						    _("Unknown sheet \"%s\""),
						    value);
			return TRUE;
		}

		sheets = g_object_get_data (G_OBJECT (wb), SSCONVERT_SHEET_SET_KEY);
		if (!sheets) {
			sheets = g_ptr_array_new ();
			g_object_set_data_full (G_OBJECT (wb),
						SSCONVERT_SHEET_SET_KEY,
						sheets,
						(GDestroyNotify)g_ptr_array_unref);
		}
		g_ptr_array_add (sheets, sheet);

		return FALSE;
	}

	if (err)
		*err = g_error_new (go_error_invalid (), 0,
				    _("Invalid export option \"%s\" for format %s"),
				    key,
				    go_file_saver_get_id (fs));

	return TRUE;
}


static int
gnm_cpp_expr (const char *expr, G_GNUC_UNUSED GHashTable *vars)
{
	int vmajor, vminor, vmicro;

	while (g_ascii_isspace (*expr))
		expr++;

	if (sscanf (expr, "GTK_CHECK_VERSION (%d,%d,%d) ", &vmajor, &vminor, &vmicro) == 3) {
		return gtk_check_version (vmajor, vminor, vmicro) ? 0 : 1;
	}

	g_warning ("Unhandled cpp expression %s", expr);
	return 0;
}


char *
gnm_cpp (const char *src, GHashTable *vars)
{
	GString *res = g_string_new (NULL);
	GString *ifdefs = g_string_new ("1");

	while (*src) {
		const char *end;

		end = strchr (src, '\n');
		if (end)
			end++;
		else
			end = src + strlen (src);

		if (*src == '#') {
			if (strncmp (src, "#ifdef ", 7) == 0 || strncmp (src, "#ifndef ", 8) == 0) {
				int is_not = (src[3] == 'n');
				const char *var = src + 7 + is_not;
				char *w;
				gboolean res;

				while (g_ascii_isspace (*var))
					var++;
				src = var;
				while (g_ascii_isalnum (*src))
					src++;
				w = g_strndup (var, src - var);
				res = is_not ^ !!g_hash_table_lookup (vars, w);
				g_string_append_c (ifdefs, ifdefs->str[ifdefs->len - 1] && res);
				g_free (w);
			} else if (strncmp (src, "#if ", 4) == 0) {
				gboolean res = gnm_cpp_expr (src + 4, vars) > 0;
				g_string_append_c (ifdefs, ifdefs->str[ifdefs->len - 1] && res);
			} else if (strncmp (src, "#else", 5) == 0) {
				ifdefs->str[ifdefs->len - 1] =
					!ifdefs->str[ifdefs->len - 1] &&
					ifdefs->str[ifdefs->len - 2];
			} else if (strncmp (src, "#endif", 6) == 0 && ifdefs->len > 1) {
				g_string_set_size (ifdefs, ifdefs->len - 1);
			} else {
				g_warning ("cpp failure");
			}
		} else {
			if (ifdefs->str[ifdefs->len - 1])
				g_string_append_len (res, src, end - src);
		}
		src = end;
	}

	g_string_free (ifdefs, TRUE);
	return g_string_free (res, FALSE);
}

// Should we write the shortest representation of floating point values
// in text-based formats (gnumeric/xlsx/ods)?
//
// This is currently off because
// (1) There are test suite considerations.  Files currently written with
//     fixed precision will not round-trip.  Presumably that can be sorted
//     out.
// (2) go_dtoa is not perfect.  We don't want to deal with (1) twice.
//
// Note: this does not change the semantics of the files.  The files mean
// precisely the same thing, ie., the different strings are representations
// of the same IEEE-754 number which is (generally) not a nice decimal
// number with few decimals.
//
// ANYONE WHO FAILS TO UNDERSTAND THE PREVIOUS PARAGRAPH WILL BE SUMMARILY
// IGNORED.
gboolean
gnm_shortest_rep_in_files (void)
{
	static int q = -1;
	if (q == -1)
		q = g_getenv ("GNM_SHORTREP_FILES") != NULL;
	return q;
}

// Returns: -1 if no ssconvert selection has been made; 0 if the sheet is not
// be exported, and 1 with a valid range if a selection has been made for the
// sheet.
int
gnm_export_range_for_sheet (Sheet const *sheet, GnmRange *dest)
{
	GnmRangeRef const *rr;

	g_return_val_if_fail (IS_SHEET (sheet), -1);
	g_return_val_if_fail (dest != NULL, -1);

	rr = g_object_get_data (G_OBJECT (sheet->workbook), "ssconvert-range");
	if (rr) {
		GnmEvalPos ep;
		Sheet *start_sheet, *end_sheet;

		gnm_rangeref_normalize (rr,
					eval_pos_init_sheet (&ep, sheet),
					&start_sheet, &end_sheet,
					dest);
		if (sheet->index_in_wb >= start_sheet->index_in_wb &&
		    sheet->index_in_wb <= end_sheet->index_in_wb)
			return 1;
	}

	memset (dest, 0, sizeof (*dest));
	return rr ? 0 : -1;
}


#ifdef GNM_SUPPLIES_GNM_SSCANF
// Miniature scanf that understands _Decimal64 arguments
// Handles
// * "%g", "%gl", "%Lg", "%Wg"
// * "%d", "%u"
// * "%s", "%c", "%*s"
// * whitespace
// * literal characters including "%%"
// and very little else.
int
gnm_sscanf (const char *str, const char *fmt, ...)
{
	va_list args;
	int res = 0;
	char *tmp = g_new (char, strlen (str) + 1);

	va_start (args, fmt);
	while (*fmt) {
		char c = *fmt++;
		if (c == '%') {
			int flag_l = 0, flag_L = 0, flag_ast = 0;
#ifdef GNM_WITH_DECIMAL64
			int flag_W = 0;
#endif
			int nchars;

			if (*fmt == '%') {
				fmt++;
				goto regular;
			}

			while (TRUE) {
				if (*fmt == '*') { fmt++; flag_ast++; continue; }
				if (*fmt == 'l') { fmt++; flag_l++; continue; }
				if (*fmt == 'L') { fmt++; flag_L++; continue; }
#ifdef GNM_WITH_DECIMAL64
				if (*fmt == *GNM_SCANF_g) { fmt++; flag_W++; continue; }
#endif
				break;
			}

			if (*fmt != 'c') {
				while (g_ascii_isspace (*str))
					str++;
			}

			if (sscanf (str, (*fmt == 'c' ? "%c%n" : "%s%n"), tmp, &nchars) != 1)
				break;
			str += nchars;

			if (flag_ast && *fmt) {
				fmt++;
				continue;
			}

			switch (*fmt++) {
			case 'e': case 'E': case 'f': case 'F':
			case 'g': case 'G': case 'a': case 'A':
#ifdef GNM_WITH_DECIMAL64
				if (flag_W) {
					*va_arg(args, _Decimal64 *) = go_strtoDd (tmp, NULL);
					break;
				}
#endif
				if (flag_L)
					*va_arg(args, long double *) = go_strtold (tmp, NULL);
				else if (flag_l)
					*va_arg(args, double *) = go_strtod (tmp, NULL);
				else
					*va_arg(args, float *) = go_strtod (tmp, NULL);
				break;

			case 'd':
				if (flag_l) {
					*va_arg(args, long *) = strtol (tmp, NULL, 10);
				} else {
					*va_arg(args, int *) = strtol (tmp, NULL, 10);
				}
				break;

			case 'u':
				if (flag_l) {
					*va_arg(args, long *) = strtoul (tmp, NULL, 10);
				} else {
					*va_arg(args, int *) = strtoul (tmp, NULL, 10);
				}
				break;

			case 'c':
				*va_arg(args, char *) = tmp[0];
				break;

			default:
				g_printerr ("Unhandled format character '%c'\n", fmt[-1]);
				abort();
			}

			res++;
		} else if (g_ascii_isspace (c)) {
			while (g_ascii_isspace (*str))
				str++;
		} else {
		regular:
			if (*str == c) {
				str++;
			} else if (*str == 0) {
				res = EOF;
				break;
			} else {
				break;
			}
		}
	}

	va_end (args);
	g_free (tmp);

	return res;
}
#endif
