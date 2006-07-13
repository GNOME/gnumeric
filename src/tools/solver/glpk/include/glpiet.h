/* glpiet.h (implicit enumeration tree) */

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

#ifndef _GLPIET_H
#define _GLPIET_H

#include "gnumeric-config.h"
#include "gnumeric.h"
#include "numbers.h"

#include "glpstr.h"

#define iet_create_tree       glp_iet_create_tree
#define iet_install_hook      glp_iet_install_hook
#define iet_revive_node       glp_iet_revive_node
#define iet_freeze_node       glp_iet_freeze_node
#define iet_clone_node        glp_iet_clone_node
#define iet_set_node_link     glp_iet_set_node_link
#define iet_delete_node       glp_iet_delete_node
#define iet_delete_tree       glp_iet_delete_tree

#define iet_get_tree_size     glp_iet_get_tree_size
#define iet_get_curr_node     glp_iet_get_curr_node
#define iet_get_next_node     glp_iet_get_next_node
#define iet_get_prev_node     glp_iet_get_prev_node
#define iet_get_up_node       glp_iet_get_up_node
#define iet_get_node_lev      glp_iet_get_node_lev
#define iet_get_node_cnt      glp_iet_get_node_cnt
#define iet_get_node_link     glp_iet_get_node_link
#define iet_pseudo_root       glp_iet_pseudo_root

#define iet_add_rows          glp_iet_add_rows
#define iet_add_cols          glp_iet_add_cols
#define iet_check_name        glp_iet_check_name
#define iet_set_row_name      glp_iet_set_row_name
#define iet_set_col_name      glp_iet_set_col_name
#define iet_set_row_link      glp_iet_set_row_link
#define iet_set_col_link      glp_iet_set_col_link
#define iet_set_row_bnds      glp_iet_set_row_bnds
#define iet_set_col_bnds      glp_iet_set_col_bnds
#define iet_set_obj_coef      glp_iet_set_obj_coef
#define iet_set_mat_row       glp_iet_set_mat_row
#define iet_set_mat_col       glp_iet_set_mat_col
#define iet_set_row_stat      glp_iet_set_row_stat
#define iet_set_col_stat      glp_iet_set_col_stat
#define iet_set_row_locl      glp_iet_set_row_locl
#define iet_set_col_locl      glp_iet_set_col_locl
#define iet_del_rows          glp_iet_del_rows
#define iet_del_cols          glp_iet_del_cols

#define iet_get_num_rows      glp_iet_get_num_rows
#define iet_get_num_cols      glp_iet_get_num_cols
#define iet_get_num_nz        glp_iet_get_num_nz
#define iet_get_row_name      glp_iet_get_row_name
#define iet_get_col_name      glp_iet_get_col_name
#define iet_get_row_link      glp_iet_get_row_link
#define iet_get_col_link      glp_iet_get_col_link
#define iet_get_row_bnds      glp_iet_get_row_bnds
#define iet_get_col_bnds      glp_iet_get_col_bnds
#define iet_get_obj_coef      glp_iet_get_obj_coef
#define iet_get_mat_row       glp_iet_get_mat_row
#define iet_get_mat_col       glp_iet_get_mat_col
#define iet_get_row_stat      glp_iet_get_row_stat
#define iet_get_col_stat      glp_iet_get_col_stat
#define iet_get_row_locl      glp_iet_get_row_locl
#define iet_get_col_locl      glp_iet_get_col_locl

typedef struct IET IET;       /* implicit enumeration tree */
typedef struct IETNPS IETNPS; /* node (sub)problem slot */
typedef struct IETNPD IETNPD; /* node (sub)problem descriptor */
typedef struct IETRGD IETRGD; /* row global descriptor */
typedef struct IETCGD IETCGD; /* column global descriptor */
typedef struct IETDQE IETDQE; /* row/column deletion entry */
typedef struct IETBQE IETBQE; /* type/bounds change entry */
typedef struct IETCQE IETCQE; /* obj. coefficient change entry */
typedef struct IETAQE IETAQE; /* constraint matrix change entry */
typedef struct IETAIJ IETAIJ; /* constraint coefficient */
typedef struct IETSQE IETSQE; /* status change entry */
typedef struct IETROW IETROW; /* row local descriptor */
typedef struct IETCOL IETCOL; /* column local descriptor */

