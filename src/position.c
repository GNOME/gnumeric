/* vim: set sw=8: */

/*
 * position.c: Utility routines for various types of positional
 *         coordinates.
 *
 * Copyright (C) 2000-2002 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "position.h"

#include "sheet.h"
#include "sheet-view.h"
#include "cell.h"
#include "value.h"
#include "ranges.h"


CellRef *
cellref_set (CellRef *ref, Sheet *sheet, int col, int row, gboolean relative)
{
	ref->sheet = sheet;
	ref->col   = col;
	ref->row   = row;
	ref->col_relative = ref->row_relative = relative;

	return ref;
}

EvalPos *
eval_pos_init (EvalPos *ep, Sheet *sheet, CellPos const *pos)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	ep->sheet = sheet;
	ep->eval  = *pos;
	ep->dep   = NULL;

	return ep;
}

EvalPos *
eval_pos_init_dep (EvalPos *ep, Dependent const *dep)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (dep != NULL, NULL);

	ep->dep = (Dependent *)dep;
	ep->sheet = dep->sheet;
	if (dependent_is_cell (dep)) {
		ep->eval = DEP_TO_CELL (dep)->pos;
	} else {
		static CellPos const pos = { 0, 0 };
		ep->eval = pos;
	}
	return ep;
}

EvalPos *
eval_pos_init_cell (EvalPos *ep, Cell const *cell)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (cell != NULL, NULL);

	ep->dep = CELL_TO_DEP (cell);
	ep->sheet = cell->base.sheet;
	ep->eval = cell->pos;
	return ep;
}

EvalPos *
eval_pos_init_sheet (EvalPos *ep, Sheet *sheet)
{
	static CellPos const pos = { 0, 0 };

	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	ep->dep = NULL;
	ep->sheet = sheet;
	ep->eval = pos;
	return ep;
}

/*
 * parse_pos_init :
 *
 * @pp : The position to init.
 * @sheet : The sheet being selected
 * @wb : The workbook being selected.
 * @row :
 * @col :
 *
 * Use either a sheet (preferred) or a workbook to initialize the supplied
 * ParsePosition.
 */
ParsePos *
parse_pos_init (ParsePos *pp, Workbook *wb, Sheet *sheet, int col, int row)
{
	/* Global */
	if (wb == NULL && sheet == NULL)
		return NULL;

	/* Either sheet or workbook */
	g_return_val_if_fail ((sheet != NULL) || (wb != NULL), NULL);
	g_return_val_if_fail (pp != NULL, NULL);

	pp->sheet = sheet;
	pp->wb = sheet ? sheet->workbook : wb;
	pp->eval.col = col;
	pp->eval.row = row;

	return pp;
}

/*
 * parse_pos_init_dep :
 *
 * @pp : The position to init.
 * @dep : The dependent
 */
ParsePos *
parse_pos_init_dep (ParsePos *pp, Dependent const *dep)
{
	g_return_val_if_fail (pp != NULL, NULL);

	pp->sheet = dep->sheet;
	pp->wb = dep->sheet->workbook;
	pp->eval.col = pp->eval.row = 0;

	return pp;
}

ParsePos *
parse_pos_init_cell (ParsePos *pp, Cell const *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (cell->base.sheet), NULL);
	g_return_val_if_fail (cell->base.sheet->workbook != NULL, NULL);

	return parse_pos_init (pp, NULL, cell->base.sheet,
			       cell->pos.col, cell->pos.row);
}

ParsePos *
parse_pos_init_evalpos (ParsePos *pp, EvalPos const *ep)
{
	g_return_val_if_fail (ep != NULL, NULL);

	return parse_pos_init (pp, NULL, ep->sheet, ep->eval.col, ep->eval.row);
}

ParsePos *
parse_pos_init_editpos (ParsePos *pp, SheetView const *sv)
{
	g_return_val_if_fail (IS_SHEET_VIEW (sv), NULL);

	return parse_pos_init (pp, NULL, sv_sheet (sv),
		sv->edit_pos.col, sv->edit_pos.row);
}

