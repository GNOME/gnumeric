/* Interface Gnumeric to Databases
 * Copyright (C) 1998,1999 Michael Lausch
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


#include <gnome.h>
#include <glib.h>

#include <gda-command.h>
#include <gda-recordset.h>
#include <gda-error.h>
#include <gda-connection.h>
#include <libgnorba/gnorba.h>


#include "../../src/gnumeric.h"
#include "../../src/func.h"
#include "../../src/plugin.h"
#include "../../src/expr.h"

char* help_execSQL = "execSQL: Execute SQL statement and display values";

static gchar*
display_recordset (Gda_Recordset* rs, Sheet* sheet, gint col, gint row)
{
	gint i;
	Cell*   cell;
	gchar*  retval;
	Gda_Field* field;

	sheet = workbook_get_current_sheet(current_workbook);
	field = gda_recordset_field_idx(rs, 0);
	retval = gda_field_name(field);

	for ( i = 1; i < gda_recordset_rowsize(rs); i++) {
		gchar*  field_name;

		field = gda_recordset_field_idx(rs, i);
		field_name = gda_field_name(field);
		cell = sheet_cell_fetch(sheet, col+i, row);
		cell_set_text(cell, field_name);
	}

	while(1) {
		row++;
		gda_recordset_move(rs, 1, 0);
		if (gda_recordset_eof(rs))
			break;
		for (i = 0; i < gda_recordset_rowsize(rs); i++) {
			gchar value_bfr[128];

			field = gda_recordset_field_idx(rs, i);
			if (!gda_field_isnull(field)) {
				gda_stringify_value(value_bfr, sizeof(value_bfr), field);
				cell = sheet_cell_fetch(sheet, col+i, row);
				g_print("Setting cell (%d/%d) to '%s'\n", col+i, row, value_bfr);
				cell_set_text(cell, value_bfr);
			}
		}
	}
	return retval;
}

static Value*
execSQL (void* sheet, GList* expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value* result;
	Gda_Connection* cnc;
	Gda_Command*    cmd;
	Gda_Recordset*  rs;
	gulong          reccount;
	ExprTree*       node;
	gint            parm_idx;
	gchar*          db_name;
	gchar*          dsn;
	gchar*          user;
	gchar*          password;
	GString*        stmt;
	gint rc;
	gchar  bfr[128];
	gchar* provider;


	if (g_list_length(expr_node_list) < 4) {
		*error_string = "Format: Databasource, Username, Password, Statement with cell references";
		return NULL;
	}
	stmt = g_string_new("");

	node = expr_node_list->data;
	g_print("1. node->oper = %d\n", node->oper);
	db_name = expr_decode_tree(node, sheet, eval_col, eval_row);
	db_name[strlen(db_name)-1] = '\0';
	g_print("1. node: value = '%s'\n", &db_name[1]);

	expr_node_list = g_list_next(expr_node_list);
	node = expr_node_list->data;
	g_print("2. node->oper = %d\n", node->oper);
	user = expr_decode_tree(node, sheet, eval_col, eval_row);
	user[strlen(user)-1] = '\0';
	g_print("2. node: value = '%s'\n", &user[1]);

	expr_node_list = g_list_next(expr_node_list);
	node = expr_node_list->data;
	g_print("3.node->oper = %d\n", node->oper);
	password = expr_decode_tree(node, sheet, eval_col, eval_row);
	password[strlen(password)-1] = '\0';
	g_print("3. node: value = '%s'\n", &password[1]);

	expr_node_list = g_list_next(expr_node_list);
	parm_idx = 0;
	cmd = gda_command_new();
	while (expr_node_list) {
		node = expr_node_list->data;

		g_print("parameter_node %d: oper = %d\n", parm_idx, node->oper);

		if (node->oper == OPER_CONSTANT) {
			if (node->u.constant->type != VALUE_STRING) {
				g_free(user);
				g_free(password);
				g_free(db_name);
				g_string_free(stmt, 1);
				*error_string = "Statement is no string\n";
				return NULL;
			}
			g_string_append(stmt, node->u.constant->v.str->str);
		}
		if (node->oper == OPER_VAR) {
			GDA_Value* gda_value;
			Cell*      parameter_cell;
			gint       cell_row;
			gint       cell_col;

			g_string_append(stmt, " ? ");
			g_print("cellref->row = %d, relative = %d\n",
				node->u.ref.row, node->u.ref.row_relative);
			g_print("cellreg->col = %d, relative = %d\n",
				node->u.ref.col, node->u.ref.col_relative);
			if (node->u.ref.row_relative)
				cell_row = eval_row + node->u.ref.row;
			else
				cell_row = node->u.ref.row;
			if (node->u.ref.col_relative)
				cell_col = eval_col + node->u.ref.col;
			else
				cell_col = node->u.ref.col;

			parameter_cell = sheet_cell_get(sheet, cell_col, cell_row);

			gda_value = GDA_Value__alloc();
			gda_value->_d = GDA_TypeVarchar;
			gda_value->_u.lvc = parameter_cell->text->str;
			gda_command_create_parameter(cmd, "param from gnumeric",
						     GDA_PARAM_IN, gda_value);
		}
		expr_node_list = g_list_next(expr_node_list);
	}

	g_snprintf(bfr, sizeof(bfr), "/gdalib/%s/Provider", &db_name[1]);
	provider = gnome_config_get_string(bfr);
	cnc = gda_connection_new(gnome_CORBA_ORB());
	gda_connection_set_provider(cnc, provider);
	g_snprintf(bfr, sizeof(bfr), "/gdalib/%s/DSN", &db_name[1]);
	dsn = gnome_config_get_string(bfr);

	rc = gda_connection_open(cnc, dsn, &user[1], &password[1]);
	if (rc != 0) {
		Gda_Error* e;
		GList* errors;
		GList* ptr;

		errors = gda_connection_get_errors(cnc);
		ptr = errors;
		while(ptr)
		{
			e = ptr->data;
			g_print("Connection::open Error: '%s'/'%s'\n", e->description, e->native);
			ptr = g_list_next(ptr);
		}
	}
	gda_command_set_connection(cmd, cnc);
	g_print("statemnt is '%s'\n", stmt->str);
	gda_command_set_text(cmd, stmt->str);
	rs = gda_command_execute(cmd, &reccount, 0);
	if (!rs) {
		Gda_Error* e;
		GList* errors;
		GList* ptr;

		errors = gda_connection_get_errors(cnc);
		ptr = errors;
		while(ptr) {
			e = ptr->data;
			g_print("Connection::open Error: '%s'/'%s'\n", e->description, e->native);
			ptr = g_list_next(ptr);
		}
		*error_string = "GDA Error";
		result = NULL;
	} else {
		result = value_new_string (display_recordset(rs, sheet, eval_col, eval_row));
		gda_recordset_free(rs);
	}
	gda_command_free(cmd);
	gda_connection_close(cnc);
	gda_connection_free(cnc);

	return result;
}

static FunctionDefinition plugin_functionp[] ={
	{ "execSQL",   "", "", &help_execSQL, execSQL, NULL},
	{ NULL, NULL}
};

static int
can_unload (PluginData *pd)
{
	Symbol *sym;

	sym = symbol_lookup (global_symbol_table, "execSQL");
	return sym->ref_count <= 1;
}

static void
cleanup_plugin (PluginData *pd)
{
	Symbol *sym;

	g_free (pd->title);
	sym = symbol_lookup (global_symbol_table, "execSQL");
	if (sym)
		symbol_unref(sym);
}


PluginInitResult
init_plugin (CommandContext *context, PluginData* pd)
{
	g_print("plugin-gda: init_plugin called\n");

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	install_symbols(plugin_functionp, "GDA Plugin");
	pd->can_unload = can_unload;
	pd->cleanup_plugin = cleanup_plugin;
	pd->title = g_strdup("Database Access");

	return PLUGIN_OK;
}
