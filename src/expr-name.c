/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * expr-name.c:  Workbook name table support, will deal
 *               with all the exotic special names.
 *
 * Author:
 *    Michael Meeks <michael@ximian.com>
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "expr-name.h"

#include "eval.h"
#include "cell.h"
#include "value.h"
#include "workbook.h"
#include "expr.h"
#include "str.h"
#include "sheet.h"
#include "sheet-style.h"

#include <gdk/gdkkeysyms.h>
#include <libgnome/gnome-i18n.h>

/* We don't expect that many global names ! */
static GList *global_names = NULL;

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
		if (dep->sheet->deps != NULL && !dependent_is_linked (dep)) {
			CellPos *pos = dependent_is_cell (dep)
				? &(DEP_TO_CELL (dep)->pos) : NULL;
			dependent_link (dep, pos);
			cb_dependent_queue_recalc (dep, NULL);
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
expr_name_handle_references (NamedExpression *nexpr, gboolean add)
{
	GSList *sheets, *ptr;

	sheets = expr_tree_referenced_sheets (nexpr->t.expr_tree);
	for (ptr = sheets ; ptr != NULL ; ptr = ptr->next) {
		Sheet *sheet = ptr->data;
		NamedExpression *found;

		/* No need to do anything during destruction */
		if (sheet->deps == NULL)
			continue;

		found = g_hash_table_lookup (sheet->deps->names, nexpr);
		if (add) {
			if (found == NULL)  {
				g_hash_table_insert (sheet->deps->names, nexpr, nexpr);
			} else {
				g_warning ("Name being registered multiple times ?");
			}
		} else {
			if (found == NULL)  {
				g_warning ("Unregistered name being being removed ?");
			} else {
				g_hash_table_remove (sheet->deps->names, nexpr);
			}
		}
	}
	g_slist_free (sheets);
}


static NamedExpression *
expr_name_lookup_list (GList *p, char const *name)
{
	for (; p ; p = p->next) {
		NamedExpression *nexpr = p->data;
		g_return_val_if_fail (nexpr != NULL, 0);
		if (g_strcasecmp (nexpr->name->str, name) == 0)
			return nexpr;
	}
	return NULL;
}

/* FIXME : Why not use hash tables ? */
NamedExpression *
expr_name_lookup (ParsePos const *pos, char const *name)
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
	NamedExpression *nexpr;

	g_return_val_if_fail (name != NULL, NULL);

	nexpr = g_new0 (NamedExpression,1);

	nexpr->ref_count = 1;
	nexpr->builtin = builtin;
	nexpr->active  = TRUE;
	nexpr->name    = string_get (name);
	nexpr->dependents =
		g_hash_table_new (g_direct_hash, g_direct_equal);

	g_return_val_if_fail (nexpr->name != NULL, NULL);

	return nexpr;
}

/*
 * NB. if we already have a circular reference in addition
 * to this one we are checking we will come to serious grief.
 */
