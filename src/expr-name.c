/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * expr-name.c:  Workbook name table support, will deal
 *               with all the exotic special names.
 *
 * Author:
 *    Michael Meeks <michael@ximian.com>
 */

#include <config.h>
#include <glib.h>
#include <gdk/gdkkeysyms.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include "expr-name.h"
#include "eval.h"
#include "cell.h"
#include "value.h"
#include "workbook.h"
#include "expr.h"
#include "str.h"
#include "sheet.h"
#include "sheet-style.h"

/* We don't expect that many global names ! */
static GList *global_names = NULL;

static NamedExpression *
expr_name_lookup_list (GList *p, const char *name)
{
	while (p) {
		NamedExpression *expr_name = p->data;
		g_return_val_if_fail (expr_name != NULL, 0);
		if (g_strcasecmp (expr_name->name->str, name) == 0)
			return expr_name;
		p = g_list_next (p);
	}
	return NULL;
}

/* FIXME : Why not use hash tables ? */
NamedExpression *
expr_name_lookup (const ParsePos *pos, const char *name)
{
	NamedExpression *res = NULL;
	Sheet const *sheet = NULL;
	Workbook const *wb = NULL;

	g_return_val_if_fail (name != NULL, NULL);

	if (pos != NULL) {
		sheet = pos->sheet;
		wb = (sheet != NULL) ? sheet->workbook : pos->wb;
	}

	if (sheet != NULL)
		res = expr_name_lookup_list (sheet->names, name);
	if (res == NULL && wb != NULL)
		res = expr_name_lookup_list (wb->names, name);
	if (res == NULL)
		res = expr_name_lookup_list (global_names, name);
	return res;
}

