/* -*- mode: c; c-basic-offset: 8 -*- */
#include <config.h>
#include <glib.h>
#include <assert.h>
#include <stdio.h>
#include <libguile.h>
#include <gnome.h>
#include <guile/gh.h>

#include "gnumeric.h"
#include "symbol.h"
#include "plugin.h"
#include "expr.h"
#include "func.h"

/* This is damn ugly.
 * However, it will get things working again (I hope)
 * until someone who actually uses this thing takes
 * over maintaing it.
 */
static EvalPosition *eval_pos = NULL;

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
				/* FIXME: we need the relative-flags,
				 * and the sheet, and workbook */
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
		cell.col = gh_scm2int(SCM_CADR(scm));
		cell.row = gh_scm2int(SCM_CDDR(scm));
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
		case VALUE_EMPTY :
			/* FIXME ?? what belongs here */
			return scm_long2num(0);
 
		case VALUE_BOOLEAN :
			return gh_bool2scm(val->v.v_bool);	
			
		case VALUE_ERROR :
			/* FIXME ?? what belongs here */
			return scm_makfrom0str(val->v.error.mesg->str);

		case VALUE_STRING :
			return scm_makfrom0str(val->v.str->str);

		case VALUE_INTEGER :
			return scm_long2num(val->v.v_int);

		case VALUE_FLOAT :
			return gh_double2scm(val->v.v_float);

		case VALUE_CELLRANGE :
			return scm_cons(scm_symbolfrom0str("cell-range"),
					scm_cons(cell_ref_to_scm(val->v.cell_range.cell_a, cell_ref),
						 cell_ref_to_scm(val->v.cell_range.cell_b, cell_ref)));

		case VALUE_ARRAY :
			{
				int x, y, i, ii;
				SCM ls;

				x = val->v.array.x;
				y = val->v.array.y;

				ls = gh_eval_str("'()");

				for(i = 0; i < y; i++)
					for(ii = 0; i < x; i++)
						ls = scm_cons(val->v.array.vals[ii][i], ls);
				return ls;
			}
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
		/* We do not need to do any distinction between an integer or
		 *  a float here. If we do so, we can crash gnumeric if the
                 *  size of scm is bigger than the size of int
		 */

		return value_new_float ((float_t)scm_num2dbl(scm, 0));
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

	else if (gh_boolean_p (scm))
		      
		return value_new_bool ((gboolean) gh_scm2bool (scm));		       

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

		case OPER_UNARY_PLUS :
			return SCM_LIST2(scm_symbolfrom0str("+"),
					 expr_to_scm(expr->u.value, cell_ref));

		case OPER_UNARY_NEG :
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

		case OPER_PERCENT :
			return SCM_LIST3(scm_symbolfrom0str("modulo"),
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
					cell_ref_to_scm(expr->u.ref, cell_ref));

	        case OPER_NAME :

	        case OPER_ARRAY :
		

		/* FIXME : default : */
	}

	return SCM_UNSPECIFIED;
}

