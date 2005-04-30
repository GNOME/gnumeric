/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnumeric-gconf.c:
 *
 * Author:
 * 	Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2002-2005 Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * Introduced the concept of "node" and implemented the win32 backend
 * by Ivan, Wong Yat Cheung <email@ivanwong.info>, 2005
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "application.h"
#include "gnumeric-gconf.h"
#include "gnumeric-gconf-priv.h"
#include "gutils.h"
#include "mstyle.h"

static GnmAppPrefs prefs;
GnmAppPrefs const *gnm_app_prefs = &prefs;
static GOConfNode *root = NULL;

#ifdef WITH_GNOME
#include <format.h>
#include <value.h>
#include <number-match.h>
#include <gconf/gconf-client.h>

struct _GOConfNode {
	gchar *path;
	gboolean root;
};

static GConfClient *gconf_client = NULL;


static void
go_conf_init ()
{
	if (!gconf_client)
		gconf_client = gconf_client_get_default ();
}

static void
go_conf_shutdown ()
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
		g_warning ("Unable to load key '%s' : because %s",
			      real_key, err->message);
		g_free (real_key);
		g_error_free (err);
		return NULL;
	}
	if (val == NULL) {
		g_warning ("Unable to load key '%s'", real_key);
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
		g_warning ("Using default value '%s'", default_val ? "true" : "false");
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
		g_warning ("Using default value '%d'", default_val);
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
		g_warning ("Using default value '%g'", default_val);
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
	GConfClient *gconf = gconf_client;

	switch (go_conf_get_type (node, key)) {
	case G_TYPE_STRING:
		value_string = gconf_client_get_string (gconf, key, NULL);

		break;
	case G_TYPE_INT:
		value_string = g_strdup_printf ("%i", go_conf_get_int (node, key));
		break;
	case G_TYPE_FLOAT:
		value_string = g_strdup_printf ("%f", go_conf_get_double (node, key));
		break;
	case G_TYPE_BOOLEAN:
		value_string = g_strdup (format_boolean (go_conf_get_bool (node, key)));
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

gboolean
go_conf_set_value_from_str (GOConfNode *node, gchar const *key, gchar const *val_str)
{
	switch (go_conf_get_type (node, key)) {
	case G_TYPE_STRING:
		go_conf_set_string (node, key, val_str);
		break;
	case G_TYPE_FLOAT: {
		GnmDateConventions const *conv = NULL;  /* workbook_date_conv (state->wb); */
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
		GnmDateConventions const *conv = NULL;  /* workbook_date_conv (state->wb); */
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
		GnmDateConventions const *conv = NULL;  /* workbook_date_conv (state->wb); */
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

#elif defined G_OS_WIN32

#include <windows.h>
#include <format.h>
#include <value.h>
#include <number-match.h>

#ifndef ERANGE
/* mingw has not defined ERANGE (yet), MSVC has it though */
# define ERANGE 34
#endif

struct _GOConfNode {
	HKEY hKey;
	gchar *path;
};

static void
go_conf_init ()
{
}

static void
go_conf_shutdown ()
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

	str_list = g_string_new ("");
	while (list) {
		g_string_append (str_list, g_strescape (list->data, NULL));
		g_string_append_c (str_list, '\n');
		list = list->next;
	}
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
	GSList *list = NULL;
	gchar *ptr;
	gchar **str_list;
	gint i;

	if ((ptr = go_conf_get_string (node, key)) != NULL) {
		str_list = g_strsplit ((const gchar *) ptr, "\n", 0);
		for (i = 0; str_list[i]; ++i)
			list = g_slist_prepend (list, g_strcompress (str_list[i]));
		g_slist_reverse (list);
		g_strfreev (str_list);
		g_free (ptr);
	}

	return list;
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
		g_warning ("Unable to load key '%s' : because %s",
			   key, msg_buf);
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
		g_warning ("Using default value '%s'", default_val ? "true" : "false");
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
		g_warning ("Using default value '%d'", default_val);
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
		g_warning ("Using default value '%g'", default_val);
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
	return go_conf_get_str_list (node, key);
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
		value_string = g_strdup (format_boolean (go_conf_get_bool (node, key)));
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
		GnmDateConventions const *conv = NULL;  /* workbook_date_conv (state->wb); */
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
		GnmDateConventions const *conv = NULL;  /* workbook_date_conv (state->wb); */
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

#else

void
go_conf_set_bool (GOConfNode *node, gchar const *key, gboolean val)
{
}

void
go_conf_set_int (GOConfNode *node, gchar const *key, gint val)
{
}

void
go_conf_set_double (GOConfNode *node, gchar const *key, gnm_float val)
{
}

void
go_conf_set_string (GOConfNode *node, gchar const *key, char const *str)
{
}

void
go_conf_set_str_list (GOConfNode *node, gchar const *key, GSList *list)
{
}

gboolean
go_conf_get_bool (GOConfNode *node, gchar const *key)
{
	return FALSE;
}

gint
go_conf_get_int	(GOConfNode *node, gchar const *key)
{
	return 0;
}

gdouble
go_conf_get_double (GOConfNode *node, gchar const *key)
{
	return 0.;
}

gchar *
go_conf_get_string (GOConfNode *node, gchar const *key)
{
	return g_strdup ("");
}

GSList *
go_conf_get_str_list (GOConfNode *node, gchar const *key)
{
	return NULL;
}

gboolean
go_conf_load_bool (GOConfNode *node, gchar const *key,
		   gboolean default_val)
{
	return default_val;
}
int
go_conf_load_int (GOConfNode *node, gchar const *key,
		  gint minima, gint maxima,
		  gint default_val)
{
	return default_val;
}

double
go_conf_load_double (GOConfNode *node, gchar const *key,
		     gdouble minima, gdouble maxima,
		     gdouble default_val)
{
	return default_val;
}
char *
go_conf_load_string (GOConfNode *node, gchar const *key)
{
	return NULL;
}
GSList *
go_conf_load_str_list (GOConfNode *node, gchar const *key)
{
	return NULL;
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
	return G_TYPE_NONE;
}

gchar *
go_conf_get_value_as_str (GOConfNode *node, gchar const *key)
{
	return g_strdup ("");
}

gboolean
go_conf_set_value_from_str (GOConfNode *node, gchar const *key, gchar const *val_str)
{
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

#endif

static void
gnm_conf_init_printer_decoration_font (void)
{
	GOConfNode *node;
	gchar *name;
	if (prefs.printer_decoration_font == NULL)
		prefs.printer_decoration_font = mstyle_new ();

	node = go_conf_get_node (root, PRINTSETUP_GCONF_DIR);
	name = go_conf_load_string (node, PRINTSETUP_GCONF_HF_FONT_NAME);
	if (name) {
		mstyle_set_font_name (prefs.printer_decoration_font, name);
		g_free (name);
	} else
		mstyle_set_font_name (prefs.printer_decoration_font, DEFAULT_FONT);
	mstyle_set_font_size (prefs.printer_decoration_font,
		go_conf_load_double (node, PRINTSETUP_GCONF_HF_FONT_SIZE, 1., 100., DEFAULT_SIZE));
	mstyle_set_font_bold (prefs.printer_decoration_font,
		go_conf_load_bool (node, PRINTSETUP_GCONF_HF_FONT_BOLD, FALSE));
	mstyle_set_font_italic (prefs.printer_decoration_font,
		go_conf_load_bool (node, PRINTSETUP_GCONF_HF_FONT_ITALIC, FALSE));
	go_conf_free_node (node);
}

static void
gnm_conf_init_essential (void)
{
	GOConfNode *node;
	
	node = go_conf_get_node (root, CONF_DEFAULT_FONT_DIR);
	prefs.default_font.name = go_conf_load_string (node, CONF_DEFAULT_FONT_NAME);
	if (prefs.default_font.name == NULL)
		prefs.default_font.name = g_strdup (DEFAULT_FONT);
	prefs.default_font.size = go_conf_load_double (
		node, CONF_DEFAULT_FONT_SIZE, 1., 100., DEFAULT_SIZE);
	prefs.default_font.is_bold = go_conf_load_bool (
		node, CONF_DEFAULT_FONT_BOLD, FALSE);
	prefs.default_font.is_italic = go_conf_load_bool (
		node, CONF_DEFAULT_FONT_ITALIC, FALSE);
	go_conf_free_node (node);

	node = go_conf_get_node (root, GNM_CONF_FILE_DIR);
	prefs.file_history_max = go_conf_load_int (
		node, GNM_CONF_FILE_HISTORY_N, 0, 20, 4);
	prefs.file_history_files = go_conf_load_str_list (node, GNM_CONF_FILE_HISTORY_FILES);
	go_conf_free_node (node);

	node = go_conf_get_node (root, PLUGIN_GCONF_DIR);
	prefs.plugin_file_states = go_conf_load_str_list (node, PLUGIN_GCONF_FILE_STATES);
	prefs.plugin_extra_dirs = go_conf_load_str_list (node, PLUGIN_GCONF_EXTRA_DIRS);
	prefs.active_plugins = go_conf_load_str_list (node, PLUGIN_GCONF_ACTIVE);
	prefs.activate_new_plugins = go_conf_load_bool (
		node, PLUGIN_GCONF_ACTIVATE_NEW, TRUE);
	go_conf_free_node (node);

	node = go_conf_get_node (root, GNM_CONF_GUI_DIR);
	prefs.horizontal_dpi = go_conf_load_double (
		node, GNM_CONF_GUI_RES_H, 10., 1000., 96.);
	prefs.vertical_dpi = go_conf_load_double (
		node, GNM_CONF_GUI_RES_V, 10., 1000., 96.);
	prefs.initial_sheet_number = go_conf_load_int (
		root, GNM_CONF_WORKBOOK_NSHEETS, 1, 64, 3);
	prefs.horizontal_window_fraction = go_conf_load_double (
		  node, GNM_CONF_GUI_WINDOW_X, .1, 1., .6);
	prefs.vertical_window_fraction = go_conf_load_double (
		  node, GNM_CONF_GUI_WINDOW_Y, .1, 1., .6);
	prefs.zoom = go_conf_load_double (
		  node, GNM_CONF_GUI_ZOOM, .1, 5., 1.);
	prefs.auto_complete = go_conf_load_bool (
		  node, GNM_CONF_GUI_ED_AUTOCOMPLETE, TRUE);
	prefs.live_scrolling = go_conf_load_bool (
		  node, GNM_CONF_GUI_ED_LIVESCROLLING, TRUE);
	go_conf_free_node (node);

	/* Unfortunately we need the printing stuff in essentials since the */
	/* first pi is created for the new sheet before the idle loop has a */
	/* chance to run                                                    */
	node = go_conf_get_node (root, PRINTSETUP_GCONF_DIR);
	prefs.printer_config = go_conf_load_string (node, PRINTSETUP_GCONF_PRINTER_CONFIG);
	prefs.print_center_horizontally = go_conf_load_bool 
		(node, PRINTSETUP_GCONF_CENTER_HORIZONTALLY, FALSE); 
	prefs.print_center_vertically = go_conf_load_bool 
		(node, PRINTSETUP_GCONF_CENTER_VERTICALLY, FALSE);
	prefs.print_grid_lines = go_conf_load_bool 
		(node, PRINTSETUP_GCONF_PRINT_GRID_LINES, FALSE);
	prefs.print_even_if_only_styles = go_conf_load_bool 
		(node, PRINTSETUP_GCONF_EVEN_IF_ONLY_STYLES, FALSE);
	prefs.print_black_and_white = go_conf_load_bool 
		(node, PRINTSETUP_GCONF_PRINT_BLACK_AND_WHITE, FALSE);
	prefs.print_titles = go_conf_load_bool 
		(node, PRINTSETUP_GCONF_PRINT_TITLES, FALSE);
	prefs.print_order_right_then_down = go_conf_load_bool 
		(node, PRINTSETUP_GCONF_RIGHT_THEN_DOWN, FALSE);
	prefs.print_scale_percentage = go_conf_load_bool 
		(node, PRINTSETUP_GCONF_SCALE_PERCENTAGE, TRUE);
	prefs.print_scale_percentage_value = go_conf_load_double 
		(node, PRINTSETUP_GCONF_SCALE_PERCENTAGE_VALUE, 1, 500, 100);
	prefs.print_scale_width = go_conf_load_int 
		(node, PRINTSETUP_GCONF_SCALE_WIDTH, 0, 100, 1);
        prefs.print_scale_height = go_conf_load_int 
		(node, PRINTSETUP_GCONF_SCALE_HEIGHT, 0, 100, 1);
	prefs.print_repeat_top = go_conf_load_string (node, PRINTSETUP_GCONF_REPEAT_TOP);
	prefs.print_repeat_left = go_conf_load_string (node, PRINTSETUP_GCONF_REPEAT_LEFT);
	prefs.print_tb_margins.top.points = go_conf_load_double 
		(node, PRINTSETUP_GCONF_MARGIN_TOP, 0.0, 10000.0, 120.0);
	prefs.print_tb_margins.bottom.points = go_conf_load_double 
		(node, PRINTSETUP_GCONF_MARGIN_BOTTOM, 0.0, 10000.0, 120.0);
	{
		/* Note: the desired display unit is stored in the  */
		/* printer config. So we are never using this field */
		/* inside the margin structure, but only setting it */
		/* in various input routines.                       */
		prefs.print_tb_margins.top.desired_display 
			= gnome_print_unit_get_by_abbreviation ("cm");
		prefs.print_tb_margins.bottom.desired_display 
			= prefs.print_tb_margins.top.desired_display;
	}
	prefs.print_all_sheets = go_conf_load_bool (
		node, PRINTSETUP_GCONF_ALL_SHEETS, TRUE);
	prefs.printer_header = go_conf_load_str_list (node, PRINTSETUP_GCONF_HEADER);
	prefs.printer_footer = go_conf_load_str_list (node, PRINTSETUP_GCONF_FOOTER);
	prefs.printer_header_formats_left = go_conf_load_str_list (node, PRINTSETUP_GCONF_HEADER_FORMAT_LEFT);
	prefs.printer_header_formats_middle = go_conf_load_str_list (node, PRINTSETUP_GCONF_HEADER_FORMAT_MIDDLE);
	prefs.printer_header_formats_right = go_conf_load_str_list (node, PRINTSETUP_GCONF_HEADER_FORMAT_RIGHT);
	go_conf_free_node (node);
}

static gboolean
gnm_conf_init_extras (void)
{
	char *tmp;
	GOConfNode *node;

	node = go_conf_get_node (root, FUNCTION_SELECT_GCONF_DIR);
	prefs.num_of_recent_funcs = go_conf_load_int (
		node, FUNCTION_SELECT_GCONF_NUM_OF_RECENT, 0, 40, 10);
	prefs.recent_funcs = go_conf_load_str_list (node, FUNCTION_SELECT_GCONF_RECENT);
	go_conf_free_node (node);

	node = go_conf_get_node (root, GNM_CONF_GUI_DIR);
	prefs.transition_keys = go_conf_load_bool (
		node, GNM_CONF_GUI_ED_TRANSITION_KEYS, FALSE);
	prefs.recalc_lag = go_conf_load_int (
		node, GNM_CONF_GUI_ED_RECALC_LAG, -5000, 5000, 200);
	go_conf_free_node (node);

	node = go_conf_get_node (root, GNM_CONF_UNDO_DIR);
	prefs.show_sheet_name = go_conf_load_bool (
		node, GNM_CONF_UNDO_SHOW_SHEET_NAME, TRUE);
	prefs.max_descriptor_width = go_conf_load_int (
		node, GNM_CONF_UNDO_MAX_DESCRIPTOR_WIDTH, 5, 256, 15);
	prefs.undo_size = go_conf_load_int (
		node, GNM_CONF_UNDO_SIZE, 1, 1000000, 100000);
	prefs.undo_max_number = go_conf_load_int (
		node, GNM_CONF_UNDO_MAXNUM, 0, 10000, 100);
	go_conf_free_node (node);

	node = go_conf_get_node (root, AUTOFORMAT_GCONF_DIR);
	prefs.autoformat.extra_dirs = go_conf_load_str_list (node, AUTOFORMAT_GCONF_EXTRA_DIRS);
	tmp = go_conf_load_string (node, AUTOFORMAT_GCONF_SYS_DIR);
	if (tmp == NULL)
		tmp = g_strdup ("autoformat-templates");
	prefs.autoformat.sys_dir = gnm_sys_data_dir (tmp);
	g_free (tmp);
	tmp = go_conf_load_string (node, AUTOFORMAT_GCONF_USR_DIR);
	if (tmp == NULL)
		tmp = g_strdup ("autoformat-templates");
	prefs.autoformat.usr_dir = gnm_usr_dir (tmp);
	g_free (tmp);
	go_conf_free_node (node);

	prefs.xml_compression_level = go_conf_load_int (
		root, GNM_CONF_XML_COMPRESSION, 0, 9, 9);

	node = go_conf_get_node (root, GNM_CONF_FILE_DIR);
	prefs.file_overwrite_default_answer = go_conf_load_bool (
		node, GNM_CONF_FILE_OVERWRITE_DEFAULT, FALSE);
	prefs.file_ask_single_sheet_save = go_conf_load_bool (
		node, GNM_CONF_FILE_SINGLE_SHEET_SAVE, TRUE);
	go_conf_free_node (node);

	node = go_conf_get_node (root, GNM_CONF_SORT_DIR);
	prefs.sort_default_by_case = go_conf_load_bool (
		node, GNM_CONF_SORT_DEFAULT_BY_CASE, FALSE);
	prefs.sort_default_retain_formats = go_conf_load_bool (
		node, GNM_CONF_SORT_DEFAULT_RETAIN_FORM, TRUE);
	prefs.sort_default_ascending = go_conf_load_bool (
		node, GNM_CONF_SORT_DEFAULT_ASCENDING, TRUE);
	prefs.sort_max_initial_clauses = go_conf_load_int (
		node, GNM_CONF_SORT_DIALOG_MAX_INITIAL, 0, 256, 10);
	go_conf_free_node (node);

	prefs.unfocused_range_selection = go_conf_load_bool (
		root, DIALOGS_GCONF_DIR "/" DIALOGS_GCONF_UNFOCUSED_RS, TRUE);
	prefs.prefer_clipboard_selection = go_conf_load_bool (
		root, GNM_CONF_CUTANDPASTE_DIR "/" GNM_CONF_CUTANDPASTE_PREFER_CLIPBOARD, TRUE);
	prefs.latex_use_utf8 = go_conf_load_bool (
		root, PLUGIN_GCONF_LATEX "/" PLUGIN_GCONF_LATEX_USE_UTF8, TRUE); 

	gnm_conf_init_printer_decoration_font ();

	return FALSE;
}

/**
 * gnm_conf_init
 *
 * @fast : Load non-essential prefs in an idle handler
 **/
void
gnm_conf_init (gboolean fast)
{
	go_conf_init ();
	root = go_conf_get_node (NULL, GNM_CONF_DIR);
	gnm_conf_init_essential ();
	if (fast)
		g_timeout_add (1000, (GSourceFunc) gnm_conf_init_extras, NULL);
	else
		gnm_conf_init_extras ();
}

void     
gnm_conf_shutdown (void)
{
	if (prefs.printer_decoration_font) {
		mstyle_unref (prefs.printer_decoration_font);
		prefs.printer_decoration_font = NULL;
	}
	go_conf_free_node (root);
	go_conf_shutdown ();
}

GOConfNode *
gnm_conf_get_root (void)
{
	return root;
}

void
gnm_gconf_set_plugin_file_states (GSList *list)
{
	g_return_if_fail (prefs.plugin_file_states != list);

	/* the const_casts are ok, the const in the header is just to keep
	 * people for doing stupid things */
	g_slist_foreach ((GSList *)prefs.plugin_file_states, (GFunc)g_free, NULL);
	g_slist_free ((GSList *)prefs.plugin_file_states);
	prefs.plugin_file_states = list;

	go_conf_set_str_list (root, PLUGIN_GCONF_DIR "/" PLUGIN_GCONF_FILE_STATES, list);
}

void
gnm_gconf_set_plugin_extra_dirs (GSList *list)
{
	g_return_if_fail (prefs.plugin_extra_dirs != list);

	/* the const_casts are ok, the const in the header is just to keep
	 * people for doing stupid things */
	g_slist_foreach ((GSList *)prefs.plugin_extra_dirs, (GFunc)g_free, NULL);
	g_slist_free ((GSList *)prefs.plugin_extra_dirs);
	prefs.plugin_extra_dirs = list;

	go_conf_set_str_list (root, PLUGIN_GCONF_DIR "/" PLUGIN_GCONF_EXTRA_DIRS, list);
}

void
gnm_gconf_set_active_plugins (GSList *list)
{
	go_conf_set_str_list (root, PLUGIN_GCONF_DIR "/" PLUGIN_GCONF_ACTIVE, list);
}

void
gnm_gconf_set_activate_new_plugins (gboolean val)
{
	go_conf_set_bool (root, PLUGIN_GCONF_DIR "/" PLUGIN_GCONF_ACTIVATE_NEW, val);
}

void
gnm_gconf_set_recent_funcs (GSList *list)
{
	go_conf_set_str_list (root, FUNCTION_SELECT_GCONF_DIR "/" FUNCTION_SELECT_GCONF_RECENT, list);

	/* the const_casts are ok, the const in the header is just to keep
	 * people for doing stupid things */
	g_slist_foreach ((GSList *)prefs.recent_funcs, (GFunc)g_free, NULL);
	g_slist_free ((GSList *)prefs.recent_funcs);

	prefs.recent_funcs = list;
}

void
gnm_gconf_set_num_recent_functions (gint val)
{
	if (val < 0)
		val = 0;
	prefs.num_of_recent_funcs = val;
	go_conf_set_int (root, FUNCTION_SELECT_GCONF_DIR "/" FUNCTION_SELECT_GCONF_NUM_OF_RECENT, val);
}

void
gnm_gconf_set_file_history_files (GSList *list)
{
	g_return_if_fail (prefs.file_history_files != list);

	/* the const_casts are ok, the const in the header is just to keep
	 * people for doing stupid things */
	g_slist_foreach ((GSList *)prefs.file_history_files, (GFunc)g_free, NULL);
	g_slist_free ((GSList *)prefs.file_history_files);
	prefs.file_history_files = list;
	go_conf_set_str_list (root, GNM_CONF_FILE_DIR "/" GNM_CONF_FILE_HISTORY_FILES, list);
}

void
gnm_gconf_set_file_history_number (gint val)
{
	if (val < 0)
		val = 0;
	prefs.file_history_max = val; 
	go_conf_set_int (root, GNM_CONF_FILE_DIR "/" GNM_CONF_FILE_HISTORY_N, val);
}


void
gnm_gconf_set_undo_size (gint val)
{
	if (val < 1)
		val = 1;
	prefs.undo_size = val; 
	go_conf_set_int (root, GNM_CONF_UNDO_DIR "/" GNM_CONF_UNDO_SIZE, val);
}


void
gnm_gconf_set_undo_max_number (gint val)
{
	if (val < 1)
		val = 1;
	prefs.undo_max_number = val;
	go_conf_set_int (root, GNM_CONF_UNDO_DIR "/" GNM_CONF_UNDO_MAXNUM, val);
}

void
gnm_gconf_set_autoformat_sys_dirs (char const * string)
{
	go_conf_set_string (root, AUTOFORMAT_GCONF_DIR "/" AUTOFORMAT_GCONF_SYS_DIR, string);
}

void
gnm_gconf_set_autoformat_usr_dirs (char const * string)
{
	go_conf_set_string (root, AUTOFORMAT_GCONF_DIR "/" AUTOFORMAT_GCONF_USR_DIR, string);
}

void
gnm_gconf_set_all_sheets (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_ALL_SHEETS, val);
}

void
gnm_gconf_set_printer_config (gchar const *str)
{
	go_conf_set_string (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_PRINTER_CONFIG, str);
	if (prefs.printer_config != str) {
		g_free (prefs.printer_config);
		prefs.printer_config = g_strdup (str);
	}
}

void
gnm_gconf_set_printer_header (gchar const *left, gchar const *middle, 
			      gchar const *right)
{
	GSList *list = NULL;
	list = g_slist_prepend (list, g_strdup (right));
	list = g_slist_prepend (list, g_strdup (middle));
	list = g_slist_prepend (list, g_strdup (left));
	go_conf_set_str_list (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_HEADER, list);
	gnm_slist_free_custom ((GSList *)prefs.printer_header, g_free);
	prefs.printer_header = list;
}

void
gnm_gconf_set_printer_footer (gchar const *left, gchar const *middle, 
			      gchar const *right)
{
	GSList *list = NULL;
	list = g_slist_prepend (list, g_strdup (right));
	list = g_slist_prepend (list, g_strdup (middle));
	list = g_slist_prepend (list, g_strdup (left));
	go_conf_set_str_list (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_FOOTER, list);
	gnm_slist_free_custom ((GSList *)prefs.printer_footer, g_free);
	prefs.printer_footer = list;
}

void     
gnm_gconf_set_print_center_horizontally (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_CENTER_HORIZONTALLY, val);
}

void     
gnm_gconf_set_print_center_vertically (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_CENTER_VERTICALLY, val);
}

void     
gnm_gconf_set_print_grid_lines (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_PRINT_GRID_LINES, val);
}

void     
gnm_gconf_set_print_even_if_only_styles (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_EVEN_IF_ONLY_STYLES, val);
}

void     
gnm_gconf_set_print_black_and_white (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_PRINT_BLACK_AND_WHITE, val);
}

