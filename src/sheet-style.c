/* vim: set sw=8: */

/*
 * sheet-style.c: storage mechanism for styles and eventually cells.
 *
 * Copyright (C) 2000-2006 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
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
#include "sheet-style.h"
#include "gnm-style-impl.h"
#include "ranges.h"
#include "sheet.h"
#include "expr.h"
#include "style.h"
#include "style-border.h"
#include "style-color.h"
#include "style-conditions.h"
#include "cell.h"
#include "gutils.h"
#include <goffice/utils/go-glib-extras.h>
#include <glib/gi18n-lib.h>
#include <string.h>
#include <math.h>

#ifndef USE_TILE_POOLS
#define USE_TILE_POOLS 1
#endif

typedef union _CellTile CellTile;
struct _GnmSheetStyleData {
	GHashTable *style_hash;
	CellTile   *styles;
	GnmStyle   *default_style;
	GnmColor   *auto_pattern_color;
};

/**
 * sheet_style_unlink
 * For internal use only
 */
void
sheet_style_unlink (Sheet *sheet, GnmStyle *st)
{
	if (sheet->style_data->style_hash != NULL)
		g_hash_table_remove (sheet->style_data->style_hash, st);
}

/**
 * sheet_style_find :
 *
 * @sheet : the sheet
 * @s     : a style
 *
 * Looks up a style from the sheets collection.  Linking if necessary.
 * ABSORBS the reference and adds a link.
 */
GnmStyle *
sheet_style_find (Sheet const *sheet, GnmStyle *s)
{
	GnmStyle *res;
	res = g_hash_table_lookup (sheet->style_data->style_hash, s);
	if (res != NULL) {
		gnm_style_link (res);
		gnm_style_unref (s);
		return res;
	}

	s = gnm_style_link_sheet (s, (Sheet *)sheet);
	g_hash_table_insert (sheet->style_data->style_hash, s, s);
	return s;
}

/* Place holder until I merge in the new styles too */
static void
pstyle_set_border (GnmStyle *st, GnmBorder *border,
		   GnmStyleBorderLocation side)
{
	gnm_style_set_border (st,
			      GNM_STYLE_BORDER_LOCATION_TO_STYLE_ELEMENT (side),
			      gnm_style_border_ref (border));
}

/* Amortize the cost of applying a partial style over a large region
 * by caching and rereferencing the merged result for repeated styles.
 */
typedef struct {
	GnmStyle	   *new_style;
	GnmStyle	   *pstyle;
	GHashTable *cache;
	Sheet	   *sheet;
} ReplacementStyle;

static ReplacementStyle *
rstyle_ctor (ReplacementStyle *res, GnmStyle *new_style, GnmStyle *pstyle, Sheet *sheet)
{
	res->sheet = sheet;
	if (new_style != NULL) {
		res->new_style = sheet_style_find (sheet, new_style);
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
cb_style_unlink (gpointer key, gpointer value, gpointer user_data)
{
	gnm_style_unlink ((GnmStyle *)key);
	gnm_style_unlink ((GnmStyle *)value);
}

static void
rstyle_dtor (ReplacementStyle *rs)
{
	if (rs->cache != NULL) {
		g_hash_table_foreach (rs->cache, cb_style_unlink, NULL);
		g_hash_table_destroy (rs->cache);
		rs->cache = NULL;
	}
	if (rs->new_style != NULL) {
		gnm_style_unlink (rs->new_style);
		rs->new_style = NULL;
	}
	if (rs->pstyle != NULL) {
		gnm_style_unref (rs->pstyle);
		rs->pstyle = NULL;
	}
}

/**
 * rstyle_apply :  Utility routine that is at the core of applying partial
 * styles or storing complete styles.  It will eventually be smarter
 * and will maintain the cache of styles associated with each sheet
 */
static void
rstyle_apply (GnmStyle **old, ReplacementStyle *rs)
{
	GnmStyle *s;
	g_return_if_fail (old != NULL);
	g_return_if_fail (rs != NULL);

	if (rs->pstyle != NULL) {
		/* Cache the merged styles keeping a reference to the originals
		 * just in case all instances change.
		 */
		s = (GnmStyle *)g_hash_table_lookup (rs->cache, *old);
		if (s == NULL) {
			GnmStyle *tmp = gnm_style_new_merged (*old, rs->pstyle);
			s = sheet_style_find (rs->sheet, tmp);
			gnm_style_link (*old);
			g_hash_table_insert (rs->cache, *old, s);
		}
	} else
		s = rs->new_style;

	if (*old != s) {
		gnm_style_link (s);
		if (*old)
			gnm_style_unlink (*old);
		*old = s;
	}
}

/****************************************************************************/

/* If you change this, change the tile_{widths,heights} here, in sheet_style_get
 * and in the sanity check in sheet_style_init
 */
#define TILE_TOP_LEVEL	3

/* This is good until a million columns.  */
#if SHEET_MAX_COLS <= 4 * 4 * 4 * 4
#define TILE_SIZE_COL 4
#elif SHEET_MAX_COLS <= 5 * 5 * 5 * 5
#define TILE_SIZE_COL 5
#elif SHEET_MAX_COLS <= 8 * 8 * 8 * 8
#define TILE_SIZE_COL 8
#elif SHEET_MAX_COLS <= 16 * 16 * 16 * 16
#define TILE_SIZE_COL 16
#else
#define TILE_SIZE_COL 32
#endif
#define PARTIAL_TILE_COL (SHEET_MAX_COLS != TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL)


/* This is good until 16M rows.  */
#if SHEET_MAX_ROWS <= 16 * 16 * 16 * 16
#define	TILE_SIZE_ROW 16
#elif SHEET_MAX_ROWS <= 20 * 20 * 20 * 20
#define	TILE_SIZE_ROW 20
#elif SHEET_MAX_ROWS <= 32 * 32 * 32 * 32
#define	TILE_SIZE_ROW 32
#else
#define	TILE_SIZE_ROW 64
#endif
#define PARTIAL_TILE_ROW (SHEET_MAX_ROWS != TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW)

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

typedef struct {
	CellTileType const type;
	GnmStyle *style [1];
} CellTileStyleSimple;
typedef struct {
	CellTileType const type;
	GnmStyle *style [TILE_SIZE_COL];
} CellTileStyleCol;
typedef struct {
	CellTileType const type;
	GnmStyle *style [TILE_SIZE_ROW];
} CellTileStyleRow;
typedef struct {
	CellTileType const type;
	GnmStyle *style [TILE_SIZE_COL * TILE_SIZE_ROW];
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

#if USE_TILE_POOLS
static int tile_pool_users;
static GOMemChunk *tile_pools[5];
#define CHUNK_ALLOC(T,p) ((T*)go_mem_chunk_alloc (p))
#define CHUNK_FREE(p,v) go_mem_chunk_free ((p), (v))
#else
#define CHUNK_ALLOC(T,c) g_new (T,1)
#define CHUNK_FREE(p,v) g_free ((v))
#endif


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
			gnm_style_unlink (tile->style_any.style [i]);
			tile->style_any.style [i] = NULL;
		}
	} else {
		g_return_if_fail (FALSE); /* don't free anything */
	}

	*((CellTileType *)&(tile->type)) = TILE_UNDEFINED; /* poison it */
	CHUNK_FREE (tile_pools[t], tile);
}

