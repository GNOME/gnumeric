/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * position.c: Utility routines for various types of positional
 *         coordinates.
 *
 * Copyright (C) 2000-2005 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "libgnumeric.h"
#include "position.h"

#include "sheet.h"
#include "sheet-view.h"
#include "cell.h"
#include "value.h"
#include "ranges.h"


GnmEvalPos *
eval_pos_init (GnmEvalPos *ep, Sheet *sheet, int col, int row)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);

	ep->eval.col = col;
	ep->eval.row = row;
	ep->sheet = sheet;
	ep->dep   = NULL;
	ep->array = NULL;

	return ep;
}

GnmEvalPos *
eval_pos_init_pos (GnmEvalPos *ep, Sheet *sheet, GnmCellPos const *pos)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	ep->eval  = *pos;
	ep->sheet = sheet;
	ep->dep   = NULL;
	ep->array = NULL;

	return ep;
}

GnmEvalPos *
eval_pos_init_dep (GnmEvalPos *ep, GnmDependent const *dep)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (dep != NULL, NULL);

	if (dependent_is_cell (dep))
		ep->eval = GNM_DEP_TO_CELL (dep)->pos;
	else
		ep->eval.col = ep->eval.row = 0;
	ep->sheet = dep->sheet;
	ep->dep   = (GnmDependent *)dep;
	ep->array = NULL;

	return ep;
}

/**
 * eval_pos_init_editpos :
 *
 * @ep : The position to init.
 * @sv : Sheetview
 *
 * The function initializes an evalpos with the edit position from the
 * given sheetview.
 *
 * Returns @ep
 */
GnmEvalPos *
eval_pos_init_editpos (GnmEvalPos *ep, SheetView const *sv)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (IS_SHEET_VIEW (sv), NULL);

	return eval_pos_init (ep, sv_sheet (sv),
		sv->edit_pos.col, sv->edit_pos.row);
}

/**
 * eval_pos_init_cell :
 *
 * @ep : The position to init.
 * @cell : A cell
 *
 * The function initializes an evalpos with the given cell
 *
 * Returns @ep
 */
GnmEvalPos *
eval_pos_init_cell (GnmEvalPos *ep, GnmCell const *cell)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (cell != NULL, NULL);

	ep->eval  = cell->pos;
	ep->sheet = cell->base.sheet;
	ep->dep   = (GnmDependent *)GNM_CELL_TO_DEP (cell);
	ep->array = NULL;

	return ep;
}

GnmEvalPos *
eval_pos_init_sheet (GnmEvalPos *ep, Sheet *sheet)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	ep->eval.col = ep->eval.row = 0;
	ep->sheet = sheet;
	ep->dep   = NULL;
	ep->array = NULL;

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
GnmParsePos *
parse_pos_init (GnmParsePos *pp, Workbook *wb, Sheet *sheet, int col, int row)
{
	/* Global */
	if (wb == NULL && sheet == NULL)
		return NULL;

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
GnmParsePos *
parse_pos_init_dep (GnmParsePos *pp, GnmDependent const *dep)
{
	g_return_val_if_fail (pp != NULL, NULL);

	pp->sheet = dep->sheet;
	pp->wb = dep->sheet->workbook;
	if (dependent_is_cell (dep))
		pp->eval = GNM_DEP_TO_CELL (dep)->pos;
	else
		pp->eval.col = pp->eval.row = 0;

	return pp;
}

GnmParsePos *
parse_pos_init_cell (GnmParsePos *pp, GnmCell const *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (cell->base.sheet), NULL);
	g_return_val_if_fail (cell->base.sheet->workbook != NULL, NULL);

	return parse_pos_init (pp, NULL, cell->base.sheet,
			       cell->pos.col, cell->pos.row);
}

GnmParsePos *
parse_pos_init_evalpos (GnmParsePos *pp, GnmEvalPos const *ep)
{
	g_return_val_if_fail (ep != NULL, NULL);

	return parse_pos_init (pp, NULL, ep->sheet, ep->eval.col, ep->eval.row);
}

GnmParsePos *
parse_pos_init_editpos (GnmParsePos *pp, SheetView const *sv)
{
	g_return_val_if_fail (IS_SHEET_VIEW (sv), NULL);

	return parse_pos_init (pp, NULL, sv_sheet (sv),
		sv->edit_pos.col, sv->edit_pos.row);
}