static Value*
func_scm_apply (FunctionEvalInfo *ei, GList *expr_node_list)
{
	int i;
	Value *value;
	char *symbol;
	SCM args = SCM_EOL,
		function,
		result;

	if (g_list_length(expr_node_list) < 1)
		return value_new_error (ei->pos, _("Invalid number of arguments"));

	value = eval_expr(ei->pos, (ExprTree*)expr_node_list->data);
	if (value == NULL)
		return value_new_error (ei->pos, _("First argument to SCM must be a Guile expression"));

	symbol = value_get_as_string (value);
	if (symbol == NULL)
		return value_new_error (ei->pos, _("First argument to SCM must be a Guile expression"));

	function = scm_eval_0str(symbol);
	if (SCM_UNBNDP(function))
		return value_new_error (ei->pos, _("Undefined scheme function"));

	value_release(value);

	for (i = g_list_length(expr_node_list) - 1; i >= 1; --i)
	{
		CellRef eval_cell;

		eval_cell.col = ei->pos->eval.col;
		eval_cell.row = ei->pos->eval.row;
		eval_cell.col_relative = 0;
		eval_cell.row_relative = 0;
		eval_cell.sheet = NULL;

		value = eval_expr (ei->pos, (ExprTree*)g_list_nth(expr_node_list, i)->data);
		if (value == NULL)
			return value_new_error (ei->pos, _("Could not evaluate argument"));

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
	Cell *cell;

	g_return_val_if_fail (eval_pos != NULL, SCM_EOL);

	cell = sheet_cell_get(eval_pos->sheet, cell_ref.col, cell_ref.row);

	if (cell == NULL)
		return SCM_EOL;

	assert (cell->value != NULL);

	return value_to_scm(cell->value, cell_ref);
}

static SCM
scm_cell_expr (SCM scm)
{
	CellRef cell_ref = scm_to_cell_ref(scm);
	Cell *cell;

	g_return_val_if_fail (eval_pos != NULL, SCM_EOL);

	cell = sheet_cell_get(eval_pos->sheet, cell_ref.col, cell_ref.row);

	if (cell == NULL || cell->parsed_node == NULL)
		return SCM_EOL;

	return expr_to_scm (cell->parsed_node, cell_ref);
}

static SCM
scm_set_cell_string (SCM scm_cell_ref, SCM scm_string)
{
	CellRef cell_ref = scm_to_cell_ref(scm_cell_ref);
	Cell *cell;

	g_return_val_if_fail (eval_pos != NULL, SCM_EOL);

	cell = sheet_cell_get(eval_pos->sheet, cell_ref.col, cell_ref.row);

	SCM_ASSERT(SCM_NIMP(scm_string) && SCM_STRINGP(scm_string), scm_string, SCM_ARG2, "set-cell-string!");

	if (cell == NULL)
		return SCM_UNSPECIFIED;

	cell_set_text (cell, SCM_CHARS(scm_string));

	return SCM_UNSPECIFIED;
}

static SCM
scm_gnumeric_funcall(SCM funcname, SCM arglist)
{
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

	return value_to_scm(function_call_with_values(eval_pos,
						      SCM_CHARS(funcname),
						      num_args,
						      values),
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
func_marshal_func (FunctionEvalInfo *ei, Value *argv[])
{
	GList *l;
	FunctionDefinition const *fndef = ei->func_def;
	SCM args = SCM_EOL, result, function;
	CellRef dummy = { 0, 0, 0, 0 };
	EvalPosition *old_eval_pos;
	int i, min, max;

	function_def_count_args(fndef, &min, &max);

	l = g_list_find_custom (funclist, (gpointer)fndef,
				(GCompareFunc)fndef_compare);
	if (l == NULL)
		return value_new_error (ei->pos, _("Unable to lookup Guile function."));

	function = ((FuncData*)l->data)->function;

	for (i = min - 1; i >= 0; --i)
		args = scm_cons (value_to_scm (argv [i], dummy), args);

	old_eval_pos = eval_pos;
	eval_pos     = ei->pos;
	result       = scm_apply (function, args, SCM_EOL);
	eval_pos     = old_eval_pos;

	return scm_to_value (result);
}

static SCM
scm_register_function(SCM scm_name, SCM scm_args, SCM scm_help, SCM scm_function)
{
	FunctionDefinition *fndef;
	FunctionCategory   *cat;
	FuncData *fdata;
	char **help;

	SCM_ASSERT(SCM_NIMP(scm_name) && SCM_STRINGP(scm_name), scm_name, SCM_ARG1, "scm_register_function");
	SCM_ASSERT(SCM_NIMP(scm_args) && SCM_STRINGP(scm_args), scm_args, SCM_ARG2, "scm_register_function");
	SCM_ASSERT(SCM_NIMP(scm_help) && SCM_STRINGP(scm_help), scm_help, SCM_ARG3, "scm_register_function");
	SCM_ASSERT(scm_procedure_p(scm_function), scm_function, SCM_ARG4, "scm_register_function");

	scm_permanent_object(scm_function); /* is this correct? */

	help  = g_new (char *, 1);
	*help = g_strdup(SCM_CHARS(scm_help));
	cat   = function_get_category ("Guile");
	fndef = function_add_args (cat, g_strdup(SCM_CHARS(scm_name)),
				   g_strdup(SCM_CHARS(scm_args)), NULL,
				   help, func_marshal_func);

	fdata = g_new(FuncData, 1);
	fdata->fndef = fndef;
	fdata->function = scm_function;

	funclist = g_list_append(funclist, fdata);

	return SCM_UNSPECIFIED;
}

static int
no_unloading_for_me (PluginData *pd)
{
	return 0;
}

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	FunctionCategory *cat;
	char *init_file_name;

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	/* Initialize just in case. */
	eval_pos = NULL;

	cat = function_get_category ("Guile");

	function_add_nodes (cat, "scm_apply", 0, "symbol", NULL, func_scm_apply);

	pd->can_unload = no_unloading_for_me;
	pd->title = g_strdup(_("Guile Plugin"));

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

	return PLUGIN_OK;
}