void     
gnm_gconf_set_print_titles (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_PRINT_TITLES, val);
}

void     
gnm_gconf_set_print_order_right_then_down (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_RIGHT_THEN_DOWN, val);
}

void     
gnm_gconf_set_print_scale_percentage (gboolean val)
{
	go_conf_set_bool (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_SCALE_PERCENTAGE, val);
}

void     
gnm_gconf_set_print_scale_percentage_value (gnm_float val)
{
	go_conf_set_double (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_SCALE_PERCENTAGE_VALUE, val);
}

void     
gnm_gconf_set_print_tb_margins (PrintMargins const *pm)
{
	/* We are not saving the GnomePrintUnits since they are */
	/* duplicated in the gnomeprintconfig                   */
	go_conf_set_double (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_MARGIN_TOP, pm->top.points);
	go_conf_set_double (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_MARGIN_BOTTOM, pm->bottom.points);
}

void     
gnm_gconf_set_print_header_formats (GSList *left, GSList *middle, 
				    GSList *right)
{
	go_conf_set_str_list (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_HEADER_FORMAT_LEFT, left);
	gnm_slist_free_custom (left, g_free);
	go_conf_set_str_list (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_HEADER_FORMAT_MIDDLE, middle);
	gnm_slist_free_custom (middle, g_free);
	go_conf_set_str_list (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_HEADER_FORMAT_RIGHT, right);
	gnm_slist_free_custom (right, g_free);
}

