/*
 * position.c: Utility routines for various types of positional
 *         coordinates.
 *
 * Copyright (C) 2000-2005 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2006-2009 Morten Welinder (terra@gnome.org)
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
#include <gnumeric.h>
#include <libgnumeric.h>
#include <position.h>

#include <sheet.h>
#include <sheet-view.h>
#include <cell.h>
#include <value.h>
#include <ranges.h>
#include <string.h>
#include <workbook.h>

/* GnmCellPos made a boxed type */
static GnmCellPos *
gnm_cell_pos_dup (GnmCellPos *pos)
{
	GnmCellPos *res = g_new (GnmCellPos, 1);
	*res = *pos;
	return res;
}

/**
 * gnm_cell_pos_get_type:
 *
 * Returns: the #GType for #GnmCellPos.
 **/
GType
gnm_cell_pos_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmCellPos",
			 (GBoxedCopyFunc)gnm_cell_pos_dup,
			 (GBoxedFreeFunc)g_free);
	}
	return t;
}

/* GnmEvalPos made a boxed type */
static GnmEvalPos *
gnm_eval_pos_dup (GnmEvalPos *ep)
{
	return go_memdup (ep, sizeof (*ep));
}

/**
 * gnm_eval_pos_get_type:
 *
 * Returns: the #GType for #GnmEvalPos.
 **/
GType
gnm_eval_pos_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmEvalPos",
			 (GBoxedCopyFunc)gnm_eval_pos_dup,
			 (GBoxedFreeFunc)g_free);
	}
	return t;
}

/**
 * eval_pos_init:
 * @ep: (out): The position to init.
 * @s: #Sheet
 * @col: column.
 * @row: row
 *
 * Returns: (type void): the initialized #GnmEvalPos (@ep).
 **/
GnmEvalPos *
eval_pos_init (GnmEvalPos *ep, Sheet *sheet, int col, int row)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);

	ep->eval.col = col;
	ep->eval.row = row;
	ep->sheet = sheet;
	ep->dep = NULL;
	ep->array_texpr = NULL;

	return ep;
}

/**
 * eval_pos_init_pos:
 * @ep: (out): The position to init.
 * @s: #Sheet
 * @pos: #GnmCellPos
 *
 * Returns: (type void): the initialized #GnmEvalPos (@ep).
 **/
GnmEvalPos *
eval_pos_init_pos (GnmEvalPos *ep, Sheet *sheet, GnmCellPos const *pos)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (pos != NULL, NULL);

	ep->eval = *pos;
	ep->sheet = sheet;
	ep->dep = NULL;
	ep->array_texpr = NULL;

	return ep;
}

/**
 * eval_pos_init_dep:
 * @ep: (out): The position to init.
 * @dep:
 *
 * Returns: (type void): the initialized #GnmEvalPos (@ep).
 **/
GnmEvalPos *
eval_pos_init_dep (GnmEvalPos *ep, GnmDependent const *dep)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (dep != NULL, NULL);

	ep->eval = *dependent_pos (dep);
	ep->sheet = dep->sheet;
	ep->dep = (GnmDependent *)dep;
	ep->array_texpr = NULL;

	return ep;
}

/**
 * eval_pos_init_editpos:
 * @ep: (out): The position to init.
 * @sv: @Sheetview
 *
 * The function initializes an evalpos with the edit position from the
 * given sheetview.
 *
 * Returns: (type void): the initialized #GnmEvalPos (@ep).
 **/
GnmEvalPos *
eval_pos_init_editpos (GnmEvalPos *ep, SheetView const *sv)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (GNM_IS_SHEET_VIEW (sv), NULL);

	return eval_pos_init (ep, sv_sheet (sv),
		sv->edit_pos.col, sv->edit_pos.row);
}

/**
 * eval_pos_init_cell:
 * @ep: (out): The position to init.
 * @cell: A cell
 *
 * The function initializes an evalpos with the given cell
 *
 * Returns: (type void): the initialized #GnmEvalPos (@ep).
 */