struct IET
{     /* implicit enumeration tree */
      /*--------------------------------------------------------------*/
      /* memory management */
      DMP *npd_pool;
      /* memory pool for IETNPD objects */
      DMP *rgd_pool;
      /* memory pool for IETRGD objects */
      DMP *cgd_pool;
      /* memory pool for IETCGD objects */
      DMP *dqe_pool;
      /* memory pool for IETDQE objects */
      DMP *bqe_pool;
      /* memory pool for IETBQE objects */
      DMP *cqe_pool;
      /* memory pool for IETCQE objects */
      DMP *aqe_pool;
      /* memory pool for IETAQE objects */
      DMP *aij_pool;
      /* memory pool for IETAIJ objects */
      DMP *sqe_pool;
      /* memory pool for IETSQE objects */
      DMP *row_pool;
      /* memory pool for IETROW objects */
      DMP *col_pool;
      /* memory pool for IETCOL objects */
      DMP *str_pool;
      /* memory pool for segmented character strings */
      char *str_buf; /* char str_buf[255+1]; */
      /* working buffer to store character strings */
      /*--------------------------------------------------------------*/
      /* implicit enumeration tree */
      int nslots;
      /* length of the array of slots (increased automatically) */
      int avail;
      /* index of first free slot; 0 means all slots are in use */
      IETNPS *slot; /* IETNPS slot[1+nslots]; */
      /* array of slots:
         slot[0] is never used;
         slot[p], 1 <= p <= nslots, either contains a pointer to some
         node of the branch-and-bound tree, in which case p is used on
         api level as the reference number of corresponding subproblem,
         or is free; all free slots are linked into single linked list;
         slot[1] always contains a pointer to the root node (it is free
         only if the tree is empty) */
      IETNPD *head;
      /* pointer to the head of the active list */
      IETNPD *tail;
      /* pointer to the tail of the active list */
      /* the active list is a doubly linked list of active subproblems
         which correspond to leaves of the tree; all subproblems in the
         active list are ordered chronologically (each new subproblem
         is always added to the tail of the list) */
      int a_cnt;
      /* current number of active nodes (including the current one) */
      int n_cnt;
      /* current number of all (active and inactive) nodes */
      int t_cnt;
      /* total number of nodes including those which have been already
         removed from the tree; this count is increased whenever a new
         node is created and never decreased */
      /*--------------------------------------------------------------*/
      /* higher-level hook routine */
      void (*hook)(void *info, int what, char *name, void *link);
      /* entry point to the higher-level hook routine; this routine is
         called whenever a subproblem, row, or column is being deleted;
         its purpose is to delete an additional information associated
         with corresponding object; the second parameter specifies what
         object is being deleted: */
#define IET_ND          401   /* subproblem is being deleted */
#define IET_RD          402   /* row is being deleted */
#define IET_CD          403   /* column is being deleted */
      void *info;
      /* transitional pointer passed to the hook routine */
      /*--------------------------------------------------------------*/
      /* current subproblem */
      IETNPD *curr;
      /* pointer to the current subproblem (which can be only active);
         NULL means the current subproblem does not exist */
      int m_max;
      /* length of the array of rows (increased automatically) */
      int n_max;
      /* length of the array of columns (increased automatically) */
      int m;
      /* number of rows, 0 <= m <= m_max */
      int n;
      /* number of columns, 0 <= n <= n_max */
      int nz;
      /* number of (non-zero) elements in the constraint matrix */
      gnm_float c0;
      /* constant term of the objective function (so-called shift) */
      gnm_float old_c0;
      /* constant term which is either inherited from parent subproblem
         or set by default (to zero) if this is the root subproblem */
      IETROW **row; /* IETROW *row[1+m_max]; */
      /* array of rows:
         row[0] is never used;
         row[i], 1 <= i <= m, is a pointer to i-th row;
         row[m+1], ..., row[m_max] are free locations */
      IETCOL **col; /* IETCOL *col[1+n_max]; */
      /* array of columns:
         col[0] is never used;
         col[j], 1 <= j <= n, is a pointer to j-th column;
         col[n+1], ..., col[n_max] are free locations */
};

