/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * expr-name.c: Supported named expressions
 *
 * Author:
 *    Jody Goldberg <jody@gnome.org>
 *
 * Based on work by:
 *    Michael Meeks <michael@ximian.com>
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "expr-name.h"

#include "dependent.h"
#include "cell.h"
#include "value.h"
#include "workbook-priv.h"
#include "expr.h"
#include "expr-impl.h"
#include "str.h"
#include "sheet.h"
#include "ranges.h"
#include "sheet-style.h"

#include <gdk/gdkkeysyms.h>

/* We don't expect that many global names ! */
static GList *global_names = NULL;

static void
cb_collect_name_deps (gpointer key, gpointer value, gpointer user_data)
{
	GSList **list = user_data;
	*list = g_slist_prepend (*list, key);
}

static GSList *
expr_name_unlink_deps (GnmNamedExpr *nexpr)
{
	GSList *ptr, *deps = NULL;

	if (nexpr->dependents == NULL)
		return NULL;

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

/**
 * expr_name_link_deps :
 * @deps :
 * @rwinfo : optionally NULL
 *
 * relink the depenents of this name, BUT if the optional @rwinfo is specified
 * and we are invalidating a sheet or workbook don't bother to relink things
 * in the same sheet or workbook.
 */
static void
expr_name_link_deps (GSList *deps, GnmExprRewriteInfo const *rwinfo)
{
	GSList *ptr = deps;

	/* put them back */
	for (; ptr != NULL ; ptr = ptr->next) {
		Dependent *dep = ptr->data;
		if (rwinfo != NULL) {
			if (rwinfo->type == GNM_EXPR_REWRITE_WORKBOOK) {
				if (rwinfo->u.workbook == dep->sheet->workbook)
					continue;
			} else if (rwinfo->type == GNM_EXPR_REWRITE_SHEET)
				if (rwinfo->u.sheet == dep->sheet)
					continue;
		}
		if (dep->sheet->deps != NULL && !dependent_is_linked (dep)) {
			CellPos *pos = dependent_is_cell (dep)
				? &(DEP_TO_CELL (dep)->pos) : NULL;
			dependent_link (dep, pos);
			dependent_queue_recalc (dep);
		}
	}

	g_slist_free (deps);
}

/**
 * expr_name_handle_references : register or unregister a name with
 *    all of the sheets it explicitly references.  This is necessary
 *    beacuse names are not dependents, and if they reference a deleted
 *    sheet we will not notice.
 */
static void
expr_name_handle_references (GnmNamedExpr *nexpr, gboolean add)
{
	GSList *sheets, *ptr;

	sheets = gnm_expr_referenced_sheets (nexpr->t.expr_tree);
	for (ptr = sheets ; ptr != NULL ; ptr = ptr->next) {
		Sheet *sheet = ptr->data;
		GnmNamedExpr *found;

		/* No need to do anything during destruction */
		if (sheet->deps == NULL)
			continue;

		found = g_hash_table_lookup (sheet->deps->referencing_names, nexpr);
		if (add) {
			if (found == NULL)  {
				g_hash_table_insert (sheet->deps->referencing_names, nexpr, nexpr);
			} else {
				g_warning ("Name being registered multiple times ?");
			}
		} else {
			if (found == NULL)  {
				g_warning ("Unregistered name being being removed ?");
			} else {
				g_hash_table_remove (sheet->deps->referencing_names, nexpr);
			}
		}
	}
	g_slist_free (sheets);
}


static GnmNamedExpr *
expr_name_lookup_list (GList *p, char const *name)
{
	for (; p ; p = p->next) {
		GnmNamedExpr *nexpr = p->data;
		g_return_val_if_fail (nexpr != NULL, 0);
		/* This is inconsistent with XL, but not too bad since it will
		 * not effect import.  It is required to support Applix names
		 * which are case sensitive.
		 */
		if (strcmp (nexpr->name->str, name) == 0)
			return nexpr;
	}
	return NULL;
}

/**
 * expr_name_lookup:
 * @pp :
 * @name :
 *
 * lookup but do not reference a named expression.
 */
/* FIXME : Why not use hash tables ? */
GnmNamedExpr *
expr_name_lookup (ParsePos const *pp, char const *name)
{
	GnmNamedExpr *res = NULL;
	Sheet const *sheet = NULL;
	Workbook const *wb = NULL;

	g_return_val_if_fail (name != NULL, NULL);

	if (pp != NULL) {
		sheet = pp->sheet;
		wb = (sheet != NULL) ? sheet->workbook : pp->wb;
	}

	if (sheet != NULL)
		res = expr_name_lookup_list (sheet->names, name);
	if (res == NULL && wb != NULL)
		res = expr_name_lookup_list (wb->names, name);
	if (res == NULL)
		res = expr_name_lookup_list (global_names, name);
	return res;
}

/**
 * expr_name_new :
 * 
 * Creates a new name without linking it into any container.
 */
GnmNamedExpr *
expr_name_new (char const *name, gboolean builtin)
{
	GnmNamedExpr *nexpr;

	g_return_val_if_fail (name != NULL, NULL);

	nexpr = g_new0 (GnmNamedExpr,1);

	nexpr->ref_count = 1;
	nexpr->builtin = builtin;
	nexpr->active  = FALSE;
	nexpr->name    = string_get (name);
	nexpr->t.expr_tree = NULL;
	nexpr->dependents  = NULL;

	g_return_val_if_fail (nexpr->name != NULL, NULL);

	return nexpr;
}

/*
 * NB. if we already have a circular reference in addition
 * to this one we are checking we will come to serious grief.
 */
static gboolean
name_refer_circular (char const *name, GnmExpr const *expr)
{
	g_return_val_if_fail (expr != NULL, TRUE);

	switch (expr->any.oper) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		return (name_refer_circular (name, expr->binary.value_a) ||
			name_refer_circular (name, expr->binary.value_b));
	case GNM_EXPR_OP_ANY_UNARY:
		return name_refer_circular (name, expr->unary.value);
	case GNM_EXPR_OP_NAME: {
		GnmNamedExpr const *nexpr = expr->name.name;
		if (nexpr->builtin)
			return FALSE;

		if (!strcmp (nexpr->name->str, name))
			return TRUE;

		/* And look inside this name tree too */
		return name_refer_circular (name, nexpr->t.expr_tree);
	}
	case GNM_EXPR_OP_FUNCALL: {
		GnmExprList *l = expr->func.arg_list;
		for (; l ; l = l->next)
			if (name_refer_circular (name, l->data))
				return TRUE;
		break;
	}
	case GNM_EXPR_OP_CONSTANT:
	case GNM_EXPR_OP_CELLREF:
	case GNM_EXPR_OP_ARRAY:
		break;
	case GNM_EXPR_OP_SET: {
		GnmExprList *l = expr->set.set;
		for (; l ; l = l->next)
			if (name_refer_circular (name, l->data))
				return TRUE;
		break;
	}
	}
	return FALSE;
}

