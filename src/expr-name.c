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
#include <string.h>
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
#include "gutils.h"
#include "sheet-style.h"

#include <gdk/gdkkeysyms.h>

static void
cb_nexpr_remove (GnmNamedExpr *nexpr)
{
	g_return_if_fail (nexpr->active);

	nexpr->active = FALSE;
	expr_name_set_expr (nexpr, NULL);
	expr_name_unref (nexpr);
}

static GnmNamedExprCollection *
gnm_named_expr_collection_new (void)
{
	GnmNamedExprCollection *res = g_new (GnmNamedExprCollection, 1);

	res->names = g_hash_table_new_full (g_str_hash, g_str_equal,
		NULL, (GDestroyNotify) cb_nexpr_remove);
	res->placeholders = g_hash_table_new_full (g_str_hash, g_str_equal,
		NULL, (GDestroyNotify) cb_nexpr_remove);

	return res;
}

/**
 * gnm_named_expr_collection_free :
 * @names : a POINTER to the collection of names
 *
 * Frees names in the local scope.
 * NOTE : THIS DOES NOT INVALIDATE NAMES THAT REFER
 *        TO THIS SCOPE.
 *        eg
 *           in scope sheet2 we have a name that refers
 *           to sheet1.  That will remain!
 **/
void
gnm_named_expr_collection_free (GnmNamedExprCollection **names)
{
	if (*names != NULL) {
		g_hash_table_destroy ((*names)->names);
		g_hash_table_destroy ((*names)->placeholders);
		g_free (*names);
		*names = NULL;
	}
}

static GnmNamedExpr *
gnm_named_expr_collection_lookup (GnmNamedExprCollection const *scope,
				  char const *name)
{
	GnmNamedExpr *nexpr = g_hash_table_lookup (scope->names, name);
	if (nexpr == NULL)
		nexpr = g_hash_table_lookup (scope->placeholders, name);
	return nexpr;
}

static void
gnm_named_expr_collection_insert (GnmNamedExprCollection const *scope,
				  GnmNamedExpr *nexpr)
{
	g_return_if_fail (!nexpr->active);

	nexpr->active = TRUE;
	g_hash_table_replace (nexpr->is_placeholder
	      ? scope->placeholders : scope->names, nexpr->name->str, nexpr);
}

typedef struct {
	Sheet const *sheet;
	Range const *r;
	GnmNamedExpr *res;
} CheckName;

static void
cb_check_name (G_GNUC_UNUSED gpointer key, GnmNamedExpr *nexpr,
	       CheckName *user)
{
	Value *v;

	if (!nexpr->active || nexpr->is_hidden)
		return;

	v = gnm_expr_get_range (nexpr->expr);
	if (v != NULL) {
		if (v->type == VALUE_CELLRANGE) {
			RangeRef const *ref = &v->v_range.cell;
			if (!ref->a.col_relative &&
			    !ref->b.col_relative &&
			    !ref->a.row_relative &&
			    !ref->b.row_relative &&
			    eval_sheet (ref->a.sheet, user->sheet) == user->sheet &&
			    eval_sheet (ref->b.sheet, user->sheet) == user->sheet &&
			    MIN (ref->a.col, ref->b.col) == user->r->start.col &&
			    MAX (ref->a.col, ref->b.col) == user->r->end.col &&
			    MIN (ref->a.row, ref->b.row) == user->r->start.row &&
			    MAX (ref->a.row, ref->b.row) == user->r->end.row)
				user->res = nexpr;
		}
		value_release (v);
	}
}
static GnmNamedExpr *
gnm_named_expr_collection_check (GnmNamedExprCollection *scope,
				 Sheet const *sheet, Range const *r)
{
	CheckName user;

	if (scope == NULL)
		return NULL;

	user.sheet = sheet;
	user.r	   = r;
	user.res   = NULL;

	g_hash_table_foreach (scope->names,
		(GHFunc) cb_check_name, &user);
	return user.res;
}

/******************************************************************************/

static void
cb_collect_name_deps (gpointer key, G_GNUC_UNUSED gpointer value,
		      gpointer user_data)
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
		if (dependent_is_linked (dep))
			dependent_unlink (dep, NULL);
	}
	return deps;
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

	sheets = gnm_expr_referenced_sheets (nexpr->expr);
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