static CellTile *
cell_tile_style_new (GnmStyle *style, CellTileType t)
{
	CellTile *res;

#if USE_TILE_POOLS
	res = CHUNK_ALLOC (CellTile, tile_pools[t]);
#else
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
		return NULL;
	}
#endif

	*((CellTileType *)&(res->type)) = t;

	if (style != NULL) {
		int i = tile_size [t];
		gnm_style_link_multiple (style, i);
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

	res = CHUNK_ALLOC (CellTilePtrMatrix, tile_pools[TILE_PTR_MATRIX]);
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
	}

	return (CellTile *)res;
}

static CellTile *
cell_tile_matrix_set (CellTile *t, GnmRange const *indic, ReplacementStyle *rs)
{
	int i, r, c;
	CellTileStyleMatrix *res;
	GnmStyle *tmp;

	g_return_val_if_fail (t != NULL, NULL);
	g_return_val_if_fail (TILE_SIMPLE <= t->type &&
			      TILE_MATRIX >= t->type, NULL);

	res = (CellTileStyleMatrix *)((t->type != TILE_MATRIX)
		? cell_tile_style_new (NULL, TILE_MATRIX) : t);

	switch (t->type) {
	case TILE_SIMPLE :
		gnm_style_link_multiple (tmp = t->style_simple.style [0],
				     i = TILE_SIZE_COL * TILE_SIZE_ROW);
		while (--i >= 0)
			res->style [i] = tmp;
		break;

	case TILE_COL :
		for (i = r = 0 ; r < TILE_SIZE_ROW ; ++r)
			for (c = 0 ; c < TILE_SIZE_COL ; ++c)
				gnm_style_link (res->style [i++] =
					     t->style_col.style [c]);
		break;
	case TILE_ROW :
		for (i = r = 0 ; r < TILE_SIZE_ROW ; ++r) {
			gnm_style_link_multiple (tmp = t->style_row.style [r],
					      TILE_SIZE_COL);
			for (c = 0 ; c < TILE_SIZE_COL ; ++c)
				res->style [i++] = tmp;
		}
		break;
	case TILE_MATRIX :
	default :
		break;
	}

	if (t->type != TILE_MATRIX)
		cell_tile_dtor (t);

	if (indic != NULL) {
		GnmStyle **style = res->style;
		r = indic->start.row;
		style += r*TILE_SIZE_COL;
		for ( ;r <= indic->end.row ; ++r, style += TILE_SIZE_COL)
			for (c = indic->start.col ; c <= indic->end.col ; ++c)
				rstyle_apply (style + c, rs);
	}

	return (CellTile *)res;
}

/****************************************************************************/

void
sheet_style_init (Sheet *sheet)
{
	GnmStyle *default_style;

	/* some simple sanity checks */
	g_assert (SHEET_MAX_COLS <= TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL);
	g_assert (SHEET_MAX_ROWS <= TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW);
	g_return_if_fail (IS_SHEET (sheet));

#if USE_TILE_POOLS
	if (tile_pool_users++ == 0) {
		tile_pools[TILE_SIMPLE] =
			go_mem_chunk_new ("simple tile pool",
					   sizeof (CellTileStyleSimple),
					   16 * 1024 - 128);
		tile_pools[TILE_COL] =
			go_mem_chunk_new ("column tile pool",
					   sizeof (CellTileStyleCol),
					   16 * 1024 - 128);
		tile_pools[TILE_ROW] =
			go_mem_chunk_new ("row tile pool",
					   sizeof (CellTileStyleRow),
					   16 * 1024 - 128);
		tile_pools[TILE_MATRIX] =
			go_mem_chunk_new ("matrix tile pool",
					   sizeof (CellTileStyleMatrix),
					   MAX (16 * 1024 - 128,
						100 * sizeof (CellTileStyleMatrix)));

		/* If this fails one day, just make two pools.  */
		g_assert (sizeof (CellTileStyleMatrix) == sizeof (CellTilePtrMatrix));
		tile_pools[TILE_PTR_MATRIX] = tile_pools[TILE_MATRIX];
	}
#endif

	if (gnm_sheet_get_max_cols (sheet) > 364238) {
		/* Oh, yeah?  */
		g_warning (_("This is a special version of Gnumeric.  It has been compiled\n"
			     "with support for a very large number of columns.  Access to the\n"
			     "column named TRUE may conflict with the constant of the same\n"
			     "name.  Expect weirdness."));
	}

	sheet->style_data = g_new (GnmSheetStyleData, 1);
	sheet->style_data->style_hash =
		g_hash_table_new (gnm_style_hash, (GCompareFunc) gnm_style_equal);
#warning "FIXME: Allocating a GnmColor here is dubious."
	sheet->style_data->auto_pattern_color = g_new (GnmColor, 1);
	memcpy (sheet->style_data->auto_pattern_color,
		style_color_auto_pattern (), sizeof (GnmColor));
	sheet->style_data->auto_pattern_color->ref_count = 1;

	default_style =  gnm_style_new_default ();
#if 0
	/* We can not do this, XL creates full page charts with background
	 * 'none' by default.  Then displays that as white. */
	if (sheet->sheet_type == GNM_SHEET_OBJECT) {
		gnm_style_set_back_color (default_style,
			style_color_new_i8 (0x50, 0x50, 0x50));
		gnm_style_set_pattern (default_style, 1);
	}
#endif
	sheet->style_data->default_style =
		sheet_style_find (sheet, default_style);
	sheet->style_data->styles =
		cell_tile_style_new (sheet->style_data->default_style,
				     TILE_SIMPLE);
}

static gboolean
cb_unlink (void *key, void *value, void *user)
{
	gnm_style_unlink (key);
	return TRUE;
}

#if USE_TILE_POOLS
static void
cb_tile_pool_leak (gpointer data, gpointer user)
{
	CellTile *tile = data;
	g_printerr ("Leaking tile at %p.\n", tile);
}
#endif

void
sheet_style_shutdown (Sheet *sheet)
{
	GHashTable *table;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->style_data != NULL);

	cell_tile_dtor (sheet->style_data->styles);
	sheet->style_data->styles = NULL;

	sheet->style_data->default_style = NULL;

	/* Clear the pointer to the hash BEFORE clearing and add a test in
	 * sheet_style_unlink.  If we don't then it is possible/probable that
	 * unlinking the styles will attempt to remove them from the hash while
	 * we are walking it.
	 */
	table = sheet->style_data->style_hash;
	sheet->style_data->style_hash = NULL;
	g_hash_table_foreach_remove (table, cb_unlink, NULL);
	g_hash_table_destroy (table);
	style_color_unref (sheet->style_data->auto_pattern_color);

	g_free (sheet->style_data);
	sheet->style_data = NULL;

