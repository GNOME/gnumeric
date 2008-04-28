/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Interface Gnumeric to Databases
 * Copyright (C) 1998,1999 Michael Lausch
 * Copyright (C) 2000-2002 Rodrigo Moya
 * Copyright (C) 2006 Vivien Malerba
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <libgda/libgda.h>
#include <string.h>
#ifdef HAVE_LIBGNOMEDB
#include <libgnomedb/gnome-db-login-dialog.h>
#include <libgnomedb/gnome-db-login.h>
#endif

#include "func.h"
#include "expr.h"
#include "value.h"
#include "workbook.h"
#include "sheet.h"
#include "gnm-i18n.h"
#include <goffice/app/go-plugin.h>
#include <goffice/app/error-info.h>
#include <goffice/utils/datetime.h>
#include <goffice/utils/go-format.h>
#include <goffice/utils/go-glib-extras.h>
#include <gnm-plugin.h>

GNM_PLUGIN_MODULE_HEADER;

static GdaClient  *connection_pool = NULL;
static gboolean    libgda_init_done = FALSE;
static GHashTable *cnc_hash = NULL;

static GnmValue *
gnm_value_new_from_gda (GValue const *gval,
			GODateConventions const *date_conv)
{
	GnmValue *res;
	GType t;

	if (NULL == gval)
		return value_new_empty ();

	g_return_val_if_fail (G_IS_VALUE (gval), value_new_empty ());

	t = G_VALUE_TYPE (gval);
	if (t == GDA_TYPE_SHORT)
		return value_new_int (gda_value_get_short (gval));
	if (t == GDA_TYPE_USHORT)
		return value_new_int (gda_value_get_ushort (gval));
	if (t ==  G_TYPE_DATE) {
		res = value_new_int (datetime_g_to_serial (
			(GDate const *) g_value_get_boxed (gval), date_conv));
		value_set_fmt (res, go_format_default_date ());
		return res;
	}

	if (t == GDA_TYPE_TIME) {
		GdaTime const *time = gda_value_get_time (gval);
		res = value_new_float ( (time->hour +
					 (time->minute +
					  time->second / 60.) / 60.) / 24.),
		value_set_fmt (res, go_format_default_time ());
		return res;
	}

	switch (t) {
	case G_TYPE_BOOLEAN :
		return value_new_bool (g_value_get_boolean (gval));

	case G_TYPE_DOUBLE :
		return value_new_float (g_value_get_double (gval));
	case G_TYPE_FLOAT :
		return value_new_float (g_value_get_float (gval));
#if 0
	case G_TYPE_INT64 : /* g_value_get_int64 (gval) */
	case G_TYPE_UINT64 : /* g_value_get_uint64 (gval) */
#endif
	case G_TYPE_INT :
		return value_new_int (g_value_get_int (gval));
	case G_TYPE_UINT :
		return value_new_int (g_value_get_uint (gval));

#if 0
	/* No way to represent a timezone, leave it as a string for now */
	case GDA_TYPE_TIMESTAMP:
#endif
#if 0
	/* Do we want to consider nested arrays ??
	 * The rest of the system is not strong enough yet. */
	case GDA_TYPE_LIST : {
		GList const *ptr;
		for (ptr = gda_value_get_list (gval) ; NULL != ptr ; ptr = ptr->next) {
		}
		return array;
	}
#endif

#if 0
	/* Use the default gvalue conversions for these */
	case G_TYPE_CHAR :
	case G_TYPE_UCHAR :
	case G_TYPE_STRING :
	case GDA_TYPE_GEOMETRIC_POINT :
	case GDA_TYPE_BINARY :

	/* this is stored as a string, let gda handle it */
	case GDA_TYPE_NUMERIC :
#endif
	default :
		break;
	}

	if (g_value_type_transformable (G_VALUE_TYPE (gval), G_TYPE_STRING)) {
		GValue str = { 0 };
		g_value_init (&str, G_TYPE_STRING);
		if (g_value_transform (gval, &str))
			return value_new_string (g_value_get_string (&str));
		g_value_unset (&str);
	}

	return value_new_empty ();
}

static GnmValue *
display_recordset (GdaDataModel *recset, GnmFuncEvalInfo *ei)
{
	GODateConventions const *date_conv;
	GnmValue* array = NULL;
	gint   col;
	gint   row;
	gint   fieldcount = 0;
	gint   rowcount = 0;

	g_return_val_if_fail (GDA_IS_DATA_MODEL (recset), NULL);

	fieldcount = gda_data_model_get_n_columns (recset);
	rowcount = gda_data_model_get_n_rows (recset);

	/* convert the GdaDataModel in an array */
	if (rowcount <= 0)
		return value_new_empty ();

	if (rowcount >= gnm_sheet_get_max_rows (ei->pos->sheet))
		return value_new_error (ei->pos, _("Too much data returned"));

	date_conv = workbook_date_conv (ei->pos->sheet->workbook);
	array = value_new_array_empty (fieldcount, rowcount);
	for (row = 0; row < rowcount; row++) {
		for (col = 0; col < fieldcount; col++) {
			value_array_set (array, col, row,
				gnm_value_new_from_gda (
					gda_data_model_get_value_at (recset, col, row),
					date_conv));
		}
	}

	return array;
}

