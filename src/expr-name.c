/*
 * expr-name.c:  Workbook name table support, will deal
 *               with all the exotic special names.
 *
 * Author:
 *    Michael Meeks <michael@ximian.com>
 */

#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include "expr-name.h"
#include "eval.h"
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
add_real (Workbook *wb, Sheet *sheet, const char *name,
	  gboolean builtin, ExprTree *expr_tree, void *expr_func)
{
	NamedExpression *expr_name;

	g_return_val_if_fail (name != NULL, NULL);

	expr_name = g_new (NamedExpression,1);

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

	switch (expr->any.oper) {
	case OPER_ANY_BINARY:
		if (!name_refer_circular (name, expr->binary.value_a))
			return TRUE;
		if (!name_refer_circular (name, expr->binary.value_b))
			return TRUE;
		break;
	case OPER_ANY_UNARY:
		if (!name_refer_circular (name, expr->unary.value))
			return TRUE;
		break;
	case OPER_NAME:
	{
		const NamedExpression *expr_name = expr->name.name;
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
NamedExpression *
expr_name_add (Workbook *wb, Sheet *sheet, const char *name,
	       ExprTree *expr, char **error_msg)
{
	NamedExpression *expr_name;

	g_return_val_if_fail (name != NULL, 0);
	g_return_val_if_fail (expr != NULL, 0);

	/* sheet, workbook, or global.  Pick only 1 */
	g_return_val_if_fail (sheet == NULL || wb == NULL, 0);

/*	printf ("Adding name '%s' to %p %p\n", name, wb, sheet);*/

	if (sheet != NULL && expr_name_lookup_list (sheet->names, name) != NULL) {
		*error_msg = _("already defined in sheet");
		return NULL;
	}
	if (wb != NULL && expr_name_lookup_list (wb->names, name) != NULL) {
		*error_msg = _("already defined in workbook");
		return NULL;
	}
	if (name_refer_circular (name, expr)) {
		*error_msg = _("circular reference");
		return NULL;
	}

	expr_name = add_real (wb, sheet, name, FALSE, expr, NULL);
	if (sheet)
		sheet->names = g_list_append (sheet->names, expr_name);
	else if (wb)
		wb->names    = g_list_append (wb->names, expr_name);
	else
		global_names = g_list_append (global_names, expr_name);

	return expr_name;
}

/**
 * expr_name_create:
 * @pos:
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
expr_name_create (Workbook *wb, Sheet *sheet, const char *name,
		  const char *value, ParseError *error)
{
	ExprTree *tree;
	ParsePos pos, *pp;

	pp = parse_pos_init (&pos, wb, sheet, 0, 0);
	tree = expr_parse_string (value, pp, NULL, error);
	if (!tree)
		return NULL;

	/*
	 * We know there has been no parse error, but set the
	 * use the message part of the struct to pass a name
	 * creation error back to the calling routine
	 */
	return expr_name_add (wb, sheet, name, tree, &error->message);
}

static void
expr_name_destroy (NamedExpression *expr_name)
{
	g_return_if_fail (expr_name);

	if (expr_name->name)
		string_unref (expr_name->name);
	expr_name->name = NULL;

	if (!expr_name->builtin && expr_name->t.expr_tree)
		expr_tree_unref (expr_name->t.expr_tree);

	expr_name->wb          = NULL;
	expr_name->sheet       = NULL;
	expr_name->t.expr_tree = NULL;

	g_free (expr_name);
}

static void
expr_name_unlink (NamedExpression *expr_name)
{
	Workbook *wb = NULL;
	Sheet    *sheet = NULL;

     /*	printf ("Removing : '%s'\n", expr_name->name->str);*/
	if (expr_name->sheet) {
		sheet = expr_name->sheet;
		g_return_if_fail (g_list_find (sheet->names, expr_name) != NULL);
		sheet->names = g_list_remove (sheet->names, expr_name);
	} else if (expr_name->wb) {
		wb = expr_name->wb;
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
	expr_name_destroy (expr_name);
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
		expr_name_destroy (expr_name);
	}
	g_list_free (names);
	return NULL;
}

GList *
expr_name_list (Workbook *wb, Sheet *sheet, gboolean builtins_too)
{
	GList *l = NULL;

	if (sheet != NULL && sheet->names != NULL)
		l = g_list_copy (sheet->names);

	if (wb != NULL && wb->names)
		l = g_list_concat (l, g_list_copy (wb->names));

	if (builtins_too && global_names != NULL)
		l = g_list_concat (l, g_list_copy (global_names));

	return l;
}

char *
expr_name_value (const NamedExpression *expr_name)
{
	if (!expr_name->builtin) {
		char * val;
		ParsePos  pos, *pp;

		pp = parse_pos_init (&pos, expr_name->wb,
				     expr_name->sheet, 0, 0);
		val = expr_tree_as_string (expr_name->t.expr_tree, pp);
		if (val == NULL)
			return g_strdup ("Error");
		return val;

	}
	return g_strdup (_("Builtin"));
}


Value *
eval_expr_name (EvalPos const * const pos, const NamedExpression *expr_name,
		ExprEvalFlags const flags)
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
		return eval_expr (pos, expr_name->t.expr_tree, flags);
}

void
expr_name_invalidate_refs_name (NamedExpression *exprn)
{
	static gboolean warned = FALSE;
	if (!warned)
		g_warning ("Implement Me !. expr_name_invalidate_refs_name\n");
	warned = TRUE;
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


/**
 * expr_name_sheet2wb:
 * @expression: 
 * 
 * Change the scope of a NamedExpression from Sheet to Workbook
 * 
 * Return Value: FALSE on error, TRUE otherwise
 **/
gboolean
expr_name_sheet2wb (NamedExpression *expression)
{
	Sheet *sheet;
	Workbook *wb;

	g_return_val_if_fail (expression->sheet != NULL, FALSE);
	
	sheet = expression->sheet;
	wb = sheet->workbook;
	
	expression->sheet = NULL;
	expression->wb = wb;

	wb->names    = g_list_prepend (wb->names, expression);
	sheet->names = g_list_remove (sheet->names, expression);

	return TRUE;
}

/**
 * expr_name_wb2sheet:
 * @expression: 
 * @sheet: 
 * 
 * Change the scope of a NamedExpression from Workbook to Sheet
 * this function does not invalidate the references that are using
 * this NamedExpression
 * 
 * Return Value: FALSE or error, TRUE otherwise
 **/
gboolean
expr_name_wb2sheet (NamedExpression *expression, Sheet *sheet)
{
	static gboolean warned = FALSE;
	if (!warned)
		g_warning ("Implement Me !. expr_name_wb2sheet\n");
	warned = TRUE;
	return FALSE;
}
	
/* ------------------------------------------------------------- */

static Value *
name_print_area (FunctionEvalInfo *ei, Value **args)
{
	if (!ei || !ei->pos->sheet)
		return value_new_error (ei->pos, _("Error: no sheet"));
	else {
		Range r = sheet_get_extent (ei->pos->sheet);
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
		NamedExpression *expr_name;
		expr_name = add_real (NULL, NULL, builtins[lp].name, TRUE, NULL,
				      builtins[lp].fn);
		global_names = g_list_append (global_names, expr_name);
		lp++;
	}
}
