/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * sheet-style.c: storage mechanism for styles and eventually cells.
 *
 * Copyright (C) 2000-2006 Jody Goldberg (jody@gnome.org)
 * Copyright 2013 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
#include "parse-util.h"
#include "cell.h"
#include "gutils.h"
#include <goffice/goffice.h>
#include <glib/gi18n-lib.h>
#include <string.h>
#include <math.h>

#define USE_TILE_POOLS 0

/* ------------------------------------------------------------------------- */

/*
 * This is, essentially, an std::multiset implementation for the style hash.
 * Note, however, that sh_lookup is based on gnm_style_equal, not gnm_style_eq.
 */
typedef GHashTable GnmStyleHash;

#if 0
/* This is a really crummy hash -- except for forcing collisions.  */
#define gnm_style_hash(st) 0
#endif

static void
sh_remove (GnmStyleHash *h, GnmStyle *st)
{
	guint32 hv = gnm_style_hash (st);
	GSList *l = g_hash_table_lookup (h, GUINT_TO_POINTER (hv));

	g_return_if_fail (l != NULL);

	if (l->data == st) {
		GSList *next = l->next;
		if (next) {
			/* We're removing the first of several elements.  */
			l->next = NULL;
			g_hash_table_replace (h, GUINT_TO_POINTER (hv), next);
		} else {
			/* We're removing the last element.  */
			g_hash_table_remove (h, GUINT_TO_POINTER (hv));
		}
	} else {
		/* We're removing an element that isn't first.  */
		l = g_slist_remove (l, st);
	}
}

static GnmStyle *
sh_lookup (GnmStyleHash *h, GnmStyle *st)
{
	guint32 hv = gnm_style_hash (st);
	GSList *l = g_hash_table_lookup (h, GUINT_TO_POINTER (hv));
	while (l) {
		GnmStyle *st2 = l->data;
		/* NOTE: This uses gnm_style_equal, not gnm_style_eq.  */
		if (gnm_style_equal (st, st2))
			return st2;
		l = l->next;
	}
	return NULL;
}

static void
sh_insert (GnmStyleHash *h, GnmStyle *st)
{
	GSList *s = g_slist_prepend (NULL, st);
	guint32 hv = gnm_style_hash (st);
	GSList *l = g_hash_table_lookup (h, GUINT_TO_POINTER (hv));
	if (l) {
		s->next = l->next;
		l->next = s;
	} else {
		g_hash_table_insert (h, GUINT_TO_POINTER (hv), s);
	}
}

static GSList *
sh_all_styles (GnmStyleHash *h)
{
	GHashTableIter iter;
	gpointer value;
	GSList *res = NULL;

	g_hash_table_iter_init (&iter, h);
	while (g_hash_table_iter_next (&iter, NULL, &value)) {
		GSList *l = value;
		for (; l; l = l->next)
			res = g_slist_prepend (res, l->data);
	}

	return res;
}

static GnmStyleHash *
sh_create (void)
{
	return g_hash_table_new_full (g_direct_hash, g_direct_equal,
				      NULL, (GDestroyNotify)g_slist_free);
}

static void
sh_destroy (GnmStyleHash *h)
{
	g_hash_table_destroy (h);
}

/* ------------------------------------------------------------------------- */

typedef union _CellTile CellTile;
struct _GnmSheetStyleData {
	/*
	 * style_hash is a set of all styles used by this sheet.  These
	 * styles are all linked.
	 *
	 * We always re-use styles from here when we can, but there can
	 * still be duplicates.  This happens when styles are changed
	 * while they are in the hash.  For example, this happens when
	 * an expression used by a validation style changes due to
	 * row/col insert/delete.
	 */
	GnmStyleHash *style_hash;

	CellTile   *styles;
	GnmStyle   *default_style;
	GnmColor   *auto_pattern_color;
};

static gboolean debug_style_optimize;

typedef struct {
	GnmSheetSize const *ss;
	gboolean recursion;
} CellTileOptimize;

static void
cell_tile_optimize (CellTile **tile, int level, CellTileOptimize *data,
		    int ccol, int crow);


/*
 * sheet_style_unlink
 * For internal use only
 */
void
sheet_style_unlink (Sheet *sheet, GnmStyle *st)
{
	if (sheet->style_data->style_hash)
		sh_remove (sheet->style_data->style_hash, st);
}

/**
 * sheet_style_find:
 * @sheet: (transfer full): the sheet
 * @st: a style
 *
 * Looks up a style from the sheets collection.  Linking if necessary.
 * ABSORBS the reference and adds a link.
 *
 * Returns: (transfer full): the new style.
 */
GnmStyle *
sheet_style_find (Sheet const *sheet, GnmStyle *s)
{
	GnmStyle *res;
	res = sh_lookup (sheet->style_data->style_hash, s);
	if (res != NULL) {
		gnm_style_link (res);
		gnm_style_unref (s);
		return res;
	}

	s = gnm_style_link_sheet (s, (Sheet *)sheet);

	/* Retry the lookup in case "s" changed.  See #585178.  */
	res = sh_lookup (sheet->style_data->style_hash, s);
	if (res != NULL) {
		gnm_style_link (res);
		/*
		 * We are abandoning the linking here.  We cannot use
		 * gnm_style_unlink as that would call sheet_style_unlink
		 * and thus remove "res" from the hash.
		 */
		s->link_count = 0;
		s->linked_sheet = NULL;
		gnm_style_unref (s);

		return res;
	}

	sh_insert (sheet->style_data->style_hash, s);
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
	GnmStyle   *new_style;
	GnmStyle   *pstyle;
	GHashTable *cache;
	Sheet	   *sheet;
} ReplacementStyle;

static void
rstyle_ctor_style (ReplacementStyle *res, GnmStyle *new_style, Sheet *sheet)
{
	res->sheet = sheet;
	res->new_style = sheet_style_find (sheet, new_style);
	res->pstyle = NULL;
	res->cache = NULL;
}