/*
 * Key structure and hash functions for that structure
 */
typedef struct {
	gchar *dsn;
	gchar *user;
	gchar *pass;
} CncKey;

static guint
cnc_key_hash_func (CncKey *key)
{
	guint retval = 0;

	if (key->dsn)
		retval = g_str_hash (key->dsn);
	if (key->user)
		retval = (retval << 4) + g_str_hash (key->user);
	if (key->pass)
		retval = (retval << 4) + g_str_hash (key->pass);

	return retval;
}

static gboolean
cnc_key_equal_func (CncKey *key1, CncKey *key2)
{
	if ((key1->dsn && !key2->dsn) ||
	    (!key1->dsn && key2->dsn) ||
	    (key1->dsn && key2->dsn && strcmp (key1->dsn, key2->dsn)))
		return FALSE;
	if ((key1->user && !key2->user) ||
	    (!key1->user && key2->user) ||
	    (key1->user && key2->user && strcmp (key1->user, key2->user)))
		return FALSE;
	if ((key1->pass && !key2->pass) ||
	    (!key1->pass && key2->pass) ||
	    (key1->pass && key2->pass && strcmp (key1->pass, key2->pass)))
		return FALSE;
	return TRUE;
}

static void
cnc_key_free (CncKey *key)
{
	g_free (key->dsn);
	g_free (key->user);
	g_free (key->pass);
	g_free (key);
}

static GdaConnection *
open_connection (const gchar *dsn, const gchar *user, const gchar *password, GdaConnectionOptions options)
{
	GdaConnection *cnc = NULL;
	gchar *real_dsn, *real_user, *real_password;
	GError *error = NULL;

	/* initialize connection pool if first time */
	if (!GDA_IS_CLIENT (connection_pool)) {
		if (!libgda_init_done) {
			gda_init (NULL, NULL, 0, NULL);
			libgda_init_done = TRUE;
		}
		connection_pool = gda_client_new ();
		if (!connection_pool)
			return NULL;
	}

	/* try to find a cnc object if we already have one */
	if (!cnc_hash)
		cnc_hash = g_hash_table_new_full ((GHashFunc) cnc_key_hash_func,
						  (GEqualFunc) cnc_key_equal_func,
						  (GDestroyNotify) cnc_key_free,
						  (GDestroyNotify) g_object_unref);
	else {
		CncKey key;

		key.dsn = (gchar *) dsn;
		key.user = (gchar *) user;
		key.pass = (gchar *) password;

		cnc = g_hash_table_lookup (cnc_hash, &key);
	}

	if (!cnc) {
		CncKey *key;

#ifdef HAVE_LIBGNOMEDB
		GtkWidget    *dialog =
			gnome_db_login_dialog_new (_("Database Connection"));
		GnomeDbLogin *login =
			gnome_db_login_dialog_get_login_widget (GNOME_DB_LOGIN_DIALOG (dialog));

		gnome_db_login_set_dsn (login, dsn);
		gnome_db_login_set_username (login, user);
		gnome_db_login_set_password (login, password);

		if (gnome_db_login_dialog_run (GNOME_DB_LOGIN_DIALOG (dialog))) {
			real_dsn = g_strdup (gnome_db_login_get_dsn (login));
			real_user = g_strdup (gnome_db_login_get_username (login));
			real_password = g_strdup (gnome_db_login_get_password (login));

			gtk_widget_destroy (dialog);
		} else {
			gtk_widget_destroy (dialog);
			return NULL;
		}
#else
		real_dsn = g_strdup (dsn);
		real_user = g_strdup (user);
		real_password = g_strdup (password);
#endif

		cnc = gda_client_open_connection (connection_pool, real_dsn, real_user, real_password, options, &error);
		if (!cnc) {
			g_warning ("Libgda error: %s\n", error->message);
			g_error_free (error);
		}

		g_free (real_dsn);
		g_free (real_user);
		g_free (real_password);

		key = g_new0 (CncKey, 1);
		if (dsn)
			key->dsn = g_strdup (dsn);
		if (user)
			key->user = g_strdup (user);
		if (password)
			key->pass = g_strdup (password);
		g_hash_table_insert (cnc_hash, key, cnc);
	}

	return cnc;
}

/*
 * execSQL function
 */
