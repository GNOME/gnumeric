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
	return NULL;
}

static ExprName *
add_real (Workbook *wb, const char *name, gboolean builtin,
	  ExprTree *expr_tree, void *expr_func)
{
	ExprName *expr_name;

	g_return_val_if_fail (name != NULL, NULL);

	expr_name = g_new (ExprName,1);

	expr_name->wb      = wb;
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

ExprName *
expr_name_add (Workbook *wb, char const *name,
	       ExprTree *expr, char **error_msg)
{
	ExprName *expr_name;

	g_return_val_if_fail (wb != NULL, 0);
	g_return_val_if_fail (name != NULL, 0);
	g_return_val_if_fail (expr != NULL, 0);
	
	if ((expr_name = expr_name_lookup (wb, name))) {
		*error_msg = _("already defined");
		return NULL;
	} else if (!wb) {
		*error_msg = _("no workbook");
		return NULL;
	}

	if (name_refer_circular (name, expr)) {
		*error_msg = _("circular reference");
		return NULL;
	}

	expr_name = add_real (wb, name, FALSE, expr, NULL);
	if (wb)
		wb->names    = g_list_append (wb->names, expr_name);
	else
		global_names = g_list_append (global_names, expr_name);
	
	return expr_name;
}

ExprName *
expr_name_create (Workbook *wb, const char *name,
		  const char *value, char **error_msg)
{
	ExprTree     *tree;
	ParsePosition pp;

	tree = expr_parse_string (value,
				  parse_pos_init (&pp, wb, 0, 0),
				  NULL, error_msg);

	if (!tree)
		return NULL;

	return expr_name_add (wb, name, tree, error_msg);
}

void
expr_name_remove (ExprName *expr_name)
{
	Workbook *wb;
	g_return_if_fail (expr_name != NULL);

	if (expr_name->wb) {
		printf ("Removing from workbook\n");
		wb = expr_name->wb;
		g_assert (g_list_find (wb->names, expr_name) != NULL);
		wb->names = g_list_remove (wb->names, expr_name);
		g_assert (g_list_find (wb->names, expr_name) == NULL);
	} else {
		printf ("Removing from globals\n");
		/* FIXME -- this code is not right.  */
		abort ();
		g_assert (g_list_find (wb->names, expr_name) != NULL);
		global_names = g_list_remove (global_names, expr_name);
		g_assert (g_list_find (wb->names, expr_name) == NULL);
	}

	printf ("Removing : '%s'\n", expr_name->name->str);
	if (expr_name->name)
		string_unref (expr_name->name);
	expr_name->name = NULL;

	if (!expr_name->builtin &&
	    expr_name->t.expr_tree)
		expr_tree_unref (expr_name->t.expr_tree);
	
	expr_name->wb          =  NULL;
	expr_name->t.expr_tree = NULL;
	
	g_free (expr_name);
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
		parse_pos_init (&pp, expr_name->wb, 0, 0);
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
eval_expr_name (FunctionEvalInfo *ei, const ExprName *expr_name)
{
	g_return_val_if_fail (ei, NULL);

	if (!expr_name)
		return value_new_error (&ei->pos, gnumeric_err_NAME);

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
		return value_new_error (&ei->pos, _("Error: no sheet"));
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
		ExprName *expr_name;
		expr_name = add_real (NULL, builtins[lp].name, TRUE, NULL,
				      builtins[lp].fn);
		global_names = g_list_append (global_names, expr_name);
		lp++;
	}
}