void     
gnm_gconf_set_gui_window_x (gnm_float val)
{
	prefs.horizontal_window_fraction = val;
	go_conf_set_double (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_WINDOW_X, val);
}

void     
gnm_gconf_set_gui_window_y (gnm_float val)
{
	prefs.vertical_window_fraction = val;
	go_conf_set_double (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_WINDOW_Y, val);
}

void     
gnm_gconf_set_gui_zoom (gnm_float val)
{
	prefs.zoom = val;
	go_conf_set_double (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_WINDOW_Y, val);
}

void     
gnm_gconf_set_default_font_size (gnm_float val)
{
	prefs.default_font.size = val;
	go_conf_set_double (
		root, GNM_CONF_FONT_DIR "/" GNM_CONF_FONT_SIZE, val);
}

void     
gnm_gconf_set_default_font_name (char const *str)
{
	go_conf_set_string (root, GNM_CONF_FONT_DIR "/" GNM_CONF_FONT_NAME, str);
	if (prefs.default_font.name != str) {
		/* the const in the header is just a safety net */
		g_free ((char *) prefs.default_font.name);
		prefs.default_font.name = g_strdup (str);
	}
}

void     
gnm_gconf_set_default_font_bold (gboolean val)
{
	prefs.default_font.is_bold = val;
	go_conf_set_bool (
		root, GNM_CONF_FONT_DIR "/" GNM_CONF_FONT_BOLD, val);
}

