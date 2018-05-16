/*
 * Copyright (C) 2009 Morten Welinder (terra@gnome.org)
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <goffice/goffice.h>
#include <glib/gi18n-lib.h>
#include <gsf/gsf-input-textline.h>

#include <gnm-plugin.h>
#include <gutils.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <value.h>
#include <mstyle.h>
#include <sheet-style.h>
#include <cell.h>
#include <ranges.h>
#include <expr.h>
#include <tools/gnm-solver.h>

#include <string.h>

GNM_PLUGIN_MODULE_HEADER;

typedef struct {
	char *name;
	GnmSolverConstraintType type;
	GnmExpr const *expr;
	gnm_float rhs;
	gnm_float range;
} MpsRow;

typedef struct {
	GOIOContext *io_context;

	GsfInputTextline *input;
	char *line;
	GPtrArray *split;

	GPtrArray *rows;
	GHashTable *row_hash;

	GHashTable *col_hash;

	Workbook *wb;
	Sheet *sheet;
	GnmSolverParameters *param;
} MpsState;

/* ------------------------------------------------------------------------- */

/* Vertical */
enum { CONSTR_BASE_COL = 3 };
enum { CONSTR_BASE_ROW = 8 };

/* Vertical */
enum { VAR_BASE_COL = 0 };
enum { VAR_BASE_ROW = 8 };

/* Horizontal */
enum { OBJ_BASE_COL = 0 };
enum { OBJ_BASE_ROW = 4 };


static void
mps_set_cell (MpsState *state, int col, int row, const gchar *str)
{
	GnmCell *cell = sheet_cell_fetch (state->sheet, col, row);
	gnm_cell_set_value (cell, value_new_string (str));
}

static void
mps_set_expr (MpsState *state, int col, int row, GnmExpr const *expr)
{
	GnmCell *cell = sheet_cell_fetch (state->sheet, col, row);
	GnmExprTop const *texpr = gnm_expr_top_new (expr);
	gnm_cell_set_expr (cell, texpr);
	gnm_expr_top_unref (texpr);
}

static void
mps_set_cell_float (MpsState *state, int col, int row, const gnm_float f)
{
	GnmCell *cell = sheet_cell_fetch (state->sheet, col, row);
        gnm_cell_set_value (cell, value_new_float (f));
}

static void
mps_set_style (MpsState *state, int c1, int r1, int c2, int r2,
	       gboolean italic, gboolean bold, gboolean ulined)
{
        GnmStyle *mstyle = gnm_style_new ();
	GnmRange range;

	range_init (&range, c1, r1, c2, r2);
	gnm_style_set_font_italic (mstyle, italic);
	gnm_style_set_font_bold (mstyle, bold);
	gnm_style_set_font_uline (mstyle, ulined);
	sheet_style_apply_range (state->sheet, &range, mstyle);
}

/* ------------------------------------------------------------------------- */

static gboolean
readline (MpsState *state)
{
	do {
		char *line = state->line =
			gsf_input_textline_utf8_gets (state->input);

		if (!line)
			return FALSE;
		if (line[0] == '*' || line[0] == 0)
			continue;

		return g_ascii_isspace (line[0]);
	} while (1);
}

static gboolean
splitline (MpsState *state)
{
	char *s;

	if (!readline (state))
		return FALSE;

	g_ptr_array_set_size (state->split, 0);
	s = state->line;
	do {
		while (g_ascii_isspace (*s))
			s++;
		if (!*s)
			break;
		g_ptr_array_add (state->split, s);
		while (*s && !g_ascii_isspace (*s))
			s++;
		if (!*s)
			break;
		*s++ = 0;
	} while (1);

	return TRUE;
}

static void
ignore_section (MpsState *state)
{
	while (readline (state))
		; /* Nothing */
}

/* ------------------------------------------------------------------------- */

static void
mps_mark_error (MpsState *state, const char *fmt, ...)
{
	GOErrorInfo *error;
	va_list args;

	if (go_io_error_occurred (state->io_context))
		return;

	va_start (args, fmt);
	error = go_error_info_new_vprintf (GO_ERROR, fmt, args);
	va_end (args);

	go_io_error_info_set (state->io_context, error);
}