static NamedExpression *
named_expr_new (char const *name, gboolean builtin)
{
	NamedExpression *expr_name;

	g_return_val_if_fail (name != NULL, NULL);

	expr_name = g_new0 (NamedExpression,1);

	expr_name->ref_count = 1;
	expr_name->builtin = builtin;
	expr_name->name    = string_get (name);
	expr_name->dependents =
		g_hash_table_new (g_direct_hash, g_direct_equal);

	g_return_val_if_fail (expr_name->name != NULL, NULL);

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

	switch (expr->any.oper) {
	case OPER_ANY_BINARY:
		return (name_refer_circular (name, expr->binary.value_a) ||
			name_refer_circular (name, expr->binary.value_b));
	case OPER_ANY_UNARY:
		return name_refer_circular (name, expr->unary.value);
	case OPER_NAME: {
		NamedExpression const *expr_name = expr->name.name;
		if (expr_name->builtin)
			return FALSE;

		if (!g_strcasecmp (expr_name->name->str, name))
			return TRUE;

		/* And look inside this name tree too */
		return name_refer_circular (name, expr_name->t.expr_tree);
	}
	case OPER_FUNCALL: {
		GList *l = expr->func.arg_list;
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
 * @pp:
 * @name:
 * @expr:
 * @error_msg:
 *
 * Absorbs the reference to @expr.
 **/
NamedExpression *
expr_name_add (ParsePos const *pp, const char *name,
	       ExprTree *expr, char **error_msg)
{
	NamedExpression *expr_name;
	GList **scope = NULL;

	g_return_val_if_fail (pp != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (expr != NULL, NULL);

/*	printf ("Adding name '%s' to %p %p\n", name, wb, sheet);*/
	if (pp->sheet != NULL) {
		scope = &(pp->sheet->names);
		*error_msg = _("already defined in sheet");
	} else if (pp->wb != NULL) {
		scope = &(pp->wb->names);
		*error_msg = _("already defined in workbook");
	} else {
		scope = &global_names;
		*error_msg = _("already globally defined ");
	}

	if (expr_name_lookup_list (*scope, name) != NULL)
		return NULL;
	if (name_refer_circular (name, expr)) {
		*error_msg = _("'%s' has a circular reference");
		return NULL;
	}
	*error_msg = NULL;

	expr_name = named_expr_new (name, FALSE);
	expr_name->t.expr_tree = expr;
	parse_pos_init (&expr_name->pos,
			pp->wb, pp->sheet, pp->eval.col, pp->eval.row);

	*scope = g_list_append (*scope, expr_name);

	return expr_name;
}

/**
 * expr_name_create:
 * @pp:
 * @name:
 * @value:
 * @error_msg:
 *
 * Parses a texual name in @value, and enters the value
 * either as a workbook name if @sheet == NULL or a sheet
 * name if @wb == NULL.
 *
 * Return value: The created NamedExpression.
 **/
NamedExpression *
expr_name_create (ParsePos const *pp, const char *name,
		  const char *value, ParseError *error)
{
	ExprTree *tree = expr_parse_string (value, pp, NULL, error);
	if (!tree)
		return NULL;

	/* We know there has been no parse error, but set the
	 * use the message part of the struct to pass a name
	 * creation error back to the calling routine
	 */
	return expr_name_add (pp, name, tree, &error->message);
}

void
expr_name_ref (NamedExpression *expr_name)
{
	g_return_if_fail (expr_name != NULL);

	expr_name->ref_count++;
}

void
expr_name_unref (NamedExpression *expr_name)
{
	g_return_if_fail (expr_name != NULL);

	if (expr_name->ref_count-- > 1)
		return;

	if (expr_name->name) {
		string_unref (expr_name->name);
		expr_name->name = NULL;
	}

	if (!expr_name->builtin && expr_name->t.expr_tree) {
		expr_tree_unref (expr_name->t.expr_tree);
		expr_name->t.expr_tree = NULL;
	}

	if (expr_name->dependents != NULL) {
		g_hash_table_destroy (expr_name->dependents);
		expr_name->dependents  = NULL;
	}

	expr_name->pos.wb      = NULL;
	expr_name->pos.sheet   = NULL;

	g_free (expr_name);
}

static void
expr_name_unlink (NamedExpression *expr_name)
{
	Workbook *wb = NULL;
	Sheet    *sheet = NULL;

	/* printf ("Removing : '%s'\n", expr_name->name->str);*/
	if (expr_name->pos.sheet) {
		sheet = expr_name->pos.sheet;
		g_return_if_fail (g_list_find (sheet->names, expr_name) != NULL);
		sheet->names = g_list_remove (sheet->names, expr_name);
	} else if (expr_name->pos.wb) {
		wb = expr_name->pos.wb;
		g_return_if_fail (g_list_find (wb->names, expr_name) != NULL);
		wb->names = g_list_remove (wb->names, expr_name);
	} else {
		g_return_if_fail (g_list_find (global_names, expr_name) != NULL);
		global_names = g_list_remove (global_names, expr_name);
	}
}

void
expr_name_remove (NamedExpression *expr_name)
{
	g_return_if_fail (expr_name != NULL);

	expr_name_unlink (expr_name);
	expr_name_invalidate_refs_name (expr_name);
	expr_name_unref (expr_name);
}

/*
 * expr_name_list_destroy :
 *
 * Frees names in the local scope.
 * NOTE : THIS DOES NOT INVALIDATE NAMES THAT REFER
 *        TO THIS SCOPE.
 *        eg
 *           in scope sheet2 we have a name that refers
 *           to sheet1.  That will remain!
 *           sheet_deps_destroy handles that.
 */
GList *
expr_name_list_destroy (GList *names)
{
	GList *p;

	/* Empty the name list */
	for (p = names ; p != NULL ; p = g_list_next (p)) {
		NamedExpression *expr_name = p->data;
		expr_name_unref (expr_name);
	}
	g_list_free (names);
	return NULL;
}

/**
 * expr_name_as_string :
 * @nexpr :
 * @pp : optionally null.
 *
 * returns a string that the caller needs to free.
 */
char *
expr_name_as_string (NamedExpression const *nexpr, ParsePos const *pp)
{
	if (nexpr->builtin)
		return g_strdup (_("Builtin"));

	if (pp == NULL)
		pp = &nexpr->pos;
	return expr_tree_as_string (nexpr->t.expr_tree, pp);
}

Value *
expr_name_eval (NamedExpression const *expr_name,
		EvalPos const *pos, ExprEvalFlags flags)
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
	}

	return expr_eval (expr_name->t.expr_tree, pos, flags);
}

/*******************************************************************
 * Manage things that depend on named expressions.
 */

void
expr_name_invalidate_refs_name (NamedExpression *nexpr)
{
}

void
expr_name_invalidate_refs_sheet (const Sheet *sheet)
{
#if 0
	static gboolean warned = FALSE;
	if (!warned)
		g_warning ("Implement Me !. expr_name_invalidate_refs_sheet\n");
	warned = TRUE;
#endif
}

void
expr_name_invalidate_refs_wb (const Workbook *wb)
{
#if 0
	static gboolean warned = FALSE;
	if (!warned)
		g_warning ("Implement Me !. expr_name_invalidate_refs_wb\n");
	warned = TRUE;
#endif
}

static void
cb_collect_name_deps (gpointer key, gpointer value, gpointer user_data)
{
	GSList **list = user_data;
	*list = g_slist_prepend (*list, key);
}

static GSList *
expr_name_unlink_deps (NamedExpression *nexpr)
{
	GSList *ptr, *deps = NULL;
	g_hash_table_foreach (nexpr->dependents, cb_collect_name_deps, &deps);

	/* pull them out */
	for (ptr = deps ; ptr != NULL ; ptr = ptr->next) {
		Dependent *dep = ptr->data;
		if (dependent_is_linked (dep)) {
			CellPos *pos = dependent_is_cell (dep)
				? &(DEP_TO_CELL (dep)->pos) : NULL;
			dependent_unlink (dep, pos);
		}
	}
	return deps;
}

static void
expr_name_link_deps (GSList *deps)
{
	GSList *ptr = deps;

	/* put them back */
	for (; ptr != NULL ; ptr = ptr->next) {
		Dependent *dep = ptr->data;
		if (!dependent_is_linked (dep)) {
			CellPos *pos = dependent_is_cell (dep)
				? &(DEP_TO_CELL (dep)->pos) : NULL;
			dependent_link (dep, pos);
			cb_dependent_queue_recalc (dep, NULL);
		}
	}

	g_slist_free (deps);
}

/**
 * expr_name_set_scope:
 * @expression: 
 * @sheet: 
 * 
 * Return Value: FALSE or error, TRUE otherwise
 **/
gboolean
expr_name_set_scope (NamedExpression *expression, Sheet *sheet)
{
	Workbook *wb;

	g_return_val_if_fail (IS_SHEET (expression->pos.sheet), FALSE);
	
	sheet = expression->pos.sheet;
	wb = sheet->workbook;
	
	expression->pos.sheet = NULL;
	expression->pos.wb = wb;

	wb->names    = g_list_prepend (wb->names, expression);
	sheet->names = g_list_remove (sheet->names, expression);

	sheet_set_dirty (sheet, TRUE);

	return TRUE;
}
	
void
expr_name_set_expr (NamedExpression *nexpr, ExprTree *new_expr)
{
	GSList *deps = NULL;

	g_return_if_fail (nexpr != NULL);
	g_return_if_fail (!nexpr->builtin);

	deps = expr_name_unlink_deps (nexpr);
	expr_tree_ref (new_expr);
	expr_tree_unref (nexpr->t.expr_tree);
	nexpr->t.expr_tree = new_expr;
	expr_name_link_deps (deps);
}

void
expr_name_add_dep (NamedExpression *nexpr, Dependent *dep)
{
	g_hash_table_insert (nexpr->dependents, dep, dep);
}

void
expr_name_remove_dep (NamedExpression *nexpr, Dependent *dep)
{
	g_hash_table_remove (nexpr->dependents, dep);
}


/* ------------------------------------------------------------- */

static Value *
name_print_area (FunctionEvalInfo *ei, Value **args)
{
	if (!ei || !ei->pos->sheet)
		return value_new_error (ei->pos, _("Error: no sheet"));
	else {
		Range r = sheet_get_extent (ei->pos->sheet, FALSE);
		sheet_style_get_extent (ei->pos->sheet, &r);
		return value_new_cellrange_r (ei->pos->sheet, &r);
	}
}

static Value *
name_sheet_title (FunctionEvalInfo *ei, Value **args)
{
	if (!ei || !ei->pos->sheet || !ei->pos->sheet->name_quoted)
		return value_new_error (ei->pos, _("Error: no sheet"));
	else
		return value_new_string (ei->pos->sheet->name_quoted);
}

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
	for (; builtins[lp].name ; lp++) {
		NamedExpression *expr_name;
		expr_name = named_expr_new (builtins[lp].name, TRUE);
		expr_name->t.expr_func = builtins[lp].fn;
		global_names = g_list_append (global_names, expr_name);
	}
}

/******************************************************************************/
/**
 * sheet_get_available_names :
 * A convenience routine to get the list of names associated with @sheet and its
 * workbook.
 *
 * The caller is responsible for freeing the list.
 * Names in the list do NOT have additional references added.
 */
GList *
sheet_get_available_names (Sheet const *sheet)
{
	GList *l = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	l = g_list_copy (sheet->names);
	return g_list_concat (l, g_list_copy (sheet->workbook->names));
}

