/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <windows.h>

#ifndef ERANGE
/* mingw has not defined ERANGE (yet), MSVC has it though */
# define ERANGE 34
#endif

struct _GOConfNode {
	HKEY hKey;
	gchar *path;
};

static void
go_conf_init (void)
{
}

static void
go_conf_shutdown (void)
{
}

static gboolean
go_conf_win32_get_node (GOConfNode *node, HKEY *phKey, gchar const *key, gboolean *is_new)
{
	gchar *path, *c;
	LONG ret;
	DWORD disposition;

	path = g_strconcat (node ? "" : "Software\\", key, NULL);
	for (c = path; *c; ++c) {
		if (*c == '/')
			*c = '\\';
	}
	ret = RegCreateKeyEx (node ? node->hKey : HKEY_CURRENT_USER, path,
			      0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
			      NULL, phKey, &disposition);
	g_free (path);

	if (is_new)
		*is_new = disposition == REG_CREATED_NEW_KEY;

	return ret == ERROR_SUCCESS;
}

static gboolean
go_conf_win32_set (GOConfNode *node, gchar const *key,
		   gint type, guchar *data, gint size)
{
	gchar *last_sep, *path = NULL;
	HKEY hKey;
	gboolean ok;

	if ((last_sep = strrchr (key, '/')) != NULL) {
		path = g_strndup (key, last_sep - key);
		ok = go_conf_win32_get_node (node, &hKey, path, NULL);
		g_free (path);
		if (!ok)
			return FALSE;
		key = last_sep + 1;
	}
	else
		hKey = node->hKey;
	RegSetValueEx (hKey, key, 0, type, data, size);
	if (path)
		RegCloseKey (hKey);

	return TRUE;
}

static gboolean
go_conf_win32_get (GOConfNode *node, gchar const *key,
		   gulong *type, guchar **data, gulong *size,
		   gboolean realloc, gint *ret_code)
{
	gchar *last_sep, *path = NULL;
	HKEY hKey;
	LONG ret;
	gboolean ok;

	if ((last_sep = strrchr (key, '/')) != NULL) {
		path = g_strndup (key, last_sep - key);
		ok = go_conf_win32_get_node (node, &hKey, path, NULL);
		g_free (path);
		if (!ok)
			return FALSE;
		key = last_sep + 1;
	}
	else
		hKey = node->hKey;
	if (!*data && realloc) {
		RegQueryValueEx (hKey, key, NULL, type, NULL, size);
		*data = g_new (guchar, *size);
	}
	while ((ret = RegQueryValueEx (hKey, key, NULL,
				       type, *data, size)) == ERROR_MORE_DATA &&
	       realloc)
		*data = g_realloc (*data, *size);
	if (path)
		RegCloseKey (hKey);
	if (ret_code)
		*ret_code = ret;

	return ret == ERROR_SUCCESS;
}

static void
go_conf_win32_clone (HKEY hSrcKey, gchar *key, HKEY hDstKey, gchar *buf1, gchar *buf2, gchar *buf3)
{
#define WIN32_MAX_REG_KEYNAME_LEN 256
#define WIN32_MAX_REG_VALUENAME_LEN 32767
#define WIN32_INIT_VALUE_DATA_LEN 2048
	gint i;
	gchar *subkey, *value_name, *data;
	DWORD name_size, type, data_size;
	HKEY hSrcSK, hDstSK;
	FILETIME ft;
	LONG ret;

	if (RegOpenKeyEx (hSrcKey, key, 0, KEY_READ, &hSrcSK) != ERROR_SUCCESS)
		return;

	if (!buf1) {
		subkey = g_malloc (WIN32_MAX_REG_KEYNAME_LEN);
		value_name = g_malloc (WIN32_MAX_REG_VALUENAME_LEN);
		data = g_malloc (WIN32_INIT_VALUE_DATA_LEN);
	}
	else {
		subkey = buf1;
		value_name = buf2;
		data = buf3;
	}

	ret = ERROR_SUCCESS;
	for (i = 0; ret == ERROR_SUCCESS; ++i) {
		name_size = WIN32_MAX_REG_KEYNAME_LEN;
		ret = RegEnumKeyEx (hSrcSK, i, subkey, &name_size, NULL, NULL, NULL, &ft);
		if (ret != ERROR_SUCCESS)
			continue;

		if (RegCreateKeyEx (hDstKey, subkey, 0, NULL, 0, KEY_WRITE,
				    NULL, &hDstSK, NULL) == ERROR_SUCCESS) {
			go_conf_win32_clone (hSrcSK, subkey, hDstSK, subkey, value_name, data);
			RegCloseKey (hDstSK);
		}
	}

	ret = ERROR_SUCCESS;
	for (i = 0; ret == ERROR_SUCCESS; ++i) {
		name_size = WIN32_MAX_REG_KEYNAME_LEN;
		data_size = WIN32_MAX_REG_VALUENAME_LEN;
		while ((ret = RegEnumValue (hSrcSK, i, value_name, &name_size,
					    NULL, &type, data, &data_size)) ==
		       ERROR_MORE_DATA)
			data = g_realloc (data, data_size);
		if (ret != ERROR_SUCCESS)
			continue;

		RegSetValueEx (hDstKey, value_name, 0, type, data, data_size);
	}

	RegCloseKey (hSrcSK);
	if (!buf1) {
		g_free (subkey);
		g_free (value_name);
		g_free (data);
	}
}

