/* vim: set sw=8: */
/**
 * ms-excel-util.c: Utility functions for MS Excel import / export
 *
 * Author:
 *    Jon K Hellan (hellan@acm.org)
 *
 * (C) 1999-2005 Jon K Hellan
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <glib.h>

#include "boot.h"
#include "style.h"
#include "ms-excel-util.h"
#include <goffice/utils/go-glib-extras.h>

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
	table->key_destroy_func = key_destroy_func;

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
two_way_table_put (TwoWayTable const *table, gpointer key,
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
		} else if (table->key_destroy_func)
			(table->key_destroy_func) (key);
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
two_way_table_key_to_idx (TwoWayTable const *table, gconstpointer key)
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
two_way_table_idx_to_key (TwoWayTable const *table, gint idx)
{
	g_return_val_if_fail (idx - table->base >= 0, NULL);
	g_return_val_if_fail (idx - table->base < (int)table->idx_to_key->len,
			      NULL);

	return g_ptr_array_index (table->idx_to_key, idx - table->base);
}

/***************************************************************************/

static GHashTable *xl_font_width_hash = NULL;
static GHashTable *xl_font_width_warned = NULL;

static XL_font_width const unknown_spec =
	{ "Unknown",	 8,	0x0924,	36.5 }; /* dup of Arial */

