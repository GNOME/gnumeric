/* vim: set sw=8: */

/*
 * sheet-style.c: storage mechanism for styles and eventually cells.
 *
 * Copyright (C) 2000, 2001 Jody Goldberg (jgoldberg@home.com)
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
#include <config.h>
#include "sheet-style.h"
#include "ranges.h"
#include "sheet.h"
#include "expr.h"
#include "format.h"
#include "style.h"
#include "style-border.h"
#include "cell.h"
#include "portability.h"

/* Place holder until I merge in the new styles too */
static void
pstyle_set_border (MStyle *st, StyleBorder *border,
		   StyleBorderLocation side)
{
	mstyle_set_border (st, MSTYLE_BORDER_TOP + side,
			   style_border_ref (border));
}

/* Amortize the cost of applying a partial style over a large region
 * by caching and rereferencing the merged result for repeated styles.
 */
typedef struct {
	MStyle	   *new_style;
	MStyle	   *pstyle;
	GHashTable *cache;
} ReplacementStyle;

static ReplacementStyle *
rstyle_ctor (ReplacementStyle *res, MStyle *new_style, MStyle *pstyle)
{
	if (pstyle == NULL) {
		res->new_style = new_style;
		res->pstyle = NULL;
		res->cache = NULL;
	} else {
		res->new_style = NULL;
		res->pstyle = pstyle;
		res->cache = g_hash_table_new (g_direct_hash, g_direct_equal);
	}
	return res;
}

static void
cb_style_unref (gpointer key, gpointer value, gpointer user_data)
{
	mstyle_unref ((MStyle *)key);
	mstyle_unref ((MStyle *)value);
}

static void
rstyle_dtor (ReplacementStyle *rs)
{
	if (rs->cache != NULL) {
		g_hash_table_foreach (rs->cache, cb_style_unref, NULL);
		g_hash_table_destroy (rs->cache);
		rs->cache = NULL;
	}
}

/**
 * rstyle_apply :  Utility routine that is at the core of applying partial
 * styles or storing complete styles.  It will eventually be smarter
 * and will maintain the cache of styles associated with each sheet
 */
static void
rstyle_apply (MStyle **old, ReplacementStyle *rs)
{
	MStyle *s;
	g_return_if_fail (old != NULL);
	g_return_if_fail (rs != NULL);

	s = rs->new_style;
	if (rs->pstyle != NULL) {
		g_return_if_fail (s == NULL);

		/* Cache the merged styles keeping a reference to the originals
		 * just in case all instances change.
		 */
		s = (MStyle *)g_hash_table_lookup (rs->cache, *old);
		if (s == NULL) {
			s = mstyle_copy_merge (*old, rs->pstyle);
			mstyle_ref (*old);
			g_hash_table_insert (rs->cache, *old, s);
		}
	}

	if (*old != s) {
		mstyle_ref (s);
		if (*old)
			mstyle_unref (*old);
		*old = s;
	}
}

#if 0
/**
 * sheet_style_lookup :
 *
 * @sheet : the sheet
 * @s     : an optional style
 * @ps    : an optional partial style
 *
 * Looks up (but does not reference) a style from the sheets collection.
 *
 * FIXME : The style engine needs to do a better job of merging like styles.
 * We should do a lookup within a sheet specific hash table.
 */
MStyle *
sheet_style_lookup (Sheet *sheet, MStyle const *s, MStyle const *ps)
{
	return NULL;
}
#endif

/****************************************************************************/

/* if you change this change the tile_{widths,heights} here, in sheet_style_get
 * andin the sanity check in sheet_style_init
 */
#define TILE_TOP_LEVEL	3

#define	TILE_SIZE_COL	4
#define	TILE_SIZE_ROW	16

typedef enum {
	TILE_UNDEFINED	= -1,
	TILE_SIMPLE	=  0,
	TILE_COL	=  1,
	TILE_ROW	=  2,
	TILE_MATRIX	=  3,
	TILE_PTR_MATRIX	=  4
} CellTileType;
static int const tile_size [] = {
	1,				/* TILE_SIMPLE */
	TILE_SIZE_COL,			/* TILE_COL */
	TILE_SIZE_ROW,			/* TILE_ROW */
	TILE_SIZE_COL * TILE_SIZE_ROW	/* TILE_MATRIX */
};
static int const tile_widths [] = {
	1,
	TILE_SIZE_COL,
	TILE_SIZE_COL * TILE_SIZE_COL,
	TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL,
	TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL
};
static int const tile_heights [] = {
	1,
	TILE_SIZE_ROW,
	TILE_SIZE_ROW * TILE_SIZE_ROW,
	TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW,
	TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW
};

typedef union _CellTile CellTile;
typedef struct {
	CellTileType const type;
	MStyle *style [1];
} CellTileStyleSimple;
typedef struct {
	CellTileType const type;
	MStyle *style [TILE_SIZE_COL];
} CellTileStyleCol;
typedef struct {
	CellTileType const type;
	MStyle *style [TILE_SIZE_ROW];
} CellTileStyleRow;
typedef struct {
	CellTileType const type;
	MStyle *style [TILE_SIZE_COL * TILE_SIZE_ROW];
} CellTileStyleMatrix;
typedef struct {
	CellTileType const type;
	CellTile	*ptr [TILE_SIZE_COL * TILE_SIZE_ROW];
} CellTilePtrMatrix;

union _CellTile {
	CellTileType const type;
	CellTileStyleSimple	style_any;
	CellTileStyleSimple	style_simple;
	CellTileStyleCol	style_col;
	CellTileStyleRow	style_row;
	CellTileStyleMatrix	style_matrix;
	CellTilePtrMatrix	ptr_matrix;
};

static void
cell_tile_dtor (CellTile *tile)
{
	CellTileType t;

	g_return_if_fail (tile != NULL);

	t = tile->type;
	if (t == TILE_PTR_MATRIX) {
		int i = TILE_SIZE_COL * TILE_SIZE_ROW;
		while (--i >= 0) {
			cell_tile_dtor (tile->ptr_matrix.ptr [i]);
			tile->ptr_matrix.ptr [i] = NULL;
		}
	} else if (TILE_SIMPLE <= t && t <= TILE_MATRIX) {
		int i = tile_size [t];
		while (--i >= 0) {
			mstyle_unref (tile->style_any.style [i]);
			tile->style_any.style [i] = NULL;
		}
	} else {
		g_return_if_fail (FALSE); /* don't free anything */
	}

	*((CellTileType *)&(tile->type)) = TILE_UNDEFINED; /* poison it */
	g_free (tile);
}

