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
#include <style.h>
#include "ms-excel-util.h"
#include <goffice/goffice.h>
#include <glib/gi18n-lib.h>
#include <hlink.h>
#include <sheet-style.h>
#include <ranges.h>

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

static void
two_way_table_dump (const TwoWayTable *table)
{
	size_t ui;

	g_printerr ("Table at %p has "
		    "unique_keys.size=%d; "
		    "all_keys.size=%d; "
		    "idx_to_key.size=%d\n",
		    table,
		    g_hash_table_size (table->unique_keys),
		    g_hash_table_size (table->all_keys),
		    table->idx_to_key->len);

	for (ui = 0; ui < table->idx_to_key->len; ui++) {
		gpointer key = g_ptr_array_index (table->idx_to_key, ui);
		g_printerr ("%p => %d %d\n", key, (int)ui,
			    two_way_table_key_to_idx (table, key));
	}
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
		gint old_index = index;
		index = table->idx_to_key->len + table->base;

		if (found) {
			if (table->key_destroy_func)
				(table->key_destroy_func) (key);
			key = two_way_table_idx_to_key (table, old_index);
		} else {
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

	if (0) two_way_table_dump (table);

	return index;
}

/**
 * two_way_table_move
 * @table Table
 * @dst_idx: The new idx for the value
 * @src_idx: stored here
 *
 * Moves the key at index @src_idx into index @dst_idx, and drops the original
 * content of @dst_idx
 **/
void
two_way_table_move (TwoWayTable const *table, gint dst_idx, gint src_idx)
{
	gpointer key_to_forget, key_to_move;
	size_t ui;

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
	g_ptr_array_index (table->idx_to_key, dst_idx) = key_to_move;

	if (table->idx_to_key->len - 1 == (size_t)src_idx)
		g_ptr_array_set_size (table->idx_to_key, src_idx);
	else
		g_ptr_array_index (table->idx_to_key, src_idx) =
			(gpointer)0xdeadbeef; /* poison */

	for (ui = 0; ui < table->idx_to_key->len; ui++) {
		if (g_ptr_array_index (table->idx_to_key, ui) == key_to_forget) {
			g_hash_table_insert (table->unique_keys, key_to_forget,
					     GINT_TO_POINTER (ui + 1));
			break;
		}
	}

	if (0) two_way_table_dump (table);
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
		{ "DejaVu Sans Mono",	 	 9,	0x0900,	32.0 },
		{ "Bitstream Vera Sans",	 8,	0x0924,	36.5 },
		{ "DejaVu Sans",	 	 8,	0x0924,	36.5 },
		{ "Sans",			 8,	0x0924,	36.5 },
		{ "Bitstream Vera Serif",	 9,	0x0900,	32.0 },
		{ "DejaVu Serif",	 	 9,	0x0900,	32.0 },
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

/*
 * This mapping was derived from http://sc.openoffice.org/excelfileformat.pdf
 * and from the documentation for the Spreadsheet::WriteExcel perl module
 * (http://freshmeat.net/projects/writeexcel/).
 */
typedef struct {
	/* PWG 5101.1-2002 name for a physical paper size,
	 * and a boolean to indicate the paper is turned */
	char const *gp_name;
	gboolean const rotated;
} paper_size_table_entry;

static paper_size_table_entry const paper_size_table[] = {
	{ NULL, FALSE},		/* printer default / undefined */

	{ "na_letter_8.5x11in", FALSE },
	{ "na_letter_8.5x11in", FALSE },	/* Letter small */
	{ "na_ledger_11x17in", FALSE  },	/* Tabloid */
	{ "na_ledger_11x17in", TRUE },	/* Ledger ROTATED*/
	{ "na_legal_8.5x14in", FALSE },	/* Legal */

	{ "na_invoice_5.5x8.5in", FALSE },	/* Statement */
	{ "na_executive_7.25x10.5in", FALSE },	/* Executive */
	{ "iso_a3_297x420mm", FALSE },
	{ "iso_a4_210x297mm", FALSE },
	{ "iso_a4_210x297mm", FALSE },		/* A4 small */

	{ "iso_a5_148x210mm", FALSE },
	{ "iso_b4_250x353mm", FALSE },
	{ "iso_b5_176x250mm", FALSE },
	{ "na_foolscap_8.5x13in", FALSE },	/* Folio */
	{ "na_quarto_8.5x10.83in", FALSE },	/* Quarto */

	{ "na_10x14_10x14in",  FALSE },	/* 10x14 */
	{ "na_ledger_11x17in", FALSE },	/* 11x17 */
	{ "na_letter_8.5x11in", FALSE },	/* Note */
	{ "na_number-9_3.875x8.875in", FALSE},	/* Envelope #9 */
	{ "na_number-10_4.125x9.5in", FALSE},	/* Envelope #10 */
	{ "na_number-11_4.5x10.375in", FALSE},	/* Envelope #11 */
	{ "na_number-12_4.75x11in", FALSE },	/* Envelope #12 */
	{ "na_number-14_5x11.5in", FALSE },	/* Envelope #14 */
	{ "na_c_17x22in", FALSE },	/* C */
	{ "na_d_22x34in", FALSE },	/* D */
	{ "na_e_34x44in", FALSE },	/* E */

	{ "iso_dl_110x220mm", FALSE },		/* Envelope DL */
	{ "iso_c5_162x229mm", FALSE },		/* Envelope C5 */
	{ "iso_c3_324x458mm", FALSE },		/* Envelope C3 */
	{ "iso_c4_229x324mm", FALSE },		/* Envelope C4 */

	{ "iso_c6_114x162mm", FALSE },		/* Envelope C6 */
	{ "iso_c6c5_114x229mm", FALSE },	/* Envelope C6/C5 */
	{ "iso_b4_250x353mm", FALSE },
	{ "iso_b5_176x250mm", FALSE },
	{ "iso_b6_125x176mm", FALSE },

	{ "om_italian_110x230mm", FALSE },	/* Envelope Italy */
	{ "na_monarch_3.875x7.5in", FALSE },	/* Envelope Monarch */
	{ "na_personal_3.625x6.5in", FALSE },	/* 6 1/2 Envelope */
	{ "na_fanfold-us_11x14.875in", TRUE },	/* US Standard Fanfold ROTATED */
	{ "na_fanfold-eur_8.5x12in", FALSE },	/* German Std Fanfold */

	{ "na_foolscap_8.5x13in", FALSE },	/* German Legal Fanfold */
	{ "iso_b4_250x353mm", FALSE },		/* Yes, twice... */
	{ "jpn_hagaki_100x148mm", FALSE },	/* Japanese Postcard */
	{ "na_9x11_9x11in", FALSE },	/* 9x11 */
	{ "na_10x11_10x11in", FALSE },	/* 10x11 */

	{ "na_11x15_11x15in", FALSE },	/* 15x11 switch landscape */
	{ "om_invite_220x220mm", FALSE },	/* Envelope Invite */
	{ NULL, FALSE},		/* undefined */
	{ NULL, FALSE },		/* undefined */
	{ "na_letter-extra_9.5x12in", FALSE },	/* Letter Extra */

	{ "na_legal-extra_9.5x15in", FALSE },	/* Legal Extra */
	{ "na_arch-b_12x18in", FALSE },	/* Tabloid Extra */
	{ "iso_a4_extra_235.5x322.3mm", FALSE },	/* A4 Extra */
	{ "na_letter_8.5x11in", FALSE },	/* Letter Transverse */
	{ "iso_a4_210x297mm", FALSE },		/* A4 Transverse */

	{ "na_letter-extra_9.5x12in", FALSE },	/* Letter Extra Transverse */
	{ "custom_super-aa4_227x356mm", FALSE },	/* Super A/A4 */
	{ "custom_super-ba3_305x487mm", FALSE },	/* Super B/A3 */
	{ "na_letter-plus_8.5x12.69in", FALSE },	/* Letter Plus */
	{ "om_folio_210x330mm", FALSE },	/* A4 Plus */

	{ "iso_a5_148x210mm", FALSE },		/* A5 Transverse */
	{ "jis_b5_182x257mm", FALSE },		/* B5 (JIS) Transverse */
	{ "iso_a3-extra_322x455mm", FALSE },	/* A3 Extra */
	{ "iso_a5-extra_174x235mm", FALSE },	/* A5 Extra */
	{ "iso_b5-extra_201x276mm", FALSE },	/* B5 (ISO) Extra */

	{ "iso_a2_420x594mm", FALSE },
	{ "iso_a3_297x420mm", FALSE },		/* A3 Transverse */
	{ "iso_a3-extra_322x455mm", FALSE },	/* A3 Extra Transverse */
	{ "jpn_oufuku_148x200mm", TRUE },	/* Dbl. Japanese Postcard ROTATED */
	{ "iso_a6_105x148mm", FALSE },

	{ NULL, FALSE },		/* FIXME: No documentation found */
	{ NULL, FALSE },		/* FIXME: No documentation found */
	{ NULL, FALSE },		/* FIXME: No documentation found */
	{ NULL, FALSE },		/* FIXME: No documentation found */
	{ "na_letter_8.5x11in", TRUE },	/* Letter Rotated */

	{ "iso_a3_297x420mm", TRUE },	/* A3 Rotated */
	{ "iso_a4_210x297mm", TRUE },	/* A4 Rotated */
	{ "iso_a5_148x210mm", TRUE },	/* A5 Rotated */
	{ "jis_b4_257x364mm", TRUE },	/* B4 (JIS) Rotated */
	{ "jis_b5_182x257mm", TRUE },	/* B5 (JIS) Rotated */

	{ "jpn_hagaki_100x148mm", TRUE },	/* Japanese Postcard Rotated */
	{ "jpn_oufuku_148x200mm", FALSE },	/* Dbl. Jap. Postcard*/
	{ "iso_a6_105x148mm", TRUE },	/* A6 Rotated */
	{ NULL, FALSE },		/* FIXME: No documentation found */
	{ NULL, FALSE },		/* FIXME: No documentation found */


	{ NULL, FALSE },		/* FIXME: No documentation found */
	{ NULL, FALSE },		/* FIXME: No documentation found */
	{ "jis_b6_128x182mm", FALSE },		/* B6 (JIS) */
	{ "jis_b6_128x182mm", TRUE },	/* B6 (JIS) Rotated */
	{ "na_11x12_11x12in", TRUE },	/* 12x11 ROTATED */
};


const char *
xls_paper_name (unsigned idx, gboolean *rotated)
{
#if 0
	/*
	 * Self-check code.  We fail in ~15 cases where we lack info
	 * to distinguish.
	 */
	static gboolean first = TRUE;
	if (first) {
		unsigned ui;

		first = FALSE;

		for (ui = 0; ui < G_N_ELEMENTS (paper_size_table); ui++) {
			const char *name = paper_size_table[ui].gp_name;
			gboolean rotated = paper_size_table[ui].rotated;
			GtkPaperSize *ps = gtk_paper_size_new (name);
			unsigned ui2 = xls_paper_size (ps, rotated);
			if (ui != ui2 && name) {
				g_printerr ("PAPER MISMATCH: %d %d %s\n",
					    ui, ui2, name);
			}
			gtk_paper_size_free (ps);
		}
	}
#endif

	if (idx < G_N_ELEMENTS (paper_size_table)) {
		*rotated = paper_size_table[idx].rotated;
		return paper_size_table[idx].gp_name;
	} else {
		*rotated = FALSE;
		return NULL;
	}
}

unsigned
xls_paper_size (GtkPaperSize *ps, gboolean rotated)
{
	const char *name = gtk_paper_size_get_name (ps);
	size_t name_len = strlen (name);
	double w = gtk_paper_size_get_width (ps, GTK_UNIT_MM);
	double h = gtk_paper_size_get_height (ps, GTK_UNIT_MM);
	unsigned ui;

	for (ui = 0; ui < G_N_ELEMENTS (paper_size_table); ui++) {
		const char *thisname = paper_size_table[ui].gp_name;
		GtkPaperSize *tps;
		double d, tw, th;

		if (!thisname ||
		    strncmp (name, thisname, name_len) != 0 ||
		    thisname[name_len] != '_')
			continue;

		if (rotated != paper_size_table[ui].rotated)
			continue;

		tps = gtk_paper_size_new (thisname);
		tw = gtk_paper_size_get_width (tps, GTK_UNIT_MM);
		th = gtk_paper_size_get_height (tps, GTK_UNIT_MM);
		gtk_paper_size_free (tps);
		d = hypot (w - tw, h - th);
		if (d < 2.0) {
			return ui;
		}
	}

	return 0;
}


static void
xls_header_footer_export1 (GString *res, const char *s, const char *section)
{
	static const struct {
		const char *name;
		const char *xls_code;
	} codes[] = {
		{ N_("TAB"),   "&A"},
		{ N_("PAGE"),  "&P"},
		{ N_("PAGES"), "&N"},
		{ N_("DATE"),  "&D"},
		{ N_("TIME"),  "&T"},
		{ N_("FILE"),  "&F"},
		{ N_("PATH"),  "&Z"},
#if 0
		{ N_("CELL"),  "" /* ??? */},
		{ N_("TITLE"), "" /* ??? */}
#endif
	};

	if (!s || *s == 0)
		return;

	g_string_append (res, section);
	while (*s) {
		const char *end;

		if (*s == '&' && s[1] == '[' && (end = strchr (s + 2, ']'))) {
			size_t l = end - (s + 2);
			unsigned ui;

			for (ui = 0; ui < G_N_ELEMENTS (codes); ui++) {
				const char *tname = _(codes[ui].name);
				if (l == strlen (tname) &&
				    g_ascii_strncasecmp (tname, s + 2, l) == 0) {
					g_string_append (res, codes[ui].xls_code);
					break;
				}
			}
			s = end + 1;
			continue;
		}

		g_string_append_c (res, *s++);
	}
}


char *
xls_header_footer_export (const GnmPrintHF *hf)
{
	GString *res = g_string_new (NULL);

	xls_header_footer_export1 (res, hf->left_format, "&L");
	xls_header_footer_export1 (res, hf->middle_format, "&C");
	xls_header_footer_export1 (res, hf->right_format, "&R");

	return g_string_free (res, FALSE);
}

void
xls_header_footer_import (GnmPrintHF **phf, const char *txt)
{
	char section = 'L';
	GString *accum;
	GnmPrintHF *hf = *phf;

	if (!hf)
		*phf = hf = gnm_print_hf_new ("", "", "");
	else {
		g_free (hf->left_format);
		hf->left_format = g_strdup ("");
		g_free (hf->middle_format);
		hf->middle_format = g_strdup ("");
		g_free (hf->right_format);
		hf->right_format = g_strdup ("");
	}

	if (!txt)
		return;

	accum = g_string_new (NULL);
	while (1) {
		if (txt[0] == 0 ||
		    (txt[0] == '&' && txt[1] && strchr ("LCR", txt[1]))) {
			char **sp;
			switch (section) {
			case 'L': sp = &hf->left_format; break;
			case 'C': sp = &hf->middle_format; break;
			case 'R': sp = &hf->right_format; break;
			default: g_assert_not_reached ();
			}
			g_free (*sp);
			*sp = g_string_free (accum, FALSE);

			if (txt[0] == 0)
				break;

			accum = g_string_new (NULL);
			section = txt[1];
			txt += 2;
			continue;
		}

		if (txt[0] != '&') {
			g_string_append_c (accum, *txt++);
			continue;
		}

		txt++;
		switch (txt[0]) {
		case 0:
			continue;
		case '&':
			g_string_append_c (accum, *txt);
			break;
		case 'A':
			g_string_append (accum, "&[TAB]");
			break;
		case 'P':
			g_string_append (accum, "&[PAGE]");
			break;
		case 'N':
			g_string_append (accum, "&[PAGES]");
			break;
		case 'D':
			g_string_append (accum, "&[DATE]");
			break;
		case 'T':
			g_string_append (accum, "&[TIME]");
			break;
		case 'F':
			g_string_append (accum, "&[FILE]");
			break;
		case 'Z':
			g_string_append (accum, "&[PATH]");
			break;
		default:
			break;
		}
		txt++;
	}
}

/*****************************************************************************/

void
xls_arrow_to_xl (GOArrow const *arrow, double width,
		 XLArrowType *ptyp, int *pl, int *pw)
{
	double s = CLAMP (width, 1.0, 5); /* Excel arrows scale with line width */

	switch (arrow->typ) {
	case GO_ARROW_NONE:
		*ptyp = XL_ARROW_NONE;
		*pl = 0;
		*pw = 0;
		break;
	case GO_ARROW_KITE:
		if (fabs (arrow->a - arrow->b) < 0.01) {
			*ptyp = XL_ARROW_REGULAR;
			*pl = (int)CLAMP ((arrow->a / (s * 3.5)) - 1, 0.0, 2.0);
			*pw = (int)CLAMP ((arrow->c / (s * 2.5)) - 1, 0.0, 2.0);
		} else if (arrow->a > arrow->b) {
			*ptyp = XL_ARROW_DIAMOND;
			*pl = (int)CLAMP ((arrow->a / (s * 5.0)) - 1, 0.0, 2.0);
			*pw = (int)CLAMP ((arrow->c / (s * 2.5)) - 1, 0.0, 2.0);
		} else if (arrow->a < 0.5 * arrow->b) {
			*ptyp = XL_ARROW_OPEN;
			*pl = (int)CLAMP ((arrow->a / (s * 1.0)) - 1, 0.0, 2.0);
			*pw = (int)CLAMP ((arrow->c / (s * 1.5)) - 1, 0.0, 2.0);
		} else {
			*ptyp = XL_ARROW_STEALTH;
			*pl = (int)CLAMP ((arrow->b / (s * 4.0)) - 1, 0.0, 2.0);
			*pw = (int)CLAMP ((arrow->c / (s * 2.0)) - 1, 0.0, 2.0);
		}
		break;
	case GO_ARROW_OVAL:
		*ptyp = XL_ARROW_OVAL;
		*pl = (int)CLAMP ((arrow->a / (s * 2.5)) - 1, 0.0, 2.0);
		*pw = (int)CLAMP ((arrow->b / (s * 2.5)) - 1, 0.0, 2.0);
		break;
	default:
		g_assert_not_reached ();
	}
}

void
xls_arrow_from_xl (GOArrow *arrow, double width, XLArrowType typ, int l, int w)
{
	double s = CLAMP (width, 1.0, 5); /* Excel arrows scale with line width */

	switch (typ) {
	case XL_ARROW_NONE:
		go_arrow_clear (arrow);
		break;
	default:
	case XL_ARROW_REGULAR:
		go_arrow_init_kite (arrow,
				    s * 3.5 * (l + 1),
				    s * 3.5 * (l + 1),
				    s * 2.5 * (w + 1));
		break;
	case XL_ARROW_STEALTH:
		go_arrow_init_kite (arrow,
				    s * 2.5 * (l + 1),
				    s * 4.0 * (l + 1),
				    s * 2.0 * (w + 1));
		break;
	case XL_ARROW_DIAMOND:
		go_arrow_init_kite (arrow,
				    s * 5 * (l + 1),
				    s * 2.5 * (l + 1),
				    s * 2.5 * (w + 1));
		break;
	case XL_ARROW_OVAL:
		go_arrow_init_oval (arrow, s * 2.5 * (l + 1), s * 2.5 * (w + 1));
		break;
	case XL_ARROW_OPEN: /* Approximation! */
		go_arrow_init_kite (arrow,
				    s * 1.0 * (l + 1),
				    s * 2.5 * (l + 1),
				    s * 1.5 * (w + 1));
		break;
	}
}

/*****************************************************************************/

GHashTable *
xls_collect_hlinks (GnmStyleList *sl, int max_col, int max_row)
{
	GHashTable *group = g_hash_table_new_full
		(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_slist_free);
	GList *keys, *k;

	for (; sl != NULL ; sl = sl->next) {
		GnmStyleRegion const *sr = sl->data;
		GnmHLink   *hlink;
		GSList	   *ranges;

		/* Clip here to avoid creating a DV record if there are no regions */
		if (sr->range.start.col >= max_col ||
		    sr->range.start.row >= max_row) {
			range_dump (&sr->range, "bounds drop\n");
			continue;
		}
		hlink  = gnm_style_get_hlink (sr->style);
		ranges = g_hash_table_lookup (group, hlink);
		if (ranges)
			g_hash_table_steal (group, hlink);
		g_hash_table_insert (group, hlink,
				     g_slist_prepend (ranges, (gpointer)&sr->range));
	}

	keys = g_hash_table_get_keys (group);
	for (k = keys; k ; k = k->next) {
		GnmHLink *hlink = k->data;
		GSList *ranges = g_hash_table_lookup (group, hlink);
		GSList *nranges = g_slist_sort (ranges, (GCompareFunc)gnm_range_compare);
		if (ranges != nranges) {
			g_hash_table_steal (group, hlink);
			g_hash_table_insert (group, hlink, nranges);
		}
	}
	g_list_free (keys);

	return group;
}

/****************************************************************************/

static guint
vip_hash (XLValInputPair const *vip)
{
	/* bogus, but who cares */
	return GPOINTER_TO_UINT (vip->v) ^ GPOINTER_TO_UINT (vip->msg);
}

static gint
vip_equal (XLValInputPair const *a, XLValInputPair const *b)
{
	return a->v == b->v && a->msg == b->msg;
}

static void
vip_free (XLValInputPair *vip)
{
	g_slist_free (vip->ranges);
	g_free (vip);
}

/* We store input msg and validation as distinct items, XL merges them find the
 * pairs, and the regions that use them */
GHashTable *
xls_collect_validations (GnmStyleList *ptr, int max_col, int max_row)
{
	GHashTable *group = g_hash_table_new_full
		((GHashFunc)vip_hash,
		 (GCompareFunc)vip_equal,
		 (GDestroyNotify)vip_free,
		 NULL);
	GHashTableIter iter;
	gpointer vip_;

	for (; ptr != NULL ; ptr = ptr->next) {
		GnmStyleRegion const *sr = ptr->data;
		XLValInputPair key, *tmp;

		/* Clip here to avoid creating a DV record if there are no regions */
		if (sr->range.start.col >= max_col ||
		    sr->range.start.row >= max_row) {
			range_dump (&sr->range, "bounds drop\n");
			continue;
		}

		key.v   = gnm_style_get_validation (sr->style);
		key.msg = gnm_style_get_input_msg (sr->style);
		tmp = g_hash_table_lookup (group, &key);
		if (tmp == NULL) {
			tmp = g_new (XLValInputPair, 1);
			tmp->v = key.v;
			tmp->msg = key.msg;
			tmp->ranges = NULL;
			g_hash_table_insert (group, tmp, tmp);
		}
		tmp->ranges = g_slist_prepend (tmp->ranges, (gpointer)&sr->range);
	}

	g_hash_table_iter_init (&iter, group);
	while (g_hash_table_iter_next (&iter, &vip_, NULL)) {
		XLValInputPair *vip = vip_;
		vip->ranges = g_slist_sort (vip->ranges, (GCompareFunc)gnm_range_compare);
	}

	return group;
}

/*****************************************************************************/