static void
rstyle_ctor_pstyle (ReplacementStyle *res, GnmStyle *pstyle, Sheet *sheet)
{
	res->sheet = sheet;
	res->new_style = NULL;
	res->pstyle = pstyle;
	res->cache = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
cb_style_unlink (gpointer key, gpointer value, G_GNUC_UNUSED gpointer user_data)
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

/*
 * rstyle_apply:  Utility routine that is at the core of applying partial
 * styles or storing complete styles.  It will eventually be smarter
 * and will maintain the cache of styles associated with each sheet
 */
static void
rstyle_apply (GnmStyle **old, ReplacementStyle *rs, GnmRange const *r)
{
	GnmStyle *s;
	g_return_if_fail (old != NULL);
	g_return_if_fail (rs != NULL);

	if (rs->pstyle != NULL) {
		/* Cache the merged styles keeping a reference to the originals
		 * just in case all instances change.
		 */
		s = g_hash_table_lookup (rs->cache, *old);
		if (s == NULL) {
			GnmStyle *tmp = gnm_style_new_merged (*old, rs->pstyle);
			s = sheet_style_find (rs->sheet, tmp);
			gnm_style_link (*old);
			g_hash_table_insert (rs->cache, *old, s);
		}
	} else
		s = rs->new_style;

	if (*old != s) {
		if (*old) {
			gnm_style_unlink_dependents (*old, r);
			gnm_style_unlink (*old);
		}

		gnm_style_link_dependents (s, r);
		gnm_style_link (s);

		*old = s;
	}
}

/****************************************************************************/

/* If you change this, change the tile_{widths,heights} here
 * and GNM_MAX_COLS and GNM_MAX_ROWS in gnumeric.h */
#define TILE_TOP_LEVEL 6

#define TILE_SIZE_COL 8
#define	TILE_SIZE_ROW 16

typedef enum {
	TILE_UNDEFINED	= -1,
	TILE_SIMPLE	=  0,
	TILE_COL	=  1,
	TILE_ROW	=  2,
	TILE_MATRIX	=  3,
	TILE_PTR_MATRIX	=  4
} CellTileType;
static int const tile_size[/*type*/] = {
	1,				/* TILE_SIMPLE */
	TILE_SIZE_COL,			/* TILE_COL */
	TILE_SIZE_ROW,			/* TILE_ROW */
	TILE_SIZE_COL * TILE_SIZE_ROW	/* TILE_MATRIX */
};
static int const tile_col_count[/*type*/] = {
	1,				/* TILE_SIMPLE */
	TILE_SIZE_COL,			/* TILE_COL */
	1,				/* TILE_ROW */
	TILE_SIZE_COL,			/* TILE_MATRIX */
	TILE_SIZE_COL			/* TILE_PTR_MATRIX */
};
static int const tile_row_count[/*type*/] = {
	1,				/* TILE_SIMPLE */
	1,				/* TILE_COL */
	TILE_SIZE_ROW,			/* TILE_ROW */
	TILE_SIZE_ROW,			/* TILE_MATRIX */
	TILE_SIZE_ROW			/* TILE_PTR_MATRIX */
};
static const char * const tile_type_str[/*type*/] = {
	"simple", "col", "row", "matrix", "ptr-matrix"
};
static int const tile_widths[/*level*/] = {
	1,
	TILE_SIZE_COL,
	TILE_SIZE_COL * TILE_SIZE_COL,
	TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL,
	TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL,
	TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL,
	TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL,
	TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL * TILE_SIZE_COL
};
static int const tile_heights[/*level*/] = {
	1,
	TILE_SIZE_ROW,
	TILE_SIZE_ROW * TILE_SIZE_ROW,
	TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW,
	TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW,
	TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW,
	TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW,
	TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW * TILE_SIZE_ROW
};

typedef struct {
	CellTileType const type;
	GnmStyle *style[1];
} CellTileStyleSimple;
typedef struct {
	CellTileType const type;
	GnmStyle *style[TILE_SIZE_COL];
} CellTileStyleCol;
typedef struct {
	CellTileType const type;
	GnmStyle *style[TILE_SIZE_ROW];
} CellTileStyleRow;
typedef struct {
	CellTileType const type;
	GnmStyle *style[TILE_SIZE_COL * TILE_SIZE_ROW];
} CellTileStyleMatrix;
typedef struct {
	CellTileType const type;
	CellTile	*ptr[TILE_SIZE_COL * TILE_SIZE_ROW];
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

static int active_sheet_count;
#if USE_TILE_POOLS
static GOMemChunk *tile_pools[5];
#define CHUNK_ALLOC(T,ctt) ((T*)go_mem_chunk_alloc (tile_pools[(ctt)]))
#define CHUNK_FREE(ctt,v) go_mem_chunk_free (tile_pools[(ctt)], (v))
#else
static const size_t tile_type_sizeof[5] = {
	sizeof (CellTileStyleSimple),
	sizeof (CellTileStyleCol),
	sizeof (CellTileStyleRow),
	sizeof (CellTileStyleMatrix),
	sizeof (CellTilePtrMatrix)
};
static int tile_allocations = 0;
#if 1
#define CHUNK_ALLOC(T,ctt) (tile_allocations++, (T*)g_slice_alloc (tile_type_sizeof[(ctt)]))
#define CHUNK_FREE(ctt,v) (tile_allocations--, g_slice_free1 (tile_type_sizeof[(ctt)], (v)))
#else
#define CHUNK_ALLOC(T,ctt) (tile_allocations++, (T*)g_malloc (tile_type_sizeof[(ctt)]))
#define CHUNK_FREE(ctt,v) (tile_allocations--, g_free ((v)))
#endif
#endif


/*
 * Destroy a CellTile (recursively if needed).  This will unlink all the
 * styles in it.  We do _not_ unlink style dependents here.  That is done
 * only in rstyle_apply.
 */
static void
cell_tile_dtor (CellTile *tile)
{
	CellTileType t;

	g_return_if_fail (tile != NULL);

	t = tile->type;
	if (t == TILE_PTR_MATRIX) {
		int i = TILE_SIZE_COL * TILE_SIZE_ROW;
		while (--i >= 0) {
			cell_tile_dtor (tile->ptr_matrix.ptr[i]);
			tile->ptr_matrix.ptr[i] = NULL;
		}
	} else if (TILE_SIMPLE <= t && t <= TILE_MATRIX) {
		int i = tile_size[t];
		while (--i >= 0) {
			gnm_style_unlink (tile->style_any.style[i]);
			tile->style_any.style[i] = NULL;
		}
	} else {
		g_return_if_fail (FALSE); /* don't free anything */
	}

	*((CellTileType *)&(tile->type)) = TILE_UNDEFINED; /* poison it */
	CHUNK_FREE (t, tile);
}

static CellTile *
cell_tile_style_new (GnmStyle *style, CellTileType t)
{
	CellTile *res = CHUNK_ALLOC (CellTile, t);
	*((CellTileType *)&(res->type)) = t;

	if (style != NULL) {
		int i = tile_size[t];
		gnm_style_link_multiple (style, i);
		while (--i >= 0)
			res->style_any.style[i] = style;
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

	res = CHUNK_ALLOC (CellTilePtrMatrix, TILE_PTR_MATRIX);
	*((CellTileType *)&(res->type)) = TILE_PTR_MATRIX;

	/* TODO:
	 * If we wanted to get fancy we could use self similarity to decrease
	 * the number of subtiles.  However, this would increase the cost of
	 * applying changes later so I'm not sure it is worth the effort.
	 */
	switch (t->type) {
	case TILE_SIMPLE: {
		int i = TILE_SIZE_COL * TILE_SIZE_ROW;
		while (--i >= 0)
			res->ptr[i] = cell_tile_style_new (
				t->style_simple.style[0], TILE_SIMPLE);
		break;
	}
	case TILE_COL: {
		int i, r, c;
		for (i = r = 0 ; r < TILE_SIZE_ROW ; ++r)
			for (c = 0 ; c < TILE_SIZE_COL ; ++c)
				res->ptr[i++] = cell_tile_style_new (
					t->style_col.style[c], TILE_SIMPLE);
		break;
	}
	case TILE_ROW: {
		int i, r, c;
		for (i = r = 0 ; r < TILE_SIZE_ROW ; ++r)
			for (c = 0 ; c < TILE_SIZE_COL ; ++c)
				res->ptr[i++] = cell_tile_style_new (
					t->style_row.style[r], TILE_SIMPLE);
		break;
	}
	case TILE_MATRIX: {
		int i = TILE_SIZE_COL * TILE_SIZE_ROW;
		while (--i >= 0)
			res->ptr[i] = cell_tile_style_new (
				t->style_matrix.style[i], TILE_SIMPLE);
		break;
	}
	default: ;
	}

	return (CellTile *)res;
}

static CellTile *
cell_tile_matrix_set (CellTile *t)
{
	int r, c;
	CellTileStyleMatrix *res;

	g_return_val_if_fail (t != NULL, NULL);
	g_return_val_if_fail (TILE_SIMPLE <= t->type &&
			      TILE_MATRIX >= t->type, NULL);

	if (t->type == TILE_MATRIX)
		return t;

	res = (CellTileStyleMatrix *)cell_tile_style_new (NULL, TILE_MATRIX);

	switch (t->type) {
	case TILE_SIMPLE: {
		GnmStyle *tmp = t->style_simple.style[0];
		int i = TILE_SIZE_COL * TILE_SIZE_ROW;
		gnm_style_link_multiple (tmp, i);
		while (--i >= 0)
			res->style[i] = tmp;
		break;
	}

	case TILE_COL: {
		int i = 0;
		for (r = 0; r < TILE_SIZE_ROW; ++r)
			for (c = 0; c < TILE_SIZE_COL; ++c)
				gnm_style_link (res->style[i++] =
						t->style_col.style[c]);
		break;
	}

	case TILE_ROW: {
		int i = 0;
		for (r = 0; r < TILE_SIZE_ROW; ++r) {
			GnmStyle *tmp = t->style_row.style[r];
			gnm_style_link_multiple (tmp, TILE_SIZE_COL);
			for (c = 0; c < TILE_SIZE_COL; ++c)
				res->style[i++] = tmp;
		}
		break;
	}

	case TILE_MATRIX:
	default:
		g_assert_not_reached();
	}

	cell_tile_dtor (t);

	return (CellTile *)res;
}

/****************************************************************************/

static void
sheet_style_sanity_check (void)
{
	unsigned c, r;
	int i;

	for (c = 1, i = 0; i <= TILE_TOP_LEVEL; i++) {
		g_assert (c < G_MAXUINT / TILE_SIZE_COL);
		c *= TILE_SIZE_COL;
	}
	g_assert (c >= GNM_MAX_COLS);

	for (r = 1, i = 0; i <= TILE_TOP_LEVEL; i++) {
		g_assert (r < G_MAXUINT / TILE_SIZE_COL);
		r *= TILE_SIZE_ROW;
	}
	g_assert (r >= GNM_MAX_ROWS);

	g_assert (G_N_ELEMENTS (tile_heights) > TILE_TOP_LEVEL + 1);

	g_assert (G_N_ELEMENTS (tile_widths) > TILE_TOP_LEVEL + 1);
}

static void
sheet_style_init_size (Sheet *sheet, int cols, int rows)
{
	GnmStyle *default_style;
	int lc = 0, lr = 0, w = TILE_SIZE_COL, h = TILE_SIZE_ROW;

	while (w < cols) {
		w *= TILE_SIZE_COL;
		lc++;
	}
	while (h < rows) {
		h *= TILE_SIZE_ROW;
		lr++;
	}
	sheet->tile_top_level = MAX (lc, lr);

	if (active_sheet_count++ == 0) {
#if USE_TILE_POOLS
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
#endif
	}

	sheet->style_data = g_new (GnmSheetStyleData, 1);
	sheet->style_data->style_hash = sh_create ();

	{
		GnmColor *ap = style_color_auto_pattern ();
#warning "FIXME: Allocating a GnmColor here is dubious."
		sheet->style_data->auto_pattern_color = g_new (GnmColor, 1);
		*sheet->style_data->auto_pattern_color = *ap;
		sheet->style_data->auto_pattern_color->ref_count = 1;
		style_color_unref (ap);
	}

	default_style =  gnm_style_new_default ();
#if 0
	/* We can not do this, XL creates full page charts with background
	 * 'none' by default.  Then displays that as white. */
	if (sheet->sheet_type == GNM_SHEET_OBJECT) {
		gnm_style_set_back_color (default_style,
			gnm_color_new_rgb8 (0x50, 0x50, 0x50));
		gnm_style_set_pattern (default_style, 1);
	}
#endif
	sheet->style_data->default_style =
		sheet_style_find (sheet, default_style);
	sheet->style_data->styles =
		cell_tile_style_new (sheet->style_data->default_style,
				     TILE_SIMPLE);
}

void
sheet_style_init (Sheet *sheet)
{
	int cols = gnm_sheet_get_max_cols (sheet);
	int rows = gnm_sheet_get_max_rows (sheet);

	debug_style_optimize = gnm_debug_flag ("style-optimize");

	sheet_style_sanity_check ();

	sheet_style_init_size (sheet, cols, rows);
}

void
sheet_style_resize (Sheet *sheet, int cols, int rows)
{
	GnmStyleList *styles, *l;
	int old_cols = gnm_sheet_get_max_cols (sheet);
	int old_rows = gnm_sheet_get_max_rows (sheet);
	GnmRange save_range, new_full;

	/* Save the style for the surviving area.  */
	range_init (&save_range, 0, 0,
		    MIN (cols, old_cols) - 1, MIN (rows, old_rows) - 1);
	styles = sheet_style_get_range (sheet, &save_range);

	/* Build new empty structures.  */
	sheet_style_shutdown (sheet);
	sheet_style_init_size (sheet, cols, rows);

	/* Reapply styles.  */
	range_init (&new_full, 0, 0, cols - 1, rows - 1);
	for (l = styles; l; l = l->next) {
		GnmStyleRegion const *sr = l->data;
		GnmRange const *r = &sr->range;
		GnmStyle *style = sr->style;
		GnmRange newr;
		if (range_intersection (&newr, r, &new_full)) {
			gnm_style_ref (style);
			sheet_style_apply_range (sheet, &newr, style);
		}
	}

	style_list_free	(styles);
}

#if USE_TILE_POOLS
static void
cb_tile_pool_leak (gpointer data, gpointer user)
{
	CellTile *tile = data;
	g_printerr ("Leaking tile at %p.\n", (void *)tile);
}
#endif

void
sheet_style_shutdown (Sheet *sheet)
{
	GnmStyleHash *table;
	GnmRange r;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->style_data != NULL);

	/*
	 * Clear all styles.  This is an easy way to clear out all
	 * style dependencies.
	 */
	range_init_full_sheet (&r, sheet);
	sheet_style_set_range (sheet, &r, sheet_style_default (sheet));

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
	g_slist_free_full (sh_all_styles (table),
			   (GDestroyNotify)gnm_style_unlink);
	sh_destroy (table);
	style_color_unref (sheet->style_data->auto_pattern_color);

	g_free (sheet->style_data);
	sheet->style_data = NULL;

	if (--active_sheet_count == 0) {
#if USE_TILE_POOLS
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
#else
		if (tile_allocations)
			g_printerr ("Leaking %d style tiles.\n", tile_allocations);
#endif
	}
}

/**
 * sheet_style_set_auto_pattern_color:
 * @sheet: The sheet
 * @grid_color: The color
 *
 * Set the color for rendering auto colored patterns in this sheet.
 * Absorbs a reference to @pattern_color;
 **/
void
sheet_style_set_auto_pattern_color (Sheet *sheet, GnmColor *pattern_color)
{
	GnmColor *apc;
	int ref_count;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->style_data != NULL);

	apc = sheet->style_data->auto_pattern_color;
	ref_count = apc->ref_count;
	*apc = *pattern_color;
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
 * sheet_style_update_grid_color:
 *
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

static gboolean
tile_is_uniform (CellTile const *tile)
{
	const int s = tile_size[tile->type];
	GnmStyle const *st = tile->style_any.style[0];
	int i;

	for (i = 1; i < s; i++)
		if (tile->style_any.style[i] != st)
			return FALSE;

	return TRUE;
}

static void
vector_apply_pstyle (CellTile *tile, ReplacementStyle *rs,
		     int cc, int cr, int level, GnmRange const *indic)
{
	const CellTileType type = tile->type;
	const int ncols = tile_col_count[type];
	const int nrows = tile_row_count[type];
	const int w1 = tile_widths[level + 1] / ncols;
	const int h1 = tile_heights[level + 1] / nrows;
	const int fcol = indic->start.col;
	const int frow = indic->start.row;
	const int lcol = MIN (ncols - 1, indic->end.col);
	const int lrow = MIN (nrows - 1, indic->end.row);
	GnmSheetSize const *ss = gnm_sheet_get_size (rs->sheet);
	int r, c;
	GnmRange rng;

	for (r = frow; r <= lrow; r++) {
		GnmStyle **st = tile->style_any.style + ncols * r;
		rng.start.row = cr + h1 * r;
		rng.end.row = MIN (rng.start.row + (h1 - 1),
				   ss->max_rows - 1);
		for (c = fcol; c <= lcol; c++) {
			rng.start.col = cc + w1 * c;
			rng.end.col = MIN (rng.start.col + (w1 - 1),
					   ss->max_cols - 1);
			rstyle_apply (st + c, rs, &rng);
		}
	}
}

/*
 * Determine whether before applying a style in the area of apply_to
 * one needs to split the tile column-wise.
 *
 * If FALSE is returned then the tile need to be split to a TILE_PTR_MATRIX
 * because the current level is not fine-grained enough.
 *
 * If TRUE is returned, TILE_SIMPLE needs to be split into TILE_COL and
 * TILE_ROW needs to be split into TILE_MATRIX.  TILE_COL and TILE_MATRIX
 * should be kept.  In indic, the inclusive post-split indicies of the
 * range will be returned.
 *
 * If apply_to covers the entire tile, TRUE will be returned and the judgement
 * on splitting above should be ignored.  The indices in indic will be as-if
 * the split was done.
 */
static gboolean
col_indicies (int corner_col, int w, GnmRange const *apply_to,
	      GnmRange *indec)
{
	int i, tmp;

	i = apply_to->start.col - corner_col;
	if (i <= 0)
		indec->start.col = 0;
	else {
		tmp = i / w;
		if (i != tmp * w)
			return FALSE;
		indec->start.col = tmp;
	}

	i = 1 + apply_to->end.col - corner_col;
	tmp = i / w;
	if (tmp >= TILE_SIZE_COL)
		indec->end.col = TILE_SIZE_COL - 1;
	else {
		if (i != tmp * w)
			return FALSE;
		indec->end.col = tmp - 1;
	}

	return TRUE;
}

/* See docs for col_indicies.  Swap cols and rows.  */
static gboolean
row_indicies (int corner_row, int h, GnmRange const *apply_to,
	      GnmRange *indic)
{
	int i, tmp;

	i = apply_to->start.row - corner_row;
	if (i <= 0)
		indic->start.row = 0;
	else {
		int tmp = i / h;
		if (i != tmp * h)
			return FALSE;
		indic->start.row = tmp;
	}

	i = 1 + apply_to->end.row - corner_row;
	tmp = i / h;
	if (tmp >= TILE_SIZE_ROW)
		indic->end.row = TILE_SIZE_ROW - 1;
	else {
		if (i != tmp * h)
			return FALSE;
		indic->end.row = tmp - 1;
	}

	return TRUE;
}

/*
 * cell_tile_apply: This is the primary logic for making changing areas in the
 * tree.  It could be further optimised if it becomes a bottle neck.
 */
static void
cell_tile_apply (CellTile **tile, int level,
		 int corner_col, int corner_row,
		 GnmRange const *apply_to,
		 ReplacementStyle *rs)
{
	int const width = tile_widths[level+1];
	int const height = tile_heights[level+1];
	int const w = tile_widths[level];
	int const h = tile_heights[level];
	gboolean const full_width = (apply_to->start.col <= corner_col &&
				     apply_to->end.col >= (corner_col+width-1));
	gboolean const full_height = (apply_to->start.row <= corner_row &&
				      apply_to->end.row >= (corner_row+height-1));
	GnmRange indic;
	CellTileType type;
	int c, r, i;

	g_return_if_fail (TILE_TOP_LEVEL >= level && level >= 0);
	g_return_if_fail (tile != NULL);
	g_return_if_fail (*tile != NULL);

	type = (*tile)->type;
	g_return_if_fail (TILE_SIMPLE <= type && type <= TILE_PTR_MATRIX);

	/* applying the same style to part of a simple-tile is a nop */
	if (type == TILE_SIMPLE &&
	    (*tile)->style_simple.style[0] == rs->new_style)
		return;

	/*
	 * Indices for the whole tile assuming a split to matrix.
	 * We can still use these indices if we don't split either way.
	 */
	indic.start.col = 0;
	indic.start.row = 0;
	indic.end.col = TILE_SIZE_COL - 1;
	indic.end.row = TILE_SIZE_ROW - 1;

	if (type == TILE_PTR_MATRIX)
		goto drill_down;
	else if (full_width && full_height)
		goto apply;
	else if (full_height) {
		if (!col_indicies (corner_col, w, apply_to, &indic))
			goto split_to_ptr_matrix;

		switch (type) {
		case TILE_SIMPLE: {
			CellTile *res;
			type = TILE_COL;
			res = cell_tile_style_new (
				(*tile)->style_simple.style[0],
				type);
			cell_tile_dtor (*tile);
			*tile = res;
			/* Fall through */
		}
		case TILE_COL:
		case TILE_MATRIX:
			goto apply;
		case TILE_ROW:
			goto split_to_matrix;
		default:
			g_assert_not_reached ();
		}
	} else if (full_width) {
		if (!row_indicies (corner_row, h, apply_to, &indic))
			goto split_to_ptr_matrix;
		switch (type) {
		case TILE_SIMPLE: {
			CellTile *res;

			type = TILE_ROW;
			res = cell_tile_style_new (
				(*tile)->style_simple.style[0],
				type);
			cell_tile_dtor (*tile);
			*tile = res;
			/* Fall through */
		}
		case TILE_ROW:
		case TILE_MATRIX:
			goto apply;
		case TILE_COL:
			goto split_to_matrix;
		default:
			g_assert_not_reached ();
		}
	} else {
		if (col_indicies (corner_col, w, apply_to, &indic) &&
		    row_indicies (corner_row, h, apply_to, &indic))
			goto split_to_matrix;
		else
			goto split_to_ptr_matrix;
	}

	g_assert_not_reached ();

split_to_matrix:
	*tile = cell_tile_matrix_set (*tile);

apply:
	vector_apply_pstyle (*tile, rs, corner_col, corner_row, level, &indic);

try_optimize:
	{
		CellTileOptimize cto;
		cto.ss = gnm_sheet_get_size (rs->sheet);
		cto.recursion = FALSE;
		cell_tile_optimize (tile, level, &cto, corner_col, corner_row);
	}
	return;

split_to_ptr_matrix:
	/*
	 * We get here when apply_to's corners are not on a TILE_MATRIX grid.
	 * Split to pointer matrix whose element tiles will have a finer grid.
	 */
	g_return_if_fail (type != TILE_PTR_MATRIX);
	{
		CellTile *res = cell_tile_ptr_matrix_new (*tile);
		cell_tile_dtor (*tile);
		*tile = res;
		type = TILE_PTR_MATRIX;
	}

drill_down:
	g_return_if_fail (type == TILE_PTR_MATRIX);
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

			cell_tile_apply ((*tile)->ptr_matrix.ptr + i + c,
					 level - 1, cc, cr, apply_to, rs);
		}
	}
	goto try_optimize;
}

