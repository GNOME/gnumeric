/* Interface Gnumeric to Databases
 * Copyright (C) 1998,1999 Michael Lausch
 * Copyright (C) 2000 Rodrigo Moya
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
#include <gda-client.h>

#include "func.h"
#include "plugin.h"
#include "plugin-util.h"
#include "error-info.h"
#include "module-plugin-defs.h"
#include "expr.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

static GdaConnectionPool* connection_pool = NULL;

static Value *
display_recordset (GdaRecordset *recset)
{
	gint       position;
	Value*     array = NULL;
	gint       col;
	gint       cnt;
	gint       fieldcount = 0;
	GPtrArray* data_loaded = NULL; // array for rows

	g_return_val_if_fail(GDA_IS_RECORDSET(recset), NULL);

	data_loaded = g_ptr_array_new();
	
	/* traverse recordset */
	position = gda_recordset_move(recset, 1, 0);
	while (position != GDA_RECORDSET_INVALID_POSITION && !gda_recordset_eof(recset)) {
		GPtrArray* row = NULL;

		fieldcount = gda_recordset_rowsize(recset);
		for (col = 0; col < fieldcount; col++) {
			GdaField* field = gda_recordset_field_idx(recset, col);
			if (field) {
				gchar* value;
				
				value = gda_stringify_value(0, 0, field);
				g_warning("adding %s", value);
				if (!row) row = g_ptr_array_new();
				g_ptr_array_add(row, (gpointer) value);
			}
		}
		if (row) g_ptr_array_add(data_loaded, (gpointer) row);
		position = gda_recordset_move(recset, 1, 0);
	}
	
	/* if there's data, convert to an Array */
	if (data_loaded->len > 0) {
		array = value_new_array_empty(fieldcount, data_loaded->len);
		for (cnt = 0; cnt < data_loaded->len; cnt++) {
			GPtrArray* row = (GPtrArray *) g_ptr_array_index(data_loaded, cnt);
			if (row) {
				for (col = 0; col < fieldcount; col++) {
					value_array_set(array,
					                col,
					                cnt,
					                value_new_string(g_ptr_array_index(row, col)));
				}
			}
		}
	}
	else array = value_new_array_empty(1, 1);

	/* free all data */
	for (cnt = 0; cnt < data_loaded->len; cnt++) {
		GPtrArray* tmp = (GPtrArray *) g_ptr_array_index(data_loaded, cnt);
		if (tmp) {
			/* free each row */
			//for (col = 0; col < tmp->len; col++) {
			//	gchar* str = (gchar *) g_ptr_array_index(data_loaded, col);
			//	if (str) g_free((gpointer) str);
			//}
			g_ptr_array_free(tmp, FALSE);
		}
	}
	g_ptr_array_free(data_loaded, FALSE);

	return array;
}

/*
 * execSQL function
 */
static char *help_execSQL = {
	N_("@FUNCTION=EXECSQL\n"
	   "@SYNTAX=EXECSQL(i)\n"
	   "@DESCRIPTION="
	   "The EXECSQL function lets you execute a command in a\n"
	   " database server, and show the results returned in\n"
	   " current sheet\n"
	   ""
	   "\n"
	   "@EXAMPLES=\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_execSQL (FunctionEvalInfo *ei, Value **args)
{
	Value*          ret;
	gchar*          dsn_name;
	gchar*          user_name;
	gchar*          password;
	gchar*          sql;
	GdaConnection* cnc;
	GdaRecordset*  recset;
	glong           reccount;
	
	dsn_name = value_get_as_string(args[0]);
	user_name = value_get_as_string(args[1]);
	password = value_get_as_string(args[2]);
	sql = value_get_as_string(args[3]);
	if (!dsn_name || !sql)
		return value_new_error(ei->pos, _("Format: execSQL(dsn,user,password,sql)"));

	/* initialize connection pool if first time */
	if (!GDA_IS_CONNECTION_POOL(connection_pool)) {
		connection_pool = gda_connection_pool_new();
		if (!connection_pool) {
			return value_new_error(ei->pos, _("Error: could not initialize connection pool"));
		}
	}
	cnc = gda_connection_pool_open_connection(connection_pool, dsn_name, user_name, password);
	if (!GDA_IS_CONNECTION(cnc)) {
		return value_new_error(ei->pos, _("Error: could not open connection to %s"));
	}
	
	/* execute command */
	recset = gda_connection_execute(cnc, sql, &reccount, 0);
	if (GDA_IS_RECORDSET(recset)) {
		ret = display_recordset(recset);
		gda_recordset_free(recset);
	}
	else ret = value_new_empty();

	return ret;
}

void
plugin_cleanup (void)
{
	/* close the connection pool */
	if (GDA_IS_CONNECTION_POOL(connection_pool)) {
		gda_connection_pool_close_all(connection_pool);
		gda_connection_pool_free(connection_pool);
		connection_pool = NULL;
	}
}

ModulePluginFunctionInfo gdaif_functions[] = {
	{"execSQL", "ssss", "dsn,username,password,sql", &help_execSQL, &gnumeric_execSQL, NULL},
	{NULL}
};