void     
gnm_gconf_set_default_font_italic (gboolean val)
{
	prefs.default_font.is_italic = val;
	go_conf_set_bool (
		root, GNM_CONF_FONT_DIR "/" GNM_CONF_FONT_ITALIC, val);
}

void
gnm_gconf_set_hf_font (GnmStyle const *mstyle)
{
	GOConfNode *node;
	GnmStyle *old_style = (prefs.printer_decoration_font != NULL) ?
		prefs.printer_decoration_font :
		mstyle_new_default ();
	
	prefs.printer_decoration_font = mstyle_copy_merge (old_style, mstyle);
	mstyle_unref (old_style);
	
	node = go_conf_get_node (root, PRINTSETUP_GCONF_DIR);
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_SIZE))
		go_conf_set_double (node, PRINTSETUP_GCONF_HF_FONT_SIZE,
			mstyle_get_font_size (mstyle));
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_NAME))
		go_conf_set_string (node, PRINTSETUP_GCONF_HF_FONT_NAME,
			mstyle_get_font_name (mstyle));
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_BOLD))
		go_conf_set_bool (node, PRINTSETUP_GCONF_HF_FONT_BOLD,
			mstyle_get_font_bold (mstyle));
	if (mstyle_is_element_set (mstyle, MSTYLE_FONT_ITALIC))
		go_conf_set_bool (node, PRINTSETUP_GCONF_HF_FONT_ITALIC,
			mstyle_get_font_italic (mstyle));
	go_conf_free_node (node);
}