/* Handler for foreach_tile.
 *
 * "width" and "height" refer to tile size which may extend beyond
 * the range supplied to foreach_tile and even beyond the sheet.
 */
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
	int const width = tile_widths[level+1];
	int const height = tile_heights[level+1];
	int const w = tile_widths[level];
	int const h = tile_heights[level];
	int c, r, i, last;

	g_return_if_fail (TILE_TOP_LEVEL >= level && level >= 0);
	g_return_if_fail (tile != NULL);

	switch (tile->type) {
	case TILE_SIMPLE:
		handler (tile->style_simple.style[0],
			 corner_col, corner_row, width, height,
			 apply_to, user);
		break;

	case TILE_COL:
		if (apply_to != NULL) {
			c = (apply_to->start.col - corner_col) / w;
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
			handler (tile->style_col.style[c],
				 corner_col + c*w, corner_row, w, height,
				 apply_to, user);
		break;

	case TILE_ROW:
		if (apply_to != NULL) {
			r = (apply_to->start.row - corner_row) / h;
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
			handler (tile->style_row.style[r],
				 corner_col, corner_row + r*h, width, h,
				 apply_to, user);
		break;

	case TILE_MATRIX:
	case TILE_PTR_MATRIX:
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
					handler (tile->style_matrix.style[r*TILE_SIZE_COL+c],
						 corner_col + c * w,
						 corner_row + r * h,
						 w, h, apply_to, user);
				} else {
					foreach_tile (
						tile->ptr_matrix.ptr[c + r*TILE_SIZE_COL],
						level-1, cc, cr, apply_to, handler, user);
				}
			}
		}
		break;

	default:
		g_warning ("Adaptive Quad Tree corruption !");
	}
}

/*
 * cell_tile_apply_pos: This is an simplified version of cell_tile_apply.  It
 * does not need all the bells and whistles because it operates on single cells.
 */
