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

inline EvalPos *
eval_pos_init (EvalPos *eval_pos, Sheet *sheet, CellPos const *pos)
{
	g_return_val_if_fail (eval_pos != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);

	eval_pos->sheet = sheet;
	eval_pos->eval  = *pos;

	return eval_pos;
}

EvalPos *
eval_pos_init_cell (EvalPos *eval_pos, Cell const *cell)
{
	CellPos pos;
	g_return_val_if_fail (cell != NULL, NULL);

	pos.col = cell->col_info->pos;
	pos.row = cell->row_info->pos;
	return eval_pos_init (eval_pos, cell->sheet, &pos);
}

EvalPos *
eval_pos_init_cellref (EvalPos *dest, EvalPos const *src, 
		  CellRef const *ref)
{
	/* FIXME : This is a place to catch all of the strange
	 * usages.  Please figure out what they were trying to do.
	 */
	*dest = *src;
	return dest;
}

/*
 * Supply either a sheet (preferred) or a workbook.
 */
ParsePos *
parse_pos_init (ParsePos *pp, Workbook *wb, Sheet *sheet, int col, int row)
{
	/* Global */
	if (wb == NULL && sheet == NULL)
		return NULL;

	/* Either sheet or workbook, not both */
	g_return_val_if_fail ((sheet != NULL) != (wb != NULL), NULL);
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
	g_return_val_if_fail (cell->sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (cell->sheet), NULL);
	g_return_val_if_fail (cell->sheet->workbook != NULL, NULL);

	return parse_pos_init (
		pp,
		NULL,
		cell->sheet,
		cell->col_info->pos,
		cell->row_info->pos);
}

ParsePos *
parse_pos_init_evalpos (ParsePos *pp, EvalPos const *ep)
{
	g_return_val_if_fail (pp != NULL, NULL);
	g_return_val_if_fail (ep != NULL, NULL);

	return parse_pos_init (pp, NULL, ep->sheet, ep->eval.col, ep->eval.row);
}

/**
 * range_ref_normalize :  Take a range_ref from a Value and normalize it
 *     by converting to absolute coords and handling inversions.
 */
void
range_ref_normalize  (Range *dest, Sheet **start_sheet, Sheet **end_sheet,
		      Value const *ref, EvalPos const *ep)
{
	g_return_if_fail (ref != NULL);
	g_return_if_fail (ep != NULL);
	g_return_if_fail (ref->type == VALUE_CELLRANGE);

	cell_get_abs_col_row (&ref->v_range.cell.a, &ep->eval,
			      &dest->start.col, &dest->start.row);
	cell_get_abs_col_row (&ref->v_range.cell.b, &ep->eval,
			      &dest->end.col, &dest->end.row);
	range_normalize (dest);

	*start_sheet = eval_sheet (ref->v_range.cell.a.sheet, ep->sheet);
	*end_sheet = eval_sheet (ref->v_range.cell.b.sheet, ep->sheet);
}
