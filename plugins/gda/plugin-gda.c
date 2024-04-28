/* Interface Gnumeric to Databases
 * Copyright (C) 1998,1999 Michael Lausch
 * Copyright (C) 2000-2002 Rodrigo Moya
 * Copyright (C) 2006 Vivien Malerba
 * Copyright (C) 2013 Dmitry Matveev
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
 * License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <libgda/libgda.h>
#include <libgda-ui/libgda-ui.h>
#include <libgda-ui/gdaui-login.h>
#include <sql-parser/gda-sql-parser.h>
#include <string.h>

#include <func.h>
#include <expr.h>
#include <value.h>
#include <workbook.h>
#include <sheet.h>
#include <gnm-i18n.h>
#include <gui-util.h>
#include <goffice/goffice.h>
#include <gnm-plugin.h>
#include <dead-kittens.h>

GNM_PLUGIN_MODULE_HEADER;

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

	t = G_VALUE_TYPE (gval);
	if (t == GDA_TYPE_NULL)
		return value_new_empty ();

	if (t == GDA_TYPE_SHORT)
		return value_new_int (gda_value_get_short (gval));
	if (t == GDA_TYPE_USHORT)
		return value_new_int (gda_value_get_ushort (gval));
	if (t ==  G_TYPE_DATE) {
		res = value_new_int (go_date_g_to_serial (
			(GDate const *) g_value_get_boxed (gval), date_conv));
		value_set_fmt (res, go_format_default_date ());
		return res;
	}

	if (t == GDA_TYPE_TIME) {
		GdaTime const *time = gda_value_get_time (gval);
		res = value_new_float ( (g_date_time_get_hour (time) +
					 (g_date_time_get_minute (time) +
					  g_date_time_get_second (time) / 60.) / 60.) / 24.),
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
		GValue str = G_VALUE_INIT;
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

	date_conv = sheet_date_conv (ei->pos->sheet);
	array = value_new_array_empty (fieldcount, rowcount);
	for (row = 0; row < rowcount; row++) {
		for (col = 0; col < fieldcount; col++) {
			const GValue *cv;
			cv = gda_data_model_get_value_at (recset, col, row, NULL);
			if (!cv) {
				value_release (array);
				return value_new_error (ei->pos, _("Can't obtain data"));
			}
			value_array_set (array, col, row,
					 gnm_value_new_from_gda (cv, date_conv));
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

static void
cnc_key_fill (CncKey *key, const gchar *dsn, const gchar *user, const gchar *pass)
{
	key->dsn  = g_strdup (dsn  ? dsn  : "");
	key->user = g_strdup (user ? user : "");
	key->pass = g_strdup (pass ? pass : "");
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

static gchar *
build_gdadsn_auth_info (const gchar *user, const gchar *pass)
{
	gchar *result = NULL;

	if (user && strlen (user) > 0 && pass && strlen (pass) > 0) {
		gchar *tmp1 = gda_rfc1738_encode (user);
		gchar *tmp2 = gda_rfc1738_encode (pass);
		result = g_strdup_printf ("USERNAME=%s;PASSWORD=%s", tmp1, tmp2);
		g_free (tmp1);
		g_free (tmp2);
	}

	return result;
}

static gboolean
query_connection_info (const gchar *dsn, const gchar *user, const gchar *password,
		       gchar **chosen_dsn, gchar **auth_string)
{
	GtkWidget  *dialog, *login, *box;
	GdaDsnInfo *existing;
	gboolean    retval = FALSE;

	/* FIXME: pass a pointer to parent window */
	dialog = gtk_dialog_new_with_buttons(_("Database Connection"),
					     NULL,
					     GTK_DIALOG_MODAL,
					     GNM_STOCK_OK, GTK_RESPONSE_ACCEPT,
					     GNM_STOCK_CANCEL, GTK_RESPONSE_REJECT,
					     NULL);

	login = gdaui_login_new (NULL);

	box = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_box_pack_start (GTK_BOX (box), login, TRUE, TRUE, 0);
	gtk_widget_show (login);

	existing = gda_config_get_dsn_info (dsn);
	if (existing) {
		GdaDsnInfo supplied = *existing;
		gchar *auth = build_gdadsn_auth_info (user, password);

		if (auth)
			supplied.auth_string = auth;

		gdaui_login_set_connection_information (GDAUI_LOGIN(login), &supplied);
		g_free (auth);
	}

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		const GdaDsnInfo *info = gdaui_login_get_connection_information (GDAUI_LOGIN (login));
		if (info) {
			if (info->name)
				*chosen_dsn = g_strdup (info->name);
			if (info->auth_string)
				*auth_string = g_strdup (info->auth_string);
			retval = TRUE;
		}
	}

	gtk_widget_destroy (dialog);
	return retval;
}