void
gnm_gconf_set_max_descriptor_width (gint val)
{
	if (val < 1)
		val = 1;
	prefs.max_descriptor_width = val;
	go_conf_set_int (
		root, GNM_CONF_UNDO_DIR "/" GNM_CONF_UNDO_MAX_DESCRIPTOR_WIDTH, val);
}

void
gnm_gconf_set_sort_dialog_max_initial (gint val)
{
	if (val < 1)
		val = 1;
	prefs.sort_max_initial_clauses = val;
	go_conf_set_int (
		root, GNM_CONF_SORT_DIR "/" GNM_CONF_SORT_DIALOG_MAX_INITIAL, val);
}

void
gnm_gconf_set_workbook_nsheets (gint val)
{
	if (val < 1)
		val = 1;
	prefs.initial_sheet_number = val;
	go_conf_set_int (root, GNM_CONF_WORKBOOK_NSHEETS, val);
}

void
gnm_gconf_set_xml_compression (gint val)
{
	if (val < 0)
		val = 0;
	prefs.xml_compression_level = val;
	go_conf_set_int (root, GNM_CONF_XML_COMPRESSION, val);
}

void     
gnm_gconf_set_show_sheet_name (gboolean val)
{
	prefs.show_sheet_name = val;
	go_conf_set_bool (
		root, GNM_CONF_UNDO_DIR "/" GNM_CONF_UNDO_SHOW_SHEET_NAME,val != FALSE);
}