static void
init_xl_font_widths (void)
{
	static XL_font_width const widths[] = {
		{ "AR PL KaitiM Big5",		 8,	0x0924,	36.5 },
		{ "AR PL KaitiM GB",		 8,	0x0924,	36.5 },
		{ "AR PL Mingti2L Big5",	 8,	0x0924,	36.5 },
		{ "AR PL SungtiL GB",		 8,	0x0924,	36.5 },
		{ "Albany AMT",			 8,	0x0924,	36.5 },
		{ "Albany",			 8,	0x0924,	36.5 },
		{ "Andale Mono",		 9,	0x0900,	32.0 },
		{ "Andale Sans",		 8,	0x0924,	36.5 },
		{ "Andy MT",			 7,	0x0955,	42.5 },
		{ "Arial Baltic",		 8,	0x0924,	36.5 },
		{ "Arial Black",		10,	0x08E3,	28.5 },
		{ "Arial CE",			 8,	0x0924,	36.5 },
		{ "Arial Cyr",			 8,	0x0924,	36.5 },
		{ "Arial Greek",		 8,	0x0924,	36.5 },
		{ "Arial Narrow",		 7,	0x0955,	42.5 },
		{ "Arial TUR",			 8,	0x0924,	36.5 },
		{ "Arial",			 8,	0x0924,	36.5 },
		{ "Baekmuk Batang",		 8,	0x0924,	36.5 },
		{ "Baekmuk Dotum",		 8,	0x0924,	36.5 },
		{ "Baekmuk Galim",		 9,	0x0900,	32.0 },
		{ "Baekmuk Headline",		 9,	0x0900,	32.0 },
		{ "Bell MT",			 8,	0x0924,	36.5 },
		{ "Bitstream Vera Sans Mono",	 9,	0x0900,	32.0 },
		{ "Bitstream Vera Sans",	 8,	0x0924,	36.5 },
		{ "Sans",			 8,	0x0924,	36.5 },
		{ "Bitstream Vera Serif",	 9,	0x0900,	32.0 },
		{ "Book Antiqua",		 8,	0x0924,	36.5 },
		{ "Bookman Old Style",		 9,	0x0900,	32.0 },
		{ "Calibri",			 9,	0x0900,	32.0 },
		{ "Century Gothic",		 8,	0x0924,	36.5 },
		{ "Comic Sans MS",		 9,	0x0900,	32.0 },
		{ "Courier New",		 9,	0x0900,	32.0 },
		{ "Courier",			 9,	0x0900,	32.0 },
		{ "Cumberland AMT",		 9,	0x0900,	32.0 },
		{ "Dutch801 SWC",		 7,	0x0955,	42.5 },
		{ "East Syriac Adiabene",	 8,	0x0924,	36.5 },
		{ "East Syriac Ctesiphon",	 8,	0x0924,	36.5 },
		{ "Estrangelo Antioch",		 8,	0x0924,	36.5 },
		{ "Estrangelo Edessa",		 8,	0x0924,	36.5 },
		{ "Estrangelo Midyat",		 8,	0x0924,	36.5 },
		{ "Estrangelo Nisibin Outline",	 8,	0x0924,	36.5 },
		{ "Estrangelo Nisibin",		 8,	0x0924,	36.5 },
		{ "Estrangelo Quenneshrin",	 8,	0x0924,	36.5 },
		{ "Estrangelo Talada",		 8,	0x0924,	36.5 },
		{ "Estrangelo TurAbdin",	 8,	0x0924,	36.5 },
		{ "Fixedsys",			 9,	0x0900,	32.0 },
		{ "Franklin Gothic Medium",	 9,	0x0900,	32.0 },
		{ "FreeMono",			 9,	0x0900,	32.0 },
		{ "FreeSans",			 8,	0x0924,	36.5 },
		{ "FreeSerif",			 8,	0x0924,	36.5 },
		{ "Garamond",			 7,	0x0955,	42.5 },
		{ "Gautami",			 8,	0x0924,	36.5 },
		{ "Georgia",			10,	0x08E3,	28.5 },
		{ "Goha-Tibeb Zemen",		 7,	0x0955,	42.5 },
		{ "Haettenschweiler",		 7,	0x0955,	42.5 },
		{ "Helv",			 8,	0x0924,	36.5 },
		{ "Helvetica",			 8,	0x0924,	36.5 },
		{ "Helvetica-Black",		10,	0x08E3,	28.5 },
		{ "Helvetica-Light",		 8,	0x0924,	36.5 },
		{ "Helvetica-Narrow",		 7,	0x0955,	42.5 },
		{ "Impact",			 8,	0x0924,	36.5 },
		{ "Incised901 SWC",		 8,	0x0924,	36.5 },
		{ "Kartika",			 6,	0x0999,	51.25 },
		{ "Latha",			10,	0x08E3,	28.5 },
		{ "LetterGothic SWC",		 9,	0x0900,	32.0 },
		{ "Lucida Console",		 9,	0x0900,	32.0 },
		{ "Lucida Sans Unicode",	 9,	0x0900,	32.0 },
		{ "Lucida Sans",		 9,	0x0900,	32.0 },
		{ "Luxi Mono",			 9,	0x0900,	32.0 },
		{ "Luxi Sans",			 8,	0x0924,	36.5 },
		{ "Luxi Serif",			 8,	0x0924,	36.5 },
		{ "MS Outlook",			 8,	0x0924,	36.5 },
		{ "MS Sans Serif",		 8,	0x0924,	36.5 },
		{ "MS Serif",			 7,	0x0955,	42.5 },
		{ "MT Extra",			15,	0x093B, 19.75 },
		{ "MV Boli",			10,	0x08E3,	28.5 },
		{ "Mangal",			 9,	0x0900,	32.0 },
		{ "Marlett",			15,	0x093B, 19.75 },
		{ "Microsoft Sans Serif",	 8,	0x0924,	36.5 },
		{ "Modern",			 7,	0x0955,	42.5 },
		{ "Monotype Corsiva",		 7,	0x0955,	42.5 },
		{ "Monotype Sorts",		15,	0x093B, 19.75 },
		{ "OmegaSerif88591",		 8,	0x0924,	36.5 },
		{ "OmegaSerif88592",		 8,	0x0924,	36.5 },
		{ "OmegaSerif88593",		 8,	0x0924,	36.5 },
		{ "OmegaSerif88594",		 8,	0x0924,	36.5 },
		{ "OmegaSerif88595",		 8,	0x0924,	36.5 },
		{ "OmegaSerifVISCII",		 8,	0x0924,	36.5 },
		{ "OpenSymbol",			 8,	0x0924,	36.5 },
		{ "OrigGaramond SWC",		 7,	0x0955,	42.5 },
		{ "Palatino Linotype",		 8,	0x0924,	36.5 },
		{ "Palatino",			 8,	0x0924,	36.5 },
		{ "Raavi",			10,	0x08E3,	28.5 },
		{ "Roman",			 7,	0x0955,	42.5 },
		{ "SUSE Sans Mono",		 9,	0x0900,	32.0 },
		{ "SUSE Sans",			 9,	0x0900,	32.0 },
		{ "SUSE Serif",			 9,	0x0900,	32.0 },
		{ "Script",			 6,	0x0999,	51.25 },
		{ "Segeo",			 8,	0x0924,	36.5 },
		{ "Serto Batnan",		 8,	0x0924,	36.5 },
		{ "Serto Jerusalem Outline",	 8,	0x0924,	36.5 },
		{ "Serto Jerusalem",		 8,	0x0924,	36.5 },
		{ "Serto Kharput",		 8,	0x0924,	36.5 },
		{ "Serto Malankara",		 8,	0x0924,	36.5 },
		{ "Serto Mardin",		 8,	0x0924,	36.5 },
		{ "Serto Urhoy",		 8,	0x0924,	36.5 },
		{ "Shruti",			 9,	0x0900,	32.0 },
		{ "Small Fonts",		 7,	0x0955,	42.5 },
		{ "Swiss742 Cn SWC",		 8,	0x0924,	36.5 },
		{ "Swiss742 SWC",		 8,	0x0924,	36.5 },
		{ "Sylfaen",			 8,	0x0924,	36.5 },
		{ "Symbol",			 7,	0x0955,	42.5 },
		{ "SymbolPS",			 7,	0x0955,	42.5 },
		{ "System",			 9,	0x0900,	32.0 },
		{ "Tahoma",			 8,	0x0924,	36.5 },
		{ "Terminal",			 9,	0x0900,	32.0 },
		{ "Thorndale AMT",		 7,	0x0955,	42.5 },
		{ "Times New Roman",		 7,	0x0955,	42.5 },
		{ "Tms Rmn",			 7,	0x0955,	42.5 },
		{ "Trebuchet MS",		 8,	0x0924,	36.5 },
		{ "Tunga",			 8,	0x0924,	36.5 },
		{ "Verdana",			 9,	0x0900,	32.0 },
		{ "Vrinda",			 7,	0x0955,	42.5 },
		{ "WST_Czec",			11,	0x08CC,	25.75 },
		{ "WST_Engl",			11,	0x08CC,	25.75 },
		{ "WST_Fren",			11,	0x08CC,	25.75 },
		{ "WST_Germ",			11,	0x08CC,	25.75 },
		{ "WST_Ital",			11,	0x08CC,	25.75 },
		{ "WST_Span",			11,	0x08CC,	25.75 },
		{ "WST_Swed",			11,	0x08CC,	25.75 },
		{ "Webdings",			15,	0x093B, 19.75 },
		{ "Wingdings 2",		17,	0x0911, 17.25 },
		{ "Wingdings 3",		13,	0x08AA, 21.25 },
		{ "Wingdings",			19,	0x08F0, 15.25 },
		{ "ZapfHumanist Dm SWC",	 8,	0x0924,	36.5 },
		{ NULL, -1, 0, 0. }
	};

#if 0
	/* TODO : fonts we had data for previously, but have not measured
	 * again */
	    {  90, 102, "AvantGarde" },
	    {  90, 102, "Century Schoolbook" },
	    {  86,  92, "CG Times" },
	    {  90, 102, "Geneva" },
	    {  90, 102, "Haettenscheiler" },
	    { 103, 110, "HE_TERMINAL" },
	    {  86,  92, "ITC Bookman" },
	    { 103, 110, "Letter Gothic MT" },
	    { 171, 182, "Map Symbols" },
	    {  86,  92, "NewCenturySchlbk" },
	    {  90, 102, "Optimum" },

	    { 109, 117, "Sans Regular" }, /* alias for bitstream */
	    { 142, 152, "Serpentine" },
	    { 103, 110, "System APL Special" },
	    { 103, 110, "System VT Special" },
	    {  86,  92, "Times" },

	    {  91,  97, "Times New Roman MT Extra Bold" },
	    {  86,  92, "ZapfChancery" },
	    { 230, 245, "ZapfDingbats" },
#endif
	int i;

	if (xl_font_width_hash == NULL) {
		xl_font_width_hash =
			g_hash_table_new (&go_ascii_strcase_hash, &go_ascii_strcase_equal);
		xl_font_width_warned =
			g_hash_table_new (&go_ascii_strcase_hash, &go_ascii_strcase_equal);
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


XL_font_width const *
xl_lookup_font_specs (char const *name)
{
	static gboolean need_init = TRUE;
	gpointer res;
	if (need_init) {
		need_init = FALSE;
		init_xl_font_widths ();
	}

	g_return_val_if_fail (xl_font_width_hash != NULL, &unknown_spec);
	g_return_val_if_fail (name != NULL, &unknown_spec);

	res = g_hash_table_lookup (xl_font_width_hash, name);

	if (res != NULL)
		return res;

	if (!g_hash_table_lookup (xl_font_width_warned, name)) {
		char *namecopy = g_strdup (name);
		g_warning ("EXCEL : unknown widths for font '%s', guessing", name);
		g_hash_table_insert (xl_font_width_warned, namecopy, namecopy);
	}

	return &unknown_spec;
}