GnmEvalPos *
eval_pos_init_cell (GnmEvalPos *ep, GnmCell const *cell)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (cell != NULL, NULL);

	ep->eval = cell->pos;
	ep->sheet = cell->base.sheet;
	ep->dep = (GnmDependent *)GNM_CELL_TO_DEP (cell);
	ep->array_texpr = NULL;

	return ep;
}

/**
 * eval_pos_init_sheet:
 * @ep: (out): The position to init.
 * @sheet: A sheet
 *
 * The function initializes an evalpos with the given sheet.
 *
 * Returns: (type void): the initialized #GnmEvalPos (@ep).
 */
GnmEvalPos *
eval_pos_init_sheet (GnmEvalPos *ep, Sheet const *sheet)
{
	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	ep->eval.col = ep->eval.row = 0;
	ep->sheet = (Sheet *)sheet;
	ep->dep = NULL;
	ep->array_texpr = NULL;

	return ep;
}

/**
 * eval_pos_is_array_context:
 * @ep: #GnmEvalPos
 *
 * Returns: %TRUE if the evaluation position is an array context.
 **/
gboolean
eval_pos_is_array_context (GnmEvalPos const *ep)
{
	return ep->array_texpr != NULL;
}


static GnmParsePos *
gnm_parse_pos_dup (GnmParsePos *pp)
{
	return go_memdup (pp, sizeof (*pp));
}

/**
 * gnm_parse_pos_get_type:
 *
 * Returns: the #GType for #GnmParsePos.
 **/
GType
gnm_parse_pos_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmParsePos",
			 (GBoxedCopyFunc)gnm_parse_pos_dup,
			 (GBoxedFreeFunc)g_free);
	}
	return t;
}

/**
 * parse_pos_init:
 * @pp: (out): The position to init.
 * @sheet: The sheet being selected
 * @wb: The workbook being selected.
 * @row:
 * @col:
 *
 * Use either a sheet (preferred) or a workbook to initialize the supplied
 * ParsePosition.
 * Returns: (type void): the initialized #GnmParsePos (@pp).
 */
GnmParsePos *
parse_pos_init (GnmParsePos *pp, Workbook *wb, Sheet const *sheet,
		int col, int row)
{
	/* Global */
	if (wb == NULL && sheet == NULL)
		return NULL;

	g_return_val_if_fail (pp != NULL, NULL);

	pp->sheet = (Sheet *)sheet;
	pp->wb = sheet ? sheet->workbook : wb;
	pp->eval.col = col;
	pp->eval.row = row;

	return pp;
}

/**
 * parse_pos_init_dep:
 * @pp: (out): The position to init.
 * @dep: The dependent
 *
 * Returns: (type void): the initialized #GnmParsePos (@pp).
 */
GnmParsePos *
parse_pos_init_dep (GnmParsePos *pp, GnmDependent const *dep)
{
	g_return_val_if_fail (pp != NULL, NULL);

	pp->sheet = dep->sheet;
	pp->wb = dep->sheet ? dep->sheet->workbook : NULL;
	pp->eval = *dependent_pos (dep);

	return pp;
}

/**
 * parse_pos_init_cell:
 * @pp: (out): The position to init.
 * @cell: The cell
 *
 * Returns: (type void): the initialized #GnmParsePos (@pp).
 */
GnmParsePos *
parse_pos_init_cell (GnmParsePos *pp, GnmCell const *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (cell->base.sheet), NULL);
	g_return_val_if_fail (cell->base.sheet->workbook != NULL, NULL);

	return parse_pos_init (pp, NULL, cell->base.sheet,
			       cell->pos.col, cell->pos.row);
}

/**
 * parse_pos_init_evalpos:
 * @pp: (out): The position to init.
 * @pos: #GnmEvalPos
 *
 * Returns: (type void): the initialized #GnmParsePos (@pp).
 */
GnmParsePos *
parse_pos_init_evalpos (GnmParsePos *pp, GnmEvalPos const *ep)
{
	g_return_val_if_fail (ep != NULL, NULL);

	return parse_pos_init (pp, NULL, ep->sheet, ep->eval.col, ep->eval.row);
}