/**
 * expr_name_add:
 * @pp:
 * @name:
 * @expr: if expr == NULL then create a placeholder with value #NAME?
 * @error_msg:
 *
 * Absorbs the reference to @expr.
 **/
GnmNamedExpr *
expr_name_add (ParsePos const *pp, char const *name,
	       GnmExpr const *expr, char const **error_msg)
{
	GnmNamedExpr *nexpr;
	GList **scope = NULL;

	g_return_val_if_fail (pp != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	/* create a placeholder */
	if (expr == NULL)
		expr = gnm_expr_new_constant (value_new_error (NULL,
			gnumeric_err_NAME));

/*	printf ("Adding name '%s' to %p %p\n", name, wb, sheet);*/
	if (pp->sheet != NULL) {
		scope = &(pp->sheet->names);
		if (error_msg)
			*error_msg = _("already defined in sheet");
	} else if (pp->wb != NULL) {
		scope = &(pp->wb->names);
		if (error_msg)
			*error_msg = _("already defined in workbook");
	} else {
		scope = &global_names;
		if (error_msg)
			*error_msg = _("already globally defined ");
	}

	if (expr_name_lookup_list (*scope, name) != NULL) {
		gnm_expr_unref (expr);
		return NULL;
	} else if (name_refer_circular (name, expr)) {
		gnm_expr_unref (expr);
		*error_msg = _("'%s' has a circular reference");
		return NULL;
	}
	if (error_msg)
		*error_msg = NULL;

	nexpr = expr_name_new (name, FALSE);
	parse_pos_init (&nexpr->pos,
			pp->wb, pp->sheet, pp->eval.col, pp->eval.row);
	expr_name_set_expr (nexpr, expr, NULL);

	*scope = g_list_append (*scope, nexpr);
	nexpr->active = TRUE;

	return nexpr;
}

void
expr_name_ref (GnmNamedExpr *nexpr)
{
	g_return_if_fail (nexpr != NULL);

	nexpr->ref_count++;
}

void
expr_name_unref (GnmNamedExpr *nexpr)
{
	g_return_if_fail (nexpr != NULL);

	if (nexpr->ref_count-- > 1)
		return;

	g_return_if_fail (!nexpr->active);

	if (nexpr->name) {
		string_unref (nexpr->name);
		nexpr->name = NULL;
	}

	if (!nexpr->builtin && nexpr->t.expr_tree != NULL)
		expr_name_set_expr (nexpr, NULL, NULL);

	if (nexpr->dependents != NULL) {
		g_hash_table_destroy (nexpr->dependents);
		nexpr->dependents  = NULL;
	}

	nexpr->pos.wb      = NULL;
	nexpr->pos.sheet   = NULL;

	g_free (nexpr);
}

/**
 * expr_name_remove :
 * @nexpr :
 *
 * Remove a @nexpr from its container and deactivate it.
 * NOTE : @nexpr may continue to exist if things still have refrences to it,
 * but they will evaluate to #REF!
 **/
void
expr_name_remove (GnmNamedExpr *nexpr)
{
	g_return_if_fail (nexpr != NULL);
	g_return_if_fail (nexpr->active);

	if (nexpr->pos.sheet) {
		Sheet *sheet = nexpr->pos.sheet;
		g_return_if_fail (g_list_find (sheet->names, nexpr) != NULL);
		sheet->names = g_list_remove (sheet->names, nexpr);
	} else if (nexpr->pos.wb) {
		Workbook *wb = nexpr->pos.wb;
		g_return_if_fail (g_list_find (wb->names, nexpr) != NULL);
		wb->names = g_list_remove (wb->names, nexpr);
	} else {
		g_return_if_fail (g_list_find (global_names, nexpr) != NULL);
		global_names = g_list_remove (global_names, nexpr);
	}
	nexpr->active = FALSE;
	expr_name_set_expr (nexpr, NULL, NULL);
	expr_name_unref (nexpr);
}

/**
 * expr_name_list_destroy :
 * @names : a POINTER to the list of names
 *
 * Frees names in the local scope.
 * NOTE : THIS DOES NOT INVALIDATE NAMES THAT REFER
 *        TO THIS SCOPE.
 *        eg
 *           in scope sheet2 we have a name that refers
 *           to sheet1.  That will remain!
 **/
void
expr_name_list_destroy (GList **names)
{
	GList *ptr, *list = *names;

	*names = NULL;
	for (ptr = list ; ptr != NULL ; ) {
		GnmNamedExpr *nexpr = ptr->data;
		ptr = ptr->next;
		if (nexpr->active) {
			nexpr->active = FALSE;
			if (!nexpr->builtin)
				expr_name_set_expr (nexpr, NULL, NULL);
			expr_name_unref (nexpr);
		} else {
			g_warning ("problem with named expressions");
		}
	}
	g_list_free (list);
}

/**
 * expr_name_as_string :
 * @nexpr :
 * @pp : optionally null.
 *
 * returns a string that the caller needs to free.
 */
char *
expr_name_as_string (GnmNamedExpr const *nexpr, ParsePos const *pp)
{
	if (nexpr->builtin)
		return g_strdup (_("Builtin"));

	if (pp == NULL)
		pp = &nexpr->pos;
	return gnm_expr_as_string (nexpr->t.expr_tree, pp);
}

Value *
expr_name_eval (GnmNamedExpr const *nexpr, EvalPos const *pos,
		GnmExprEvalFlags flags)
{
	g_return_val_if_fail (pos, NULL);

	if (!nexpr)
		return value_new_error (pos, gnumeric_err_NAME);

	if (nexpr->builtin) {
		FunctionEvalInfo ei;
		ei.pos = pos;
		ei.func_call = NULL; /* FIXME : This is ugly. why are there
					no descriptors for builtins */
		return (*nexpr->t.expr_func) (&ei, NULL);
	}

	return gnm_expr_eval (nexpr->t.expr_tree, pos, flags);
}

/*******************************************************************
 * Manage things that depend on named expressions.
 */
/**
 * expr_name_set_scope:
 * @nexpr:
 * @sheet:
 *
 * Return Value: FALSE or error, TRUE otherwise
 **/
gboolean
expr_name_set_scope (GnmNamedExpr *nexpr, Sheet *sheet)
{
	Workbook *wb;

	g_return_val_if_fail (nexpr != NULL, FALSE);

	if (sheet == NULL) {
		g_return_val_if_fail (IS_SHEET (nexpr->pos.sheet), FALSE);

		sheet->names = g_list_remove (nexpr->pos.sheet->names, nexpr);

		wb = nexpr->pos.sheet->workbook;
		wb->names    = g_list_prepend (wb->names, nexpr);
		nexpr->pos.wb = wb;
		nexpr->pos.sheet = NULL;
	} else {
		g_return_val_if_fail (nexpr->pos.sheet == NULL, FALSE);
		g_return_val_if_fail (IS_SHEET (sheet), FALSE);

		wb = sheet->workbook;
		nexpr->pos.wb = NULL;
		nexpr->pos.sheet = sheet;
		wb->names    = g_list_remove (wb->names, nexpr);
		sheet->names = g_list_prepend (sheet->names, nexpr);
	}
	workbook_set_dirty (wb, TRUE);

	return TRUE;
}

/**
 * expr_name_set_expr :
 * @nexpr : the named expression
 * @new_expr : the new content
 * @rwinfo : optional.
 *
 * Unrefs the current content of @nexpr and absorbs a ref to @new_expr.
 **/
void
expr_name_set_expr (GnmNamedExpr *nexpr, GnmExpr const *new_expr,
		    GnmExprRewriteInfo const *rwinfo)
{
	GSList *deps = NULL;

	g_return_if_fail (nexpr != NULL);
	g_return_if_fail (!nexpr->builtin);

	if (new_expr == nexpr->t.expr_tree)
		return;
	if (nexpr->t.expr_tree != NULL) {
		deps = expr_name_unlink_deps (nexpr);
		expr_name_handle_references (nexpr, FALSE);
		gnm_expr_unref (nexpr->t.expr_tree);
	}
	nexpr->t.expr_tree = new_expr;
	expr_name_link_deps (deps, rwinfo);

	if (new_expr != NULL)
		expr_name_handle_references (nexpr, TRUE);
}

void
expr_name_add_dep (GnmNamedExpr *nexpr, Dependent *dep)
{
	if (nexpr->dependents == NULL)
		nexpr->dependents = g_hash_table_new (g_direct_hash,
						      g_direct_equal);

	g_hash_table_insert (nexpr->dependents, dep, dep);
}

void
expr_name_remove_dep (GnmNamedExpr *nexpr, Dependent *dep)
{
	g_return_if_fail (nexpr->dependents != NULL);

	g_hash_table_remove (nexpr->dependents, dep);
}

/**
 * expr_name_is_placeholder :
 * @ne :
 *
 * Returns TRUE if @ne is a placeholder for an unknown name
 **/
gboolean
expr_name_is_placeholder (GnmNamedExpr const *nexpr)
{
	g_return_val_if_fail (nexpr != NULL, FALSE);
	return !nexpr->builtin &&
		gnm_expr_is_err (nexpr->t.expr_tree, gnumeric_err_NAME);
}

/* ---------------------------------------------------------------------- */

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
	gchar const *name;
	GnmFuncArgs fn;
} const builtins[] =
{
	/* Consolidate_Area
	   Auto_Open
	   Auto_Close
	   Extract
	   Database	just a standard name, no associated range
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
	int lp = 0;

	/* Not in global function table though ! */
	for (; builtins[lp].name ; lp++) {
		GnmNamedExpr *nexpr;
		nexpr = expr_name_new (builtins[lp].name, TRUE);
		nexpr->t.expr_func = builtins[lp].fn;
		nexpr->active = TRUE;
		global_names = g_list_append (global_names, nexpr);
	}
}