struct IETNPS
{     /* node (sub)problem slot */
      IETNPD *node;
      /* pointer to subproblem descriptor; NULL means free slot */
      int next;
      /* index of another free slot (only if this slot is free) */
};

struct IETNPD
{     /* node (sub)problem descriptor */
      int p;
      /* subproblem reference number (it is the index of corresponding
         slot, i.e. slot[p] points to this descriptor) */
      IETNPD *up;
      /* pointer to parent subproblem; NULL means this node is the root
         of the tree, in which case p = 1 */
      int level;
      /* node level (the root node has level 0) */
      int count;
      /* if count = 0, this subproblem is active; if count > 0, this
         subproblem is inactive, in which case the count is the number
         of its child subproblems */
      IETRGD *r_add;
      /* linked list of own rows of this subproblem which were added by
         iet_add_rows; rows in this list follow in the same order as in
         which they were added */
      IETCGD *c_add;
      /* linked list of own columns of this subproblem which were added
         by iet_add_cols; columns in this list follow in the same order
         as in which they were added */
      IETDQE *r_del;
      /* linked list of inherited rows which were deleted from this
         subproblem by iet_del_rows */
      IETDQE *c_del;
      /* linked list of inherited columns which were deleted from this
         subproblem by iet_del_cols */
      IETBQE *r_bnds;
      /* linked list of rows whose type and bounds were changed by
         iet_set_row_bnds; this list is destroyed on reviving and built
         anew on freezing the subproblem */
      IETBQE *c_bnds;
      /* linked list of columns whose type and bounds were changed by
         iet_set_col_bnds; this list is destroyed on reviving and built
         anew on freezing the subproblem */
      IETCQE *c_obj;
      /* linked list of columns whose objective coefficients were
         changed by iet_set_obj_coef; this list is destroyed on reviving
         and built anew on freezing the subproblem */
      IETAQE *r_mat;
      /* linked list of rows whose constraint coefficients were changed
         by iet_set_mat_row; this list is destroyed on reviving and
         built anew on freezing the subproblem */
      IETAQE *c_mat;
      /* linked list of columns whose constraint coefficients were
         changed by iet_set_mat_col; this list is destroyed on reviving
         and built anew on freezing the subproblem */
      IETSQE *r_stat;
      /* linked list of rows whose statuses in basic solution were
         changed by iet_set_row_stat; this list is destroyed on reviving
         and built anew on freezing the subproblem */
      IETSQE *c_stat;
      /* linked list of columns whose statuses in basic solution were
         changed by iet_set_col_stat; this list is destroyed on reviving
         and built anew on freezing the subproblem */
      void *link;
      /* reserved for higher-level extension */
      IETNPD *temp;
      /* auxiliary pointer used by some routines */
      IETNPD *prev;
      /* pointer to previous subproblem in the active list */
      IETNPD *next;
      /* pointer to next subproblem in the active list */
};

struct IETRGD
{     /* row global descriptor */
      IETNPD *host;
      /* pointer to the subproblem where this row was introduced */
      STR *name;
      /* row name (1 to 255 chars); NULL means no name is assigned to
         this row */
      int i;
      /* ordinal number (1 to m) assigned to this row in the current
         subproblem; zero means that either this row is not included in
         the current subproblem or the latter does not exist */
      void *link;
      /* reserved for higher-level extension */
      IETRGD *temp;
      /* auxiliary pointer used by some routines */
      IETRGD *next;
      /* pointer to next row for the same host subproblem */
};

