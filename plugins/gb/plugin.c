/* -*- mode: c; c-basic-offset: 8 -*- */
/**
 * boot.c: Gnome Basic support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 2000 HelixCode, Inc
 **/

#include <config.h>
#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <glib.h>
#include <gnome.h>

#include <gb/libgb.h>
#include <gbrun/libgbrun.h>
#include <libole2/ms-ole-vba.h>

#include "gnumeric.h"
#include "symbol.h"
#include "plugin.h"
#include "expr.h"
#include "func.h"

#include "streams.h"
#include "../excel/excel.h"

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

static GBValue *
value_to_gb (FunctionEvalInfo *ei, Value *val)
{
	if (val == NULL)
		return NULL;

	switch (val->type) {
	case VALUE_EMPTY:
		/* FIXME ?? what belongs here */
		return gb_value_new_empty ();
 
	case VALUE_BOOLEAN:
		return gb_value_new_boolean (val->v_bool.val);	
			
	case VALUE_ERROR:
		/* FIXME ?? what belongs here */
		return gb_value_new_string_chars (val->v_err.mesg->str);
			
	case VALUE_STRING:
		return gb_value_new_string_chars (val->v_str.val->str);

	case VALUE_INTEGER:
		return gb_value_new_long (val->v_int.val);

	case VALUE_FLOAT:
		return gb_value_new_double (val->v_float.val);

	default:
		g_warning ("Unimplemented %d -> GB translation", val->type);

		return gb_value_new_int (0);
	}
}

static Value *
gb_to_value (FunctionEvalInfo *ei, GBValue *v)
{
	switch (v->type) {
	case GB_VALUE_EMPTY:
	case GB_VALUE_NULL:
		return value_new_empty ();

	case GB_VALUE_INT:
	case GB_VALUE_LONG:
		return value_new_int (gb_value_get_as_long (v));

	case GB_VALUE_SINGLE:
	case GB_VALUE_DOUBLE:
		return value_new_float (gb_value_get_as_double (v));

	case GB_VALUE_STRING:
		return value_new_string (v->v.s->str);

	default:
		g_warning ("Unimplemented GB %d -> gnumeric type mapping",
			   v->type);
		return value_new_error (ei->pos, "Unknown mapping");
	}
}

static int
dont_unload (PluginData *pd)
{
	return 0;
}

static void
cleanup (PluginData *pd)
{
	gbrun_shutdown ();
	gb_shutdown ();
}

/*
static const GBModule *
parse_file (char const * filename)
{
	int fd, len;
	struct stat sbuf;
	char const * data;
	GBModule const *module;
	GBEvalContext *ec;

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

	data = mmap (0, len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (data == MAP_FAILED) {

		fprintf (stderr, "%s : unable to mmap\n", filename);
		close (fd);
		return NULL;
	}

	ec = gb_eval_context_new ();
	module = gb_parse_stream (data, len, ec);
	if (!module)
		fprintf (stderr, "%s : %s", filename,
			 gb_eval_context_get_text (ec));
	gtk_object_destroy (GTK_OBJECT (ec));

	munmap ((char *)data, len);
	close (fd);

	return module;
}

typedef struct {
	GBRunContext *rc;
	GBRoutine     *r;
} func_data_t;
*/

#if 0
static Value *
generic_marshaller (FunctionEvalInfo *ei, GList *nodes)
{
	func_data_t *fd;
	GSList      *args = NULL;
	GList       *l;
	GBValue     *gb_ans;
	Value       *ans;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_def != NULL, NULL);

	fd = function_def_get_user_data (ei->func_def);
	g_return_val_if_fail (fd != NULL, NULL);

	for (l = nodes; l; l = l->next) {
		/*
		 * TODO : When the translation mechanism is more complete
		 * this can be relaxed.  We do not need to require
		 * non emptiness, or scalarness.
		 */
		Value *v = eval_expr (ei->pos, l->data, EVAL_STRICT);

		args = g_slist_prepend (args, value_to_gb (ei, v));
		
		value_release (v);
	}

	args = g_slist_reverse (args);

	gb_ans = gbrun_routine (fd->rc, fd->r->name, args);
	if (gb_ans)
		ans = gb_to_value (ei, gb_ans);
	else {
		GBEvalContext *ec = GB_EVAL_CONTEXT (fd->rc->ec);
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

	return ans;
}
#endif

/*
typedef struct {
	FunctionCategory *cat;
	GBRunContext    *rc;
} register_closure_t;

static void
cb_register_functions (gpointer key, gpointer value, gpointer user_data)
{
	register_closure_t *c = user_data;
	GBRoutine          *r = value;
	FunctionDefinition *fndef;
	func_data_t        *fd;

	fndef = function_add_nodes (c->cat, r->name, "", NULL, NULL,
				    generic_marshaller);
	
	fd     = g_new (func_data_t, 1);
	fd->rc = c->rc;
	fd->r  = r;

	function_def_set_user_data (fndef, fd);
}
*/

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
 * read_gb:
 * @context: 
 * @wb: 
 * @f: 
 * 
 * The main function that organises all of the GB from a new Excel file.
 * 
 * Return value: TRUE on success.
 **/
static gboolean
read_gb (CommandContext *context, Workbook *wb, MsOle *f)
{
	GBLexerStream    *proj_stream;
	GBWorkbookData   *wd;
	GBProject        *gb_proj;

	g_return_val_if_fail (f != NULL, -1);
	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (context != NULL, -1);

	proj_stream = gb_project_stream (context, f);
	if (!proj_stream)
		return TRUE;

	wd = g_new0 (GBWorkbookData, 1);
	gtk_object_set_data (GTK_OBJECT (wb), GB_PROJECT_KEY, wd);

	wd->ec = gbrun_eval_context_new ("Gnumeric GB plugin",
					 GBRUN_SEC_HARD);

	gb_proj = gb_project_new (GB_EVAL_CONTEXT (wd->ec), proj_stream);
	if (!gb_proj) {
		g_warning ("Failed to parse project file '%s'",
			   gbrun_eval_context_get_text (wd->ec));
		return FALSE;
  	}

	wd->proj = gbrun_project_new (wd->ec, gb_proj, stream_provider, f);

	if (!wd->proj) {
		g_warning ("Error creating project '%s'",
			   gbrun_eval_context_get_text (wd->ec));
		return FALSE;
	} else {
		GSList *fns, *f;

		fns = gbrun_project_fn_names (wd->proj);
		
		/*
		 * FIXME: Argh, this means we need per workbook functions; ha, ha ha.
		 */ 
		for (f = fns; f; f = f->next) {
			g_warning ("FIXME: register function '%s'", (char *)f->data);
		}
	}

	return TRUE;
}

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	GBEvalContext *ec;

	if (plugin_version_mismatch (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	gb_init ();

	ec = gb_eval_context_new ();
	gbrun_init (ec);
	if (gb_eval_exception (ec)) {
		g_warning ("Error initializing gb '%s'",
			   gb_eval_context_get_text (ec));
		return PLUGIN_ERROR;
	}

/*	plugin_data_set_user_data (pd, gb_pd);*/

	ms_excel_read_gb = read_gb;

	if (plugin_data_init (pd, dont_unload, cleanup,
			      _("Gnome Basic"),
			      _("Enables Gnome Basic support")))
		return PLUGIN_OK;
	else
		return PLUGIN_ERROR;

	return PLUGIN_OK;
}

