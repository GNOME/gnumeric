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

#include "func.h"
#include "plugin.h"
#include "plugin-util.h"
#include "error-info.h"
#include "module-plugin-defs.h"
#include "expr.h"
#include "value.h"

#include <libgnome/gnome-i18n.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static GdaClient* connection_pool = NULL;

static Value *
display_recordset (GdaDataModel *recset)
{
	Value* array = NULL;
	gint   col;
	gint   row;
	gint   fieldcount = 0;
	gint   rowcount = 0;

	g_return_val_if_fail (GDA_IS_DATA_MODEL (recset), NULL);

	fieldcount = gda_data_model_get_n_columns (GDA_DATA_MODEL (recset));
	rowcount = gda_data_model_get_n_rows (GDA_DATA_MODEL (recset));

	/* convert the GdaDataModel in an array */
	if (rowcount > 0) {
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
	}
	else
		array = value_new_array_empty (1, 1);

	return array;
}

/*
 * execSQL function
 */
static char const *help_execSQL = {
	N_("@FUNCTION=EXECSQL\n"
	   "@SYNTAX=EXECSQL(i)\n"
	   "@DESCRIPTION="
	   "The EXECSQL function lets you execute a command in a\n"
	   " database server, and show the results returned in\n"
	   " current sheet. It uses libgda as the means for\n"
	   " accessing the databases.\n"
	   ""
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_execSQL (FunctionEvalInfo *ei, Value **args)
{
	Value*         ret;
	gchar*         dsn_name;
	gchar*         user_name;
	gchar*         password;
	gchar*         sql;
	GdaConnection* cnc;
	GdaDataModel*  recset;
	GList*         recset_list;
	GdaCommand*    cmd;

	dsn_name = value_get_as_string (args[0]);
	user_name = value_get_as_string (args[1]);
	password = value_get_as_string (args[2]);
	sql = value_get_as_string (args[3]);
	if (!dsn_name || !sql)
		return value_new_error (ei->pos, _("Format: execSQL(dsn,user,password,sql)"));

	/* initialize connection pool if first time */
	if (!GDA_IS_CLIENT (connection_pool)) {
		connection_pool = gda_client_new ();
		if (!connection_pool) {
			return value_new_error (ei->pos, _("Error: could not initialize connection pool"));
		}
	}
	cnc = gda_client_open_connection (connection_pool, dsn_name, user_name, password);
	if (!GDA_IS_CONNECTION (cnc)) {
		return value_new_error(ei->pos, _("Error: could not open connection to %s"));
	}

	/* execute command */
	cmd = gda_command_new (sql, GDA_COMMAND_TYPE_SQL, 0);
	recset_list = gda_connection_execute_command (cnc, cmd, NULL);
	gda_command_free (cmd);
	if (recset_list) {
		recset = (GdaDataModel *) recset_list->data;
		if (!GDA_IS_DATA_MODEL (recset))
			ret = value_new_error (ei->pos, _("Error: no recordsets were returned"));
		else
			ret = display_recordset (recset);

		g_list_foreach (recset_list, (GFunc) g_object_unref, NULL);
		g_list_free (recset_list);
	}
	else
		ret = value_new_empty ();

	return ret;
}

void
plugin_cleanup (void)
{
	/* close the connection pool */
	if (GDA_IS_CLIENT (connection_pool)) {
		g_object_unref (G_OBJECT (connection_pool));
		connection_pool = NULL;
	}
}

ModulePluginFunctionInfo gdaif_functions[] = {
	{"execSQL", "ssss", "dsn,username,password,sql", &help_execSQL, &gnumeric_execSQL, NULL, NULL, NULL },
	{NULL}
};