struct IETCGD
{     /* column global descriptor */
      IETNPD *host;
      /* pointer to subproblem where this column was introduced */
      STR *name;
      /* column name (1 to 255 chars); NULL means no name is assigned
         to this column */
      int j;
      /* ordinal number (1 to n) assigned to this column in the current
         subproblem; zero means that either this column is not included
         in the current subproblem or the latter does not exist */
      void *link;
      /* reserved for higher-level extension */
      IETCGD *temp;
      /* auxiliary pointer used by some routines */
      IETCGD *next;
      /* pointer to next row for the same host subproblem */
};

struct IETDQE
{     /* row/column deletion entry */
      union { IETRGD *row; IETCGD *col; } u;
      /* pointer to corresponding row/column */
      IETDQE *next;
      /* pointer to next entry for the same subproblem */
};

struct IETBQE
{     /* type/bounds change entry */
      union { IETRGD *row; IETCGD *col; } u;
      /* pointer to corresponding row/column */
      int type;
      /* new type */
      gnm_float lb;
      /* new lower bound */
      gnm_float ub;
      /* new upper bound */
      IETBQE *next;
      /* pointer to next entry for the same subproblem */
};

struct IETCQE
{     /* objective coefficient change entry */
      IETCGD *col;
      /* pointer to corresponding column; NULL means constant term */
      gnm_float coef;
      /* new objective coefficient or constant term */
      IETCQE *next;
      /* pointer to next entry for the same subproblem */
};

struct IETAQE
{     /* constraint matrix change entry */
      union { IETRGD *row; IETCGD *col; } u;
      /* pointer to corresponding row/column */
      IETAIJ *ptr;
      /* pointer to new list of constraint coefficients */
      IETAQE *next;
      /* pointer to next entry for the same subproblem */
};

struct IETAIJ
{     /* constraint coefficient */
      IETRGD *row;
      /* pointer to row where this coefficient is placed */
      IETCGD *col;
      /* pointer to column where this coefficient is placed */
      gnm_float val;
      /* numeric (non-zero) value of this coefficient */
      IETAIJ *link;
      /* pointer to next coefficient for the same change entry */
      /*--------------------------------------------------------------*/
      /* the following four pointers are used only if this coefficient
         is included in the current subproblem */
      IETAIJ *r_prev;
      /* pointer to previous coefficient in the same row */
      IETAIJ *r_next;
      /* pointer to next coefficient in the same row */
      IETAIJ *c_prev;
      /* pointer to previous coefficient in the same column */
      IETAIJ *c_next;
      /* pointer to next coefficient in the same column */
};

struct IETSQE
{     /* status change entry */
      union { IETRGD *row; IETCGD *col; } u;
      /* pointer to corresponding row/column */
      int stat;
      /* new status */
      IETSQE *next;
      /* pointer to next entry for the same subproblem */
};

struct IETROW
{     /* row local descriptor */
      IETRGD *glob;
      /* pointer to corresponding global descriptor */
      int type;
      /* type of auxiliary variable associated with this row: */
#define IET_FR          411   /* free variable */
#define IET_LO          412   /* variable with lower bound */
#define IET_UP          413   /* variable with upper bound */
#define IET_DB          414   /* gnm_float-bounded variable */
#define IET_FX          415   /* fixed variable */
      gnm_float lb;
      /* lower bound; if the row has no lower bound, lb is zero */
      gnm_float ub;
      /* upper bound; if the row has no upper bound, ub is zero */
      /* if the row type is IET_FX, ub is equal to lb */
      IETNPD *set_by;
      /* pointer to subproblem of highest level (between the root and
         the current subproblem) where either this row was introduced
         by iet_add_rows or its constraint coefficients were changed by
         iet_set_mat_row */
      IETAIJ *ptr;
      /* pointer to doubly linked list of constraint coefficients which
         are placed in this row */
      int stat;
      /* status of auxiliary variable associated with this row: */
#define IET_BS          421   /* basic variable */
#define IET_NL          422   /* non-basic variable on lower bound */
#define IET_NU          423   /* non-basic variable on upper bound */
#define IET_NF          424   /* non-basic free variable */
#define IET_NS          425   /* non-basic fixed variable */
      int old_type;
      gnm_float old_lb;
      gnm_float old_ub;
      int old_stat;
      /* type, lower bound, upper bound, and status of this row either
         inherited from parent subproblem or set by default on creating
         this row */
      void *link;
      /* reserved for higher-level extension */
};

