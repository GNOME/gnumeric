/*
 * mps.c: MPS file importer.
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@cs.helsinki.fi>
 *   File handling code copied from Dif module.
 *
 *      MPS importer module.  MPS format is a de facto standard ASCII format
 *      among most of the commercial LP solvers.
 *
 *      This implementation does not yet support ranges and bounds but is
 *      already quite suitable for testing the solving algorithms.
 *      See, for example, the Netlib collection of LP problems in MPS format
 *      (ftp://netlib2.cs.utk.edu/lp/data).
 */
#include <config.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "portability.h"
#include "gnumeric.h"
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
#include "sheet-style.h"


GNUMERIC_MODULE_PLUGIN_INFO_DECL;

#define N_INPUT_LINES_BETWEEN_UPDATES   50


/*
 * Data types
 */

/* MPS Row type (E, L, G, or N) */
typedef enum {
        EqualityRow, LessOrEqualRow, GreaterOrEqualRow, ObjectiveRow
} MpsRowType;

/* MPS Row */
typedef struct {
        MpsRowType type;
        gchar      *name;
        gint       index;
} MpsRow;

/* MPS Column */
typedef struct {
        gchar      *name;
        MpsRow     *row;
        gnum_float value;
} MpsCol;

/* MPS Range */
typedef struct {
        gchar      *name;
        MpsRow     *row;
        gnum_float value;
} MpsRange;

/* MPS Bound type (LO, UP, FX, FR, MI, BV, LI, or UI) */
typedef enum {
        LowerBound, UpperBound, FixedVariable, FreeVariable, LowerBoundInf,
	  BinaryVariable, LowerBoundInt, UpperBoundInt
} MpsBoundType;

/* MPS Bound */
typedef struct {
        char         *name;
        MpsCol       *column;
        gnum_float   value;
        MpsBoundType type;
} MpsBound;

/* MPS RHS */
typedef struct {
        gchar      *name;
        MpsRow     *row;
        gnum_float value;
} MpsRhs;

/* Column mapping */
typedef struct {
        gchar *name;
        gint  index;
} MpsColInfo;


/* Input context */
typedef struct {
        IOContext *io_context;

        gint   data_size;
        gchar *data, *cur;

        gint   line_no;
        gchar *line;
        gint   line_len, alloc_line_len;

        Sheet  *sheet;

        gchar      *name;
        GSList     *rows;
        GSList     *cols;
        GSList     *rhs;
        gint       n_rows, n_cols;
        GHashTable *row_hash;
        GHashTable *col_hash;
        gchar      **col_name_tbl;
        MpsRow     *objective_row;
        gnum_float **matrix;
} MpsInputContext;


/*
 * Constants
 */

static const int MAIN_INFO_ROW       = 1;
static const int MAIN_INFO_COL       = 0;
static const int OBJECTIVE_VALUE_COL = 1;

static const int VARIABLE_COL        = 1;
static const int VARIABLE_ROW        = 5;

static const int CONSTRAINT_COL      = 1;
static const int CONSTRAINT_ROW      = 10;

/* Error margin in the equiality comparison */
static const gchar *BINDING_LIMIT = "0.00000001";


/*************************************************************************
 *
 * Public Interface of the module
 */

/* Reads the MPS file in and creates a spreadsheet model of it. */
void
mps_file_open (GnumFileOpener const *fo, IOContext *io_context,
               WorkbookView *wbv, char const *file_name);

/*************************************************************************/

static gboolean
get_token (char **line, char *dst, int maxlen)
{
	int i;

	while (isspace ((unsigned char)**line))
		(*line)++;

	for (i = 0; !isspace ((unsigned char)**line); i++) {
		if (i == maxlen)
			return FALSE;  /* name too long */
		dst[i] = **line;
		(*line)++;
	}
	dst[i] = 0;
	return TRUE;
}


/* Writes a string into a cell. */
static void
mps_set_cell (Sheet *sh, int col, int row, const gchar *str)
{
        Cell *cell = sheet_cell_fetch (sh, col, row);
        sheet_cell_set_value (cell, value_new_string (str), NULL);
}

