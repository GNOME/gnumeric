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
func_plusone (FunctionDefinition * fndef, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	
	v->type = VALUE_FLOAT;
	v->v.v_float = value_get_as_double (argv [0]) + 1.0;
	
	return v;
}

static FunctionDefinition plugin_functions [] = {
	{ "plusone",     "f",    "number",    NULL, NULL, func_plusone },
	{ NULL, NULL },
};

static int
can_unload (PluginData *pd)
{
	Symbol *sym;
	
	sym = symbol_lookup ("plusone");
	return sym->ref_count <= 1;
}
  
static void
cleanup_plugin (PluginData *pd)
{
	Symbol *sym;
	
	g_free (pd->title);
	sym = symbol_lookup ("plusone");
	if (sym) {
		symbol_unref(sym);
	}
}

int init_plugin (PluginData * pd)
{
	install_symbols (plugin_functions);
	pd->can_unload = can_unload;
	pd->cleanup_plugin = cleanup_plugin;
	pd->title = g_strdup ("PlusOne Plugin");
	return 0;
}

