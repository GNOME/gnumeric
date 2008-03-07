/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <glib/gstdio.h>
#include <errno.h>

#define BOOL_GROUP    "Booleans"
#define INT_GROUP     "Ints"
#define DOUBLE_GROUP  "Doubles"
#define STRING_GROUP  "Strings"
#define STRLIST_GROUP "StringLists"

struct _GOConfNode {
	gchar *path;
};

static GKeyFile *key_file = NULL;

static gchar *get_rc_filename (void)
{
	const gchar *home = g_get_home_dir ();
	gchar *fname = NULL;

	if (home != NULL)
		fname = g_build_filename (home, ".gnumericrc", NULL);

	return fname;
}

static void dump_key_data_to_file (void)
{
	FILE *fp = NULL;
	gchar *rcfile = NULL;
	gchar *key_data;

	rcfile = get_rc_filename ();
	if (rcfile == NULL) {
		g_warning ("Couldn't determine the name of the configuration file");
		return;
	}

	fp = g_fopen (rcfile, "w");
	if (fp == NULL) {
		g_warning ("Couldn't write configuration info to %s", rcfile);
		g_free (rcfile);
		return;
	}

	key_data = g_key_file_to_data (key_file, NULL, NULL);

	if (key_data != NULL) {
		fputs (key_data, fp);
		g_free (key_data);
	}

	fclose (fp);
	g_free (rcfile);
}

static gchar *
go_conf_get_real_key (GOConfNode const *key, gchar const *subkey)
{
	return key ? g_strconcat ((key)->path, "/", subkey, NULL) :
		     g_strdup (subkey);
}

static void
go_conf_init (void)
{
	gchar *rcfile = get_rc_filename ();

	if (rcfile != NULL) {
		key_file = g_key_file_new ();
		g_key_file_load_from_file (key_file, rcfile, G_KEY_FILE_NONE, NULL);
		g_free (rcfile);
	}
}

static void
go_conf_shutdown (void)
{
	dump_key_data_to_file ();
	g_key_file_free (key_file);
	key_file = NULL;
}

GOConfNode *
go_conf_get_node (GOConfNode *parent, gchar const *key)
{
	GOConfNode *node;

	node = g_new (GOConfNode, 1);
	node->path = go_conf_get_real_key (parent, key);

	return node;
}

void
go_conf_free_node (GOConfNode *node)
{
	if (node != NULL) {
		g_free (node->path);
		g_free (node);
	}
}

void
go_conf_set_bool (GOConfNode *node, gchar const *key, gboolean val)
{
	gchar *real_key = go_conf_get_real_key (node, key);
	g_key_file_set_boolean (key_file, BOOL_GROUP, real_key, val);
	g_free (real_key);
}

void
go_conf_set_int (GOConfNode *node, gchar const *key, gint val)
{
	gchar *real_key = go_conf_get_real_key (node, key);
	g_key_file_set_integer (key_file, INT_GROUP, real_key, val);
	g_free (real_key);
}

void
go_conf_set_double (GOConfNode *node, gchar const *key, gnm_float val)
{
	gchar *real_key = go_conf_get_real_key (node, key);
	gchar str[G_ASCII_DTOSTR_BUF_SIZE];

	g_ascii_dtostr (str, sizeof (str), val);
	g_key_file_set_value (key_file, DOUBLE_GROUP, real_key, str);
	g_free (real_key);
}

void
go_conf_set_string (GOConfNode *node, gchar const *key, char const *str)
{
	gchar *real_key = go_conf_get_real_key (node, key);
	g_key_file_set_string (key_file, STRING_GROUP, real_key, str);
	g_free (real_key);
}

void
go_conf_set_str_list (GOConfNode *node, gchar const *key, GSList *list)
{
	gchar *real_key;
	gchar **strs = NULL;
	int i, ns;

	if (list == NULL)
		return;

	real_key = go_conf_get_real_key (node, key);
	ns = g_slist_length (list);
	strs = g_new (gchar *, ns);

	for (i = 0; i < ns; i++) {
		const gchar *lstr = list->data;
		strs[i] = g_strdup (lstr);
		list = list->next;
	}

	g_key_file_set_string_list (key_file, STRLIST_GROUP, real_key,
				    (gchar const **const) strs, ns);
	g_free (real_key);

	for (i = 0; i < ns; i++)
		g_free (strs[i]);
	g_free (strs);
}

gboolean
go_conf_get_bool (GOConfNode *node, gchar const *key)
{
	gboolean val;
	gchar *real_key;

	real_key = go_conf_get_real_key (node, key);
	val = g_key_file_get_boolean (key_file, BOOL_GROUP, real_key, NULL);
	g_free (real_key);

	return val;
}

gint
go_conf_get_int	(GOConfNode *node, gchar const *key)
{
	gboolean val;
	gchar *real_key;

	real_key = go_conf_get_real_key (node, key);
	val = g_key_file_get_integer (key_file, INT_GROUP, real_key, NULL);
	g_free (real_key);

	return val;
}

gdouble
go_conf_get_double (GOConfNode *node, gchar const *key)
{
	gchar *ptr;
	gchar *real_key;
	gdouble val;

	real_key = go_conf_get_real_key (node, key);
	ptr = g_key_file_get_value (key_file, DOUBLE_GROUP, real_key, NULL);
	g_free (real_key);
	if (ptr) {
		val = g_ascii_strtod (ptr, NULL);
		g_free (ptr);
		if (errno != ERANGE)
			return val;
	}

	return 0.0;
}

/**
 * go_conf_get_string :
 * @node : #GOConfNode
 * @key : non NULL string.
 *
 * Returns the string value of @node's @key child as a string which the called needs to free
 **/