GOConfNode *
go_conf_get_node (GOConfNode *parent, const gchar *key)
{
	HKEY hKey;
	GOConfNode *node = NULL;
	gboolean is_new;

	if (go_conf_win32_get_node (parent, &hKey, key, &is_new)) {
		if (!parent && is_new) {
			gchar *path;

			path = g_strconcat (".DEFAULT\\Software\\", key, NULL);
			go_conf_win32_clone (HKEY_USERS, path, hKey, NULL, NULL, NULL);
			g_free (path);
		}
		node = g_malloc (sizeof (GOConfNode));
		node->hKey = hKey;
		node->path = g_strdup (key);
	}

	return node;
}

void
go_conf_free_node (GOConfNode *node)
{
	if (node) {
		RegCloseKey (node->hKey);
		g_free (node->path);
		g_free (node);
	}
}

void
go_conf_set_bool (GOConfNode *node, gchar const *key, gboolean val)
{
	guchar bool = val ? 1 : 0;

	go_conf_win32_set (node, key, REG_BINARY, (guchar *) &bool,
			   sizeof (bool));
}

void
go_conf_set_int (GOConfNode *node, gchar const *key, gint val)
{
	go_conf_win32_set (node, key, REG_DWORD, (guchar *) &val,
			   sizeof (DWORD));
}

void
go_conf_set_double (GOConfNode *node, gchar const *key, gnm_float val)
{
	gchar str[G_ASCII_DTOSTR_BUF_SIZE];

	g_ascii_dtostr (str, sizeof (str), val);
	go_conf_win32_set (node, key, REG_SZ, (guchar *) str,
			   strlen (str) + 1);
}

void
go_conf_set_string (GOConfNode *node, gchar const *key, gchar const *str)
{
	go_conf_win32_set (node, key, REG_SZ, (guchar *) str,
			   strlen (str) + 1);
}

void
go_conf_set_str_list (GOConfNode *node, gchar const *key, GSList *list)
{
	GString *str_list;
	GSList *list_node = list;

	str_list = g_string_new ("");
	while (list_node) {
		g_string_append (str_list, g_strescape (list_node->data, NULL));
		g_string_append_c (str_list, '\n');
		list_node = list_node->next;
	}
	if (list)
		g_string_truncate (str_list, str_list->len - 1);
	go_conf_win32_set (node, key, REG_SZ, (guchar *) str_list->str,
			   str_list->len + 1);
	g_string_free (str_list, TRUE);
}

gboolean
go_conf_get_bool (GOConfNode *node, gchar const *key)
{
	guchar val, *ptr = &val;
	gulong type, size = sizeof (val);

	if (go_conf_win32_get (node, key, &type, &ptr, &size, FALSE, NULL) &&
	    type == REG_BINARY)
		return val;

	return FALSE;
}

gint
go_conf_get_int	(GOConfNode *node, gchar const *key)
{
	gint val;
	gulong type, size = sizeof (DWORD);
	guchar *ptr = (guchar *) &val;

	if (go_conf_win32_get (node, key, &type, &ptr, &size, FALSE, NULL) &&
	    type == REG_DWORD)
		return val;

	return 0;
}

