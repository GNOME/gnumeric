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

#include "style-condition.h"

#include "expr.h"
#include "sheet.h"
#include "value.h"
#include "workbook.h"

/*********************************************************************************/

#define DEP_TO_STYLE_CONDITION(d_ptr) (StyleCondition *)(((char *)d_ptr) - GTK_STRUCT_OFFSET(StyleCondition, dep))

static void
style_condition_dep_eval (Dependent *dep)
{
	StyleCondition *sc;
	
	g_return_if_fail (dep != NULL);
	sc = DEP_TO_STYLE_CONDITION (dep);
	
	if (sc->val) {
		value_release (sc->val);
		sc->val = NULL;
	}
		
	if (dep->expression) {
		EvalPos ep;

		ep.eval.row = ep.eval.col = 0;
		ep.sheet = dep->sheet;
			
		sc->val = eval_expr (&ep, dep->expression, 0);
	}
}

static void
style_condition_dep_set_expr (Dependent *dep, ExprTree *new_expr)
{
	StyleCondition *sc = DEP_TO_STYLE_CONDITION (dep);
	
	/* Make sure no invalid 'cached' value
	 * of the previous expression remains
	 */
	if (sc->val) {
		value_release (sc->val);
		sc->val = NULL;
	}
}

static void
style_condition_dep_debug_name (Dependent const *dep, FILE *out)
{
	g_return_if_fail (dep != NULL);
	
	fprintf (out, "StyleCondition Dep %p", dep);
}

static DEPENDENT_MAKE_TYPE (style_condition_dep, &style_condition_dep_set_expr)

/*********************************************************************************/

StyleCondition *
style_condition_new (Sheet *sheet, StyleConditionOp op, ExprTree *expr)
{
	StyleCondition *sc;

	/* Be paranoid, expr is only needed for value restrictions */
	if (op >= STYLE_CONDITION_EQUAL && op <= STYLE_CONDITION_LESS_EQUAL)
		g_return_val_if_fail (expr != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	
	sc = g_new0 (StyleCondition, 1);

	sc->ref_count = 1;	
	sc->op        = op;
	sc->dep.sheet = sheet;
	sc->dep.flags = style_condition_dep_get_dep_type ();
	dependent_set_expr (&sc->dep, expr);

	sc->val  = NULL;
	sc->next = NULL;
		
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

		dependent_set_expr (&sc->dep, NULL);
		if (sc->val) {
			value_release (sc->val);
			sc->val = NULL;
		}
		g_free (sc);
	}
}

/**
 * style_condition_chain:
 * Chain @src to @dst.
 *
 * Chains are always 'AND' chains, meaning that
 * all the conditions in the chain have to be met.
 * (the chain is evaluated as a whole)
 * 
 * Return value: @dst (the beginning of the chain)
 **/
StyleCondition *
style_condition_chain (StyleCondition *dst, StyleCondition *src)
{
	g_return_val_if_fail (dst != NULL, NULL);
	g_return_val_if_fail (src != NULL, NULL);
	g_return_val_if_fail (dst->next == NULL, NULL);
	
	dst->next = src;
	return dst;
}

gboolean
style_condition_eval (StyleCondition *sc, Value *val, StyleFormat *format)
{
	StyleCondition *scl;

	g_return_val_if_fail (val != NULL, FALSE);
	
	for (scl = sc; scl != NULL; scl = scl->next) {
		if (scl->op >= STYLE_CONDITION_EQUAL &&
		    scl->op <= STYLE_CONDITION_LESS_EQUAL) {
			/* Value restriction */
			ValueCompare vc;
			
			/*
			 * Apparantly no eval has been done yet, so
			 * we'll have to force it.
			 */
			if (scl->val == NULL) {
				g_return_val_if_fail (dependent_needs_recalc (&scl->dep), FALSE);
				dependent_eval (&sc->dep);
				g_return_val_if_fail (scl->val != NULL, FALSE);
			}

			vc = value_compare (val, scl->val, TRUE);
			
			switch (scl->op) {
			case STYLE_CONDITION_EQUAL :
				if (vc != IS_EQUAL) return FALSE; break;
			case STYLE_CONDITION_NOT_EQUAL :
				if (vc == IS_EQUAL) return FALSE; break;
			case STYLE_CONDITION_LESS :
				if (vc != IS_LESS) return FALSE; break;
			case STYLE_CONDITION_GREATER :
				if (vc != IS_GREATER) return FALSE; break;
			case STYLE_CONDITION_LESS_EQUAL :
				if (vc == IS_GREATER) return FALSE; break;
			case STYLE_CONDITION_GREATER_EQUAL :
				if (vc == IS_LESS) return FALSE; break;
			default :
				g_warning ("Style Condition: Unhandled operator"); return FALSE;
			}
		} else {
			/* Type restriction */

			/*
			 * FIXME: Note all type restrictions are implemented yet
			 * some of them will need the StyleFormat passed to this
			 * function
			 */
			
			switch (scl->op) {
			case STYLE_CONDITION_IS_INT :
				if (val->type != VALUE_BOOLEAN
				    && val->type != VALUE_INTEGER)
					return FALSE; break;
			case STYLE_CONDITION_IS_FLOAT :
				if (val->type != VALUE_FLOAT) return FALSE; break;
			case STYLE_CONDITION_IS_IN_LIST :
				g_warning ("Style Condition: 'Is In List' not implemented");
				break;
			case STYLE_CONDITION_IS_DATE :
				g_warning ("Style Condition: 'In Date' not implemented");
				break;
			case STYLE_CONDITION_IS_TIME :
				g_warning ("Style Condition: 'Is Time' not implemented");
				break;
			case STYLE_CONDITION_IS_TEXTLEN :
				g_warning ("Style Condition: 'Is Text Length' not implemented");
				break;
			default :
				g_warning ("Style Condition: Unhandled operator"); return FALSE;
			}
		}
	}
	
	return TRUE;
}
