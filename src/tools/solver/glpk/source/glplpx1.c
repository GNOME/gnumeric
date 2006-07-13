/* glplpx1.c (problem creating and modifying routines) */

/*----------------------------------------------------------------------
-- This code is part of GNU Linear Programming Kit (GLPK).
--
-- Copyright (C) 2000, 01, 02, 03, 04, 05, 06 Andrew Makhorin,
-- Department for Applied Informatics, Moscow Aviation Institute,
-- Moscow, Russia. All rights reserved. E-mail: <mao@mai2.rcnet.ru>.
--
-- GLPK is free software; you can redistribute it and/or modify it
-- under the terms of the GNU General Public License as published by
-- the Free Software Foundation; either version 2, or (at your option)
-- any later version.
--
-- GLPK is distributed in the hope that it will be useful, but WITHOUT
-- ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
-- or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
-- License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with GLPK; see the file COPYING. If not, write to the Free
-- Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
-- 02110-1301, USA.
----------------------------------------------------------------------*/

#include <float.h>
#include <math.h>
#include <string.h>
#include "glplib.h"
#define _GLPLPX_UNLOCK
#include "glplpx.h"

/*----------------------------------------------------------------------
-- lpx_create_prob - create problem object.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- LPX *lpx_create_prob(void);
--
-- *Description*
--
-- The routine lpx_create_prob creates a new LP/MIP problem object.
--
-- Being created the problem object corresponds to an "empty" problem,
-- which has no rows and columns.
--
-- *Returns*
--
-- The routine returns a pointer to the problem object created. */

LPX *lpx_create_prob(void)
{     LPX *lp;
      int m_max = 50, n_max = 100;
      lp = umalloc(sizeof(LPX));
      /* memory management */
      lp->row_pool = dmp_create_pool(sizeof(LPXROW));
      lp->col_pool = dmp_create_pool(sizeof(LPXCOL));
      lp->aij_pool = dmp_create_pool(sizeof(LPXAIJ));
      lp->str_pool = create_str_pool();
      lp->str_buf = ucalloc(255+1, sizeof(char));
      /* LP/MIP data */
      lp->name = NULL;
      lp->klass = LPX_LP;
      lp->obj = NULL;
      lp->dir = LPX_MIN;
      lp->c0 = 0.0;
      lp->m_max = m_max;
      lp->n_max = n_max;
      lp->m = 0;
      lp->n = 0;
      lp->row = ucalloc(1+m_max, sizeof(LPXROW *));
      lp->col = ucalloc(1+n_max, sizeof(LPXCOL *));
#if 1 /* 15/VIII-2004 */
      lp->r_tree = NULL;
      lp->c_tree = NULL;
#endif
      /* LP basis */
      lp->b_stat = LPX_B_UNDEF;
      lp->basis = ucalloc(1+m_max, sizeof(int));
      lp->b_inv = NULL;
      /* LP/MIP solution */
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->some = 0;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      /* control parameters and statistics */
      lpx_reset_parms(lp);
      return lp;
}

/*----------------------------------------------------------------------
-- lpx_set_prob_name - assign (change) problem name.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_prob_name(LPX *lp, char *name);
--
-- *Description*
--
-- The routine lpx_set_prob_name assigns a given symbolic name to the
-- problem object, which the parameter lp points to.
--
-- If the parameter name is NULL or empty string, the routine erases an
-- existing name of the problem object. */

void lpx_set_prob_name(LPX *lp, char *name)
{     if (name == NULL || name[0] == '\0')
      {  if (lp->name != NULL)
         {  delete_str(lp->name);
            lp->name = NULL;
         }
      }
      else
      {  if (strlen(name) > 255)
            fault("lpx_set_prob_name: problem name too long");
         if (lp->name == NULL) lp->name = create_str(lp->str_pool);
         set_str(lp->name, name);
      }
      return;
}

/*----------------------------------------------------------------------
-- lpx_set_class - set (change) problem class.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_class(LPX *lp, int klass);
--
-- *Description*
--
-- The routine lpx_set_class sets (changes) the class of the problem
-- object as specified by the parameter klass:
--
-- LPX_LP  - pure linear programming (LP) problem;
-- LPX_MIP - mixed integer programming (MIP) problem. */

void lpx_set_class(LPX *lp, int klass)
{     if (!(klass == LPX_LP || klass == LPX_MIP))
         fault("lpx_set_class: klass = %d; invalid problem class",
            klass);
      lp->klass = klass;
      return;
}

/*----------------------------------------------------------------------
-- lpx_set_obj_name - assign (change) objective function name.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_obj_name(LPX *lp, char *name);
--
-- *Description*
--
-- The routine lpx_set_obj_name assigns a given symbolic name to the
-- objective function of the specified problem object.
--
-- If the parameter name is NULL or empty string, the routine erases an
-- existing name of the objective function. */

void lpx_set_obj_name(LPX *lp, char *name)
{     if (name == NULL || name[0] == '\0')
      {  if (lp->obj != NULL)
         {  delete_str(lp->obj);
            lp->obj = NULL;
         }
      }
      else
      {  if (strlen(name) > 255)
            fault("lpx_set_obj_name: objective name too long");
         if (lp->obj == NULL) lp->obj = create_str(lp->str_pool);
         set_str(lp->obj, name);
      }
      return;
}

/*----------------------------------------------------------------------
-- lpx_set_obj_dir - set (change) optimization direction flag.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_obj_dir(LPX *lp, int dir);
--
-- *Description*
--
-- The routine lpx_set_obj_dir sets (changes) optimization direction
-- flag (i.e. "sense" of the objective function) as specified by the
-- parameter dir:
--
-- LPX_MIN - minimization;
-- LPX_MAX - maximization. */

