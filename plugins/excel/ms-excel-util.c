/* vim: set sw=8: */
/**
 * ms-excel-util.c: Utility functions for MS Excel import / export
 *
 * Author:
 *    Jon K Hellan (hellan@acm.org)
 *
 * (C) 1999, 2000 Jon K Hellan
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <glib.h>

#include "boot.h"
#include "style.h"
#include "ms-excel-util.h"

#include <stdio.h>
#include <string.h>

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif

/*
 * TwoWayTable
 *
 * This is a data structure for a one to one mapping between a key and an
 * index. You can access the key by index and the index by key. Indices are
 * assigned to keys in the order they are entered.
 *
 * Example of use:
 * Build a table of unique fonts in the workbook, assigning each an index.
 * Then write them out ordered by index.
 *
 * Methods:
 * two_way_table_new:        Make a TwoWayTable.
 * two_way_table_free:       Destroy the TwoWayTable
 * two_way_table_put:        Put a key to the TwoWayTable
 * two_way_table_move:	     Move a key from one index to another
 * two_way_table_key_to_idx: Find index given a key
 * two_way_table_idx_to_key: Find key given the index
 *
 * Implementation:
 * A g_hash_table and a g_ptr_array. The value stored in the hash
 * table is index + 1. This is because hash lookup returns NULL on
 * failure. If 0 could be stored in the hash, we could not distinguish
 * this value from failure.
 */

/**
 * two_way_table_new
 * @hash_func        Hash function
 * @key_compare_func Comparison function
 * @base             Index valuse start from here.
 *
 * Makes a TwoWayTable. Returns the table.
 */
TwoWayTable *
two_way_table_new (GHashFunc      hash_func,
		   GCompareFunc   key_compare_func,
		   gint           base,
		   GDestroyNotify key_destroy_func)
{
	TwoWayTable *table = g_new (TwoWayTable, 1);

	g_return_val_if_fail (base >= 0, NULL);
	table->all_keys    = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						    key_destroy_func, NULL);
	table->unique_keys = g_hash_table_new (hash_func, key_compare_func);
	table->idx_to_key  = g_ptr_array_new ();
	table->base        = base;

	return table;
}

/**
 * two_way_table_free
 * @table Table
 *
 * Destroys the TwoWayTable.
 */
void
two_way_table_free (TwoWayTable *table)
{
	g_hash_table_destroy (table->all_keys);
	g_hash_table_destroy (table->unique_keys);
	g_ptr_array_free (table->idx_to_key, TRUE);
	g_free (table);
}

/**
 * two_way_table_put
 * @table  Table
 * @key    Key to enter
 * @potentially_unique True if key is entered also if already in table
 * @apf    Function to call after putting.
 *
 * Puts a key to the TwoWayTable if it is not already there. Returns
 * the index of the key. apf is of type AfterPutFunc, and can be used
 * for logging, freeing resources, etc. It is told if the key was
 * entered or not.
 */
gint
two_way_table_put (const TwoWayTable *table, gpointer key,
		   gboolean potentially_unique,
		   AfterPutFunc apf, gconstpointer closure)
{
	gint index = two_way_table_key_to_idx (table, key);
	gboolean found = (index >= 0);
	gboolean addit = !found || !potentially_unique;
	gpointer unique;

	if (addit) {
		index = table->idx_to_key->len + table->base;

		if (!found) {
			/* We have not seen this pointer before, but is the key
			 * already in the set ?
			 */
			unique = g_hash_table_lookup (table->all_keys, key);
			if (unique == NULL)
				g_hash_table_insert (table->all_keys, key,
						     GINT_TO_POINTER (index + 1));
			g_hash_table_insert (table->unique_keys, key,
					     GINT_TO_POINTER (index + 1));
		}
		g_ptr_array_add (table->idx_to_key, key);
	}

	if (apf)
		apf (key, addit, index, closure);

	return index;
}

/**
 * two_way_table_move
 * @table Table
 * @dst_idx : The new idx for the value
 * @src_idx : stored here
 *
 * Moves the key at index @src_idx into index @dst_idx, and drops the original
 * content of @dst_idx
 **/