static CellTile *
cell_tile_style_new (MStyle *style, CellTileType t)
{
	CellTile *res;

	switch (t) {
	case TILE_SIMPLE : res = (CellTile *)g_new (CellTileStyleSimple, 1);
			   break;
	case TILE_COL :	   res = (CellTile *)g_new (CellTileStyleCol, 1);
			   break;
	case TILE_ROW :	   res = (CellTile *)g_new (CellTileStyleRow, 1);
			   break;
	case TILE_MATRIX : res = (CellTile *)g_new (CellTileStyleMatrix, 1);
			   break;
	default : g_return_val_if_fail (FALSE, NULL);
	};

	*((CellTileType *)&(res->type)) = t;

	if (style != NULL) {
		int i = tile_size [t];
		mstyle_ref_multiple (style, i);
		while (--i >= 0)
			res->style_any.style [i] = style;
	}

	return res;
}

static CellTile *
cell_tile_ptr_matrix_new (CellTile *t)
{
	CellTilePtrMatrix *res;

	g_return_val_if_fail (t != NULL, NULL);
	g_return_val_if_fail (TILE_SIMPLE <= t->type &&
			      TILE_MATRIX >= t->type, NULL);

	res = g_new (CellTilePtrMatrix, 1);
	*((CellTileType *)&(res->type)) = TILE_PTR_MATRIX;

	/* TODO :
	 * If we wanted to get fancy we could use self similarity to decrease
	 * the number of subtiles.  However, this would increase the cost of
	 * applying changes later so I'm not sure it is worth the effort.
	 */
	switch (t->type) {
	case TILE_SIMPLE : {
		int i = TILE_SIZE_COL * TILE_SIZE_ROW;
		while (--i >= 0)
			res->ptr [i] = cell_tile_style_new (
				t->style_simple.style [0], TILE_SIMPLE);
		break;
	}
	case TILE_COL : {
		int i, r, c;
		for (i = r = 0 ; r < TILE_SIZE_ROW ; ++r)
			for (c = 0 ; c < TILE_SIZE_COL ; ++c)
				res->ptr [i++] = cell_tile_style_new (
					t->style_col.style [c], TILE_SIMPLE);
		break;
	}
	case TILE_ROW : {
		int i, r, c;
		for (i = r = 0 ; r < TILE_SIZE_ROW ; ++r)
			for (c = 0 ; c < TILE_SIZE_COL ; ++c)
				res->ptr [i++] = cell_tile_style_new (
					t->style_row.style [r], TILE_SIMPLE);
		break;
	}
	case TILE_MATRIX : {
		int i = TILE_SIZE_COL * TILE_SIZE_ROW;
		while (--i >= 0)
			res->ptr [i] = cell_tile_style_new (
				t->style_simple.style [i], TILE_SIMPLE);
		break;
	}
	default : ;
	};

	return (CellTile *)res;
}

static CellTile *
cell_tile_matrix_set (CellTile *t, Range const *indic, ReplacementStyle *rs)
{
	int i, r, c;
	CellTileStyleMatrix *res;
	MStyle *tmp;

	g_return_val_if_fail (t != NULL, NULL);
	g_return_val_if_fail (TILE_SIMPLE <= t->type &&
			      TILE_MATRIX >= t->type, NULL);

	res = (CellTileStyleMatrix *)((t->type != TILE_MATRIX)
		? cell_tile_style_new (NULL, TILE_MATRIX) : t);

	switch (t->type) {
	case TILE_SIMPLE :
		mstyle_ref_multiple (tmp = t->style_simple.style [0],
				     i = TILE_SIZE_COL * TILE_SIZE_ROW);
		while (--i >= 0)
			res->style [i] = tmp;
		break;

	case TILE_COL :
		for (i = r = 0 ; r < TILE_SIZE_ROW ; ++r)
			for (c = 0 ; c < TILE_SIZE_COL ; ++c)
				mstyle_ref (res->style [i++] =
					    t->style_col.style [c]);
		break;
	case TILE_ROW :
		for (i = r = 0 ; r < TILE_SIZE_ROW ; ++r) {
			mstyle_ref_multiple (tmp = t->style_row.style [r],
					     TILE_SIZE_COL);
			for (c = 0 ; c < TILE_SIZE_COL ; ++c)
				res->style [i++] = tmp;
		}
		break;
	case TILE_MATRIX :
	default :
		break;
	};

	if (t->type != TILE_MATRIX)
		cell_tile_dtor (t);

	if (indic != NULL) {
		MStyle **style = res->style;
		r = indic->start.row;
		style += r*TILE_SIZE_COL;
		for ( ;r <= indic->end.row ; ++r, style += TILE_SIZE_COL)
			for (c = indic->start.col ; c <= indic->end.col ; ++c)
				rstyle_apply (style + c, rs);
	}

	return (CellTile *)res;
}

/****************************************************************************/

struct _SheetStyleData {
	GHashTable *style_hash;
	CellTile   *styles;
	MStyle	   *default_style;
};

void
sheet_style_init (Sheet *sheet)
{
	/* some simple sanity checks */
	g_assert (SHEET_MAX_COLS <= TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL);
	g_assert (SHEET_MAX_ROWS <= TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW);
	g_return_if_fail (IS_SHEET (sheet));

	sheet->style_data = g_new (SheetStyleData, 1);
	sheet->style_data->style_hash =
		g_hash_table_new (mstyle_hash, (GCompareFunc) mstyle_equal);
	sheet->style_data->default_style = mstyle_new_default ();
	sheet->style_data->styles =
		cell_tile_style_new (sheet->style_data->default_style,
				     TILE_SIMPLE);
}

void
sheet_style_shutdown (Sheet *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->style_data != NULL);

	cell_tile_dtor (sheet->style_data->styles);
	sheet->style_data->styles = NULL;

	g_hash_table_destroy (sheet->style_data->style_hash);
	sheet->style_data->style_hash = NULL;

	mstyle_unref (sheet->style_data->default_style);

	g_free (sheet->style_data);
	sheet->style_data = NULL;
}

/****************************************************************************/

static MStyle *
vector_apply_pstyle (MStyle **styles, int n, ReplacementStyle *rs)
{
	gboolean is_uniform = TRUE;
	MStyle *prev = NULL;

	while (--n >= 0) {
		rstyle_apply (styles + n, rs);
		if (is_uniform) {
			if (prev == NULL)
				prev = styles [n];
			else if (prev != styles [n])
				is_uniform = FALSE;
		}
	}
	return is_uniform ? prev : NULL;
}

static gboolean
col_indicies (int corner_col, int w, Range const *apply_to,
	      int *first_index, int *last_index)
{
	int i, tmp;

	i = apply_to->start.col - corner_col;
	tmp = i / w;
	if (i != (tmp * w))
		return FALSE;
	*first_index = (i >= 0) ? tmp : 0;

	i = 1 + apply_to->end.col - corner_col;
	tmp = i / w;
	if (i != (tmp * w))
		return FALSE;
	*last_index = (tmp <= TILE_SIZE_COL) ? (tmp-1) : TILE_SIZE_COL -1;

	return TRUE;
}

