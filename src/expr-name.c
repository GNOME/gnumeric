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
#include <glib/gi18n-lib.h>
#include <string.h>
#include "gnumeric.h"
#include "expr-name.h"

#include "dependent.h"
#include "value.h"
#include "workbook-priv.h"
#include "expr.h"
#include "expr-impl.h"
#include "str.h"
#include "sheet.h"
#include "ranges.h"
#include "gutils.h"
#include "sheet-style.h"

#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-locale.h>

/**
 * expr_name_validate:
 * @name: tentative name
 *
 * returns TRUE if the given name is valid, FALSE otherwise.
 */
gboolean
expr_name_validate (const char *name)
{
	GnmCellPos cp;
	const char *p;

	g_return_val_if_fail (name != NULL, FALSE);

	if (name[0] == 0)
		return FALSE;

	/* What about other locales.  */
	if (strcmp (name, go_locale_boolean_name (TRUE)) == 0 ||
	    strcmp (name, go_locale_boolean_name (FALSE)) == 0)
		return FALSE;

	/* What about R1C1?  */
	if (cellpos_parse (name, &cp, TRUE))
		return FALSE;

	/* Hmm...   Now what?  */
	if (!g_unichar_isalpha (g_utf8_get_char (name)) &&
	    name[0] != '_')
		return FALSE;

	for (p = name; *p; p = g_utf8_next_char (p)) {
		if (!g_unichar_isalnum (g_utf8_get_char (p)) &&
		    p[0] != '_')
			return FALSE;
	}

	return TRUE;
}


static void
cb_nexpr_remove (GnmNamedExpr *nexpr)
{
	g_return_if_fail (nexpr->active);

	nexpr->active = FALSE;
	expr_name_set_expr (nexpr, NULL);
	expr_name_unref (nexpr);
}

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
		GnmDependent *dep = ptr->data;
		if (dependent_is_linked (dep))
			dependent_unlink (dep);
	}
	return deps;
}

