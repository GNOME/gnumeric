/*
 * style-condition.c: Implementation of the condition framework.
 *
 * Copyright (C) Almer S. Tigelaar <almer@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include "style-condition.h"

#include "expr.h"
#include "sheet.h"
#include "value.h"
#include "workbook.h"

/*********************************************************************************/

#define DEP_TO_STYLE_CONDITION_EXPR(d_ptr) (StyleConditionExpr *)(((char *)d_ptr) - GTK_STRUCT_OFFSET(StyleConditionExpr, dep))

static void
style_condition_expr_dep_eval (Dependent *dep)
{
	StyleConditionExpr *sce;
	
	g_return_if_fail (dep != NULL);
	sce = DEP_TO_STYLE_CONDITION_EXPR (dep);
	
	if (sce->val) {
		value_release (sce->val);
		sce->val = NULL;
	}
		
	if (dep->expression) {
		EvalPos ep;

		ep.eval.row = ep.eval.col = 0;
		ep.sheet = dep->sheet;
			
		sce->val = expr_eval (dep->expression, &ep, 0);
	}
}

static void
style_condition_expr_dep_set_expr (Dependent *dep, ExprTree *new_expr)
{
	StyleConditionExpr *sce = DEP_TO_STYLE_CONDITION_EXPR (dep);
	
	/* Make sure no invalid 'cached' value
	 * of the previous expression remains
	 */
	if (sce->val) {
		value_release (sce->val);
		sce->val = NULL;
	}
}

static void
style_condition_expr_dep_debug_name (Dependent const *dep, FILE *out)
{
	g_return_if_fail (dep != NULL);
	
	fprintf (out, "StyleCondition Dep %p", dep);
}

static DEPENDENT_MAKE_TYPE (style_condition_expr_dep, &style_condition_expr_dep_set_expr)

/*********************************************************************************/

StyleCondition *
style_condition_new_expr (Sheet *sheet, StyleConditionOperator op,
			  ExprTree *expr)
{
	StyleCondition *sc;

	g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	
	sc = g_new0 (StyleCondition, 1);

	sc->type      = SCT_EXPR;
	sc->ref_count = 1;
	
	sc->u.expr.op        = op;
	sc->u.expr.dep.sheet = sheet;
	sc->u.expr.dep.flags = style_condition_expr_dep_get_dep_type ();
	dependent_set_expr (&sc->u.expr.dep, expr);
	sc->u.expr.val = NULL;
	
	sc->next    = NULL;
	sc->next_op = SCB_NONE;
		
	return sc;
}

StyleCondition *
style_condition_new_constraint (StyleConditionConstraint constraint)
{
	StyleCondition *sc;

	sc = g_new0 (StyleCondition, 1);

	sc->type      = SCT_CONSTRAINT;
	sc->ref_count = 1;

	sc->u.constraint = constraint;
	
	sc->next    = NULL;
	sc->next_op = SCB_NONE;

	return sc;
}

StyleCondition *
style_condition_new_flags (StyleConditionFlags flags)
{
	StyleCondition *sc;

	sc = g_new0 (StyleCondition, 1);

	sc->type      = SCT_FLAGS;
	sc->ref_count = 1;

	sc->u.flags = flags;
	
	sc->next    = NULL;
	sc->next_op = SCB_NONE;

	return sc;
}

void
style_condition_ref (StyleCondition *sc)
{
	g_return_if_fail (sc != NULL);
	g_return_if_fail (sc->ref_count > 0);

	sc->ref_count++;
}

void
style_condition_unref (StyleCondition *sc)
{
	g_return_if_fail (sc != NULL);
	g_return_if_fail (sc->ref_count > 0);

	sc->ref_count--;
	
	if (sc->ref_count <= 0) {
		if (sc->next)
			style_condition_unref (sc->next);

		if (sc->type == SCT_EXPR) {
			dependent_set_expr (&sc->u.expr.dep, NULL);
			if (sc->u.expr.val) {
				value_release (sc->u.expr.val);
				sc->u.expr.val = NULL;
			}
		} else if (sc->type != SCT_CONSTRAINT && sc->type != SCT_FLAGS)
			g_warning ("Unhandled StyleCondition type");
		
		g_free (sc);
	}
}

/**
 * style_condition_chain:
 * Chain @src to @dst with operator @op.
 *
 * Return value: @dst (the beginning of the chain)
 **/
StyleCondition *
style_condition_chain (StyleCondition *dst, StyleConditionBool op,
		       StyleCondition *src)
{
	g_return_val_if_fail (dst != NULL, NULL);
	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (dst->next == NULL, NULL);
	
	dst->next    = src;
	dst->next_op = op;
	return dst;
}