void
two_way_table_move (TwoWayTable const *table, gint dst_idx, gint src_idx)
{
	gpointer key_to_forget, key_to_move;

	key_to_forget = two_way_table_idx_to_key (table, dst_idx);
	key_to_move   = two_way_table_idx_to_key (table, src_idx);

	g_hash_table_remove (table->all_keys, key_to_move);
	g_hash_table_remove (table->all_keys, key_to_forget);
	g_hash_table_remove (table->unique_keys, key_to_move);
	g_hash_table_remove (table->unique_keys, key_to_forget);

	dst_idx += table->base;
	src_idx += table->base;
	g_hash_table_insert (table->all_keys, key_to_move,
		GINT_TO_POINTER (dst_idx + table->base + 1));
	g_hash_table_insert (table->unique_keys, key_to_move,
		GINT_TO_POINTER (dst_idx + table->base + 1));
	g_ptr_array_index   (table->idx_to_key, dst_idx) = key_to_move;
	g_ptr_array_index   (table->idx_to_key, src_idx) = (gpointer)0xdeadbeef; /* poison */
}

/**
 * two_way_table_key_to_idx
 * @table Table
 * @key   Key
 *
 * Returns index of key, or -1 if key not found.
 */
gint
two_way_table_key_to_idx (const TwoWayTable *table, gconstpointer key)
{
	return GPOINTER_TO_INT (g_hash_table_lookup (table->unique_keys, key)) - 1;
}

/**
 * two_way_table_idx_to_key
 * @table Table
 * @idx   Index
 *
 * Returns key bound to index, or NULL if index is out of range.
 */
gpointer
two_way_table_idx_to_key (const TwoWayTable *table, gint idx)
{
	g_return_val_if_fail (idx - table->base >= 0, NULL);
	g_return_val_if_fail (idx - table->base < (int)table->idx_to_key->len,
			      NULL);

	return g_ptr_array_index (table->idx_to_key, idx - table->base);
}

/***************************************************************************/

static GHashTable *xl_font_width_hash = NULL;
static GHashTable *xl_font_width_warned = NULL;

struct XL_font_width {
    int const char_width_pts;
    int const defaultchar_width_pts;
    char const * const name;
};

