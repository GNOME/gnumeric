/*
 * Sample plugin demostration
 *
 * Author:
 *   Tom Dyas (tdyas@romulus.rutgers.edu)
 */

#include <config.h>
#include <gnome.h>
#include <glib.h>

#include "../../src/gnumeric.h"
#include "../../src/func.h"
#include "../../src/plugin.h"
#include "../../src/value.h"

static Value *
func_plusone (FunctionEvalInfo *ei, Value *argv [])
{
	return value_new_float (value_get_as_float (argv [0]) + 1.0);
}

static int
can_unload (PluginData *pd)
{
	FunctionDefinition *func;

	func = func_lookup_by_name ("plusone", NULL);
	return func != NULL && func->ref_count <= 1;
}

static void
cleanup_plugin (PluginData *pd)
{
	FunctionDefinition *func;

	func = func_lookup_by_name ("plusone", NULL);
	if (func)
		func_unref (func);
}

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	FunctionCategory *cat;

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	cat = function_get_category (_("Sample Plugin"));
	function_add_args (cat, "plusone", "f", "number", NULL, func_plusone);

	if (plugin_data_init (pd, can_unload, cleanup_plugin,
				_("PlusOne"),
				_("Sample Plugin")))
	        return PLUGIN_OK;
	else
	        return PLUGIN_ERROR;
}

