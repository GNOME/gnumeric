/* -*- mode: c; c-basic-offset: 8 -*- */

/*
  Authors: Ariel Rios <ariel@arcavia.com>
*/             

#include <libguile.h>
#include <gtk/gtk.h>
#include <guile/gh.h>
#include "guile-support.h"


SCM
scm_symbolfrom0str (char *name)
{
	return SCM_CAR(scm_intern0(name));
}

/*
  FIXME: 
  This funcs are useless, since we really don't care about
  the starting position of the range, we do care on the values
  of each cell and that is not given bu this functions.
*/

SCM
cell_ref_to_scm (CellRef cell, CellRef eval_cell)
{
	int col = cell.col_relative ? cell.col + eval_cell.col : cell.col,
		row = cell.row_relative ? cell.row + eval_cell.row : cell.row;

	return scm_cons (scm_symbolfrom0str ("cell-ref"),
			scm_cons(scm_long2num (col), scm_long2num (row)));
				/* FIXME: we need the relative-flags,
				 * and the sheet, and workbook */
}

CellRef
scm_to_cell_ref (SCM scm)
{
	/* Sheet local, absolute references */
	CellRef cell = { NULL, 0, 0, FALSE, FALSE };

	if (SCM_NIMP (scm) && SCM_CONSP (scm)
	    && SCM_NFALSEP (scm_eq_p (SCM_CAR (scm), scm_symbolfrom0str ("cell-ref")))
	    && SCM_NIMP (SCM_CDR (scm)) && SCM_CONSP (SCM_CDR (scm))
	    && SCM_NFALSEP (scm_number_p (SCM_CADR (scm))) && SCM_NFALSEP (scm_number_p (SCM_CDDR(scm))))
	{
		cell.col = gh_scm2int (SCM_CADR (scm));
		cell.row = gh_scm2int (SCM_CDDR (scm));
	}
	else
		;		/* FIXME: should report error */
	
	return cell;
}

SCM
list_to_scm (GList *list, CellRef cell)
{
  return SCM_UNSPECIFIED;
}

SCM
gnumeric_list2scm (GList *list, SCM (*data_to_scm)(void*))
{
  SCM res, *tail = &res;

  while (list)
    {
      *tail = scm_cons (data_to_scm (&list->data), *tail); 
      tail = SCM_CDRLOC (*tail);
      list = list->next;
    }

  *tail = SCM_EOL;

  return res;

}

GList *
gnumeric_scm2list (SCM obj , void (*fromscm)(SCM, void*))
{
   GList *res = NULL, *tail = NULL;

  if (obj == SCM_BOOL_F)
    return NULL;
  else if (obj == SCM_EOL || (SCM_NIMP(obj) && SCM_CONSP(obj)))
    {
      while (SCM_NIMP(obj) && SCM_CONSP(obj))
      {
        GList *n = g_list_alloc ();
	if (res == NULL)
	  res = tail = n;
	else 
	  {
	    g_list_concat (tail, n);
	    tail = n;
	  }
	if (fromscm)
	  fromscm (SCM_CAR (obj), &(n->data));
	else
	  n->data = NULL;
	obj = SCM_CDR(obj);
      }
    }
  else if (SCM_NIMP(obj) && SCM_VECTORP(obj))
    {
      int len = SCM_LENGTH (obj), i;
      SCM *elts = SCM_VELTS (obj);
      for (i = 0; i < len; i++)
	{
	  GList *n = g_list_alloc ();
	  if (res == NULL)
	    res = tail = n;
	  else 
	    {
	      g_list_concat (tail, n);
	      tail = n;
	    }
	  if (fromscm)
	    fromscm (elts[i], &(n->data));
	  else
	    n->data = NULL;
	}
    }

  return res;
}



SCM
value_to_scm (Value const *val, CellRef cell_ref)
{
	if (val == NULL)
		return SCM_EOL;

	switch (val->type)
	{
		case VALUE_EMPTY :
			return gh_eval_str ("'()");
 
		case VALUE_BOOLEAN :
			return gh_bool2scm (val->v_bool.val);	
			
		case VALUE_ERROR :
			return scm_makfrom0str (val->v_err.mesg->str);

		case VALUE_STRING :
			return scm_makfrom0str (val->v_str.val->str);

		case VALUE_INTEGER :
			return scm_long2num (val->v_int.val);

		case VALUE_FLOAT :
			return gh_double2scm (val->v_float.val);

		case VALUE_CELLRANGE :
			/* FIXME : Support inverted ranges */
			return scm_cons (scm_symbolfrom0str ("cell-range"),
					 scm_cons (cell_ref_to_scm(val->v_range.cell.a, cell_ref),
						   cell_ref_to_scm (val->v_range.cell.b, cell_ref)));

						   case VALUE_ARRAY :
			{
				int x, y, i, ii;
				SCM ls;

				x = val->v_array.x;
				y = val->v_array.y;

				ls = gh_eval_str ("'()");

				/* FIXME : I added the value_to_scm wrapper. This seems more correct */
				for (i = 0; i < y; i++)
					for (ii = 0; i < x; i++)
						ls = scm_cons (value_to_scm (val->v_array.vals[ii][i], cell_ref), ls);
				return scm_reverse (ls);
			}
	}

	return SCM_UNSPECIFIED;
}

