/* glpmip1.c */

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
#include "glpmip.h"

/*----------------------------------------------------------------------
-- mip_create_tree - create branch-and-bound tree.
--
-- *Synopsis*
--
-- #include "glpmip.h"
-- MIPTREE *mip_create_tree(int m, int n, int dir);
--
-- *Description*
--
-- The routine mip_create_tree creates the branch-and-bound tree.
--
-- The parameters m, n, and dir specify, respectively, the number of
-- rows, the number of columns, and optimization direction (LPX_MIN or
-- LPX_MAX). This information is global and valid for all subproblems
-- to be created during the search.
--
-- Being created the tree consists of the only root subproblem whose
-- reference number is 1. Note that initially the root subproblem is in
-- frozen state and therefore needs to be revived.
--
-- *Returns*
--
-- The routine returns a pointer to the tree created. */

MIPTREE *mip_create_tree(int m, int n, int dir)
{     MIPTREE *tree;
      MIPNODE *node;
      int j, p;
      if (m < 1)
         fault("mip_create_tree: m = %d; invalid number of rows", m);
      if (n < 1)
         fault("mip_create_tree: n = %d; invalid number of columns", n);
      if (!(dir == LPX_MIN || dir == LPX_MAX))
         fault("mip_create_tree: dir = %d; invalid direction", dir);
      tree = umalloc(sizeof(MIPTREE));
      tree->m = m;
      tree->n = n;
      tree->dir = dir;
      tree->int_obj = 0;
      tree->int_col = ucalloc(1+n, sizeof(int));
      tree->node_pool = dmp_create_pool(sizeof(MIPNODE));
      tree->bnds_pool = dmp_create_pool(sizeof(MIPBNDS));
      tree->stat_pool = dmp_create_pool(sizeof(MIPSTAT));
      tree->nslots = 20;
      tree->avail = 0;
      tree->slot = ucalloc(1+tree->nslots, sizeof(MIPSLOT));
      tree->head = NULL;
      tree->tail = NULL;
      tree->a_cnt = 0;
      tree->n_cnt = 0;
      tree->t_cnt = 0;
      tree->found = 0;
      tree->best = 0.0;
      tree->mipx = ucalloc(1+m+n, sizeof(gnm_float));
      tree->curr = NULL;
      tree->lp = lpx_create_prob();
      tree->old_type = ucalloc(1+m+n, sizeof(int));
      tree->old_lb = ucalloc(1+m+n, sizeof(gnm_float));
      tree->old_ub = ucalloc(1+m+n, sizeof(gnm_float));
      tree->old_stat = ucalloc(1+m+n, sizeof(int));
      tree->non_int = ucalloc(1+n, sizeof(int));
      tree->msg_lev = 2;
      tree->branch = 2;
      tree->btrack = 2;
      tree->tol_int = 1e-5;
      tree->tol_obj = 1e-7;
      tree->tm_lim = -1.0;
      tree->out_frq = 5.0;
      tree->out_dly = 10.0;
      tree->tm_beg = utime();
      tree->tm_lag = 0.0;
      /* initialize integer column flags */
      for (j = 1; j <= n; j++) tree->int_col[j] = 0;
      /* initialize the stack of free slots */
      for (p = tree->nslots; p >= 1; p--)
      {  tree->slot[p].node = NULL;
         tree->slot[p].next = tree->avail;
         tree->avail = p;
      }
      /* pull a free slot for the root node */
      p = tree->avail;
      insist(p == 1);
      tree->avail = tree->slot[p].next;
      insist(tree->slot[p].node == NULL);
      tree->slot[p].next = 0;
      /* create and initialize the root subproblem */
      tree->slot[p].node = node = dmp_get_atom(tree->node_pool);
      node->p = p;
      node->up = NULL;
      node->level = 0;
      node->count = 0;
      node->bnds = NULL;
      node->stat = NULL;
      node->bound = (dir == LPX_MIN ? -DBL_MAX : +DBL_MAX);
      node->ii_cnt = 0;
      node->ii_sum = 0.0;
      node->temp = NULL;
      node->prev = NULL;
      node->next = NULL;
      /* add the root subproblem to the active list */
      tree->head = tree->tail = node;
      tree->a_cnt++;
      tree->n_cnt++;
      tree->t_cnt++;
      /* initialize LP relaxation template */
      lpx_add_rows(tree->lp, m);
      lpx_add_cols(tree->lp, n);
      lpx_set_obj_dir(tree->lp, dir);
      return tree;
}