void
expr_name_shutdown (void)
{
	expr_name_list_destroy (&global_names);
}

/******************************************************************************/
/**
 * sheet_names_get_available :
 * A convenience routine to get the list of names associated with @sheet and its
 * workbook.
 *
 * The caller is responsible for freeing the list.
 * Names in the list do NOT have additional references added.
 */
GList *
sheet_names_get_available (Sheet const *sheet)
{
	GList *l = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	l = g_list_copy (sheet->names);
	return g_list_concat (l, g_list_copy (sheet->workbook->names));
}

static char const *
namelist_check (GList *ptr, Sheet const *sheet, Range const *r)
{
	Value *v;
	GnmNamedExpr *nexpr;

	for (; ptr != NULL ; ptr = ptr->next) {
		nexpr = ptr->data;
		if (!nexpr->active || nexpr->builtin)
			continue;
		v = gnm_expr_get_range (nexpr->t.expr_tree);
		if (v != NULL) {
			if (v->type == VALUE_CELLRANGE) {
				RangeRef const *ref = &v->v_range.cell;
				if (!ref->a.col_relative &&
				    !ref->a.row_relative &&
				    !ref->b.col_relative &&
				    !ref->b.row_relative &&
				    eval_sheet (ref->a.sheet, sheet) == sheet &&
				    eval_sheet (ref->b.sheet, sheet) == sheet &&
				    MIN (ref->a.col, ref->b.col) == r->start.col &&
				    MAX (ref->a.col, ref->b.col) == r->end.col &&
				    MIN (ref->a.row, ref->b.row) == r->start.row &&
				    MAX (ref->a.row, ref->b.row) == r->end.row) {
					value_release (v);
					return nexpr->name->str;
				}
			}
			value_release (v);
		}
	}
	return NULL;
}

/**
 * sheet_names_check :
 * @sheet :
 * @r :
 *
 * Returns a constant string if @sheet!@r is the target of a named range.
 * Preference is given to workbook scope over sheet.
 **/
char const *
sheet_names_check (Sheet const *sheet, Range const *r)
{
	char const *res;
	Range tmp;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (r != NULL, NULL);

	tmp = *r;
	range_normalize (&tmp);
	res = namelist_check (sheet->workbook->names, sheet, &tmp);
	if (res != NULL)
		return res;
	return namelist_check (sheet->names, sheet, &tmp);
}