static gboolean
style_condition_expr_eval (StyleConditionExpr *sce, Value *val, StyleFormat *format)
{
	ValueCompare vc;
			
	/*
	 * Apparantly no eval has been done yet, so
	 * we'll have to force it.
	 */
	if (sce->val == NULL) {
		g_return_val_if_fail (dependent_needs_recalc (&sce->dep), FALSE);
		dependent_eval (&sce->dep);
		g_return_val_if_fail (sce->val != NULL, FALSE);
	}

	vc = value_compare (val, sce->val, TRUE);
			
	switch (sce->op) {
	case SCO_EQUAL :
		if (vc != IS_EQUAL) return FALSE; break;
	case SCO_NOT_EQUAL :
		if (vc == IS_EQUAL) return FALSE; break;
	case SCO_LESS :
		if (vc != IS_LESS) return FALSE; break;
	case SCO_GREATER :
		if (vc != IS_GREATER) return FALSE; break;
	case SCO_LESS_EQUAL :
		if (vc == IS_GREATER) return FALSE; break;
	case SCO_GREATER_EQUAL :
		if (vc == IS_LESS) return FALSE; break;
	default :
		g_warning ("Style Condition: Unhandled operator"); return FALSE;
	}

	return TRUE;
}

static gboolean
style_condition_constraint_eval (StyleConditionConstraint scc, Value *val, StyleFormat *format)
{
	switch (scc) {
	case SCC_IS_INT :
		if (val->type != VALUE_BOOLEAN
		    && val->type != VALUE_INTEGER)
			return FALSE; break;
	case SCC_IS_FLOAT :
		if (val->type != VALUE_FLOAT) return FALSE; break;
	case SCC_IS_IN_LIST :
		g_warning ("Style Condition: 'Is In List' not implemented");
		break;
	case SCC_IS_DATE :
		g_warning ("Style Condition: 'In Date' not implemented");
		break;
	case SCC_IS_TIME :
		g_warning ("Style Condition: 'Is Time' not implemented");
		break;
	case SCC_IS_TEXTLEN :
		g_warning ("Style Condition: 'Is Text Length' not implemented");
		break;
	default :
		g_warning ("Style Condition: Unhandled operator");
		return FALSE;
	}
	
	return TRUE;
}

static gboolean
style_condition_flags_eval (StyleConditionFlags scf, Value *val, StyleFormat *format)
{
	if (val->type == VALUE_EMPTY && (scf & SCF_ALLOW_BLANK))
		return TRUE;
	else
		return FALSE;
}