static gboolean
row_indicies (int corner_row, int h, Range const *apply_to,
	      int *first_index, int *last_index)
{
	int i, tmp;

	i = apply_to->start.row - corner_row;
	tmp = i / h;
	if (i != (tmp * h))
		return FALSE;
	*first_index = (i >= 0) ? tmp : 0;

	i = 1 + apply_to->end.row - corner_row;
	tmp = i / h;
	if (i != (tmp * h))
		return FALSE;
	*last_index = (tmp <= TILE_SIZE_ROW) ? (tmp-1) : TILE_SIZE_ROW -1;

	return TRUE;
}

/**
 * cell_tile_apply : This is the primary logic for making changing areas in the
 * tree.  It could be further optimised if it becomes a bottle neck.
 */
static void
cell_tile_apply (CellTile **tile, int level,
		 int corner_col, int corner_row,
		 Range const *apply_to,
		 ReplacementStyle *rs)
{
	int const width = tile_widths [level+1];
	int const height = tile_heights [level+1];
	int const w = tile_widths [level];
	int const h = tile_heights [level];
	gboolean const full_width = (apply_to->start.col <= corner_col &&
				     apply_to->end.col >= (corner_col+width-1));
	gboolean const full_height = (apply_to->start.row <= corner_row &&
				      apply_to->end.row >= (corner_row+height-1));
	Range indic;
	CellTile *res = NULL;
	CellTileType type;
	int c, r, i;

	g_return_if_fail (TILE_TOP_LEVEL >= level && level >= 0);
	g_return_if_fail (tile != NULL);
	g_return_if_fail (*tile != NULL);

	type = (*tile)->type;
	g_return_if_fail (TILE_SIMPLE <= type && type <= TILE_PTR_MATRIX);

	/* applying the same style to part of a simple-tile is a nop */
	if (type == TILE_SIMPLE && 
	    (*tile)->style_simple.style [0] == rs->new_style)
		return;

	/* Apply new style over top of the entire tile */
	if (full_width && full_height) {
		if (type == TILE_SIMPLE) {
			rstyle_apply ((*tile)->style_simple.style, rs);
			return;
		}
		if (rs->new_style != NULL) {
			res = cell_tile_style_new (rs->new_style,
						   (type = TILE_SIMPLE));
			cell_tile_dtor (*tile);
			*tile = res;
		}
		if (TILE_SIMPLE <= type && type <= TILE_MATRIX) {
			MStyle *uniform = vector_apply_pstyle (
				(*tile)->style_any.style, tile_size [type], rs);
			if (uniform == NULL)
				return;

			res = cell_tile_style_new (uniform, TILE_SIMPLE);
			cell_tile_dtor (*tile);
			*tile = res;
			return;
		}
	} else if (full_height) {
		if (col_indicies (corner_col, w, apply_to,
				  &indic.start.col, &indic.end.col)) {
			if (type == TILE_SIMPLE) {
				res = cell_tile_style_new (
					(*tile)->style_simple.style [0],
					(type = TILE_COL));
				cell_tile_dtor (*tile);
				*tile = res;
			}
			if (type == TILE_COL) {
				int i = indic.start.col;
				for (;i <= indic.end.col ; ++i)
					rstyle_apply ((*tile)->style_col.style + i, rs);
				return;
			}
			if (type != TILE_PTR_MATRIX) {
				indic.start.row = 0;
				indic.end.row = TILE_SIZE_ROW - 1;
				*tile = cell_tile_matrix_set (*tile, &indic, rs);
				return;
			}
		}
	} else if (full_width) {
		if (row_indicies (corner_row, h, apply_to,
				  &indic.start.row, &indic.end.row)) {
			if (type == TILE_SIMPLE) {
				res = cell_tile_style_new (
					(*tile)->style_simple.style [0],
					(type = TILE_ROW));
				cell_tile_dtor (*tile);
				*tile = res;
			}
			if (type == TILE_ROW) {
				int i = indic.start.row;
				for (;i <= indic.end.row ; ++i)
					rstyle_apply ((*tile)->style_row.style + i, rs);
				return;
			}
			if (type != TILE_PTR_MATRIX) {
				indic.start.col = 0;
				indic.end.col = TILE_SIZE_COL - 1;
				*tile = cell_tile_matrix_set (*tile, &indic, rs);
				return;
			}
		}
	} else {
		if (col_indicies (corner_col, w, apply_to,
				  &indic.start.col, &indic.end.col) &&
		    row_indicies (corner_row, h, apply_to,
				  &indic.start.row, &indic.end.row) &&
		    type != TILE_PTR_MATRIX) {
			*tile = cell_tile_matrix_set (*tile, &indic, rs);
			return;
		}
	}

	if (res == NULL && type != TILE_PTR_MATRIX) {
		type = TILE_PTR_MATRIX;
		res = cell_tile_ptr_matrix_new (*tile);
		cell_tile_dtor (*tile);
		*tile = res;
	}

	/* drill down */
	g_return_if_fail (type == TILE_PTR_MATRIX);
	level--;
	for (i = r = 0 ; r < TILE_SIZE_ROW ; ++r, i += TILE_SIZE_COL) {
		int const cr = corner_row + h*r;
		if (cr > apply_to->end.row)
			break;
		if ((cr + h) <= apply_to->start.row)
			continue;

		for (c = 0 ; c < TILE_SIZE_COL ; ++c) {
			int const cc = corner_col + w*c;
			if (cc > apply_to->end.col)
				break;
			if ((cc + w) <= apply_to->start.col)
				continue;

			/* TODO : we could be smarter and merge things
			 * if the sub tiles become uniform
			 */
			cell_tile_apply ((*tile)->ptr_matrix.ptr + i + c,
					 level, cc, cr, apply_to, rs);
		}
	}
}