/*----------------------------------------------------------------------
-- mip_revive_node - revive specified subproblem.
--
-- *Synopsis*
--
-- #include "glpmip.h"
-- void mip_revive_node(MIPTREE *tree, int p);
--
-- *Description*
--
-- The routine mip_revive_node revives the specified subproblem, whose
-- reference number is p, and thereby makes it the current subproblem.
-- Note that the specified subproblem must be active. Besides, if the
-- current subproblem already exists, it must be frozen before reviving
-- another subproblem. */

void mip_revive_node(MIPTREE *tree, int p)
{     int m = tree->m;
      int n = tree->n;
      LPX *lp = tree->lp;
      MIPNODE *node, *root;
      MIPBNDS *bnds;
      MIPSTAT *stat;
      int i, j;
      /* obtain pointer to the specified subproblem */
      if (!(1 <= p && p <= tree->nslots))
err:     fault("mip_revive_node: p = %d; invalid subproblem reference n"
            "umber", p);
      node = tree->slot[p].node;
      if (node == NULL) goto err;
      /* the specified subproblem must be active */
      if (node->count != 0)
         fault("mip_revive_node: p = %d; reviving inactive subproblem n"
            "ot allowed", p);
      /* the current subproblem must not exist */
      if (tree->curr != NULL)
         fault("mip_revive_node: current subproblem already exists");
      /* the specified subproblem becomes current */
      tree->curr = node;
      /* obtain pointer to the root subproblem */
      root = tree->slot[1].node;
      insist(root != NULL);
      /* build the path from the root to the given node */
      node->temp = NULL;
      for (node = node; node != NULL; node = node->up)
      {  if (node->up == NULL)
            insist(node == root);
         else
            node->up->temp = node;
      }
      /* assign initial "standard" attributes to rows and columns */
      for (i = 1; i <= m; i++)
      {  lpx_set_row_bnds(lp, i, LPX_FR, 0.0, 0.0);
         lpx_set_row_stat(lp, i, LPX_BS);
      }
      for (j = 1; j <= n; j++)
      {  lpx_set_col_bnds(lp, j, LPX_FX, 0.0, 0.0);
         lpx_set_col_stat(lp, j, LPX_NS);
      }
      /* walk from the root to the current node and restore attributes
         of rows and columns for the revived subproblem */
      for (node = root; node != NULL; node = node->temp)
      {  /* if the given node has been reached, save attributes of rows
            and columns which currently correspond to the parent of the
            revived subproblem */
         if (node->temp == NULL)
         {  for (i = 1; i <= m; i++)
            {  tree->old_type[i] = lpx_get_row_type(lp, i);
               tree->old_lb[i] = lpx_get_row_lb(lp, i);
               tree->old_ub[i] = lpx_get_row_ub(lp, i);
               tree->old_stat[i] = lpx_get_row_stat(lp, i);
            }
            for (j = 1; j <= n; j++)
            {  tree->old_type[m+j] = lpx_get_col_type(lp, j);
               tree->old_lb[m+j] = lpx_get_col_lb(lp, j);
               tree->old_ub[m+j] = lpx_get_col_ub(lp, j);
               tree->old_stat[m+j] = lpx_get_col_stat(lp, j);
            }
         }
         /* restore types and bounds of rows and columns */
         for (bnds = node->bnds; bnds != NULL; bnds = bnds->next)
         {  if (bnds->k <= m)
               lpx_set_row_bnds(lp, bnds->k, bnds->type, bnds->lb,
                  bnds->ub);
            else
               lpx_set_col_bnds(lp, bnds->k - m, bnds->type, bnds->lb,
                  bnds->ub);
         }
         /* restore statuses of rows and columns */
         for (stat = node->stat; stat != NULL; stat = stat->next)
         {  if (stat->k <= m)
               lpx_set_row_stat(lp, stat->k, stat->stat);
            else
               lpx_set_col_stat(lp, stat->k - m, stat->stat);
         }
      }
      /* the specified subproblem has been revived; delete its change
         lists */
      node = tree->curr;
      /* delete the bounds change list */
      while (node->bnds != NULL)
      {  bnds = node->bnds;
         node->bnds = bnds->next;
         dmp_free_atom(tree->bnds_pool, bnds);
      }
      /* delete the statuses change list */
      while (node->stat != NULL)
      {  stat = node->stat;
         node->stat = stat->next;
         dmp_free_atom(tree->stat_pool, stat);
      }
      return;
}