/**
 * parse_pos_init_editpos:
 * @pp: (out): The position to init.
 * @sv: sheet view
 *
 * Returns: (type void): the initialized #GnmParsePos (@pp).
 */
GnmParsePos *
parse_pos_init_editpos (GnmParsePos *pp, SheetView const *sv)
{
	g_return_val_if_fail (GNM_IS_SHEET_VIEW (sv), NULL);

	return parse_pos_init (pp, NULL, sv_sheet (sv),
		sv->edit_pos.col, sv->edit_pos.row);
}

/**
 * parse_pos_init_sheet:
 * @pp: (out): The position to init.
 * @sheet: The sheet
 *
 * Returns: (type void): the initialized #GnmParsePos (@pp).
 */
GnmParsePos *
parse_pos_init_sheet (GnmParsePos *pp, Sheet const *sheet)
{
	g_return_val_if_fail (pp != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	return parse_pos_init (pp, NULL, sheet, 0, 0);
}

/********************************************************************************/


static GnmCellRef *
gnm_cellref_dup (GnmCellRef *cr)
{
	return go_memdup (cr, sizeof (*cr));
}

/**
 * gnm_cellref_get_type:
 *
 * Returns: the #GType for #GnmCellRef.
 **/
GType
gnm_cellref_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmCellRef",
			 (GBoxedCopyFunc)gnm_cellref_dup,
			 (GBoxedFreeFunc)g_free);
	}
	return t;
}

/**
 * gnm_cellref_init:
 * @ref: (out): #GnmCellRef
 * @sheet: (nullable): #Sheet
 * @col: column
 * @row: row
 * @relative: %TRUE for relative, %FALSE for absolute
 *
 * Returns: (transfer none): the initialized #GnmCellRef (@ref).
 **/
GnmCellRef *
gnm_cellref_init (GnmCellRef *ref, Sheet *sheet, int col, int row, gboolean relative)
{
	ref->sheet = sheet;
	ref->col   = col;
	ref->row   = row;
	ref->col_relative = ref->row_relative = relative;

	return ref;
}

/**
 * gnm_cellref_equal:
 * @a: #GnmCellRef
 * @b: #GnmCellRef
 *
 * Returns: %TRUE if @a and @b are equal.
 **/
gboolean
gnm_cellref_equal (GnmCellRef const *a, GnmCellRef const *b)
{
	return (a->col == b->col) &&
	       (a->col_relative == b->col_relative) &&
	       (a->row == b->row) &&
	       (a->row_relative == b->row_relative) &&
	       (a->sheet == b->sheet);
}

/**
 * gnm_cellref_hash:
 * @cr: #GnmCellRef
 *
 * Returns: a hash value for @cr.
 **/
guint
gnm_cellref_hash (GnmCellRef const *cr)
{
	guint h = cr->row;
	h = (h << 16) | (h >> 16);
	h ^= ((guint)cr->col << 2);
	if (cr->col_relative) h ^= 1;
	if (cr->row_relative) h ^= 2;
	return h;
}

/**
 * gnm_cellref_get_col:
 * @ref: #GnmCellRef
 * @ep: #GnmEvalPos
 *
 * Resolves column in @ref to an absolute column number.
 *
 * Returns: the effective column of @ref at @ep.
 **/
int
gnm_cellref_get_col (GnmCellRef const *ref, GnmEvalPos const *ep)
{
	g_return_val_if_fail (ref != NULL, 0);
	g_return_val_if_fail (ep != NULL, 0);

	if (ref->col_relative) {
		Sheet const *sheet = eval_sheet (ref->sheet, ep->sheet);
		int res = (ep->eval.col + ref->col) % gnm_sheet_get_max_cols (sheet);
		if (res < 0)
			return res + gnm_sheet_get_max_cols (sheet);
		return res;
	}
	return ref->col;
}

