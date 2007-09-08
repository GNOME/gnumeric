/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <gconf/gconf-client.h>

struct _GOConfNode {
	gchar *path;
	gboolean root;
};

static GConfClient *gconf_client = NULL;

static void
go_conf_init (void)
{
	if (!gconf_client)
		gconf_client = gconf_client_get_default ();
}

static void
go_conf_shutdown (void)
{
	if (gconf_client) {
		g_object_unref (G_OBJECT (gconf_client));
		gconf_client = NULL;
	}
}

static gchar *
go_conf_get_real_key (GOConfNode const *key, gchar const *subkey)
{
	return key ? g_strconcat ((key)->path, "/", subkey, NULL) :
		     g_strdup (subkey);
}

GOConfNode *
go_conf_get_node (GOConfNode *parent, gchar const *key)
{
	GOConfNode *node;

	node = g_new (GOConfNode, 1);
	gconf_client = gconf_client;
	node->root = !parent;
	if (node->root) {
		node->path = g_strconcat ("/apps/", key, NULL);
		gconf_client_add_dir (gconf_client, node->path,
				      GCONF_CLIENT_PRELOAD_RECURSIVE,
				      NULL);
	}
	else
		node->path = go_conf_get_real_key (parent, key);

	return node;
}

void
go_conf_free_node (GOConfNode *node)
{
	if (node) {
		if (node->root)
			gconf_client_remove_dir (gconf_client, node->path, NULL);
		g_free (node->path);
		g_free (node);
	}
}

void
go_conf_sync (GOConfNode *node)
{
	gconf_client_suggest_sync (gconf_client, NULL);
}

void
go_conf_set_bool (GOConfNode *node, gchar const *key, gboolean val)
{
	gchar *real_key = go_conf_get_real_key (node, key);
	gconf_client_set_bool (gconf_client, real_key, val, NULL);
	g_free (real_key);
}

void
go_conf_set_int (GOConfNode *node, gchar const *key, gint val)
{
	gchar *real_key = go_conf_get_real_key (node, key);
	gconf_client_set_int (gconf_client, real_key, val, NULL);
	g_free (real_key);
}

void
go_conf_set_double (GOConfNode *node, gchar const *key, gnm_float val)
{
	gchar *real_key = go_conf_get_real_key (node, key);
	gconf_client_set_float (gconf_client, real_key, val, NULL);
	g_free (real_key);
}

void
go_conf_set_string (GOConfNode *node, gchar const *key, gchar const *str)
{
	gchar *real_key = go_conf_get_real_key (node, key);
	gconf_client_set_string (gconf_client, real_key, str, NULL);
	g_free (real_key);
}

void
go_conf_set_str_list (GOConfNode *node, gchar const *key, GSList *list)
{
	gchar *real_key = go_conf_get_real_key (node, key);
	gconf_client_set_list (gconf_client, real_key,
		GCONF_VALUE_STRING, list, NULL);
	g_free (real_key);
}

static GConfValue *
go_conf_get (GOConfNode *node, gchar const *key, GConfValueType t)
{
	GError *err = NULL;
	GConfValue *val;
	gchar *real_key;

	real_key = go_conf_get_real_key (node, key);
	val = gconf_client_get (gconf_client, real_key, &err);

	if (err != NULL) {
		d (g_warning ("Unable to load key '%s' : because %s",
			      real_key, err->message));
		g_free (real_key);
		g_error_free (err);
		return NULL;
	}
	if (val == NULL) {
		d (g_warning ("Unable to load key '%s'", real_key));
		g_free (real_key);
		return NULL;
	}

	if (val->type != t) {
#if 1 /* gconf_value_type_to_string is internal */
		g_warning ("Expected `%d' got `%d' for key %s",
			   t, val->type, real_key);
#else
		g_warning ("Expected `%s' got `%s' for key %s",
			   gconf_value_type_to_string (t),
			   gconf_value_type_to_string (val->type),
			   real_key);
#endif
		g_free (real_key);
		gconf_value_free (val);
		return NULL;
	}
	g_free (real_key);

	return val;
}