/*----------------------------------------------------------------------
-- mip_freeze_node - freeze current subproblem.
--
-- *Synopsis*
--
-- #include "glpmip.h"
-- void mip_freeze_node(MIPTREE *tree);
--
-- *Description*
--
-- The routine mip_freeze_node freezes the current subproblem. */

void mip_freeze_node(MIPTREE *tree)
{     int m = tree->m;
      int n = tree->n;
      LPX *lp = tree->lp;
      MIPNODE *node;
      MIPBNDS *bnds;
      MIPSTAT *ssss;
      int k, type, stat;
      gnm_float lb, ub;
      /* obtain pointer to the current subproblem */
      node = tree->curr;
      if (node == NULL)
         fault("mip_freeze_node: current subproblem does not exist");
      /* build change lists for rows and columns */
      insist(node->bnds == NULL);
      insist(node->stat == NULL);
      for (k = 1; k <= m+n; k++)
      {  if (k <= m)
         {  type = lpx_get_row_type(lp, k);
            lb = lpx_get_row_lb(lp, k);
            ub = lpx_get_row_ub(lp, k);
            stat = lpx_get_row_stat(lp, k);
         }
         else
         {  type = lpx_get_col_type(lp, k-m);
            lb = lpx_get_col_lb(lp, k-m);
            ub = lpx_get_col_ub(lp, k-m);
            stat = lpx_get_col_stat(lp, k-m);
         }
         /* save type and bounds of row/column, if changed */
         if (!(tree->old_type[k] == type && tree->old_lb[k] == lb &&
               tree->old_ub[k] == ub))
         {  bnds = dmp_get_atom(tree->bnds_pool);
            bnds->k = k;
            bnds->type = type;
            bnds->lb = lb;
            bnds->ub = ub;
            bnds->next = node->bnds;
            node->bnds = bnds;
         }
         /* save status of row/column, if changed */
         if (tree->old_stat[k] != stat)
         {  ssss = dmp_get_atom(tree->stat_pool);
            ssss->k = k;
            ssss->stat = stat;
            ssss->next = node->stat;
            node->stat = ssss;
         }
      }
      /* the current subproblem has been frozen */
      tree->curr = NULL;
      return;
}

/*----------------------------------------------------------------------
-- mip_clone_node - clone specified subproblem.
--
-- *Synopsis*
--
-- #include "glpmip.h"
-- void mip_clone_node(MIPTREE *tree, int p, int nnn, int ref[]);
--
-- *Description*
--
-- The routine mip_clone_node clones the specified subproblem, whose
-- reference number is p, creating its nnn exact copies. Note that the
-- specified subproblem must be active and must be in the frozen state
-- (i.e. it must not be the current subproblem).
--
-- Each clone, an exact copy of the specified subproblem, becomes a new
-- active subproblem added to the end of the active list. After cloning
-- the specified subproblem becomes inactive.
--
-- The reference numbers of clone subproblems are stored to locations
-- ref[1], ..., ref[nnn]. */

