/* -*- mode: c; c-basic-offset: 8 -*- */
#include <glib.h>
#include <assert.h>
#include <stdio.h>
#include <libguile.h>
#include <gnome.h>

#include "../../src/gnumeric.h"
#include "../../src/symbol.h"
#include "../../src/plugin.h"
#include "../../src/expr.h"
#include "../../src/func.h"

static int
scm_num2int (SCM num)
{
	return (int)scm_num2long(num, (char*)SCM_ARG1, "scm_num2int"); /* may I use scm_num2long? */
}

static SCM
scm_symbolfrom0str (char *name)
{
	return SCM_CAR(scm_intern0(name));
}

static SCM
list_to_scm (GList *list, CellRef eval_cell)
{
				/* FIXME: implement this */
	return SCM_EOL;
}

static SCM
cell_ref_to_scm (CellRef cell, CellRef eval_cell)
{
	int col = cell.col_relative ? cell.col + eval_cell.col : cell.col,
		row = cell.row_relative ? cell.row + eval_cell.row : cell.row;

	return scm_cons(scm_symbolfrom0str("cell-ref"),
			scm_cons(scm_long2num(col), scm_long2num(row)));
				/* FIXME: we need the relative-flags also */
}

static CellRef
scm_to_cell_ref (SCM scm)
{
	CellRef cell = { 0, 0, 0, 0 };

	if (SCM_NIMP(scm) && SCM_CONSP(scm)
	    && SCM_NFALSEP(scm_eq_p(SCM_CAR(scm), scm_symbolfrom0str("cell-ref")))
	    && SCM_NIMP(SCM_CDR(scm)) && SCM_CONSP(SCM_CDR(scm))
	    && SCM_NFALSEP(scm_number_p(SCM_CADR(scm))) && SCM_NFALSEP(scm_number_p(SCM_CDDR(scm))))
	{
		cell.col = (int)scm_num2int(SCM_CADR(scm));
		cell.row = (int)scm_num2int(SCM_CDDR(scm));
	}
	else
		;		/* FIXME: should report error */

	return cell;
}

static SCM
value_to_scm (Value *val, CellRef cell_ref)
{
	if (val == NULL)
		return SCM_EOL;

	switch (val->type)
	{
		case VALUE_STRING :
			return scm_makfrom0str(val->v.str->str);

		case VALUE_INTEGER :
			return scm_long2num(val->v.v_int);

		case VALUE_FLOAT :
			return scm_dbl2big(val->v.v_float);

		case VALUE_CELLRANGE :
			return scm_cons(scm_symbolfrom0str("cell-range"),
					scm_cons(cell_ref_to_scm(val->v.cell_range.cell_a, cell_ref),
						 cell_ref_to_scm(val->v.cell_range.cell_b, cell_ref)));

		case VALUE_ARRAY :
			return SCM_UNSPECIFIED;
	}

	return SCM_UNSPECIFIED;
}

static Value*
scm_to_value (SCM scm)
{
	if (SCM_NIMP(scm) && SCM_STRINGP(scm))
	{
		Value *val = g_new(Value, 1);

		val->type = VALUE_STRING;
		val->v.str = string_get(SCM_CHARS(scm)); /* assuming (wrongly?) that scm strings are zero-terminated */
		return val;
	}
	else if (SCM_NFALSEP(scm_number_p(scm)))
	{
		if (scm_integer_p(scm))
			return value_int((int)scm_num2int(scm));
		else
			return value_float((float)scm_num2dbl(scm, 0));
	}
	else if (SCM_NIMP(scm) && SCM_CONSP(scm))
	{
		if (scm_eq_p(SCM_CAR(scm), scm_symbolfrom0str("cell-range"))
		    && SCM_NIMP(SCM_CDR(scm)) && SCM_CONSP(SCM_CDR(scm)))
		{
			Value *val = g_new(Value, 1);

			val->type = VALUE_CELLRANGE;
			val->v.cell_range.cell_a = scm_to_cell_ref(SCM_CADR(scm));
			val->v.cell_range.cell_b = scm_to_cell_ref(SCM_CDDR(scm));
			return val;
		}
	}

	return NULL;		/* maybe we should return something more meaningful!? */
}

