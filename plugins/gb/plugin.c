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
#include <glib.h>
#include <assert.h>
#include <stdio.h>
#include <libguile.h>
#include <gnome.h>

#include "gnumeric.h"
#include "symbol.h"
#include "plugin.h"
#include "expr.h"
#include "func.h"

static GBValue *
value_to_gb (Value *val, CellRef cell_ref)
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
gb_to_value (GBValue *v)
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
		return value_new_string (v->v.str->str);

	default:
		g_warning ("Unimplemented GB %d -> gnumeric type mapping",
			   v->type);
		return value_new_error ("Unknown mapping");
	}
}

static int
dont_unload (PluginData *pd)
{
	return 0;
}

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	FunctionCategory *cat;

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	cat = function_get_category ("Gnome Basic");

	/* I do nothing yet */
	function_add_nodes (cat, "gb", 0, "symbol", NULL, NULL);

	pd->can_unload = dont_unload;
	pd->title = g_strdup(_("Gnome Basic Plugin"));

	return PLUGIN_OK;
}
