/*
 * Sample plugin demostration
 *
 * Author:
 *   Tom Dyas (tdyas@romulus.rutgers.edu)
 */
#include <gnome.h>
#include <glib.h>

#include "../../src/gnumeric.h"
#include "../../src/func.h"
#include "../../src/plugin.h"

static Value *
func_plusone (FunctionEvalInfo *ei, Value *argv [])
{
	Value *v = g_new (Value, 1);
	
	v->type = VALUE_FLOAT;
	v->v.v_float = value_get_as_float (argv [0]) + 1.0;
	
	return v;
}

static int
can_unload (PluginData *pd)
{
	Symbol *sym;
	
	sym = symbol_lookup (global_symbol_table, "plusone");
	return sym->ref_count <= 1;
}
  
static void
cleanup_plugin (PluginData *pd)
{
	Symbol *sym;
	
	g_free (pd->title);
	sym = symbol_lookup (global_symbol_table, "plusone");
	if (sym) {
		symbol_unref(sym);
	}
}

int
init_plugin (PluginData *pd)
{
	FunctionCategory *cat = function_get_category (_("Sample Plugin"));

	function_add_args (cat, "plusone", "f", "number", NULL, func_plusone);

	pd->can_unload = can_unload;
	pd->cleanup_plugin = cleanup_plugin;
	pd->title = g_strdup ("PlusOne Plugin");
	return 0;
}