static void
mps_parse_name (MpsState *state)
{
	const char *s;

	mps_set_cell (state, 0, 0, _("Program Name"));
	mps_set_style (state, 0, 0, 0, 0, FALSE, TRUE, FALSE);

	s = state->line + 4;
	while (g_ascii_isspace (*s))
		s++;
	if (*s) {
		mps_set_cell (state, 0, 1, s);
	}

	ignore_section (state);
}

static void
mps_parse_rows (MpsState *state)
{
	gboolean seen_objfunc = FALSE;

	g_ptr_array_add (state->rows, NULL);

	while (splitline (state)) {
		GPtrArray *split = state->split;
		const char *type;
		const char *name;
		MpsRow *row;
		gboolean is_objfunc = FALSE;

		if (split->len < 2) {
			mps_mark_error (state,
					_("Invalid line in ROWS section"));
			ignore_section (state);
			return;
		}
		type = g_ptr_array_index (split, 0);
		name = g_ptr_array_index (split, 1);

		if (g_hash_table_lookup (state->row_hash, name)) {
			mps_mark_error (state,
					_("Duplicate rows name %s"),
					name);
			ignore_section (state);
			return;
		}

		if (strcmp (type, "E") == 0) {
			row = g_new0 (MpsRow, 1);
			row->type = GNM_SOLVER_EQ;
		} else if (strcmp (type, "L") == 0) {
			row = g_new0 (MpsRow, 1);
			row->type = GNM_SOLVER_LE;
		} else if (strcmp (type, "G") == 0) {
			row = g_new0 (MpsRow, 1);
			row->type = GNM_SOLVER_GE;
		} else if (strcmp (type, "N") == 0) {
			if (seen_objfunc) {
				mps_mark_error (state,
						_("Duplicate objective row"));
				ignore_section (state);
				return;
			}
			row = g_new0 (MpsRow, 1);
			is_objfunc = TRUE;
			seen_objfunc = TRUE;
			g_ptr_array_index (state->rows, 0) = row;
		} else {
			mps_mark_error (state,
					_("Invalid row type %s"),
					type);
			ignore_section (state);
			return;
		}

		row->name = g_strdup (name);
		g_hash_table_insert (state->row_hash, row->name, row);
		if (!is_objfunc)
			g_ptr_array_add (state->rows, row);
	}

	if (!seen_objfunc) {
		mps_mark_error (state,
				_("Missing objective row"));
		return;
	}
}

static void
mps_parse_columns (MpsState *state)
{
	gboolean integer = FALSE;
	GnmCell *cell = NULL;

	while (splitline (state)) {
		GPtrArray *split = state->split;
		const char *colname;
		unsigned ui;

		if (split->len == 3 &&
		    strcmp (g_ptr_array_index (split, 1), "'MARKER'") == 0) {
			const char *marker = g_ptr_array_index (split, 2);
			if (strcmp (marker, "'INTORG'") == 0)
				integer = TRUE;
			else if (strcmp (marker, "'INTEND'") == 0)
				integer = FALSE;
			else {
				mps_mark_error (state,
						_("Invalid marker"));
			}
			continue;
		}

		if (split->len % 2 == 0) {
			colname = NULL;
			/* Re-use cell */
		} else {
			colname = g_ptr_array_index (split, 0);
			cell = g_hash_table_lookup (state->col_hash, colname);
		}

		if (!cell) {
			int x = VAR_BASE_COL;
			int y = VAR_BASE_ROW + 1 + g_hash_table_size (state->col_hash);
			cell = sheet_cell_fetch (state->sheet, x + 1, y);
			if (colname) {
				g_hash_table_insert (state->col_hash,
						     g_strdup (colname),
						     cell);
				mps_set_cell (state, x, y, colname);
			}

			if (integer) {
				MpsRow *row = g_new0 (MpsRow, 1);
				GnmCellRef cr;

				gnm_cellref_init (&cr, NULL,
						  cell->pos.col, cell->pos.row,
						  FALSE);
				row->name = g_strdup (colname);
				row->type = GNM_SOLVER_INTEGER;
				row->expr = gnm_expr_new_cellref (&cr);
				g_ptr_array_add (state->rows, row);
			}
		}

		for (ui = split->len % 2; ui < split->len; ui += 2) {
			const char *rowname = g_ptr_array_index (split, ui);
			const char *valtxt = g_ptr_array_index (split, ui + 1);
			gnm_float val = gnm_strto (valtxt, NULL);
			gboolean neg = (val < 0);
			MpsRow *row = g_hash_table_lookup (state->row_hash,
							   rowname);
			GnmCellRef cr;
			GnmExpr const *expr;

			if (!row) {
				mps_mark_error (state,
						_("Invalid row name, %s, in columns"),
						rowname);
				continue;
			}
			if (val == 0)
				continue;

			if (row->expr) {
				val = gnm_abs (val);
			}

			gnm_cellref_init (&cr, NULL,
					  cell->pos.col, cell->pos.row,
					  FALSE);
			expr = gnm_expr_new_cellref (&cr);
			if (gnm_abs (val) != 1) {
				expr = gnm_expr_new_binary
					(gnm_expr_new_constant (value_new_float (val)),
					 GNM_EXPR_OP_MULT,
					 expr);
			} else if (neg && row->expr == NULL)
				expr = gnm_expr_new_unary
					(GNM_EXPR_OP_UNARY_NEG,
					 expr);

			if (row->expr) {
				expr = gnm_expr_new_binary
					(row->expr,
					 neg ? GNM_EXPR_OP_SUB : GNM_EXPR_OP_ADD,
					 expr);
			}

			row->expr = expr;
		}
	}
}