static void
foreach_tile (CellTile *tile, int level,
	      int corner_col, int corner_row,
	      Range const *apply_to,
	      void (*handler) (MStyle *style,
			       int corner_col, int corner_row, int width, int height,
			       Range const *apply_to, gpointer user),
	      gpointer user)
{
	int const width = tile_widths [level+1];
	int const height = tile_heights [level+1];
	int const w = tile_widths [level];
	int const h = tile_heights [level];
	int c, r, i, last;

	g_return_if_fail (TILE_TOP_LEVEL >= level && level >= 0);
	g_return_if_fail (tile != NULL);

	switch (tile->type) {
	case TILE_SIMPLE :
	(*handler) (tile->style_simple.style [0],
		    corner_col, corner_row, width, height,
		    apply_to, user);
	break;

	case TILE_COL :
	if (apply_to != NULL) {
		c    = (apply_to->start.col - corner_col) / w;
		if (c < 0)
			c = 0;
		last = (apply_to->end.col - corner_col) / w + 1;
		if (last > TILE_SIZE_COL)
			last = TILE_SIZE_COL;
	} else {
		c = 0;
		last = TILE_SIZE_COL;
	}
	for (; c < last ; ++c)
		(*handler) (tile->style_col.style [c],
			    corner_col + c*w, corner_row, w, height,
			    apply_to, user);
	break;

	case TILE_ROW :
	if (apply_to != NULL) {
		r    = (apply_to->start.row - corner_row) / h;
		if (r < 0)
			r = 0;
		last = (apply_to->end.row - corner_row) / h + 1;
		if (last > TILE_SIZE_ROW)
			last = TILE_SIZE_ROW;
	} else {
		r = 0;
		last = TILE_SIZE_ROW;
	}
	for (; r < last ; ++r)
		(*handler) (tile->style_row.style [r],
			    corner_col, corner_row + r*h, width, h,
			    apply_to, user);
	break;

	case TILE_MATRIX :
	case TILE_PTR_MATRIX :
	for (i = r = 0 ; r < TILE_SIZE_ROW ; ++r, i += TILE_SIZE_COL) {
		int const cr = corner_row + h*r;
		if (apply_to) {
			if (cr > apply_to->end.row)
				break;
			if ((cr + h) <= apply_to->start.row)
				continue;
		}

		for (c = 0 ; c < TILE_SIZE_COL ; ++c) {
			int const cc = corner_col + w*c;
			if (apply_to) {
				if (cc > apply_to->end.col)
					break;
				if ((cc + w) <= apply_to->start.col)
					continue;
			}

			if (tile->type == TILE_MATRIX) {
				(*handler) (tile->style_matrix.style [r*TILE_SIZE_COL+c],
					    corner_col + c*w, corner_row + r*h,
					    w, h, apply_to, user);
			} else {
				foreach_tile (
				      tile->ptr_matrix.ptr [c + r*TILE_SIZE_COL],
				      level-1, cc, cr, apply_to, handler, user);
			}
		}
	}
	break;

	default :
		g_warning ("Adaptive Quad Tree corruption !");
	}
}

/**
 * cell_tile_apply_pos : This is an simplified version of cell_tile_apply.  It
 * does not need all the bells and whistles because it operates on single cells.
 */
static void
cell_tile_apply_pos (CellTile **tile, int level,
		     int col, int row,
		     ReplacementStyle *rs)
{
	CellTile *tmp;
	CellTileType type;

	g_return_if_fail (col >= 0);
	g_return_if_fail (col < SHEET_MAX_COLS);
	g_return_if_fail (row >= 0);
	g_return_if_fail (row < SHEET_MAX_ROWS);

tail_recursion :
	g_return_if_fail (TILE_TOP_LEVEL >= level && level >= 0);
	g_return_if_fail (tile != NULL);
	g_return_if_fail (*tile != NULL);

	tmp = *tile;
	type = tmp->type;
	g_return_if_fail (TILE_SIMPLE <= type && type <= TILE_PTR_MATRIX);

	if (level > 0) {
		int const w = tile_widths [level];
		int const c = col / w;
		int const h = tile_heights [level];
		int const r = row / h;

		if (type != TILE_PTR_MATRIX) {
			tmp = cell_tile_ptr_matrix_new (tmp);
			cell_tile_dtor (*tile);
			*tile = tmp;
		}
		tile = tmp->ptr_matrix.ptr + r * TILE_SIZE_COL + c;
		level--;
		col -= c*w;
		row -= r*h;
		goto tail_recursion;
	} else if (type != TILE_MATRIX)
		*tile = tmp = cell_tile_matrix_set (tmp, NULL, NULL);

	g_return_if_fail (tmp->type == TILE_MATRIX);
	rstyle_apply (tmp->style_matrix.style + row*TILE_SIZE_COL + col, rs);
}

/**
 * sheet_style_set_range
 *
 * @sheet :
 * @range :
 * @style
 *
 * Change the complete style for a region.
 * This function absorbs a reference to the new @style.
 */
void
sheet_style_set_range (Sheet *sheet, Range const *range,
		       MStyle *style)
{
	ReplacementStyle rs;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);

	cell_tile_apply (&sheet->style_data->styles,
			 TILE_TOP_LEVEL, 0, 0,
			 range, rstyle_ctor (&rs, style, NULL));
	mstyle_unref (style);
}

/**
 * sheet_style_set_pos :
 *
 * @sheet :
 * @col   :
 * @row   :
 * @style :
 *
 * Change the complete style for a single cell.
 * This function absorbs a reference to the the new @style.
 */
void
sheet_style_set_pos (Sheet *sheet, int col, int row,
		     MStyle *style)
{
	ReplacementStyle rs;

	g_return_if_fail (IS_SHEET (sheet));

	cell_tile_apply_pos (&sheet->style_data->styles,
			     TILE_TOP_LEVEL, col, row,
			     rstyle_ctor (&rs, style, NULL));
	mstyle_unref (style);
}

/**
 * sheet_style_default :
 *
 * @sheet :
 *
 * Return the default style for a sheet.
 * NOTE : This does NOT add a reference.
 */
MStyle *
sheet_style_default (Sheet const *sheet)
{
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (sheet->style_data != NULL, NULL);

	mstyle_ref (sheet->style_data->default_style);
	return sheet->style_data->default_style;
}

/**
 * sheet_style_get :
 *
 * @sheet :
 * @col   :
 * @row   :
 *
 * Find the fully qualified style applicable to the specified cellpos.
 */
MStyle *
sheet_style_get (Sheet const *sheet, int col, int row)
{
	int width = TILE_SIZE_COL*TILE_SIZE_COL*TILE_SIZE_COL;
	int height = TILE_SIZE_ROW*TILE_SIZE_ROW*TILE_SIZE_ROW;
	int c, r, level = TILE_TOP_LEVEL;
	CellTile *tile = sheet->style_data->styles;

tail_recursion :
	c = col / width;
	r = row / height;

	g_return_val_if_fail (tile != NULL, NULL);
	g_return_val_if_fail (0 <= c && c < TILE_SIZE_COL, NULL);
	g_return_val_if_fail (0 <= r && r < TILE_SIZE_ROW, NULL);

	switch (tile->type) {
	case TILE_SIMPLE : return tile->style_simple.style [0];
	case TILE_COL :	   return tile->style_col.style [c];
	case TILE_ROW :	   return tile->style_row.style [r];
	case TILE_MATRIX : return tile->style_matrix.style [r*TILE_SIZE_COL+c];

	case TILE_PTR_MATRIX :
		g_return_val_if_fail (level > 0, NULL);

		level--;
		tile = tile->ptr_matrix.ptr [r*TILE_SIZE_COL + c];
		col -= c * width;
		row -= r * height;
		width /= TILE_SIZE_COL;
		height /= TILE_SIZE_ROW;
		goto tail_recursion;

	default :
		break;
	};

	g_warning ("Adaptive Quad Tree corruption !");
	return NULL;
}

#define border_null(b)	((b) == none || (b) == NULL)

