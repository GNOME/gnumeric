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

#include "gnumeric.h"
#include "symbol.h"
#include "plugin.h"
#include "expr.h"
#include "func.h"

#ifndef MAP_FAILED
/* Someone needs their head examining - BSD ? */
#	define MAP_FAILED ((void *)-1)
#endif

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
		return gb_value_new_boolean (val->v.v_bool);	
			
	case VALUE_ERROR:
		/* FIXME ?? what belongs here */
		return gb_value_new_string_chars (val->v.error.mesg->str);
			
	case VALUE_STRING:
		return gb_value_new_string_chars (val->v.str->str);

	case VALUE_INTEGER:
		return gb_value_new_int (val->v.v_int);

	case VALUE_FLOAT:
		return gb_value_new_double (val->v.v_float);

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
	if (pd->private_data)
		gbrun_context_destroy (pd->private_data);
	pd->private_data = NULL;

	gbrun_shutdown ();
	gb_shutdown ();
}

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
		/* FIXME : Move error into error_result */
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
		Value *v = eval_expr (ei->pos, l->data);

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

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	register_closure_t c = { NULL, NULL };
	char              *fname;

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	gb_init ();
	gbrun_init ();

	fname = gnome_util_prepend_user_home (".gnumeric-vb");
	if (g_file_exists (fname)) {
		const GBModule *module;

		module = parse_file (fname);
		if (module) {
			c.cat = function_get_category ("Gnome Basic");
			c.rc  = gbrun_context_new (module, GB_RUN_SEC_HARD);
			g_hash_table_foreach (module->routines,
					      cb_register_functions, &c);
		}
	}
	g_free (fname);

	pd->can_unload     = dont_unload;
	pd->cleanup_plugin = cleanup;
	pd->title          = g_strdup(_("Gnome Basic Plugin"));
	pd->private_data   = c.rc;

	return PLUGIN_OK;
}