/**
 * expr_name_lookup:
 * @pp :
 * @name :
 *
 * lookup but do not reference a named expression.
 */
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

	if (sheet != NULL && sheet->names != NULL)
		res = gnm_named_expr_collection_lookup (sheet->names, name);
	if (res == NULL && wb != NULL && wb->names != NULL)
		res = gnm_named_expr_collection_lookup (wb->names, name);
	return res;
}

/**
 * expr_name_new :
 * 
 * Creates a new name without linking it into any container.
 */
static GnmNamedExpr *
expr_name_new (char const *name, gboolean is_placeholder)
{
	GnmNamedExpr *nexpr;

	g_return_val_if_fail (name != NULL, NULL);

	nexpr = g_new0 (GnmNamedExpr,1);

	nexpr->ref_count	= 1;
	nexpr->active		= FALSE;
	nexpr->name		= string_get (name);
	nexpr->expr		= NULL;
	nexpr->dependents	= NULL;
	nexpr->is_placeholder	= is_placeholder;
	nexpr->is_hidden	= FALSE;

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
		if (!strcmp (nexpr->name->str, name))
			return TRUE;

		/* And look inside this name tree too */
		return name_refer_circular (name, nexpr->expr);
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
 * @link_to_container:
 *
 * Absorbs the reference to @expr.
 * If @error_msg is non NULL it may hold a pointer to a translated descriptive
 * string.  NOTE : caller is responsible for freeing the error message.
 *
 * The reference semantics of the new expression are
 * 1) new names with @link_to_container TRUE are referenced by the container.
 *    The caller DOES NOT OWN a reference to the result, and needs to add their
 *    own.
 * 2) if @link_to_container is FALSE the the caller DOES OWN a reference, and
 *    can free the result by unrefing the name.
 **/