static GdaConnection *
open_connection (const gchar *dsn, const gchar *user, const gchar *password, GdaConnectionOptions options)
{
	GdaConnection *cnc = NULL;
	GError *error = NULL;

	/* initialize connection pool if first time */
	if (!libgda_init_done) {
		gdaui_init ();
		libgda_init_done = TRUE;
	}

	/* try to find a cnc object if we already have one */
	if (!cnc_hash)
		cnc_hash = g_hash_table_new_full ((GHashFunc) cnc_key_hash_func,
						  (GEqualFunc) cnc_key_equal_func,
						  (GDestroyNotify) cnc_key_free,
						  (GDestroyNotify) g_object_unref);
	else {
		CncKey *key = g_new0 (CncKey, 1);
		cnc_key_fill (key, dsn, user, password);
		cnc = g_hash_table_lookup (cnc_hash, key);
		cnc_key_free (key);
	}

	if (!cnc) {
		gchar *chosen_dsn = NULL, *auth_string = NULL;

		if (query_connection_info (dsn, user, password, &chosen_dsn, &auth_string) && chosen_dsn) {
			cnc = gda_connection_open_from_dsn_name (chosen_dsn, auth_string, options, &error);
			if (!cnc) {
				g_warning ("Libgda error: %s\n", error->message);
				g_error_free (error);
			} else {
				gchar  *real_user = NULL, *real_pass = NULL;
				CncKey *key = g_new0 (CncKey, 1);

				if (auth_string) {
					GdaQuarkList *ql = gda_quark_list_new_from_string (auth_string);
					real_user = g_strdup (gda_quark_list_find (ql, "USERNAME"));
					real_pass = g_strdup (gda_quark_list_find (ql, "PASSWORD"));
					gda_quark_list_free (ql);
				}

				cnc_key_fill (key, chosen_dsn, real_user, real_pass);
				g_hash_table_insert (cnc_hash, key, cnc);
				g_free (real_user);
				g_free (real_pass);
			}
		}

		g_free (chosen_dsn);
		g_free (auth_string);
	}

	return cnc;
}

static GnmValue *
conn_error (GnmFuncEvalInfo *ei, const gchar *dsn_name)
{
	gchar    *tmp = g_strdup_printf (_("Error: could not open connection to %s"), dsn_name);
	GnmValue *ret = value_new_error(ei->pos, tmp);
	g_free (tmp);
	return ret;
}

/*
 * execSQL function
 */