struct IETCOL
{     /* column local descriptor */
      IETCGD *glob;
      /* pointer to corresponding global descriptor */
      int type;
      /* type of structural variable associated with this column: */
#define IET_FR          411   /* free variable */
#define IET_LO          412   /* variable with lower bound */
#define IET_UP          413   /* variable with upper bound */
#define IET_DB          414   /* gnm_float-bounded variable */
#define IET_FX          415   /* fixed variable */
      gnm_float lb;
      /* lower bound; if the column has no lower bound, lb is zero */
      gnm_float ub;
      /* upper bound; if the column has no upper bound, ub is zero */
      /* if the column type is IET_FX, ub is equal to lb */
      gnm_float coef;
      /* objective coefficient at the structural variable */
      IETNPD *set_by;
      /* pointer to subproblem of highest level (between the root and
         the current suboroblem) where either this column was introduced
         by iet_add_cols or its constraint coefficients were changed by
         iet_set_mat_col */
      IETAIJ *ptr;
      /* pointer to doubly linked list of constraint coefficients which
         are placed in this column */
      int stat;
      /* status of structural variable associated with this column: */
#define IET_BS          421   /* basic variable */
#define IET_NL          422   /* non-basic variable on lower bound */
#define IET_NU          423   /* non-basic variable on upper bound */
#define IET_NF          424   /* non-basic free variable */
#define IET_NS          425   /* non-basic fixed variable */
      int old_type;
      gnm_float old_lb;
      gnm_float old_ub;
      gnm_float old_coef;
      int old_stat;
      /* type, lower bound, upper bound, objective coefficient, and
         status of this column either inherited from parent subproblem
         or set by default on creating this column */
      void *link;
      /* reserved for higher-level extension */
};

/**********************************************************************/
/* * *                  TREE MANAGEMENT ROUTINES                  * * */
/**********************************************************************/

IET *iet_create_tree(void);
/* create implicit enumeration tree */

void iet_install_hook(IET *iet, void (*hook)(void *info, int what,
      char *name, void *link), void *info);
/* install higher-level hook routine */

void iet_revive_node(IET *iet, int p);
/* revive specified subproblem */

void iet_freeze_node(IET *iet);
/* freeze current subproblem */

void iet_clone_node(IET *iet, int p, int nnn);
/* clone specified subproblem */

void iet_set_node_link(IET *iet, int p, void *link);
/* set link to subproblem extension */

void iet_delete_node(IET *iet, int p);
/* delete specified subproblem */

void iet_delete_tree(IET *iet);
/* delete implicit enumeration tree */

/**********************************************************************/
/* * *                  TREE EXPLORING ROUTINES                   * * */
/**********************************************************************/

void iet_get_tree_size(IET *iet, int *a_cnt, int *n_cnt, int *t_cnt);
/* determine current size of the tree */

int iet_get_curr_node(IET *iet);
/* determine current active subproblem */

int iet_get_next_node(IET *iet, int p);
/* determine next active subproblem */

int iet_get_prev_node(IET *iet, int p);
/* determine previous active subproblem */

int iet_get_up_node(IET *iet, int p);
/* determine parent subproblem */