GnmParsePos *
parse_pos_init_sheet (GnmParsePos *pp, Sheet *sheet)
{
	g_return_val_if_fail (pp != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	return parse_pos_init (pp, NULL, sheet, 0, 0);
}

/********************************************************************************/

GnmCellRef *
gnm_cellref_init (GnmCellRef *ref, Sheet *sheet, int col, int row, gboolean relative)
{
	ref->sheet = sheet;
	ref->col   = col;
	ref->row   = row;
	ref->col_relative = ref->row_relative = relative;

	return ref;
}

gboolean
gnm_cellref_equal (GnmCellRef const *a, GnmCellRef const *b)
{
	return (a->col == b->col) &&
	       (a->col_relative == b->col_relative) &&
	       (a->row == b->row) &&
	       (a->row_relative == b->row_relative) &&
	       (a->sheet == b->sheet);
}

guint
gnm_cellref_hash (GnmCellRef const *cr)
{
	guint h = ((cr->row << 8) ^ cr->col) * 4;
	if (cr->col_relative) h |= 1;
	if (cr->row_relative) h |= 2;
	return h;
}

int
gnm_cellref_get_col (GnmCellRef const *ref, GnmEvalPos const *ep)
{
	g_return_val_if_fail (ref != NULL, 0);
	g_return_val_if_fail (ep != NULL, 0);

	if (ref->col_relative) {
		int res = (ep->eval.col + ref->col) % gnm_sheet_get_max_cols (ref->sheet);
		if (res < 0)
			return res + gnm_sheet_get_max_cols (ref->sheet);
		return res;
	}
	return ref->col;
}

int
gnm_cellref_get_row (GnmCellRef const *ref, GnmEvalPos const *ep)
{
	g_return_val_if_fail (ref != NULL, 0);
	g_return_val_if_fail (ep != NULL, 0);

	if (ref->row_relative) {
		int res = (ep->eval.row + ref->row) % gnm_sheet_get_max_rows (ref->sheet);
		if (res < 0)
			return res + gnm_sheet_get_max_rows (ref->sheet);
		return res;
	}
	return ref->row;
}

void
gnm_cellpos_init_cellref (GnmCellPos *res,
			  GnmCellRef const *cell_ref, GnmCellPos const *pos)
{
	g_return_if_fail (cell_ref != NULL);
	g_return_if_fail (res != NULL);

	if (cell_ref->col_relative) {
		res->col = (cell_ref->col + pos->col) % gnm_sheet_get_max_cols (cell_ref->sheet);
		if (res->col < 0)
			res->col += gnm_sheet_get_max_cols (cell_ref->sheet);
	} else
		res->col = cell_ref->col;

	if (cell_ref->row_relative) {
		res->row = (cell_ref->row + pos->row) % gnm_sheet_get_max_rows (cell_ref->sheet);
		if (res->row < 0)
			res->row += gnm_sheet_get_max_rows (cell_ref->sheet);
	} else
		res->row = cell_ref->row;
}

void
gnm_cellref_make_abs (GnmCellRef *dest, GnmCellRef const *src, GnmEvalPos const *ep)
{
	g_return_if_fail (dest != NULL);
	g_return_if_fail (src != NULL);
	g_return_if_fail (ep != NULL);

	*dest = *src;
	if (src->col_relative) {
		dest->col = (dest->col + ep->eval.col) % gnm_sheet_get_max_cols (dest->sheet);
		if (dest->col < 0)
			dest->col += gnm_sheet_get_max_cols (dest->sheet);
	}

	if (src->row_relative) {
		dest->row = (dest->row + ep->eval.row) % gnm_sheet_get_max_rows (dest->sheet);
		if (dest->row < 0)
			dest->row += gnm_sheet_get_max_rows (dest->sheet);
	}

	dest->row_relative = dest->col_relative = FALSE;
}

void
gnm_cellref_set_col_ar (GnmCellRef *cr, GnmParsePos const *pp, gboolean abs_rel)
{
	if (cr->col_relative ^ abs_rel) {
		if (cr->col_relative)
			cr->col += pp->eval.col;
		else
			cr->col -= pp->eval.col;
		cr->col_relative = abs_rel;
	}
}

void
gnm_cellref_set_row_ar (GnmCellRef *cr, GnmParsePos const *pp, gboolean abs_rel)
{
	if (cr->row_relative ^ abs_rel) {
		if (cr->row_relative)
			cr->row += pp->eval.row;
		else
			cr->row -= pp->eval.row;
		cr->row_relative = abs_rel;
	}
}

gboolean
gnm_rangeref_equal (GnmRangeRef const *a, GnmRangeRef const *b)
{
	return  gnm_cellref_equal (&a->a, &b->a) &&
		gnm_cellref_equal (&a->b, &b->b);
}

guint
gnm_rangeref_hash (GnmRangeRef const *rr)
{
	return gnm_cellref_hash (&rr->a) << 16 | gnm_cellref_hash (&rr->b);
}

GnmRangeRef *
gnm_rangeref_dup (GnmRangeRef const *rr)
{
	GnmRangeRef *res;

	g_return_val_if_fail (rr != NULL, NULL);

	res = g_new (GnmRangeRef, 1);
	*res = *rr;
	return res;
}

/**
 * gnm_rangeref_normalize :  Take a range_ref and normalize it
 *     by converting to absolute coords and handling inversions.
 */
void
gnm_rangeref_normalize (GnmRangeRef const *ref, GnmEvalPos const *ep,
			Sheet **start_sheet, Sheet **end_sheet, GnmRange *dest)
{
	g_return_if_fail (ref != NULL);
	g_return_if_fail (ep != NULL);

	gnm_cellpos_init_cellref (&dest->start, &ref->a, &ep->eval);
	gnm_cellpos_init_cellref (&dest->end, &ref->b, &ep->eval);
	range_normalize (dest);

	*start_sheet = eval_sheet (ref->a.sheet, ep->sheet);
	*end_sheet   = eval_sheet (ref->b.sheet, *start_sheet);
}

guint
gnm_cellpos_hash (GnmCellPos const *key)
{
	return (key->row << 8) | key->col;
}

gint
gnm_cellpos_equal (GnmCellPos const *a, GnmCellPos const *b)
{
	return (a->row == b->row && a->col == b->col);
}