static void
cell_tile_apply_pos (CellTile **tile, int level,
		     int col, int row,
		     ReplacementStyle *rs)
{
	CellTile *tmp;
	CellTileType type;
	GnmRange rng;

	g_return_if_fail (col >= 0);
	g_return_if_fail (col < gnm_sheet_get_max_cols (rs->sheet));
	g_return_if_fail (row >= 0);
	g_return_if_fail (row < gnm_sheet_get_max_rows (rs->sheet));

	range_init (&rng, col, row, col, row);

tail_recursion:
	g_return_if_fail (TILE_TOP_LEVEL >= level && level >= 0);
	g_return_if_fail (tile != NULL);
	g_return_if_fail (*tile != NULL);

	tmp = *tile;
	type = tmp->type;
	g_return_if_fail (TILE_SIMPLE <= type && type <= TILE_PTR_MATRIX);

	if (level > 0) {
		int const w = tile_widths[level];
		int const c = col / w;
		int const h = tile_heights[level];
		int const r = row / h;

		if (type != TILE_PTR_MATRIX) {
			/* applying the same style to part of a simple-tile is a nop */
			if (type == TILE_SIMPLE &&
			    (*tile)->style_simple.style[0] == rs->new_style)
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
		*tile = tmp = cell_tile_matrix_set (tmp);

	g_return_if_fail (tmp->type == TILE_MATRIX);
	rstyle_apply (tmp->style_matrix.style + row * TILE_SIZE_COL + col,
		      rs,
		      &rng);
}

/**
 * sheet_style_set_range:
 * @sheet:
 * @range:
 * @style: #GnmStyle
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

	rstyle_ctor_style (&rs, style, sheet);
	cell_tile_apply (&sheet->style_data->styles,
			 sheet->tile_top_level, 0, 0,
			 range, &rs);
	rstyle_dtor (&rs);
}

/**
 * sheet_style_apply_col:
 * @sheet:
 * @col:
 * @style: #GnmStyle
 *
 * NOTE: This is a simple wrapper for now.  When we support col/row styles it
 *	will make life easier.
 *
 * Apply a partial style to a full col.
 * The routine absorbs a reference to the partial style.
 **/
void
sheet_style_apply_col (Sheet *sheet, int col, GnmStyle *pstyle)
{
	GnmRange r;
	range_init_cols (&r, sheet, col, col);
	sheet_style_apply_range (sheet, &r, pstyle);
}

/**
 * sheet_style_apply_row:
 * @sheet:
 * @row:
 * @style: #GnmStyle
 *
 * NOTE: This is a simple wrapper for now.  When we support col/row styles it
 *	will make life easier.
 *
 * Apply a partial style to a full col.
 * The routine absorbs a reference to the partial style.
 **/
void
sheet_style_apply_row (Sheet  *sheet, int row, GnmStyle *pstyle)
{
	GnmRange r;
	range_init_rows (&r, sheet, row, row);
	sheet_style_apply_range (sheet, &r, pstyle);
}

/**
 * sheet_style_apply_pos:
 * @sheet:
 * @col:
 * @row:
 * @style: #GnmStyle
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

	rstyle_ctor_pstyle (&rs, pstyle, sheet);
	cell_tile_apply_pos (&sheet->style_data->styles,
			     sheet->tile_top_level, col, row,
			     &rs);
	rstyle_dtor (&rs);
}
/**
 * sheet_style_set_pos:
 * @sheet:
 * @col:
 * @row:
 * @style:
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

	rstyle_ctor_style (&rs, style, sheet);
	cell_tile_apply_pos (&sheet->style_data->styles,
			     sheet->tile_top_level, col, row,
			     &rs);
	rstyle_dtor (&rs);
}

/**
 * sheet_style_default:
 * @sheet:
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
 * sheet_style_get:
 * @sheet: #Sheet
 * @col:
 * @row:
 *
 * Find the fully qualified style applicable to the specified cellpos.
 * Does _not_ add a reference.
 **/
GnmStyle const *
sheet_style_get (Sheet const *sheet, int col, int row)
{
	int level = sheet->tile_top_level;
	CellTile *tile = sheet->style_data->styles;

	while (1) {
		int width = tile_widths[level];
		int height = tile_heights[level];
		int c = col / width;
		int r = row / height;

		g_return_val_if_fail (tile != NULL, NULL);
		g_return_val_if_fail (0 <= c && c < TILE_SIZE_COL, NULL);
		g_return_val_if_fail (0 <= r && r < TILE_SIZE_ROW, NULL);

		switch (tile->type) {
		case TILE_SIMPLE:
			return tile->style_simple.style[0];
		case TILE_COL:
			return tile->style_col.style[c];
		case TILE_ROW:
			return tile->style_row.style[r];
		case TILE_MATRIX:
			return tile->style_matrix.style[r * TILE_SIZE_COL + c];

		case TILE_PTR_MATRIX:
			g_return_val_if_fail (level > 0, NULL);

			level--;
			tile = tile->ptr_matrix.ptr[r * TILE_SIZE_COL + c];
			col -= c * width;
			row -= r * height;
			continue;

		default:
			g_warning ("Adaptive Quad Tree corruption !");
			return NULL;
		}
	}
}

#define border_null(b)	((b) == none || (b) == NULL)

static void
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

	if (left != none && border_null (sr->vertical[i]))
		sr->vertical[i] = left;
	v = border_null (right) ? left : right;

	while (i <= end) {
		sr->styles[i] = style;
		if (top != none && border_null (sr->top[i]))
			sr->top[i] = top;
		sr->bottom[i] = bottom;
		sr->vertical[++i] = v;
	}
	if (border_null (right))
		sr->vertical[i] = right;
}

static void
get_style_row (CellTile const *tile, int level,
	       int corner_col, int corner_row,
	       GnmStyleRow *sr)
{
	int const width = tile_widths[level+1];
	int const w = tile_widths[level];
	int const h = tile_heights[level];
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
		style_row (tile->style_any.style[r],
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
				style_row (styles[c],
					   corner_col, corner_col + w - 1, sr, TRUE);
		} else {
			CellTile * const *tiles = tile->ptr_matrix.ptr + r*TILE_SIZE_COL;

			g_return_if_fail (level > 0);

			for ( level-- ; c <= last_c ; c++, corner_col += w)
				get_style_row (tiles[c], level,
					       corner_col, corner_row, sr);
		}
	}
}

/**
 * sheet_style_get_row:
 * @sheet: #Sheet
 * @sr: #GnmStyleRow
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
	sr->vertical[sr->start_col] = gnm_style_border_none ();
	get_style_row (sheet->style_data->styles, sheet->tile_top_level, 0, 0, sr);
}

static void
cb_get_row (GnmStyle *style,
	    int corner_col, G_GNUC_UNUSED int corner_row,
	    int width, G_GNUC_UNUSED int height,
	    GnmRange const *apply_to, gpointer user_)
{
	GnmStyle **res = user_;
	int i;

	/* The given dimensions refer to the tile, not the area. */
	width = MIN (width, apply_to->end.col - corner_col + 1);

	for (i = 0; i < width; i++)
		res[corner_col + i] = style;
}

GnmStyle **
sheet_style_get_row2 (Sheet const *sheet, int row)
{
	GnmRange r;
	GnmStyle **res = g_new (GnmStyle *, gnm_sheet_get_max_cols (sheet));

	range_init_rows (&r, sheet, row, row);

	foreach_tile (sheet->style_data->styles,
		      sheet->tile_top_level, 0, 0, &r,
		      cb_get_row, res);

	return res;
}