static inline void
style_row (MStyle *style, int start_col, int end_col, StyleRow *sr)
{
	StyleBorder const *top, *bottom, *none = style_border_none ();
	StyleBorder const *left, *right, *v;
	int const end = MIN (end_col, sr->end_col);
	int i = MAX (start_col, sr->start_col);

	top = mstyle_get_border (style, MSTYLE_BORDER_TOP);
	bottom = mstyle_get_border (style, MSTYLE_BORDER_BOTTOM);
	left = mstyle_get_border (style, MSTYLE_BORDER_LEFT);
	right = mstyle_get_border (style, MSTYLE_BORDER_RIGHT);

	/* Cancel grids if there is a background */
	if (sr->hide_grid || mstyle_get_pattern (style) > 0) {
		if (top == none)
			top = NULL;
		if (bottom == none)
			bottom = NULL;
		if (left == none)
			left = NULL;
		if (right == none)
			right = NULL;
	}

	if (left != none && border_null (sr->vertical [i]))
		sr->vertical [i] = left;
	v = (right != none) ? right : left;

	while (i <= end) {
		sr->styles [i] = style;
		if (top != none && border_null (sr->top [i]))
			sr->top [i] = top;
		sr->bottom [i] = bottom;
		sr->vertical [++i] = v;
	}
	if (right == none)
		sr->vertical [i] = none;
}

static void
get_style_row (CellTile const *tile, int level,
	       int corner_col, int corner_row,
	       StyleRow *sr)
{
	int const width = tile_widths [level+1];
	int const w = tile_widths [level];
	int const h = tile_heights [level];
	int r = 0;
	CellTileType t;

	g_return_if_fail (TILE_TOP_LEVEL >= level && level >= 0);
	g_return_if_fail (tile != NULL);

	t = tile->type;

	if (t != TILE_SIMPLE && t != TILE_COL) {
		r = (sr->row > corner_row) ? (sr->row - corner_row)/ h : 0;
		g_return_if_fail (r < TILE_SIZE_ROW);
	}

	if (t == TILE_ROW || t == TILE_SIMPLE) {
		style_row (tile->style_any.style [r],
			   corner_col, corner_col + width - 1, sr);
	} else {
		/* find the start and end */
		int c;
		int last_c = (sr->end_col - corner_col) / w;
		if (last_c >= TILE_SIZE_COL)
			last_c = TILE_SIZE_COL-1;
		if (sr->start_col > corner_col) {
			c = (sr->start_col - corner_col) / w;
			corner_col += c * w;
		} else
			c = 0;

		corner_row += h*r;

		if (t != TILE_PTR_MATRIX) {
			MStyle * const *styles = tile->style_any.style + r*TILE_SIZE_COL;

			for ( ; c <= last_c ; c++, corner_col += w)
				style_row (styles [c],
					   corner_col, corner_col + w - 1, sr);
		} else {
			CellTile * const *tiles = tile->ptr_matrix.ptr + r*TILE_SIZE_COL;

			g_return_if_fail (level > 0);

			for ( level-- ; c <= last_c ; c++, corner_col += w)
				get_style_row (tiles [c], level,
					       corner_col, corner_row, sr);
		}
	}
}

/**
 * sheet_style_get_row :
 * 
 * @sheet :
 * @sr    :
 *
 * A utility routine which efficiently retrieves a range of styles within a row.
 * It also merges adjacent borders as necessary.
 */
void
sheet_style_get_row (Sheet const *sheet, StyleRow *sr)
{

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sr != NULL);
	g_return_if_fail (sr->styles != NULL);
	g_return_if_fail (sr->vertical != NULL);
	g_return_if_fail (sr->top != NULL);
	g_return_if_fail (sr->bottom != NULL);

	sr->vertical [sr->start_col] = style_border_none ();
	get_style_row (sheet->style_data->styles, TILE_TOP_LEVEL, 0, 0, sr);
}

/**
 * style_row_init :
 *
 * A small utility routine to initialize the grid drawing StyleRow data
 * structure.
 */
void
style_row_init (StyleBorder const * * *prev_vert,
		StyleRow *sr, StyleRow *next_sr,
		int start_col, int end_col, gpointer mem, gboolean hide_grid)
{
	int n, col;
	StyleBorder const *none = hide_grid ? NULL : style_border_none ();

	/* alias the arrays for easy access so that array [col] is valid
	 * for all elements start_col-1 .. end_col+1 inclusive.
	 * Note that this means that in some cases array [-1] is legal.
	 */
	n = end_col - start_col + 3; /* 1 before, 1 after, 1 fencepost */
	sr->vertical	 = mem;
	sr->vertical 	-= start_col-1;
	sr->top		 = sr->vertical + n;
	sr->bottom	 = sr->top + n;
	next_sr->top	 = sr->bottom; /* yes they should share */
	next_sr->bottom	 = next_sr->top + n;
	next_sr->vertical = next_sr->bottom + n;
	*prev_vert	 = next_sr->vertical + n;
	sr->styles	 = ((MStyle const **) (*prev_vert + n));
	next_sr->styles	 = sr->styles + n;
	sr->start_col	 = next_sr->start_col = start_col;
	sr->end_col	 = next_sr->end_col   = end_col;
	sr->hide_grid    = next_sr->hide_grid = hide_grid;

	/* Init the areas that sheet_style_get_row will not */
	for (col = start_col-1 ; col <= end_col+1; ++col)
		(*prev_vert) [col] = sr->top [col] = none;
	sr->vertical	  [start_col-1] = sr->vertical	    [end_col+1] =
	next_sr->vertical [start_col-1] = next_sr->vertical [end_col+1] =
	next_sr->top	  [start_col-1] = next_sr->top	    [end_col+1] =
	next_sr->bottom	  [start_col-1] = next_sr->bottom   [end_col+1] = none;
}

/**
 * sheet_style_apply_range :
 * @sheet :
 * @range :
 * @pstyle:
 *
 * Apply a partial style to a region.
 * The routine absorbs a reference to the partial style.
 */
void
sheet_style_apply_range (Sheet *sheet, Range const *range, MStyle *pstyle)
{
	ReplacementStyle rs;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);

	cell_tile_apply (&sheet->style_data->styles,
			 TILE_TOP_LEVEL, 0, 0,
			 range, rstyle_ctor (&rs, NULL, pstyle));
	rstyle_dtor (&rs);
	mstyle_unref (pstyle);
}

static void
apply_border (Sheet *sheet, Range const *r,
	      StyleBorderLocation side,
	      StyleBorder *border)
{
	MStyle *pstyle = mstyle_new ();
	pstyle_set_border (pstyle, border, side);
	sheet_style_apply_range (sheet, r, pstyle);
}

/**
 * sheet_style_apply_border :
 *
 * @sheet   :
 * @range   :
 * @borders :
 *
 * When a user applies a border to a region we attempt to remove the border
 * from the opposing side to avoid overlapping border specifications.
 * eg
 * if we apply a top border to a range, we would clear the bottom border
 * of the range offset upwards.
 */