gchar *
go_conf_get_string (GOConfNode *node, gchar const *key)
{
	gchar *real_key;
	gchar *val = NULL;

	real_key = go_conf_get_real_key (node, key);
	val = g_key_file_get_string (key_file, STRING_GROUP, real_key, NULL);
	g_free (real_key);

	return val;
}

GSList *
go_conf_get_str_list (GOConfNode *node, gchar const *key)
{
	gchar *real_key;
	GSList *list = NULL;
	gchar **str_list;
	gsize i, nstrs;

	real_key = go_conf_get_real_key (node, key);
	str_list = g_key_file_get_string_list (key_file, STRLIST_GROUP, real_key, &nstrs, NULL);
	g_free (real_key);

	if (str_list != NULL) {
		for (i = 0; i < nstrs; i++) {
			if (str_list[i][0]) {
				list = g_slist_append (list, g_strcompress (str_list[i]));
			}
		}
		g_strfreev (str_list);
	}

	return list;
}

gboolean
go_conf_load_bool (GOConfNode *node, gchar const *key, gboolean default_val)
{
	gchar *real_key;
	gboolean val;
	GError *err = NULL;

	real_key = go_conf_get_real_key (node, key);
	val = g_key_file_get_boolean (key_file, BOOL_GROUP, real_key, &err);
	if (err) {
		val = default_val;
		g_error_free (err);
#if 0
		d (g_warning ("%s: using default value '%s'", real_key, default_val ? "true" : "false"));
#endif
	}

	g_free (real_key);
	return val;
}

int
go_conf_load_int (GOConfNode *node, gchar const *key,
		  gint minima, gint maxima,
		  gint default_val)
{
	gchar *real_key;
	int val;
	GError *err = NULL;

	real_key = go_conf_get_real_key (node, key);
	val = g_key_file_get_integer (key_file, INT_GROUP, real_key, &err);

	if (err) {
		val = default_val;
		g_error_free(err);
#if 0
		d (g_warning ("%s: using default value %d", real_key, default_val));
#endif
	} else if (val < minima || val > maxima) {
		val = default_val;
	}

	g_free (real_key);
	return val;
}

double
go_conf_load_double (GOConfNode *node, gchar const *key,
		     gdouble minima, gdouble maxima,
		     gdouble default_val)
{
	gchar *real_key;
	gchar *ptr;
	double val;
	GError *err = NULL;

	real_key = go_conf_get_real_key (node, key);
	ptr = g_key_file_get_value (key_file, DOUBLE_GROUP, real_key, &err);

	if (err) {
		val = default_val;
		g_error_free (err);
	} else {
		val = g_ascii_strtod (ptr, NULL);
		if (val < minima || val > maxima) {
			val = default_val;
		}
	}

	g_free(ptr);
	g_free (real_key);
	return val;
}

char *
go_conf_load_string (GOConfNode *node, gchar const *key)
{
	gchar *real_key;
	char *val = NULL;
	GError *err = NULL;

	real_key = go_conf_get_real_key (node, key);
	val = g_key_file_get_string (key_file, STRING_GROUP, real_key, &err);

	if (err) {
#if 0
		g_warning (err->message);
#endif
		g_error_free (err);
	}

	g_free (real_key);
	return val;
}

GSList *
go_conf_load_str_list (GOConfNode *node, gchar const *key)
{
	return go_conf_get_str_list (node, key);
}

char *
go_conf_get_short_desc (GOConfNode *node, gchar const *key)
{
	return NULL;
}

gchar *
go_conf_get_long_desc  (GOConfNode *node, gchar const *key)
{
	return NULL;
}

GType
go_conf_get_type (GOConfNode *node, gchar const *key)
{
	gchar **groups;
	gchar *real_key;
	GType type = G_TYPE_NONE;
	gsize i, ng;

	real_key = go_conf_get_real_key (node, key);
	groups = g_key_file_get_groups (key_file, &ng);

	if (groups != NULL) {
		for (i = 0; i < ng; i++) {
			if (g_key_file_has_key (key_file, groups[i], real_key, NULL)) {
				if (!g_ascii_strcasecmp (groups[i], BOOL_GROUP)) {
					type = G_TYPE_BOOLEAN;
				} else if (!g_ascii_strcasecmp (groups[i], INT_GROUP)) {
					type = G_TYPE_INT;
				} else if (!g_ascii_strcasecmp (groups[i], DOUBLE_GROUP)) {
					type = G_TYPE_DOUBLE;
				} else if (!g_ascii_strcasecmp (groups[i], STRING_GROUP)) {
					type = G_TYPE_STRING;
				} else if (!g_ascii_strcasecmp (groups[i], STRLIST_GROUP)) {
					type = G_TYPE_STRING;
				}
				break;
			}
		}
		g_strfreev (groups);
	}

	g_free (real_key);

	return type;
}

gchar *
go_conf_get_value_as_str (GOConfNode *node, gchar const *key)
{
	gchar *val = NULL;
	gchar *real_key = go_conf_get_real_key (node, key);
	val = g_key_file_get_string (key_file, STRING_GROUP, real_key, NULL);
	g_free (real_key);
	return val;
}

gboolean
go_conf_set_value_from_str (GOConfNode *node, gchar const *key, gchar const *val_str)
{
	gchar *real_key = go_conf_get_real_key (node, key);
	g_key_file_set_value (key_file, STRING_GROUP, real_key, val_str);
	g_free (real_key);
	return TRUE;
}

void
go_conf_sync (GOConfNode *node)
{
}

void
go_conf_remove_monitor (guint monitor_id)
{
}

guint
go_conf_add_monitor (GOConfNode *node, gchar const *key,
		     GOConfMonitorFunc monitor, gpointer data)
{
	return 1;
}