/**
 * style_row_init:
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

	/* alias the arrays for easy access so that array[col] is valid
	 * for all elements start_col-1 .. end_col+1 inclusive.
	 * Note that this means that in some cases array[-1] is legal.
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
		(*prev_vert)[col] = sr->top[col] = none;
	sr->vertical	 [start_col-1] = sr->vertical	   [end_col+1] =
	next_sr->vertical[start_col-1] = next_sr->vertical[end_col+1] =
	next_sr->top	 [start_col-1] = next_sr->top	   [end_col+1] =
	next_sr->bottom	 [start_col-1] = next_sr->bottom  [end_col+1] = none;
}

/**
 * sheet_style_apply_range:
 * @sheet:
 * @range:
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

	rstyle_ctor_pstyle (&rs, pstyle, sheet);
	cell_tile_apply (&sheet->style_data->styles,
			 sheet->tile_top_level, 0, 0,
			 range, &rs);
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
 * sheet_style_apply_border:
 * @sheet:
 * @range:
 * @borders:
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

	if (borders[GNM_STYLE_BORDER_TOP]) {
		/* 1.1 top inner */
		GnmRange r = *range;
		r.end.row = r.start.row;
		apply_border (sheet, &r, GNM_STYLE_BORDER_TOP,
			      borders[GNM_STYLE_BORDER_TOP]);

		/* 1.2 top outer */
		r.start.row--;
		if (r.start.row >= 0) {
			r.end.row = r.start.row;
			apply_border (sheet, &r, GNM_STYLE_BORDER_BOTTOM,
				      gnm_style_border_none ());
		}
	}

	if (borders[GNM_STYLE_BORDER_BOTTOM]) {
		/* 2.1 bottom inner */
		GnmRange r = *range;
		r.start.row = r.end.row;
		apply_border (sheet, &r, GNM_STYLE_BORDER_BOTTOM,
			      borders[GNM_STYLE_BORDER_BOTTOM]);

		/* 2.2 bottom outer */
		r.end.row++;
		if (r.end.row < gnm_sheet_get_last_row (sheet)) {
			r.start.row = r.end.row;
			apply_border (sheet, &r, GNM_STYLE_BORDER_TOP,
				      gnm_style_border_none ());
		}
	}

	if (borders[GNM_STYLE_BORDER_LEFT]) {
		/* 3.1 left inner */
		GnmRange r = *range;
		r.end.col = r.start.col;
		apply_border (sheet, &r, GNM_STYLE_BORDER_LEFT,
			      borders[GNM_STYLE_BORDER_LEFT]);

		/* 3.2 left outer */
		r.start.col--;
		if (r.start.col >= 0) {
			r.end.col = r.start.col;
			apply_border (sheet, &r, GNM_STYLE_BORDER_RIGHT,
				      gnm_style_border_none ());
		}
	}

	if (borders[GNM_STYLE_BORDER_RIGHT]) {
		/* 4.1 right inner */
		GnmRange r = *range;
		r.start.col = r.end.col;
		apply_border (sheet, &r, GNM_STYLE_BORDER_RIGHT,
			      borders[GNM_STYLE_BORDER_RIGHT]);

		/* 4.2 right outer */
		r.end.col++;
		if (r.end.col < gnm_sheet_get_last_col (sheet)) {
			r.start.col = r.end.col;
			apply_border (sheet, &r, GNM_STYLE_BORDER_LEFT,
				      gnm_style_border_none ());
		}
	}

	/* Interiors horizontal : prefer top */
	if (borders[GNM_STYLE_BORDER_HORIZ] != NULL) {
		/* 5.1 horizontal interior top */
		if (range->start.row != range->end.row) {
			GnmRange r = *range;
			++r.start.row;
			apply_border (sheet, &r, GNM_STYLE_BORDER_TOP,
				      borders[GNM_STYLE_BORDER_HORIZ]);
		}
		/* 5.2 interior bottom */
		if (range->start.row != range->end.row) {
			GnmRange r = *range;
			--r.end.row;
			apply_border (sheet, &r, GNM_STYLE_BORDER_BOTTOM,
				      gnm_style_border_none ());
		}
	}

	/* Interiors vertical: prefer left */
	if (borders[GNM_STYLE_BORDER_VERT] != NULL) {
		/* 6.1 vertical interior left */
		if (range->start.col != range->end.col) {
			GnmRange r = *range;
			++r.start.col;
			apply_border (sheet, &r, GNM_STYLE_BORDER_LEFT,
				      borders[GNM_STYLE_BORDER_VERT]);
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
	if (borders[GNM_STYLE_BORDER_DIAG] != NULL) {
		pstyle = gnm_style_new ();
		pstyle_set_border (pstyle, borders[GNM_STYLE_BORDER_DIAG],
				   GNM_STYLE_BORDER_DIAG);
	}
	if (borders[GNM_STYLE_BORDER_REV_DIAG]) {
		if (pstyle == NULL)
			pstyle = gnm_style_new ();
		pstyle_set_border (pstyle, borders[GNM_STYLE_BORDER_REV_DIAG],
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
		   G_GNUC_UNUSED int corner_col, G_GNUC_UNUSED int corner_row,
		   G_GNUC_UNUSED int width, G_GNUC_UNUSED int height,
		   G_GNUC_UNUSED GnmRange const *apply_to, FindConflicts *ptr)
{
	ptr->conflicts = gnm_style_find_conflicts (ptr->accum, style, ptr->conflicts);
}

static void
border_mask_internal (gboolean *known, GnmBorder **borders,
		      GnmBorder const *b, GnmStyleBorderLocation l)
{
	if (!known[l]) {
		known[l] = TRUE;
		borders[l] = (GnmBorder *)b;
		gnm_style_border_ref (borders[l]);
	} else if (borders[l] != b && borders[l] != NULL) {
		gnm_style_border_unref (borders[l]);
		borders[l] = NULL;
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
	GnmBorder const *b = vec[first];

	if (b == NULL)
		b = gnm_style_border_none ();
	while (first++ < last) {
		GnmBorder const *tmp = vec[first];
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
 * sheet_style_get_uniform:
 * @sheet:
 * @range:
 * @borders:
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
	gboolean known[GNM_STYLE_BORDER_EDGE_MAX];
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
			known[i] = FALSE;
			borders[i] = gnm_style_border_ref ((GnmBorder *)none);
		}
	} else {
		for (i = GNM_STYLE_BORDER_TOP ; i < GNM_STYLE_BORDER_EDGE_MAX ; i++)
			known[i] = TRUE;
	}

	user.accum = *style;
	user.conflicts = 0; /* no conflicts yet */
	foreach_tile (sheet->style_data->styles,
		      sheet->tile_top_level, 0, 0, r,
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
		sr.top[col] = none;

	/* merge the bottom of the previous row */
	if (r->start.row > 0) {
		GnmBorder const ** roller;
		sr.row = r->start.row - 1;
		sheet_style_get_row (sheet, &sr);
		roller = sr.top; sr.top = sr.bottom; sr.bottom = roller;
	}

	/*
	 * TODO: The border handling is tricky and currently VERY slow for
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

		border_mask (known, borders, sr.vertical[r->start.col],
			     GNM_STYLE_BORDER_LEFT);
		border_mask (known, borders, sr.vertical[r->end.col+1],
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
	if (r->end.row < gnm_sheet_get_last_row (sheet)) {
		sr.row = r->end.row + 1;
		sheet_style_get_row (sheet, &sr);
	}
	border_mask_vec (known, borders, sr.top, r->start.col, r->end.col,
			 GNM_STYLE_BORDER_BOTTOM);

	return user.conflicts;
}

/**
 * sheet_style_relocate:
 * @rinfo:
 *
 * Slide the styles from the origin region to the new position.
 */
void
sheet_style_relocate (GnmExprRelocateInfo const *rinfo)
{
	GnmCellPos corner;
	GnmStyleList *styles;

	g_return_if_fail (rinfo != NULL);

	styles = sheet_style_get_range (rinfo->origin_sheet, &rinfo->origin);

	sheet_style_set_range (rinfo->origin_sheet, &rinfo->origin,
			       sheet_style_default (rinfo->origin_sheet));
	corner.col = rinfo->origin.start.col + rinfo->col_offset;
	corner.row = rinfo->origin.start.row + rinfo->row_offset;
	sheet_style_set_list (rinfo->target_sheet, &corner, styles, NULL, NULL);
	style_list_free	(styles);
}

/**
 * sheet_style_insert_colrow:
 * @rinfo:
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
		styles = sheet_style_get_range (rinfo->origin_sheet,
			       range_init (&r, col, 0, col, gnm_sheet_get_last_row (rinfo->origin_sheet)));
		if (o > 0)
			for (ptr = styles ; ptr != NULL ; ptr = ptr->next)
				((GnmStyleRegion *)ptr->data)->range.end.col = o;

	} else if (rinfo->row_offset != 0) {
		int const o = rinfo->row_offset - 1;
		int row = corner.row - 1;
		if (row < 0)
			row = 0;
		corner.col = 0;
		range_init_rows (&r, rinfo->origin_sheet, row, row);
		styles = sheet_style_get_range (rinfo->origin_sheet, &r);
		if (o > 0)
			for (ptr = styles ; ptr != NULL ; ptr = ptr->next)
				((GnmStyleRegion *)ptr->data)->range.end.row = o;
	}

	sheet_style_relocate (rinfo);

	if (styles != NULL) {
		sheet_style_set_list (rinfo->target_sheet, &corner, styles,
				      NULL, NULL);
		style_list_free	(styles);
	}
}

static void
cb_style_extent (GnmStyle *style,
		 int corner_col, int corner_row, int width, int height,
		 GnmRange const *apply_to, gpointer user)
{
	GnmRange *res = user;
	if (gnm_style_visible_in_blank (style)) {
		int tmp;

		/* The given dimensions refer to the tile, not the area. */
		width = MIN (width, apply_to->end.col - corner_col + 1);
		height = MIN (height, apply_to->end.row - corner_row + 1);

		tmp = corner_col+width-1;
		if (res->end.col < tmp)
			res->end.col = tmp;
		if (res->start.col > corner_col)
			res->start.col = corner_col;

		tmp = corner_row+height-1;
		if (res->end.row < tmp)
			res->end.row = tmp;
		if (res->start.row > corner_row)
			res->start.row = corner_row;
	}
}

/**
 * sheet_style_get_extent:
 * @sheet: sheet to measure
 * @r: starting range and resulting range
 *
 * A simple implementation that finds the smallest range containing all visible styles
 * and containing @res.
 */
void
sheet_style_get_extent (Sheet const *sheet, GnmRange *res)
{
	GnmRange r;

	range_init_full_sheet (&r, sheet);
	foreach_tile (sheet->style_data->styles,
		      sheet->tile_top_level, 0, 0, &r,
		      cb_style_extent, res);
}

struct cb_nondefault_extent {
	GnmRange *res;
	GnmStyle **col_defaults;
};

static void
cb_nondefault_extent (GnmStyle *style,
		      int corner_col, int corner_row, int width, int height,
		      GnmRange const *apply_to, gpointer user_)
{
	struct cb_nondefault_extent *user = user_;
	GnmRange *res = user->res;
	int i;

	for (i = 0; i < width; i++) {
		int col = corner_col + i;
		if (col >= apply_to->start.col &&
		    col <= apply_to->end.col &&
		    style != user->col_defaults[col]) {
			int max_row = MIN (corner_row + height - 1,
					   apply_to->end.row);
			int min_row = MAX (corner_row, apply_to->start.row);

			res->start.col = MIN (col, res->start.col);
			res->start.row = MIN (min_row, res->start.row);

			res->end.col = MAX (col, res->end.col);
			res->end.row = MAX (max_row, res->end.row);
		}
	}
}

void
sheet_style_get_nondefault_extent (Sheet const *sheet, GnmRange *extent,
				   const GnmRange *src, GnmStyle **col_defaults)
{
	struct cb_nondefault_extent user;
	user.res = extent;
	user.col_defaults = col_defaults;
	foreach_tile (sheet->style_data->styles,
		      sheet->tile_top_level, 0, 0, src,
		      cb_nondefault_extent, &user);
}

struct cb_is_default {
	gboolean res;
	GnmStyle **col_defaults;
};

static void
cb_is_default (GnmStyle *style,
	       int corner_col, G_GNUC_UNUSED int corner_row,
	       int width, G_GNUC_UNUSED int height,
	       GnmRange const *apply_to, gpointer user_)
{
	struct cb_is_default *user = user_;
	int i;

	/* The given "width" refers to the tile, not the area. */
	width = MIN (width, apply_to->end.col - corner_col + 1);

	for (i = 0; user->res && i < width; i++) {
		if (style != user->col_defaults[corner_col + i])
			user->res = FALSE;
	}
}

gboolean
sheet_style_is_default (Sheet const *sheet, const GnmRange *r, GnmStyle **col_defaults)
{
	struct cb_is_default user;

	user.res = TRUE;
	user.col_defaults = col_defaults;

	foreach_tile (sheet->style_data->styles,
		      sheet->tile_top_level, 0, 0, r,
		      cb_is_default, &user);

	return user.res;
}

struct cb_get_nondefault {
	guint8 *res;
	GnmStyle **col_defaults;
};

static void
cb_get_nondefault (GnmStyle *style,
		   int corner_col, G_GNUC_UNUSED int corner_row,
		   int width, G_GNUC_UNUSED int height,
		   GnmRange const *apply_to, gpointer user_)
{
	struct cb_get_nondefault *user = user_;
	int i;

	/* The given dimensions refer to the tile, not the area. */
	width = MIN (width, apply_to->end.col - corner_col + 1);
	height = MIN (height, apply_to->end.row - corner_row + 1);

	for (i = 0; i < width; i++) {
		if (style != user->col_defaults[corner_col + i]) {
			int j;
			for (j = 0; j < height; j++)
				user->res[corner_row + j] = 1;
			break;
		}
	}
}

guint8 *
sheet_style_get_nondefault_rows (Sheet const *sheet, GnmStyle **col_defaults)
{
	struct cb_get_nondefault user;
	GnmRange r;

	range_init_full_sheet (&r, sheet);

	user.res = g_new0 (guint8, gnm_sheet_get_max_rows (sheet));
	user.col_defaults = col_defaults;

	foreach_tile (sheet->style_data->styles,
		      sheet->tile_top_level, 0, 0, &r,
		      cb_get_nondefault, &user);

	return user.res;
}

struct cb_most_common {
	GHashTable *h;
	int l;
	gboolean is_col;
};

static void
cb_most_common (GnmStyle *style,
		int corner_col, int corner_row, int width, int height,
		GnmRange const *apply_to, gpointer user)
{
	struct cb_most_common *cmc = user;
	int *counts = g_hash_table_lookup (cmc->h, style);
	int i;
	if (!counts) {
		counts = g_new0 (int, cmc->l);
		g_hash_table_insert (cmc->h, style, counts);
	}

	/* The given dimensions refer to the tile, not the area. */
	width = MIN (width, apply_to->end.col - corner_col + 1);
	height = MIN (height, apply_to->end.row - corner_row + 1);

	if (cmc->is_col)
		for (i = 0; i < width; i++)
			counts[corner_col + i] += height;
	else
		for (i = 0; i < height; i++)
			counts[corner_row + i] += width;
}

/**
 * sheet_style_most_common:
 * @sheet: sheet to inspect
 * @is_col: if %TRUE, look for common styles in columns; if FALSE, look in rows.
 *
 * Returns: an array of styles describing the most common styles, one per column
 * or row.
 */
GnmStyle **
sheet_style_most_common (Sheet const *sheet, gboolean is_col)
{
	GnmRange r;
	struct cb_most_common cmc;
	int *max;
	GnmStyle **res;
	GHashTableIter iter;
	gpointer key, value;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	range_init_full_sheet (&r, sheet);
	cmc.h = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	cmc.l = colrow_max (is_col, sheet);
	cmc.is_col = is_col;
	foreach_tile (sheet->style_data->styles,
		      sheet->tile_top_level, 0, 0, &r,
		      cb_most_common, &cmc);

	max = g_new0 (int, cmc.l);
	res = g_new0 (GnmStyle *, cmc.l);
	g_hash_table_iter_init (&iter, cmc.h);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		int *counts = value;
		GnmStyle *style = key;
		int j;
		for (j = 0; j < cmc.l; j++) {
			/* FIXME: we really ought to break ties in a
			   consistent way that does not depend on hash
			   order.  */
			if (counts[j] > max[j]) {
				max[j] = counts[j];
				res[j] = style;
			}
		}
	}
	g_hash_table_destroy (cmc.h);
	g_free (max);

	return res;
}

/****************************************************************************/

/**
 * gnm_style_region_new:
 * @range: #GnmRange
 * @style: #GnmStyle
 *
 * Returns: (transfer full): the newly allocated #GnmStyleRegion.
 **/
GnmStyleRegion *
gnm_style_region_new (GnmRange const *range, GnmStyle *style)
{
	GnmStyleRegion *sr;

	sr = g_new (GnmStyleRegion, 1);
	sr->range = *range;
	sr->style = style;
	gnm_style_ref (style);

	return sr;
}

void
gnm_style_region_free (GnmStyleRegion *sr)
{
	g_return_if_fail (sr != NULL);

	gnm_style_unref (sr->style);
	sr->style = NULL;
	g_free (sr);
}

static GnmStyleRegion *
gnm_style_region_copy (GnmStyleRegion *sr)
{
	GnmStyleRegion *res = g_new (GnmStyleRegion, 1);
	*res = *sr;
	gnm_style_ref (sr->style);
	return res;
}

GType
gnm_style_region_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmStyleRegion",
			 (GBoxedCopyFunc)gnm_style_region_copy,
			 (GBoxedFreeFunc)gnm_style_region_free);
	}
	return t;
}