static GnmFuncHelp const help_execSQL[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=EXECSQL\n"
	   "@SYNTAX=EXECSQL(dsn,username,password,sql)\n"
	   "@DESCRIPTION="
	   "The EXECSQL function lets you execute a command in a"
	   " database server, and show the results returned in"
	   " current sheet. It uses libgda as the means for"
	   " accessing the databases.\n"
	   "For using it, you need first to set up a libgda data source."
	   "\n"
	   "@EXAMPLES=\n"
	   "To get all the data from the table \"Customers\" present"
	   " in the \"mydatasource\" GDA data source, you would use:\n"
	   "EXECSQL(\"mydatasource\",\"username\",\"password\",\"SELECT * FROM customers\")\n"
	   "@SEEALSO=READDBTABLE")
    },
    { GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_execSQL (GnmFuncEvalInfo *ei, GnmValue const  * const *args)
{
	GnmValue*         ret;
	gchar*         dsn_name;
	gchar*         user_name;
	gchar*         password;
	gchar*         sql;
	GdaConnection* cnc;
	GdaDataModel*  recset;
	GList*         recset_list;
	GdaCommand*    cmd;
	GError*        error = NULL;

	dsn_name = value_get_as_string (args[0]);
	user_name = value_get_as_string (args[1]);
	password = value_get_as_string (args[2]);
	sql = value_get_as_string (args[3]);
	if (!dsn_name || !sql)
		return value_new_error (ei->pos, _("Format: execSQL(dsn,user,password,sql)"));

	cnc = open_connection (dsn_name, user_name, password, GDA_CONNECTION_OPTIONS_READ_ONLY);
	if (!GDA_IS_CONNECTION (cnc)) {
		return value_new_error(ei->pos, _("Error: could not open connection to %s"));
	}

	/* execute command */
	cmd = gda_command_new (sql, GDA_COMMAND_TYPE_SQL, 0);
	recset_list = gda_connection_execute_command (cnc, cmd, NULL, &error);
	gda_command_free (cmd);
	if (recset_list) {
		recset = (GdaDataModel *) recset_list->data;
		if (!GDA_IS_DATA_MODEL (recset))
			ret = value_new_error (ei->pos, _("Error: no recordsets were returned"));
		else
			ret = display_recordset (recset, ei);

		go_list_free_custom (recset_list, g_object_unref);
	} else {
		if (error) {
			ret = value_new_error (ei->pos, error->message);
			g_error_free (error);
		} else
			ret = value_new_empty ();
	}

	return ret;
}

/*
 * readDBTable function
 */
static GnmFuncHelp const help_readDBTable[] = {
    { GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=READDBTABLE\n"
	   "@SYNTAX=READDBTABLE(dsn,username,password,table)\n"
	   "@DESCRIPTION="
	   "The READDBTABLE function lets you get the contents of"
	   " a table, as stored in a database. "
	   "For using it, you need first to set up a libgda data source."
	   "\n"
	   "Note that this function returns all the rows in the given"
	   " table. If you want to get data from more than one table"
	   " or want a more precise selection (conditions), use the"
	   " EXECSQL function."
	   "\n"
	   "@EXAMPLES=\n"
	   "To get all the data from the table \"Customers\" present"
	   " in the \"mydatasource\" GDA data source, you would use:\n"
	   "READDBTABLE(\"mydatasource\",\"username\",\"password\",\"customers\")\n"
	   "@SEEALSO=EXECSQL")
    },
    { GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_readDBTable (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	GnmValue*         ret;
	gchar*         dsn_name;
	gchar*         user_name;
	gchar*         password;
	gchar*         table;
	GdaConnection* cnc;
	GdaDataModel*  recset;
	GList*         recset_list;
	GdaCommand*    cmd;
	GError*        error = NULL;

	dsn_name = value_get_as_string (args[0]);
	user_name = value_get_as_string (args[1]);
	password = value_get_as_string (args[2]);
	table = value_get_as_string (args[3]);
	if (!dsn_name || !table)
		return value_new_error (ei->pos, _("Format: readDBTable(dsn,user,password,table)"));

	cnc = open_connection (dsn_name, user_name, password, GDA_CONNECTION_OPTIONS_READ_ONLY);
	if (!GDA_IS_CONNECTION (cnc)) {
		return value_new_error(ei->pos, _("Error: could not open connection to %s"));
	}

	/* execute command */
	cmd = gda_command_new (table, GDA_COMMAND_TYPE_TABLE, 0);
	recset_list = gda_connection_execute_command (cnc, cmd, NULL, &error);
	gda_command_free (cmd);
	if (recset_list) {
		recset = (GdaDataModel *) recset_list->data;
		if (!GDA_IS_DATA_MODEL (recset))
			ret = value_new_error (ei->pos, _("Error: no recordsets were returned"));
		else
			ret = display_recordset (recset, ei);

		go_list_free_custom (recset_list, g_object_unref);
	} else {
		if (error) {
			ret = value_new_error (ei->pos, error->message);
			g_error_free (error);
		} else
			ret = value_new_empty ();
	}

	return ret;
}

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	/* close the connection pool */
	if (GDA_IS_CLIENT (connection_pool)) {
		g_object_unref (G_OBJECT (connection_pool));
		connection_pool = NULL;
	}
}

GnmFuncDescriptor gdaif_functions[] = {
	{
		"execSQL",	"ssss", "dsn,username,password,sql",
		help_execSQL, &gnumeric_execSQL, NULL, NULL, NULL
	},
	{
		"readDBTable", "ssss", "dsn,username,password,table",
		help_readDBTable, &gnumeric_readDBTable, NULL, NULL, NULL
	},
	{NULL}
};