/**
 * gnm_cellref_get_row:
 * @ref: #GnmCellRef
 * @ep: #GnmEvalPos
 *
 * Resolves column in @ref to an absolute column number.
 *
 * Returns: the effective row of @ref at @ep.
 **/
int
gnm_cellref_get_row (GnmCellRef const *ref, GnmEvalPos const *ep)
{
	g_return_val_if_fail (ref != NULL, 0);
	g_return_val_if_fail (ep != NULL, 0);

	if (ref->row_relative) {
		Sheet const *sheet = eval_sheet (ref->sheet, ep->sheet);
		int res = (ep->eval.row + ref->row) % gnm_sheet_get_max_rows (sheet);
		if (res < 0)
			return res + gnm_sheet_get_max_rows (sheet);
		return res;
	}
	return ref->row;
}

static int
modulo (int i, int max)
{
	if (i < 0) {
		i %= max;
		if (i < 0)
			i += max;
	} else if (i >= max)
		i %= max;

	return i;
}


/**
 * gnm_cellpos_init_cellref_ss:
 * @res: (out): #GnmCellPos
 * @cell_ref: #GnmCellRef
 * @pos: #GnmCellPos
 * @ss: #GnmSheetSize
 *
 * Initializes @res from @cell_ref at @pos in a sheet of size @ss.
 **/
void
gnm_cellpos_init_cellref_ss (GnmCellPos *res, GnmCellRef const *cell_ref,
			     GnmCellPos const *pos, GnmSheetSize const *ss)
{
	g_return_if_fail (cell_ref != NULL);
	g_return_if_fail (res != NULL);

	if (cell_ref->col_relative) {
		int col = cell_ref->col + pos->col;
		res->col = modulo (col, ss->max_cols);
	} else
		res->col = cell_ref->col;

	if (cell_ref->row_relative) {
		int row = cell_ref->row + pos->row;
		res->row = modulo (row, ss->max_rows);
	} else
		res->row = cell_ref->row;
}

/**
 * gnm_cellpos_init_cellref:
 * @res: (out): #GnmCellPos
 * @cr: #GnmCellRef
 * @pos: #GnmCellPos
 * @base_sheet: #Sheet
 *
 * Initializes @res from @cell_ref at @pos in @base_sheet.
 **/
void
gnm_cellpos_init_cellref (GnmCellPos *res, GnmCellRef const *cell_ref,
			  GnmCellPos const *pos, Sheet const *base_sheet)
{
	Sheet const *sheet = eval_sheet (cell_ref->sheet, base_sheet);
	gnm_cellpos_init_cellref_ss (res, cell_ref, pos,
				     gnm_sheet_get_size (sheet));
}

/**
 * gnm_cellref_make_abs:
 * @dest: (out): #GnmCellRef
 * @src: #GnmCellRef
 * @ep: #GnmEvalPos
 *
 * Makes @dest an absolute version of @src at @ep.
 **/
void
gnm_cellref_make_abs (GnmCellRef *dest, GnmCellRef const *src, GnmEvalPos const *ep)
{
	GnmCellPos pos;

	g_return_if_fail (dest != NULL);
	g_return_if_fail (src != NULL);
	g_return_if_fail (ep != NULL);

	gnm_cellpos_init_cellref (&pos, src, &ep->eval, ep->sheet);

	dest->sheet = src->sheet;
	dest->col = pos.col;
	dest->row = pos.row;
	dest->col_relative = FALSE;
	dest->row_relative = FALSE;
}

/**
 * gnm_cellref_set_col_ar:
 * @cr: (inout): #GnmCellRef
 * @pp: #GnmParsePos
 * @abs_rel: %TRUE for relative, %FALSE for absolute
 *
 * Sets the column of @cr to be absolute or relative.
 **/
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

/**
 * gnm_cellref_set_row_ar:
 * @cr: (inout): #GnmCellRef
 * @pp: #GnmParsePos
 * @abs_rel: %TRUE for relative, %FALSE for absolute
 *
 * Sets the row of @cr to be absolute or relative.
 **/
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