#if USE_TILE_POOLS
	if (--tile_pool_users == 0) {
		go_mem_chunk_foreach_leak (tile_pools[TILE_SIMPLE],
					    cb_tile_pool_leak, NULL);
		go_mem_chunk_destroy (tile_pools[TILE_SIMPLE], FALSE);
		tile_pools[TILE_SIMPLE] = NULL;

		go_mem_chunk_foreach_leak (tile_pools[TILE_COL],
					    cb_tile_pool_leak, NULL);
		go_mem_chunk_destroy (tile_pools[TILE_COL], FALSE);
		tile_pools[TILE_COL] = NULL;

		go_mem_chunk_foreach_leak (tile_pools[TILE_ROW],
					    cb_tile_pool_leak, NULL);
		go_mem_chunk_destroy (tile_pools[TILE_ROW], FALSE);
		tile_pools[TILE_ROW] = NULL;

		go_mem_chunk_foreach_leak (tile_pools[TILE_MATRIX],
					    cb_tile_pool_leak, NULL);
		go_mem_chunk_destroy (tile_pools[TILE_MATRIX], FALSE);
		tile_pools[TILE_MATRIX] = NULL;

		/* If this fails one day, just make two pools.  */
		g_assert (sizeof (CellTileStyleMatrix) == sizeof (CellTilePtrMatrix));
		tile_pools[TILE_PTR_MATRIX] = NULL;
	}
#endif
}

/**
 * sheet_style_set_auto_pattern_color :
 * @sheet:         The sheet
 * @pattern_color: The color
 *
 * Set the color for rendering auto colored patterns in this sheet.
 * Absorbs a reference to @pattern_color;
 **/
void
sheet_style_set_auto_pattern_color (Sheet  *sheet, GnmColor *pattern_color)
{
	GnmColor *apc;
	int ref_count;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->style_data != NULL);

	apc = sheet->style_data->auto_pattern_color;
	ref_count = apc->ref_count;
	memcpy(apc, pattern_color, sizeof (GnmColor));
	apc->is_auto = TRUE;
	apc->ref_count = ref_count;
	style_color_unref (pattern_color);
}

/**
 * sheet_style_get_auto_pattern_color:
 * @sheet: the sheet
 *
 * Caller receives a reference to the result.
 * Returns the color for rendering auto colored patterns in this sheet.
 **/
GnmColor *
sheet_style_get_auto_pattern_color (Sheet const *sheet)
{
	GnmColor *sc;
	g_return_val_if_fail (IS_SHEET (sheet), style_color_black ());
	g_return_val_if_fail (sheet->style_data != NULL, style_color_black ());
	g_return_val_if_fail (sheet->style_data->auto_pattern_color != NULL,
			      style_color_black ());

	sc = sheet->style_data->auto_pattern_color;
	style_color_ref (sc);

	return sc;
}

/**
 * This function updates the color of gnm_style_border_none when the sheet to be
 * rendered is known. gnm_style_border_none tells how to render the
 * grid. Because the grid color may be different for different sheets, the
 * functions which render the grid call this function first.  The rule for
 * selecting the grid color, which is the same as in Excel, is: - if the
 * auto pattern color is default (which is black), the grid color is gray,
 * as returned by style_color_grid ().  - otherwise, the auto pattern color
 * is used for the grid.
 */
void
sheet_style_update_grid_color (Sheet const *sheet)
{
	GnmColor *default_auto = style_color_auto_pattern ();
	GnmColor *sheet_auto = sheet_style_get_auto_pattern_color (sheet);
	GnmColor *grid_color = style_color_grid ();
	GnmColor *new_color;

	new_color = (style_color_equal (default_auto, sheet_auto)
		     ? grid_color : sheet_auto);

	/* Do nothing if we already have the right color */
	if (gnm_style_border_none()->color != new_color) {
		style_color_ref (new_color); /* none_set eats the ref */
		gnm_style_border_none_set_color (new_color);
	}
	style_color_unref (grid_color);
	style_color_unref (sheet_auto);
	style_color_unref (default_auto);
}

/****************************************************************************/