static void
init_xl_font_widths (void)
{
	/* These are the widths in pixels for a 128pt fonts assuming 96dpi
	 * They were inductively calculated.
	 *
	 * The 'default' width is based on the hypothesis that the default
	 *     column width is always 8*default.  Edit the 'Normal' Style
	 *     and change the font to 128pt then look at the pixel width
	 *     displayed when resizing the column.
	 *
	 * The standard width is also based on setting the Normal styles font.
	 *    However, it is calculated by changing the column width until it is
	 *    and integer CHAR width. Note the PIXEL size then resize to a CHAR
	 *    width 1 unit larger.  Take the pixel difference.
	 */
	static struct XL_font_width const widths[] = {
	    { 95, 102, "Arial" },
	    { 114, 122, "Arial Black" },
	    { 78, 84, "Arial Narrow" },
	    { 95, 102, "AvantGarde" },
	    { 86, 92, "Book Antiqua" },
	    { 106, 113, "Bookman Old Style" },
	    { 95, 102, "Century Gothic" },
	    { 95, 102, "Century Schoolbook" },
	    { 86, 92, "CG Times" },
	    { 104, 111, "Comic Sans MS" },
	    { 60, 64, "Courier" },
	    { 103, 110, "Courier New" },
	    { 103, 110, "Fixedsys" },
	    { 80, 86, "Garamond" },
	    /* { 115, 122, "Geneva" }, These are the real numbers */
	    { 95, 102, "Geneva" }, /* These are the defaults when Geneva is not available */
	    { 95, 102, "Haettenscheiler" },
	    { 103, 110, "HE_TERMINAL" },
	    { 95, 102, "Helvetica" },
	    { 95, 102, "Helv" },
	    { 95, 102, "Helvetica-Black" },
	    { 95, 102, "Helvetica-Light" },
	    { 95, 102, "Helvetica-Narrow" },
	    { 93, 100, "Impact" },
	    { 86, 92, "ITC Bookman" },
	    { 103, 110, "Letter Gothic MT" },
	    { 103, 110, "Lucida Console" },
	    { 108, 115, "Lucida Sans Unicode" },
	    { 171, 182, "Map Symbols" },
	    { 171, 182, "Marlett" },
	    { 81, 87, "Modern" },
	    { 75, 80, "Monotype Corsiva" },
	    { 156, 166, "Monotype Sorts" },
	    { 86, 92, "MS Outlook" },
	    { 90, 96, "MS Sans Serif" },
	    { 80, 86, "MS Serif" },
	    { 174, 186, "MT Extra" },
	    { 86, 92, "NewCenturySchlbk" },
	    { 95, 102, "Optimum" },
	    { 86, 92, "Palatino" },
	    { 81, 87, "Roman" },
	    { 69, 74, "Script" },
	    { 142, 152, "Serpentine" },
	    { 95, 102, "Small Fonts" },
	    { 86, 92, "Symbol" },
	    { 40, 43, "System" },
	    { 103, 110, "System APL Special" },
	    { 103, 110, "System VT Special" },
	    { 93, 100, "Tahoma" },
	    { 50, 54, "Terminal" },
	    { 86, 92, "Times" },

	    /* TODO : actual measurement was 86, but that was too big when
	     * columns with 10pt fonts.  Figure out why ? (test case aksjefon.xls)
	     */
	    { 83, 92, "Times New Roman" },
	    { 91, 97, "Times New Roman MT Extra Bold" },
	    { 90, 96, "Trebuchet MS" },
	    { 109, 117, "Verdana" },
	    { 171, 182, "Webdings" },
	    { 230, 245, "Wingdings" },
	    { 194, 207, "Wingdings 2" },
	    { 95, 102, "Wingdings 3" },
	    { 86, 92, "ZapfChancery" },
	    { 230, 245, "ZapfDingbats" },
	    { -1, -1, NULL }
	};
	int i;

	if (xl_font_width_hash == NULL) {
		xl_font_width_hash =
		    g_hash_table_new (&g_str_hash, &g_str_equal);
		xl_font_width_warned =
		    g_hash_table_new (&g_str_hash, &g_str_equal);
	}

	g_assert (xl_font_width_hash != NULL);
	g_assert (xl_font_width_warned != NULL);

	for (i = 0; widths[i].name != NULL ; ++i)
		g_hash_table_insert (xl_font_width_hash,
				     (gpointer)widths[i].name,
				     (gpointer)(widths+i));
}

static void
cb_destroy_xl_font_widths (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
}

void
destroy_xl_font_widths (void)
{
	if (xl_font_width_hash) {
		g_hash_table_destroy (xl_font_width_hash);
		xl_font_width_hash = NULL;

		g_hash_table_foreach (xl_font_width_warned,
				      cb_destroy_xl_font_widths,
				      NULL);
		g_hash_table_destroy (xl_font_width_warned);
		xl_font_width_warned = NULL;
	}
}


double
lookup_font_base_char_width (char const *name, double size_pts,
			     gboolean const is_default)
{
	static gboolean need_init = TRUE;
	gpointer res;
	if (need_init) {
		need_init = FALSE;
		init_xl_font_widths ();
	}

	g_return_val_if_fail (xl_font_width_hash != NULL, 10.);
	g_return_val_if_fail (name != NULL, 10.);

	res = g_hash_table_lookup (xl_font_width_hash, name);

	size_pts /= 20.;
	if (res != NULL) {
		struct XL_font_width const * info = res;
		double width = (is_default)
			? info->defaultchar_width_pts
			: info->char_width_pts;

		/* Crude Linear interpolation of the width */
		width = size_pts * width / 128.;

		/* Round to pixels */
		width = (int)(width +.5);

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 0)
			printf ("%s %g = %g\n", name, size_pts, width);
#endif

		/* Convert to pts using the hard coded 96dpi that the
		 * measurements assume.
		 * NOTE : We must round BEFORE converting in order to match
		 */
		width *= 72./96.;
		return width;
	}

	if (!g_hash_table_lookup (xl_font_width_warned, name)) {
		char *namecopy = g_strdup (name);
		g_warning ("EXCEL : unknown widths for font '%s', guessing", name);
		g_hash_table_insert (xl_font_width_warned, namecopy, namecopy);
	}

	/* Use a rough heuristic for unknown fonts. */
	return .5625 * size_pts;
}