void     
gnm_gconf_set_latex_use_utf8 (gboolean val)
{
	prefs.latex_use_utf8 = val;
	go_conf_set_bool (
		root, PLUGIN_GCONF_LATEX "/" PLUGIN_GCONF_LATEX_USE_UTF8, val != FALSE);
}

void     
gnm_gconf_set_sort_retain_form (gboolean val)
{
	prefs.sort_default_retain_formats = val;
	go_conf_set_bool (
		root, GNM_CONF_SORT_DIR "/" GNM_CONF_SORT_DEFAULT_RETAIN_FORM, val != FALSE);
}

void     
gnm_gconf_set_sort_by_case (gboolean val)
{
	prefs.sort_default_by_case = val;
	go_conf_set_bool (
		root, GNM_CONF_SORT_DIR "/" GNM_CONF_SORT_DEFAULT_BY_CASE, val != FALSE);
}

void     
gnm_gconf_set_sort_ascending (gboolean val)
{
	prefs.sort_default_ascending = val;
	go_conf_set_bool (
		root, GNM_CONF_SORT_DIR "/" GNM_CONF_SORT_DEFAULT_ASCENDING, val != FALSE);
}

void     
gnm_gconf_set_gui_transition_keys (gboolean val)
{
	prefs.transition_keys = val;
	go_conf_set_bool (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_ED_TRANSITION_KEYS, val != FALSE);
}

