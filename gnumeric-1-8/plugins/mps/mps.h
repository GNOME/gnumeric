/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * mps.h: The main header file for the MPS file importer.
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *   File handling code copied from Dif module.
 *
 *      MPS importer module.  MPS format is a de facto standard ASCII format
 *      among most of the commercial LP solvers.
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
#ifndef GNUMERIC_PLUGINS_MPS_H
#define GNUMERIC_PLUGINS_MPS_H 1

#include <gsf/gsf-input-textline.h>
#include "numbers.h"
#include <gnumeric.h>

#define N_INPUT_LINES_BETWEEN_UPDATES   50
#define MAX_COL                         160


/*************************************************************************
 *
 * Data structures.
 */

/*
 * MPS Row type (E, L, G, or N).
 */
typedef enum {
        EqualityRow, LessOrEqualRow, GreaterOrEqualRow, ObjectiveRow
} MpsRowType;

/*
 * MPS Row.
 */
typedef struct {
        MpsRowType type;
        gchar      *name;
        gint       index;
} MpsRow;

/*
 * MPS Column.
 */
typedef struct {
        gchar      *name;
        MpsRow     *row;
        gnm_float value;
} MpsCol;

/*
 * MPS Range.
 */
typedef struct {
        gchar      *name;
        MpsRow     *row;
        gnm_float value;
} MpsRange;

/*
 * MPS Bound type (LO, UP, FX, FR, MI, BV, LI, or UI).
 */
typedef enum {
        LowerBound, UpperBound, FixedVariable, FreeVariable, LowerBoundInf,
	BinaryVariable, LowerBoundInt, UpperBoundInt
} MpsBoundType;

/*
 * MPS Bound.
 */
typedef struct {
        char         *name;
        gint         col_index;
        gnm_float   value;
        MpsBoundType type;
} MpsBound;


/*
 * MPS RHS.
 */
typedef struct {
        gchar      *name;
        MpsRow     *row;
        gnm_float value;
} MpsRhs;

/*
 * Column mapping.
 */
typedef struct {
        gchar *name;
        gint  index;
} MpsColInfo;


/*
 * Input context.
 */
typedef struct {
        IOContext *io_context;

	GsfInputTextline *input;
        gint   line_no;
        gchar *line;

        Sheet  *sheet;

        gchar      *name;
        GSList     *rows;
        GSList     *cols;
        GSList     *rhs;
        GSList     *bounds;
        gint       n_rows, n_cols, n_bounds;
        GHashTable *row_hash;
        GHashTable *col_hash;
        gchar      **col_name_tbl;
        MpsRow     *objective_row;
        gnm_float **matrix;
} MpsInputContext;



/*************************************************************************
 *
 * Constants.
 */

static const int MAIN_INFO_ROW       = 1;
static const int MAIN_INFO_COL       = 0;
static const int OBJECTIVE_VALUE_COL = 1;

static const int VARIABLE_COL        = 1;
static const int VARIABLE_ROW        = 5;

static const int CONSTRAINT_COL      = 1;
static const int CONSTRAINT_ROW      = 10;


/*************************************************************************
 *
 * The Public Interface of the module
 */

/* Reads the MPS file in and creates a spreadsheet model of it. */
void     mps_file_open  (GOFileOpener const *fo, IOContext *io_context,
			 WorkbookView *wbv, GsfInput *input);

void     mps_parse_file (MpsInputContext *ctxt);
gboolean mps_add_row    (MpsInputContext *ctxt, MpsRowType type, gchar *txt);


#endif


