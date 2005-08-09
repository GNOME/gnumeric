/* Interface Gnumeric to Databases
 * Copyright (C) 1998,1999 Michael Lausch
 * Copyright (C) 2000-2002 Rodrigo Moya
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
#ifdef HAVE_LIBGNOMEDB
#include <libgnomedb/gnome-db-login-dialog.h>
#include <libgnomedb/gnome-db-login.h>
#endif

#include "func.h"
#include "expr.h"
#include "value.h"
#include "gnm-i18n.h"
#include <goffice/app/go-plugin.h>
#include <goffice/app/error-info.h>
#include <gnm-plugin.h>

GNM_PLUGIN_MODULE_HEADER;

static GdaClient* connection_pool = NULL;

static GnmValue *
display_recordset (GdaDataModel *recset, FunctionEvalInfo *ei)
{
	GnmValue* array = NULL;
	gint   col;
	gint   row;
	gint   fieldcount = 0;
	gint   rowcount = 0;

	g_return_val_if_fail (GDA_IS_DATA_MODEL (recset), NULL);

	fieldcount = gda_data_model_get_n_columns (GDA_DATA_MODEL (recset));
	rowcount = gda_data_model_get_n_rows (GDA_DATA_MODEL (recset));

	/* convert the GdaDataModel in an array */
	if (rowcount <= 0)
		return value_new_empty ();

	if (rowcount >= SHEET_MAX_ROWS)
		return value_new_error (ei->pos, _("Too much data returned"));

	array = value_new_array_empty (fieldcount, rowcount);
	for (row = 0; row < rowcount; row++) {
		for (col = 0; col < fieldcount; col++) {
			gchar *str;
			const GdaValue *value;

			value = gda_data_model_get_value_at (GDA_DATA_MODEL (recset),
							     col, row);
			str = gda_value_stringify ((GdaValue *) value);
			value_array_set (array,
					 col,
					 row,
					 value_new_string(str));

			g_free (str);
		}
	}

	return array;
}

static GdaConnection *
open_connection (const gchar *dsn, const gchar *user, const gchar *password, GdaConnectionOptions options)
{
	GdaConnection *cnc;
	gchar *real_dsn, *real_user, *real_password;
#ifdef HAVE_LIBGNOMEDB
	GtkWidget *dialog, *login;
#endif
	GError *error = NULL;

	/* initialize connection pool if first time */
	if (!GDA_IS_CLIENT (connection_pool)) {
		connection_pool = gda_client_new ();
		if (!connection_pool)
			return NULL;
	}

#ifdef HAVE_LIBGNOMEDB
	dialog = gnome_db_login_dialog_new (_("Database Connection"));
	login = gnome_db_login_dialog_get_login_widget (GNOME_DB_LOGIN_DIALOG (dialog));

	gnome_db_login_set_dsn (GNOME_DB_LOGIN (login), dsn);
	gnome_db_login_set_username (GNOME_DB_LOGIN (login), user);
	gnome_db_login_set_password (GNOME_DB_LOGIN (login), password);

	if (gnome_db_login_dialog_run (GNOME_DB_LOGIN_DIALOG (dialog))) {
		real_dsn = g_strdup (gnome_db_login_get_dsn (GNOME_DB_LOGIN (login)));
		real_user = g_strdup (gnome_db_login_get_username (GNOME_DB_LOGIN (login)));
		real_password = g_strdup (gnome_db_login_get_password (GNOME_DB_LOGIN (login)));

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
gnumeric_execSQL (FunctionEvalInfo *ei, GnmValue **args)
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

		g_list_foreach (recset_list, (GFunc) g_object_unref, NULL);
		g_list_free (recset_list);
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
gnumeric_readDBTable (FunctionEvalInfo *ei, GnmValue **args)
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

		g_list_foreach (recset_list, (GFunc) g_object_unref, NULL);
		g_list_free (recset_list);
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
	{"execSQL", "ssss", "dsn,username,password,sql", help_execSQL, &gnumeric_execSQL, NULL, NULL, NULL },
	{"readDBTable", "ssss", "dsn,username,password,table", help_readDBTable, &gnumeric_readDBTable, NULL, NULL, NULL },
	{NULL}
};