gdouble
go_conf_get_double (GOConfNode *node, gchar const *key)
{
	gchar *ptr = go_conf_get_string (node, key);
	gdouble val;

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
	DWORD type, size = 0;
	guchar *ptr = NULL;

	if (go_conf_win32_get (node, key, &type, &ptr, &size, TRUE, NULL) &&
	    type == REG_SZ)
		return ptr;

	g_free (ptr);

	return NULL;
}

GSList *
go_conf_get_str_list (GOConfNode *node, gchar const *key)
{
	return go_conf_load_str_list (node, key);
}

static guchar *
go_conf_get (GOConfNode *node, gchar const *key, gulong expected)
{
	gulong type, size = 0;
	guchar *ptr = NULL;
	gint ret_code;

	if (!go_conf_win32_get (node, key, &type, &ptr, &size, TRUE, &ret_code)) {
		LPTSTR msg_buf;

		FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER |
			       FORMAT_MESSAGE_FROM_SYSTEM |
			       FORMAT_MESSAGE_IGNORE_INSERTS,
			       NULL,
			       ret_code,
			       MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
			       (LPTSTR) &msg_buf,
			       0,
			       NULL);
		d (g_warning ("Unable to load key '%s' : because %s",
			      key, msg_buf));
		LocalFree (msg_buf);
		g_free (ptr);
		return NULL;
	}

	if (type != expected) {
		g_warning ("Expected `%lu' got `%lu' for key %s of node %s",
			   expected, type, key, node->path);
		g_free (ptr);
		return NULL;
	}

	return ptr;
}

gboolean
go_conf_load_bool (GOConfNode *node, gchar const *key,
		   gboolean default_val)
{
	guchar *val = go_conf_get (node, key, REG_BINARY);
	gboolean res;

	if (val) {
		res = (gboolean) *val;
		g_free (val);
	} else {
		d (g_warning ("Using default value '%s'", default_val ? "true" : "false"));
		return default_val;
	}

	return res;
}

gint
go_conf_load_int (GOConfNode *node, gchar const *key,
		  gint minima, gint maxima, gint default_val)
{
	guchar *val = go_conf_get (node, key, REG_DWORD);
	gint res;

	if (val) {
		res = *(gint *) val;
		g_free (val);
		if (res < minima || maxima < res) {
			g_warning ("Invalid value '%d' for %s. If should be >= %d and <= %d",
				   res, key, minima, maxima);
			val = NULL;
		}
	}
	if (!val) {
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
	gchar *val = (gchar *) go_conf_get (node, key, REG_SZ);

	if (val) {
		res = g_ascii_strtod (val, NULL);
		g_free (val);
		if (errno == ERANGE || res < minima || maxima < res) {
			g_warning ("Invalid value '%g' for %s.  If should be >= %g and <= %g",
				   res, key, minima, maxima);
			val = NULL;
		}
	}
	if (!val) {
		d (g_warning ("Using default value '%g'", default_val));
		return default_val;
	}

	return res;
}

gchar *
go_conf_load_string (GOConfNode *node, gchar const *key)
{
	return go_conf_get (node, key, REG_SZ);
}

GSList *
go_conf_load_str_list (GOConfNode *node, gchar const *key)
{
	GSList *list = NULL;
	gchar *ptr;
	gchar **str_list;
	gint i;

	if ((ptr = go_conf_get_string (node, key)) != NULL) {
		str_list = g_strsplit ((gchar const *) ptr, "\n", 0);
		for (i = 0; str_list[i]; ++i)
			list = g_slist_prepend (list, g_strcompress (str_list[i]));
		list = g_slist_reverse (list);
		g_strfreev (str_list);
		g_free (ptr);
	}

	return list;
}

gchar *
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
	gulong type, size;
	guchar *ptr = NULL;
	GType t = G_TYPE_NONE;

	if (go_conf_win32_get (node, key, &type, &ptr, &size, FALSE, NULL)) {
		switch (type) {
		case REG_BINARY:
			t = G_TYPE_BOOLEAN; break;
		case REG_DWORD:
			t = G_TYPE_INT; break;
		case REG_SZ:
			t = G_TYPE_STRING; break;
		}
	}

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
		value_string = go_conf_get_string (node, key);
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
go_conf_set_value_from_str (GOConfNode *node, gchar const *key,
			    gchar const *val_str)
{
	switch (go_conf_get_type (node, key)) {
	case G_TYPE_STRING:
		go_conf_set_string (node, key, val_str);
		break;
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
go_conf_sync (GOConfNode *node)
{
	if (node)
		RegFlushKey (node->hKey);
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