void mip_clone_node(MIPTREE *tree, int p, int nnn, int ref[])
{     MIPNODE *node, *orig;
      int k;
      /* obtain pointer to the subproblem to be cloned */
      if (!(1 <= p && p <= tree->nslots))
err:     fault("mip_clone_node: p = %d; invalid subproblem reference nu"
            "mber", p);
      node = tree->slot[p].node;
      if (node == NULL) goto err;
      /* the specified subproblem must be active */
      if (node->count != 0)
         fault("mip_clone_node: p = %d; cloning inactive subproblem not"
            " allowed", p);
      /* and must be in the frozen state */
      if (tree->curr == node)
         fault("mip_clone_node: p = %d; cloning current subproblem not "
            "allowed", p);
      /* remove the specified subproblem from the active list, because
         it becomes inactive */
      if (node->prev == NULL)
         tree->head = node->next;
      else
         node->prev->next = node->next;
      if (node->next == NULL)
         tree->tail = node->prev;
      else
         node->next->prev = node->prev;
      node->prev = node->next = NULL;
      tree->a_cnt--;
      /* set the child count of the specified subproblem, which is the
         number of clones to be created */
      if (nnn < 1)
         fault("mip_clone_node: nnn = %d; invalid number of clone subpr"
            "oblems", nnn);
      node->count = nnn;
      /* save pointer to the specified subproblem */
      orig = node;
      /* create clone subproblems */
      for (k = 1; k <= nnn; k++)
      {  /* if no free slots are available, increase the room */
         if (tree->avail == 0)
         {  int nslots = tree->nslots;
            MIPSLOT *save = tree->slot;
            tree->nslots = nslots + nslots;
            insist(tree->nslots > nslots);
            tree->slot = ucalloc(1+tree->nslots, sizeof(MIPSLOT));
            memcpy(&tree->slot[1], &save[1], nslots * sizeof(MIPSLOT));
            /* push more free slots into the stack */
            for (p = tree->nslots; p > nslots; p--)
            {  tree->slot[p].node = NULL;
               tree->slot[p].next = tree->avail;
               tree->avail = p;
            }
            ufree(save);
         }
         /* pull a free slot from the stack */
         p = tree->avail;
         tree->avail = tree->slot[p].next;
         insist(tree->slot[p].node == NULL);
         tree->slot[p].next = 0;
         /* create descriptor for new subproblem */
         tree->slot[p].node = node = dmp_get_atom(tree->node_pool);
         node->p = p;
         node->up = orig;
         node->level = orig->level + 1;
         node->count = 0;
         node->bnds = NULL;
         node->stat = NULL;
         node->bound = orig->bound;
         node->ii_cnt = 0;
         node->ii_sum = 0.0;
         node->temp = NULL;
         node->prev = tree->tail;
         node->next = NULL;
         /* add the new subproblem to the end of the active list */
         if (tree->head == NULL)
            tree->head = node;
         else
            tree->tail->next = node;
         tree->tail = node;
         tree->a_cnt++;
         tree->n_cnt++;
         tree->t_cnt++;
         /* store the clone subproblem reference number */
         ref[k] = p;
      }
      return;
}

/*----------------------------------------------------------------------
-- mip_delete_node - delete specified subproblem.
--
-- *Synopsis*
--
-- #include "glpmip.h"
-- void mip_delete_node(MIPTREE *tree, int p);
--
-- *Description*
--
-- The routine mip_delete_node deletes the specified subproblem, whose
-- reference number is p. The subproblem must be active and must be in
-- the frozen state (i.e. it must not be the current subproblem).
--
-- Note that deletion is performed recursively, i.e. if a subproblem to
-- be deleted is the only child of its parent, the parent subproblem is
-- also deleted, etc. */

void mip_delete_node(MIPTREE *tree, int p)
{     MIPNODE *node, *temp;
      MIPBNDS *bnds;
      MIPSTAT *stat;
      /* obtain pointer to the subproblem to be deleted */
      if (!(1 <= p && p <= tree->nslots))
err:     fault("mip_delete_node: p = %d; invalid subproblem reference n"
            "umber", p);
      node = tree->slot[p].node;
      if (node == NULL) goto err;
      /* the specified subproblem must be active */
      if (node->count != 0)
         fault("mip_delete_node: p = %d; deleting inactive subproblem n"
            "ot allowed", p);
      /* and must be in the frozen state */
      if (tree->curr == node)
         fault("mip_delete_node: p = %d; deleting current subproblem no"
            "t allowed", p);
      /* remove the specified subproblem from the active list, because
         it is gone from the tree */
      if (node->prev == NULL)
         tree->head = node->next;
      else
         node->prev->next = node->next;
      if (node->next == NULL)
         tree->tail = node->prev;
      else
         node->next->prev = node->prev;
      node->prev = node->next = NULL;
      tree->a_cnt--;
loop: /* recursive deletion starts here */
      /* delete the bounds change list */
      while (node->bnds != NULL)
      {  bnds = node->bnds;
         node->bnds = bnds->next;
         dmp_free_atom(tree->bnds_pool, bnds);
      }
      /* delete the statuses change list */
      while (node->stat != NULL)
      {  stat = node->stat;
         node->stat = stat->next;
         dmp_free_atom(tree->stat_pool, stat);
      }
      /* free the corresponding node slot */
      p = node->p;
      insist(tree->slot[p].node == node);
      tree->slot[p].node = NULL;
      tree->slot[p].next = tree->avail;
      tree->avail = p;
      /* save pointer to the parent subproblem */
      temp = node->up;
      /* delete the subproblem descriptor */
      dmp_free_atom(tree->node_pool, node);
      tree->n_cnt--;
      /* take pointer to the parent subproblem */
      node = temp;
      if (node != NULL)
      {  /* the parent subproblem exists; decrease the number of its
            child subproblems */
         insist(node->count > 0);
         node->count--;
         /* if now the parent subproblem has no childs, it also must be
            deleted */
         if (node->count == 0) goto loop;
      }
      return;
}