static SCM
expr_to_scm (ExprTree *expr, CellRef cell_ref)
{
	switch (expr->oper)
	{
		case OPER_EQUAL :
			return SCM_LIST3(scm_symbolfrom0str("="),
					 expr_to_scm(expr->u.binary.value_a, cell_ref),
					 expr_to_scm(expr->u.binary.value_b, cell_ref));

		case OPER_GT :
			return SCM_LIST3(scm_symbolfrom0str(">"),
					 expr_to_scm(expr->u.binary.value_a, cell_ref),
					 expr_to_scm(expr->u.binary.value_b, cell_ref));

		case OPER_LT :
			return SCM_LIST3(scm_symbolfrom0str("<"),
					 expr_to_scm(expr->u.binary.value_a, cell_ref),
					 expr_to_scm(expr->u.binary.value_b, cell_ref));

		case OPER_GTE :
			return SCM_LIST3(scm_symbolfrom0str(">="),
					 expr_to_scm(expr->u.binary.value_a, cell_ref),
					 expr_to_scm(expr->u.binary.value_b, cell_ref));

		case OPER_LTE :
			return SCM_LIST3(scm_symbolfrom0str("<="),
					 expr_to_scm(expr->u.binary.value_a, cell_ref),
					 expr_to_scm(expr->u.binary.value_b, cell_ref));

		case OPER_NOT_EQUAL :
			return SCM_LIST3(scm_symbolfrom0str("<>"),
					 expr_to_scm(expr->u.binary.value_a, cell_ref),
					 expr_to_scm(expr->u.binary.value_b, cell_ref));

		case OPER_ADD :
			return SCM_LIST3(scm_symbolfrom0str("+"),
					 expr_to_scm(expr->u.binary.value_a, cell_ref),
					 expr_to_scm(expr->u.binary.value_b, cell_ref));

		case OPER_SUB :
			return SCM_LIST3(scm_symbolfrom0str("-"),
					 expr_to_scm(expr->u.binary.value_a, cell_ref),
					 expr_to_scm(expr->u.binary.value_b, cell_ref));

		case OPER_NEG :
			return SCM_LIST2(scm_symbolfrom0str("neg"),
					 expr_to_scm(expr->u.value, cell_ref));

		case OPER_MULT :
			return SCM_LIST3(scm_symbolfrom0str("*"),
					 expr_to_scm(expr->u.binary.value_a, cell_ref),
					 expr_to_scm(expr->u.binary.value_b, cell_ref));

		case OPER_DIV :
			return SCM_LIST3(scm_symbolfrom0str("/"),
					 expr_to_scm(expr->u.binary.value_a, cell_ref),
					 expr_to_scm(expr->u.binary.value_b, cell_ref));

		case OPER_EXP :
			return SCM_LIST3(scm_symbolfrom0str("expt"),
					 expr_to_scm(expr->u.binary.value_a, cell_ref),
					 expr_to_scm(expr->u.binary.value_b, cell_ref));

		case OPER_CONCAT :
			return SCM_LIST3(scm_symbolfrom0str("string-append"),
					 expr_to_scm(expr->u.binary.value_a, cell_ref),
					 expr_to_scm(expr->u.binary.value_b, cell_ref));

		case OPER_FUNCALL :
			return SCM_LIST3(scm_symbolfrom0str("funcall"),
					 scm_makfrom0str(expr->u.function.symbol->str),
					 list_to_scm(expr->u.function.arg_list, cell_ref));

		case OPER_CONSTANT :
			return value_to_scm(expr->u.constant, cell_ref);

		case OPER_VAR :
			return scm_cons(scm_symbolfrom0str("var"),
					cell_ref_to_scm(expr->u.constant->v.cell, cell_ref));
	}

	return SCM_UNSPECIFIED;
}

static Value*
func_scm_eval (FunctionDefinition *fn, Value *argv[], char **error_string)
{
	SCM result;

	if (argv[0]->type != VALUE_STRING)
	{
		*error_string = "Argument must be a Guile expression";
		return NULL;
	}

	result = scm_eval_0str(argv[0]->v.str->str);

	return scm_to_value(result);
}