static GnmStyle *
vector_apply_pstyle (GnmStyle **styles, int n, ReplacementStyle *rs)
{
	gboolean is_uniform = TRUE;
	GnmStyle *prev = NULL;

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
col_indicies (int corner_col, int w, GnmRange const *apply_to,
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
row_indicies (int corner_row, int h, GnmRange const *apply_to,
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
		 GnmRange const *apply_to,
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
	GnmRange indic;
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
			GnmStyle *uniform = vector_apply_pstyle (
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

typedef void (*ForeachTileFunc) (GnmStyle *style,
				 int corner_col, int corner_row, int width, int height,
				 GnmRange const *apply_to, gpointer user);
static void
foreach_tile (CellTile *tile, int level,
	      int corner_col, int corner_row,
	      GnmRange const *apply_to,
	      ForeachTileFunc handler,
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
			/* applying the same style to part of a simple-tile is a nop */
			if (type == TILE_SIMPLE &&
			    (*tile)->style_simple.style [0] == rs->new_style)
				return;

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
 * @style : #GnmStyle
 *
 * Change the complete style for a region.
 * This function absorbs a reference to the new @style.
 */
void
sheet_style_set_range (Sheet *sheet, GnmRange const *range,
		       GnmStyle *style)
{
	ReplacementStyle rs;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);

	cell_tile_apply (&sheet->style_data->styles,
			 TILE_TOP_LEVEL, 0, 0,
			 range, rstyle_ctor (&rs, style, NULL, sheet));
	rstyle_dtor (&rs);
}

/**
 * sheet_style_set_col
 * @sheet :
 * @col :
 * @style : #GnmStyle
 *
 * NOTE : This is a simple wrapper for now.  When we support col/row styles it
 *	will make life easier.
 *
 * Change the complete style for a full col.
 * This function absorbs a reference to the new @style.
 **/
void
sheet_style_set_col (Sheet *sheet, int col, GnmStyle *style)
{
	GnmRange r;
	sheet_style_set_range (sheet, range_init_cols (&r, col, col), style);
}

/**
 * sheet_style_apply_col
 * @sheet :
 * @col :
 * @pstyle : #GnmStyle
 *
 * NOTE : This is a simple wrapper for now.  When we support col/row styles it
 *	will make life easier.
 *
 * Apply a partial style to a full col.
 * The routine absorbs a reference to the partial style.
 **/
void
sheet_style_apply_col (Sheet *sheet, int col, GnmStyle *pstyle)
{
	GnmRange r;
	sheet_style_apply_range (sheet, range_init_cols (&r, col, col), pstyle);
}

/**
 * sheet_style_set_row
 * @sheet :
 * @row :
 * @style : #GnmStyle
 *
 * NOTE : This is a simple wrapper for now.  When we support col/row styles it
 *	will make life easier.
 *
 * Change the complete style for a full row.
 * This function absorbs a reference to the new @style.
 **/
void
sheet_style_set_row (Sheet  *sheet, int row, GnmStyle *style)
{
	GnmRange r;
	sheet_style_set_range (sheet, range_init_rows (&r, row, row), style);
}

/**
 * sheet_style_apply_row
 * @sheet :
 * @row :
 * @pstyle : #GnmStyle
 *
 * NOTE : This is a simple wrapper for now.  When we support col/row styles it
 *	will make life easier.
 *
 * Apply a partial style to a full col.
 * The routine absorbs a reference to the partial style.
 **/
void
sheet_style_apply_row (Sheet  *sheet, int row, GnmStyle *pstyle)
{
	GnmRange r;
	sheet_style_apply_range (sheet, range_init_rows (&r, row, row), pstyle);
}

/**
 * sheet_style_apply_pos :
 * @sheet :
 * @col   :
 * @row   :
 * @pstyle : #GnmStyle
 *
 * Apply a partial style to a single cell
 * This function absorbs a reference to the new @style.
 **/
void
sheet_style_apply_pos (Sheet *sheet, int col, int row,
		       GnmStyle *pstyle)
{
	ReplacementStyle rs;

	g_return_if_fail (IS_SHEET (sheet));

	cell_tile_apply_pos (&sheet->style_data->styles,
			     TILE_TOP_LEVEL, col, row,
			     rstyle_ctor (&rs, NULL, pstyle, sheet));
	rstyle_dtor (&rs);
}
/**
 * sheet_style_set_pos :
 * @sheet :
 * @col   :
 * @row   :
 * @style :
 *
 * Change the complete style for a single cell.
 * This function absorbs a reference to the new @style.
 **/
void
sheet_style_set_pos (Sheet *sheet, int col, int row,
		     GnmStyle *style)
{
	ReplacementStyle rs;

	g_return_if_fail (IS_SHEET (sheet));

	cell_tile_apply_pos (&sheet->style_data->styles,
			     TILE_TOP_LEVEL, col, row,
			     rstyle_ctor (&rs, style, NULL, sheet));
	rstyle_dtor (&rs);
}

/**
 * sheet_style_default :
 * @sheet :
 *
 * Returns a reference to default style for a sheet.
 **/
GnmStyle *
sheet_style_default (Sheet const *sheet)
{
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (sheet->style_data != NULL, NULL);

	gnm_style_ref (sheet->style_data->default_style);
	return sheet->style_data->default_style;
}

/**
 * sheet_style_get :
 * @sheet : #Sheet
 * @col   :
 * @row   :
 *
 * Find the fully qualified style applicable to the specified cellpos.
 * Does _not_ add a reference.
 **/
GnmStyle const *
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
	}

	g_warning ("Adaptive Quad Tree corruption !");
	return NULL;
}

#define border_null(b)	((b) == none || (b) == NULL)

static inline void
style_row (GnmStyle *style, int start_col, int end_col, GnmStyleRow *sr, gboolean accept_conditions)
{
	GnmBorder const *top, *bottom, *none = gnm_style_border_none ();
	GnmBorder const *left, *right, *v;
	int const end = MIN (end_col, sr->end_col);
	int i = MAX (start_col, sr->start_col);

	if (accept_conditions && style->conditions) {
		GnmEvalPos ep;
		int res;

		for (eval_pos_init (&ep, (Sheet *)sr->sheet, i, sr->row); ep.eval.col <= end ; ep.eval.col++) {
			res = gnm_style_conditions_eval (style->conditions, &ep);
			style_row (res >= 0 ? g_ptr_array_index (style->cond_styles, res) : style,
				   ep.eval.col, ep.eval.col, sr, FALSE);
		}
		return;
	}

	top = gnm_style_get_border (style, MSTYLE_BORDER_TOP);
	bottom = gnm_style_get_border (style, MSTYLE_BORDER_BOTTOM);
	left = gnm_style_get_border (style, MSTYLE_BORDER_LEFT);
	right = gnm_style_get_border (style, MSTYLE_BORDER_RIGHT);

	/* Cancel grids if there is a background */
	if (sr->hide_grid || gnm_style_get_pattern (style) > 0) {
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
	v = border_null (right) ? left : right;

	while (i <= end) {
		sr->styles [i] = style;
		if (top != none && border_null (sr->top [i]))
			sr->top [i] = top;
		sr->bottom [i] = bottom;
		sr->vertical [++i] = v;
	}
	if (border_null (right))
		sr->vertical [i] = right;
}

static void
get_style_row (CellTile const *tile, int level,
	       int corner_col, int corner_row,
	       GnmStyleRow *sr)
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
			   corner_col, corner_col + width - 1, sr, TRUE);
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
			GnmStyle * const *styles = tile->style_any.style + r*TILE_SIZE_COL;

			for ( ; c <= last_c ; c++, corner_col += w)
				style_row (styles [c],
					   corner_col, corner_col + w - 1, sr, TRUE);
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
 * @sheet : #Sheet
 * @sr    : #GnmStyleRow
 *
 * A utility routine which efficiently retrieves a range of styles within a row.
 * It also merges adjacent borders as necessary.
 **/
void
sheet_style_get_row (Sheet const *sheet, GnmStyleRow *sr)
{

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sr != NULL);
	g_return_if_fail (sr->styles != NULL);
	g_return_if_fail (sr->vertical != NULL);
	g_return_if_fail (sr->top != NULL);
	g_return_if_fail (sr->bottom != NULL);

	sr->sheet = sheet;
	sr->vertical [sr->start_col] = gnm_style_border_none ();
	get_style_row (sheet->style_data->styles, TILE_TOP_LEVEL, 0, 0, sr);
}

/**
 * style_row_init :
 *
 * A small utility routine to initialize the grid drawing GnmStyleRow data
 * structure.
 */
void
style_row_init (GnmBorder const * * *prev_vert,
		GnmStyleRow *sr, GnmStyleRow *next_sr,
		int start_col, int end_col, gpointer mem, gboolean hide_grid)
{
	int n, col;
	GnmBorder const *none = hide_grid ? NULL : gnm_style_border_none ();

	/* alias the arrays for easy access so that array [col] is valid
	 * for all elements start_col-1 .. end_col+1 inclusive.
	 * Note that this means that in some cases array [-1] is legal.
	 */
	n = end_col - start_col + 3; /* 1 before, 1 after, 1 fencepost */
	sr->vertical	 = mem;
	sr->vertical	-= start_col-1;
	sr->top		 = sr->vertical + n;
	sr->bottom	 = sr->top + n;
	next_sr->top	 = sr->bottom; /* yes they should share */
	next_sr->bottom	 = next_sr->top + n;
	next_sr->vertical = next_sr->bottom + n;
	*prev_vert	 = next_sr->vertical + n;
	sr->styles	 = ((GnmStyle const **) (*prev_vert + n));
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
sheet_style_apply_range (Sheet *sheet, GnmRange const *range, GnmStyle *pstyle)
{
	ReplacementStyle rs;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);

	cell_tile_apply (&sheet->style_data->styles,
			 TILE_TOP_LEVEL, 0, 0,
			 range, rstyle_ctor (&rs, NULL, pstyle, sheet));
	rstyle_dtor (&rs);
}

static void
apply_border (Sheet *sheet, GnmRange const *r,
	      GnmStyleBorderLocation side,
	      GnmBorder *border)
{
	GnmStyle *pstyle = gnm_style_new ();
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
			  GnmRange const *range,
			  GnmBorder **borders)
{
	GnmStyle *pstyle = NULL;

	if (borders == NULL)
		return;

	if (borders [GNM_STYLE_BORDER_TOP]) {
		/* 1.1 top inner */
		GnmRange r = *range;
		r.end.row = r.start.row;
		apply_border (sheet, &r, GNM_STYLE_BORDER_TOP,
			      borders [GNM_STYLE_BORDER_TOP]);

		/* 1.2 top outer */
		r.start.row--;
		if (r.start.row >= 0) {
			r.end.row = r.start.row;
			apply_border (sheet, &r, GNM_STYLE_BORDER_BOTTOM,
				      gnm_style_border_none ());
		}
	}

	if (borders [GNM_STYLE_BORDER_BOTTOM]) {
		/* 2.1 bottom inner */
		GnmRange r = *range;
		r.start.row = r.end.row;
		apply_border (sheet, &r, GNM_STYLE_BORDER_BOTTOM,
			      borders [GNM_STYLE_BORDER_BOTTOM]);

		/* 2.2 bottom outer */
		r.end.row++;
		if (r.end.row < (gnm_sheet_get_max_rows (sheet)-1)) {
			r.start.row = r.end.row;
			apply_border (sheet, &r, GNM_STYLE_BORDER_TOP,
				      gnm_style_border_none ());
		}
	}

	if (borders [GNM_STYLE_BORDER_LEFT]) {
		/* 3.1 left inner */
		GnmRange r = *range;
		r.end.col = r.start.col;
		apply_border (sheet, &r, GNM_STYLE_BORDER_LEFT,
			      borders [GNM_STYLE_BORDER_LEFT]);

		/* 3.2 left outer */
		r.start.col--;
		if (r.start.col >= 0) {
			r.end.col = r.start.col;
			apply_border (sheet, &r, GNM_STYLE_BORDER_RIGHT,
				      gnm_style_border_none ());
		}
	}

	if (borders [GNM_STYLE_BORDER_RIGHT]) {
		/* 4.1 right inner */
		GnmRange r = *range;
		r.start.col = r.end.col;
		apply_border (sheet, &r, GNM_STYLE_BORDER_RIGHT,
			      borders [GNM_STYLE_BORDER_RIGHT]);

		/* 4.2 right outer */
		r.end.col++;
		if (r.end.col < (gnm_sheet_get_max_cols (sheet)-1)) {
			r.start.col = r.end.col;
			apply_border (sheet, &r, GNM_STYLE_BORDER_LEFT,
				      gnm_style_border_none ());
		}
	}

	/* Interiors horizontal : prefer top */
	if (borders [GNM_STYLE_BORDER_HORIZ] != NULL) {
		/* 5.1 horizontal interior top */
		if (range->start.row != range->end.row) {
			GnmRange r = *range;
			++r.start.row;
			apply_border (sheet, &r, GNM_STYLE_BORDER_TOP,
				      borders [GNM_STYLE_BORDER_HORIZ]);
		}
		/* 5.2 interior bottom */
		if (range->start.row != range->end.row) {
			GnmRange r = *range;
			--r.end.row;
			apply_border (sheet, &r, GNM_STYLE_BORDER_BOTTOM,
				      gnm_style_border_none ());
		}
	}

	/* Interiors vertical : prefer left */
	if (borders [GNM_STYLE_BORDER_VERT] != NULL) {
		/* 6.1 vertical interior left */
		if (range->start.col != range->end.col) {
			GnmRange r = *range;
			++r.start.col;
			apply_border (sheet, &r, GNM_STYLE_BORDER_LEFT,
				      borders [GNM_STYLE_BORDER_VERT]);
		}

		/* 6.2 The vertical interior right */
		if (range->start.col != range->end.col) {
			GnmRange r = *range;
			--r.end.col;
			apply_border (sheet, &r, GNM_STYLE_BORDER_RIGHT,
				      gnm_style_border_none ());
		}
	}

	/* 7. Diagonals (apply both in one pass) */
	if (borders [GNM_STYLE_BORDER_DIAG] != NULL) {
		pstyle = gnm_style_new ();
		pstyle_set_border (pstyle, borders [GNM_STYLE_BORDER_DIAG],
				   GNM_STYLE_BORDER_DIAG);
	}
	if (borders [GNM_STYLE_BORDER_REV_DIAG]) {
		if (pstyle == NULL)
			pstyle = gnm_style_new ();
		pstyle_set_border (pstyle, borders [GNM_STYLE_BORDER_REV_DIAG],
				   GNM_STYLE_BORDER_REV_DIAG);
	}
	if (pstyle != NULL)
		sheet_style_apply_range (sheet, range, pstyle);
}

/****************************************************************************/

typedef struct {
	GnmStyle	*accum;
	unsigned int	 conflicts;
} FindConflicts;

static void
cb_find_conflicts (GnmStyle *style,
		   int corner_col, int corner_row, int width, int height,
		   GnmRange const *apply_to, FindConflicts *ptr)
{
	ptr->conflicts = gnm_style_find_conflicts (ptr->accum, style, ptr->conflicts);
}

static void
border_mask_internal (gboolean *known, GnmBorder **borders,
		      GnmBorder const *b, GnmStyleBorderLocation l)
{
	if (!known [l]) {
		known [l] = TRUE;
		borders [l] = (GnmBorder *)b;
		gnm_style_border_ref (borders [l]);
	} else if (borders [l] != b && borders [l] != NULL) {
		gnm_style_border_unref (borders [l]);
		borders [l] = NULL;
	}
}

static void
border_mask (gboolean *known, GnmBorder **borders,
	     GnmBorder const *b, GnmStyleBorderLocation l)
{
	if (b == NULL)
		b = gnm_style_border_none ();
	border_mask_internal (known, borders, b, l);
}

static void
border_mask_vec (gboolean *known, GnmBorder **borders,
		 GnmBorder const * const *vec, int first, int last,
		 GnmStyleBorderLocation l)
{
	GnmBorder const *b = vec [first];

	if (b == NULL)
		b = gnm_style_border_none ();
	while (first++ < last) {
		GnmBorder const *tmp = vec [first];
		if (tmp == NULL)
			tmp = gnm_style_border_none ();
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
 * Find out what style elements are common to every cell in a range
 * Returns a flag of TRUE if there was a conflict a given style element
 */
unsigned int
sheet_style_find_conflicts (Sheet const *sheet, GnmRange const *r,
			    GnmStyle **style, GnmBorder **borders)
{
	int n, col, row, start_col, end_col;
	GnmStyleRow sr;
	GnmStyleBorderLocation i;
	gboolean known [GNM_STYLE_BORDER_EDGE_MAX];
	GnmBorder const *none = gnm_style_border_none ();
	FindConflicts user;

	g_return_val_if_fail (IS_SHEET (sheet), 0);
	g_return_val_if_fail (r != NULL, 0);
	g_return_val_if_fail (style != NULL, 0);
	g_return_val_if_fail (borders != NULL, 0);

	/* init style set with a copy of the top left corner of the 1st range */
	if (*style == NULL) {
		GnmStyle const *tmp = sheet_style_get (sheet, r->start.col, r->start.row);
		*style = gnm_style_dup (tmp);
		for (i = GNM_STYLE_BORDER_TOP ; i < GNM_STYLE_BORDER_EDGE_MAX ; i++) {
			known [i] = FALSE;
			borders [i] = gnm_style_border_ref ((GnmBorder *)none);
		}
	} else {
		for (i = GNM_STYLE_BORDER_TOP ; i < GNM_STYLE_BORDER_EDGE_MAX ; i++)
			known [i] = TRUE;
	}

	user.accum = *style;
	user.conflicts = 0; /* no conflicts yet */
	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, r,
		      (ForeachTileFunc)cb_find_conflicts, &user);

	/* copy over the diagonals */
	for (i = GNM_STYLE_BORDER_REV_DIAG ; i <= GNM_STYLE_BORDER_DIAG ; i++) {
		GnmStyleElement se = GNM_STYLE_BORDER_LOCATION_TO_STYLE_ELEMENT (i);
		if (user.conflicts & (1 << se))
			borders[i] = NULL;
		else
			borders[i] = gnm_style_border_ref (
				gnm_style_get_border (*style, se));
	}

	start_col = r->start.col;
	if (r->start.col > 0)
		start_col--;
	end_col = r->end.col;
	if (r->end.col < gnm_sheet_get_max_cols (sheet))
		end_col++;

	/* allocate then alias the arrays for easy access */
	n = end_col - start_col + 2;
	sr.vertical	 = (GnmBorder const **)g_alloca (n *
			    (3 * sizeof (GnmBorder const *) +
			     sizeof (GnmStyle const *)));
	sr.top	      = sr.vertical + n;
	sr.bottom     = sr.top + n;
	sr.styles     = ((GnmStyle const **) (sr.bottom + n));
	sr.vertical  -= start_col; sr.top     -= start_col;
	sr.bottom    -= start_col; sr.styles  -= start_col;
	sr.start_col  = start_col; sr.end_col  = end_col;
	sr.hide_grid  = sheet->hide_grid;

	/* pretend the previous bottom had no borders */
	for (col = start_col ; col <= end_col; ++col)
		sr.top [col] = none;

	/* merge the bottom of the previous row */
	if (r->start.row > 0) {
		GnmBorder const ** roller;
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
		GnmBorder const **roller;
		sr.row = row;
		sheet_style_get_row (sheet, &sr);

		border_mask (known, borders, sr.vertical [r->start.col],
			     GNM_STYLE_BORDER_LEFT);
		border_mask (known, borders, sr.vertical [r->end.col+1],
			     GNM_STYLE_BORDER_RIGHT);
		border_mask_vec (known, borders, sr.top,
				 r->start.col, r->end.col, (row == r->start.row)
				 ? GNM_STYLE_BORDER_TOP : GNM_STYLE_BORDER_HORIZ);
		if (r->start.col != r->end.col)
			border_mask_vec (known, borders, sr.vertical,
					 r->start.col+1, r->end.col,
					 GNM_STYLE_BORDER_VERT);

		roller = sr.top; sr.top = sr.bottom; sr.bottom = roller;
	}

	/* merge the top of the next row */
	if (r->end.row < (gnm_sheet_get_max_rows (sheet)-1)) {
		sr.row = r->end.row + 1;
		sheet_style_get_row (sheet, &sr);
	}
	border_mask_vec (known, borders, sr.top, r->start.col, r->end.col,
			 GNM_STYLE_BORDER_BOTTOM);

	return user.conflicts;
}

/**
 * sheet_style_relocate
 *
 * @rinfo :
 *
 * Slide the styles from the origin region to the new position.
 */
void
sheet_style_relocate (GnmExprRelocateInfo const *rinfo)
{
	GnmCellPos corner;
	GnmStyleList *styles;

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
sheet_style_insert_colrow (GnmExprRelocateInfo const *rinfo)
{
	GnmCellPos corner;
	GnmStyleList *ptr, *styles = NULL;
	GnmRange r;

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
					       range_init_cols (&r, col, col));
		if (o > 0)
			for (ptr = styles ; ptr != NULL ; ptr = ptr->next)
				((GnmStyleRegion *)ptr->data)->range.end.col = o;

	} else if (rinfo->row_offset != 0) {
		int const o = rinfo->row_offset - 1;
		int row = corner.row - 1;
		if (row < 0)
			row = 0;
		corner.col = 0;
		styles = sheet_style_get_list (rinfo->origin_sheet,
					       range_init_rows (&r, row, row));
		if (o > 0)
			for (ptr = styles ; ptr != NULL ; ptr = ptr->next)
				((GnmStyleRegion *)ptr->data)->range.end.row = o;
	}

	sheet_style_relocate (rinfo);

	if (styles != NULL) {
		sheet_style_set_list (rinfo->target_sheet, &corner, FALSE, styles);
		style_list_free	(styles);
	}
}

static void
cb_visible_content (GnmStyle *style,
		    int corner_col, int corner_row, int width, int height,
		    GnmRange const *apply_to, gpointer res)
{
	*((gboolean *)res) |= gnm_style_visible_in_blank (style);
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
sheet_style_has_visible_content (Sheet const *sheet, GnmRange *src)
{
	gboolean res = FALSE;
	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, src,
		      cb_visible_content, &res);
	return res;
}

typedef struct {
	GnmRange *res;
	GnmStyle **most_common_in_cols;
} StyleExtentData;

static void
cb_style_extent (GnmStyle *style,
		 int corner_col, int corner_row, int width, int height,
		 GnmRange const *apply_to, gpointer user)
{
	StyleExtentData *data = user;
	if (gnm_style_visible_in_blank (style)) {

		/* always check if the column is extended */
		int tmp = corner_col+width-1;
		int i = corner_col;
		if (data->res->end.col < tmp)
			data->res->end.col = tmp;
		if (data->res->start.col > corner_col)
			data->res->start.col = corner_col;

		/* only check the row if the style is not the most common in
		 * all of the columns in the tile */
		if (data->most_common_in_cols != NULL) {
			for (; i <= tmp ; i++)
				if (style != data->most_common_in_cols[i])
					break;
			if (i > tmp)
				return;
		}
		tmp = corner_row+height-1;
		if (data->res->end.row < tmp)
			data->res->end.row = tmp;
		if (data->res->start.row > corner_row)
			data->res->start.row = corner_row;
	}
}

/**
 * sheet_style_get_extent :
 *
 * @sheet :
 * @r     :
 * most_common_in_cols : optionally NULL.
 *
 * A simple implementation that finds the smallest range containing all visible styles
 * and containing res. x If @most_common_in_cols is specified it finds the most common
 * style for each column (0..SHEET_MAX_COLS-1) and ignores that style in
 * boundary calculations.
 */
void
sheet_style_get_extent (Sheet const *sheet, GnmRange *res,
			GnmStyle **most_common_in_cols)
{
	StyleExtentData data;
	GnmRange r;

	if (most_common_in_cols != NULL) {
		unsigned i;
		for (i = 0; i < gnm_sheet_get_max_cols (sheet); i++)
			most_common_in_cols[i] = sheet_style_most_common_in_col (sheet, i);
	}

	/* This could easily be optimized */
	data.res = res;
	data.most_common_in_cols = most_common_in_cols;
	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, range_init_full_sheet(&r),
		      cb_style_extent, &data);
}