gboolean
go_conf_load_bool (GOConfNode *node, gchar const *key, gboolean default_val)
{
	gboolean res;
	GConfValue *val = go_conf_get (node, key, GCONF_VALUE_BOOL);

	if (val != NULL) {
		res = gconf_value_get_bool (val);
		gconf_value_free (val);
	} else {
		d (g_warning ("Using default value '%s'", default_val ? "true" : "false"));
		return default_val;
	}
	return res;
}

gint
go_conf_load_int (GOConfNode *node, gchar const *key, gint minima, gint maxima, gint default_val)
{
	gint res = -1;
	GConfValue *val = go_conf_get (node, key, GCONF_VALUE_INT);

	if (val != NULL) {
		res = gconf_value_get_int (val);
		gconf_value_free (val);
		if (res < minima || maxima < res) {
			g_warning ("Invalid value '%d' for %s.  If should be >= %d and <= %d",
				   res, key, minima, maxima);
			val = NULL;
		}
	}
	if (val == NULL) {
		d (g_warning ("Using default value '%d'", default_val));
		return default_val;
	}
	return res;
}

gdouble
go_conf_load_double (GOConfNode *node, gchar const *key,
		     gdouble minima, gdouble maxima, gdouble default_val)
{
	gdouble res = -1;
	GConfValue *val = go_conf_get (node, key, GCONF_VALUE_FLOAT);

	if (val != NULL) {
		res = gconf_value_get_float (val);
		gconf_value_free (val);
		if (res < minima || maxima < res) {
			g_warning ("Invalid value '%g' for %s.  If should be >= %g and <= %g",
				   res, key, minima, maxima);
			val = NULL;
		}
	}
	if (val == NULL) {
		d (g_warning ("Using default value '%g'", default_val));
		return default_val;
	}
	return res;
}

gchar *
go_conf_load_string (GOConfNode *node, gchar const *key)
{
	gchar *val;

	gchar *real_key = go_conf_get_real_key (node, key);
	val = gconf_client_get_string (gconf_client, real_key, NULL);
	g_free (real_key);

	return val;
}

GSList *
go_conf_load_str_list (GOConfNode *node, gchar const *key)
{
	GSList *list;

	gchar *real_key = go_conf_get_real_key (node, key);
	list = gconf_client_get_list (gconf_client, real_key,
				      GCONF_VALUE_STRING, NULL);
	g_free (real_key);

	return list;
}

static GConfSchema *
get_schema (GOConfNode *node, gchar const *key)
{
	gchar *schema_key = g_strconcat (
		"/schemas", node->path, "/", key, NULL);
	GConfSchema *schema = gconf_client_get_schema (
		gconf_client, schema_key, NULL);
	g_free (schema_key);
	return schema;
}

gchar *
go_conf_get_short_desc (GOConfNode *node, gchar const *key)
{
	GConfSchema *schema = get_schema (node, key);

	if (schema != NULL) {
		gchar *desc = g_strdup (gconf_schema_get_short_desc (schema));
		gconf_schema_free (schema);
		return desc;
	}
	return NULL;
}

gchar *
go_conf_get_long_desc  (GOConfNode *node, gchar const *key)
{
	GConfSchema *schema = get_schema (node, key);

	if (schema != NULL) {
		gchar *desc =  g_strdup (gconf_schema_get_long_desc (schema));
		gconf_schema_free (schema);
		return desc;
	}
	return NULL;
}

GType
go_conf_get_type (GOConfNode *node, gchar const *key)
{
	GConfSchema *schema = get_schema (node, key);
	GType t;

	switch (gconf_schema_get_type (schema)) {
	case GCONF_VALUE_STRING: t = G_TYPE_STRING; break;
	case GCONF_VALUE_FLOAT: t = G_TYPE_FLOAT; break;
	case GCONF_VALUE_INT: t = G_TYPE_INT; break;
	case GCONF_VALUE_BOOL: t = G_TYPE_BOOLEAN; break;
	default :
		t = G_TYPE_NONE;
	}

	if (schema != NULL)
		gconf_schema_free (schema);
	return t;
}

gchar *
go_conf_get_value_as_str (GOConfNode *node, gchar const *key)
{
	gchar *value_string;

	switch (go_conf_get_type (node, key)) {
	case G_TYPE_STRING:
		value_string = go_conf_get_string (node, key);
		break;
	case G_TYPE_INT:
		value_string = g_strdup_printf ("%i", go_conf_get_int (node, key));
		break;
	case G_TYPE_FLOAT:
		value_string = g_strdup_printf ("%f", go_conf_get_double (node, key));
		break;
	case G_TYPE_BOOLEAN:
		value_string = g_strdup (go_locale_boolean_name (go_conf_get_bool (node, key)));
		break;
	default:
		value_string = g_strdup ("ERROR FIXME");
	}

	return value_string;
}