static Value*
func_scm_apply (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	int i;
	Value *value;
	char *symbol;
	SCM args = SCM_EOL,
		function,
		result;

	if (g_list_length(expr_node_list) < 1)
	{
		*error_string = "Invalid number of arguments";
		return NULL;
	}

	value = eval_expr(tsheet, (ExprTree*)expr_node_list->data, eval_col, eval_row, error_string);
	if (value == NULL)
	{
		*error_string = "First argument to SCM must be a Guile expression";
		return NULL;
	}
	symbol = value_string(value);
	if (symbol == NULL)
	{
		*error_string = "First argument to SCM must be a Guile expression";
		return NULL;
	}
	function = scm_eval_0str(symbol);
	if (SCM_UNBNDP(function))
	{
		*error_string = "Undefined scheme function";
		return NULL;
	}
	value_release(value);

	for (i = g_list_length(expr_node_list) - 1; i >= 1; --i)
	{
		CellRef eval_cell = { eval_col, eval_row, 0, 0 };

		value = eval_expr(tsheet, (ExprTree*)g_list_nth(expr_node_list, i)->data, eval_col, eval_row, error_string);
		if (value == NULL)
		{
			*error_string = "Could not evaluate argument";
			return NULL;
		}
		args = scm_cons(value_to_scm(value, eval_cell), args);
		value_release(value);
	}

	result = scm_apply(function, args, SCM_EOL);

	return scm_to_value(result);
}

static SCM
scm_cell_value (SCM scm)
{
	CellRef cell_ref = scm_to_cell_ref(scm);
	Cell *cell = sheet_cell_get(workbook_get_current_sheet(current_workbook), cell_ref.col, cell_ref.row);

	if (cell == NULL)
		return SCM_EOL;

	assert(cell->value != NULL);

	return value_to_scm(cell->value, cell_ref);
}

static SCM
scm_cell_expr (SCM scm)
{
	CellRef cell_ref = scm_to_cell_ref(scm);
	Cell *cell = sheet_cell_get(workbook_get_current_sheet(current_workbook), cell_ref.col, cell_ref.row);

	if (cell == NULL || cell->parsed_node == NULL)
		return SCM_EOL;

	return expr_to_scm(cell->parsed_node, cell_ref);
}

static SCM
scm_set_cell_string (SCM scm_cell_ref, SCM scm_string)
{
	CellRef cell_ref = scm_to_cell_ref(scm_cell_ref);
	Cell *cell = sheet_cell_fetch(workbook_get_current_sheet(current_workbook), cell_ref.col, cell_ref.row);

	SCM_ASSERT(SCM_NIMP(scm_string) && SCM_STRINGP(scm_string), scm_string, SCM_ARG2, "set-cell-string!");

	if (cell == NULL)
		return SCM_UNSPECIFIED;

	cell_set_text(cell, SCM_CHARS(scm_string));

	return SCM_UNSPECIFIED;
}

static SCM
scm_gnumeric_funcall(SCM funcname, SCM arglist)
{
	char *error_string;
	int i, num_args;
	Value **values;
	CellRef cell_ref = { 0, 0, 0, 0 };

	SCM_ASSERT(SCM_NIMP(funcname) && SCM_STRINGP(funcname), funcname, SCM_ARG1, "gnumeric-funcall");
	SCM_ASSERT(SCM_NFALSEP(scm_list_p(arglist)), arglist, SCM_ARG2, "gnumeric-funcall");

	num_args = scm_ilength(arglist);
	values = g_new(Value*, num_args);
	for (i = 0; i < num_args; ++i)
	{
		values[i] = scm_to_value(SCM_CAR(arglist));
		arglist = SCM_CDR(arglist);
	}

	return value_to_scm(function_call_with_values(workbook_get_current_sheet(current_workbook),
						      SCM_CHARS(funcname),
						      num_args,
						      values,
						      &error_string),
			    cell_ref);
}

