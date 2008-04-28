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
#include "mps.h"
#include "cell.h"
#include "sheet.h"
#include "value.h"
#include <goffice/app/io-context.h>
#include "workbook-view.h"
#include "workbook.h"
#include <gnm-plugin.h>
#include "ranges.h"
#include "style.h"
#include "value.h"
#include "solver.h"
#include "sheet-style.h"
#include "parse-util.h"
#include "func.h"
#include "expr.h"
#include <goffice/app/file.h>
#include <goffice/utils/go-format.h>
#include <goffice/app/error-info.h>
#include <glib/gi18n-lib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

GNM_PLUGIN_MODULE_HEADER;


/*************************************************************************
 *
 * Sheet creation.
 */

/* Writes a string into a cell. */
static inline void
mps_set_cell (Sheet *sheet, int col, int row, const gchar *str)
{
        gnm_cell_set_value (sheet_cell_fetch (sheet, col, row),
			value_new_string (str));
}

/* Writes a float into a cell. */
static inline void
mps_set_cell_float (Sheet *sheet, int col, int row, const gnm_float f)
{
        gnm_cell_set_value (sheet_cell_fetch (sheet, col, row), value_new_float (f));
}

static void
mps_set_style (Sheet *sh, int c1, int r1, int c2, int r2,
	       gboolean italic, gboolean bold, gboolean ulined)
{
        GnmStyle *mstyle;
	GnmRange  range;

	mstyle = gnm_style_new ();
	range_init (&range, c1, r1, c2, r2);
	gnm_style_set_font_italic (mstyle, italic);
	gnm_style_set_font_bold   (mstyle, bold);
	gnm_style_set_font_uline  (mstyle, ulined);
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

	ctxt->matrix = g_new (gnm_float *, ctxt->n_rows + ctxt->n_bounds);
	for (i = 0; i < ctxt->n_rows + ctxt->n_bounds; i++) {
	          ctxt->matrix[i] = g_new (gnm_float, ctxt->n_cols);
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
		        LessOrEqualRow, GreaterOrEqualRow, EqualityRow
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
				      ctxt->col_name_tbl[i]);
	} else {
		GString *buf;
		for (i = 0; i < MAX_COL; i++) {
			buf = g_string_new (NULL);
			g_string_append_printf (buf, "C[%d]", i + 1);
			mps_set_cell (sh, VARIABLE_COL + i, row, buf->str);
			g_string_free (buf, TRUE);
		}

		for (i = 0; i < n_rows_per_fn; i++) {
			buf = g_string_new (NULL);
			g_string_append_printf (buf, "R[%d]", i + 1);
			mps_set_cell (sh, VARIABLE_COL - 1, row + i + 1,
				      buf->str);
			g_string_free (buf, TRUE);
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
				      ctxt->col_name_tbl[i]);
	} else {
		GString *buf;
		for (i = 0; i < MAX_COL; i++) {
			buf = g_string_new (NULL);
			g_string_append_printf (buf, "C[%d]", i + 1);
			mps_set_cell (sh, CONSTRAINT_COL + i, row, buf->str);
			g_string_free (buf, TRUE);
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
}


static void
mps_write_coefficients (MpsInputContext *ctxt, Sheet *sh,
			SolverParameters *param)
{
	GSList  *current;
	int     i, n, r, ecol, inc2;
	int     n_rows_per_fn;
	GnmRange   range, v_range;
	GnmCell    *cell;
	const GnmExprTop *texpr;

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

	/* Initialize var_range to contain the range name of the
	 * objective function variables. */
	i = 0;
	range_init (&v_range, VARIABLE_COL,
		    VARIABLE_ROW + n_rows_per_fn - 1,
		    (ctxt->n_cols % MAX_COL),
		    VARIABLE_ROW + n_rows_per_fn - 1);

	i = 0;
	for (current = ctxt->rows; current != NULL; current = current->next) {
	          SolverConstraint   *c;
		  MpsRow             *row = current->data;
		  int                col, r;
		  const GnmExprTop   *texpr;
		  GnmCellRef          ref1, ref2;

		  static const gchar *const type_str[] = {
			  "=", "<=", ">="
		  };
		  static const SolverConstraintType type_map[] = {
			  SolverEQ, SolverLE, SolverGE
		  };

		  if (row->type == ObjectiveRow)
		          continue;
		  col = CONSTRAINT_COL;
		  r   = CONSTRAINT_ROW  +  i * n_rows_per_fn  +  inc2;

		  /* Add row name. */
		  mps_set_cell (sh, col - 1, r, row->name);

		  /* Coefficients. */
		  for (n = 0; n < ctxt->n_cols; n++)
			  /* Write only non-zero coefficents in order to save
			   * memory, and, in addition, to speed up the loading.
			   */
#ifndef MPS_WRITE_ZERO_COEFFICIENTS
			  if (ctxt->matrix[row->index][n] != 0)
#endif
				  mps_set_cell_float
					  (sh, col + n % MAX_COL,
					   r + n / MAX_COL,
					   ctxt->matrix[row->index][n]);

		  /* Add Type field. */
		  mps_set_cell (sh, ecol + 2, r, type_str[(int) row->type]);


		  /* Add RHS field (zero). */
		  mps_set_cell_float (sh, ecol + 3, r, 0);


		  /* Add LHS field using SUMPRODUCT function. */
		  range_init (&range, col, r, ctxt->n_cols, r);
		  cell = sheet_cell_fetch (sh, ecol + 1, r);
		  texpr = gnm_expr_top_new
			  (gnm_expr_new_funcall2
			   (gnm_func_lookup ("SUMPRODUCT", NULL),
			    gnm_expr_new_constant
			    (value_new_cellrange_r (NULL, &v_range)),
			    gnm_expr_new_constant
			    (value_new_cellrange_r (NULL, &range))));
		  gnm_cell_set_expr (cell, texpr);
		  gnm_expr_top_unref (texpr);
		  cell_queue_recalc (cell);

		  /* Add Slack calculation */
		  gnm_cellref_init (&ref1, sh, ecol + 1, r, FALSE);
		  gnm_cellref_init (&ref2, sh, ecol + 3, r, FALSE);
		  cell = sheet_cell_fetch (sh, ecol + 4, r);
		  texpr = gnm_expr_top_new
			  (gnm_expr_new_funcall1
			   (gnm_func_lookup ("ABS", NULL),
			    gnm_expr_new_binary
			    (gnm_expr_new_cellref (&ref1),
			     GNM_EXPR_OP_SUB,
			     gnm_expr_new_cellref (&ref2))));
		  gnm_cell_set_expr (cell, texpr);
		  gnm_expr_top_unref (texpr);
		  cell_queue_recalc (cell);

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
	          MpsRhs *rhs = current->data;

		  mps_set_cell_float (sh, ecol + 3,
				      r + rhs->row->index * n_rows_per_fn,
				      rhs->value);
		  current = current->next;
	}

	/* Write the objective fn. */
	range_init (&range, VARIABLE_COL,
		    VARIABLE_ROW + 1 + n_rows_per_fn,
		    ctxt->n_cols,
		    VARIABLE_ROW + 1 + n_rows_per_fn);
	texpr = gnm_expr_top_new
		(gnm_expr_new_funcall2
		 (gnm_func_lookup ("SUMPRODUCT", NULL),
		  gnm_expr_new_constant
		  (value_new_cellrange_r (NULL, &v_range)),
		  gnm_expr_new_constant
		  (value_new_cellrange_r (NULL, &range))));
	cell = sheet_cell_fetch (sh, OBJECTIVE_VALUE_COL, MAIN_INFO_ROW);
	gnm_cell_set_expr (cell, texpr);
	gnm_expr_top_unref (texpr);
	cell_queue_recalc (cell);

	/* Store the input cell range for the Solver dialog. */
	range_init (&range, VARIABLE_COL, VARIABLE_ROW,
		    MAX_COL, VARIABLE_ROW + n_rows_per_fn - 1);
	param->input_entry_str = g_strdup (range_as_string (&range));
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
			GString *buf = g_string_new (NULL);
			g_string_append_printf (buf, "%s (R[%d])",
					   ctxt->objective_row->name, i+1);
			mps_set_cell (sh, VARIABLE_COL - 1,
				      VARIABLE_ROW + 1 + i + n_rows_per_fn,
				      buf->str);
			g_string_free (buf, TRUE);
		}
	}

	param->target_cell = sheet_cell_fetch (sh, OBJECTIVE_VALUE_COL,
					       MAIN_INFO_ROW);
	param->problem_type = SolverMinimize;

	/* Write the name of the program. */
	if (ctxt->name != NULL)
		mps_set_cell (sh, MAIN_INFO_COL, MAIN_INFO_ROW, ctxt->name);


	/* Autofit column A */
	i = sheet_col_size_fit_pixels (sh, 0, 0, gnm_sheet_get_max_rows (ctxt->sheet) - 1, FALSE);
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

	ctxt->input	     = (GsfInputTextline *)gsf_input_textline_new (input);
	ctxt->line_no        = 1;
	ctxt->line           = NULL;
	ctxt->sheet          = workbook_sheet_add (wb, -1);

	ctxt->name           = NULL;
	ctxt->rows           = NULL;
	ctxt->cols           = NULL;
	ctxt->rhs            = NULL;
	ctxt->bounds         = NULL;
        ctxt->row_hash	     = g_hash_table_new (g_str_hash, g_str_equal);
        ctxt->col_hash	     = g_hash_table_new (g_str_hash, g_str_equal);
        ctxt->col_name_tbl   = NULL;
	ctxt->objective_row  = NULL;
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

	g_free (ctxt->col_name_tbl);
	ctxt->col_name_tbl = NULL;

	g_free (ctxt->matrix);
	ctxt->matrix = NULL;

	g_free (ctxt->name);
	g_object_unref (G_OBJECT (ctxt->input)); ctxt->input = NULL;
	g_free (ctxt);
}



/*---------------------------------------------------------------------*/

/*
 * The public plug-in API.
 */

void
mps_file_open (GOFileOpener const *fo, IOContext *io_context,
               WorkbookView *wbv, GsfInput *input)
{
        MpsInputContext *ctxt;

	ctxt = mps_input_context_new (io_context, wb_view_get_workbook (wbv),
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
	} else if (!gnumeric_io_error_occurred (io_context))
		gnumeric_io_error_unknown (io_context);
}