static GnmFuncHelp const help_execSQL[] = {
	{ GNM_FUNC_HELP_NAME, F_("EXECSQL:result of executing @{sql} in the "
				 "libgda data source @{dsn}") },
	{ GNM_FUNC_HELP_ARG, F_("dsn:libgda data source") },
	{ GNM_FUNC_HELP_ARG, F_("username:user name to access @{dsn}") },
	{ GNM_FUNC_HELP_ARG, F_("password:password to access @{dsn} as @{username}") },
	{ GNM_FUNC_HELP_ARG, F_("sql:SQL command") },
	{ GNM_FUNC_HELP_NOTE, F_("Before using EXECSQL, you need to set up a libgda "
				 "data source.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=EXECSQL(\"mydatasource\",\"username\",\"password\""
	  ",\"SELECT * FROM customers\")" },
	{ GNM_FUNC_HELP_SEEALSO, "READDBTABLE" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_execSQL (GnmFuncEvalInfo *ei, GnmValue const  * const *args)
{
	GnmValue*      ret;
	gchar*         dsn_name;
	gchar*         user_name;
	gchar*         password;
	gchar*         sql;
	GdaConnection* cnc;
	GdaDataModel*  recset;
	GdaStatement*  stmt;
	GError*        error = NULL;
	GdaSqlParser  *parser;
	const gchar   *remain;

	dsn_name = value_get_as_string (args[0]);
	user_name = value_get_as_string (args[1]);
	password = value_get_as_string (args[2]);
	sql = value_get_as_string (args[3]);
	if (!dsn_name || !sql)
		return value_new_error (ei->pos, _("Format: execSQL(dsn,user,password,sql)"));

	cnc = open_connection (dsn_name, user_name, password, GDA_CONNECTION_OPTIONS_READ_ONLY);
	if (!GDA_IS_CONNECTION (cnc))
		return conn_error (ei, dsn_name);

	/* execute command */
	parser = gda_connection_create_parser (cnc);
	if (!parser)
		parser = gda_sql_parser_new ();
	stmt = gda_sql_parser_parse_string (parser, sql, &remain, &error);
	g_object_unref (parser);
	if (!stmt) {
		ret = value_new_error (ei->pos, error->message);
		g_error_free (error);
		return ret;
	}

	if (remain) {
		g_object_unref (stmt);
		return value_new_error (ei->pos, _("More than one statement in SQL string"));
	}

	recset = gda_connection_statement_execute_select (cnc, stmt, NULL, &error);
	g_object_unref (stmt);
	if (recset) {
		ret = display_recordset (recset, ei);
		g_object_unref (recset);
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
	{ GNM_FUNC_HELP_NAME, F_("READDBTABLE:all rows of the table @{table} in @{dsn}") },
	{ GNM_FUNC_HELP_ARG, F_("dsn:libgda data source") },
	{ GNM_FUNC_HELP_ARG, F_("username:user name to access @{dsn}") },
	{ GNM_FUNC_HELP_ARG, F_("password:password to access @{dsn} as @{username}") },
	{ GNM_FUNC_HELP_ARG, F_("table:SQL table to retrieve") },
	{ GNM_FUNC_HELP_NOTE, F_("Before using EXECSQL, you need to set up a libgda "
				 "data source.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=READDBTABLE(\"mydatasource\",\"username\","
	  "\"password\",\"customers\")" },
	{ GNM_FUNC_HELP_SEEALSO, "EXECSQL" },
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
	GdaStatement*  stmt;
	GError*        error = NULL;
	GdaSqlParser  *parser;
	const gchar   *remain;
	gchar         *sql;

	dsn_name = value_get_as_string (args[0]);
	user_name = value_get_as_string (args[1]);
	password = value_get_as_string (args[2]);
	table = value_get_as_string (args[3]);
	if (!dsn_name || !table)
		return value_new_error (ei->pos, _("Format: readDBTable(dsn,user,password,table)"));

	cnc = open_connection (dsn_name, user_name, password, GDA_CONNECTION_OPTIONS_READ_ONLY);
	if (!GDA_IS_CONNECTION (cnc))
		return conn_error (ei, dsn_name);

	/* execute command */
	parser = gda_connection_create_parser (cnc);
	if (!parser)
		parser = gda_sql_parser_new ();
	sql = g_strdup_printf ("SELECT * FROM %s", table); /* FIXME: create statement from API */
	stmt = gda_sql_parser_parse_string (parser, sql, &remain, &error);
	g_free (sql);
	g_object_unref (parser);
	if (!stmt) {
		ret = value_new_error (ei->pos, error->message);
		g_error_free (error);
		return ret;
	}

	if (remain) {
		g_object_unref (stmt);
		return value_new_error (ei->pos, _("More than one statement in SQL string"));
	}

	recset = gda_connection_statement_execute_select (cnc, stmt, NULL, &error);
	g_object_unref (stmt);

	if (recset) {
		ret = display_recordset (recset, ei);
		g_object_unref (recset);
	} else {
		if (error) {
			ret = value_new_error (ei->pos, error->message);
			g_error_free (error);
		} else
			ret = value_new_empty ();
	}

	return ret;
}

static void
view_data_sources (GnmAction const *action, WorkbookControl *wbc)
{
	char *argv[2];

	argv[0] = "/usr/bin/env gda-control-center-6.0";
	argv[1] = NULL;
	if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL)) {
		go_gtk_notice_dialog (wbcg_toplevel (WBC_GTK (wbc)), GTK_MESSAGE_INFO,
				      _("Could not run GNOME database configuration tool ('%s')"),
				      argv[0]);
	}
	g_free (argv[0]);
}

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	/* nothing to do */
	g_hash_table_destroy (cnc_hash);
	cnc_hash = NULL;
}

GnmModulePluginUIActions const gdaif_ui_actions[] = {
	{"ViewDataSources", view_data_sources},
	{NULL}
};

GnmFuncDescriptor gdaif_functions[] = {
	{ "execSQL",     "ssss", help_execSQL,     gnumeric_execSQL, 	 NULL },
	{ "readDBTable", "ssss", help_readDBTable, gnumeric_readDBTable, NULL },
	{NULL}
};