void
style_condition_dump (StyleCondition *sc)
{
	StyleCondition *sci;
	int             i;

	g_return_if_fail (sc != NULL);
	
	fprintf (stdout, "---------------------------------------\n");
	for (i = 0, sci = sc; sci != NULL; sci = sci->next, i++) {
		char     *t = NULL;
		GString  *s = NULL;
		
		fprintf (stdout, "=> Element %d\n", i);

		switch (sci->type) {
		case SCT_EXPR       : t = "Expression"; break;
		case SCT_CONSTRAINT : t = "Constraint"; break;
		case SCT_FLAGS      : t = "Flags";      break;
		default :
			t = "Unknown";
		}
		fprintf (stdout, "\tType          : %s\n", t);
		fprintf (stdout, "\tRef. Count    : %d\n", sci->ref_count);

		switch (sci->type) {
		case SCT_EXPR :
			switch (sci->u.expr.op) {
			case SCO_EQUAL         : t = "== Equal";         break;
			case SCO_NOT_EQUAL     : t = "!= Not Equal";     break;
			case SCO_GREATER       : t = ">  Greater";       break;
			case SCO_LESS          : t = "<  Less";          break;
			case SCO_GREATER_EQUAL : t = ">= Greater Equal"; break;
			case SCO_LESS_EQUAL    : t = "<= Less Equal";    break;
			default :
				t = "?  Unknown";
			}
			fprintf (stdout, "\t\tOperator   : %s\n", t);

			if (sci->u.expr.dep.expression) {
				ParsePos  pp;
				Sheet    *sheet = sci->u.expr.dep.sheet;
				
				parse_pos_init (&pp, sheet->workbook, sheet, 0, 0);
				t = expr_tree_as_string (sci->u.expr.dep.expression, &pp);
				fprintf (stdout, "\t\tExpression : %s\n", t);
				g_free (t);
				t = NULL;
			} else
				fprintf (stdout, "\t\tExpression : (NULL)\n");

			if (sci->u.expr.val)
				fprintf (stdout, "\t\tResult     : %s (Cached)\n",
					 value_peek_string (sci->u.expr.val));
			else
				fprintf (stdout, "\t\tResult     : (NULL)\n");
			
			break;
		case SCT_CONSTRAINT :
			switch (sci->u.constraint) {
			case SCC_IS_INT     : t = "Whole Number (Integer)"; break;
			case SCC_IS_FLOAT   : t = "Decimals     (Float)";   break;
			case SCC_IS_IN_LIST : t = "List         (Region)";  break;
			case SCC_IS_DATE    : t = "Date         (Integer)"; break;
			case SCC_IS_TIME    : t = "Time         (Integer)"; break;
			case SCC_IS_TEXTLEN : t = "TextLength   (Integer)"; break;
			case SCC_IS_CUSTOM  : t = "Custom       (Any)";     break;
			default :
				t = "Unknown      (???)";
			}
			fprintf (stdout, "\t\tConstraint : %s\n", t);
			
			break;
		case SCT_FLAGS :
			s = g_string_new ("");

			if (sci->u.flags & SCF_ALLOW_BLANK)
				g_string_append (s, "(Allow Blank)");
			if (sci->u.flags & SCF_IN_CELL_DROPDOWN)
				g_string_append (s, "(In Cell Dropdown)");
			
			fprintf (stdout, "\t\tFlags      : %s\n", s->str);
			g_string_free (s, TRUE);
			
			break;
		}

		switch (sci->next_op) {
		case SCB_NONE     : t = "  (None)";     break;
		case SCB_AND      : t = "& (And)";      break;
		case SCB_OR       : t = "| (Or)";       break;
		case SCB_AND_PAIR : t = "& (And PAIR)"; break;
		case SCB_OR_PAIR  : t = "| (Or PAIR)";  break;
		default :
			t = "? (Unknown)";
		}
		fprintf (stdout, "\tNext Operator : %s\n\n", t);
	}
	fprintf (stdout, "---------------------------------------\n\n");
}

gboolean
style_condition_eval (StyleCondition *sc, Value *val, StyleFormat *format)
{
	StyleCondition     *sci;
	StyleConditionBool  scb = SCB_NONE;
	gboolean            prev_result = FALSE;
	gboolean            result = TRUE;
	
	g_return_val_if_fail (val != NULL, FALSE);

	for (sci = sc; sci != NULL; sci = sci->next) {
		result = TRUE;
		
		switch (sci->type) {
		case SCT_EXPR :
			result = style_condition_expr_eval (&sci->u.expr, val, format);
			break;
		case SCT_CONSTRAINT :
			result = style_condition_constraint_eval (sci->u.constraint, val, format);
			break;
		case SCT_FLAGS :
			result = style_condition_flags_eval (sci->u.flags, val, format);
			break;
		default :
			g_warning ("Unknown StyleCondition type");
		}

		/*
		 * NOTE: Pay careful attention to the four chain operators.
		 *
		 * The normal two, SCB_AND and SCB_OR, just evaluate things as list.
		 * Example : X SCB_AND Y SCB_OR Z
		 *           (first does "X & Y" then does "Y | Z")
		 * Observe that none of the operators take precedence over the other.
		 *
		 * The other two, SCB_AND_PAIR and SCB_OR_PAIR explicitely indicate
		 * precendence by using 'parens'.
		 * Example : X SCB_OR_PAIR Y SCB_AND_PAIR Z
		 *           (can be written as : "((X | Y) & Z)")
		 *
		 * And a combination  : X SCB_OR_PAIR Y SCB_AND_PAIR Z SCB_OR J
		 * Which evaluates to : ((X | Y) & Z) | J
		 */
		switch (scb) {
		case SCB_AND :
			if (!(result && prev_result))
				return FALSE;
			else
				result = TRUE;
			break;
		case SCB_OR :
			if (!(result || prev_result))
				return FALSE;
			else
				result = TRUE;
			break;
		case SCB_AND_PAIR :
			result = (result && prev_result);
			break;
		case SCB_OR_PAIR :
			result = (result || prev_result);
			break;
		case SCB_NONE :
			if (sci != sc && sci->next != NULL)
				g_warning ("Corrupted StyleCondition Chain");
			break;
		default :
			g_warning ("Unknown StyleCondition Chain Operator");
		}

		scb = sci->next_op;
		prev_result = result;
	}
	
	return result;
}
