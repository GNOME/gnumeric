/* -*- mode: c; c-basic-offset: 8 -*- */
/**
 * boot.c: Gnome Basic support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 2000 HelixCode, Inc
 **/

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
/* Do not include c-t-y-p-e-.-h */

#include <glib.h>
#include <gnome.h>

#include <gb/libgb.h>
#include <gb/gb-mmap-lex.h>
#include <gbrun/libgbrun.h>
#include <libole2/ms-ole-vba.h>

#include "plugin.h"
#include "plugin-util.h"
#include "error-info.h"
#include "module-plugin-defs.h"
#include "expr.h"
#include "func.h"
#include "sheet.h"

#include "common.h"
#include "streams.h"
#include "excel-gb-application.h"
#include "excel-gb-worksheet.h"
#include "excel-gb-context.h"
#include "../excel/excel.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

#ifndef MAP_FAILED
/* Someone needs their head examining - BSD ? */
#	define MAP_FAILED ((void *)-1)
#endif

#define GB_PROJECT_KEY "GBRun::Project"

typedef struct {
	GBRunEvalContext *ec;
	GBRunProject     *proj;
} GBWorkbookData;

int gb_debug = 0;

void
plugin_cleanup_general (ErrorInfo **ret_error)
{
	*ret_error = NULL;
	gbrun_shutdown ();
	gb_shutdown ();
}

static Value *
generic_marshaller (FunctionEvalInfo *ei, GList *nodes)
{
	GSList  *args = NULL;
	GList   *l;
	GBValue *gb_ans;
	Value   *ans;
	GBWorkbookData *wd;

	ExcelGBApplication *application;
	ExcelGBWorksheet   *worksheet;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_def != NULL, NULL);

	wd = gnm_func_get_user_data (ei->func_def);
	g_return_val_if_fail (wd != NULL, NULL);

	{ /* Register Excel objects with GB */
		/* FIXME: the way these objects are exposed is really wierd */
		application = excel_gb_application_new (ei->pos->sheet->workbook);
		gbrun_project_register_module (
			wd->proj, GB_OBJECT (application));
		gbrun_project_register_object (
			wd->proj, "Workbook", GBRUN_OBJECT (application));

		worksheet = excel_gb_worksheet_new (ei->pos->sheet);
		gbrun_project_register_module (
			wd->proj, GB_OBJECT (worksheet));
		gbrun_project_register_object (
			wd->proj, "Worksheet", GBRUN_OBJECT (worksheet));
	}

	for (l = nodes; l; l = l->next) {
		/*
		 * TODO : When the translation mechanism is more complete
		 * this can be relaxed.  We do not need to require
		 * non emptiness, or scalarness.
		 */
		Value *v = gnm_expr_eval (l->data, ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);

		args = g_slist_prepend (args, value_to_gb (v));

		value_release (v);
	}

	args = g_slist_reverse (args);

	gb_ans = gbrun_project_invoke (wd->ec, wd->proj, gnm_func_get_name (ei->func_def), args);
	if (gb_ans)
		ans = gb_to_value (gb_ans);

	else {
		GBEvalContext *ec = GB_EVAL_CONTEXT (wd->ec);
		char *str;

		str = gb_eval_context_get_text (ec);
		if (str) {
			ans = value_new_error (ei->pos, str);

			g_free (str);
		} else
			ans = value_new_error (ei->pos, _("Unknown GB error"));

		gb_eval_context_reset (ec);
	}
	gb_value_destroy (gb_ans);

	while (args) {
		gb_value_destroy (args->data);
		args = g_slist_remove (args, args->data);
	}

	{ /* De-Register Excel objects with GB */
		gbrun_project_deregister_module (wd->proj, GB_OBJECT (application));
		gbrun_project_deregister_module (wd->proj, GB_OBJECT (worksheet));
		gb_object_unref (GB_OBJECT (application));
		gb_object_unref (GB_OBJECT (worksheet));
		gbrun_project_deregister_object (wd->proj, "Workbook");
		gbrun_project_deregister_object (wd->proj, "Worksheet");
	}

	return ans;
}

static void
register_vb_function (Workbook         *opt_workbook,
		      const char       *name,
		      GnmFuncGroup *cat,
		      GBWorkbookData   *wd)
{
	GnmFunc *fndef;

	/* FIXME: we need per workbook names */
	fndef = function_add_nodes (cat, name, "", NULL, NULL,
				    generic_marshaller, NULL);
	gnm_func_set_user_data (fndef, wd);
}

