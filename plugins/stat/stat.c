/* Statistics Plugin
 *
 * Richard Hestilow <hestgray@ionet.net>
 */

#include "stat.h"
#include <math.h>

static char *help_stat_stdev = { 
	N_("@FUNCTION=STDEV\n"
	"@SYNTAX = STDEV(value1, value2, ...)"
	"@DESCRIPTION="
	"Computes the standard deviation of all the values and cells"
	" referenced in the argument list. This is equivalent to the square "
	"root of the variance."
	"\n"
	"@SEEALSO=VARIANCE")
};			
				
static char *help_stat_variance = {
	N_("@FUNCTION=VARIANCE\n"
	"@SYNTAX = VARIANCE(value1, value2, ...)"
	"@DESCRIPTION="
	"Computes the variation of all the values and cells referenced in the "
	"argument list. This is	equivalent to the sum of (value - average)^2, "
	"divided by the number of values minus 1."
	"@SEEALSO=AVERAGE")
};

static FunctionDefinition plugin_functions[] = {
	{"stdev", "", "", &help_stat_stdev, stat_stdev, NULL },
	{"variance", "", "", &help_stat_variance, stat_variance, NULL },
	{ NULL, NULL },};


static int can_unload(PluginData *pd) {
	Symbol *sym;
	sym = symbol_lookup("stat_variance");
	return sym->ref_count <= 1;
}
	
int init_plugin (PluginData *pd) {
	install_symbols(plugin_functions);
	pd->can_unload = can_unload;
	pd->cleanup_plugin = cleanup_plugin;
	pd->title = g_strdup("Statistics Plugin");
	return 0;
}

static void cleanup_plugin (PluginData *pd) {
	Symbol *sym;
	unsigned int i;
	
	g_free (pd->title);
	
	for(i=0;i<(((sizeof(plugin_functions))/(sizeof(FunctionDefinition)))-1);i++)  {
	sym = symbol_lookup(plugin_functions[i].name);
	if (sym) 
			symbol_unref(sym);
	}
}

static Value *stat_variance(void *sheet, GList *expr_node_list, int eval_col,
				int eval_row, char **error_string) {
	Value *result, *avg, *tmpval;
	GPtrArray *values;
	gpointer *pdata;	
	float tmp;
	unsigned int i;
	
	values = g_ptr_array_new();
	result = g_new(Value, 1);
	result->type = VALUE_FLOAT;
	result->v.v_float = 0.0;
	
	
	function_iterate_argument_values(sheet, callback_var, values,
					expr_node_list, eval_col, eval_row,
					error_string);
	avg = function_call_with_values(sheet, "average", values->len, 
			values->pdata, error_string);
	
	for(i=0;i<(values->len); i++) {
		tmpval = g_ptr_array_index(values, i);
		tmp = tmpval->v.v_float - value_get_as_double(avg);
		tmp *= tmp;
		result->v.v_float += tmp;
	}
	result->v.v_float /= values->len - 1;
	
	return result;
}

static Value *stat_stdev(void *sheet, GList *expr_node_list, int eval_col,                               int eval_row, char **error_string) {
	Value *result, *var;
	
	result = g_new(Value, 1);
	result->type = VALUE_FLOAT;
	result->v.v_float = 0.0;

	var = stat_variance(sheet, expr_node_list, eval_col, eval_row,
				error_string);

	result->v.v_float = sqrt(var->v.v_float);

	return result;
}
	

int callback_var ( Sheet *sheet, Value *value, char **error_string, 
		void *closure) {
	GPtrArray *values = (GPtrArray *) closure;

	float tmp;
	tmp =value_get_as_double(value);
	value->v.v_float = tmp;
	value->type = VALUE_FLOAT;
	g_ptr_array_add(values, g_memdup(value, sizeof(*value)));
	return TRUE;
}


			