typedef struct {
	FunctionDefinition *fndef;
	SCM function;
} FuncData;

static GList *funclist = NULL;

static int
fndef_compare(FuncData *fdata, FunctionDefinition *fndef)
{
	return (fdata->fndef != fndef);
}

static Value*
func_marshal_func (FunctionDefinition *fndef, Value *argv[], char **error_string)
{
	GList *l;
	SCM args = SCM_EOL,
		result,
		function;
	CellRef dummy = { 0, 0, 0, 0 };
	int i,
		count = strlen(fndef->args);

	l = g_list_find_custom(funclist, fndef, (GCompareFunc)fndef_compare);
	if (l == NULL)
	{
		*error_string = "Unable to lookup Guile function.";
		return NULL;
	}

	function = ((FuncData*)l->data)->function;

	for (i = count - 1; i >= 0; --i)
		args = scm_cons(value_to_scm(argv[i], dummy), args);

	result = scm_apply(function, args, SCM_EOL);

	return scm_to_value(result);
}

static SCM
scm_register_function(SCM scm_name, SCM scm_args, SCM scm_help, SCM scm_function)
{
	FunctionDefinition *fndef;
	FuncData *fdata;

	SCM_ASSERT(SCM_NIMP(scm_name) && SCM_STRINGP(scm_name), scm_name, SCM_ARG1, "scm_register_function");
	SCM_ASSERT(SCM_NIMP(scm_args) && SCM_STRINGP(scm_args), scm_args, SCM_ARG2, "scm_register_function");
	SCM_ASSERT(SCM_NIMP(scm_help) && SCM_STRINGP(scm_help), scm_help, SCM_ARG3, "scm_register_function");
	SCM_ASSERT(scm_procedure_p(scm_function), scm_function, SCM_ARG4, "scm_register_function");

	scm_permanent_object(scm_function); /* is this correct? */

	fndef = g_new0(FunctionDefinition, 1);

	fdata = g_new(FuncData, 1);
	fdata->fndef = fndef;
	fdata->function = scm_function;

	funclist = g_list_append(funclist, fdata);

	fndef->name = g_strdup(SCM_CHARS(scm_name));
	fndef->args = g_strdup(SCM_CHARS(scm_args));
	fndef->named_arguments = NULL;
	fndef->help = g_new(char*, 1);
	*fndef->help = g_strdup(SCM_CHARS(scm_help));
	fndef->fn = func_marshal_func;

	symbol_install(global_symbol_table, fndef->name, SYMBOL_FUNCTION, fndef);

	return SCM_UNSPECIFIED;
}

static FunctionDefinition plugin_functions[] = {
	{ "scm_eval", "s", "expr", NULL, NULL, func_scm_eval },
	{ "scm_apply", 0, "symbol", NULL, func_scm_apply, NULL },
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};

static int
no_unloading_for_me (PluginData *pd)
{
	return 0;
}

int init_plugin (PluginData *pd);

int
init_plugin (PluginData *pd)
{
	char *init_file_name;

	install_symbols(plugin_functions);
	pd->can_unload = no_unloading_for_me;
	pd->title = g_strdup("Guile Plugin");

	scm_make_gsubr("cell-value", 1, 0, 0, scm_cell_value);
	scm_make_gsubr("cell-expr", 1, 0, 0, scm_cell_expr);
	scm_make_gsubr("set-cell-string!", 2, 0, 0, scm_set_cell_string);
	scm_make_gsubr("gnumeric-funcall", 2, 0, 0, scm_gnumeric_funcall);
	scm_make_gsubr("register-function", 4, 0, 0, scm_register_function);

	init_file_name = gnome_unconditional_datadir_file("gnumeric/guile/gnumeric_startup.scm");
	scm_apply(scm_eval_0str("(lambda (filename)"
				"  (if (access? filename R_OK)"
				"    (load filename)"
				"    (display (string-append \"could not read Guile plug-in init file\" filename \"\n\"))))"),
		  scm_cons(scm_makfrom0str(init_file_name), SCM_EOL),
		  SCM_EOL);

	return 0;
}