static void
mps_parse_bounds (MpsState *state)
{
	while (splitline (state)) {
		GPtrArray *split = state->split;
		const char *bt = split->len
			? g_ptr_array_index (split, 0)
			: "?";
		GnmSolverConstraintType type;
		gboolean integer = FALSE;
		unsigned ui;

		if (strcmp (bt, "UP") == 0 || strcmp (bt, "UI") == 0) {
			type = GNM_SOLVER_LE;
			integer = (bt[1] == 'I');
		} else if (strcmp (bt, "LO") == 0 ||
			   strcmp (bt, "LI") == 0) {
			type = GNM_SOLVER_GE;
			integer = (bt[1] == 'I');
		} else if (strcmp (bt, "FX") == 0)
			type = GNM_SOLVER_EQ;
		else if (strcmp (bt, "FR") == 0 ||
			 strcmp (bt, "PL") == 0 ||
			 strcmp (bt, "MI") == 0)
			continue;
		else if (strcmp (bt, "BV") == 0) {
			type = GNM_SOLVER_BOOLEAN;
			integer = TRUE;
		} else {
			mps_mark_error (state,
					_("Invalid bounds type %s"),
					bt);
			continue;
		}

		for (ui = 2 - split->len % 2; ui < split->len; ui += 2) {
			const char *colname = g_ptr_array_index (split, ui);
			MpsRow *row;
			GnmCell *cell = g_hash_table_lookup (state->col_hash,
							     colname);
			const char *valtxt = g_ptr_array_index (split, ui + 1);
			gnm_float val = gnm_strto (valtxt, NULL);
			GnmCellRef cr;

			if (!cell) {
				mps_mark_error (state,
						_("Invalid column name, %s, in bounds"),
						colname);
				continue;
			}

			gnm_cellref_init (&cr, NULL,
					  cell->pos.col, cell->pos.row,
					  FALSE);

			row = g_new0 (MpsRow, 1);
			row->name = g_strdup (colname);
			row->type = type;
			row->rhs = val;
			row->expr = gnm_expr_new_cellref (&cr);
			g_ptr_array_add (state->rows, row);

			if (integer) {
				row = g_new0 (MpsRow, 1);
				row->name = g_strdup (colname);
				row->type = GNM_SOLVER_INTEGER;
				row->expr = gnm_expr_new_cellref (&cr);
				g_ptr_array_add (state->rows, row);
			}
		}
	}
}

static void
mps_parse_rhs (MpsState *state, gboolean is_rhs)
{
	while (splitline (state)) {
		GPtrArray *split = state->split;
		unsigned ui;

		/* The name column is optional.  */
		for (ui = split->len % 2; ui < split->len; ui += 2) {
			const char *rowname = g_ptr_array_index (split, ui);
			const char *valtxt = g_ptr_array_index (split, ui + 1);
			gnm_float val = gnm_strto (valtxt, NULL);
			MpsRow *row = g_hash_table_lookup (state->row_hash,
							   rowname);

			if (!row) {
				mps_mark_error (state,
						_("Invalid row name, %s, in rhs/ranges section"),
						rowname);
				continue;
			}

			if (is_rhs)
				row->rhs += val;
			else
				row->range += val;
		}
	}
}

