/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * mps.c: MPS file importer.
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 *      MPS importer module.  MPS format is a de facto standard ASCII format
 *      among most of the commercial LP solvers.
 *
 *      This implementation does not yet support ranges and all types
 *      of bounds but is already quite suitable for testing the
 *      solving algorithms.  See, for example, the Netlib collection
 *      of LP problems in MPS format (ftp://netlib2.cs.utk.edu/lp/data).
 *
 *      Supported bound types are: UP and LO.
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
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "cell.h"
#include "sheet.h"
#include "value.h"
#include "file.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"
#include "ranges.h"
#include "style.h"
#include "value.h"
#include "solver.h"
#include "sheet-style.h"
#include "parse-util.h"
#include "mps.h"

#include <libgnome/gnome-i18n.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;


/* Error margin in the equiality comparison */
static const gchar *BINDING_LIMIT    = "0.00000001";


/*************************************************************************
 *
 * Sheet creation.
 */

/* Writes a string into a cell. */
static inline void
mps_set_cell (Sheet *sh, int col, int row, const gchar *str)
{
        Cell *cell = sheet_cell_fetch (sh, col, row);

        sheet_cell_set_value (cell, value_new_string (str));
}

/* Writes a float into a cell. */
static inline void
mps_set_cell_float (Sheet *sh, int col, int row, const gnum_float f)
{
        Cell *cell = sheet_cell_fetch (sh, col, row);

        sheet_cell_set_value (cell, value_new_float (f));
}

static void
mps_set_style (Sheet *sh, int c1, int r1, int c2, int r2,
	       gboolean italic, gboolean bold, gboolean ulined)
{
        MStyle *mstyle;
	Range  range;

	mstyle = mstyle_new ();
	range_init (&range, c1, r1, c2, r2);
	mstyle_set_font_italic (mstyle, italic);
	mstyle_set_font_bold   (mstyle, bold);
	mstyle_set_font_uline  (mstyle, ulined);
	sheet_style_apply_range (sh, &range, mstyle);
}

/* Callback for the hash table mapping. */
static void
put_into_index (gpointer key, gpointer value, gpointer user_data)
{
        MpsInputContext *ctxt = (MpsInputContext *) user_data;
	MpsColInfo *info = (MpsColInfo *) value;

	ctxt->col_name_tbl[info->index] = info->name;
}

/* Make the constraint coefficient matrix and other preparations. */
static void
mps_prepare (WorkbookView *wbv, MpsInputContext *ctxt)
{
        gint i, n, col;
        GSList *current, *tmp;

        ctxt->rows = g_slist_reverse (ctxt->rows);
	ctxt->cols = g_slist_reverse (ctxt->cols);

	ctxt->col_name_tbl = g_new (gchar *, ctxt->n_cols);
	g_hash_table_foreach (ctxt->col_hash, put_into_index, (gpointer) ctxt);

	ctxt->matrix = g_new (gnum_float *, ctxt->n_rows + ctxt->n_bounds);
	for (i = 0; i < ctxt->n_rows + ctxt->n_bounds; i++) {
	          ctxt->matrix[i] = g_new (gnum_float, ctxt->n_cols);
		  for (n = 0; n < ctxt->n_cols; n++)
		            ctxt->matrix[i][n] = 0.0;
	}

	current = ctxt->cols;
	while (current != NULL) {
	          MpsCol     *col = (MpsCol *) current->data;
		  MpsColInfo *info;

		  info = (MpsColInfo *) g_hash_table_lookup (ctxt->col_hash,
							     col->name);
		  ctxt->matrix[col->row->index][info->index] = col->value;
		  current = current->next;
	}

	if ((ctxt->n_cols + MAX_COL - 1) / MAX_COL == 1)
		col = CONSTRAINT_COL + ctxt->n_cols - 1;
	else
		col = CONSTRAINT_COL + MAX_COL - 1;

	current    = ctxt->bounds;
	tmp        = ctxt->rows;
	ctxt->rows = NULL;
	i          = ctxt->n_rows + ctxt->n_bounds - 2;
	while (current != NULL) {
	        MpsBound *bound = (MpsBound *) current->data;
		static const MpsRowType type_map[] = {
		        LessOrEqualRow, GreaterOrEqualRow
		};

		ctxt->matrix[ctxt->n_rows][bound->col_index] = 1.0;

		mps_set_cell_float (wbv->current_sheet, col + 3,
				    i-- + CONSTRAINT_ROW,
				    bound->value);

		mps_add_row (ctxt, type_map[(gint) bound->type], bound->name);

		current = current->next;
	}
	ctxt->rows = g_slist_concat (tmp, ctxt->rows);
}


static void
mps_write_sheet_labels (MpsInputContext *ctxt, Sheet *sh)
{
	int i, row, col, inc;
	int n_rows_per_fn;

	/*
	 * Sheet header titles.
	 */

	/* Print 'Program Name'. */
	n_rows_per_fn = (ctxt->n_cols + MAX_COL - 1) / MAX_COL;
	mps_set_cell (sh, MAIN_INFO_COL, MAIN_INFO_ROW - 1, _("Program Name"));
	mps_set_style (sh, MAIN_INFO_COL, MAIN_INFO_ROW - 1,
		       MAIN_INFO_COL + 5, MAIN_INFO_ROW - 1,
		       FALSE, TRUE, FALSE);

	/* Print 'Status'. */
	mps_set_cell (sh, MAIN_INFO_COL + 3, MAIN_INFO_ROW - 1, _("Feasible"));

	/* Names of the variables. */
	row = VARIABLE_ROW - 1;
	if (n_rows_per_fn == 1) {
		for (i = 0; i < ctxt->n_cols; i++)
			mps_set_cell (sh, VARIABLE_COL + i, row,
				      ctxt->col_name_tbl [i]);
	} else {
		GString *buf;
		for (i = 0; i < MAX_COL; i++) {
			buf = g_string_new ("");
			g_string_sprintfa (buf, "C[%d]", i + 1);
			mps_set_cell (sh, VARIABLE_COL + i, row, buf->str);
			g_string_free (buf, FALSE);
		}

		for (i = 0; i < n_rows_per_fn; i++) {
			buf = g_string_new ("");
			g_string_sprintfa (buf, "R[%d]", i + 1);
			mps_set_cell (sh, VARIABLE_COL - 1, row + i + 1,
				      buf->str);
			g_string_free (buf, FALSE);
		}
		mps_set_style (sh, VARIABLE_COL - 1, row,
			       VARIABLE_COL - 1, row + n_rows_per_fn,
			       FALSE, TRUE, FALSE);
	}
	mps_set_style (sh, VARIABLE_COL, row, VARIABLE_COL + MAX_COL,
		       row, FALSE, TRUE, FALSE);


	/* Print 'Objective value'. */
	mps_set_cell (sh, MAIN_INFO_COL + 1, MAIN_INFO_ROW - 1,
		      _("Objective Value"));

	inc = n_rows_per_fn * 2;

	/* Print 'Objective function:' */
	mps_set_cell (sh, VARIABLE_COL, VARIABLE_ROW - 2,
		      _("Objective function:"));
	mps_set_style (sh, VARIABLE_COL, VARIABLE_ROW - 2,
		       VARIABLE_COL, VARIABLE_ROW - 2,
		       FALSE, TRUE, TRUE);

	/* Print 'Constraints:'. */
	mps_set_cell (sh, CONSTRAINT_COL, CONSTRAINT_ROW - 2 + inc,
		      _("Constraints:"));
	mps_set_style (sh, CONSTRAINT_COL, CONSTRAINT_ROW - 2 + inc,
		       CONSTRAINT_COL, CONSTRAINT_ROW - 2 + inc,
		       FALSE, TRUE, TRUE);

	/*
	 * Print constraint titles.
	 */

	/* Name field. */
	row = CONSTRAINT_ROW - 1 + inc;
	mps_set_cell (sh, CONSTRAINT_COL - 1, row, _("Name"));

	/* Names of the variables. */
	if (n_rows_per_fn == 1) {
		for (i = 0; i < ctxt->n_cols; i++)
			mps_set_cell (sh, CONSTRAINT_COL + i, row,
				      ctxt->col_name_tbl [i]);
	} else {
		GString *buf;
		for (i = 0; i < MAX_COL; i++) {
			buf = g_string_new ("");
			g_string_sprintfa (buf, "C[%d]", i + 1);
			mps_set_cell (sh, CONSTRAINT_COL + i, row, buf->str);
			g_string_free (buf, FALSE);
		}
	}
	mps_set_style (sh, CONSTRAINT_COL - 1, row,
		       CONSTRAINT_COL + MAX_COL + 5,
		       row, FALSE, TRUE, FALSE);


	/* Value, Type, RHS, Slack, and Status titles. */
	if (n_rows_per_fn == 1)
		col = CONSTRAINT_COL + ctxt->n_cols - 1;
	else
		col = CONSTRAINT_COL + MAX_COL - 1;

	mps_set_cell (sh, col + 1, row, _("Value"));
	mps_set_cell (sh, col + 2, row, _("Type"));
	mps_set_cell (sh, col + 3, row, _("RHS"));
	mps_set_cell (sh, col + 4, row, _("Slack"));
	mps_set_cell (sh, col + 5, row, _("Status"));
}


static void
mps_write_coefficients (MpsInputContext *ctxt, Sheet *sh,
			SolverParameters *param)
{
	GSList  *current;
	int     i, n, r, ecol, inc2;
	int     n_rows_per_fn;
	GString *var_range [2];
	Range   range;
	Cell    *cell;
	GString *buf;

	/*
	 * Add objective function stuff into the sheet.
	 */

	/* Print the column names, initialize the variables to 0, and
	 * print the coefficients of the objective function. */
	n_rows_per_fn = (ctxt->n_cols + MAX_COL - 1) / MAX_COL;
	if (n_rows_per_fn == 1)
		ecol = CONSTRAINT_COL + ctxt->n_cols - 1;
	else
		ecol = CONSTRAINT_COL + MAX_COL - 1;
	for (i = 0; i < ctxt->n_cols; i++) {
		  mps_set_cell_float (sh, VARIABLE_COL + i % MAX_COL,
				      VARIABLE_ROW + (i / MAX_COL), 0.0);
		  mps_set_cell_float (sh, VARIABLE_COL + i % MAX_COL,
				      VARIABLE_ROW + n_rows_per_fn
				      + (i / MAX_COL) + 1,
		           ctxt->matrix[ctxt->objective_row->index][i]);
	}

	/*
	 * Add constraints into the sheet.
	 */

	/* Print constraints. */
	inc2               = 2 * n_rows_per_fn;
	param->constraints = NULL;
	var_range [0]      = g_string_new ("");
	var_range [1]      = g_string_new ("");

	/* Initialize var_range to contain the range name of the
	 * objective function variables. */
	if (ctxt->n_cols % MAX_COL != 0) {
		i = 0;
		if (n_rows_per_fn > 1) {
			range_init (&range, VARIABLE_COL, VARIABLE_ROW,
				    MAX_COL, VARIABLE_ROW + n_rows_per_fn - 2);
			g_string_sprintfa (var_range [i++], "%s",
					   range_name (&range));
		}
		range_init (&range, VARIABLE_COL,
			    VARIABLE_ROW + n_rows_per_fn - 1,
			    (ctxt->n_cols % MAX_COL),
			    VARIABLE_ROW + n_rows_per_fn - 1);
		g_string_sprintfa (var_range [i], "%s", range_name (&range));
	} else {
		range_init (&range, VARIABLE_COL, VARIABLE_ROW,
			    MAX_COL, VARIABLE_ROW + n_rows_per_fn - 1);
		g_string_sprintfa (var_range [0], "%s", range_name (&range));
	}

	i = 0;
	for (current = ctxt->rows; current != NULL; current = current->next) {
	          SolverConstraint   *c;
		  MpsRow             *row;
		  int                col, r;

		  static const gchar *type_str[] = {
			  "=", "<=", ">="
		  };
		  static const SolverConstraintType type_map[] = {
			  SolverEQ, SolverLE, SolverGE
		  };

		  row = (MpsRow *) current->data;
		  if (row->type == ObjectiveRow)
		          continue;
		  col = CONSTRAINT_COL;
		  r   = CONSTRAINT_ROW  +  i * n_rows_per_fn  +  inc2;
		  
		  /* Add row name. */
		  if (n_rows_per_fn == 1)
			  mps_set_cell (sh, col - 1, r, row->name);
		  else {
			  for (n = 0; n < (ctxt->n_cols+MAX_COL)/MAX_COL; n++) {
				  buf = g_string_new ("");
				  g_string_sprintfa (buf, "%s[%d]",
						     row->name, n+1);
				  mps_set_cell (sh, col - 1, r + n, buf->str);
				  g_string_free (buf, FALSE);
			  }
		  }

		  /* Coefficients. */
		  for (n = 0; n < ctxt->n_cols; n++)
		          mps_set_cell_float (sh, col + n % MAX_COL,
					      r + n / MAX_COL,
					      ctxt->matrix[row->index][n]);

		  /* Add Type field. */
		  mps_set_cell (sh, ecol + 2, r, type_str[(int) row->type]);


		  /* Add RHS field (zero). */
		  mps_set_cell_float (sh, ecol + 3, r, 0);


		  /* Add LHS field using SUMPRODUCT function. */
		  buf = g_string_new ("");
		  if (ctxt->n_cols % MAX_COL == 0) {
			  range_init (&range, col, r, MAX_COL,
				      r + n_rows_per_fn - 1); 
			  g_string_sprintfa (buf, "=SUMPRODUCT(%s,%s)",
					     var_range[0]->str,
					     range_name (&range));
		  } else {
			  if (n_rows_per_fn > 1) {
				  range_init (&range, col, r, MAX_COL,
					      r + n_rows_per_fn - 2); 
				  g_string_sprintfa (buf, "=SUMPRODUCT(%s,%s)",
						     var_range[0]->str,
						     range_name (&range));
				  range_init (&range, col, r+n_rows_per_fn - 1,
					      ctxt->n_cols % MAX_COL,
					      r + n_rows_per_fn - 1); 
				  g_string_sprintfa (buf, "+SUMPRODUCT(%s,%s)",
						     var_range[1]->str,
						     range_name (&range));
			  } else {
				  range_init (&range, col, r, ctxt->n_cols, r);
				  g_string_sprintfa (buf, "=SUMPRODUCT(%s,%s)",
						     var_range[0]->str,
						     range_name (&range));
			  }
		  }
		  cell = sheet_cell_fetch (sh, ecol + 1, r);
		  sheet_cell_set_text  (cell, buf->str);
		  g_string_free (buf, FALSE);


		  /* Add Slack calculation */
		  buf = g_string_new ("");
		  if (row->type == LessOrEqualRow) {
		          g_string_sprintfa (buf, "=%s-",
					     cell_coord_name (ecol + 3, r));
			  g_string_sprintfa (buf, "%s",
					     cell_coord_name (ecol + 1, r));
		  } else if (row->type == GreaterOrEqualRow) {
		          g_string_sprintfa (buf, "=%s-",
					     cell_coord_name (ecol + 1, r));
			  g_string_sprintfa (buf, "%s",
					     cell_coord_name (ecol + 3, r));
		  } else {
		          g_string_sprintfa (buf, "=ABS(%s-",
					     cell_coord_name (ecol + 1, r));
			  g_string_sprintfa (buf, "%s",
					     cell_coord_name (ecol + 3, r));
			  g_string_sprintfa (buf, ")");
		  }
		  cell = sheet_cell_fetch (sh, ecol + 4, r);
		  sheet_cell_set_text (cell, buf->str);
		  g_string_free (buf, FALSE);


		  /* Add Status field */
		  buf = g_string_new ("");
		  if (row->type == EqualityRow) {
		          g_string_sprintfa (buf,
					     "=IF(%s>%s,\"NOK\", \"Binding\")",
					     cell_coord_name (ecol + 4, r),
					     BINDING_LIMIT);
		  } else {
		          g_string_sprintfa (buf,
					     "=IF(%s<0,\"NOK\", ",
					     cell_coord_name (ecol + 4, r));
			  g_string_sprintfa (buf,
					     "IF(%s<=%s,\"Binding\","
					     "\"Not Binding\"))",
					     cell_coord_name (ecol + 4, r),
					     BINDING_LIMIT);
		  }
		  cell = sheet_cell_fetch (sh, ecol + 5, r);
		  cell_set_text (cell, buf->str);
		  cell_eval (cell);
		  g_string_free (buf, FALSE);

		  /* Add Solver constraint */
		  c          = g_new (SolverConstraint, 1);
		  c->lhs.col = ecol + 1;
		  c->lhs.row = r;
		  c->rhs.col = ecol + 3;
		  c->rhs.row = r;
		  c->type    = type_map[row->type];  /* const_cast */
		  c->cols    = 1;
		  c->rows    = 1;
		  c->str     = write_constraint_str (c->lhs.col, c->lhs.row,
						     c->rhs.col, c->rhs.row,
						     c->type, c->cols,
						     c->rows);

		  param->constraints = g_slist_append (param->constraints, c);
		  i++;
	}

	/* Write RHSes. */
	current = ctxt->rhs;
	r   = CONSTRAINT_ROW  +  inc2;
	while (current != NULL) {
	          MpsRhs *rhs = (MpsRhs *) current->data;

		  mps_set_cell_float (sh, ecol + 3,
				      r + rhs->row->index * n_rows_per_fn,
				      rhs->value);
		  current = current->next;
	}

	/* Write the objective fn. */
	buf = g_string_new ("");
	if (ctxt->n_cols % MAX_COL == 0) {
		range_init (&range, VARIABLE_COL,
			    VARIABLE_ROW + 1 + n_rows_per_fn,
			    MAX_COL,
			    VARIABLE_ROW + 2*n_rows_per_fn);
		g_string_sprintfa (buf, "=SUMPRODUCT(%s,%s)", var_range[0]->str,
				   range_name (&range));
	} else {
		if (n_rows_per_fn > 1) {
			range_init (&range, VARIABLE_COL,
				    VARIABLE_ROW + 1 + n_rows_per_fn,
				    MAX_COL,
				    VARIABLE_ROW + 2 * n_rows_per_fn - 1);
			g_string_sprintfa (buf, "=SUMPRODUCT(%s,%s)",
					   var_range[0]->str,
					   range_name (&range));
			range_init (&range, VARIABLE_COL,
				    VARIABLE_ROW + 2 * n_rows_per_fn,
				    ctxt->n_cols % MAX_COL,
				    VARIABLE_ROW + 2 * n_rows_per_fn);
			g_string_sprintfa (buf, "+SUMPRODUCT(%s,%s)",
					   var_range[1]->str,
					   range_name (&range));
		} else {
			range_init (&range, VARIABLE_COL,
				    VARIABLE_ROW + 1 + n_rows_per_fn,
				    ctxt->n_cols,
				    VARIABLE_ROW + 1 + n_rows_per_fn);
			g_string_sprintfa (buf, "=SUMPRODUCT(%s,%s)",
					   var_range[0]->str,
					   range_name (&range));
		}
	}
	cell = sheet_cell_fetch (sh, OBJECTIVE_VALUE_COL, MAIN_INFO_ROW);
	sheet_cell_set_text (cell, buf->str);
	g_string_free (buf, FALSE);

	/* Store the input cell range for the Solver dialog. */
	g_string_free (var_range [0], FALSE);
	var_range [0] = g_string_new ("");
	range_init (&range, VARIABLE_COL, VARIABLE_ROW,
		    MAX_COL, VARIABLE_ROW + n_rows_per_fn - 1);
	g_string_sprintfa (var_range [0], "%s", range_name (&range));

	param->input_entry_str = g_strdup (var_range [0]->str);
	g_string_free (var_range [0], FALSE);
	g_string_free (var_range [1], FALSE);
}

/* Creates the spreadsheet model. */
static void
mps_create_sheet (MpsInputContext *ctxt, WorkbookView *wbv)
{
        Sheet            *sh = wbv->current_sheet;
	gint             i;
	int              n_rows_per_fn;
	SolverParameters *param = sh->solver_parameters;

	n_rows_per_fn = (ctxt->n_cols + MAX_COL - 1) / MAX_COL;
	mps_prepare (wbv, ctxt);

	mps_write_sheet_labels (ctxt, sh);
	mps_write_coefficients (ctxt, sh, param);

	/* Print the name of the objective function */
	if (ctxt->n_cols < MAX_COL)
		mps_set_cell (sh, VARIABLE_COL - 1,
			      VARIABLE_ROW + 1 + n_rows_per_fn,
			      ctxt->objective_row->name);
	else {
		for (i = 0; i < n_rows_per_fn; i++) {
			GString *buf = g_string_new ("");
			g_string_sprintfa (buf, "%s (R[%d])",
					   ctxt->objective_row->name, i+1);
			mps_set_cell (sh, VARIABLE_COL - 1,
				      VARIABLE_ROW + 1 + i + n_rows_per_fn,
				      buf->str);
			g_string_free (buf, FALSE);
		}
	}

	param->target_cell = sheet_cell_fetch (sh, OBJECTIVE_VALUE_COL,
					       MAIN_INFO_ROW);
	param->problem_type = SolverMinimize;

	/* Write the name of the program. */
	if (ctxt->name != NULL)
		mps_set_cell (sh, MAIN_INFO_COL, MAIN_INFO_ROW, ctxt->name);


	/* Autofit column A */
	i = sheet_col_size_fit_pixels (sh, 0);
	if (i == 0)
	          return;
	sheet_col_set_size_pixels (sh, 0, i, TRUE);
	sheet_recompute_spans_for_col (sh, 0);
	workbook_recalc (sh->workbook);
}


/************************************************************************
 *
 * Data structure initializations and releasing.
 */

/* Make the initializations. */
static MpsInputContext *
mps_input_context_new (IOContext *io_context, Workbook *wb, GsfInput *input)
{
        MpsInputContext *ctxt = NULL;

	ctxt = g_new (MpsInputContext, 1);
	ctxt->io_context     = io_context;

	ctxt->input	     = gsf_input_textline_new (input);
	ctxt->line_no        = 1;
	ctxt->line           = NULL;
	ctxt->sheet          = workbook_sheet_add (wb, NULL, FALSE);

	ctxt->name           = NULL;
	ctxt->rows           = NULL;
	ctxt->cols           = NULL;
	ctxt->rhs            = NULL;
	ctxt->bounds         = NULL;
        ctxt->row_hash	     = g_hash_table_new (g_str_hash, g_str_equal);
        ctxt->col_hash	     = g_hash_table_new (g_str_hash, g_str_equal);
        ctxt->col_name_tbl   = NULL;
        ctxt->matrix	     = NULL;

	ctxt->n_rows = ctxt->n_cols = ctxt->n_bounds = 0;

	g_slist_free (ctxt->rows);

	io_progress_message (io_context, _("Reading file..."));

	return ctxt;
}

/* Call-back for mps_input_context_destroy. */
static gboolean
rh_rm_cb (gpointer key, gpointer value, gpointer user_data)
{
        return TRUE;
}

/* Call-back for mps_input_context_destroy. */
static gboolean
ch_rm_cb (gpointer key, gpointer value, gpointer user_data)
{
        MpsColInfo *info = (MpsColInfo *) value;

	g_free (info->name);
	g_free (info);

        return TRUE;
}

/* Free the allocated memory. */
static void
mps_input_context_destroy (MpsInputContext *ctxt)
{
        GSList *current;

	io_progress_unset (ctxt->io_context);

	/* Free ROWS */
	for (current = ctxt->rows; current != NULL; current = current->next) {
	           MpsRow *row = (MpsRow *) current->data;
		   g_free (row->name);
		   g_free (current->data);
	}

	/* Free COLUMNS */
	for (current = ctxt->cols; current != NULL; current = current->next) {
	           MpsCol *col = (MpsCol *) current->data;
		   g_free (col->name);
		   g_free (current->data);
	}

	ctxt->cols = NULL;
	/* Free RHSs */
	for (current = ctxt->rhs; current != NULL; current = current->next) {
	           MpsRhs *rhs = (MpsRhs *) current->data;
		   g_free (rhs->name);
		   g_free (current->data);
	}

	/* Free BOUNDS */
	for (current = ctxt->bounds; current!=NULL; current = current->next) {
	           MpsBound *bound = (MpsBound *) current->data;
		   g_free (bound->name);
		   g_free (current->data);
	}

	g_slist_free (ctxt->rows);
	g_slist_free (ctxt->cols);
	g_slist_free (ctxt->rhs);
	g_slist_free (ctxt->bounds);

	g_hash_table_foreach_remove (ctxt->row_hash, (GHRFunc) rh_rm_cb, NULL);
	g_hash_table_foreach_remove (ctxt->col_hash, (GHRFunc) ch_rm_cb, NULL);
	g_hash_table_destroy (ctxt->row_hash);
	g_hash_table_destroy (ctxt->col_hash);

	if (ctxt->col_name_tbl) {
		g_free (ctxt->col_name_tbl);
		ctxt->col_name_tbl = NULL;
	}

	if (ctxt->matrix) {
		g_free (ctxt->matrix);
		ctxt->matrix = NULL;
	}
	g_free (ctxt->name);
	g_object_unref (G_OBJECT (ctxt->input)); ctxt->input = NULL;
	g_free (ctxt);
}



/*---------------------------------------------------------------------*/

/*
 * The public plug-in API.
 */

void
mps_file_open (GnumFileOpener const *fo, IOContext *io_context,
               WorkbookView *wbv, GsfInput *input)
{
        MpsInputContext *ctxt;

	ctxt = mps_input_context_new (io_context, wb_view_workbook (wbv),
				      input);
	if (ctxt != NULL) {
	        mps_parse_file (ctxt);
		if (gnumeric_io_error_occurred (io_context)) {
		        gnumeric_io_error_push (io_context, error_info_new_str
						(_("Error while reading MPS "
						   "file.")));
		} else
			mps_create_sheet (ctxt, wbv);
		mps_input_context_destroy (ctxt);
	} else if (!gnumeric_io_error_occurred)
		gnumeric_io_error_unknown (io_context);
}