static gboolean
debug_style_list (void)
{
	static int debug = -1;
	if (debug < 0)
		debug = gnm_debug_flag ("style-list");
	return debug;
}

typedef struct {
	GPtrArray *accum;
	GHashTable *by_tl, *by_br;
	guint64 area;
	gboolean (*style_equal) (GnmStyle const *a, GnmStyle const *b);
	gboolean (*style_filter) (GnmStyle const *style);
	GnmSheetSize const *sheet_size;
} ISL;

static gboolean
merge_ranges (GnmRange *a, GnmRange const *b)
{
	if (a->start.row == b->start.row &&
	    a->end.row == b->end.row &&
	    a->end.col + 1 == b->start.col) {
		/* "a" is just left of "b".  */
		a->end.col = b->end.col;
		return TRUE;
	}

	if (a->start.col == b->start.col &&
	    a->end.col == b->end.col &&
	    a->end.row + 1 == b->start.row) {
		/* "a" is just on top of "b".  */
		a->end.row = b->end.row;
		return TRUE;
	}

	/* Punt.  */
	return FALSE;
}

static gboolean
try_merge_pair (ISL *data, unsigned ui1, unsigned ui2)
{
	GnmStyleRegion *a;
	GnmStyleRegion *b;

	if (ui1 >= data->accum->len || ui2 >= data->accum->len)
		return FALSE;

	a = g_ptr_array_index (data->accum, ui1);
	b = g_ptr_array_index (data->accum, ui2);

	if (!data->style_equal (a->style, b->style))
		return FALSE;

	if (!merge_ranges (&a->range, &b->range))
		return FALSE;

	gnm_style_region_free (b);
	g_ptr_array_remove_index (data->accum, ui2);

	return TRUE;
}

static void
cb_style_list_add_node (GnmStyle *style,
			int corner_col, int corner_row, int width, int height,
			GnmRange const *apply_to, gpointer user_)
{
	ISL *data = user_;
	GnmSheetSize const *ss = data->sheet_size;
	GnmStyleRegion *sr;
	GnmRange range;

	/* Can this even happen? */
	if (corner_col >= ss->max_cols || corner_row >= ss->max_rows)
		return;

	if (data->style_filter && !data->style_filter (style))
		return;

	range.start.col = corner_col;
	range.start.row = corner_row;
	range.end.col = MIN (corner_col + width - 1, ss->max_cols - 1);
	range.end.row = MIN (corner_row + height - 1, ss->max_rows - 1);

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

	data->area += (guint64)range_width (&range) * range_height (&range);

	sr = gnm_style_region_new (&range, style);
	g_ptr_array_add (data->accum, sr);

	while (try_merge_pair (data, data->accum->len - 2, data->accum->len - 1))
		/* Nothing */;
}

static void
verify_hashes (ISL *data)
{
	GHashTable *by_tl = data->by_tl;
	GHashTable *by_br = data->by_br;
	unsigned ui;
	guint64 area = 0;

	g_return_if_fail (g_hash_table_size (by_tl) == data->accum->len);
	g_return_if_fail (g_hash_table_size (by_br) == data->accum->len);

	for (ui = 0; ui < data->accum->len; ui++) {
		GnmStyleRegion *sr = g_ptr_array_index (data->accum, ui);
		g_return_if_fail (g_hash_table_lookup (by_tl, &sr->range.start) == sr);
		g_return_if_fail (g_hash_table_lookup (by_br, &sr->range.end) == sr);
		area += range_height (&sr->range) *
			(guint64)range_width (&sr->range);
	}

	g_return_if_fail (area == data->area);
}

static void
merge_vertical_stripes (ISL *data)
{
	unsigned ui;
	GHashTable *by_tl = data->by_tl;
	GHashTable *by_br = data->by_br;
	gboolean debug = debug_style_list ();
	gboolean paranoid = debug;

	for (ui = 0; ui < data->accum->len; ui++) {
		GnmStyleRegion *a = g_ptr_array_index (data->accum, ui);
		GnmStyleRegion *c;
		GnmCellPos cr;
		GSList *Bs = NULL, *l;
		gboolean fail = FALSE;

		/*  We're looking for the setup below and extend Bs down  */
		/*  taking over part of C which is then extended to       */
		/*  include all of A.                                     */
		/*                                                        */
		/*             +----+                                     */
		/*             |    +---------+                           */
		/*   +---------+ B1 |   B2    |                           */
		/*   |    A    |    |         |                           */
		/*   +---------+----+---------+                           */
		/*   |           C            |                           */
		/*   +------------------------+                           */

		cr.col = a->range.start.col;
		cr.row = a->range.end.row + 1;
		c = g_hash_table_lookup (by_tl, &cr);
		if (!c || !data->style_equal (a->style, c->style))
			continue;

		cr.col = c->range.end.col;
		cr.row = a->range.end.row;
		while (cr.col > a->range.end.col) {
			GnmStyleRegion *b = g_hash_table_lookup (by_br, &cr);
			if (!b || !data->style_equal (a->style, b->style)) {
				fail = TRUE;
				break;
			}
			Bs = g_slist_prepend (Bs, b);
			cr.col = b->range.start.col - 1;
		}
		if (fail || cr.col != a->range.end.col) {
			g_slist_free (Bs);
			continue;
		}

		if (debug) {
			g_printerr ("Vertical stripe merge:\n");
			g_printerr ("A: %s\n", range_as_string (&a->range));
			for (l = Bs; l; l = l-> next) {
				GnmStyleRegion *b = l->data;
				g_printerr ("B: %s\n", range_as_string (&b->range));
			}
			g_printerr ("C: %s\n", range_as_string (&c->range));
		}

		g_hash_table_remove (by_tl, &a->range.start);
		g_hash_table_remove (by_br, &a->range.end);
		g_ptr_array_remove_index_fast (data->accum, ui);
		ui--;

		g_hash_table_remove (by_tl, &c->range.start);
		g_hash_table_remove (by_br, &c->range.end);
		c->range.start.row = a->range.start.row;
		c->range.end.col = a->range.end.col;
		g_hash_table_insert (by_tl, &c->range.start, c);
		g_hash_table_insert (by_br, &c->range.end, c);
		if (debug)
			g_printerr ("New C: %s\n", range_as_string (&c->range));

		for (l = Bs; l; l = l-> next) {
			GnmStyleRegion *b = l->data;
			g_hash_table_remove (by_br, &b->range.end);
			b->range.end.row = c->range.end.row;
			g_hash_table_insert (by_br, &b->range.end, b);
			if (debug)
				g_printerr ("New B: %s\n", range_as_string (&b->range));
		}
		if (debug)
			g_printerr ("\n");

		gnm_style_region_free (a);
		g_slist_free (Bs);

		if (paranoid) verify_hashes (data);
	}
}