/****************************************************************************/

static GnmStyleRegion *
style_region_new (GnmRange const *range, GnmStyle *style)
{
	GnmStyleRegion *sr;

	sr = g_new (GnmStyleRegion, 1);
	sr->range = *range;
	sr->style = style;
	gnm_style_ref (style);

	return sr;
}

static void
style_region_free (GnmStyleRegion *sr)
{
	g_return_if_fail (sr != NULL);

	gnm_style_unref (sr->style);
	sr->style = NULL;
	g_free (sr);
}

typedef struct {
	GHashTable *cache;
	gboolean (*style_equal) (GnmStyle const *a, GnmStyle const *b);
} StyleListMerge;

static void
cb_style_list_add_node (GnmStyle *style,
			int corner_col, int corner_row, int width, int height,
			GnmRange const *apply_to, gpointer user)
{
	StyleListMerge *mi = user;
	GnmStyleRegion *sr = NULL;
	GnmCellPos	key;
	GnmRange range;

	range.start.col = corner_col;
	range.start.row = corner_row;
	range.end.col = corner_col + width - 1;
	range.end.row = corner_row + height - 1;

#if PARTIAL_TILE_COL
	if (corner_col >= SHEET_MAX_COLS)
		return;
	range.end.col = MIN (range.end.col, SHEET_MAX_COLS - 1);
#endif

#if PARTIAL_TILE_ROW
	if (corner_row >= SHEET_MAX_ROWS)
		return;
	range.end.row = MIN (range.end.row, SHEET_MAX_ROWS - 1);
#endif

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

	/* Do some simple minded merging vertically */
	key.col = range.end.col;
	key.row = range.start.row - 1;
#ifdef DEBUG_STYLE_LIST
	range_dump (&range, " Checking\n");
#endif
	if (key.row >= 0 &&
	    (sr = (GnmStyleRegion *)g_hash_table_lookup (mi->cache, &key)) != NULL &&
	    sr->range.start.col == range.start.col && (mi->style_equal) (sr->style, style)) {
		g_hash_table_remove (mi->cache, &key);
		sr->range.end.row = range.end.row;
#ifdef DEBUG_STYLE_LIST
		range_dump (&sr->range, " <= merged into\n");
#endif
	} else {
#ifdef DEBUG_STYLE_LIST
		range_dump (&range, " <= Added\n");
#endif
		sr = style_region_new (&range, style);
	}

	g_hash_table_insert (mi->cache, &sr->range.end, sr);
}

