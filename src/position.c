/* vim: set sw=8: */

/*
 * position.c: Utility routines for various types of positional
 *         coordinates.
 *
 * Copyright (C) 2000 Jody Goldberg (jgoldberg@home.com)
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
#include "config.h"
#include "position.h"
#include "sheet.h"
#include "cell.h"
#include "value.h"
#include "ranges.h"

EvalPos *
eval_pos_init (EvalPos *eval_pos, Sheet *sheet, CellPos const *pos)
{
	g_return_val_if_fail (eval_pos != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);

	eval_pos->sheet = sheet;
	eval_pos->eval  = *pos;

	return eval_pos;
}

EvalPos *
eval_pos_init_dep (EvalPos *eval_pos, Dependent const *dep)
{
	g_return_val_if_fail (dep != NULL, NULL);

	if (DEPENDENT_CELL == (dep->flags & DEPENDENT_TYPE_MASK)) {
		Cell const *cell = DEP_TO_CELL (dep);
		return eval_pos_init (eval_pos, dep->sheet, &cell->pos);
	} else {
		static CellPos const pos = { 0, 0 };
		return eval_pos_init (eval_pos, dep->sheet, &pos);
	}
}

EvalPos *
eval_pos_init_cell (EvalPos *eval_pos, Cell const *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);

	return eval_pos_init (eval_pos, cell->base.sheet, &cell->pos);
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

ParsePos *
parse_pos_init_cell (ParsePos *pp, Cell const *cell)
{
	g_return_val_if_fail (pp != NULL, NULL);
	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (cell->base.sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (cell->base.sheet), NULL);
	g_return_val_if_fail (cell->base.sheet->workbook != NULL, NULL);

	return parse_pos_init (pp, NULL, cell->base.sheet,
			       cell->pos.col, cell->pos.row);
}

ParsePos *
parse_pos_init_evalpos (ParsePos *pp, EvalPos const *ep)
{
	g_return_val_if_fail (pp != NULL, NULL);
	g_return_val_if_fail (ep != NULL, NULL);

	return parse_pos_init (pp, NULL, ep->sheet, ep->eval.col, ep->eval.row);
}

void
cell_ref_make_abs (CellRef *dest, CellRef const *src, EvalPos const *ep)
{
	g_return_if_fail (dest != NULL);
	g_return_if_fail (src != NULL);
	g_return_if_fail (ep != NULL);

	*dest = *src;
	if (src->col_relative)
		dest->col += ep->eval.col;

	if (src->row_relative)
		dest->row += ep->eval.row;

	dest->row_relative = dest->col_relative = FALSE;
}

int
cell_ref_get_abs_col (CellRef const *ref, EvalPos const *pos)
{
	g_return_val_if_fail (ref != NULL, 0);
	g_return_val_if_fail (pos != NULL, 0);

	if (ref->col_relative)
		return pos->eval.col + ref->col;
	return ref->col;

}

int
cell_ref_get_abs_row (CellRef const *ref, EvalPos const *pos)
{
	g_return_val_if_fail (ref != NULL, 0);
	g_return_val_if_fail (pos != NULL, 0);

	if (ref->row_relative)
		return pos->eval.row + ref->row;
	return ref->row;
}

void
cell_get_abs_col_row (CellRef const *cell_ref,
		      CellPos const *pos,
		      int *col, int *row)
{
	g_return_if_fail (cell_ref != NULL);

	if (cell_ref->col_relative)
		*col = pos->col + cell_ref->col;
	else
		*col = cell_ref->col;

	if (cell_ref->row_relative)
		*row = pos->row + cell_ref->row;
	else
		*row = cell_ref->row;
}