/*----------------------------------------------------------------------
-- mip_delete_tree - delete branch-and-bound tree.
--
-- *Synopsis*
--
-- #include "glpmip.h"
-- void mip_delete_tree(MIPTREE *tree);
--
-- *Description*
--
-- The routine mip_delete_tree deletes the branch-and-bound tree, which
-- the parameter tree points to. This frees all the memory allocated to
-- the tree. */

void mip_delete_tree(MIPTREE *tree)
{     ufree(tree->int_col);
      dmp_delete_pool(tree->node_pool);
      dmp_delete_pool(tree->bnds_pool);
      dmp_delete_pool(tree->stat_pool);
      ufree(tree->slot);
      ufree(tree->mipx);
      lpx_delete_prob(tree->lp);
      ufree(tree->old_type);
      ufree(tree->old_lb);
      ufree(tree->old_ub);
      ufree(tree->old_stat);
      ufree(tree->non_int);
      ufree(tree);
      return;
}

/*----------------------------------------------------------------------
-- mip_pseudo_root - find pseudo-root of the branch-and-bound tree.
--
-- *Synopsis*
--
-- #include "glpmip.h"
-- int mip_pseudo_root(MIPTREE *tree);
--
-- *Description*
--
-- The routine mip_pseudo_root finds so-called pseudo-root of the tree,
-- a node which is the highest-leveled common ancestor of all active
-- nodes including the current one . (Juenger calls such node "the root
-- of the remaining tree".)
--
-- If walk from the root of the tree, the pseudo-root is the first node
-- which has more than one child:
--
--                 root -->    A              Level 0
--                             |
--                             B              Level 1
--                             |
--          pseudo-root -->    C              Level 2
--                            / \
--                          /     \
--                        /         \
--                       D           G        Level 3
--                     /   \       /   \
--                    E     F     H     I     Level 4
--                   ...   ...   ...   ...
--
-- (However, if the tree has the only active node, the pseudo-root is
-- that active node by definition.)
--
-- *Returns*
--
-- The routine mip_pseudo_root returns the subproblem reference number
-- which corresponds to the pseudo-root. However, if the tree is empty,
-- zero is returned. */

int mip_pseudo_root(MIPTREE *tree)
{     MIPNODE *root, *node;
      /* obtain pointer to the root node of the entire tree */
      root = tree->slot[1].node;
      /* if the tree is empty, there is no pseudo-root node */
      if (root == NULL) goto done;
      /* obtain pointer to any active node */
      node = tree->head;
      insist(node != NULL);
      /* build the path from the root to the active node */
      node->temp = NULL;
      for (node = node; node != NULL; node = node->up)
      {  if (node->up == NULL)
            insist(node == root);
         else
            node->up->temp = node;
      }
      /* walk from the root to the active node and find the pseudo-root
         of the tree */
      for (root = root; root != NULL; root = root->temp)
         if (root->count != 1) break;
      insist(root != NULL);
done: /* return the reference number of the pseudo-root found */
      return root == NULL ? 0 : root->p;
}