void
sheet_style_apply_border (Sheet       *sheet,
			  Range const *range,
			  StyleBorder **borders)
{
	MStyle *pstyle = NULL;

	if (borders == NULL)
		return;

	if (borders [STYLE_BORDER_TOP]) {
		/* 1.1 top inner */
		Range r = *range;
		r.end.row = r.start.row;
		apply_border (sheet, &r, STYLE_BORDER_TOP,
			      borders [STYLE_BORDER_TOP]);

		/* 1.2 top outer */
		r.start.row--;
		if (r.start.row >= 0) {
			r.end.row = r.start.row;
			apply_border (sheet, &r, STYLE_BORDER_BOTTOM,
				      style_border_none ());
		}
	}

	if (borders [STYLE_BORDER_BOTTOM]) {
		/* 2.1 bottom inner */
		Range r = *range;
		r.start.row = r.end.row;
		apply_border (sheet, &r, STYLE_BORDER_BOTTOM,
			      borders [STYLE_BORDER_BOTTOM]);

		/* 2.2 bottom outer */
		r.end.row++;
		if (r.end.row < (SHEET_MAX_ROWS-1)) {
			r.start.row = r.end.row;
			apply_border (sheet, &r, STYLE_BORDER_TOP,
				      style_border_none ());
		}
	}

	if (borders [STYLE_BORDER_LEFT]) {
		/* 3.1 left inner */
		Range r = *range;
		r.end.col = r.start.col;
		apply_border (sheet, &r, STYLE_BORDER_LEFT,
			      borders [STYLE_BORDER_LEFT]);

		/* 3.2 left outer */
		r.start.col--;
		if (r.start.col >= 0) {
			r.end.col = r.start.col;
			apply_border (sheet, &r, STYLE_BORDER_RIGHT,
				      style_border_none ());
		}
	}

	if (borders [STYLE_BORDER_RIGHT]) {
		/* 4.1 right inner */
		Range r = *range;
		r.start.col = r.end.col;
		apply_border (sheet, &r, STYLE_BORDER_RIGHT,
			      borders [STYLE_BORDER_RIGHT]);

		/* 4.2 right outer */
		r.end.col++;
		if (r.end.col < (SHEET_MAX_COLS-1)) {
			r.start.col = r.end.col;
			apply_border (sheet, &r, STYLE_BORDER_LEFT,
				      style_border_none ());
		}
	}

	/* Interiors horizontal : prefer top */
	if (borders [STYLE_BORDER_HORIZ] != NULL) {
		/* 5.1 horizontal interior top */
		if (range->start.row != range->end.row) {
			Range r = *range;
			++r.start.row;
			apply_border (sheet, &r, STYLE_BORDER_TOP,
				      borders [STYLE_BORDER_HORIZ]);
		}
		/* 5.2 interior bottom */
		if (range->start.row != range->end.row) {
			Range r = *range;
			--r.end.row;
			apply_border (sheet, &r, STYLE_BORDER_BOTTOM,
				      style_border_none ());
		}
	}

	/* Interiors vertical : prefer left */
	if (borders [STYLE_BORDER_VERT] != NULL) {
		/* 6.1 vertical interior left */
		if (range->start.col != range->end.col) {
			Range r = *range;
			++r.start.col;
			apply_border (sheet, &r, STYLE_BORDER_LEFT,
				      borders [STYLE_BORDER_VERT]);
		}

		/* 6.2 The vertical interior right */
		if (range->start.col != range->end.col) {
			Range r = *range;
			--r.end.col;
			apply_border (sheet, &r, STYLE_BORDER_RIGHT,
				      style_border_none ());
		}
	}

	/* 7. Diagonals (apply both in one pass) */
	if (borders [STYLE_BORDER_DIAG] != NULL) {
		pstyle = mstyle_new ();
		pstyle_set_border (pstyle, borders [STYLE_BORDER_DIAG],
				   STYLE_BORDER_DIAG);
	}
	if (borders [STYLE_BORDER_REV_DIAG]) {
		if (pstyle == NULL)
			pstyle = mstyle_new ();
		pstyle_set_border (pstyle, borders [STYLE_BORDER_REV_DIAG],
				   STYLE_BORDER_REV_DIAG);
	}
	if (pstyle != NULL)
		sheet_style_apply_range (sheet, range, pstyle);
}

/****************************************************************************/

static void
cb_filter_style (MStyle *style,
		 int corner_col, int corner_row, int width, int height,
		 Range const *apply_to, gpointer user)
{
	MStyle *accumulator = user;
	mstyle_compare (accumulator, style);
}

static void
border_mask_internal (gboolean *known, StyleBorder **borders,
		      StyleBorder const *b, StyleBorderLocation l)
{
	if (!known [l]) {
		known [l] = TRUE;
		borders [l] = (StyleBorder *)b;
		style_border_ref (borders [l]);
	} else if (borders [l] != b && borders [l] != NULL) {
		style_border_unref (borders [l]);
		borders [l] = NULL;
	}
}

static void
border_mask (gboolean *known, StyleBorder **borders,
	     StyleBorder const *b, StyleBorderLocation l)
{
	if (b == NULL)
		b = style_border_none ();
	border_mask_internal (known, borders, b, l);
}

static void
border_mask_vec (gboolean *known, StyleBorder **borders,
		 StyleBorder const * const *vec, int first, int last,
		 StyleBorderLocation l)
{
	StyleBorder const *b = vec [first];

	if (b == NULL)
		b = style_border_none ();
	while (first++ < last) {
		StyleBorder const *tmp = vec [first];
		if (tmp == NULL)
			tmp = style_border_none ();
		if (b != tmp) {
			b = NULL;
			break;
		}
	}

	border_mask_internal (known, borders, b, l);
}

/**
 * sheet_style_get_uniform :
 *
 * @sheet   :
 * @range   :
 * @borders :
 *
 * Find out what style elements are common to every cell in a range.
 */