static void
expr_name_relink_deps (GnmNamedExpr *nexpr)
{
	GSList *deps = NULL;

	if (nexpr->dependents == NULL)
		return;

	g_hash_table_foreach (nexpr->dependents, cb_collect_name_deps, &deps);
	dependents_link (deps);
	g_slist_free (deps);
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
 * @names : The collection of names
 *
 * Frees names defined in the local scope.
 * NOTE : THIS DOES NOT INVALIDATE NAMES THAT REFER
 *        TO THIS SCOPE.
 *        eg
 *           in scope sheet2 we have a name that refers
 *           to sheet1.  That will remain!
 **/
void
gnm_named_expr_collection_free (GnmNamedExprCollection *names)
{
	if (names != NULL) {
		g_hash_table_destroy (names->names);
		g_hash_table_destroy (names->placeholders);
		g_free (names);
	}
}

static void
cb_unlink_all_names (G_GNUC_UNUSED gpointer key,
		     gpointer value,
		     G_GNUC_UNUSED gpointer user_data)
{
	GnmNamedExpr *nexpr = value;
	GSList *deps = expr_name_unlink_deps (nexpr);
	g_slist_free (deps);
}

void
gnm_named_expr_collection_unlink (GnmNamedExprCollection *names)
{
	if (!names)
		return;

	if (names->names) {
		g_hash_table_foreach (names->names,
				      cb_unlink_all_names,
				      NULL);
	}
}

static void
cb_relink_all_names (G_GNUC_UNUSED gpointer key,
		     gpointer value,
		     G_GNUC_UNUSED gpointer user_data)
{
	GnmNamedExpr *nexpr = value;
	expr_name_relink_deps (nexpr);
}

void
gnm_named_expr_collection_relink (GnmNamedExprCollection *names)
{
	if (!names)
		return;

	if (names->names) {
		g_hash_table_foreach (names->names,
				      cb_relink_all_names,
				      NULL);
	}
}

GnmNamedExpr *
gnm_named_expr_collection_lookup (GnmNamedExprCollection const *scope,
				  char const *name)
{
	if (scope != NULL) {
		GnmNamedExpr *nexpr = g_hash_table_lookup (scope->names, name);
		if (nexpr == NULL)
			nexpr = g_hash_table_lookup (scope->placeholders, name);
		return nexpr;
	} else
		return NULL;
}

static void
cb_list_names (G_GNUC_UNUSED gpointer key,
	       gpointer value,
	       gpointer user_data)
{
	GSList **pres = user_data;
	GO_SLIST_PREPEND (*pres, value);
}

GSList *
gnm_named_expr_collection_list (GnmNamedExprCollection const *scope)
{
	GSList *res = NULL;
	if (scope && scope->names) {
		g_hash_table_foreach (scope->names,
				      cb_list_names,
				      &res);
	}
	return res;
}

static void
gnm_named_expr_collection_insert (GnmNamedExprCollection const *scope,
				  GnmNamedExpr *nexpr)
{
	/* name can be active at this point, eg we are converting a
	 * placeholder, or changing a scope */
	nexpr->active = TRUE;
	g_hash_table_replace (nexpr->is_placeholder
	      ? scope->placeholders : scope->names, nexpr->name->str, nexpr);
}

typedef struct {
	Sheet const *sheet;
	GnmRange const *r;
	GnmNamedExpr *res;
} CheckName;

static void
cb_check_name (G_GNUC_UNUSED gpointer key, GnmNamedExpr *nexpr,
	       CheckName *user)
{
	GnmValue *v;

	if (!nexpr->active || nexpr->is_hidden || !nexpr->texpr)
		return;

	v = gnm_expr_top_get_range (nexpr->texpr);
	if (v != NULL) {
		if (v->type == VALUE_CELLRANGE) {
			GnmRangeRef const *ref = &v->v_range.cell;
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
				 Sheet const *sheet, GnmRange const *r)
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

/**
 * expr_name_handle_references : register or unregister a name with
 *    all of the sheets it explicitly references.  This is necessary
 *    because names are not dependents, and if they reference a deleted
 *    sheet we will not notice.
 */
static void
expr_name_handle_references (GnmNamedExpr *nexpr, gboolean add)
{
	GSList *sheets, *ptr;

	sheets = gnm_expr_top_referenced_sheets (nexpr->texpr);
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
				g_warning ("Unregistered name being removed?");
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
expr_name_lookup (GnmParsePos const *pp, char const *name)
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
 **/
GnmNamedExpr *
expr_name_new (char const *name, gboolean is_placeholder)
{
	GnmNamedExpr *nexpr;

	g_return_val_if_fail (name != NULL, NULL);

	nexpr = g_new0 (GnmNamedExpr,1);

	nexpr->ref_count	= 1;
	nexpr->active		= FALSE;
	nexpr->name		= gnm_string_get (name);
	nexpr->texpr		= NULL;
	nexpr->dependents	= NULL;
	nexpr->is_placeholder	= is_placeholder;
	nexpr->is_hidden	= FALSE;
	nexpr->is_permanent	= FALSE;
	nexpr->is_editable	= TRUE;

	g_return_val_if_fail (nexpr->name != NULL, NULL);

	return nexpr;
}

/*
 * NB. if we already have a circular reference in addition
 * to this one we are checking we will come to serious grief.
 */
static gboolean
do_expr_name_loop_check (char const *name, GnmExpr const *expr)
{
	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
	case GNM_EXPR_OP_ANY_BINARY:
		return (do_expr_name_loop_check (name, expr->binary.value_a) ||
			do_expr_name_loop_check (name, expr->binary.value_b));
	case GNM_EXPR_OP_ANY_UNARY:
		return do_expr_name_loop_check (name, expr->unary.value);
	case GNM_EXPR_OP_NAME: {
		GnmNamedExpr const *nexpr = expr->name.name;
		if (!strcmp (nexpr->name->str, name))
			return TRUE;
		if (nexpr->texpr != NULL) /* look inside this name tree too */
			return expr_name_check_for_loop (name, nexpr->texpr);
		return FALSE;
	}
	case GNM_EXPR_OP_FUNCALL: {
		int i;
		for (i = 0; i < expr->func.argc; i++)
			if (do_expr_name_loop_check (name, expr->func.argv[i]))
				return TRUE;
		break;
	}
	case GNM_EXPR_OP_CONSTANT:
	case GNM_EXPR_OP_CELLREF:
	case GNM_EXPR_OP_ARRAY_CORNER:
	case GNM_EXPR_OP_ARRAY_ELEM:
		break;
	case GNM_EXPR_OP_SET: {
		int i;
		for (i = 0; i < expr->set.argc; i++)
			if (do_expr_name_loop_check (name, expr->set.argv[i]))
				return TRUE;
		break;
	}
	}
	return FALSE;
}

gboolean
expr_name_check_for_loop (char const *name, GnmExprTop const *texpr)
{
	g_return_val_if_fail (texpr != NULL, TRUE);

	return do_expr_name_loop_check (name, texpr->expr);
}

static void
expr_name_queue_deps (GnmNamedExpr *nexpr)
{
	if (nexpr->dependents)
		g_hash_table_foreach (nexpr->dependents,
				      (GHFunc)dependent_queue_recalc,
				      NULL);
}

/**
 * expr_name_add:
 * @pp:
 * @name:
 * @texpr: if texpr == NULL then create a placeholder with value #NAME?
 * @error_msg:
 * @link_to_container:
 *
 * Absorbs the reference to @texpr.
 * If @error_msg is non NULL it may hold a pointer to a translated descriptive
 * string.  NOTE : caller is responsible for freeing the error message.
 *
 * The reference semantics of the new expression are
 * 1) new names with @link_to_container TRUE are referenced by the container.
 *    The caller DOES NOT OWN a reference to the result, and needs to add their
 *    own.
 * 2) if @link_to_container is FALSE the caller DOES OWN a reference, and
 *    can free the result by unrefing the name.
 **/
GnmNamedExpr *
expr_name_add (GnmParsePos const *pp, char const *name,
	       GnmExprTop const *texpr, char **error_msg,
	       gboolean link_to_container,
	       GnmNamedExpr *stub)
{
	GnmNamedExpr *nexpr = NULL;
	GnmNamedExprCollection *scope = NULL;

	g_return_val_if_fail (pp != NULL, NULL);
	g_return_val_if_fail (pp->sheet != NULL || pp->wb != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (stub == NULL || stub->is_placeholder, NULL);

	if (texpr != NULL && expr_name_check_for_loop (name, texpr)) {
		gnm_expr_top_unref (texpr);
		if (error_msg)
			*error_msg = g_strdup_printf (_("'%s' has a circular reference"), name);
		return NULL;
	}

	scope = (pp->sheet != NULL) ? pp->sheet->names : pp->wb->names;
	if (scope != NULL) {
		/* see if there was a place holder */
		nexpr = g_hash_table_lookup (scope->placeholders, name);
		if (nexpr != NULL) {
			if (texpr == NULL) {
				/* there was already a placeholder for this */
				expr_name_ref (nexpr);
				return nexpr;
			}

			/* convert the placeholder into a real name */
			g_hash_table_steal (scope->placeholders, name);
			nexpr->is_placeholder = FALSE;
		} else {
			nexpr = g_hash_table_lookup (scope->names, name);
			/* If this is a permanent name, we may be adding it on opening of a file, although */
			/* the name is already in place. */
			if (nexpr != NULL) {
				if (nexpr->is_permanent)
					link_to_container = FALSE;
				else {
					if (error_msg != NULL)
						*error_msg = (pp->sheet != NULL)
							? g_strdup_printf (_("'%s' is already defined in sheet"), name)
							: g_strdup_printf (_("'%s' is already defined in workbook"), name);

					gnm_expr_top_unref (texpr);
					return NULL;
				}
			}
		}
	} else if (pp->sheet != NULL)
		scope = pp->sheet->names = gnm_named_expr_collection_new ();
	else
		scope = pp->wb->names = gnm_named_expr_collection_new ();

	if (error_msg)
		*error_msg = NULL;

	if (nexpr == NULL) {
		if (stub != NULL) {
			nexpr = stub;
			stub->is_placeholder = FALSE;
			gnm_string_unref (stub->name);
			stub->name = gnm_string_get (name);
		} else
			nexpr = expr_name_new (name, texpr == NULL);
	}
	parse_pos_init (&nexpr->pos,
		pp->wb, pp->sheet, pp->eval.col, pp->eval.row);
	if (texpr == NULL)
		texpr = gnm_expr_top_new_constant
			(value_new_error_NAME (NULL));
	expr_name_set_expr (nexpr, texpr);
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
		gnm_string_unref (nexpr->name);
		nexpr->name = NULL;
	}

	if (nexpr->texpr != NULL)
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
 * NOTE : @nexpr may continue to exist if things still have references to it,
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
expr_name_as_string (GnmNamedExpr const *nexpr, GnmParsePos const *pp,
		     GnmConventions const *fmt)
{
	if (pp == NULL)
		pp = &nexpr->pos;
	return gnm_expr_top_as_string (nexpr->texpr, pp, fmt);
}

GnmValue *
expr_name_eval (GnmNamedExpr const *nexpr, GnmEvalPos const *pos,
		GnmExprEvalFlags flags)
{
	g_return_val_if_fail (pos, NULL);

	if (!nexpr)
		return value_new_error_NAME (pos);

	return gnm_expr_top_eval (nexpr->texpr, pos, flags);
}

/**
 * expr_name_downgrade_to_placeholder:
 * @nexpr:
 *
 * Takes a real non-placeholder name and converts it to being a placeholder.
 * unrefing its expression
 **/
void
expr_name_downgrade_to_placeholder (GnmNamedExpr *nexpr)
{
	GnmNamedExprCollection *scope;

	g_return_if_fail (nexpr != NULL);
	g_return_if_fail (nexpr->pos.sheet != NULL || nexpr->pos.wb != NULL);
	g_return_if_fail (nexpr->active);
	g_return_if_fail (!nexpr->is_placeholder);

	scope = (nexpr->pos.sheet != NULL)
		? nexpr->pos.sheet->names : nexpr->pos.wb->names;

	g_return_if_fail (scope != NULL);

	g_hash_table_steal (scope->names, nexpr->name->str);

	nexpr->is_placeholder = TRUE;
	expr_name_set_expr
		(nexpr,
		 gnm_expr_top_new_constant (value_new_error_NAME (NULL)));
	gnm_named_expr_collection_insert (scope, nexpr);
}

/*******************************************************************
 * Manage things that depend on named expressions.
 */
/**
 * expr_name_set_scope:
 * @nexpr:
 * @sheet:
 *
 * Returns a translated error string which the caller must free if something
 * goes wrong.
 **/
char *
expr_name_set_scope (GnmNamedExpr *nexpr, Sheet *sheet)
{
	GnmNamedExprCollection *scope, **new_scope;

	g_return_val_if_fail (nexpr != NULL, NULL);
	g_return_val_if_fail (nexpr->pos.sheet != NULL || nexpr->pos.wb != NULL, NULL);
	g_return_val_if_fail (nexpr->active, NULL);

	scope = (nexpr->pos.sheet != NULL)
		? nexpr->pos.sheet->names : nexpr->pos.wb->names;

	g_return_val_if_fail (scope != NULL, NULL);

	new_scope = (sheet != NULL) ? &(sheet->names) : &(nexpr->pos.wb->names);
	if (*new_scope != NULL) {
		if (NULL != g_hash_table_lookup ((*new_scope)->placeholders, nexpr->name->str) ||
		    NULL != g_hash_table_lookup ((*new_scope)->names, nexpr->name->str))
			return g_strdup_printf (((sheet != NULL)
				? _("'%s' is already defined in sheet")
				: _("'%s' is already defined in workbook")), nexpr->name->str);
	} else
		*new_scope = gnm_named_expr_collection_new ();

	g_hash_table_steal (
		nexpr->is_placeholder ? scope->placeholders : scope->names,
		nexpr->name->str);

	nexpr->pos.sheet = sheet;
	gnm_named_expr_collection_insert (*new_scope, nexpr);
	return NULL;
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
expr_name_set_expr (GnmNamedExpr *nexpr, GnmExprTop const *texpr)
{
	GSList *good = NULL;

	g_return_if_fail (nexpr != NULL);

	if (texpr == nexpr->texpr)
		return;
	if (nexpr->texpr != NULL) {
		GSList *deps = NULL, *junk = NULL;

		deps = expr_name_unlink_deps (nexpr);
		expr_name_handle_references (nexpr, FALSE);
		gnm_expr_top_unref (nexpr->texpr);

		/*
		 * We do not want to relink deps for sheets that are going
		 * away.  This speeds up exit for workbooks with lots of
		 * names defined.
		 */
		while (deps) {
			GSList *next = deps->next;
			GnmDependent *dep = deps->data;

			if (dep->sheet && dep->sheet->being_invalidated)
				deps->next = junk, junk = deps;
			else
				deps->next = good, good = deps;

			deps = next;
		}

		g_slist_free (junk);
	}
	nexpr->texpr = texpr;
	dependents_link (good);
	g_slist_free (good);

	if (texpr != NULL)
		expr_name_handle_references (nexpr, TRUE);

	expr_name_queue_deps (nexpr);
}

void
expr_name_add_dep (GnmNamedExpr *nexpr, GnmDependent *dep)
{
	if (nexpr->dependents == NULL)
		nexpr->dependents = g_hash_table_new (g_direct_hash,
						      g_direct_equal);

	g_hash_table_insert (nexpr->dependents, dep, dep);
}

void
expr_name_remove_dep (GnmNamedExpr *nexpr, GnmDependent *dep)
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

	return gnm_expr_top_is_err (nexpr->texpr, GNM_ERROR_NAME);
}

int
expr_name_cmp_by_name (GnmNamedExpr const *a, GnmNamedExpr const *b)
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

	if (res == 0)	/* By name.  */
		res = go_utf8_collate_casefold (a->name->str, b->name->str);

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
sheet_names_check (Sheet const *sheet, GnmRange const *r)
{
	GnmNamedExpr *nexpr;
	GnmRange tmp;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (r != NULL, NULL);

	tmp = *r;
	range_normalize (&tmp);
	nexpr = gnm_named_expr_collection_check (sheet->names, sheet, &tmp);
	if (nexpr == NULL) {
		nexpr = gnm_named_expr_collection_check (sheet->workbook->names, sheet, &tmp);
		/* The global name is not accessible if there is a local name (#306685) */
		if (nexpr != NULL &&
		    gnm_named_expr_collection_lookup (sheet->names, nexpr->name->str) != NULL)
			return NULL;
	}

	return (nexpr != NULL) ? nexpr->name->str : NULL;
}


/**
 * expr_name_perm_add:
 * @name:               name
 * @texpr:              string to be the value of the name
 * @is_editable:        whether this is a predefined action
 *
 * This is a wrapper around expr_name_add to set this as permanent name.
 *
 *
 **/
void
expr_name_perm_add (Sheet *sheet, char const *name,
		    GnmExprTop const *value,
		    gboolean is_editable)
{
	GnmNamedExpr *res;
	GnmParsePos pp;

	parse_pos_init_sheet (&pp, sheet);
	res = expr_name_add (&pp, name, value, NULL, TRUE, NULL);
	if (res) {
		res->is_permanent = TRUE;
		res->is_editable = is_editable;
	}


}

/* ------------------------------------------------------------------------- */

static void
expr_name_set_expr_ref (GnmNamedExpr *nexpr, GnmExprTop const *texpr)
{
	gnm_expr_top_ref (texpr);
	expr_name_set_expr (nexpr, texpr);
}


GOUndo *
expr_name_set_expr_undo_new (GnmNamedExpr *ne)
{
	expr_name_ref (ne);
	gnm_expr_top_ref (ne->texpr);

	return go_undo_binary_new (ne, (gpointer)ne->texpr,
				   (GOUndoBinaryFunc)expr_name_set_expr_ref,
				   (GFreeFunc)expr_name_unref,
				   (GFreeFunc)gnm_expr_top_unref);
}

/* ------------------------------------------------------------------------- */