static void
mps_parse_file (MpsState *state)
{
	gboolean done = FALSE;
	readline (state);

	while (!done) {
		char *line = state->line;
		char *section;
		unsigned ui;

		if (!line) {
			/* Ignore missing end marker.  */
			break;
		}

		ui = 0;
		while (g_ascii_isalnum (line[ui]))
			ui++;
		section = g_strndup (line, ui);

		if (strcmp (section, "ENDATA") == 0)
			done = TRUE;
		else if (strcmp (section, "NAME") == 0)
			mps_parse_name (state);
		else if (strcmp (section, "ROWS") == 0)
			mps_parse_rows (state);
		else if (strcmp (section, "COLUMNS") == 0)
			mps_parse_columns (state);
		else if (strcmp (section, "BOUNDS") == 0)
			mps_parse_bounds (state);
		else if (strcmp (section, "RHS") == 0)
			mps_parse_rhs (state, TRUE);
		else if (strcmp (section, "RANGES") == 0)
			mps_parse_rhs (state, FALSE);
		else {
			g_warning ("Invalid section %s\n", section);
			ignore_section (state);
		}
		g_free (section);
	}
}

/* ------------------------------------------------------------------------- */

static void
make_constraint (MpsState *state, int x, int y, MpsRow *row,
		 GnmSolverConstraintType type, gnm_float rhs)
{
	GnmSolverParameters *param = state->param;
	GnmSolverConstraint *c = gnm_solver_constraint_new (state->sheet);
	GnmRange r;
	const char * const type_str[] =	{
		"\xe2\x89\xa4" /* "<=" */,
		"\xe2\x89\xa5" /* ">=" */,
		"=", "Int", "Bool"
	};

	c->type = type;
	if (gnm_solver_constraint_has_rhs (c)) {
		range_init (&r, x + 1, y, x + 1, y);
		gnm_solver_constraint_set_lhs
			(c,
			 value_new_cellrange_r (NULL, &r));
		range_init (&r, x + 3, y, x + 3, y);
		gnm_solver_constraint_set_rhs
			(c,
			 value_new_cellrange_r (NULL, &r));

		mps_set_cell_float (state, x + 3, y, rhs);
	} else {
		/* Refer directly to the variable.  */
		gnm_solver_constraint_set_lhs
			(c,
			 gnm_expr_get_range (row->expr));
	}

	if (row->name)
		mps_set_cell (state, x, y, row->name);
	if (row->expr) {
		GnmCellRef cr;
		mps_set_expr (state, x + 1, y, row->expr);
		gnm_cellref_init (&cr, NULL, 0, -1, TRUE);
		row->expr = gnm_expr_new_cellref (&cr);
	} else
		mps_set_cell_float (state, x + 1, y, 0);

	mps_set_cell (state, x + 2, y, type_str[type]);

	param->constraints = g_slist_append (param->constraints, c);
}



