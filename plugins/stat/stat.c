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
	"divided by the number of values minus 1. If you want it divided by "
	"N instead of N - 1, use NVARIANCE."
	"@SEEALSO=AVERAGE, NVARIANCE")
};

static char *help_stat_nvariance = {
	N_("@FUNCTION=NVARIANCE\n"
	"@SYNTAX = NVARIANVE(value1, value2, ...)"
	"@DESCRIPTION="
	"Computes the variation of all the values and cells references in the "
	"argument list. This is equivalent to the sum of (value - average)^2, "
	"divided by the number of values. If you want it divided by N - 1 "
	"instead of N, use VARIANCE."
	"@SEEALSO=AVERAGE, VARIANCE")
};
	
static FunctionDefinition plugin_functions[] = {
{"stdev", "", "", &help_stat_stdev, stat_stdev, NULL },
{"variance", "", "", &help_stat_variance, stat_variance, NULL },
{"nvariance", "", "", &help_stat_nvariance, stat_nvariance, NULL },
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
	Value *result;
	float undiv;
	unsigned int len;
	
	result = g_new(Value *, 1);
	result->type = VALUE_FLOAT;
	result->v.v_float = 0.0;
	
	undiv = stat_undivided_variance(sheet, expr_node_list, eval_col,
					eval_row, error_string);
	len = g_list_length(expr_node_list);
	if (len == 1) {
		*error_string = _("variance - division by 0");
		g_free(result);
		return NULL;
	}

	result->v.v_float = undiv / (len - 1);
	
	return result;
}

static Value *stat_nvariance(void *sheet, GList *expr_node_list, int eval_col,
			int eval_row, char **error_string) {
	Value *result;
	float undiv;
	unsigned int len;
	
	result = g_new(Value, 1);
	result->type = VALUE_FLOAT;
	result->v.v_float = 0.0;
	
	undiv = stat_undivided_variance(sheet, expr_node_list, eval_col,
					eval_row, error_string);
	
	len = g_list_length(expr_node_list);
	
	result->v.v_float = undiv / len;

	return result;
}
	
float stat_undivided_variance(void *sheet, GList *expr_node_list, 
				int eval_col, int eval_row, 
				char **error_string) {
	
	Value *avgV, *tmpval;
	GPtrArray *values;
	float tmp, result;
	unsigned int i;
	float avg;
	
	avgV = g_new(Value *, 1);
	tmpval = g_new(Value *, 1);

	values = g_ptr_array_new();
	
	
	function_iterate_argument_values(sheet, callback_var, values,
					expr_node_list, eval_col, eval_row,
					error_string);
	avgV = function_call_with_values(sheet, "average", values->len, 
			values->pdata, error_string);
	
	avg = value_get_as_double(avgV);
	
	for(i=0;i<(values->len); i++) {
		tmpval = g_ptr_array_index(values, i);
		tmp = tmpval->v.v_float - avg;
		tmp *= tmp;
		result += tmp;
	}
	
	g_free(avgV);	
	
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

	g_free(var);

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


			