void     
gnm_gconf_set_gui_livescrolling (gboolean val)
{
	prefs.live_scrolling = val;
	go_conf_set_bool (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_ED_LIVESCROLLING, val != FALSE);
}

void     
gnm_gconf_set_file_overwrite (gboolean val)
{
	prefs.file_overwrite_default_answer = val;
	go_conf_set_bool (
		root, GNM_CONF_FILE_DIR "/" GNM_CONF_FILE_OVERWRITE_DEFAULT, val != FALSE);
}

void     
gnm_gconf_set_file_single_sheet_save (gboolean val)
{
	prefs.file_ask_single_sheet_save = val;
	go_conf_set_bool (
		root, GNM_CONF_FILE_DIR "/" GNM_CONF_FILE_SINGLE_SHEET_SAVE, val != FALSE);
}

void    
gnm_gconf_set_gui_resolution_h (gnm_float val)
{
	if (val < 50)
		val = 50;
	if (val > 250)
		val = 250;
	prefs.horizontal_dpi = val;
	go_conf_set_double (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_RES_H, val);
}

void     
gnm_gconf_set_gui_resolution_v (gnm_float val)
{
	if (val < 50)
		val = 50;
	if (val > 250)
		val = 250;
	prefs.vertical_dpi = val;
	go_conf_set_double (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_RES_V, val);
}

void     
gnm_gconf_set_unfocused_rs (gboolean val)
{
	prefs.unfocused_range_selection = val;
	go_conf_set_bool (
		root, DIALOGS_GCONF_DIR "/" DIALOGS_GCONF_UNFOCUSED_RS, val != FALSE);
}

void     
gnm_gconf_set_autocomplete (gboolean val)
{
	prefs.auto_complete = val;
	go_conf_set_bool (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_ED_AUTOCOMPLETE, val != FALSE);
}

void     
gnm_gconf_set_prefer_clipboard  (gboolean val)
{
	prefs.prefer_clipboard_selection = val;
	go_conf_set_bool (
		root, GNM_CONF_CUTANDPASTE_DIR "/" GNM_CONF_CUTANDPASTE_PREFER_CLIPBOARD, val != FALSE);
}