int iet_get_node_lev(IET *iet, int p);
/* determine subproblem level */

int iet_get_node_cnt(IET *iet, int p);
/* determine number of child subproblems */

void *iet_get_node_link(IET *iet, int p);
/* obtain link to subproblem extension */

int iet_pseudo_root(IET *iet);
/* find pseudo-root of the tree */

/**********************************************************************/
/* * *               SUBPROBLEM MODIFYING ROUTINES                * * */
/**********************************************************************/

void iet_add_rows(IET *iet, int nrs);
/* add new rows to current subproblem */

void iet_add_cols(IET *iet, int ncs);
/* add new columns to current subproblem */

int iet_check_name(IET *iet, char *name);
/* check correctness of symbolic name */

void iet_set_row_name(IET *iet, int i, char *name);
/* assign symbolic name to row */

void iet_set_col_name(IET *iet, int j, char *name);
/* assign symbolic name to column */

void iet_set_row_link(IET *iet, int i, void *link);
/* set link to row global extension */

void iet_set_col_link(IET *iet, int j, void *link);
/* set link to column global extension */

void iet_set_row_bnds(IET *iet, int i, int type, gnm_float lb, gnm_float ub);
/* set row type and bouns */

void iet_set_col_bnds(IET *iet, int j, int type, gnm_float lb, gnm_float ub);
/* set column type and bounds */

void iet_set_obj_coef(IET *iet, int j, gnm_float coef);
/* set objective coefficient or constant term */

void iet_set_mat_row(IET *iet, int i, int len, int ind[], gnm_float val[]);
/* replace row of constraint matrix */

void iet_set_mat_col(IET *iet, int j, int len, int ind[], gnm_float val[]);
/* replace column of constraint matrix */

void iet_set_row_stat(IET *iet, int i, int stat);
/* set row status */

void iet_set_col_stat(IET *iet, int j, int stat);
/* set column status */

void iet_set_row_locl(IET *iet, int i, void *link);
/* set link to row local extension */

void iet_set_col_locl(IET *iet, int j, void *link);
/* set link to column local extension */

void iet_del_rows(IET *iet, int nrs, int num[]);
/* delete specified rows from current subproblem */

void iet_del_cols(IET *iet, int ncs, int num[]);
/* delete specified columns from current subproblem */

/**********************************************************************/
/* * *                SUBPROBLEM QUERYING ROUTINES                * * */
/**********************************************************************/

int iet_get_num_rows(IET *iet);
/* determine number of rows */

int iet_get_num_cols(IET *iet);
/* determine number of columns */

int iet_get_num_nz(IET *iet);
/* determine number of constraint coefficients */

char *iet_get_row_name(IET *iet, int i);
/* obtain row name */

char *iet_get_col_name(IET *iet, int j);
/* obtain column name */

void *iet_get_row_link(IET *iet, int i);
/* obtain link to row global extension */

void *iet_get_col_link(IET *iet, int j);
/* obtain link to column global extension */

int iet_get_row_bnds(IET *iet, int i, gnm_float *lb, gnm_float *ub);
/* determine row type and bounds */

int iet_get_col_bnds(IET *iet, int j, gnm_float *lb, gnm_float *ub);
/* determine column type and bounds */

gnm_float iet_get_obj_coef(IET *iet, int j);
/* determine objective coefficient */

int iet_get_mat_row(IET *iet, int i, int ind[], gnm_float val[]);
/* obtain row of constraint matrix */

int iet_get_mat_col(IET *iet, int j, int ind[], gnm_float val[]);
/* obtain column of constraint matrix */

int iet_get_row_stat(IET *iet, int i);
/* obtain row status */

int iet_get_col_stat(IET *iet, int j);
/* obtain column status */

void *iet_get_row_locl(IET *iet, int i);
/* obtain link to row local extension */

void *iet_get_col_locl(IET *iet, int j);
/* obtain link to column local extension */

#endif

/* eof */
