/*
 * expr-name.c:  Workbook name table support, will deal
 *               with all the exotic special names.
 *
 * Author:
 *    Michael Meeks <michael@imaginator.com>
 *
 */

#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include "gnumeric.h"
#include "eval.h"
#include "value.h"
#include "workbook.h"
#include "expr-name.h"
#include "expr.h"

/* We don't expect that many global names ! */
static GList *global_names = NULL;

/*
 * FIXME: when we sort out the parser's scope problems
 * we'll fix this.
 */
static Workbook *
get_real_wb (Workbook *wb, Sheet *sheet)
{
	if (wb)
		return wb;
	if (sheet)
		return sheet->workbook;

	g_warning ("duff name scope");

	return NULL;
}

ExprName *
expr_name_lookup (Workbook *wb, Sheet *sheet, char const *name)
{
	GList *p = global_names;
       
	g_return_val_if_fail (name != NULL, NULL);

	while (p) {
		ExprName *expr_name = p->data;
		g_return_val_if_fail (expr_name != NULL, 0);
		if (g_strcasecmp (expr_name->name->str, name) == 0)
			return expr_name;
		p = g_list_next (p);
	}

	if (wb)
		p = wb->names;
	while (p) {
		ExprName *expr_name = p->data;
		g_return_val_if_fail (expr_name != NULL, 0);
		if (g_strcasecmp (expr_name->name->str, name) == 0)
			return expr_name;
		p = g_list_next (p);
	}

	if (sheet)
		p = sheet->names;
	while (p) {
		ExprName *expr_name = p->data;
		g_return_val_if_fail (expr_name != NULL, 0);
		if (g_strcasecmp (expr_name->name->str, name) == 0)
			return expr_name;
		p = g_list_next (p);
	}

	return NULL;
}

static ExprName *
add_real (Workbook *wb, Sheet *sheet, const char *name,
	  gboolean builtin, ExprTree *expr_tree, void *expr_func)
{
	ExprName *expr_name;

	g_return_val_if_fail (name != NULL, NULL);

	expr_name = g_new (ExprName,1);

	expr_name->wb      = wb;
	expr_name->sheet   = sheet;
	expr_name->name    = string_get (name);

	g_return_val_if_fail (expr_name->name != NULL, NULL);

	expr_name->builtin = builtin;

	if (builtin)
		expr_name->t.expr_func = expr_func;
	else
		expr_name->t.expr_tree = expr_tree;

	return expr_name;
}

/*
 * NB. if we already have a circular reference in addition
 * to this one we are checking we will come to serious grief.
 */
static gboolean
name_refer_circular (const char *name, ExprTree *expr)
{
	g_return_val_if_fail (expr != NULL, TRUE);

	switch (expr->oper) {
	case OPER_ANY_BINARY:
		if (!name_refer_circular (name, expr->u.binary.value_a))
			return TRUE;
		if (!name_refer_circular (name, expr->u.binary.value_b))
			return TRUE;
		break;
	case OPER_ANY_UNARY:
		if (!name_refer_circular (name, expr->u.value))
			return TRUE;
		break;
	case OPER_NAME:
	{
		const ExprName *expr_name = expr->u.name;
		if (!g_strcasecmp (expr_name->name->str, name))
			return TRUE;
		/* And look inside this name tree too */
		if (!expr_name->builtin &&
		    !name_refer_circular (name, expr_name->t.expr_tree))
			return TRUE;
		break;
	}
	case OPER_FUNCALL:
	{
		GList *l = expr->u.function.arg_list;
		while (l) {
			if (name_refer_circular (name, l->data))
				return TRUE;
			l = g_list_next (l);
		}
		break;
	}
	case OPER_CONSTANT:
	case OPER_VAR:
	case OPER_ARRAY:
		break;
	}
	return FALSE;
}

/**
 * expr_name_add:
 * @wb: 
 * @sheet: 
 * @name: 
 * @expr: 
 * @error_msg: 
 * 
 * Parses a texual name in @value, and enters the value
 * either as a workbook name if @sheet == NULL or a sheet
 * name if @wb == NULL. If both wb & sheet == NULL then
 * it must be a truly global name.
 * 
 * Return value: 
 **/
ExprName *
expr_name_add (Workbook *wb, Sheet *sheet, char const *name,
	       ExprTree *expr, char **error_msg)
{
	ExprName *expr_name;

	g_return_val_if_fail (name != NULL, 0);
	g_return_val_if_fail (expr != NULL, 0);

/*	printf ("Adding name '%s' to %p %p\n", name, wb, sheet);*/
	
	if ((expr_name = expr_name_lookup (wb, sheet, name))) {
		*error_msg = _("already defined");
		return NULL;
	} else if (!wb && !sheet) {
		*error_msg = _("no scope");
		return NULL;
	}

	if (name_refer_circular (name, expr)) {
		*error_msg = _("circular reference");
		return NULL;
	}

	expr_name = add_real (wb, sheet, name, FALSE, expr, NULL);
	if (wb)
		wb->names    = g_list_append (wb->names, expr_name);
	else if (sheet)
		sheet->names = g_list_append (sheet->names, expr_name);
	else
		global_names = g_list_append (global_names, expr_name);
	
	return expr_name;
}

/**
 * expr_name_create:
 * @wb: 
 * @sheet:
 * @name: 
 * @value: 
 * @error_msg: 
 * 
 * Parses a texual name in @value, and enters the value
 * either as a workbook name if @sheet == NULL or a sheet
 * name if @wb == NULL.
 * 
 * Return value: The created ExprName.
 **/