static gboolean
read_gb (gpointer            *jody_broke_the_context,
         Workbook            *wb,
         GBLexerStream       *proj_stream,
         GBRunStreamProvider *provider,
         gpointer             provider_data)
{
	GBWorkbookData   *wd;
	GBProject        *gb_proj;

	g_return_val_if_fail (proj_stream != NULL, FALSE);

	/* FIXME: leak for Morten */
	wd = g_new0 (GBWorkbookData, 1);

	wd->ec = GBRUN_EVAL_CONTEXT (
		excel_gb_context_new (
			"Gnumeric GB plugin", GBRUN_SEC_HARD));

	gb_proj = gb_project_new (GB_EVAL_CONTEXT (wd->ec), proj_stream);
	if (!gb_proj) {
		g_warning ("Failed to parse project file '%s'",
			   gbrun_eval_context_get_text (wd->ec));
		return FALSE;
  	}

	gtk_object_destroy (GTK_OBJECT (proj_stream));

	wd->proj = gbrun_project_new (wd->ec, gb_proj, provider, provider_data);

	if (!wd->proj) {
		g_warning ("Error creating project '%s'",
			   gbrun_eval_context_get_text (wd->ec));
		return FALSE;
	} else {
		{ /* 1. Register GB functions with Excel */
			GSList *fns, *f;
			GnmFuncGroup *group = gnm_func_group_fetch ("GnomeBasic");

			fns = gbrun_project_fn_names (wd->proj);

			/* FIXME: Argh, this means we need per workbook functions; ha, ha ha. */
			for (f = fns; f; f = f->next)
				register_vb_function (wb, f->data, cat, wd);

			g_slist_free (fns);
		}
	}

	/* Run the 'Main' function ( or whatever ) */
	if (!gbrun_project_execute (wd->ec, wd->proj)) {
		g_warning ("An exception occurred\n%s",
			   gb_eval_context_get_text (GB_EVAL_CONTEXT (wd->ec)));
		return FALSE;

	} else
		return TRUE;
}

static GBLexerStream *
stream_provider (GBRunEvalContext *ec,
		 const char       *name,
		 gpointer          user_data)
{
	MsOle         *f = user_data;
	MsOleStream   *s;
	MsOleVba      *vba;

	if (ms_ole_stream_open (&s, f, "_VBA_PROJECT_CUR/VBA", name, 'r')
	    != MS_OLE_ERR_OK) {
		g_warning ("Error opening %s", name);
		return NULL;
	}

	vba = ms_ole_vba_open (s);
	ms_ole_stream_close (&s);

	if (!vba) {
		g_warning  ("Error file '%s' is not a valid VBA stream", name);
		return NULL;
	}

	return gb_ole_stream_new (vba);
}

/**
 * read_ole2_gb:
 * @context:
 * @wb:
 * @f:
 *
 * The main function that organises all of the GB from a new Excel file.
 *
 * Return value: TRUE on success.
 **/
static gboolean
read_ole2_gb (gpointer *jody_broke_the_context, Workbook *wb, MsOle *f)
{
	GBLexerStream *proj_stream;

	g_return_val_if_fail (f != NULL, -1);
	g_return_val_if_fail (wb != NULL, -1);

	proj_stream = gb_project_stream (jody_broke_the_context, f);
	if (!proj_stream)
		return TRUE;

	return read_gb (jody_broke_the_context, wb, proj_stream, stream_provider, f);
}

static GBLexerStream *
file_to_stream (const char *filename)
{
	guint8            *data;
	struct stat        sbuf;
	int                fd, len;
	GBLexerStream     *ans;

	fd = open (filename, O_RDONLY);
	if (fd < 0 || fstat (fd, &sbuf) < 0) {
		fprintf (stderr, "gb: %s : %s\n", filename, strerror (errno));

		if (fd >= 0)
			close (fd);
		return NULL;
	}

	if ((len = sbuf.st_size) <= 0) {
		fprintf (stderr, "%s : empty file\n", filename);
		close (fd);
		return NULL;
	}

	data = g_new (guint8, len);
	if (read (fd, data, len) != len) {
		fprintf (stderr, "Short read error on '%s'\n", filename);
		g_free (data);
		return NULL;
	}

	if (!isspace (data [len - 1])) {
		fprintf (stderr, "File must end in whitespace");
		g_free (data);
		return NULL;
	}

	ans = gb_mmap_stream_new (data, len);

	close (fd);

	return ans;
}

static GBLexerStream *
file_provider (GBRunEvalContext *ec,
	       const char       *name,
	       gpointer          user_data)
{
	GBLexerStream *ret = NULL;

	if (g_file_test (name, G_FILE_TEST_EXISTS))
		ret = file_to_stream (name);

	else {
		char *fname;

		fname = g_strdup_printf ("%s/%s", g_get_home_dir (), name);

		if (g_file_test ((fname), G_FILE_TEST_EXISTS))
			ret = file_to_stream (fname);
		else
			g_warning ("Error opening '%s'", fname);

		g_free (fname);
	}

	return ret;
}

void
plugin_init_general (ErrorInfo **err)
{
	GBEvalContext *ec;
	GBLexerStream *proj_stream;
	char          *proj_name;

	g_return_if_fail (err != NULL);

	*err = NULL;

	gb_init ();

	ec = gb_eval_context_new ();
	gbrun_init (ec);
	if (gb_eval_exception (ec)) {
		*err = error_info_new_printf (
			_("Error initializing gb '%s'"),
			gb_eval_context_get_text (ec));
		return;
	}

	excel_gb_application_register_types ();

/*	plugin_data_set_user_data (pd, gb_pd);*/

	ms_excel_read_gb = (MsExcelReadGbFn) read_ole2_gb;

	proj_name = g_strdup_printf ("%s/gnumeric.gbp", g_get_home_dir ());
	if (g_file_test (projname, G_FILE_TEST_EXISTS)) {
		proj_stream = file_to_stream (proj_name);
		if (!read_gb (NULL, NULL, proj_stream, file_provider, NULL)) {
			*err = error_info_new_printf (_("Error in project '%s'"), proj_name);
		}
	}
	g_free (proj_name);
	/* Don't try to deactivate the plugin */
	gnm_plugin_use_ref (PLUGIN);
}