static gboolean
cb_hash_merge_horiz (gpointer hash_key, gpointer value, gpointer user)
{
	StyleListMerge *mi = user;
	GnmStyleRegion *sr = value, *srh;
	GnmCellPos	key;

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
		    (srh = (GnmStyleRegion *)g_hash_table_lookup (mi->cache, &key)) != NULL &&
		    srh->range.start.row == sr->range.start.row && (mi->style_equal) (sr->style, srh->style)) {
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
	GnmStyleList **res = user_data;
	GnmStyleRegion *sr = value;

	/* Already merged */
	if (sr->range.start.col < 0) {
		style_region_free (sr);
		return TRUE;
	}

#ifdef DEBUG_STYLE_LIST
	range_dump (&sr->range, "\n");
#endif

	*res = g_slist_prepend (*res, value);
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
GnmStyleList *
sheet_style_get_list (Sheet const *sheet, GnmRange const *r)
{
	GnmStyleList *res = NULL;
	StyleListMerge mi;

	mi.style_equal = gnm_style_equal;
	mi.cache = g_hash_table_new ((GHashFunc)&gnm_cellpos_hash,
				     (GCompareFunc)&gnm_cellpos_equal);

	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, r,
		      cb_style_list_add_node, &mi);
#ifdef DEBUG_STYLE_LIST
	g_printerr ("=========\n");
#endif
	g_hash_table_foreach_remove (mi.cache, cb_hash_merge_horiz, &mi);
	g_hash_table_foreach_remove (mi.cache, cb_hash_to_list, &res);
#ifdef DEBUG_STYLE_LIST
	g_printerr ("=========\n");
#endif
	g_hash_table_destroy (mi.cache);

	return res;
}