/********************************************************************************/

gboolean
cellref_equal (CellRef const *a, CellRef const *b)
{
	return (a->col == b->col) &&
	       (a->col_relative == b->col_relative) &&
	       (a->row == b->row) &&
	       (a->row_relative == b->row_relative) &&
	       (a->sheet == b->sheet);
}

int
cellref_get_abs_col (CellRef const *ref, EvalPos const *ep)
{
	g_return_val_if_fail (ref != NULL, 0);
	g_return_val_if_fail (ep != NULL, 0);

	if (ref->col_relative) {
		int res = (ep->eval.col + ref->col) % SHEET_MAX_COLS;
		if (res < 0)
			return res + SHEET_MAX_COLS;
		return res;
	}
	return ref->col;
}

int
cellref_get_abs_row (CellRef const *ref, EvalPos const *ep)
{
	g_return_val_if_fail (ref != NULL, 0);
	g_return_val_if_fail (ep != NULL, 0);

	if (ref->row_relative) {
		int res = (ep->eval.row + ref->row) % SHEET_MAX_ROWS;
		if (res < 0)
			return res + SHEET_MAX_ROWS;
		return res;
	}
	return ref->row;
}

void
cellref_get_abs_pos (CellRef const *cell_ref,
		     CellPos const *pos,
		     CellPos *res)
{
	g_return_if_fail (cell_ref != NULL);
	g_return_if_fail (res != NULL);

	if (cell_ref->col_relative) {
		res->col = (cell_ref->col + pos->col) % SHEET_MAX_COLS;
		if (res->col < 0)
			res->col += SHEET_MAX_COLS;
	} else
		res->col = cell_ref->col;

	if (cell_ref->row_relative) {
		res->row = (cell_ref->row + pos->row) % SHEET_MAX_ROWS;
		if (res->row < 0)
			res->row += SHEET_MAX_ROWS;
	} else
		res->row = cell_ref->row;
}

void
cellref_make_abs (CellRef *dest, CellRef const *src, EvalPos const *ep)
{
	g_return_if_fail (dest != NULL);
	g_return_if_fail (src != NULL);
	g_return_if_fail (ep != NULL);

	*dest = *src;
	if (src->col_relative) {
		dest->col = (dest->col + ep->eval.col) % SHEET_MAX_COLS;
		if (dest->col < 0)
			dest->col += SHEET_MAX_COLS;
	}

	if (src->row_relative) {
		dest->row = (dest->row + ep->eval.row) % SHEET_MAX_ROWS;
		if (dest->row < 0)
			dest->row += SHEET_MAX_ROWS;
	}

	dest->row_relative = dest->col_relative = FALSE;
}


guint
cellref_hash (const CellRef *cr)
{
	guint h = ((cr->col * 5) ^ cr->row) * 4;
	if (cr->col_relative) h |= 1;
	if (cr->row_relative) h |= 2;
	return h;
}

RangeRef *
value_to_rangeref (Value *v, gboolean release)
{
	RangeRef *gr;

	g_return_val_if_fail (v->type == VALUE_CELLRANGE, NULL);

	gr = g_new0 (RangeRef, 1);
	*gr = v->v_range.cell;

	if (release)
		value_release (v);
	
	return gr;
	
}


/**
 * range_ref_normalize :  Take a range_ref and normalize it
 *     by converting to absolute coords and handling inversions.
 */
void
rangeref_normalize (RangeRef const *ref, EvalPos const *ep,
		    Sheet **start_sheet, Sheet **end_sheet, Range *dest)
{
	g_return_if_fail (ref != NULL);
	g_return_if_fail (ep != NULL);

	cellref_get_abs_pos (&ref->a, &ep->eval, &dest->start);
	cellref_get_abs_pos (&ref->b, &ep->eval, &dest->end);
	range_normalize (dest);

	*start_sheet = eval_sheet (ref->a.sheet, ep->sheet);
	*end_sheet = eval_sheet (ref->b.sheet, *start_sheet);
}