gboolean
go_conf_get_bool (GOConfNode *node, gchar const *key)
{
	gboolean val;
	gchar *real_key;

	real_key = go_conf_get_real_key (node, key);
	val = gconf_client_get_bool (gconf_client, real_key, NULL);
	g_free (real_key);

	return val;
}

gint
go_conf_get_int	(GOConfNode *node, gchar const *key)
{
	gint val;
	gchar *real_key;

	real_key = go_conf_get_real_key (node, key);
	val = gconf_client_get_int (gconf_client, real_key, NULL);
	g_free (real_key);

	return val;
}

gdouble
go_conf_get_double (GOConfNode *node, gchar const *key)
{
	gdouble val;
	gchar *real_key;

	real_key = go_conf_get_real_key (node, key);
	val = gconf_client_get_float (gconf_client, real_key, NULL);
	g_free (real_key);

	return val;
}

gchar *
go_conf_get_string (GOConfNode *node, gchar const *key)
{
	gchar *real_key = go_conf_get_real_key (node, key);
	gchar *res = gconf_client_get_string (gconf_client, real_key, NULL);
	g_free (real_key);
	return res;
}


GSList *
go_conf_get_str_list (GOConfNode *node, gchar const *key)
{
	return go_conf_load_str_list (node, key);
}

gboolean
go_conf_set_value_from_str (GOConfNode *node, gchar const *key, gchar const *val_str)
{
	switch (go_conf_get_type (node, key)) {
	case G_TYPE_STRING:
		go_conf_set_string (node, key, val_str);
		break;
	case G_TYPE_FLOAT: {
		GODateConventions const *conv = NULL;  /* workbook_date_conv (state->wb); */
		GnmValue *value = format_match_number (val_str, NULL, conv);
		if (value != NULL) {
			gnm_float the_float = value_get_as_float (value);
			go_conf_set_double (node, key, the_float);
		}
		if (value)
			value_release (value);
		break;
	}
	case G_TYPE_INT: {
		GODateConventions const *conv = NULL;  /* workbook_date_conv (state->wb); */
		GnmValue *value = format_match_number (val_str, NULL, conv);
		if (value != NULL) {
			gint the_int = value_get_as_int (value);
			go_conf_set_int (node, key, the_int);
		}
		if (value)
			value_release (value);
		break;
	}
	case G_TYPE_BOOLEAN: {
		GODateConventions const *conv = NULL;  /* workbook_date_conv (state->wb); */
		GnmValue *value = format_match_number (val_str, NULL, conv);
		gboolean err, the_bool;
		if (value != NULL) {
			err = FALSE;
			the_bool =  value_get_as_bool (value, &err);
			go_conf_set_bool (node, key, the_bool);
		}
		if (value)
			value_release (value);
		break;
	}
	default:
		g_warning ("Unsupported gconf type in preference dialog");
	}

	return TRUE;
}

void
go_conf_remove_monitor (guint monitor_id)
{
	gconf_client_notify_remove (gconf_client,
		GPOINTER_TO_INT (monitor_id));
}

typedef struct {
	GOConfMonitorFunc monitor;
	gpointer data;
} GOConfClosure;

static void
cb_key_changed (GConfClient *client, guint cnxn_id,
		GConfEntry *entry, GOConfClosure *close)
{
	close->monitor (NULL, gconf_entry_get_key (entry), close->data);
}

guint
go_conf_add_monitor (GOConfNode *node, gchar const *key,
		     GOConfMonitorFunc monitor, gpointer data)
{
	guint ret;
	GOConfClosure *close = g_new0 (GOConfClosure, 1);
	gchar *real_key;

	close->monitor = monitor;
	close->data = data;
	real_key = go_conf_get_real_key (node, key);
	ret = gconf_client_notify_add (gconf_client, real_key,
		(GConfClientNotifyFunc) cb_key_changed, close, g_free, NULL);
	g_free (real_key);

	return ret;
}