static gboolean
name_refer_circular (char const *name, ExprTree *expr)
{
	g_return_val_if_fail (expr != NULL, TRUE);

	switch (expr->any.oper) {
	case OPER_ANY_BINARY:
		return (name_refer_circular (name, expr->binary.value_a) ||
			name_refer_circular (name, expr->binary.value_b));
	case OPER_ANY_UNARY:
		return name_refer_circular (name, expr->unary.value);
	case OPER_NAME: {
		NamedExpression const *nexpr = expr->name.name;
		if (nexpr->builtin)
			return FALSE;

		if (!g_strcasecmp (nexpr->name->str, name))
			return TRUE;

		/* And look inside this name tree too */
		return name_refer_circular (name, nexpr->t.expr_tree);
	}
	case OPER_FUNCALL: {
		ExprList *l = expr->func.arg_list;
		for (; l ; l = l->next)
			if (name_refer_circular (name, l->data))
				return TRUE;
		break;
	}
	case OPER_CONSTANT:
	case OPER_VAR:
	case OPER_ARRAY:
		break;
	case OPER_SET: {
		ExprList *l = expr->set.set;
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
 * @expr:
 * @error_msg:
 *
 * Absorbs the reference to @expr.
 **/
NamedExpression *
expr_name_add (ParsePos const *pp, char const *name,
	       ExprTree *expr, char const **error_msg)
{
	NamedExpression *nexpr;
	GList **scope = NULL;

	g_return_val_if_fail (pp != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (expr != NULL, NULL);

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

	if (expr_name_lookup_list (*scope, name) != NULL)
		return NULL;
	if (name_refer_circular (name, expr)) {
		*error_msg = _("'%s' has a circular reference");
		return NULL;
	}
	if (error_msg)
		*error_msg = NULL;

	nexpr = named_expr_new (name, FALSE);
	parse_pos_init (&nexpr->pos,
			pp->wb, pp->sheet, pp->eval.col, pp->eval.row);
	expr_name_set_expr (nexpr, expr);

	*scope = g_list_append (*scope, nexpr);

	return nexpr;
}

/**
 * expr_name_create:
 * @pp:
 * @name:
 * @value:
 * @error:
 *
 * Parses a texual name in @value, and enters the value
 * either as a workbook name if @sheet == NULL or a sheet
 * name if @wb == NULL.
 *
 * Return value: The created NamedExpression.
 **/
NamedExpression *
expr_name_create (ParsePos const *pp, char const *name,
		  char const *value, ParseError *error)
{
	NamedExpression *res;
	char const *err = NULL;
	ExprTree *tree;
	
	tree = expr_parse_str (value, pp, GNM_PARSER_DEFAULT, NULL, error);
	if (!tree)
		return NULL;

	/* We know there has been no parse error, but set the
	 * use the message part of the struct to pass a name
	 * creation error back to the calling routine
	 */
	res = expr_name_add (pp, name, tree, &err);
	if (err != NULL)
		error->message = g_strdup (err);
	return res;
}

void
expr_name_ref (NamedExpression *nexpr)
{
	g_return_if_fail (nexpr != NULL);

	nexpr->ref_count++;
}

void
expr_name_unref (NamedExpression *nexpr)
{
	g_return_if_fail (nexpr != NULL);

	if (nexpr->ref_count-- > 1)
		return;

	if (nexpr->name) {
		string_unref (nexpr->name);
		nexpr->name = NULL;
	}

	if (!nexpr->builtin && nexpr->t.expr_tree != NULL)
		expr_name_set_expr (nexpr, NULL);

	if (nexpr->dependents != NULL) {
		g_hash_table_destroy (nexpr->dependents);
		nexpr->dependents  = NULL;
	}

	nexpr->pos.wb      = NULL;
	nexpr->pos.sheet   = NULL;

	g_free (nexpr);
}

/**
 * expr_name_unlink :
 *
 * Linked names can be looked up and used.  It is possible to have a non-zero
 * ref count and be unlinked.
 */
static void
expr_name_unlink (NamedExpression *nexpr)
{
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
	expr_name_unref (nexpr);
	nexpr->active = FALSE;
}

void
expr_name_remove (NamedExpression *nexpr)
{
	g_return_if_fail (nexpr != NULL);
	g_return_if_fail (nexpr->active);

	/* Ref it so that we can clear it even if it is unused */
	expr_name_ref (nexpr);
	expr_name_unlink (nexpr);
	expr_name_set_expr (nexpr, NULL);
	expr_name_unref (nexpr);
}

/**
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
		NamedExpression *nexpr = p->data;
		nexpr->active = FALSE;
		expr_name_unref (nexpr);
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
expr_name_eval (NamedExpression const *nexpr,
		EvalPos const *pos, ExprEvalFlags flags)
{
	g_return_val_if_fail (pos, NULL);

	if (!nexpr)
		return value_new_error (pos, gnumeric_err_NAME);

	if (nexpr->builtin) {
		FunctionEvalInfo ei;
		ei.pos = pos;
		ei.func_def = NULL; /* FIXME : This is ugly. why are there
					no descriptors for builtins */
		return nexpr->t.expr_func (&ei, NULL);
	}

	return expr_eval (nexpr->t.expr_tree, pos, flags);
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
expr_name_set_scope (NamedExpression *nexpr, Sheet *sheet)
{
	Workbook *wb;

	g_return_val_if_fail (IS_SHEET (nexpr->pos.sheet), FALSE);
	
	sheet = nexpr->pos.sheet;
	wb = sheet->workbook;
	
	nexpr->pos.sheet = NULL;
	nexpr->pos.wb = wb;

	wb->names    = g_list_prepend (wb->names, nexpr);
	sheet->names = g_list_remove (sheet->names, nexpr);

	sheet_set_dirty (sheet, TRUE);

	return TRUE;
}
	
void
expr_name_set_expr (NamedExpression *nexpr, ExprTree *new_expr)
{
	GSList *deps = NULL;

	g_return_if_fail (nexpr != NULL);
	g_return_if_fail (!nexpr->builtin);

	if (new_expr != NULL)
		expr_tree_ref (new_expr);
	if (nexpr->t.expr_tree != NULL) {
		deps = expr_name_unlink_deps (nexpr);
		expr_name_handle_references (nexpr, FALSE);
		expr_tree_unref (nexpr->t.expr_tree);
	}
	nexpr->t.expr_tree = new_expr;
	expr_name_link_deps (deps);
	if (new_expr != NULL)
		expr_name_handle_references (nexpr, TRUE);
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
		NamedExpression *nexpr;
		nexpr = named_expr_new (builtins[lp].name, TRUE);
		nexpr->t.expr_func = builtins[lp].fn;
		global_names = g_list_append (global_names, nexpr);
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