static void
cb_style_list_add_conditions (GnmStyle *style,
			      int corner_col, int corner_row,
			      int width, int height,
			      GnmRange const *apply_to, gpointer user)
{
	if (NULL != gnm_style_get_conditions (style))
		cb_style_list_add_node (style,
			corner_col, corner_row, width, height, apply_to, user);
}

static gboolean
style_conditions_equal (GnmStyle const *a, GnmStyle const *b)
{
	return	gnm_style_get_conditions (a) == gnm_style_get_conditions (b);
}

/**
 * sheet_style_collect_conditions:
 * @sheet :
 * @range :
 *
 * Returns a list of areas with conditionals, Caller is responsible for
 * freeing.
 **/
GnmStyleList *
sheet_style_collect_conditions (Sheet const *sheet, GnmRange const *r)
{
	GnmStyleList *res = NULL;
	StyleListMerge mi;
	mi.style_equal = style_conditions_equal;
	mi.cache = g_hash_table_new ((GHashFunc)&gnm_cellpos_hash,
				     (GCompareFunc)&gnm_cellpos_equal);

	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, r,
		      cb_style_list_add_conditions, &mi);
#ifdef DEBUG_STYLE_LIST
	g_printerr ("=========\n");
#endif
	g_hash_table_foreach_remove (mi.cache, cb_hash_merge_horiz, &mi);
	g_hash_table_foreach_remove (mi.cache, cb_hash_to_list, &res);
#ifdef DEBUG_STYLE_LIST
	g_printerr ("=========\n");
#endif
	g_hash_table_destroy (mi.cache);

	return res;
}
static void
cb_style_list_add_hlink (GnmStyle *style,
			 int corner_col, int corner_row,
			 int width, int height,
			 GnmRange const *apply_to, gpointer user)
{
	/* collect only the area with validation */
	if (NULL != gnm_style_get_hlink (style))
		cb_style_list_add_node (style,
			corner_col, corner_row, width, height, apply_to, user);
}

static gboolean
style_hlink_equal (GnmStyle const *a, GnmStyle const *b)
{
	return	gnm_style_get_hlink (a) == gnm_style_get_hlink (b);
}

/**
 * sheet_style_collect_hlinks :
 * @sheet :
 * @range :
 *
 * Returns a list of areas with hyperlinks, Caller is responsible for freeing.
 **/
GnmStyleList *
sheet_style_collect_hlinks (Sheet const *sheet, GnmRange const *r)
{
	GnmStyleList *res = NULL;
	StyleListMerge mi;

	mi.style_equal = style_hlink_equal;
	mi.cache = g_hash_table_new ((GHashFunc)&gnm_cellpos_hash,
				     (GCompareFunc)&gnm_cellpos_equal);

	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, r,
		      cb_style_list_add_hlink, &mi);