/*----------------------------------------------------------------------
-- mip_best_node - find active node with best local bound.
--
-- *Synopsis*
--
-- #include "glpmip.h"
-- int mip_best_node(MIPTREE *tree);
--
-- *Description*
--
-- The routine mip_best_node finds an active node whose local bound is
-- best among other active nodes.
--
-- It is understood that the integer optimal solution of the original
-- mip problem cannot be better than the best bound, so the best bound
-- is an upper (minimization) or lower (maximization) global bound for
-- the original problem.
--
-- *Returns*
--
-- The routine mip_best_node returns the subproblem reference number
-- for the best node. However, if the tree is empty, it returns zero. */

int mip_best_node(MIPTREE *tree)
{     MIPNODE *node, *best = NULL;
      switch (tree->dir)
      {  case LPX_MIN:
            /* minimization */
            for (node = tree->head; node != NULL; node = node->next)
               if (best == NULL || best->bound > node->bound)
                  best = node;
            break;
         case LPX_MAX:
            /* maximization */
            for (node = tree->head; node != NULL; node = node->next)
               if (best == NULL || best->bound < node->bound)
                  best = node;
            break;
         default:
            insist(tree != tree);
      }
      return best == NULL ? 0 : best->p;
}

/*----------------------------------------------------------------------
-- mip_relative_gap - compute relative mip gap.
--
-- *Synopsis*
--
-- #include "glpmip.h"
-- gnm_float mip_relative_gap(MIPTREE *tree);
--
-- *Description*
--
-- The routine mip_relative_gap computes the relative mip gap using the
-- formula:
--
--    rel_gap = |best_mip - best_bound| / (|best_mip| + DBL_EPSILON),
--
-- where best_mip is the best integer feasible solution found so far,
-- best_bound is the best global bound. If no integer feasible solution
-- has been found yet, rel_gap is set to DBL_MAX.
--
-- *Returns*
--
-- The routine mip_relative_gap returns the relative mip gap. */

gnm_float mip_relative_gap(MIPTREE *tree)
{     int p;
      gnm_float best_mip, best_bound, rel_gap;
      if (tree->found)
      {  best_mip = tree->best;
         p = mip_best_node(tree);
         if (p == 0)
         {  /* the tree is empty */
            rel_gap = 0.0;
         }
         else
         {  best_bound = tree->slot[p].node->bound;
            rel_gap = gnm_abs(best_mip - best_bound) / (gnm_abs(best_mip) +
               DBL_EPSILON);
         }
      }
      else
      {  /* no integer feasible solution has been found yet */
         rel_gap = DBL_MAX;
      }
      return rel_gap;
}

/*----------------------------------------------------------------------
-- mip_solve_node - solve LP relaxation of current subproblem.
--
-- *Synopsis*
--
-- #include "glpmip.h"
-- int mip_solve_node(MIPTREE *tree);
--
-- *Description*
--
-- The routine mip_solve_node solves or re-optimizes LP relaxation of
-- the current subproblem using the simplex method.
--
-- *Returns*
--
-- The routine returns the code which is reported by lpx_simplex(). */

int mip_solve_node(MIPTREE *tree)
{     LPX *lp = tree->lp;
      int ret;
      if (tree->curr == NULL)
         fault("mip_solve_node: current subproblem does not exist");
      /* set some control parameters */
      lpx_set_int_parm(lp, LPX_K_MSGLEV,
         tree->msg_lev <= 1 ? tree->msg_lev : 2);
      lpx_set_int_parm(lp, LPX_K_DUAL, 1);
      lpx_set_real_parm(lp, LPX_K_OUTDLY,
         tree->msg_lev <= 2 ? tree->out_dly : 0.0);
      /* if the incumbent objective value is already known, use it to
         prematurely terminate the dual simplex search */
      if (tree->found)
      {  switch (tree->dir)
         {  case LPX_MIN:
               lpx_set_real_parm(lp, LPX_K_OBJUL, tree->best);
               break;
            case LPX_MAX:
               lpx_set_real_parm(lp, LPX_K_OBJLL, tree->best);
               break;
            default:
               insist(tree != tree);
         }
      }
      /* try to solve/re-optimize the LP relaxation */
      ret = lpx_simplex(lp);
      return ret;
}

/* eof */