void
sheet_style_get_uniform	(Sheet const *sheet, Range const *r,
			 MStyle **style, StyleBorder **borders)
{
	int n, col, row, start_col, end_col;
	StyleRow sr;
	StyleBorderLocation i;
	gboolean known [STYLE_BORDER_EDGE_MAX];
	StyleBorder const *none = style_border_none ();

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (r != NULL);
	g_return_if_fail (style != NULL);
	g_return_if_fail (borders != NULL);

	/* init style set with a copy of the top left corner of the 1st range */
	if (*style == NULL) {
		MStyle const *tmp;

		tmp = sheet_style_get (sheet, r->start.col, r->start.row);
		*style = mstyle_copy (tmp);
		for (i = STYLE_BORDER_TOP ; i < STYLE_BORDER_EDGE_MAX ; i++) {
			known [i] = FALSE;
			borders [i] = style_border_ref ((StyleBorder *)none);
		}
	} else {
		for (i = STYLE_BORDER_TOP ; i < STYLE_BORDER_EDGE_MAX ; i++)
			known [i] = TRUE;
	}

	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, r,
		      cb_filter_style, *style);

	/* copy over the diagonals */
	for (i = STYLE_BORDER_REV_DIAG ; i <= STYLE_BORDER_DIAG ; i++)
		if (!mstyle_is_element_conflict (*style, MSTYLE_BORDER_TOP+i))
			borders [i] = style_border_ref (
				mstyle_get_border (*style, MSTYLE_BORDER_TOP+i));
		else
			borders [i] = NULL;

	start_col = r->start.col;
	if (r->start.col > 0)
		start_col--;
	end_col = r->end.col;
	if (r->end.col < SHEET_MAX_COLS)
		end_col++;

	/* allocate then alias the arrays for easy access */
	n = end_col - start_col + 2;
	sr.vertical	 = (StyleBorder const **)g_alloca (n *
			    (3 * sizeof (StyleBorder const *) +
			     sizeof (MStyle const *)));
	sr.top	      = sr.vertical + n;
	sr.bottom     = sr.top + n;
	sr.styles     = ((MStyle const **) (sr.bottom + n));
	sr.vertical  -= start_col; sr.top     -= start_col;
	sr.bottom    -= start_col; sr.styles  -= start_col;
	sr.start_col  = start_col; sr.end_col  = end_col;
	sr.hide_grid  = sheet->hide_grid;

	/* pretend the previous bottom had no borders */
	for (col = start_col ; col <= end_col; ++col)
		sr.top [col] = none;

	/* merge the bottom of the previous row */
 	if (r->start.row > 0) {
		StyleBorder const ** roller;
		sr.row = r->start.row - 1;
		sheet_style_get_row (sheet, &sr);
		roller = sr.top; sr.top = sr.bottom; sr.bottom = roller;
	}

	/*
	 * TODO : The border handling is tricky and currently VERY slow for
	 * large ranges.  We could easily optimize this.  There is no need to
	 * retrieve the style in every cell just to do a filter for uniformity
	 * by row.  One day we should do a special case version of
	 * sheet_style_get_row probably style_get_uniform_col (this will be
	 * faster)
	 */
	for (row = r->start.row ; row <= r->end.row ; row++) {
		StyleBorder const **roller;
		sr.row = row;
		sheet_style_get_row (sheet, &sr);

		border_mask (known, borders, sr.vertical [r->start.col],
			     STYLE_BORDER_LEFT);
		border_mask (known, borders, sr.vertical [r->end.col+1],
			     STYLE_BORDER_RIGHT);
		border_mask_vec (known, borders, sr.top,
				 r->start.col, r->end.col, (row == r->start.row)
				 ? STYLE_BORDER_TOP : STYLE_BORDER_HORIZ);
		if (r->start.col != r->end.col)
			border_mask_vec (known, borders, sr.vertical,
					 r->start.col+1, r->end.col,
					 STYLE_BORDER_VERT);

		roller = sr.top; sr.top = sr.bottom; sr.bottom = roller;
	}

	/* merge the top of the next row */
 	if (r->end.row < (SHEET_MAX_ROWS-1)) {
		sr.row = r->end.row + 1;
		sheet_style_get_row (sheet, &sr);
	}
	border_mask_vec (known, borders, sr.top, r->start.col, r->end.col,
			 STYLE_BORDER_BOTTOM);
}

/**
 * sheet_style_relocate
 *
 * @rinfo :
 *
 * Slide the styles from the origin region to the new position.
 */
void
sheet_style_relocate (ExprRelocateInfo const *rinfo)
{
	CellPos corner;
	StyleList *styles;

	g_return_if_fail (rinfo != NULL);

	styles = sheet_style_get_list (rinfo->origin_sheet, &rinfo->origin);

	sheet_style_set_range (rinfo->origin_sheet, &rinfo->origin,
			       sheet_style_default (rinfo->origin_sheet));
	corner.col = rinfo->origin.start.col + rinfo->col_offset;
	corner.row = rinfo->origin.start.row + rinfo->row_offset;
	sheet_style_set_list (rinfo->target_sheet, &corner, FALSE, styles);
	style_list_free	(styles);
}

/**
 * sheet_style_insert_colrow 
 *
 * @rinfo :
 *
 * A utility routine to give the effect of stretching the styles when a col/row
 * is inserted.  This is done by applying the styles from the left/top col/row
 * to the new region.
 */
void
sheet_style_insert_colrow (ExprRelocateInfo const *rinfo)
{
	CellPos corner;
	StyleList *ptr, *styles = NULL;
	Range r;

	g_return_if_fail (rinfo != NULL);
	g_return_if_fail (rinfo->origin_sheet == rinfo->target_sheet);

	/* 1) copy col/row to the top/left of the region, and extend it */
	corner = rinfo->origin.start;
	if (rinfo->col_offset != 0) {
		int const o = rinfo->col_offset - 1;
		int col = corner.col - 1;

		if (col < 0)
			col = 0;
		corner.row = 0;
		styles = sheet_style_get_list (rinfo->origin_sheet,
			       range_init (&r, col, 0, col, SHEET_MAX_ROWS-1));
		if (o > 0)
			for (ptr = styles ; ptr != NULL ; ptr = ptr->next)
				((StyleRegion *)ptr->data)->range.end.col = o;

	} else if (rinfo->row_offset != 0) {
		int const o = rinfo->row_offset - 1;
		int row = corner.row - 1;
		if (row < 0)
			row = 0;
		corner.col = 0;
		styles = sheet_style_get_list (rinfo->origin_sheet,
			       range_init (&r, 0, row, SHEET_MAX_COLS-1, row));
		if (o > 0)
			for (ptr = styles ; ptr != NULL ; ptr = ptr->next)
				((StyleRegion *)ptr->data)->range.end.row = o;
	}

	sheet_style_relocate (rinfo);

	if (styles != NULL) {
		sheet_style_set_list (rinfo->target_sheet, &corner, FALSE, styles);
		style_list_free	(styles);
	}
}

static void
cb_style_extent (MStyle *style,
		 int corner_col, int corner_row, int width, int height,
		 Range const *apply_to, gpointer user)
{
	if (mstyle_visible_in_blank (style)) {
		Range *r = (Range *)user;
		int tmp;

		tmp = corner_col+width-1;
		if (r->end.col < tmp)
			r->end.col = tmp;
		tmp = corner_row+height-1;
		if (r->end.row < tmp)
			r->end.row = tmp;
	}
}

static void
cb_visible_content (MStyle *style,
		    int corner_col, int corner_row, int width, int height,
		    Range const *apply_to, gpointer res)
{
	*((gboolean *)res) |= mstyle_visible_in_blank (style);
}

/**
 * sheet_style_has_visible_content :
 *
 * @sheet :
 * @r     :
 *
 * Are any of the styles in the target region visible in a blank cell.  The
 * implementation is simplistic.  We should really ignore borders at the
 * edges IF they have been seen before.
 */
gboolean
sheet_style_has_visible_content (Sheet const *sheet, Range *src)
{ 
	gboolean res = FALSE;
	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, src,
		      cb_visible_content, &res);
	return res;
}