static void
merge_horizontal_stripes (ISL *data)
{
	unsigned ui;
	GHashTable *by_tl = data->by_tl;
	GHashTable *by_br = data->by_br;
	gboolean debug = debug_style_list ();
	gboolean paranoid = debug;

	for (ui = 0; ui < data->accum->len; ui++) {
		GnmStyleRegion *a = g_ptr_array_index (data->accum, ui);
		GnmStyleRegion *c;
		GnmCellPos cr;
		GSList *Bs = NULL, *l;
		gboolean fail = FALSE;

		/*  We're looking for the setup below and extend Bs right */
		/*  taking over part of C which is then extended to       */
		/*  include all of A.                                     */
		/*                                                        */
		/*                        +-----+-----+                   */
		/*                        |  A  |     |                   */
		/*                   +----+-----+     |                   */
		/*                   |    B1    |     |                   */
		/*                   +--+-------+     |                   */
		/*                      |       |  C  |                   */
		/*                      |       |     |                   */
		/*                      |  B2   |     |                   */
		/*                      |       |     |                   */
		/*                      |       |     |                   */
		/*                      +-------+-----+                   */

		cr.col = a->range.end.col + 1;
		cr.row = a->range.start.row;
		c = g_hash_table_lookup (by_tl, &cr);
		if (!c || !data->style_equal (a->style, c->style))
			continue;

		cr.col = a->range.end.col;
		cr.row = c->range.end.row;
		while (cr.row > a->range.end.row) {
			GnmStyleRegion *b = g_hash_table_lookup (by_br, &cr);
			if (!b || !data->style_equal (a->style, b->style)) {
				fail = TRUE;
				break;
			}
			Bs = g_slist_prepend (Bs, b);
			cr.row = b->range.start.row - 1;
		}
		if (fail || cr.row != a->range.end.row) {
			g_slist_free (Bs);
			continue;
		}

		if (debug) {
			g_printerr ("Horizontal stripe merge:\n");
			g_printerr ("A: %s\n", range_as_string (&a->range));
			for (l = Bs; l; l = l-> next) {
				GnmStyleRegion *b = l->data;
				g_printerr ("B: %s\n", range_as_string (&b->range));
			}
			g_printerr ("C: %s\n", range_as_string (&c->range));
		}

		g_hash_table_remove (by_tl, &a->range.start);
		g_hash_table_remove (by_br, &a->range.end);
		g_ptr_array_remove_index_fast (data->accum, ui);
		ui--;

		g_hash_table_remove (by_tl, &c->range.start);
		g_hash_table_remove (by_br, &c->range.end);
		c->range.start.col = a->range.start.col;
		c->range.end.row = a->range.end.row;
		g_hash_table_insert (by_tl, &c->range.start, c);
		g_hash_table_insert (by_br, &c->range.end, c);
		if (debug)
			g_printerr ("New C: %s\n", range_as_string (&c->range));

		for (l = Bs; l; l = l-> next) {
			GnmStyleRegion *b = l->data;
			g_hash_table_remove (by_br, &b->range.end);
			b->range.end.col = c->range.end.col;
			g_hash_table_insert (by_br, &b->range.end, b);
			if (debug)
				g_printerr ("New B: %s\n", range_as_string (&b->range));
		}
		if (debug)
			g_printerr ("\n");

		gnm_style_region_free (a);
		g_slist_free (Bs);

		if (paranoid) verify_hashes (data);
	}
}

static int
by_col_row (GnmStyleRegion **a, GnmStyleRegion **b)
{
	int d;

	d = (*a)->range.start.col - (*b)->range.start.col;
	if (d)
		return d;

	d = (*a)->range.start.row - (*b)->range.start.row;
	return d;
}

static GnmStyleList *
internal_style_list (Sheet const *sheet, GnmRange const *r,
		     gboolean (*style_equal) (GnmStyle const *a, GnmStyle const *b),
		     gboolean (*style_filter) (GnmStyle const *style))
{
	GnmRange full_sheet;
	ISL data;
	GnmStyleList *res = NULL;
	unsigned ui, prelen;
	gboolean paranoid = FALSE;
	guint64 sheet_area;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	if (r) {
		/* This can happen if the last row or column is deleted.  */
		if (!range_valid (r))
			return NULL;
	} else
		r = range_init_full_sheet (&full_sheet, sheet);

	data.accum = g_ptr_array_new ();
	data.by_tl = g_hash_table_new ((GHashFunc)gnm_cellpos_hash,
				       (GEqualFunc)gnm_cellpos_equal);
	data.by_br = g_hash_table_new ((GHashFunc)gnm_cellpos_hash,
				       (GEqualFunc)gnm_cellpos_equal);
	data.area = 0;
	data.style_equal = style_equal;
	data.style_filter = style_filter;
	data.sheet_size = gnm_sheet_get_size (sheet);

	foreach_tile (sheet->style_data->styles,
		      sheet->tile_top_level, 0, 0, r,
		      cb_style_list_add_node, &data);

	sheet_area = (guint64)range_height (r) * range_width (r);
	if (data.style_filter ? (data.area > sheet_area) : (data.area != sheet_area))
		g_warning ("Strange size issue in internal_style_list");

	/*
	 * Simple, fast optimization first.  For the file underlying
	 * bug 699045 this brings down 332688 entries to just 86.
	 */
	if (data.accum->len >= 2) {
		g_ptr_array_sort (data.accum, (GCompareFunc)by_col_row);
		for (ui = data.accum->len - 1; ui > 0; ui--) {
			try_merge_pair (&data, ui - 1, ui);
		}
	}

	/* Populate hashes.  */
	for (ui = 0; ui < data.accum->len; ui++) {
		GnmStyleRegion *sr = g_ptr_array_index (data.accum, ui);
		g_hash_table_insert (data.by_tl, &sr->range.start, sr);
		g_hash_table_insert (data.by_br, &sr->range.end, sr);
	}

	if (paranoid) verify_hashes (&data);

	do {
		prelen = data.accum->len;
		merge_vertical_stripes (&data);
		merge_horizontal_stripes (&data);
	} while (prelen > data.accum->len);

	/* Always verify once.  */
	verify_hashes (&data);

	if (debug_style_list ())
		g_printerr ("Total of %d ranges:\n", data.accum->len);
	for (ui = data.accum->len; ui-- > 0; ) {
		GnmStyleRegion *sr = g_ptr_array_index (data.accum, ui);
		if (debug_style_list ())
			g_printerr ("  %s %p\n",
				    range_as_string (&sr->range),
				    sr->style);
		res = g_slist_prepend (res, sr);
	}

	g_ptr_array_free (data.accum, TRUE);
	g_hash_table_destroy (data.by_tl);
	g_hash_table_destroy (data.by_br);
	return res;
}

/**
 * sheet_style_get_range:
 * @sheet: the sheet in which to find styles
 * @r:     optional range to scan
 *
 * Get a list of rectangles and their associated styles.
 * Caller is responsible for freeing.  Note that when a range is given,
 * the resulting ranges are relative to the input range.
 *
 * Returns: (transfer full):
 */
GnmStyleList *
sheet_style_get_range (Sheet const *sheet, GnmRange const *r)
{
	return internal_style_list (sheet, r,
				    gnm_style_eq,
				    NULL);
}

static gboolean
style_conditions_equal (GnmStyle const *a, GnmStyle const *b)
{
	return	gnm_style_get_conditions (a) == gnm_style_get_conditions (b);
}

static gboolean
style_conditions_filter (GnmStyle const *style)
{
	return gnm_style_get_conditions (style) != NULL;
}

/**
 * sheet_style_collect_conditions:
 * @sheet:
 * @r:
 *
 * Returns: (transfer full): a list of areas with conditionals, Caller is
 * responsible for freeing.
 **/
GnmStyleList *
sheet_style_collect_conditions (Sheet const *sheet, GnmRange const *r)
{
	return internal_style_list (sheet, r,
				    style_conditions_equal,
				    style_conditions_filter);
}


static gboolean
style_hlink_equal (GnmStyle const *a, GnmStyle const *b)
{
	return	gnm_style_get_hlink (a) == gnm_style_get_hlink (b);
}

static gboolean
style_hlink_filter (GnmStyle const *style)
{
	return gnm_style_get_hlink (style) != NULL;
}

/**
 * sheet_style_collect_hlinks:
 * @sheet:
 * @r:
 *
 * Returns: (transfer full): a list of areas with hyperlinks, Caller is
 * responsible for freeing.
 **/
GnmStyleList *
sheet_style_collect_hlinks (Sheet const *sheet, GnmRange const *r)
{
	return internal_style_list (sheet, r,
				    style_hlink_equal,
				    style_hlink_filter);
}


static gboolean
style_validation_equal (GnmStyle const *a, GnmStyle const *b)
{
	return	gnm_style_get_validation (a) == gnm_style_get_validation (b) &&
		gnm_style_get_input_msg (a) == gnm_style_get_input_msg (b);
}

static gboolean
style_validation_filter (GnmStyle const *style)
{
	return (gnm_style_get_validation (style) != NULL ||
		gnm_style_get_input_msg (style) != NULL);
}

/**
 * sheet_style_collect_validations:
 * @sheet: the to trawl
 * @r: (allow-none): range to restrict to
 *
 * Returns: (transfer full): a list of areas with validation or input
 * message.
 **/
GnmStyleList *
sheet_style_collect_validations (Sheet const *sheet, GnmRange const *r)
{
	return internal_style_list (sheet, r,
				    style_validation_equal,
				    style_validation_filter);
}

/**
 * sheet_style_set_list:
 * @sheet: #Sheet
 * @corner: The top-left corner (in LTR mode)
 * @l: #GnmStyleList
 * @range_modify: (scope call):
 * @data: user data
 *
 * Overwrites the styles of the ranges given by @corner with the content of
 * @list. Optionally transposing the ranges
 **/
GnmSpanCalcFlags
sheet_style_set_list (Sheet *sheet, GnmCellPos const *corner,
		      GnmStyleList const *list,
		      sheet_style_set_list_cb_t range_modify,
		      gpointer data)
{
	GnmSpanCalcFlags spanflags = GNM_SPANCALC_SIMPLE;
	GnmStyleList const *l;

	g_return_val_if_fail (IS_SHEET (sheet), spanflags);

	/* Sluggish but simple implementation for now */
	for (l = list; l; l = l->next) {
		GnmStyleRegion const *sr = l->data;
		GnmRange              r  = sr->range;

		range_translate (&r, sheet, +corner->col, +corner->row);
		if (range_modify)
			range_modify (&r, sheet, data);

		gnm_style_ref (sr->style);
		sheet_style_set_range (sheet, &r, sr->style);
		spanflags |= gnm_style_required_spanflags (sr->style);
	}
	return spanflags;
}

/**
 * style_list_free:
 * @l: the list to free
 *
 * Free up the ressources in the style list.  Including unreferencing the
 * styles.
 */
void
style_list_free (GnmStyleList *list)
{
	g_slist_free_full (list, (GDestroyNotify)gnm_style_region_free);
}

/**
 * style_list_get_style:
 * @l: A style list.
 * @col:
 * @row:
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
cb_find_link (GnmStyle *style,
	      G_GNUC_UNUSED int corner_col, G_GNUC_UNUSED int corner_row,
	      G_GNUC_UNUSED int width, G_GNUC_UNUSED int height,
	      G_GNUC_UNUSED GnmRange const *apply_to, gpointer user)
{
	GnmHLink **link = user;
	if (*link == NULL)
		*link = gnm_style_get_hlink (style);
}

/**
 * sheet_style_region_contains_link:
 * @sheet:
 * @r:
 *
 * Utility routine that checks to see if a region contains at least 1 hyper link
 * and returns the 1st one it finds.
 *
 * Returns: (transfer none): the found #GmHLink if any.
 **/