/**
 * gnm_rangeref_equal:
 * @a: #GnmRangeRef
 * @b: #GnmRangeRef
 *
 * Returns: %TRUE if @a and @b are equal.
 **/
gboolean
gnm_rangeref_equal (GnmRangeRef const *a, GnmRangeRef const *b)
{
	return  gnm_cellref_equal (&a->a, &b->a) &&
		gnm_cellref_equal (&a->b, &b->b);
}

/**
 * gnm_rangeref_hash:
 * @rr: #GnmRangeRef
 *
 * Returns: a hash value for @rr.
 **/
guint
gnm_rangeref_hash (GnmRangeRef const *rr)
{
	guint h = gnm_cellref_hash (&rr->a);
	h = (h << 16) | (h >> 16);
	h ^= gnm_cellref_hash (&rr->b);
	return h;
}

/**
 * gnm_rangeref_dup:
 * @rr: #GnmRangeRef
 *
 * Returns: (transfer full): a copy of @rr.
 **/
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
 * gnm_rangeref_get_type:
 *
 * Returns: the #GType for #GnmRangeRef.
 **/
GType
gnm_rangeref_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmRangeRef",
			 (GBoxedCopyFunc)gnm_rangeref_dup,
			 (GBoxedFreeFunc)g_free);
	}
	return t;
}

/**
 * gnm_rangeref_normalize_pp:
 * @ref: #GnmRangeRef
 * @pp: #GnmParsePos
 * @start_sheet: (out) (transfer none): starting sheet
 * @end_sheet: (out) (transfer none): ending sheet
 * @dest: (out): range
 *
 * Normalizes @ref at @pp.
 **/
void
gnm_rangeref_normalize_pp (GnmRangeRef const *ref, GnmParsePos const *pp,
			   Sheet **start_sheet, Sheet **end_sheet,
			   GnmRange *dest)
{
	GnmSheetSize const *ss;

	g_return_if_fail (ref != NULL);
	g_return_if_fail (pp != NULL);

	*start_sheet = eval_sheet (ref->a.sheet, pp->sheet);
	*end_sheet   = eval_sheet (ref->b.sheet, *start_sheet);

	ss = gnm_sheet_get_size2 (*start_sheet, pp->wb);
	gnm_cellpos_init_cellref_ss (&dest->start, &ref->a, &pp->eval, ss);

	ss = *end_sheet
		? gnm_sheet_get_size (*end_sheet)
		: ss;
	gnm_cellpos_init_cellref_ss (&dest->end, &ref->b, &pp->eval, ss);

	range_normalize (dest);
}

/**
 * gnm_rangeref_normalize:
 * @rr: reference
 * @ep: evaluation position
 * @start_sheet: (out) (transfer none): starting sheet
 * @end_sheet: (out) (transfer none): ending sheet
 * @dest: (out): range
 *
 * Take a range_ref and normalize it by converting to absolute coords and
 * handling inversions.
 */
void
gnm_rangeref_normalize (GnmRangeRef const *ref, GnmEvalPos const *ep,
			Sheet **start_sheet, Sheet **end_sheet, GnmRange *dest)
{
	GnmParsePos pp;

	parse_pos_init_evalpos (&pp, ep);
	gnm_rangeref_normalize_pp (ref, &pp, start_sheet, end_sheet, dest);
}

/**
 * gnm_cellpos_hash:
 * @key: #GnmCellPos
 *
 * Returns: a hash value for @key.
 **/
guint
gnm_cellpos_hash (GnmCellPos const *key)
{
	guint h = key->row;
	h = (h << 16) | (h >> 16);
	h ^= key->col;
	return h;
}

/**
 * gnm_cellpos_equal:
 * @a: #GnmCellPos
 * @b: #GnmCellPos
 *
 * Returns: %TRUE if @a and @b are equal.
 **/
gint
gnm_cellpos_equal (GnmCellPos const *a, GnmCellPos const *b)
{
	return (a->row == b->row && a->col == b->col);
}