void lpx_set_obj_dir(LPX *lp, int dir)
{     if (!(dir == LPX_MIN || dir == LPX_MAX))
         fault("lpx_set_obj_dir: dir = %d; invalid direction flag",
            dir);
      lp->dir = dir;
      /* invalidate solution components */
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_add_rows - add new rows to problem object.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_add_rows(LPX *lp, int nrs);
--
-- *Description*
--
-- The routine lpx_add_rows adds nrs rows (constraints) to a problem
-- object, which the parameter lp points to. New rows are always added
-- to the end of the row list, so the ordinal numbers of existing rows
-- remain unchanged.
--
-- Each new row is initiallyg_free (unbounded) and has empty list of the
-- constraint coefficients.
--
-- *Returns*
--
-- The routine lpx_add_rows returns the ordinal number of the first new
-- row added to the problem object. */

int lpx_add_rows(LPX *lp, int nrs)
{     LPXROW *row;
      int m_new, i;
      /* determine new number of rows */
      if (nrs < 1)
         fault("lpx_add_rows: nrs = %d; invalid number of rows", nrs);
      m_new = lp->m + nrs;
      insist(m_new > 0);
      /* increase the room, if necessary */
      if (lp->m_max < m_new)
      {  LPXROW **save = lp->row;
         while (lp->m_max < m_new)
         {  lp->m_max += lp->m_max;
            insist(lp->m_max > 0);
         }
         lp->row = ucalloc(1+lp->m_max, sizeof(LPXROW *));
         memcpy(&lp->row[1], &save[1], lp->m * sizeof(LPXROW *));
         ufree(save);
         /* do not forget about the basis header */
         ufree(lp->basis);
         lp->basis = ucalloc(1+lp->m_max, sizeof(int));
      }
      /* add new rows to the end of the row list */
      for (i = lp->m+1; i <= m_new; i++)
      {  /* create row descriptor */
         lp->row[i] = row = dmp_get_atom(lp->row_pool);
         row->i = i;
         row->name = NULL;
#if 1 /* 15/VIII-2004 */
         row->node = NULL;
#endif
         row->type = LPX_FR;
         row->lb = row->ub = 0.0;
         row->ptr = NULL;
         row->rii = 1.0;
         row->stat = LPX_BS;
         row->b_ind = -1;
         row->prim = row->dual = 0.0;
         row->pval = row->dval = 0.0;
         row->mipx = 0.0;
      }
      /* set new number of rows */
      lp->m = m_new;
      /* invalidate the basis and solution components */
      lp->b_stat = LPX_B_UNDEF;
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      /* return the ordinal number of the first row added */
      return m_new - nrs + 1;
}

/*----------------------------------------------------------------------
-- lpx_add_cols - add new columns to problem object.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- int lpx_add_cols(LPX *lp, int ncs);
--
-- *Description*
--
-- The routine lpx_add_cols adds ncs columns (structural variables) to
-- a problem object, which the parameter lp points to. New columns are
-- always added to the end of the column list, so the ordinal numbers
-- of existing columns remain unchanged.
--
-- Each new column is initially fixed at zero and has empty list of the
-- constraint coefficients.
--
-- *Returns*
--
-- The routine lpx_add_cols returns the ordinal number of the first new
-- column added to the problem object. */

int lpx_add_cols(LPX *lp, int ncs)
{     LPXCOL *col;
      int n_new, j;
      /* determine new number of columns */
      if (ncs < 1)
         fault("lpx_add_cols: ncs = %d; invalid number of columns",
            ncs);
      n_new = lp->n + ncs;
      insist(n_new > 0);
      /* increase the room, if necessary */
      if (lp->n_max < n_new)
      {  LPXCOL **save = lp->col;
         while (lp->n_max < n_new)
         {  lp->n_max += lp->n_max;
            insist(lp->n_max > 0);
         }
         lp->col = ucalloc(1+lp->n_max, sizeof(LPXCOL *));
         memcpy(&lp->col[1], &save[1], lp->n * sizeof(LPXCOL *));
         ufree(save);
      }
      /* add new columns to the end of the column list */
      for (j = lp->n+1; j <= n_new; j++)
      {  /* create column descriptor */
         lp->col[j] = col = dmp_get_atom(lp->col_pool);
         col->j = j;
         col->name = NULL;
#if 1 /* 15/VIII-2004 */
         col->node = NULL;
#endif
         col->kind = LPX_CV;
         col->type = LPX_FX;
         col->lb = col->ub = 0.0;
         col->coef = 0.0;
         col->ptr = NULL;
         col->sjj = 1.0;
         col->stat = LPX_NS;
         col->b_ind = -1;
         col->prim = col->dual = 0.0;
         col->pval = col->dval = 0.0;
         col->mipx = 0.0;
      }
      /* set new number of columns */
      lp->n = n_new;
      /* invalidate the basis and solution components */
      lp->b_stat = LPX_B_UNDEF;
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      /* return the ordinal number of the first column added */
      return n_new - ncs + 1;
}

/*----------------------------------------------------------------------
-- lpx_set_row_name - assign (change) row name.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_row_name(LPX *lp, int i, char *name);
--
-- *Description*
--
-- The routine lpx_set_row_name assigns a given symbolic name to i-th
-- row (auxiliary variable) of the specified problem object.
--
-- If the parameter name is NULL or empty string, the routine erases an
-- existing name of i-th row. */

void lpx_set_row_name(LPX *lp, int i, char *name)
{     LPXROW *row;
      if (!(1 <= i && i <= lp->m))
         fault("lpx_set_row_name: i = %d; row number out of range", i);
      row = lp->row[i];
#if 1 /* 15/VIII-2004 */
      if (row->node != NULL)
      {  insist(lp->r_tree != NULL);
         avl_delete_node(lp->r_tree, row->node);
         row->node = NULL;
      }
#endif
      if (name == NULL || name[0] == '\0')
      {  if (row->name != NULL)
         {  delete_str(row->name);
            row->name = NULL;
         }
      }
      else
      {  if (strlen(name) > 255)
            fault("lpx_set_row_name: i = %d; row name too long", i);
         if (row->name == NULL) row->name = create_str(lp->str_pool);
         set_str(row->name, name);
      }
#if 1 /* 15/VIII-2004 */
      if (lp->r_tree != NULL && row->name != NULL)
      {  insist(row->node == NULL);
         row->node = avl_insert_by_key(lp->r_tree, row->name);
         row->node->link = row;
      }
#endif
      return;
}

/*----------------------------------------------------------------------
-- lpx_set_col_name - assign (change) column name.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_col_name(LPX *lp, int j, char *name);
--
-- *Description*
--
-- The routine lpx_set_col_name assigns a given symbolic name to j-th
-- column (structural variable) of the specified problem object.
--
-- If the parameter name is NULL or empty string, the routine erases an
-- existing name of j-th column. */

void lpx_set_col_name(LPX *lp, int j, char *name)
{     LPXCOL *col;
      if (!(1 <= j && j <= lp->n))
         fault("lpx_set_col_name: j = %d; column number out of range",
            j);
      col = lp->col[j];
#if 1 /* 15/VIII-2004 */
      if (col->node != NULL)
      {  insist(lp->c_tree != NULL);
         avl_delete_node(lp->c_tree, col->node);
         col->node = NULL;
      }
#endif
      if (name == NULL || name[0] == '\0')
      {  if (col->name != NULL)
         {  delete_str(col->name);
            col->name = NULL;
         }
      }
      else
      {  if (strlen(name) > 255)
            fault("lpx_set_col_name: j = %d; column name too long", j);
         if (col->name == NULL) col->name = create_str(lp->str_pool);
         set_str(col->name, name);
      }
#if 1 /* 15/VIII-2004 */
      if (lp->c_tree != NULL && col->name != NULL)
      {  insist(col->node == NULL);
         col->node = avl_insert_by_key(lp->c_tree, col->name);
         col->node->link = col;
      }
#endif
      return;
}

/*----------------------------------------------------------------------
-- lpx_set_col_kind - set (change) column kind.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_col_kind(LPX *lp, int j, int kind);
--
-- *Description*
--
-- The routine lpx_set_col_kind sets (changes) the kind of j-th column
-- (structural variable) as specified by the parameter kind:
--
-- LPX_CV - continuous variable;
-- LPX_IV - integer variable. */

void lpx_set_col_kind(LPX *lp, int j, int kind)
{     if (lp->klass != LPX_MIP)
         fault("lpx_set_col_kind: not a MIP problem");
      if (!(1 <= j && j <= lp->n))
         fault("lpx_set_col_kind: j = %d; column number out of range",
            j);
      if (!(kind == LPX_CV || kind == LPX_IV))
         fault("lpx_set_col_kind: j = %d; kind = %d; invalid column kin"
            "d", j, kind);
      lp->col[j]->kind = kind;
      lp->i_stat = LPX_I_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_set_row_bnds - set (change) row bounds.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_row_bnds(LPX *lp, int i, int type, gnm_float lb,
--    gnm_float ub);
--
-- *Description*
--
-- The routine lpx_set_row_bnds sets (changes) type and bounds of i-th
-- row of the specified problem object.
--
-- Parameters type, lb, and ub specify the type, lower bound, and upper
-- bound, respectively, as follows:
--
--     Type          Bounds            Comments
--    ------------------------------------------------------
--    LPX_FR   -inf <  x <  +inf   Free variable
--    LPX_LO     lb <= x <  +inf   Variable with lower bound
--    LPX_UP   -inf <  x <=  ub    Variable with upper bound
--    LPX_DB     lb <= x <=  ub    Double-bounded variable
--    LPX_FX           x  =  lb    Fixed variable
--
-- where x is the auxiliary variable associated with i-th row.
--
-- If the row has no lower bound, the parameter lb is ignored. If the
-- row has no upper bound, the parameter ub is ignored. If the row is
-- of fixed type, only the parameter lb is used while the parameter ub
-- is ignored. */

void lpx_set_row_bnds(LPX *lp, int i, int type, gnm_float lb, gnm_float ub)
{     LPXROW *row;
      if (!(1 <= i && i <= lp->m))
         fault("lpx_set_row_bnds: i = %d; row number out of range", i);
      row = lp->row[i];
      row->type = type;
      switch (type)
      {  case LPX_FR:
            row->lb = row->ub = 0.0;
            if (row->stat != LPX_BS) row->stat = LPX_NF;
            break;
         case LPX_LO:
            row->lb = lb, row->ub = 0.0;
            if (row->stat != LPX_BS) row->stat = LPX_NL;
            break;
         case LPX_UP:
            row->lb = 0.0, row->ub = ub;
            if (row->stat != LPX_BS) row->stat = LPX_NU;
            break;
         case LPX_DB:
            row->lb = lb, row->ub = ub;
            if (!(row->stat == LPX_BS ||
                  row->stat == LPX_NL || row->stat == LPX_NU))
               row->stat = (gnm_abs(lb) <= gnm_abs(ub) ? LPX_NL : LPX_NU);
            break;
         case LPX_FX:
            row->lb = row->ub = lb;
            if (row->stat != LPX_BS) row->stat = LPX_NS;
            break;
         default:
            fault("lpx_set_row_bnds: i = %d; type = %d; invalid row typ"
               "e", i, type);
      }
      /* invalidate solution components */
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_set_col_bnds - set (change) column bounds.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_col_bnds(LPX *lp, int j, int type, gnm_float lb,
--    gnm_float ub);
--
-- *Description*
--
-- The routine lpx_set_col_bnds sets (changes) type and bounds of j-th
-- column of the specified problem object.
--
-- Parameters type, lb, and ub specify the type, lower bound, and upper
-- bound, respectively, as follows:
--
--     Type          Bounds            Comments
--    ------------------------------------------------------
--    LPX_FR   -inf <  x <  +inf   Free variable
--    LPX_LO     lb <= x <  +inf   Variable with lower bound
--    LPX_UP   -inf <  x <=  ub    Variable with upper bound
--    LPX_DB     lb <= x <=  ub    Double-bounded variable
--    LPX_FX           x  =  lb    Fixed variable
--
-- where x is the structural variable associated with j-th column.
--
-- If the column has no lower bound, the parameter lb is ignored. If the
-- column has no upper bound, the parameter ub is ignored. If the column
-- is of fixed type, only the parameter lb is used while the parameter
-- ub is ignored. */

void lpx_set_col_bnds(LPX *lp, int j, int type, gnm_float lb, gnm_float ub)
{     LPXCOL *col;
      if (!(1 <= j && j <= lp->n))
         fault("lpx_set_col_bnds: j = %d; column number out of range",
            j);
      col = lp->col[j];
      col->type = type;
      switch (type)
      {  case LPX_FR:
            col->lb = col->ub = 0.0;
            if (col->stat != LPX_BS) col->stat = LPX_NF;
            break;
         case LPX_LO:
            col->lb = lb, col->ub = 0.0;
            if (col->stat != LPX_BS) col->stat = LPX_NL;
            break;
         case LPX_UP:
            col->lb = 0.0, col->ub = ub;
            if (col->stat != LPX_BS) col->stat = LPX_NU;
            break;
         case LPX_DB:
            col->lb = lb, col->ub = ub;
            if (!(col->stat == LPX_BS ||
                  col->stat == LPX_NL || col->stat == LPX_NU))
               col->stat = (gnm_abs(lb) <= gnm_abs(ub) ? LPX_NL : LPX_NU);
            break;
         case LPX_FX:
            col->lb = col->ub = lb;
            if (col->stat != LPX_BS) col->stat = LPX_NS;
            break;
         default:
            fault("lpx_set_col_bnds: j = %d; type = %d; invalid column "
               "type", j, type);
      }
      /* invalidate solution components */
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_set_obj_coef - set (change) obj. coefficient or constant term.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_obj_coef(LPX *lp, int j, gnm_float coef);
--
-- *Description*
--
-- The routine lpx_set_obj_coef sets (changes) objective coefficient at
-- j-th structural variable (column) of the specified problem object.
--
-- If the parameter j is 0, the routine sets (changes) the constant term
-- (i.e. "shift") of the objective function. */

void lpx_set_obj_coef(LPX *lp, int j, gnm_float coef)
{     if (!(0 <= j && j <= lp->n))
         fault("lpx_set_obj_coef: j = %d; column number out of range",
            j);
      if (j == 0)
         lp->c0 = coef;
      else
         lp->col[j]->coef = coef;
      /* invalidate solution components */
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_set_mat_row - set (replace) row of the constraint matrix.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_mat_row(LPX *lp, int i, int len, int ind[],
--    gnm_float val[]);
--
-- *Description*
--
-- The routine lpx_set_mat_row stores (replaces) the contents of i-th
-- row of the constraint matrix of the specified problem object.
--
-- Column indices and numeric values of new row elements must be placed
-- in locations ind[1], ..., ind[len] and val[1], ..., val[len], where
-- 0 <= len <= n is new length of i-th row, n is the current number of
-- columns in the problem object. Should note that zero elements as well
-- as elements with identical column indices are not allowed.
--
-- If the parameter len is zero, the both parameters ind and val can be
-- specified as NULL. */

void lpx_set_mat_row(LPX *lp, int i, int len, int ind[], gnm_float val[])
{     LPXROW *row;
      LPXCOL *col;
      LPXAIJ *aij;
      int j, k;
      /* obtain pointer to i-th row */
      if (!(1 <= i && i <= lp->m))
         fault("lpx_set_mat_row: i = %d; row number out of range", i);
      row = lp->row[i];
      /* remove all existing elements from i-th row */
      while (row->ptr != NULL)
      {  /* take next element in the row */
         aij = row->ptr;
         /* remove the element from the row list */
         row->ptr = aij->r_next;
         /* obtain pointer to corresponding column */
         col = aij->col;
         /* remove the element from the column list */
         if (aij->c_prev == NULL)
            col->ptr = aij->c_next;
         else
            aij->c_prev->c_next = aij->c_next;
         if (aij->c_next == NULL)
            ;
         else
            aij->c_next->c_prev = aij->c_prev;
         /* return the element to the memory pool */
         dmp_free_atom(lp->aij_pool, aij);
      }
      /* store new contents of i-th row */
      if (!(0 <= len && len <= lp->n))
         fault("lpx_set_mat_row: i = %d; len = %d; invalid row length",
            i, len);
      for (k = 1; k <= len; k++)
      {  /* take number j of corresponding column */
         j = ind[k];
         /* obtain pointer to j-th column */
         if (!(1 <= j && j <= lp->n))
            fault("lpx_set_mat_row: i = %d; ind[%d] = %d; column index "
               "out of range", i, k, j);
         col = lp->col[j];
         /* if there is element with the same column index, it can only
            be found in the beginning of j-th column list */
         if (col->ptr != NULL && col->ptr->row->i == i)
            fault("lpx_set_mat_row: i = %d; ind[%d] = %d; duplicate col"
               "umn indices not allowed", i, k, j);
         /* create new element */
         aij = dmp_get_atom(lp->aij_pool);
         aij->row = row;
         aij->col = col;
         if (val[k] == 0.0)
            fault("lpx_set_mat_row: i = %d; ind[%d] = %d; zero element "
               "not allowed", i, k, j);
         aij->val = val[k];
         /* add the new element to the beginning of i-th row and j-th
            column lists */
         aij->r_prev = NULL;
         aij->r_next = row->ptr;
         aij->c_prev = NULL;
         aij->c_next = col->ptr;
         if (row->ptr != NULL) row->ptr->r_prev = aij;
         if (col->ptr != NULL) col->ptr->c_prev = aij;
         row->ptr = col->ptr = aij;
      }
      /* invalidate the basis and solution components */
      lp->b_stat = LPX_B_UNDEF;
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_set_mat_col - set (replace) column of the constraint matrix.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_mat_col(LPX *lp, int j, int len, int ind[],
--    gnm_float val[]);
--
-- *Description*
--
-- The routine lpx_set_mat_col stores (replaces) the contents of j-th
-- column of the constraint matrix of the specified problem object.
--
-- Row indices and numeric values of new column elements must be placed
-- in locations ind[1], ..., ind[len] and val[1], ..., val[len], where
-- 0 <= len <= m is new length of j-th column, m is the current number
-- of rows in the problem object. Should note that zero elements as well
-- as elements with identical row indices are not allowed.
--
-- If the parameter len is zero, the both parameters ind and val can be
-- specified as NULL. */

void lpx_set_mat_col(LPX *lp, int j, int len, int ind[], gnm_float val[])
{     LPXROW *row;
      LPXCOL *col;
      LPXAIJ *aij;
      int i, k;
      /* obtain pointer to j-th column */
      if (!(1 <= j && j <= lp->n))
         fault("lpx_set_mat_col: j = %d; column number out of range",
            j);
      col = lp->col[j];
      /* remove all existing elements from j-th column */
      while (col->ptr != NULL)
      {  /* take next element in the column */
         aij = col->ptr;
         /* remove the element from the column list */
         col->ptr = aij->c_next;
         /* obtain pointer to corresponding row */
         row = aij->row;
         /* remove the element from the row list */
         if (aij->r_prev == NULL)
            row->ptr = aij->r_next;
         else
            aij->r_prev->r_next = aij->r_next;
         if (aij->r_next == NULL)
            ;
         else
            aij->r_next->r_prev = aij->r_prev;
         /* return the element to the memory pool */
         dmp_free_atom(lp->aij_pool, aij);
      }
      /* store new contents of j-th column */
      if (!(0 <= len && len <= lp->m))
         fault("lpx_set_mat_col: j = %d; len = %d; invalid column lengt"
            "h", j, len);
      for (k = 1; k <= len; k++)
      {  /* take number i of corresponding row */
         i = ind[k];
         /* obtain pointer to i-th row */
         if (!(1 <= i && i <= lp->m))
            fault("lpx_set_mat_col: j = %d; ind[%d] = %d; row index out"
               " of range", j, k, i);
         row = lp->row[i];
         /* if there is element with the same row index, it can only be
            found in the beginning of i-th row list */
         if (row->ptr != NULL && row->ptr->col->j == j)
            fault("lpx_set_mat_col: j = %d; ind[%d] = %d; duplicate row"
               " indices not allowed", j, k, i);
         /* create new element */
         aij = dmp_get_atom(lp->aij_pool);
         aij->row = row;
         aij->col = col;
         if (val[k] == 0.0)
            fault("lpx_set_mat_col: j = %d; ind[%d] = %d; zero element "
               "not allowed", j, k, i);
         aij->val = val[k];
         /* add the new element to the beginning of i-th row and j-th
            column lists */
         aij->r_prev = NULL;
         aij->r_next = row->ptr;
         aij->c_prev = NULL;
         aij->c_next = col->ptr;
         if (row->ptr != NULL) row->ptr->r_prev = aij;
         if (col->ptr != NULL) col->ptr->c_prev = aij;
         row->ptr = col->ptr = aij;
      }
      /* invalidate the basis and solution components */
      lp->b_stat = LPX_B_UNDEF;
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_load_matrix - load (replace) the whole constraint matrix.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_load_matrix(LPX *lp, int ne, int ia[], int ja[],
--    gnm_float ar[]);
--
-- *Description*
--
-- The routine lpx_load_matrix loads the constraint matrix passed in
-- the arrays ia, ja, and ar into the specified problem object. Before
-- loading the current contents of the constraint matrix is destroyed.
--
-- Constraint coefficients (elements of the constraint matrix) should
-- be specified as triplets (ia[k], ja[k], ar[k]) for k = 1, ..., ne,
-- where ia[k] is the row index, ja[k] is the column index, ar[k] is a
-- numeric value of corresponding constraint coefficient. The parameter
-- ne specifies the total number of (non-zero) elements in the matrix
-- to be loaded. Note that coefficients with identical indices as well
-- as zero coefficients are not allowed.
--
-- If the parameter ne is zero, the parameters ia, ja, and ar can be
-- specified as NULL. */

void lpx_load_matrix(LPX *lp, int ne, int ia[], int ja[], gnm_float ar[])
{     LPXROW *row;
      LPXCOL *col;
      LPXAIJ *aij;
      int i, j, k;
      /* clear the constraint matrix */
      for (i = 1; i <= lp->m; i++) lp->row[i]->ptr = NULL;
      for (j = 1; j <= lp->n; j++) lp->col[j]->ptr = NULL;
      dmp_free_all(lp->aij_pool);
      /* load the new contents of the constraint matrix and build its
         row lists */
      if (ne < 0)
         fault("lpx_load_matrix: ne = %d; invalid number of matrix elem"
            "ents", ne);
      for (k = 1; k <= ne; k++)
      {  /* take indices of new element */
         i = ia[k], j = ja[k];
         /* obtain pointer to i-th row */
         if (!(1 <= i && i <= lp->m))
            fault("lpx_load_matrix: ia[%d] = %d; row index out of range"
               , k, i);
         row = lp->row[i];
         /* obtain pointer to j-th column */
         if (!(1 <= j && j <= lp->n))
            fault("lpx_load_matrix: ja[%d] = %d; column index out of ra"
               "nge", k, j);
         col = lp->col[j];
         /* create new element */
         aij = dmp_get_atom(lp->aij_pool);
         aij->row = row;
         aij->col = col;
         if (ar[k] == 0.0)
            fault("lpx_load_matrix: ar[%d] = 0; zero element not allowe"
               "d", k);
         aij->val = ar[k];
         /* add the new element to the beginning of i-th row list */
         aij->r_prev = NULL;
         aij->r_next = row->ptr;
         if (row->ptr != NULL) row->ptr->r_prev = aij;
         row->ptr = aij;
      }
      /* build column lists of the constraint matrix and check elements
         with identical indices */
      for (i = 1; i <= lp->m; i++)
      {  for (aij = lp->row[i]->ptr; aij != NULL; aij = aij->r_next)
         {  /* obtain pointer to corresponding column */
            col = aij->col;
            /* if there is element with identical indices, it can only
               be found in the beginning of j-th column list */
            if (col->ptr != NULL && col->ptr->row->i == i)
            {  for (k = ne; k >= 1; k--)
                  if (ia[k] == i && ja[k] == col->j) break;
               fault("lpx_load_mat: ia[%d] = %d; ja[%d] = %d; duplicate"
                  " elements not allowed", k, i, k, col->j);
            }
            /* add the element to the beginning of j-th column list */
            aij->c_prev = NULL;
            aij->c_next = col->ptr;
            if (col->ptr != NULL) col->ptr->c_prev = aij;
            col->ptr = aij;
         }
      }
      /* invalidate the basis and solution components */
      lp->b_stat = LPX_B_UNDEF;
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_order_matrix - order rows and columns of the constraint matrix.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_order_matrix(LPX *lp);
--
-- *Description*
--
-- The routine lpx_order_matrix rebuilds row and column linked lists of
-- the constraint matrix of the specified problem object.
--
-- On exit the constraint matrix is not changed, however, elements in
-- the row linked lists are ordered in ascending their column indices,
-- and elements in the column linked are ordered in ascending their row
-- indices. */

void lpx_order_matrix(LPX *lp)
{     LPXAIJ *aij;
      int i, j;
      /* rebuild row lists */
      for (i = lp->m; i >= 1; i--)
         lp->row[i]->ptr = NULL;
      for (j = lp->n; j >= 1; j--)
      {  for (aij = lp->col[j]->ptr; aij != NULL; aij = aij->c_next)
         {  i = aij->row->i;
            aij->r_prev = NULL;
            aij->r_next = lp->row[i]->ptr;
            if (aij->r_next != NULL) aij->r_next->r_prev = aij;
            lp->row[i]->ptr = aij;
         }
      }
      /* rebuild column lists */
      for (j = lp->n; j >= 1; j--)
         lp->col[j]->ptr = NULL;
      for (i = lp->m; i >= 1; i--)
      {  for (aij = lp->row[i]->ptr; aij != NULL; aij = aij->r_next)
         {  j = aij->col->j;
            aij->c_prev = NULL;
            aij->c_next = lp->col[j]->ptr;
            if (aij->c_next != NULL) aij->c_next->c_prev = aij;
            lp->col[j]->ptr = aij;
         }
      }
      return;
}

/*----------------------------------------------------------------------
-- lpx_set_rii - set (change) row scale factor.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_rii(LPX *lp, int i, gnm_float rii);
--
-- *Description*
--
-- The routine lpx_set_rii sets (changes) the scale factor r[i,i] for
-- i-th row of the specified problem object. */

void lpx_set_rii(LPX *lp, int i, gnm_float rii)
{     if (!(1 <= i && i <= lp->m))
         fault("lpx_set_rii: i = %d; row number out of range", i);
      if (rii <= 0.0)
         fault("lpx_set_rii: i = %d; rii = %g; invalid scale factor",
            i, rii);
      lp->row[i]->rii = rii;
      /* invalidate the basis */
      lp->b_stat = LPX_B_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_set sjj - set (change) column scale factor.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_sjj(LPX *lp, int j, gnm_float sjj);
--
-- *Description*
--
-- The routine lpx_set_sjj sets (changes) the scale factor s[j,j] for
-- j-th column of the specified problem object. */

void lpx_set_sjj(LPX *lp, int j, gnm_float sjj)
{     if (!(1 <= j && j <= lp->n))
         fault("lpx_set_sjj: j = %d; column number out of range", j);
      if (sjj <= 0.0)
         fault("lpx_set_sjj: j = %d; sjj = %g; invalid scale factor",
            j, sjj);
      lp->col[j]->sjj = sjj;
      /* invalidate the basis */
      lp->b_stat = LPX_B_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_set_row_stat - set (change) row status.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_row_stat(LPX *lp, int i, int stat);
--
-- *Description*
--
-- The routine lpx_set_row_stat sets (changes) status of the auxiliary
-- variable associated with i-th row.
--
-- The new status of the auxiliary variable should be specified by the
-- parameter stat as follows:
--
-- LPX_BS - basic variable;
-- LPX_NL - non-basic variable;
-- LPX_NU - non-basic variable on its upper bound; if the variable is
--          not gnm_float-bounded, this means the same as LPX_NL (only in
--          case of this routine);
-- LPX_NF - the same as LPX_NL (only in case of this routine);
-- LPX_NS - the same as LPX_NL (only in case of this routine). */

void lpx_set_row_stat(LPX *lp, int i, int stat)
{     LPXROW *row;
      if (!(1 <= i && i <= lp->m))
         fault("lpx_set_row_stat: i = %d; row number out of range", i);
      if (!(stat == LPX_BS || stat == LPX_NL || stat == LPX_NU ||
            stat == LPX_NF || stat == LPX_NS))
         fault("lpx_set_row_stat: i = %d; stat = %d; invalid status",
            i, stat);
      row = lp->row[i];
      if (stat != LPX_BS)
      {  switch (row->type)
         {  case LPX_FR: stat = LPX_NF; break;
            case LPX_LO: stat = LPX_NL; break;
            case LPX_UP: stat = LPX_NU; break;
            case LPX_DB: if (stat != LPX_NU) stat = LPX_NL; break;
            case LPX_FX: stat = LPX_NS; break;
            default: insist(row != row);
         }
      }
      row->stat = stat;
      /* invalidate the basis and solution components */
      lp->b_stat = LPX_B_UNDEF;
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_set_col_stat - set (change) column status.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_set_col_stat(LPX *lp, int j, int stat);
--
-- *Description*
--
-- The routine lpx_set_col_stat sets (changes) status of the structural
-- variable associated with j-th column.
--
-- The new status of the structural variable should be specified by the
-- parameter stat as follows:
--
-- LPX_BS - basic variable;
-- LPX_NL - non-basic variable;
-- LPX_NU - non-basic variable on its upper bound; if the variable is
--          not gnm_float-bounded, this means the same as LPX_NL (only in
--          case of this routine);
-- LPX_NF - the same as LPX_NL (only in case of this routine);
-- LPX_NS - the same as LPX_NL (only in case of this routine). */

void lpx_set_col_stat(LPX *lp, int j, int stat)
{     LPXCOL *col;
      if (!(1 <= j && j <= lp->n))
         fault("lpx_set_col_stat: j = %d; column number out of range",
            j);
      if (!(stat == LPX_BS || stat == LPX_NL || stat == LPX_NU ||
            stat == LPX_NF || stat == LPX_NS))
         fault("lpx_set_col_stat: j = %d; stat = %d; invalid status",
            j, stat);
      col = lp->col[j];
      if (stat != LPX_BS)
      {  switch (col->type)
         {  case LPX_FR: stat = LPX_NF; break;
            case LPX_LO: stat = LPX_NL; break;
            case LPX_UP: stat = LPX_NU; break;
            case LPX_DB: if (stat != LPX_NU) stat = LPX_NL; break;
            case LPX_FX: stat = LPX_NS; break;
            default: insist(col != col);
         }
      }
      col->stat = stat;
      /* invalidate the basis and solution components */
      lp->b_stat = LPX_B_UNDEF;
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_del_rows - delete specified rows from problem object.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_del_rows(LPX *lp, int nrs, int num[]);
--
-- *Description*
--
-- The routine lpx_del_rows deletes specified rows from the problem
-- object. Ordinal numbers of rows to be deleted should be placed in
-- locations num[1], ..., num[nrs], where nrs > 0.
--
-- Note that deleting rows involves changing ordinal numbers of other
-- rows remaining in the problem object. New ordinal numbers of the
-- remaining rows are assigned under the assumption that the original
-- order of rows is not changed. */

void lpx_del_rows(LPX *lp, int nrs, int num[])
{     LPXROW *row;
      int i, k, m_new;
      /* mark rows to be deleted */
      if (nrs < 1)
         fault("lpx_del_rows: nrs = %d; invalid number of rows", nrs);
      for (k = 1; k <= nrs; k++)
      {  /* take number i of row to be deleted */
         i = num[k];
         /* obtain pointer to i-th row */
         if (!(1 <= i && i <= lp->m))
            fault("lpx_del_rows: num[%d] = %d; row number out of range",
               k, i);
         row = lp->row[i];
         /* check that the row is not marked yet */
         if (row->i == 0)
            fault("lpx_del_rows: num[%d] = %d; duplicate row numbers no"
               "t allowed", k, i);
         /* erase symbolic name assigned to the row */
         lpx_set_row_name(lp, i, NULL);
#if 1 /* 15/VIII-2004 */
         insist(row->node == NULL);
#endif
         /* erase corresponding row of the constraint matrix */
         lpx_set_mat_row(lp, i, 0, NULL, NULL);
         /* mark the row to delete it */
         row->i = 0;
      }
      /* delete all marked rows from the row list */
      m_new = 0;
      for (i = 1; i <= lp->m; i++)
      {  /* obtain pointer to i-th row */
         row = lp->row[i];
         /* check if the row is marked or not */
         if (row->i == 0)
         {  /* the row is marked; delete it */
            dmp_free_atom(lp->row_pool, row);
         }
         else
         {  /* the row is not marked; keep it */
            row->i = ++m_new;
            lp->row[row->i] = row;
         }
      }
      /* set new number of rows */
      lp->m = m_new;
      /* invalidate the basis and solution components */
      lp->b_stat = LPX_B_UNDEF;
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_del_cols - delete specified columns from problem object.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_del_cols(LPX *lp, int ncs, int num[]);
--
-- *Description*
--
-- The routine lpx_del_cols deletes specified columns from the problem
-- object. Ordinal numbers of columns to be deleted should be placed in
-- locations num[1], ..., num[ncs], where ncs > 0.
--
-- Note that deleting columns involves changing ordinal numbers of
-- other columns remaining in the problem object. New ordinal numbers
-- of the remaining columns are assigned under the assumption that the
-- original order of columns is not changed. */

void lpx_del_cols(LPX *lp, int ncs, int num[])
{     LPXCOL *col;
      int j, k, n_new;
      /* mark columns to be deleted */
      if (ncs < 1)
         fault("lpx_del_cols: ncs = %d; invalid number of columns",
            ncs);
      for (k = 1; k <= ncs; k++)
      {  /* take number j of column to be deleted */
         j = num[k];
         /* obtain pointer to j-th column */
         if (!(1 <= j && j <= lp->n))
            fault("lpx_del_cols: num[%d] = %d; column number out of ran"
               "ge", k, j);
         col = lp->col[j];
         /* check that the column is not marked yet */
         if (col->j == 0)
            fault("lpx_del_cols: num[%d] = %d; duplicate column numbers"
               " not allowed", k, j);
         /* erase symbolic name assigned to the column */
         lpx_set_col_name(lp, j, NULL);
#if 1 /* 15/VIII-2004 */
         insist(col->node == NULL);
#endif
         /* erase corresponding column of the constraint matrix */
         lpx_set_mat_col(lp, j, 0, NULL, NULL);
         /* mark the column to delete it */
         col->j = 0;
      }
      /* delete all marked columns from the column list */
      n_new = 0;
      for (j = 1; j <= lp->n; j++)
      {  /* obtain pointer to j-th column */
         col = lp->col[j];
         /* check if the column is marked or not */
         if (col->j == 0)
         {  /* the column is marked; delete it */
            dmp_free_atom(lp->col_pool, col);
         }
         else
         {  /* the column is not marked; keep it */
            col->j = ++n_new;
            lp->col[col->j] = col;
         }
      }
      /* set new number of columns */
      lp->n = n_new;
      /* invalidate the basis and solution components */
      lp->b_stat = LPX_B_UNDEF;
      lp->p_stat = LPX_P_UNDEF;
      lp->d_stat = LPX_D_UNDEF;
      lp->t_stat = LPX_T_UNDEF;
      lp->i_stat = LPX_I_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_delete_prob - delete problem object.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_delete_prob(LPX *lp);
--
-- *Description*
--
-- The routine lpx_delete_prob deletes an LP/MIP problem object, which
-- the parameter lp points to, freeing all the memory allocated to this
-- object. */

void lpx_delete_prob(LPX *lp)
{     dmp_delete_pool(lp->row_pool);
      dmp_delete_pool(lp->col_pool);
      dmp_delete_pool(lp->aij_pool);
      dmp_delete_pool(lp->str_pool);
      ufree(lp->str_buf);
      ufree(lp->row);
      ufree(lp->col);
      if (lp->r_tree != NULL) avl_delete_tree(lp->r_tree);
      if (lp->c_tree != NULL) avl_delete_tree(lp->c_tree);
      ufree(lp->basis);
      if (lp->b_inv != NULL) inv_delete(lp->b_inv);
      ufree(lp);
      return;
}

#if 1 /* 15/VIII-2004 */
static int compare_names(void *info, void *key1, void *key2)
{     insist(info == NULL);
      return compare_str(key1, key2);
}

void lpx_create_index(LPX *lp)
{     if (lp->r_tree == NULL)
      {  LPXROW *row;
         int i;
         lp->r_tree = avl_create_tree(NULL, compare_names);
         for (i = 1; i <= lp->m; i++)
         {  row = lp->row[i];
            insist(row->node == NULL);
            if (row->name != NULL)
            {  row->node = avl_insert_by_key(lp->r_tree, row->name);
               row->node->link = row;
            }
         }
      }
      if (lp->c_tree == NULL)
      {  LPXCOL *col;
         int j;
         lp->c_tree = avl_create_tree(NULL, compare_names);
         for (j = 1; j <= lp->n; j++)
         {  col = lp->col[j];
            insist(col->node == NULL);
            if (col->name != NULL)
            {  col->node = avl_insert_by_key(lp->c_tree, col->name);
               col->node->link = col;
            }
         }
      }
      return;
}

int lpx_find_row(LPX *lp, char *name)
{     int i;
      if (lp->r_tree == NULL)
         fault("lpx_find_row: row index does not exist");
      if (name == NULL || name[0] == '\0' || strlen(name) > 255)
         i = 0;
      else
      {  AVLNODE *node;
         STR *key;
         key = create_str(lp->str_pool);
         set_str(key, name);
         node = avl_find_by_key(lp->r_tree, key);
         delete_str(key);
         i = (node == NULL ? 0 : ((LPXROW *)node->link)->i);
      }
      return i;
}

int lpx_find_col(LPX *lp, char *name)
{     int j;
      if (lp->c_tree == NULL)
         fault("lpx_find_col: column index does not exist");
      if (name == NULL || name[0] == '\0' || strlen(name) > 255)
         j = 0;
      else
      {  AVLNODE *node;
         STR *key;
         key = create_str(lp->str_pool);
         set_str(key, name);
         node = avl_find_by_key(lp->c_tree, key);
         delete_str(key);
         j = (node == NULL ? 0 : ((LPXCOL *)node->link)->j);
      }
      return j;
}

void lpx_delete_index(LPX *lp)
{     if (lp->r_tree != NULL)
      {  int i;
         for (i = 1; i <= lp->m; i++) lp->row[i]->node = NULL;
         avl_delete_tree(lp->r_tree), lp->r_tree = NULL;
      }
      if (lp->c_tree != NULL)
      {  int j;
         for (j = 1; j <= lp->n; j++) lp->col[j]->node = NULL;
         avl_delete_tree(lp->c_tree), lp->c_tree = NULL;
      }
      return;
}
#endif

/*----------------------------------------------------------------------
-- lpx_put_lp_basis - store LP basis information.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_put_lp_basis(LPX *lp, int b_stat, int basis[], INV *b_inv);
--
-- *Description*
--
-- The routine lpx_put_lp_basis stores an LP basis information into the
-- specified problem object.
--
-- NOTE: This routine is intended for internal use only. */

void lpx_put_lp_basis(LPX *lp, int b_stat, int basis[], INV *b_inv)
{     LPXROW *row;
      LPXCOL *col;
      int i, k;
      /* store basis status */
      if (!(b_stat == LPX_B_UNDEF || b_stat == LPX_B_VALID))
         fault("lpx_put_lp_basis: b_stat = %d; invalid basis status",
            b_stat);
      lp->b_stat = b_stat;
      /* store basis header */
      if (basis != NULL)
         for (i = 1; i <= lp->m; i++) lp->basis[i] = basis[i];
      /* store factorization of the basis matrix */
      if (b_inv != NULL) lp->b_inv = b_inv;
      /* if the basis is claimed to be valid, check it */
      if (lp->b_stat == LPX_B_VALID)
      {  for (k = 1; k <= lp->m; k++) lp->row[k]->b_ind = 0;
         for (k = 1; k <= lp->n; k++) lp->col[k]->b_ind = 0;
         for (i = 1; i <= lp->m; i++)
         {  k = lp->basis[i];
            if (!(1 <= k && k <= lp->m+lp->n))
               fault("lpx_put_lp_basis: basis[%d] = %d; invalid referen"
                  "ce to basic variable", i, k);
            if (k <= lp->m)
            {  row = lp->row[k];
               if (row->stat != LPX_BS)
                  fault("lpx_put_lp_basis: basis[%d] = %d; invalid refe"
                     "rence to non-basic row", i, k);
               if (row->b_ind != 0)
                  fault("lpx_put_lp_basis: basis[%d] = %d; duplicate re"
                     "ference to basic row", i, k);
               row->b_ind = i;
            }
            else
            {  col = lp->col[k-lp->m];
               if (col->stat != LPX_BS)
                  fault("lpx_put_lp_basis: basis[%d] = %d; invalid refe"
                     "rence to non-basic column", i, k);
               if (col->b_ind != 0)
                  fault("lpx_put_lp_basis: basis[%d] = %d; duplicate re"
                     "ference to basic column", i, k);
               col->b_ind = i;
            }
         }
         if (lp->b_inv == NULL)
            fault("lpx_put_lp_basis: factorization of basis matrix not "
               "provided");
         if (lp->b_inv->m != lp->m)
            fault("lpx_put_lp_basis: factorization of basis matrix has "
               "wrong dimension");
         if (!lp->b_inv->valid)
            fault("lpx_put_lp_basis: factorization of basis matrix is n"
               "ot valid");
      }
      return;
}

/*----------------------------------------------------------------------
-- lpx_put_solution - store basic solution components.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_put_solution(LPX *lp, int p_stat, int d_stat,
--    int row_stat[], gnm_float row_prim[], gnm_float row_dual[],
--    int col_stat[], gnm_float col_prim[], gnm_float col_dual[]);
--
-- *Description*
--
-- The routine lpx_put_solution stores basic solution components into
-- the specified problem object.
--
-- NOTE: This routine is intended for internal use only. */

void lpx_put_solution(LPX *lp, int p_stat, int d_stat,
      int row_stat[], gnm_float row_prim[], gnm_float row_dual[],
      int col_stat[], gnm_float col_prim[], gnm_float col_dual[])
{     LPXROW *row;
      LPXCOL *col;
      int i, j;
      /* store primal status */
      if (!(p_stat == LPX_P_UNDEF  || p_stat == LPX_P_FEAS ||
            p_stat == LPX_P_INFEAS || p_stat == LPX_P_NOFEAS))
         fault("lpx_put_solution: p_stat = %d; invalid primal status",
            p_stat);
      lp->p_stat = p_stat;
      /* store dual status */
      if (!(d_stat == LPX_D_UNDEF  || d_stat == LPX_D_FEAS ||
            d_stat == LPX_D_INFEAS || d_stat == LPX_D_NOFEAS))
         fault("lpx_put_solution: d_stat = %d; invalid dual status",
            d_stat);
      lp->d_stat = d_stat;
      /* store row solution components */
      for (i = 1; i <= lp->m; i++)
      {  row = lp->row[i];
         if (row_stat != NULL)
         {  row->stat = row_stat[i];
            if (!(row->stat == LPX_BS ||
                  row->type == LPX_FR && row->stat == LPX_NF ||
                  row->type == LPX_LO && row->stat == LPX_NL ||
                  row->type == LPX_UP && row->stat == LPX_NU ||
                  row->type == LPX_DB && row->stat == LPX_NL ||
                  row->type == LPX_DB && row->stat == LPX_NU ||
                  row->type == LPX_FX && row->stat == LPX_NS))
               fault("lpx_put_solution: row_stat[%d] = %d; invalid row "
                  "status", i, row->stat);
         }
         if (row_prim != NULL) row->prim = row_prim[i];
         if (row_dual != NULL) row->dual = row_dual[i];
      }
      /* store column solution components */
      for (j = 1; j <= lp->n; j++)
      {  col = lp->col[j];
         if (col_stat != NULL)
         {  col->stat = col_stat[j];
            if (!(col->stat == LPX_BS ||
                  col->type == LPX_FR && col->stat == LPX_NF ||
                  col->type == LPX_LO && col->stat == LPX_NL ||
                  col->type == LPX_UP && col->stat == LPX_NU ||
                  col->type == LPX_DB && col->stat == LPX_NL ||
                  col->type == LPX_DB && col->stat == LPX_NU ||
                  col->type == LPX_FX && col->stat == LPX_NS))
               fault("lpx_put_solution: row_stat[%d] = %d; invalid colu"
                  "mn status", j, col->stat);
         }
         if (col_prim != NULL) col->prim = col_prim[j];
         if (col_dual != NULL) col->dual = col_dual[j];
      }
      /* invalidate the basis (only if statuses of rows and/or columns
         have been changed) */
      if (!(row_stat == NULL && col_stat == NULL))
         lp->b_stat = LPX_B_UNDEF;
      return;
}

/*----------------------------------------------------------------------
-- lpx_put_ray_info - store row/column which causes unboundness.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_put_ray_info(LPX *lp, int k);
--
-- *Description*
--
-- The routine lpx_put_ray_info stores the number of row/column, which
-- causes primal unboundness.
--
-- NOTE: This routine is intended for internal use only. */

void lpx_put_ray_info(LPX *lp, int k)
{     if (!(0 <= k && k <= lp->m+lp->n))
         fault("lpx_put_ray_info: ray = %d; row/column number out of ra"
            "nge", k);
      lp->some = k;
      return;
}

/*----------------------------------------------------------------------
-- lpx_put_ipt_soln - store interior-point solution components.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_put_ipt_soln(LPX *lp, int t_stat, gnm_float row_pval[],
--    gnm_float row_dval[], gnm_float col_pval[], gnm_float col_dval[]);
--
-- *Description*
--
-- The routine lpx_put_ipt_soln stores solution components obtained by
-- interior-point solver into the specified problem object.
--
-- NOTE: This routine is intended for internal use only. */

void lpx_put_ipt_soln(LPX *lp, int t_stat, gnm_float row_pval[],
      gnm_float row_dval[], gnm_float col_pval[], gnm_float col_dval[])
{     LPXROW *row;
      LPXCOL *col;
      int i, j;
      /* store interior-point status */
      if (!(t_stat == LPX_T_UNDEF || t_stat == LPX_T_OPT))
         fault("lpx_put_ipm_soln: t_stat = %d; invalid interior-point s"
            "tatus", t_stat);
      lp->t_stat = t_stat;
      /* store row solution components */
      for (i = 1; i <= lp->m; i++)
      {  row = lp->row[i];
         if (row_pval != NULL) row->pval = row_pval[i];
         if (row_dval != NULL) row->dval = row_dval[i];
      }
      /* store column solution components */
      for (j = 1; j <= lp->n; j++)
      {  col = lp->col[j];
         if (col_pval != NULL) col->pval = col_pval[j];
         if (col_dval != NULL) col->dval = col_dval[j];
      }
      return;
}

/*----------------------------------------------------------------------
-- lpx_put_mip_soln - store mixed integer solution components.
--
-- *Synopsis*
--
-- #include "glplpx.h"
-- void lpx_put_mip_soln(LPX *lp, int i_stat, gnm_float row_mipx[],
--    gnm_float col_mipx[]);
--
-- *Description*
--
-- The routine lpx_put_mip_soln stores solution components obtained by
-- branch-and-bound solver into the specified problem object.
--
-- NOTE: This routine is intended for internal use only. */

void lpx_put_mip_soln(LPX *lp, int i_stat, gnm_float row_mipx[],
      gnm_float col_mipx[])
{     LPXROW *row;
      LPXCOL *col;
      int i, j;
      /* store mixed integer status */
      if (!(i_stat == LPX_I_UNDEF || i_stat == LPX_I_OPT ||
            i_stat == LPX_I_FEAS  || i_stat == LPX_I_NOFEAS))
         fault("lpx_put_mip_soln: i_stat = %d; invalid mixed integer st"
            "atus", i_stat);
      lp->i_stat = i_stat;
      /* store row solution components */
      if (row_mipx != NULL)
      {  for (i = 1; i <= lp->m; i++)
         {  row = lp->row[i];
            row->mipx = row_mipx[i];
         }
      }
      /* store column solution components */
      if (col_mipx != NULL)
      {  for (j = 1; j <= lp->n; j++)
         {  col = lp->col[j];
            col->mipx = col_mipx[j];
         }
      }
      /* if the solution is claimed to be integer feasible, check it */
      if (lp->i_stat == LPX_I_OPT || lp->i_stat == LPX_I_FEAS)
      {  for (j = 1; j <= lp->n; j++)
         {  col = lp->col[j];
            if (col->kind == LPX_IV && col->mipx != gnm_floor(col->mipx))
               fault("lpx_put_mip_soln: col_mipx[%d] = %.*g; must be in"
                  "tegral", j, DBL_DIG, col->mipx);
         }
      }
      return;
}

/* eof */