#ifdef DEBUG_STYLE_LIST
	g_printerr ("=========\n");
#endif
	g_hash_table_foreach_remove (mi.cache, cb_hash_merge_horiz, &mi);
	g_hash_table_foreach_remove (mi.cache, cb_hash_to_list, &res);
#ifdef DEBUG_STYLE_LIST
	g_printerr ("=========\n");
#endif
	g_hash_table_destroy (mi.cache);

	return res;
}

static void
cb_style_list_add_validation (GnmStyle *style,
			      int corner_col, int corner_row,
			      int width, int height,
			      GnmRange const *apply_to, gpointer user)
{
	/* collect only the area with validation */
	if (NULL != gnm_style_get_validation (style) ||
	    NULL != gnm_style_get_input_msg (style))
		cb_style_list_add_node (style,
			corner_col, corner_row, width, height, apply_to, user);
}

static gboolean
style_validation_equal (GnmStyle const *a, GnmStyle const *b)
{
	return	gnm_style_get_validation (a) == gnm_style_get_validation (b) &&
		gnm_style_get_input_msg (a) == gnm_style_get_input_msg (b);
}

/**
 * sheet_style_collect_validations :
 * @sheet :
 * @range :
 *
 * Returns a list of areas with validation, Caller is responsible for freeing.
 **/
GnmStyleList *
sheet_style_collect_validations (Sheet const *sheet, GnmRange const *r)
{
	GnmStyleList *res = NULL;
	StyleListMerge mi;

	mi.style_equal = style_validation_equal;
	mi.cache = g_hash_table_new ((GHashFunc)&gnm_cellpos_hash,
				     (GCompareFunc)&gnm_cellpos_equal);

	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, r,
		      cb_style_list_add_validation, &mi);
#ifdef DEBUG_STYLE_LIST
	g_printerr ("=========\n");
#endif
	g_hash_table_foreach_remove (mi.cache, cb_hash_merge_horiz, &mi);
	g_hash_table_foreach_remove (mi.cache, cb_hash_to_list, &res);
#ifdef DEBUG_STYLE_LIST
	g_printerr ("=========\n");
#endif
	g_hash_table_destroy (mi.cache);

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
 **/
GnmSpanCalcFlags
sheet_style_set_list (Sheet *sheet, GnmCellPos const *corner,
		      gboolean transpose, GnmStyleList const *list)
{
	GnmSpanCalcFlags spanflags = GNM_SPANCALC_SIMPLE;
	GnmStyleList const *l;

	g_return_val_if_fail (IS_SHEET (sheet), spanflags);

	/* Sluggish but simple implementation for now */
	for (l = list; l; l = l->next) {
		GnmStyleRegion const *sr = l->data;
		GnmRange              r  = sr->range;

		range_translate (&r, +corner->col, +corner->row);
		if (transpose)
			range_transpose (&r, corner);

		gnm_style_ref (sr->style);
		sheet_style_set_range (sheet, &r, sr->style);
		spanflags |= gnm_style_required_spanflags (sr->style);
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
style_list_free (GnmStyleList *list)
{
	GnmStyleList *l;

	for (l = list; l; l = l->next)
		style_region_free (l->data);
	g_slist_free (list);
}

/**
 * style_list_get_style :
 *
 * @list : A style list.
 * @col  :
 * @row  :
 *
 * Attempts to find the style associated with the @pos offset within the 0,0
 * based style list.
 * The resulting style does not have its reference count bumped.
 **/
GnmStyle const *
style_list_get_style (GnmStyleList const *list, int col, int row)
{
	GnmStyleList const *l;

	for (l = list; l; l = l->next) {
		GnmStyleRegion const *sr = l->data;
		GnmRange const *r = &sr->range;
		if (range_contains (r, col, row))
			return sr->style;
	}
	return NULL;
}

static void
cb_accumulate_count (GnmStyle *style,
		     int corner_col, int corner_row, int width, int height,
		     GnmRange const *apply_to, gpointer accumulator)
{
	gpointer count;

	count = g_hash_table_lookup (accumulator, style);
	if (count == NULL) {
		int *res = g_new (int, 1);
		*res = height;
		g_hash_table_insert (accumulator, style, res);
	} else
		*((int *)count) += height;
}

typedef struct
{
	GnmStyle *style;
	int     count;
} MostCommon;

static void
cb_find_max (gpointer key, gpointer value, gpointer user_data)
{
	MostCommon *mc = user_data;
	int count = *((int *)value);
	if (mc->style == NULL || mc->count < count) {
		mc->style = key;
		mc->count = count;
	}

	g_free (value);
}

/**
 * sheet_style_most_common_in_col :
 * @sheet :
 * @col :
 *
 * Find the most common style in a column.
 * The resulting style does not have its reference count bumped.
 */
GnmStyle *
sheet_style_most_common_in_col (Sheet const *sheet, int col)
{
	MostCommon  res;
	GHashTable *accumulator;
	GnmRange       r;

	range_init_cols (&r, col, col);
	accumulator = g_hash_table_new (gnm_style_hash, (GCompareFunc) gnm_style_equal);
	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, &r,
		      cb_accumulate_count, accumulator);

	res.style = NULL;
	g_hash_table_foreach (accumulator, cb_find_max, &res);
	g_hash_table_destroy (accumulator);
	return res.style;
}

static void
cb_find_link (GnmStyle *style,
	      int corner_col, int corner_row, int width, int height,
	      GnmRange const *apply_to, gpointer user)
{
	GnmHLink **link = user;
	if (*link == NULL)
		*link = gnm_style_get_hlink (style);
}

/**
 * sheet_style_region_contains_link :
 * @sheet :
 * @r :
 *
 * Utility routine that checks to see if a region contains at least 1 hyper link
 * and returns the 1st one it finds.
 **/
GnmHLink *
sheet_style_region_contains_link (Sheet const *sheet, GnmRange const *r)
{
	GnmHLink *res = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (r != NULL, NULL);

	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, r,
		      cb_find_link, &res);
	return res;
}

#if 0
static void
cb_validate (GnmStyle *style,
	     int corner_col, int corner_row, int width, int height,
	     GnmRange const *apply_to, Sheet const *sheet)
{
	if (g_hash_table_lookup (sheet->style_data->style_hash, style) == NULL) {
		GnmRange r;
		range_init (&r,
			   corner_col, corner_row,
			   corner_col+width -1, corner_row+height-1);
		g_warning ("%s!%s", sheet->name_unquoted,
			   range_as_string (&r));
	}
}

/* Verify that every style in the sheet is in the style_hash */
static void
debug_very_style_hash (Sheet *sheet)
{
	foreach_tile (sheet->style_data->styles,
		      TILE_TOP_LEVEL, 0, 0, NULL,
		      cb_validate, sheet);
}
#endif

void
sheet_style_foreach (Sheet const *sheet, GHFunc	func, gpointer user_data)
{
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->style_data != NULL);

	g_hash_table_foreach (sheet->style_data->style_hash, func, user_data);
}
