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
#include "expr-name.h"

/* We don't expect that many names ! */
static GList *global_names = NULL;

ExprName *
expr_name_lookup (Workbook *wb, char const *name)
{
	GList *p = global_names;
	while (p) {
		ExprName *expr_name = p->data;
		g_return_val_if_fail (expr_name, 0);
		if ((!wb || !expr_name->wb || expr_name->wb == wb) &&
		    (g_strcasecmp (expr_name->name->str, name) == 0))
			return expr_name;
		p = g_list_next (p);
	}
	return NULL;
}

static ExprName *
add_real (Workbook *wb, char *name, gboolean builtin,
	  ExprTree *expr_tree, void *expr_func)
{
	ExprName *expr_name;

	g_return_val_if_fail (name, 0);

	expr_name = g_new (ExprName,1);

	expr_name->wb      = wb;
	expr_name->name    = string_get (name);
	expr_name->builtin = builtin;
	if (builtin)
		expr_name->t.expr_func = expr_func;
	else
		expr_name->t.expr_tree = expr_tree;

	global_names = g_list_append (global_names, expr_name);

	return expr_name;
}

ExprName *
expr_name_add (Workbook *wb, char const *name,
	       ExprTree *expr, char **error_msg)
{
	ExprName *expr_name;

	g_return_val_if_fail (name, 0);
	
	if ((expr_name = expr_name_lookup (wb, name))) {
		*error_msg = _("already defined");
		return NULL;
	} else if (!wb) {
		*error_msg = _("no workbook");
		return NULL;
	}

	return add_real (wb, name, FALSE, expr, NULL);
}

void
expr_name_remove (ExprName *expr_name)
{
	global_names = g_list_remove (global_names, expr_name);

	if (expr_name) {
		if (expr_name->name->str) {
			g_free (expr_name->name->str);
			expr_name->name->str = 0;
		}
		if (!expr_name->builtin &&
		    expr_name->t.expr_tree)
			expr_tree_unref (expr_name->t.expr_tree);

		expr_name->wb   = 0;
		expr_name->t.expr_tree = 0;

		g_free (expr_name);
	}
}

void
expr_name_clean (Workbook *wb)
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
expr_name_list (Workbook *wb, gboolean builtins_too)
{
	GList *p   = global_names;
	GList *ans = NULL;
	while (p) {
		ExprName *expr_name = p->data;
		g_return_val_if_fail (expr_name, 0);

		if ((!wb || expr_name->wb == wb))
			ans = g_list_append (ans, expr_name);
		
		p = g_list_next (p);
	}
	return ans;
}

Value *
eval_expr_name (FunctionEvalInfo *ei, const ExprName *expr_name)
{
	g_return_val_if_fail (ei, NULL);

	if (!expr_name)
		return function_error (ei, gnumeric_err_NAME);

	if (expr_name->builtin)
		return expr_name->t.expr_func (ei, NULL);
	else
		return eval_expr (ei, expr_name->t.expr_tree);
}

/* ------------------------------------------------------------- */

static Value *
name_sheet_title (FunctionEvalInfo *ei, Value **args)
{
	if (!ei || !ei->pos.sheet || !ei->pos.sheet->name)
		return value_new_string (_("Error: no sheet"));
	else
		return value_new_string (ei->pos.sheet->name);
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
	   Criteria
	   Print_Area
	   Print_Titles
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
		add_real (NULL, builtins[lp].name, TRUE, NULL,
			  builtins[lp].fn);
		lp++;
	}
}