/**
 * sheet_style_get_extent :
 *
 * @sheet :
 * @r     :
 *
 * A simple implementation that find the max lower and right styles that are
 * visible.
 */
void
sheet_style_get_extent (Sheet const *sheet, Range *res)
{ 
	Range r;

	/* This could easily be optimized */
	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, range_init_full_sheet(&r),
		      cb_style_extent, res);
}

/****************************************************************************/

static StyleRegion *
style_region_new (Range const *range, MStyle *mstyle)
{
	StyleRegion *sr;

	sr = g_new (StyleRegion, 1);
	sr->range = *range;
	sr->style = mstyle;
	mstyle_ref (mstyle);

	return sr;
}

static void
style_region_free (StyleRegion *sr)
{
	g_return_if_fail (sr != NULL);

	mstyle_unref (sr->style);
	sr->style = NULL;
	g_free (sr);
}

static void
cb_style_list_add_node (MStyle *style,
			int corner_col, int corner_row, int width, int height,
			Range const *apply_to, gpointer user)
{
	GHashTable *cache = user;
	StyleRegion *sr = NULL;
	CellPos	key;
	Range range;

	range.start.col = corner_col;
	range.start.row = corner_row;
	range.end.col = corner_col + width - 1;
	range.end.row = corner_row + height - 1;

	if (apply_to) {
		range.start.col -= apply_to->start.col;
		if (range.start.col < 0)
			range.start.col = 0;
		range.start.row -= apply_to->start.row;
		if (range.start.row < 0)
			range.start.row = 0;

		if (range.end.col > apply_to->end.col)
			range.end.col = apply_to->end.col;
		range.end.col -= apply_to->start.col;
		if (range.end.row > apply_to->end.row)
			range.end.row = apply_to->end.row;
		range.end.row -= apply_to->start.row;
	}
#ifdef DEBUG_STYLE_LIST
	range_dump (&range, " <= Add node \n");
#endif

	/* Do some simple minded merging vertically */
	key.col = range.end.col;
	key.row = range.start.row - 1;
	if (key.row >= 0 &&
	    (sr = (StyleRegion *)g_hash_table_lookup (cache, &key)) != NULL &&
	    sr->range.start.col == range.start.col && mstyle_equal (sr->style, style)) {
		g_hash_table_remove (cache, &key);
		sr->range.end.row = range.end.row;
	} else
		sr = style_region_new (&range, style);

	g_hash_table_insert (cache, &sr->range.end, sr);
}

static gboolean
cb_hash_merge_horiz (gpointer hash_key, gpointer value, gpointer user_data)
{
	GHashTable *cache = user_data;
	StyleRegion *sr = value, *srh;
	CellPos	key;

#ifdef DEBUG_STYLE_LIST
	range_dump (&sr->range, "\n");
#endif

	/* Already merged */
	if (sr->range.start.col < 0) {
		style_region_free (sr);
		return TRUE;
	}

	/* Do some simple minded merging horizontally */
	key.row = sr->range.end.row;
	do {
		key.col = sr->range.start.col - 1;
		if (key.col >= 0 &&
		    (srh = (StyleRegion *)g_hash_table_lookup (cache, &key)) != NULL &&
		    srh->range.start.row == sr->range.start.row && mstyle_equal (sr->style, srh->style)) {
			g_return_val_if_fail (srh->range.start.col >= 0, FALSE);
			sr->range.start.col = srh->range.start.col;
			srh->range.start.col = -1;
		} else
			return FALSE;
	} while (1);
	return FALSE; /* stupid compilers */
}

static gboolean
cb_hash_to_list (gpointer key, gpointer	value, gpointer	user_data)
{
	StyleList **res = user_data;
	StyleRegion *sr = value;

#ifdef DEBUG_STYLE_LIST
	range_dump (&sr->range, "\n");
#endif

	/* Already merged */
	if (sr->range.start.col < 0) {
		style_region_free (sr);
		return TRUE;
	}

	*res = g_list_prepend (*res, value);
	return FALSE;
}

/**
 * sheet_style_get_list :
 *
 * @sheet :
 * @range :
 *
 * Get a list of rectangles and their associated styles
 * Caller is responsible for freeing.
 */
StyleList *
sheet_style_get_list (Sheet const *sheet, Range const *r)
{
	StyleList *res = NULL;
	GHashTable *cache = g_hash_table_new ((GHashFunc)&cellpos_hash,
					      (GCompareFunc)&cellpos_cmp);
	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, r,
		      cb_style_list_add_node, cache);
#ifdef DEBUG_STYLE_LIST
	fprintf(stderr, "=========\n");
#endif
	g_hash_table_foreach_remove (cache, cb_hash_merge_horiz, cache);
	g_hash_table_foreach_remove (cache, cb_hash_to_list, &res);
#ifdef DEBUG_STYLE_LIST
	fprintf(stderr, "=========\n");
#endif
	g_hash_table_destroy (cache);

	return res;
}

/**
 * sheet_style_set_list 
 *
 * @sheet     :
 * @corner    :
 * @transpose :
 * @list      :
 *
 * Applies a list of styles to the sheet with the supplied offset.  Optionally
 * transposing the ranges
 */
SpanCalcFlags
sheet_style_set_list (Sheet *sheet, CellPos const *corner,
		      gboolean transpose, GList const *list)
{
	SpanCalcFlags spanflags = SPANCALC_SIMPLE;
	GList const *l;

	g_return_val_if_fail (IS_SHEET (sheet), spanflags);

	/* Sluggish but simple implementation for now */
	for (l = list; l; l = l->next) {
		StyleRegion const *sr = l->data;
		Range              r  = sr->range;

		range_translate (&r, +corner->col, +corner->row);
		if (transpose)
			range_transpose (&r, corner);

		mstyle_ref (sr->style);
		sheet_style_set_range (sheet, &r, sr->style);
		spanflags |= required_updates_for_style (sr->style);
	}
	return spanflags;
}

/**
 * style_list_free :
 *
 * @list : the list to free
 *
 * Free up the ressources in the style list.  Including unreferencing the
 * styles.
 */
void
style_list_free (StyleList *list)
{
	StyleList *l;

	for (l = list; l; l = l->next)
		style_region_free (l->data);
	g_list_free (list);
}

/**
 * style_list_get_style :
 *
 * @list : A style list.
 * @pos  : The offset from the upper left corner.
 *
 * Attempts to find the style associated with the @pos offset within the 0,0
 * based style list.
 * The resulting style does not have its reference count bumped.
 */
MStyle const *
style_list_get_style (StyleList const *list, CellPos const *pos)
{
	StyleList const *l;

	g_return_val_if_fail (pos != NULL, NULL);

	for (l = list; l; l = l->next) {
		StyleRegion const *sr = l->data;
		Range const *r = &sr->range;
		if (range_contains (r, pos->col, pos->row))
			return sr->style;
	}
	return NULL;
}