/* Writes a float into a cell. */
static void
mps_set_cell_float (Sheet *sh, int col, int row, const gnum_float f)
{
        Cell *cell = sheet_cell_fetch (sh, col, row);
        sheet_cell_set_value (cell, value_new_float (f), NULL);
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
mps_prepare (MpsInputContext *ctxt)
{
        gint i, n;
        GSList *current;

        ctxt->rows = g_slist_reverse (ctxt->rows);
	ctxt->cols = g_slist_reverse (ctxt->cols);

	ctxt->col_name_tbl = g_new (gchar *, ctxt->n_cols);
	g_hash_table_foreach (ctxt->col_hash, put_into_index, (gpointer) ctxt);

	ctxt->matrix = g_new (gnum_float *, ctxt->n_rows);
	for (i = 0; i < ctxt->n_rows; i++) {
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
}


/* Creates the spreadsheet model. */
static void
mps_create_sheet (WorkbookView *wbv, MpsInputContext *ctxt)
{
        MStyle *mstyle;
        Sheet  *sh = wbv->current_sheet;
	GString *var_range = g_string_new ("");
	GString *buf;
	Range  range;
	MpsRow *row;
	GSList *current;
	gint   i, n;
	static gchar  *type_str[] = { "=", "<=", ">=" };
	Cell *cell;
	SolverParameters *param = &sh->solver_parameters;

	mps_prepare (ctxt);

	/* Initialize var_range to contain the range name of the
	 * objective function variables. */
	range_init (&range, VARIABLE_COL, VARIABLE_ROW, ctxt->n_cols,
		    VARIABLE_ROW);
	g_string_sprintfa (var_range, "%s", range_name (&range));

	/*
	 * Add main program information into the sheet.
	 */

	/* Print 'Program Name'. */
	mps_set_cell (sh, MAIN_INFO_COL, MAIN_INFO_ROW - 1,
		      "Program Name");
	mps_set_cell (sh, MAIN_INFO_COL, MAIN_INFO_ROW, ctxt->name);

	/* Print 'Objective value'. */
	mps_set_cell (sh, MAIN_INFO_COL + 1, MAIN_INFO_ROW - 1,
		      "Objective Value");
	range_init (&range, VARIABLE_COL, VARIABLE_ROW + 1,
		    ctxt->n_cols, VARIABLE_ROW + 1);
	buf = g_string_new ("");
	g_string_sprintfa (buf, "=SUMPRODUCT(%s,%s)", var_range->str,
			   range_name (&range));
	cell = sheet_cell_fetch (sh, OBJECTIVE_VALUE_COL, MAIN_INFO_ROW);
	cell_set_text (cell, buf->str);
	g_string_free (buf, FALSE);

	/* Print 'Status'. */
	mps_set_cell (sh, MAIN_INFO_COL + 3, MAIN_INFO_ROW - 1, "Feasible");
	range_init (&range, ctxt->n_cols + 5, CONSTRAINT_ROW,
		    ctxt->n_cols + 5, CONSTRAINT_ROW + ctxt->n_rows - 2);
	buf = g_string_new ("=IF(COUNTIF(");
	g_string_sprintfa (buf, "%s,\"No\")>0,\"No\",\"Yes\")",
			   range_name (&range));
	cell = sheet_cell_fetch (sh, MAIN_INFO_COL + 3, MAIN_INFO_ROW);
	cell_set_text (cell, buf->str);
	g_string_free (buf, FALSE);

	/*
	 * Add objective function stuff into the sheet.
	 */

	/* Print 'Objective function:' */
	mps_set_cell (sh, VARIABLE_COL, VARIABLE_ROW - 2, "Objective function:");

	/* Print 'Current values' */
	mps_set_cell (sh, VARIABLE_COL - 1, VARIABLE_ROW, "Current values");

	/* Print the name of the objective function */
	mps_set_cell (sh, VARIABLE_COL - 1, VARIABLE_ROW + 1,
		      ctxt->objective_row->name);

	/* Print the column names, initialize the variables to 0, and
	 * print the coefficients of the objective function. */
	for (i = 0; i < ctxt->n_cols; i++) {
	          mps_set_cell (sh, i + VARIABLE_COL, VARIABLE_ROW - 1,
				ctxt->col_name_tbl[i]);
		  mps_set_cell_float (sh, i + VARIABLE_COL, VARIABLE_ROW, 0.0);
		  mps_set_cell_float (sh, i + VARIABLE_COL, VARIABLE_ROW + 1,
		           ctxt->matrix[ctxt->objective_row->index][i]);
	}

	/*
	 * Add constraints into the sheet.
	 */

	/* Print 'Constraints:'. */
	mps_set_cell (sh, CONSTRAINT_COL, CONSTRAINT_ROW - 2, "Constraints:");

	/* Print constraint titles. */
	mps_set_cell (sh, CONSTRAINT_COL - 1, CONSTRAINT_ROW - 1, "Name");
	for (i = 0; i < ctxt->n_cols; i++)
	          mps_set_cell (sh, i + CONSTRAINT_COL, CONSTRAINT_ROW - 1,
				ctxt->col_name_tbl[i]);
	mps_set_cell (sh, ctxt->n_cols + 1, CONSTRAINT_ROW - 1, "Value");
	mps_set_cell (sh, ctxt->n_cols + 2, CONSTRAINT_ROW - 1, "Type");
	mps_set_cell (sh, ctxt->n_cols + 3, CONSTRAINT_ROW - 1, "RHS");
	mps_set_cell (sh, ctxt->n_cols + 4, CONSTRAINT_ROW - 1, "Slack");
	mps_set_cell (sh, ctxt->n_cols + 5, CONSTRAINT_ROW - 1, "Status");

	/* Print constraints. */
	i = 0;
	param->constraints = NULL;
	for (current = ctxt->rows; current != NULL; current = current->next) {
	          SolverConstraint *c;

		  row = (MpsRow *) current->data;
		  if (row->type == ObjectiveRow)
		          continue;

		  /* Add row name */
		  mps_set_cell (sh, CONSTRAINT_COL - 1, i + CONSTRAINT_ROW,
				row->name);

		  /* Add Type field */
		  mps_set_cell (sh, ctxt->n_cols + 2, i + CONSTRAINT_ROW,
				type_str[(int) row->type]);

		  /* Add row calculation using SUMPRODUCT function */
		  range_init (&range, CONSTRAINT_COL, i + CONSTRAINT_ROW,
			      ctxt->n_cols, i + CONSTRAINT_ROW);

		  buf = g_string_new ("");
		  g_string_sprintfa (buf, "=SUMPRODUCT(%s,%s)",
				     var_range->str, range_name (&range));

		  cell = sheet_cell_fetch (sh, ctxt->n_cols + 1,
					   i + CONSTRAINT_ROW);
		  cell_set_text (cell, buf->str);
		  g_string_free (buf, FALSE);

		  /* Add Slack calculation */
		  buf = g_string_new ("");
		  if (row->type == LessOrEqualRow) {
		          g_string_sprintfa (buf, "=%s-",
					     cell_coord_name (ctxt->n_cols + 3,
							      i + CONSTRAINT_ROW));
			  g_string_sprintfa (buf, "%s",
					     cell_coord_name (ctxt->n_cols + 1,
							      i + CONSTRAINT_ROW));
		  } else if (row->type == GreaterOrEqualRow) {
		          g_string_sprintfa (buf, "=%s-",
					     cell_coord_name (ctxt->n_cols + 1,
							      i + CONSTRAINT_ROW));
			  g_string_sprintfa (buf, "%s",
					     cell_coord_name (ctxt->n_cols + 3,
							      i + CONSTRAINT_ROW));
		  } else {
		          g_string_sprintfa (buf, "=ABS(%s-",
					     cell_coord_name (ctxt->n_cols + 1,
							      i + CONSTRAINT_ROW));
			  g_string_sprintfa (buf, "%s",
					     cell_coord_name (ctxt->n_cols + 3,
							      i + CONSTRAINT_ROW));
			  g_string_sprintfa (buf, ")");
		  }
		  cell = sheet_cell_fetch (sh, ctxt->n_cols + 4,
					   i + CONSTRAINT_ROW);
		  cell_set_text (cell, buf->str);
		  g_string_free (buf, FALSE);

		  /* Add Status field */
		  buf = g_string_new ("");
		  if (row->type == EqualityRow) {
		          g_string_sprintfa (buf,
					     "=IF(%s>%s,\"NOK\", \"Binding\")",
					     cell_coord_name (ctxt->n_cols + 4,
							      i + CONSTRAINT_ROW),
					     BINDING_LIMIT);
		  } else {
		          g_string_sprintfa (buf,
					     "=IF(%s<0,\"NOK\", ",
					     cell_coord_name (ctxt->n_cols + 4,
							      i + CONSTRAINT_ROW));
			  g_string_sprintfa (buf,
					     "IF(%s<=%s,\"Binding\","
					     "\"Not Binding\"))",
					     cell_coord_name (ctxt->n_cols + 4,
							      i + CONSTRAINT_ROW),
					     BINDING_LIMIT);
		  }
		  cell = sheet_cell_fetch (sh, ctxt->n_cols + 5,
					   i + CONSTRAINT_ROW);
		  cell_set_text (cell, buf->str);
		  g_string_free (buf, FALSE);

		  for (n = 0; n < ctxt->n_cols; n++)
		          mps_set_cell_float (sh, n + 1, i + CONSTRAINT_ROW,
					      ctxt->matrix[row->index][n]);
		  /* Initialize RHS to 0 */
		  mps_set_cell_float (sh, n + 3, i + CONSTRAINT_ROW, 0.0);

		  /* Add Solver constraint */
		  c = g_new (SolverConstraint, 1);
		  c->lhs.col = ctxt->n_cols + 1;
		  c->lhs.row = i + CONSTRAINT_ROW;
		  c->rhs.col = ctxt->n_cols + 3;
		  c->rhs.row = i + CONSTRAINT_ROW;
		  c->type = type_str[row->type];
		  c->cols = 1;
		  c->rows = 1;
		  c->str = write_constraint_str (c->lhs.col, c->lhs.row,
						 c->rhs.col, c->rhs.row,
						 c->type, c->cols,
						 c->rows);

		  param->constraints = g_slist_append (param->constraints, c);
		  i++;
	}

	current = ctxt->rhs;
	while (current != NULL) {
	          MpsRhs *rhs = (MpsRhs *) current->data;

		  mps_set_cell_float (sh, ctxt->n_cols + 3,
				      rhs->row->index + CONSTRAINT_ROW,
				      rhs->value);
		  current = current->next;
	}

	param->target_cell = sheet_cell_fetch (sh, OBJECTIVE_VALUE_COL,
					       MAIN_INFO_ROW);
	param->problem_type = 0;
	param->input_entry_str = g_strdup (var_range->str);
	g_string_free (var_range, FALSE);

	/* Set the Objective function names Italic */
	mstyle = mstyle_new ();
	range_init (&range, VARIABLE_COL - 1, VARIABLE_ROW,
		    VARIABLE_COL - 1, VARIABLE_ROW + 1);
	mstyle_set_font_italic (mstyle, TRUE);
	sheet_style_apply_range (sh, &range, mstyle);

	/* Set the row names Italic */
	mstyle = mstyle_new ();
	range_init (&range, CONSTRAINT_COL - 1, CONSTRAINT_ROW,
		    CONSTRAINT_COL - 1, CONSTRAINT_ROW + ctxt->n_rows);
	mstyle_set_font_italic (mstyle, TRUE);
	sheet_style_apply_range (sh, &range, mstyle);

	/* Set the row status fields Italic */
	mstyle = mstyle_new ();
	range_init (&range, ctxt->n_cols + 5, CONSTRAINT_ROW,
		    ctxt->n_cols + 5, CONSTRAINT_ROW + ctxt->n_rows);
	mstyle_set_font_italic (mstyle, TRUE);
	sheet_style_apply_range (sh, &range, mstyle);

	/* Set a few titles Italic */
	mstyle = mstyle_new ();
	range_init (&range, VARIABLE_COL, VARIABLE_ROW - 2,
		    VARIABLE_COL, VARIABLE_ROW - 2);
	mstyle_set_font_italic (mstyle, TRUE);
	sheet_style_apply_range (sh, &range, mstyle);
	mstyle = mstyle_new ();
	range_init (&range, CONSTRAINT_COL, CONSTRAINT_ROW - 2,
		    CONSTRAINT_COL, CONSTRAINT_ROW - 2);
	mstyle_set_font_italic (mstyle, TRUE);
	sheet_style_apply_range (sh, &range, mstyle);

	/* Set the main info row Bold + Underlined */
	mstyle = mstyle_new ();
	range_init (&range, MAIN_INFO_COL, MAIN_INFO_ROW - 1,
		    MAIN_INFO_COL + 3, MAIN_INFO_ROW - 1);
	mstyle_set_font_bold (mstyle, TRUE);
	mstyle_set_font_uline (mstyle, TRUE);
	sheet_style_apply_range (sh, &range, mstyle);

	/* Set the variable names row Bold + Underlined */
	mstyle = mstyle_new ();
	range_init (&range, VARIABLE_COL, VARIABLE_ROW - 1,
		    VARIABLE_COL + ctxt->n_cols, VARIABLE_ROW - 1);
	mstyle_set_font_bold (mstyle, TRUE);
	mstyle_set_font_uline (mstyle, TRUE);
	sheet_style_apply_range (sh, &range, mstyle);

	/* Set the constraint titles row Bold + Underlined */
	mstyle = mstyle_new ();
	range_init (&range, CONSTRAINT_COL - 1, CONSTRAINT_ROW - 1,
		    ctxt->n_cols + 5, CONSTRAINT_ROW - 1);
	mstyle_set_font_bold (mstyle, TRUE);
	mstyle_set_font_uline (mstyle, TRUE);
	sheet_style_apply_range (sh, &range, mstyle);

	/* Autofit column A */
	i = sheet_col_size_fit_pixels (sh, 0);
	if (i == 0)
	          return;
	sheet_col_set_size_pixels (sh, 0, i, TRUE);
	sheet_recompute_spans_for_col (sh, 0);
}


/* Make the initializations. */
static MpsInputContext *
mps_input_context_new (IOContext *io_context, Workbook *wb, char const *file_name){
        MpsInputContext *ctxt = NULL;
	gint size;
	char *data;
	ErrorInfo *mmap_error;

	data = gnumeric_mmap_error_info (file_name, &size, &mmap_error);
	if (mmap_error != NULL) {
	        gnumeric_io_error_info_set (io_context, mmap_error);
		return NULL;
	}

	ctxt = g_new (MpsInputContext, 1);
	ctxt->io_context     = io_context;
	ctxt->data_size      = size;
	ctxt->data           = data;
	ctxt->cur            = data;
	ctxt->line_no        = 1;
	ctxt->line           = g_malloc (1);
	ctxt->line_len       = 0;
	ctxt->alloc_line_len = 0;
	ctxt->sheet          = workbook_sheet_add (wb, NULL, FALSE);

	ctxt->name           = NULL;
	ctxt->rows           = NULL;
	ctxt->cols           = NULL;
	ctxt->rhs            = NULL;

	ctxt->n_rows = ctxt->n_cols = 0;

	g_slist_free (ctxt->rows);

	io_progress_message (io_context, _("Reading file..."));
	memory_io_progress_set (io_context, ctxt->data, ctxt->data_size);

	return ctxt;
}

static gboolean
rh_rm_cb (gpointer key, gpointer value, gpointer user_data)
{
        return TRUE;
}

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
	munmap (ctxt->data, ctxt->data_size);

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

	g_slist_free (ctxt->rows);
	g_slist_free (ctxt->cols);
	g_slist_free (ctxt->rhs);

	g_hash_table_foreach_remove (ctxt->row_hash, (GHRFunc) rh_rm_cb, NULL);
	g_hash_table_foreach_remove (ctxt->col_hash, (GHRFunc) ch_rm_cb, NULL);
	g_hash_table_destroy (ctxt->row_hash);
	g_hash_table_destroy (ctxt->col_hash);

	g_free (ctxt->col_name_tbl);
	g_free (ctxt->matrix);
	g_free (ctxt->line);
	g_free (ctxt->name);
	g_free (ctxt);
}


/* Read a line from the file. */
static gboolean
mps_get_line (MpsInputContext *ctxt)
{
        gchar *p, *p_limit;

	p_limit = ctxt->data + ctxt->data_size;
	if (ctxt->cur >= p_limit) {
	        ctxt->line[0] = '\0';
		ctxt->line_len = 0;
		return FALSE;
	}

	for (p = ctxt->cur; p < p_limit && p[0] != '\n' && p[0] != '\r'; p++);

	ctxt->line_len = p - ctxt->cur;
	if (ctxt->line_len > ctxt->alloc_line_len) {
	        g_free (ctxt->line);
		ctxt->alloc_line_len = MAX (ctxt->alloc_line_len * 2,
					    ctxt->line_len);
		ctxt->line = g_malloc (ctxt->alloc_line_len + 1);
	}
	if (ctxt->line_len > 0) {
	        memcpy (ctxt->line, ctxt->cur, ctxt->line_len);
	}
	ctxt->line[ctxt->line_len] = '\0';

	if (p == p_limit || (p == p_limit - 1 && (p[0] == '\n' ||
						  p[0] == '\r'))) {
	        ctxt->cur = p_limit;
	} else if ((p[0] == '\n' && p[1] == '\r') || (p[0] == '\r' &&
						      p[1] == '\n')) {
	        ctxt->cur = p + 2;
	} else {
	        ctxt->cur = p + 1;
	}

	if ((++ctxt->line_no % N_INPUT_LINES_BETWEEN_UPDATES) == 0) {
	        memory_io_progress_update (ctxt->io_context, ctxt->cur);
	}

	return TRUE;
}


/* Add one ROW definition. */
static gboolean
mps_add_row (MpsInputContext *ctxt, MpsRowType type, gchar *txt)
{
        MpsRow *row;
	int    size;

        while (isspace ((unsigned char)*txt))
	          txt++;

	row = g_new (MpsRow, 1);
	size = ctxt->line_len - (txt - ctxt->line);

	if (size == 0)
	          return FALSE;

	row->name = strcpy (g_malloc (size + 1), txt);
	row->type = type;
	row->index = ctxt->n_rows;
	ctxt->n_rows += 1;

	ctxt->rows = g_slist_prepend (ctxt->rows, row);

	if (type == ObjectiveRow)
	          ctxt->objective_row = row;

	return TRUE;
}

/* Add one COLUMN definition. */
static gboolean
mps_add_column (MpsInputContext *ctxt, gchar *row_name, gchar *col_name,
		const gchar *value_str)
{
        MpsCol *col;
	MpsRow *row = (MpsRow *) g_hash_table_lookup (ctxt->row_hash, row_name);
	MpsColInfo *i;

	if (row == NULL)
	          return FALSE;
	col = g_new (MpsCol, 1);
	col->row = row;
	col->name = g_strdup (col_name);
	col->value = atof (value_str);
	ctxt->cols = g_slist_prepend (ctxt->cols, col);

	i = (MpsColInfo *) g_hash_table_lookup (ctxt->col_hash, col_name);
	if (i == NULL) {
	          i = g_new (MpsColInfo, 1);
		  i->index = ctxt->n_cols;
		  i->name = g_strdup (col_name);
		  ctxt->n_cols += 1;
		  g_hash_table_insert (ctxt->col_hash, col->name, (gpointer) i);
	}

	return TRUE;
}


/* Add one RHS definition. */
static gboolean
mps_add_rhs (MpsInputContext *ctxt, gchar *rhs_name, gchar *row_name,
	     const gchar *value_str)
{
        MpsRhs *rhs;

	rhs = g_new (MpsRhs, 1);
	rhs->name = g_strdup (rhs_name);
	rhs->row = (MpsRow *) g_hash_table_lookup (ctxt->row_hash, row_name);
	if (rhs->row == NULL)
	          return FALSE;
	rhs->value = atof (value_str);
	ctxt->rhs = g_slist_prepend (ctxt->rhs, rhs);

	return TRUE;
}

/* NAME section parsing.
 * Returns FALSE on error.
 */
static gboolean
mps_parse_name (MpsInputContext *ctxt)
{
        while (1) {
	        gchar *line;

		if (!mps_get_line (ctxt))
		        return FALSE;

		if (strncmp (ctxt->line, "NAME", 4) == 0
		    && isspace ((unsigned char)ctxt->line[4])) {
		        line = ctxt->line + 5;
			while (isspace ((unsigned char)*line))
			        line++;

			ctxt->name = strcpy (g_malloc (ctxt->line_len -
						       (line-ctxt->line) + 1),
					     line);
			break;
		} else
		        return FALSE;
	}

	return TRUE;
}


/* ROWS section parsing.
 * Returns FALSE on error.
 */
static gboolean
mps_parse_rows (MpsInputContext *ctxt)
{
        gchar  *line;
	GSList *tmp;

	while (1) {
	        if (!mps_get_line (ctxt))
		        return FALSE;

		if (strncmp (ctxt->line, "ROWS", 4) == 0)
		        break;
		else
		        return FALSE;
	}

	while (1) {
	        if (!mps_get_line (ctxt))
		        return FALSE;

		if (isspace ((unsigned char)*ctxt->line)) {
		        line = ctxt->line + 1;
			if (*line == 'E') {
			        if (!mps_add_row (ctxt, EqualityRow, line+1))
				        return FALSE;
			} else if (*line == 'L') {
			        if (!mps_add_row (ctxt, LessOrEqualRow,
						  line+1))
				  return FALSE;
			} else if (*line == 'G') {
			        if (!mps_add_row (ctxt, GreaterOrEqualRow,
						  line+1))
				  return FALSE;
			} else if (*line == 'N') {
			        if (!mps_add_row (ctxt, ObjectiveRow, line+1))
				        return FALSE;
			} else
			        return FALSE;
		} else
		        break;
	}

	ctxt->row_hash = g_hash_table_new (g_str_hash, g_str_equal);
	for (tmp = ctxt->rows; tmp != NULL; tmp = tmp->next) {
	          MpsRow *row = (MpsRow *) tmp->data;
		  g_hash_table_insert (ctxt->row_hash, row->name, (gpointer) row);
	}

	return TRUE;
}


/* COLUMNS section parsing.
 * Returns FALSE on error.
 */
static gboolean
mps_parse_columns (MpsInputContext *ctxt)
{
	ctxt->col_hash = g_hash_table_new (g_str_hash, g_str_equal);
	while (1) {
	        if (strncmp (ctxt->line, "COLUMNS", 7) == 0)
		        break;
		else
		        return FALSE;

		if (!mps_get_line (ctxt))
		        return FALSE;
	}

	while (1) {
		gchar *line;
		gchar col_name[50], row_name[50], value[100];

	        if (!mps_get_line (ctxt))
		        return FALSE;

		line = ctxt->line;

		if (!isspace ((unsigned char)*line))
			break;

		/* Column name */
		if (!get_token (&line, col_name, sizeof (col_name) - 2))
			return FALSE;

		/* Row name */
		if (!get_token (&line, row_name, sizeof (row_name) - 2))
			return FALSE;

		/* Value */
		if (!get_token (&line, value, sizeof (value) - 2))
			return FALSE;

		if (!mps_add_column (ctxt, row_name, col_name, value))
			return FALSE;

		while (*line && isspace ((unsigned char )*line))
			line++;

		/* Optional second column */
		if (*line) {
			/* Row name */
			if (!get_token (&line, row_name, sizeof (row_name) - 2))
				return FALSE;

			/* Value */
			if (!get_token (&line, value, sizeof (value) - 2))
				return FALSE;

			if (!mps_add_column (ctxt, row_name, col_name,
					     value))
				return FALSE;
		}
	}

	return TRUE;
}


/* RHS section parsing.
 * Returns FALSE on error.
 */
static gboolean
mps_parse_rhs (MpsInputContext *ctxt)
{
	while (1) {
	        if (strncmp (ctxt->line, "RHS", 3) == 0)
		        break;
		else
		        return FALSE;

		if (!mps_get_line (ctxt))
		        return FALSE;
	}

	while (1) {
		gchar *line;
		gchar rhs_name[50], row_name[50], value[100];

	        if (!mps_get_line (ctxt))
		        return FALSE;

		line = ctxt->line;

		if (!isspace ((unsigned char)*line))
			break;

		/* RHS name */
		if (!get_token (&line, rhs_name, sizeof (rhs_name) - 2))
			return FALSE;

		/* Row name */
		if (!get_token (&line, row_name, sizeof (row_name) - 2))
			return FALSE;

		/* Value */
		if (!get_token (&line, value, sizeof (value) - 2))
			return FALSE;

		if (!mps_add_rhs (ctxt, rhs_name, row_name, value))
		        return FALSE;

		while (*line && isspace ((unsigned char)*line))
		        line++;

		/* Optional second RHS definition */
		if (*line) {
			/* Row name */
			if (!get_token (&line, row_name, sizeof (row_name) - 2))
				return FALSE;

			/* Value */
			if (!get_token (&line, value, sizeof (value) - 2))
				return FALSE;

			if (!mps_add_rhs (ctxt, rhs_name, row_name, value))
			        return FALSE;
		}
	}

	return TRUE;
}


static void
mps_parse_sheet (MpsInputContext *ctxt)
{
        if (!mps_parse_name (ctxt)) {
	        gnumeric_io_error_info_set (ctxt->io_context, error_info_new_printf (
										     _("Problem name was not defined in the file.")));
	} else if (!mps_parse_rows (ctxt)) {
	        gnumeric_io_error_info_set (ctxt->io_context, error_info_new_printf (
										     _("Invalid ROWS section in the file.")));
	} else if (!mps_parse_columns (ctxt)) {
	        gnumeric_io_error_info_set (ctxt->io_context, error_info_new_printf (
										     _("Invalid COLUMNS section in the file.")));
	} else if (!mps_parse_rhs (ctxt)) {
	        gnumeric_io_error_info_set (ctxt->io_context, error_info_new_printf (
										     _("Invalid RHS section in the file.")));
	}
}

void
mps_file_open (GnumFileOpener const *fo, IOContext *io_context,
               WorkbookView *wbv, char const *file_name)
{
        MpsInputContext *ctxt;

	ctxt = mps_input_context_new (io_context, wb_view_workbook (wbv),
				      file_name);
	if (ctxt != NULL) {
	        mps_parse_sheet (ctxt);
		if (gnumeric_io_error_occurred (io_context)) {
		        gnumeric_io_error_push (io_context, error_info_new_str
						(_("Error while reading MPS file.")));
		}
		mps_create_sheet (wbv, ctxt);
		mps_input_context_destroy (ctxt);
	} else {
	        if (!gnumeric_io_error_occurred) {
		        gnumeric_io_error_unknown (io_context);
		}
	}
}