Value*
scm_to_value (SCM scm)
{
	if (SCM_NIMP(scm) && SCM_STRINGP(scm)) {
		return value_new_string (SCM_CHARS(scm));

	} else if (SCM_NFALSEP(scm_number_p(scm))) {
		return value_new_float ((float_t)scm_num2dbl(scm, 0));
		
	} else if (SCM_NIMP(scm) && SCM_CONSP(scm))
	{
		/*
		  FIXME;
		*/
		if (scm_eq_p(SCM_CAR(scm), scm_symbolfrom0str("cell-range"))
		    && SCM_NIMP(SCM_CDR(scm)) && SCM_CONSP(SCM_CDR(scm)))
		{
			CellRef a = scm_to_cell_ref(SCM_CADR(scm));
			CellRef b = scm_to_cell_ref(SCM_CDDR(scm));

			/* The refs are always absolute so the 0,0 is irrelevant */
			return value_new_cellrange (&a, &b, 0, 0);
		}
	}

	else if (gh_boolean_p (scm))
		      
		return value_new_bool ((gboolean) gh_scm2bool (scm));		       

	return NULL;		/* maybe we should return something more meaningful!? */
}

SCM
expr_to_scm (ExprTree *expr, CellRef cell_ref)
{
	switch (expr->any.oper)
	{
		case OPER_EQUAL :
			return SCM_LIST3(scm_symbolfrom0str("="),
					 expr_to_scm(expr->binary.value_a, cell_ref),
					 expr_to_scm(expr->binary.value_b, cell_ref));

		case OPER_GT :
			return SCM_LIST3(scm_symbolfrom0str(">"),
					 expr_to_scm(expr->binary.value_a, cell_ref),
					 expr_to_scm(expr->binary.value_b, cell_ref));

		case OPER_LT :
			return SCM_LIST3(scm_symbolfrom0str("<"),
					 expr_to_scm(expr->binary.value_a, cell_ref),
					 expr_to_scm(expr->binary.value_b, cell_ref));

		case OPER_GTE :
			return SCM_LIST3(scm_symbolfrom0str(">="),
					 expr_to_scm(expr->binary.value_a, cell_ref),
					 expr_to_scm(expr->binary.value_b, cell_ref));

		case OPER_LTE :
			return SCM_LIST3(scm_symbolfrom0str("<="),
					 expr_to_scm(expr->binary.value_a, cell_ref),
					 expr_to_scm(expr->binary.value_b, cell_ref));

		case OPER_NOT_EQUAL :
			return SCM_LIST3(scm_symbolfrom0str("<>"),
					 expr_to_scm(expr->binary.value_a, cell_ref),
					 expr_to_scm(expr->binary.value_b, cell_ref));

					 case OPER_ADD :
			return SCM_LIST3(scm_symbolfrom0str("+"),
					 expr_to_scm(expr->binary.value_a, cell_ref),
					 expr_to_scm(expr->binary.value_b, cell_ref));

		case OPER_SUB :
			return SCM_LIST3(scm_symbolfrom0str("-"),
					 expr_to_scm(expr->binary.value_a, cell_ref),
					 expr_to_scm(expr->binary.value_b, cell_ref));

		case OPER_UNARY_PLUS :
			return SCM_LIST2(scm_symbolfrom0str("+"),
					 expr_to_scm(expr->unary.value, cell_ref));

		case OPER_UNARY_NEG :
			return SCM_LIST2(scm_symbolfrom0str("neg"),
					 expr_to_scm(expr->unary.value, cell_ref));

		case OPER_MULT :
			return SCM_LIST3(scm_symbolfrom0str("*"),
					 expr_to_scm(expr->binary.value_a, cell_ref),
					 expr_to_scm(expr->binary.value_b, cell_ref));

		case OPER_DIV :
			return SCM_LIST3(scm_symbolfrom0str("/"),
					 expr_to_scm(expr->binary.value_a, cell_ref),
					 expr_to_scm(expr->binary.value_b, cell_ref));

		case OPER_PERCENT :
			return SCM_LIST3(scm_symbolfrom0str("/"),
					 expr_to_scm(expr->unary.value, cell_ref),
					 gh_double2scm(100.));

		case OPER_EXP :
			return SCM_LIST3(scm_symbolfrom0str("expt"),
					 expr_to_scm(expr->binary.value_a, cell_ref),
					 expr_to_scm(expr->binary.value_b, cell_ref));

		case OPER_CONCAT :
			return SCM_LIST3(scm_symbolfrom0str("string-append"),
					 expr_to_scm(expr->binary.value_a, cell_ref),
					 expr_to_scm(expr->binary.value_b, cell_ref));
			
	       case OPER_FUNCALL :
	               return SCM_UNSPECIFIED;
		  /*
			return SCM_LIST3(scm_symbolfrom0str("funcall"),
					 scm_makfrom0str(expr->func.func->name),
					 list_to_scm(expr->func.arg_list, cell_ref));
		  */
		case OPER_CONSTANT :
			return value_to_scm(expr->constant.value, cell_ref);

			case OPER_VAR :
			return scm_cons(scm_symbolfrom0str("var"),
					cell_ref_to_scm(expr->var.ref, cell_ref));

	        case OPER_NAME :

	        case OPER_ARRAY :
		

		/* FIXME : default : */
	}

	return SCM_UNSPECIFIED;
}