GnmHLink *
sheet_style_region_contains_link (Sheet const *sheet, GnmRange const *r)
{
	GnmHLink *res = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (r != NULL, NULL);

	foreach_tile (sheet->style_data->styles,
		      sheet->tile_top_level, 0, 0, r,
		      cb_find_link, &res);
	return res;
}

/**
 * sheet_style_foreach:
 * @sheet: #Sheet
 * @func: (scope call): callback
 * @user_data: user data.
 *
 * Executes @func for each style in the sheet.
 **/
void
sheet_style_foreach (Sheet const *sheet, GFunc func, gpointer user_data)
{
	GSList *styles;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (sheet->style_data != NULL);

	styles = sh_all_styles (sheet->style_data->style_hash);
	styles = g_slist_sort (styles, (GCompareFunc)gnm_style_cmp);
	g_slist_foreach (styles, func, user_data);
	g_slist_free (styles);
}

/**
 * sheet_style_range_foreach:
 * @sheet: #Sheet
 * @r: optional range
 * @func: (scope call): callback.
 * @user_data: user data
 *
 **/
void
sheet_style_range_foreach (Sheet const *sheet, GnmRange const *r,
			   GHFunc func, gpointer user_data)
{
	GnmStyleList *styles, *l;

	styles = sheet_style_get_range (sheet, r);

	for (l = styles; l; l = l->next) {
		GnmStyleRegion *sr = l->data;
		if (r) {
			sr->range.start.col += r->start.col;
			sr->range.start.row += r->start.row;
			sr->range.end.col += r->start.col;
			sr->range.end.row += r->start.row;
		}
		func (NULL, sr, user_data);
		gnm_style_region_free (sr);
	}

	g_slist_free (styles);
}

/* ------------------------------------------------------------------------- */

static void
cell_tile_dump (CellTile **tile, int level, CellTileOptimize *data,
		int ccol, int crow)
{
	CellTileType type;
	int const w = tile_widths[level];
	int const h = tile_heights[level];
	GnmRange rng;
	const char *indent = "";

	type = (*tile)->type;

	range_init (&rng,
		    ccol, crow,
		    MIN (ccol + tile_widths[level + 1] - 1,
			 data->ss->max_cols - 1),
		    MIN (crow + tile_heights[level + 1] - 1,
			 data->ss->max_rows - 1));

	g_printerr ("%s%s: type %s\n",
		    indent,
		    range_as_string (&rng),
		    tile_type_str[type]);

	if (type == TILE_PTR_MATRIX) {
		int i;

		for (i = 0; i < TILE_SIZE_COL * TILE_SIZE_ROW; i++) {
			CellTile **subtile = (*tile)->ptr_matrix.ptr + i;
			int c = i % TILE_SIZE_COL;
			int r = i / TILE_SIZE_COL;
			cell_tile_dump (subtile, level - 1, data,
					ccol + w * c,
					crow + h * r);
		}
	} else {
#if 0
		int i;

		for (i = 0; i < tile_size[type]; i++) {
			g_printerr ("%s: %d %p\n",
				    indent,
				    i,
				    (*tile)->style_any.style[i]);
		}
#endif
	}
}

/* ------------------------------------------------------------------------- */

static void
cell_tile_optimize (CellTile **tile, int level, CellTileOptimize *data,
		    int ccol, int crow)
{
	CellTileType type;
	int const w = tile_widths[level];
	int const h = tile_heights[level];
	CellTile *res;
	int i;
	GnmRange rng;

	type = (*tile)->type;
	if (type == TILE_SIMPLE)
		return;

	range_init (&rng,
		    ccol, crow,
		    MIN (ccol + tile_widths[level + 1] - 1,
			 data->ss->max_cols - 1),
		    MIN (crow + tile_heights[level + 1] - 1,
			 data->ss->max_rows - 1));

	switch (type) {
	case TILE_COL:
	case TILE_ROW:
		if (!tile_is_uniform (*tile))
			return;

		type = TILE_SIMPLE;
		break;

	case TILE_PTR_MATRIX: {
		gboolean all_simple = TRUE;
		int i;

		for (i = 0; i < TILE_SIZE_COL * TILE_SIZE_ROW; i++) {
			CellTile **subtile = (*tile)->ptr_matrix.ptr + i;
			if (data->recursion) {
				int c = i % TILE_SIZE_COL;
				int r = i / TILE_SIZE_COL;
				cell_tile_optimize (subtile, level - 1, data,
						    ccol + w * c,
						    crow + h * r);
			}
			if ((*subtile)->type != TILE_SIMPLE)
				all_simple = FALSE;
		}
		if (!all_simple)
			return;

		res = cell_tile_style_new (NULL, TILE_MATRIX);
		for (i = 0; i < TILE_SIZE_COL * TILE_SIZE_ROW; i++) {
			CellTile *subtile = (*tile)->ptr_matrix.ptr[i];
			GnmStyle *st = subtile->style_simple.style[0];
			gnm_style_link (st);
			res->style_matrix.style[i] = st;
		}

		if (debug_style_optimize)
			g_printerr ("Turning %s (%dx%d) from a %s into a %s\n",
				    range_as_string (&rng),
				    range_width (&rng), range_height (&rng),
				    tile_type_str[(*tile)->type],
				    tile_type_str[res->type]);
		cell_tile_dtor (*tile);
		*tile = res;

		/* Fall through */
	}

	case TILE_MATRIX: {
		gboolean csame = TRUE;
		gboolean rsame = TRUE;
		int c, r, i;

		for (i = r = 0 ; r < TILE_SIZE_ROW ; ++r, i += TILE_SIZE_COL) {
			for (c = 0 ; c < TILE_SIZE_COL ; ++c) {
				if (rsame && c &&
				    !gnm_style_eq ((*tile)->style_matrix.style[i + c],
						   (*tile)->style_matrix.style[i    ])) {
					rsame = FALSE;
					if (!csame)
						return;
				}
				if (csame && r &&
				    !gnm_style_eq ((*tile)->style_matrix.style[i + c],
						   (*tile)->style_matrix.style[    c])) {
					csame = FALSE;
					if (!rsame)
						return;
				}
			}
		}

		if (csame && rsame)
			type = TILE_SIMPLE;
		else if (csame) {
			type = TILE_COL;
		} else {
			type = TILE_ROW;
		}
		break;
	}

	default:
		g_assert_not_reached ();
	}

	if (debug_style_optimize)
		g_printerr ("Turning %s (%dx%d) from a %s into a %s\n",
			    range_as_string (&rng),
			    range_width (&rng), range_height (&rng),
			    tile_type_str[(*tile)->type],
			    tile_type_str[type]);

	res = cell_tile_style_new (NULL, type);
	switch (type) {
	case TILE_SIMPLE:
		res->style_simple.style[0] = (*tile)->style_any.style[0];
		break;
	case TILE_ROW:
		for (i = 0; i < TILE_SIZE_ROW; i++)
			res->style_row.style[i] =
				(*tile)->style_matrix.style[i * TILE_SIZE_COL];
		break;
	case TILE_COL:
		for (i = 0; i < TILE_SIZE_COL; i++)
			res->style_col.style[i] =
				(*tile)->style_matrix.style[i];
		break;
	default:
		g_assert_not_reached ();
	}

	for (i = 0; i < tile_size[type]; i++)
		gnm_style_link (res->style_any.style[i]);

	cell_tile_dtor (*tile);
	*tile = res;
}

static GSList *
sample_styles (Sheet *sheet)
{
	GnmSheetSize const *ss = gnm_sheet_get_size (sheet);
	GSList *res = NULL;
	int c = 0, r = 0;
	const int SKIP = 1;

	while (1) {
		GnmStyle const *mstyle = sheet_style_get (sheet, c, r);
		if (res == NULL ||  !gnm_style_eq (mstyle, res->data)) {
			gnm_style_ref (mstyle);
			res = g_slist_prepend (res, GINT_TO_POINTER (c));
			res = g_slist_prepend (res, GINT_TO_POINTER (r));
			res = g_slist_prepend (res, (gpointer)mstyle);
		}

		c += SKIP;
		if (c >= ss->max_cols) {
			c -= ss->max_cols;
			r++;
			if (r >= ss->max_rows)
				break;
		}
	}

	return g_slist_reverse (res);
}

static void
verify_styles (GSList *pre, GSList *post)
{
	GSList *lpre, *lpost;
	gboolean silent = FALSE, bad = FALSE;

	for (lpre = pre, lpost = post;
	     lpre || lpost;
	     lpre = (lpre ? lpre->next->next->next : NULL),
	     lpost = (lpost ? lpost->next->next->next : NULL)) {
		int cpre = lpre ? GPOINTER_TO_INT (lpre->data) : -1;
		int rpre = lpre ? GPOINTER_TO_INT (lpre->next->data) : -1;
		GnmStyle const *spre = lpre ? lpre->next->next->data : NULL;
		int cpost = lpost ? GPOINTER_TO_INT (lpost->data) : -1;
		int rpost = lpost ? GPOINTER_TO_INT (lpost->next->data) : -1;
		GnmStyle const *spost = lpost ? lpost->next->next->data : NULL;

		if (!silent) {
			if (!spre || !spost) {
				bad = TRUE;
				g_warning ("Style optimizer failure at end!");
				silent = TRUE;
			} else if (cpre != cpost || rpre != rpost) {
				bad = TRUE;
				g_warning ("Style optimizer position conflict at %s!",
					   cell_coord_name (cpre, rpre));
				silent = TRUE;
			} else if (!gnm_style_eq (spre, spost)) {
				bad = TRUE;
				g_warning ("Style optimizer failure at %s!",
					   cell_coord_name (cpre, rpre));
			}
		}

		if (spre) gnm_style_unref (spre);
		if (spost) gnm_style_unref (spost);
	}

	g_slist_free (pre);
	g_slist_free (post);

	g_assert (!bad);
}

void
sheet_style_optimize (Sheet *sheet)
{
	CellTileOptimize data;
	GSList *pre;
	gboolean verify;

	g_return_if_fail (IS_SHEET (sheet));

	if (gnm_debug_flag ("no-style-optimize"))
		return;

	sheet_colrow_optimize (sheet);

	data.ss = gnm_sheet_get_size (sheet);
	data.recursion = TRUE;

	if (debug_style_optimize) {
		g_printerr ("Optimizing %s\n", sheet->name_unquoted);
		cell_tile_dump (&sheet->style_data->styles,
				sheet->tile_top_level, &data,
				0, 0);
	}

	verify = gnm_debug_flag ("style-optimize-verify");
	pre = verify ? sample_styles (sheet) : NULL;

	cell_tile_optimize (&sheet->style_data->styles,
			    sheet->tile_top_level, &data,
			    0, 0);

	if (debug_style_optimize)
		g_printerr ("Optimizing %s...done\n", sheet->name_unquoted);

	if (verify) {
		GSList *post = sample_styles (sheet);
		verify_styles (pre, post);
	}
}