ExprName *
expr_name_create (Workbook *wb, Sheet *sheet, const char *name,
		  const char *value, char **error_msg)
{
	ExprTree     *tree;
	ParsePosition pp;

	tree = expr_parse_string (value,
				  parse_pos_init (&pp,
						  get_real_wb (wb, sheet),
						  0, 0),
				  NULL, error_msg);

	if (!tree)
		return NULL;

	return expr_name_add (wb, sheet, name, tree, error_msg);
}

void
expr_name_remove (ExprName *expr_name)
{
	Workbook *wb;
	Sheet    *sheet;

	g_return_if_fail (expr_name != NULL);

	if (expr_name->wb) {
		wb = expr_name->wb;
		g_assert (g_list_find (wb->names, expr_name) != NULL);
		wb->names = g_list_remove (wb->names, expr_name);
		g_assert (g_list_find (wb->names, expr_name) == NULL);
	} else if (expr_name->sheet) {
		sheet = expr_name->sheet;
		g_assert (g_list_find (sheet->names, expr_name) != NULL);
		sheet->names = g_list_remove (sheet->names, expr_name);
		g_assert (g_list_find (sheet->names, expr_name) == NULL);
	} else {
		printf ("Removing from globals\n");
		/* FIXME -- this code is not right.  */
		abort ();
		g_assert (g_list_find (wb->names, expr_name) != NULL);
		global_names = g_list_remove (global_names, expr_name);
		g_assert (g_list_find (wb->names, expr_name) == NULL);
	}

/*	printf ("Removing : '%s'\n", expr_name->name->str);*/
	if (expr_name->name)
		string_unref (expr_name->name);
	expr_name->name = NULL;

	if (!expr_name->builtin &&
	    expr_name->t.expr_tree)
		expr_tree_unref (expr_name->t.expr_tree);
	
	expr_name->wb          = NULL;
	expr_name->sheet       = NULL;
	expr_name->t.expr_tree = NULL;
	
	g_free (expr_name);
}

void
expr_name_clean_sheet (Sheet *sheet)
{
	GList *p = global_names;
	GList *next ;
	while (p) {
		ExprName *expr_name = p->data;
		g_return_if_fail (expr_name);

		next = g_list_next (p);
		if ((!sheet || expr_name->sheet == sheet))
			expr_name_remove (expr_name);
		
		p = next;
	}
}

void
expr_name_clean_workbook (Workbook *wb)
{
	GList *p = global_names;
	GList *next ;
	while (p) {
		ExprName *expr_name = p->data;
		g_return_if_fail (expr_name);

		next = g_list_next (p);
		if ((!wb || expr_name->wb == wb))
			expr_name_remove (expr_name);
		
		p = next;
	}
}

GList *
expr_name_list (Workbook *wb, Sheet *sheet, gboolean builtins_too)
{
	GList *l;
	g_return_val_if_fail (wb != NULL, NULL);
	
	if (wb->names)
		l = g_list_copy (wb->names);
	else
		l = NULL;
	if (builtins_too && global_names)
		l = g_list_append (l, g_list_copy (global_names));

	return l;
}

char *
expr_name_value (const ExprName *expr_name)
{
	gchar         *val;
	ParsePosition  pp;

	if (!expr_name->builtin) {
		parse_pos_init (&pp, get_real_wb (expr_name->wb, 
						  expr_name->sheet),
				0, 0);
		val = expr_decode_tree (expr_name->t.expr_tree, &pp);
	} else
		val = g_strdup (_("Builtin"));

	if (val)
		return val;
	else
		return g_strdup ("Error");

	return val;
}


Value *
eval_expr_name (EvalPosition const * const pos, const ExprName *expr_name)
{
	g_return_val_if_fail (pos, NULL);

	if (!expr_name)
		return value_new_error (pos, gnumeric_err_NAME);

	if (expr_name->builtin) {
		FunctionEvalInfo ei;
		ei.pos = pos;
		ei.func_def = NULL; /* FIXME : This is ugly. why are there
					no descriptors for builtins */
		return expr_name->t.expr_func (&ei, NULL);
	} else
		return eval_expr (pos, expr_name->t.expr_tree);
}

/* ------------------------------------------------------------- */

static Value *
name_print_area (FunctionEvalInfo *ei, Value **args)
{
	if (!ei || !ei->pos->sheet)
		return value_new_error (ei->pos, _("Error: no sheet"));
	else {
		Range r = sheet_get_extent (ei->pos->sheet);
		return value_new_cellrange_r (sheet, r);
	}
}

static Value *
name_sheet_title (FunctionEvalInfo *ei, Value **args)
{
	if (!ei || !ei->pos->sheet || !ei->pos->sheet->name)
		return value_new_error (ei->pos, _("Error: no sheet"));
	else
		return value_new_string (ei->pos->sheet->name);
}

/* ------------------------------------------------------------- */

static struct {
	gchar        *name;
	FunctionArgs *fn;
} builtins[] =
{
	/* Consolidate_Area
	   Auto_Open
	   Auto_Close
	   Extract
	   Database
	   Criteria*/
	{ "Print_Area", name_print_area },
/*	   Print_Titles
	   Recorder
	   Data_Form
	   Auto_Activate
	   Auto_Deactivate */
	{ "Sheet_Title", name_sheet_title },
	{ NULL, NULL }
};

/* See: S59DA9.HTM for compatibility */
void 
expr_name_init (void)
{
	int lp=0;
	
	/* Not in global function table though ! */
	while (builtins[lp].name) {
		ExprName *expr_name;
		expr_name = add_real (NULL, NULL, builtins[lp].name, TRUE, NULL,
				      builtins[lp].fn);
		global_names = g_list_append (global_names, expr_name);
		lp++;
	}
}