static void
mps_fill_sheet (MpsState *state)
{
	unsigned ui;
	GnmSolverParameters *param = state->param;
	int x = CONSTR_BASE_COL;
	int y = CONSTR_BASE_ROW;

	/* ---------------------------------------- */

	mps_set_cell (state, x, y,  _("Constraint"));
	mps_set_cell (state, x + 1, y, _("Value"));
	mps_set_cell (state, x + 2, y, _("Type"));
	mps_set_cell (state, x + 3, y, _("Limit"));
	mps_set_style (state, x, y, x + 3, y, FALSE, TRUE, FALSE);

	/* Zeroth row is objective function.  */
	for (ui = 1; ui < state->rows->len; ui++) {
		MpsRow *row = g_ptr_array_index (state->rows, ui);

		y++;

		switch (row->type) {
		case GNM_SOLVER_LE:
			if (row->range != 0)
				make_constraint (state, x, y++, row,
						 GNM_SOLVER_GE,
						 row->rhs - gnm_abs (row->range));
			make_constraint (state, x, y, row, row->type, row->rhs);
			break;
		case GNM_SOLVER_GE:
			make_constraint (state, x, y, row, row->type, row->rhs);
			if (row->range != 0)
				make_constraint (state, x, ++y, row,
						 GNM_SOLVER_LE,
						 row->rhs + gnm_abs (row->range));
			break;
		case GNM_SOLVER_EQ:
			if (row->range == 0)
				make_constraint (state, x, y, row,
						 row->type, row->rhs);
			else if (row->range > 0) {
				make_constraint (state, x, y, row,
						 GNM_SOLVER_GE, row->rhs);
				make_constraint (state, x, y, row,
						 GNM_SOLVER_LE,
						 row->rhs + gnm_abs (row->range));
			} else {
				make_constraint (state, x, y, row,
						 GNM_SOLVER_GE,
						 row->rhs - gnm_abs (row->range));
				make_constraint (state, x, y, row,
						 GNM_SOLVER_LE, row->rhs);
			}
			break;
		case GNM_SOLVER_INTEGER:
		case GNM_SOLVER_BOOLEAN:
			make_constraint (state, x, y, row, row->type, 0);
			break;
		default:
			g_assert_not_reached ();
		}
	}

	/* ---------------------------------------- */

	{
		GnmRange r;
		GnmValue *vinput;

		mps_set_cell (state, VAR_BASE_COL, VAR_BASE_ROW,
			      _("Variable"));
		mps_set_cell (state, VAR_BASE_COL + 1, VAR_BASE_ROW,
			      _("Value"));
		mps_set_style (state, VAR_BASE_COL, VAR_BASE_ROW,
			       VAR_BASE_COL + 1, VAR_BASE_ROW,
			       FALSE, TRUE, FALSE);

		range_init (&r,
			    VAR_BASE_COL + 1, VAR_BASE_ROW + 1,
			    VAR_BASE_COL + 1, VAR_BASE_ROW + g_hash_table_size (state->col_hash));
		vinput = value_new_cellrange_r (NULL, &r);
		gnm_solver_param_set_input (param, vinput);
	}

	/* ---------------------------------------- */

	if (state->rows->len > 0) {
		int x = OBJ_BASE_COL;
		int y = OBJ_BASE_ROW;
		MpsRow *row = g_ptr_array_index (state->rows, 0);
		GnmCellRef cr;

		mps_set_cell (state, x, y, _("Objective function"));
		mps_set_style (state, x, y, x, y,  FALSE, TRUE, FALSE);

		if (row->expr) {
			mps_set_expr (state, x + 1, y, row->expr);
			row->expr = NULL;
		} else
			mps_set_cell_float (state, x + 1, y, 0);

		param->problem_type = GNM_SOLVER_MINIMIZE;

		gnm_cellref_init (&cr, NULL, x + 1, y, FALSE);
		gnm_solver_param_set_target (param, &cr);
	}
}

/* ------------------------------------------------------------------------- */

void
mps_file_open  (GOFileOpener const *fo, GOIOContext *io_context,
		WorkbookView *wbv, GsfInput *input);

void
mps_file_open (GOFileOpener const *fo, GOIOContext *io_context,
               WorkbookView *wbv, GsfInput *input)
{
	MpsState state;
	GnmLocale *locale;
	unsigned ui;

	memset (&state, 0, sizeof (state));
	state.io_context = io_context;
	state.wb = wb_view_get_workbook (wbv);
	state.input = GSF_INPUT_TEXTLINE (gsf_input_textline_new (input));
	state.sheet = workbook_sheet_add (state.wb, -1,
					  GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);
	state.param = state.sheet->solver_parameters;
	state.split = g_ptr_array_new ();
	state.rows = g_ptr_array_new ();
	state.row_hash = g_hash_table_new (g_str_hash, g_str_equal);
	state.col_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
						g_free, NULL);

	locale = gnm_push_C_locale ();
	mps_parse_file (&state);
	gnm_pop_C_locale (locale);

	if (go_io_error_occurred (io_context)) {
		go_io_error_push (io_context, go_error_info_new_str
				  (_("Error while reading MPS file.")));
	} else {
		mps_fill_sheet (&state);
		workbook_recalc_all (state.wb);
	}

	g_hash_table_destroy (state.row_hash);
	for (ui = 0; ui < state.rows->len; ui++) {
		MpsRow *row = g_ptr_array_index (state.rows, ui);
		if (!row)
			continue;
		g_free (row->name);
		if (row->expr)
			gnm_expr_free (row->expr);
		g_free (row);
	}
	g_ptr_array_free (state.rows, TRUE);

	g_hash_table_destroy (state.col_hash);

	g_ptr_array_free (state.split, TRUE);
	g_object_unref (state.input);
}