GnmNamedExpr *
expr_name_add (ParsePos const *pp, char const *name,
	       GnmExpr const *expr, char **error_msg,
	       gboolean link_to_container)
{
	GnmNamedExpr *nexpr;
	GnmNamedExprCollection *scope = NULL;

	g_return_val_if_fail (pp != NULL, NULL);
	g_return_val_if_fail (pp->sheet != NULL || pp->wb != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	if (expr != NULL && name_refer_circular (name, expr)) {
		gnm_expr_unref (expr);
		if (error_msg)
			*error_msg = g_strdup_printf (_("'%s' has a circular reference"), name);
		return NULL;
	}

	if (pp->sheet != NULL) {
		if (pp->sheet->names == NULL)
			pp->sheet->names = gnm_named_expr_collection_new ();
		scope = pp->sheet->names;
	} else {
		if (pp->wb->names == NULL)
			pp->wb->names = gnm_named_expr_collection_new ();
		scope = pp->wb->names;
	}

	/* see if there was a place holder */
	nexpr = g_hash_table_lookup (scope->placeholders, name);
	if (nexpr != NULL) {
		if (expr == NULL) {
			/* there was already a placeholder for this */
			expr_name_ref (nexpr);
			return nexpr;
		}

		/* convert the placeholder into a real name */
		g_hash_table_remove (scope->placeholders, nexpr->name);
		nexpr->is_placeholder = FALSE;
	} else {
		nexpr = g_hash_table_lookup (scope->names, name);
		if (nexpr != NULL) {
			if (error_msg != NULL) {
				*error_msg = (pp->sheet != NULL)
					? g_strdup_printf (_("'%s' is already defined in sheet"), name)
					: g_strdup_printf (_("'%s' is already defined in workbook"), name);
			}
			gnm_expr_unref (expr);
			return NULL;
		}
	}

	if (error_msg)
		*error_msg = NULL;

	if (nexpr == NULL)
		nexpr = expr_name_new (name, expr == NULL);
	parse_pos_init (&nexpr->pos,
		pp->wb, pp->sheet, pp->eval.col, pp->eval.row);
	if (expr == NULL)
		expr = gnm_expr_new_constant (value_new_error_NAME (NULL));
	expr_name_set_expr (nexpr, expr);
	if (link_to_container)
		gnm_named_expr_collection_insert (scope, nexpr);

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

	if (nexpr->expr != NULL)
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
	GnmNamedExprCollection *scope;

	g_return_if_fail (nexpr != NULL);
	g_return_if_fail (nexpr->pos.sheet != NULL || nexpr->pos.wb != NULL);
	g_return_if_fail (nexpr->active);

	scope = (nexpr->pos.sheet != NULL)
		? nexpr->pos.sheet->names : nexpr->pos.wb->names;

	g_return_if_fail (scope != NULL);

	g_hash_table_remove (
		nexpr->is_placeholder ? scope->placeholders : scope->names,
		nexpr->name->str);
}

/**
 * expr_name_as_string :
 * @nexpr :
 * @pp : optionally null.
 *
 * returns a string that the caller needs to free.
 */
char *
expr_name_as_string (GnmNamedExpr const *nexpr, ParsePos const *pp,
		     GnmExprConventions const *fmt)
{
	if (pp == NULL)
		pp = &nexpr->pos;
	return gnm_expr_as_string (nexpr->expr, pp, fmt);
}

Value *
expr_name_eval (GnmNamedExpr const *nexpr, EvalPos const *pos,
		GnmExprEvalFlags flags)
{
	g_return_val_if_fail (pos, NULL);

	if (!nexpr)
		return value_new_error_NAME (pos);

	return gnm_expr_eval (nexpr->expr, pos, flags);
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
	GnmNamedExprCollection *scope;

	g_return_val_if_fail (nexpr != NULL, FALSE);
	g_return_val_if_fail (nexpr->pos.sheet != NULL || nexpr->pos.wb != NULL, FALSE);
	g_return_val_if_fail (nexpr->active, FALSE);

	scope = (nexpr->pos.sheet != NULL)
		? nexpr->pos.sheet->names : nexpr->pos.wb->names;

	g_return_val_if_fail (scope != NULL, FALSE);

	g_hash_table_remove (
		nexpr->is_placeholder ? scope->placeholders : scope->names,
		nexpr->name->str);

	nexpr->pos.sheet = sheet;

	scope = (sheet != NULL) ? sheet->names : nexpr->pos.wb->names;
	gnm_named_expr_collection_insert (scope, nexpr);

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
expr_name_set_expr (GnmNamedExpr *nexpr, GnmExpr const *new_expr)
{
	GSList *deps = NULL;

	g_return_if_fail (nexpr != NULL);

	if (new_expr == nexpr->expr)
		return;
	if (nexpr->expr != NULL) {
		deps = expr_name_unlink_deps (nexpr);
		expr_name_handle_references (nexpr, FALSE);
		gnm_expr_unref (nexpr->expr);
	}
	nexpr->expr = new_expr;
	dependents_link (deps, NULL);

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
	return gnm_expr_is_err (nexpr->expr, GNM_ERROR_NAME);
}

int
expr_name_by_name (GnmNamedExpr const *a, GnmNamedExpr const *b)
{
	Sheet const *sheeta = a->pos.sheet;
	Sheet const *sheetb = b->pos.sheet;
	int res = 0;

	if (sheeta != sheetb) {
		/* Locals after non-locals.  */
		if (!sheeta || !sheetb)
			return (!sheeta) - (!sheetb);

		/* By non-local sheet order.  */
		res = g_utf8_collate (sheeta->name_case_insensitive,
				      sheetb->name_case_insensitive);
	}

	if (res == 0) {
		/* By name.  */
		res = gnumeric_utf8_collate_casefold (a->name->str, b->name->str);
	}

	return res;
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
static void
cb_get_names (G_GNUC_UNUSED gpointer key, GnmNamedExpr *nexpr,
	      GList **accum)
{
	if (!nexpr->is_hidden)
		*accum = g_list_prepend (*accum, nexpr);
}

/**
 * sheet_names_get_available :
 * @sheet :
 *
 * Gets the list of non hidden names available in the context @sheet.
 * Caller is responsible for freeing the list, but not its content.
 **/
GList *
sheet_names_get_available (Sheet const *sheet)
{
	GList *res = NULL;
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	if (sheet->names != NULL)
		g_hash_table_foreach (sheet->names->names,
			(GHFunc) cb_get_names, &res);
	if (sheet->workbook->names != NULL)
		g_hash_table_foreach (sheet->workbook->names->names,
			(GHFunc) cb_get_names, &res);

	return res;
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
	GnmNamedExpr *nexpr;
	Range tmp;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (r != NULL, NULL);

	tmp = *r;
	range_normalize (&tmp);
	nexpr = gnm_named_expr_collection_check (sheet->names, sheet, &tmp);
	if (nexpr == NULL)
		nexpr = gnm_named_expr_collection_check (sheet->workbook->names, sheet, &tmp);

	return (nexpr != NULL) ? nexpr->name->str : NULL;
}

