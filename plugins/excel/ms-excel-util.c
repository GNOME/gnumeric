/**
 * ms-excel-util.c: Utility functions for MS Excel import / export
 *
 * Author:
 *    Jon K Hellan (hellan@acm.org)
 *
 * (C) 1999, 2000 Jon K Hellan
 * excel_iconv* family of functions (C) 2001 by Vlad Harchev <hvv@hippo.ru>
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <glib.h>

#include "boot.h"
#include "style.h"
#include "ms-excel-util.h"

#include <stdio.h>
#include <string.h>
#include <locale.h>

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif

#ifdef HAVE_ICONV_H
#define HAVE_ICONV
#include <iconv.h>
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
 * two_way_table_replace:    Replace the key for an index in the TwoWayTable
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
two_way_table_new (GHashFunc    hash_func,
		   GCompareFunc key_compare_func,
		   gint         base)
{
	TwoWayTable *table = g_new (TwoWayTable, 1);

	g_return_val_if_fail (base >= 0, NULL);
	table->all_keys  = g_hash_table_new (g_direct_hash, g_direct_equal);
	table->unique_keys	   = g_hash_table_new (hash_func, key_compare_func);
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
 * two_way_table_replace
 * @table Table
 * @idx   Index to be updated
 * @key   New key to be assigned to the index
 *
 * Replaces the key bound to an index with a new value. Returns the old key.
 */
gpointer
two_way_table_replace (const TwoWayTable *table, gint idx, gpointer key)
{
	gpointer old_key = two_way_table_idx_to_key (table, idx);

	g_hash_table_remove (table->all_keys, old_key);
	g_hash_table_insert (table->all_keys, key, GINT_TO_POINTER (idx + 1));
	g_ptr_array_index   (table->idx_to_key, idx) = key;

	return old_key;
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
lookup_font_base_char_width_new (char const * const name, double size_pts,
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



#ifdef HAVE_ICONV
static char const *
get_locale_charset_name (void)
{
	const char *ccharset;
	static char const * charset = NULL;

	if (charset != NULL)
		return charset;

#ifdef _NL_CTYPE_CODESET_NAME
	ccharset = nl_langinfo (_NL_CTYPE_CODESET_NAME);
#elif defined(CODESET)
	ccharset = nl_langinfo (CODESET);
#else
	{
		char const *locale = setlocale (LC_CTYPE, NULL);
		if (locale != NULL) {
			const char *tmp = strchr (locale, '.');
			if (tmp != NULL)
				ccharset = tmp + 1;
		}
	}
#endif
	return (charset = (charset != NULL) ? g_strdup (charset) : "ISO-8859-1");
}
#endif

typedef struct
{
	char const * const * keys;/*NULL-terminated list*/
	int value;
} s_hash_entry;

/* here is a list of languages for which cp1251 is used on Windows*/
static char const * const cyr_locales[] =
{
	"be", "be_BY", "bulgarian", "bg", "bg_BG", "mk", "mk_MK",
	"russian", "ru", "ru_RU", "ru_UA", "sp", "sp_YU", "sr", "sr_YU",
	"ukrainian", "uk", "uk_UA", NULL
};


/* here is a list of languages for which cp for cjk is used on Windows*/
static char const * const jp_locales[] =
{
	"japan", "japanese", "ja", "ja_JP", NULL
};

static char const * const zhs_locales[] =
{
	"chinese-s", "zh", "zh_CN", NULL
};

static char const * const kr_locales[] =
{
	"korean", "ko", "ko_KR", NULL
};

static char const * const zht_locales[] =
{
	"chinese-t", "zh_HK", "zh_TW", NULL
 };
 
static s_hash_entry const win_codepages[]=
{
	{ cyr_locales , 1251 },
	{ jp_locales  , 932 },
	{ zhs_locales , 936 },
	{ kr_locales  , 949 },
	{ zht_locales , 950 },
	{ NULL } /*terminator*/
};

guint
excel_iconv_win_codepage (void)
{
	static guint codepage = 0;

	if (codepage == 0) {
		char *lang;

		if ((lang = getenv("WINDOWS_LANGUAGE")) == NULL) {
			char const *locale = setlocale (LC_CTYPE, NULL);
			if (locale != NULL) {
				char const *lang_sep = strchr (locale, '.');
				if (lang_sep)
				    lang = g_strndup (locale, lang_sep - locale);
				else
				    lang = g_strdup (locale); /* simplifies exit */
			}
		}

		if (lang != NULL) {
			s_hash_entry const * entry;

			for (entry = win_codepages; entry->keys; ++entry) {
				char const* const * key;
				for (key = entry->keys; *key; ++key)
					if (!g_strcasecmp (*key, lang)) {
						g_free (lang);
						return (codepage = entry->value);
					}
			}
			g_free (lang);
		}
		codepage = 1252; /* default */
	}
	return codepage;
}

/*these two will figure out which charset names to use*/
excel_iconv_t
excel_iconv_open_for_import (guint codepage)
{
#ifndef HAVE_ICONV
	return (excel_iconv_t)(-1);
#else
	iconv_t iconv_handle;

	/* What the hell ?
	 * this makes no sense.  UCS2 is a 2 byte encoding.
	 * We only use this for 1 byte strings.  What does it mean
	 * to say that all strings are 2 byte, then store them a 1 byte.
	 * Make a big big guess and just assume 8859-1
	 */
	if (codepage != 1200) {
		char* src_charset = g_strdup_printf ("CP%d", codepage);
		iconv_handle = iconv_open ("UTF-8", src_charset);
		g_free (src_charset);
		if (iconv_handle != (excel_iconv_t)(-1))
			return iconv_handle;
		g_warning ("Unknown codepage %d", codepage);
	}
	return iconv_open ("UTF-8", "ISO-8859-1");
#endif
}

excel_iconv_t
excel_iconv_open_for_export (void)
{
#ifndef HAVE_ICONV
	return (excel_iconv_t)(-1);
#else
	static char* dest_charset = NULL;
	iconv_t iconv_handle;

	if (!dest_charset)
		dest_charset = g_strdup_printf ("CP%d", excel_iconv_win_codepage());
	iconv_handle = iconv_open (dest_charset, get_locale_charset_name ());
	return iconv_handle;
#endif
}

void
excel_iconv_close (excel_iconv_t handle)
{
#ifdef HAVE_ICONV
	if (handle && handle != (excel_iconv_t)(-1))
		iconv_close (handle);
#endif
}

size_t
excel_iconv (excel_iconv_t handle,
	     char const * *inbuf, size_t *inbytesleft,
	     char **outbuf, size_t *outbytesleft)
{
#ifndef HAVE_ICONV
	guint tocopy = *inbytesleft <= *outbytesleft ? *inbytesleft : *outbytesleft;
	memcpy(*outbuf,*inbuf,tocopy);
	*outbuf += tocopy;
	*inbuf += tocopy;
	*outbytesleft -= tocopy;
	*inbytesleft -= tocopy;
#else
	while (*inbytesleft) {
		if (handle && handle != (iconv_t)(-1))
			iconv ((iconv_t)handle, (char **)inbuf, inbytesleft,
			       outbuf, outbytesleft);
		if (!*inbytesleft || !*outbytesleft)
			return 0;
		/*got invalid seq - so replace it with original character*/
		**outbuf = **inbuf; (*outbuf)++; (*outbytesleft)--;
		(*inbuf)++; (*inbytesleft)--;
	}
#endif
	return 0;
}

size_t
excel_wcstombs (char *outbuf, wchar_t *wcs, size_t length)
{
	size_t i;
	char *outbuf_orig = outbuf;

	excel_iconv_t cd =
#ifndef HAVE_ICONV
		(excel_iconv_t)(-1);
#else
		iconv_open (get_locale_charset_name (), "utf8");
#endif

	for(i = 0; i < length; i++) {
		wchar_t wc = wcs[i];
#ifdef HAVE_ICONV
		if (cd != (excel_iconv_t)(-1)) {
			char utf8buf[9];
			size_t out_len = 10;
			char* inbuf_ptr = utf8buf;
			/* convert this char to utf8, and then to
			 * locale's encoding, rathern than from UCS2, since
			 * various iconv implementations use various byte order
			 * and names for UCS2. Utf8 is known to every one.
			 */
			size_t inbuf_len = g_unichar_to_utf8((gint)wc, (gchar*)utf8buf);

			iconv((iconv_t)cd,&inbuf_ptr,&inbuf_len,&outbuf,&out_len);
			if (!inbuf_len)
				continue;
		}
		/* find a replacement for wc*/
		if (wc < 128) /* very strange - why can't convert? */
			*(outbuf++) = (char)wc;
#else
		if (wc < 256) /* assume it's ISO-8869-1*/
			*(outbuf++) = (unsigned char)wc;
#endif
		else {
			const char* str;
			switch (wc) {
				/* The entries here are generated from
				 * src/translit.def from libiconv package.
				 */
				case 0x00A2: str="c"	; break; /* CENT SIGN	*/
				case 0x00A3: str="lb"	; break; /* POUND SIGN	*/
				case 0x00A4: str=""	; break; /* CURRENCY SIGN	*/
				case 0x00A5: str="yen"	; break; /* YEN SIGN	*/
				case 0x00A6: str="|"	; break; /* BROKEN BAR	*/
				case 0x00A7: str="SS"	; break; /* SECTION SIGN	*/
				case 0x00A8: str="\""	; break; /* DIAERESIS	*/
				case 0x00A9: str="(c)"	; break; /* COPYRIGHT SIGN	*/
				case 0x00AA: str="a"	; break; /* FEMININE ORDINAL INDICATOR	*/
				case 0x00AB: str="<<"	; break; /* LEFT-POINTING DOUBLE ANGLE QUOTATION MARK	*/
				case 0x00AC: str="not"	; break; /* NOT SIGN	*/
				case 0x00AD: str="-"	; break; /* SOFT HYPHEN	*/
				case 0x00AE: str="(R)"	; break; /* REGISTERED SIGN	*/
				case 0x00AF: str=""	; break; /* MACRON	*/
				case 0x00B0: str="^0"	; break; /* DEGREE SIGN	*/
				case 0x00B1: str="+/-"	; break; /* PLUS-MINUS SIGN	*/
				case 0x00B2: str="^2"	; break; /* SUPERSCRIPT TWO	*/
				case 0x00B3: str="^3"	; break; /* SUPERSCRIPT THREE	*/
				case 0x00B4: str="'"	; break; /* ACUTE ACCENT	*/
				case 0x00B5: str="u"	; break; /* MICRO SIGN	*/
				case 0x00B6: str="P"	; break; /* PILCROW SIGN	*/
				case 0x00B7: str="."	; break; /* MIDDLE DOT	*/
				case 0x00B8: str=","	; break; /* CEDILLA	*/
				case 0x00B9: str="^1"	; break; /* SUPERSCRIPT ONE	*/
				case 0x00BA: str="o"	; break; /* MASCULINE ORDINAL INDICATOR	*/
				case 0x00BB: str=">>"	; break; /* RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK	*/
				case 0x00BC: str="1/4"	; break; /* VULGAR FRACTION ONE QUARTER	*/
				case 0x00BD: str="1/2"	; break; /* VULGAR FRACTION ONE HALF	*/
				case 0x00BE: str="3/4"	; break; /* VULGAR FRACTION THREE QUARTERS	*/
				case 0x00BF: str="?"	; break; /* INVERTED QUESTION MARK	*/
				case 0x00C0: str="`A"	; break; /* LATIN CAPITAL LETTER A WITH GRAVE	*/
				case 0x00C1: str="´A"	; break; /* LATIN CAPITAL LETTER A WITH ACUTE	*/
				case 0x00C2: str="^A"	; break; /* LATIN CAPITAL LETTER A WITH CIRCUMFLEX	*/
				case 0x00C3: str="~A"	; break; /* LATIN CAPITAL LETTER A WITH TILDE	*/
				case 0x00C4: str="\"A"	; break; /* LATIN CAPITAL LETTER A WITH DIAERESIS	*/
				case 0x00C5: str="A"	; break; /* LATIN CAPITAL LETTER A WITH RING ABOVE	*/
				case 0x00C6: str="AE"	; break; /* LATIN CAPITAL LETTER AE	*/
				case 0x00C7: str="C"	; break; /* LATIN CAPITAL LETTER C WITH CEDILLA	*/
				case 0x00C8: str="`E"	; break; /* LATIN CAPITAL LETTER E WITH GRAVE	*/
				case 0x00C9: str="´E"	; break; /* LATIN CAPITAL LETTER E WITH ACUTE	*/
				case 0x00CA: str="^E"	; break; /* LATIN CAPITAL LETTER E WITH CIRCUMFLEX	*/
				case 0x00CB: str="\"E"	; break; /* LATIN CAPITAL LETTER E WITH DIAERESIS	*/
				case 0x00CC: str="`I"	; break; /* LATIN CAPITAL LETTER I WITH GRAVE	*/
				case 0x00CD: str="´I"	; break; /* LATIN CAPITAL LETTER I WITH ACUTE	*/
				case 0x00CE: str="^I"	; break; /* LATIN CAPITAL LETTER I WITH CIRCUMFLEX	*/
				case 0x00CF: str="\"I"	; break; /* LATIN CAPITAL LETTER I WITH DIAERESIS	*/
				case 0x00D0: str="D"	; break; /* LATIN CAPITAL LETTER ETH	*/
				case 0x00D1: str="~N"	; break; /* LATIN CAPITAL LETTER N WITH TILDE	*/
				case 0x00D2: str="`O"	; break; /* LATIN CAPITAL LETTER O WITH GRAVE	*/
				case 0x00D3: str="´O"	; break; /* LATIN CAPITAL LETTER O WITH ACUTE	*/
				case 0x00D4: str="^O"	; break; /* LATIN CAPITAL LETTER O WITH CIRCUMFLEX	*/
				case 0x00D5: str="~O"	; break; /* LATIN CAPITAL LETTER O WITH TILDE	*/
				case 0x00D6: str="\"O"	; break; /* LATIN CAPITAL LETTER O WITH DIAERESIS	*/
				case 0x00D7: str="x"	; break; /* MULTIPLICATION SIGN	*/
				case 0x00D8: str="O"	; break; /* LATIN CAPITAL LETTER O WITH STROKE	*/
				case 0x00D9: str="`U"	; break; /* LATIN CAPITAL LETTER U WITH GRAVE	*/
				case 0x00DA: str="´U"	; break; /* LATIN CAPITAL LETTER U WITH ACUTE	*/
				case 0x00DB: str="^U"	; break; /* LATIN CAPITAL LETTER U WITH CIRCUMFLEX	*/
				case 0x00DC: str="\"U"	; break; /* LATIN CAPITAL LETTER U WITH DIAERESIS	*/
				case 0x00DD: str="´Y"	; break; /* LATIN CAPITAL LETTER Y WITH ACUTE	*/
				case 0x00DE: str="Th"	; break; /* LATIN CAPITAL LETTER THORN	*/
				case 0x00DF: str="ss"	; break; /* LATIN SMALL LETTER SHARP S	*/
				case 0x00E0: str="`a"	; break; /* LATIN SMALL LETTER A WITH GRAVE	*/
				case 0x00E1: str="´a"	; break; /* LATIN SMALL LETTER A WITH ACUTE	*/
				case 0x00E2: str="^a"	; break; /* LATIN SMALL LETTER A WITH CIRCUMFLEX	*/
				case 0x00E3: str="~a"	; break; /* LATIN SMALL LETTER A WITH TILDE	*/
				case 0x00E4: str="\"a"	; break; /* LATIN SMALL LETTER A WITH DIAERESIS	*/
				case 0x00E5: str="a"	; break; /* LATIN SMALL LETTER A WITH RING ABOVE	*/
				case 0x00E6: str="ae"	; break; /* LATIN SMALL LETTER AE	*/
				case 0x00E7: str="c"	; break; /* LATIN SMALL LETTER C WITH CEDILLA	*/
				case 0x00E8: str="`e"	; break; /* LATIN SMALL LETTER E WITH GRAVE	*/
				case 0x00E9: str="´e"	; break; /* LATIN SMALL LETTER E WITH ACUTE	*/
				case 0x00EA: str="^e"	; break; /* LATIN SMALL LETTER E WITH CIRCUMFLEX	*/
				case 0x00EB: str="\"e"	; break; /* LATIN SMALL LETTER E WITH DIAERESIS	*/
				case 0x00EC: str="`i"	; break; /* LATIN SMALL LETTER I WITH GRAVE	*/
				case 0x00ED: str="´i"	; break; /* LATIN SMALL LETTER I WITH ACUTE	*/
				case 0x00EE: str="^i"	; break; /* LATIN SMALL LETTER I WITH CIRCUMFLEX	*/
				case 0x00EF: str="\"i"	; break; /* LATIN SMALL LETTER I WITH DIAERESIS	*/
				case 0x00F0: str="d"	; break; /* LATIN SMALL LETTER ETH	*/
				case 0x00F1: str="~n"	; break; /* LATIN SMALL LETTER N WITH TILDE	*/
				case 0x00F2: str="`o"	; break; /* LATIN SMALL LETTER O WITH GRAVE	*/
				case 0x00F3: str="´o"	; break; /* LATIN SMALL LETTER O WITH ACUTE	*/
				case 0x00F4: str="^o"	; break; /* LATIN SMALL LETTER O WITH CIRCUMFLEX	*/
				case 0x00F5: str="~o"	; break; /* LATIN SMALL LETTER O WITH TILDE	*/
				case 0x00F6: str="\"o"	; break; /* LATIN SMALL LETTER O WITH DIAERESIS	*/
				case 0x00F7: str=":"	; break; /* DIVISION SIGN	*/
				case 0x00F8: str="o"	; break; /* LATIN SMALL LETTER O WITH STROKE	*/
				case 0x00F9: str="`u"	; break; /* LATIN SMALL LETTER U WITH GRAVE	*/
				case 0x00FA: str="´u"	; break; /* LATIN SMALL LETTER U WITH ACUTE	*/
				case 0x00FB: str="^u"	; break; /* LATIN SMALL LETTER U WITH CIRCUMFLEX	*/
				case 0x00FC: str="\"u"	; break; /* LATIN SMALL LETTER U WITH DIAERESIS	*/
				case 0x00FD: str="´y"	; break; /* LATIN SMALL LETTER Y WITH ACUTE	*/
				case 0x00FE: str="th"	; break; /* LATIN SMALL LETTER THORN	*/
				case 0x00FF: str="\"y"	; break; /* LATIN SMALL LETTER Y WITH DIAERESIS	*/
				case 0x0100: str="A"	; break; /* LATIN CAPITAL LETTER A WITH MACRON	*/
				case 0x0101: str="a"	; break; /* LATIN SMALL LETTER A WITH MACRON	*/
				case 0x0102: str="A"	; break; /* LATIN CAPITAL LETTER A WITH BREVE	*/
				case 0x0103: str="a"	; break; /* LATIN SMALL LETTER A WITH BREVE	*/
				case 0x0104: str="A"	; break; /* LATIN CAPITAL LETTER A WITH OGONEK	*/
				case 0x0105: str="a"	; break; /* LATIN SMALL LETTER A WITH OGONEK	*/
				case 0x0106: str="´C"	; break; /* LATIN CAPITAL LETTER C WITH ACUTE	*/
				case 0x0107: str="´c"	; break; /* LATIN SMALL LETTER C WITH ACUTE	*/
				case 0x0108: str="^C"	; break; /* LATIN CAPITAL LETTER C WITH CIRCUMFLEX	*/
				case 0x0109: str="^c"	; break; /* LATIN SMALL LETTER C WITH CIRCUMFLEX	*/
				case 0x010A: str="C"	; break; /* LATIN CAPITAL LETTER C WITH DOT ABOVE	*/
				case 0x010B: str="c"	; break; /* LATIN SMALL LETTER C WITH DOT ABOVE	*/
				case 0x010C: str="C"	; break; /* LATIN CAPITAL LETTER C WITH CARON	*/
				case 0x010D: str="c"	; break; /* LATIN SMALL LETTER C WITH CARON	*/
				case 0x010E: str="D"	; break; /* LATIN CAPITAL LETTER D WITH CARON	*/
				case 0x010F: str="d"	; break; /* LATIN SMALL LETTER D WITH CARON	*/
				case 0x0110: str="D"	; break; /* LATIN CAPITAL LETTER D WITH STROKE	*/
				case 0x0111: str="d"	; break; /* LATIN SMALL LETTER D WITH STROKE	*/
				case 0x0112: str="E"	; break; /* LATIN CAPITAL LETTER E WITH MACRON	*/
				case 0x0113: str="e"	; break; /* LATIN SMALL LETTER E WITH MACRON	*/
				case 0x0114: str="E"	; break; /* LATIN CAPITAL LETTER E WITH BREVE	*/
				case 0x0115: str="e"	; break; /* LATIN SMALL LETTER E WITH BREVE	*/
				case 0x0116: str="E"	; break; /* LATIN CAPITAL LETTER E WITH DOT ABOVE	*/
				case 0x0117: str="e"	; break; /* LATIN SMALL LETTER E WITH DOT ABOVE	*/
				case 0x0118: str="E"	; break; /* LATIN CAPITAL LETTER E WITH OGONEK	*/
				case 0x0119: str="e"	; break; /* LATIN SMALL LETTER E WITH OGONEK	*/
				case 0x011A: str="E"	; break; /* LATIN CAPITAL LETTER E WITH CARON	*/
				case 0x011B: str="e"	; break; /* LATIN SMALL LETTER E WITH CARON	*/
				case 0x011C: str="^G"	; break; /* LATIN CAPITAL LETTER G WITH CIRCUMFLEX	*/
				case 0x011D: str="^g"	; break; /* LATIN SMALL LETTER G WITH CIRCUMFLEX	*/
				case 0x011E: str="G"	; break; /* LATIN CAPITAL LETTER G WITH BREVE	*/
				case 0x011F: str="g"	; break; /* LATIN SMALL LETTER G WITH BREVE	*/
				case 0x0120: str="G"	; break; /* LATIN CAPITAL LETTER G WITH DOT ABOVE	*/
				case 0x0121: str="g"	; break; /* LATIN SMALL LETTER G WITH DOT ABOVE	*/
				case 0x0122: str="G"	; break; /* LATIN CAPITAL LETTER G WITH CEDILLA	*/
				case 0x0123: str="g"	; break; /* LATIN SMALL LETTER G WITH CEDILLA	*/
				case 0x0124: str="^H"	; break; /* LATIN CAPITAL LETTER H WITH CIRCUMFLEX	*/
				case 0x0125: str="^h"	; break; /* LATIN SMALL LETTER H WITH CIRCUMFLEX	*/
				case 0x0126: str="H"	; break; /* LATIN CAPITAL LETTER H WITH STROKE	*/
				case 0x0127: str="h"	; break; /* LATIN SMALL LETTER H WITH STROKE	*/
				case 0x0128: str="~I"	; break; /* LATIN CAPITAL LETTER I WITH TILDE	*/
				case 0x0129: str="~i"	; break; /* LATIN SMALL LETTER I WITH TILDE	*/
				case 0x012A: str="I"	; break; /* LATIN CAPITAL LETTER I WITH MACRON	*/
				case 0x012B: str="i"	; break; /* LATIN SMALL LETTER I WITH MACRON	*/
				case 0x012C: str="I"	; break; /* LATIN CAPITAL LETTER I WITH BREVE	*/
				case 0x012D: str="i"	; break; /* LATIN SMALL LETTER I WITH BREVE	*/
				case 0x012E: str="I"	; break; /* LATIN CAPITAL LETTER I WITH OGONEK	*/
				case 0x012F: str="i"	; break; /* LATIN SMALL LETTER I WITH OGONEK	*/
				case 0x0130: str="I"	; break; /* LATIN CAPITAL LETTER I WITH DOT ABOVE	*/
				case 0x0131: str="i"	; break; /* LATIN SMALL LETTER DOTLESS I	*/
				case 0x0132: str="IJ"	; break; /* LATIN CAPITAL LIGATURE IJ	*/
				case 0x0133: str="ij"	; break; /* LATIN SMALL LIGATURE IJ	*/
				case 0x0134: str="^J"	; break; /* LATIN CAPITAL LETTER J WITH CIRCUMFLEX	*/
				case 0x0135: str="^j"	; break; /* LATIN SMALL LETTER J WITH CIRCUMFLEX	*/
				case 0x0136: str="K"	; break; /* LATIN CAPITAL LETTER K WITH CEDILLA	*/
				case 0x0137: str="k"	; break; /* LATIN SMALL LETTER K WITH CEDILLA	*/
				case 0x0138: str=""	; break; /* LATIN SMALL LETTER KRA	*/
				case 0x0139: str="L"	; break; /* LATIN CAPITAL LETTER L WITH ACUTE	*/
				case 0x013A: str="l"	; break; /* LATIN SMALL LETTER L WITH ACUTE	*/
				case 0x013B: str="L"	; break; /* LATIN CAPITAL LETTER L WITH CEDILLA	*/
				case 0x013C: str="l"	; break; /* LATIN SMALL LETTER L WITH CEDILLA	*/
				case 0x013D: str="L"	; break; /* LATIN CAPITAL LETTER L WITH CARON	*/
				case 0x013E: str="l"	; break; /* LATIN SMALL LETTER L WITH CARON	*/
				case 0x013F: str="L"	; break; /* LATIN CAPITAL LETTER L WITH MIDDLE DOT	*/
				case 0x0140: str="l"	; break; /* LATIN SMALL LETTER L WITH MIDDLE DOT	*/
				case 0x0141: str="L"	; break; /* LATIN CAPITAL LETTER L WITH STROKE	*/
				case 0x0142: str="l"	; break; /* LATIN SMALL LETTER L WITH STROKE	*/
				case 0x0143: str="´N"	; break; /* LATIN CAPITAL LETTER N WITH ACUTE	*/
				case 0x0144: str="´n"	; break; /* LATIN SMALL LETTER N WITH ACUTE	*/
				case 0x0145: str="N"	; break; /* LATIN CAPITAL LETTER N WITH CEDILLA	*/
				case 0x0146: str="n"	; break; /* LATIN SMALL LETTER N WITH CEDILLA	*/
				case 0x0147: str="N"	; break; /* LATIN CAPITAL LETTER N WITH CARON	*/
				case 0x0148: str="n"	; break; /* LATIN SMALL LETTER N WITH CARON	*/
				case 0x0149: str="'n"	; break; /* LATIN SMALL LETTER N PRECEDED BY APOSTROPHE	*/
				case 0x014A: str=""	; break; /* LATIN CAPITAL LETTER ENG	*/
				case 0x014B: str=""	; break; /* LATIN SMALL LETTER ENG	*/
				case 0x014C: str="O"	; break; /* LATIN CAPITAL LETTER O WITH MACRON	*/
				case 0x014D: str="o"	; break; /* LATIN SMALL LETTER O WITH MACRON	*/
				case 0x014E: str="O"	; break; /* LATIN CAPITAL LETTER O WITH BREVE	*/
				case 0x014F: str="o"	; break; /* LATIN SMALL LETTER O WITH BREVE	*/
				case 0x0150: str="\"O"	; break; /* LATIN CAPITAL LETTER O WITH DOUBLE ACUTE	*/
				case 0x0151: str="\"o"	; break; /* LATIN SMALL LETTER O WITH DOUBLE ACUTE	*/
				case 0x0152: str="OE"	; break; /* LATIN CAPITAL LIGATURE OE	*/
				case 0x0153: str="oe"	; break; /* LATIN SMALL LIGATURE OE	*/
				case 0x0154: str="´R"	; break; /* LATIN CAPITAL LETTER R WITH ACUTE	*/
				case 0x0155: str="´r"	; break; /* LATIN SMALL LETTER R WITH ACUTE	*/
				case 0x0156: str="R"	; break; /* LATIN CAPITAL LETTER R WITH CEDILLA	*/
				case 0x0157: str="r"	; break; /* LATIN SMALL LETTER R WITH CEDILLA	*/
				case 0x0158: str="R"	; break; /* LATIN CAPITAL LETTER R WITH CARON	*/
				case 0x0159: str="r"	; break; /* LATIN SMALL LETTER R WITH CARON	*/
				case 0x015A: str="´S"	; break; /* LATIN CAPITAL LETTER S WITH ACUTE	*/
				case 0x015B: str="´s"	; break; /* LATIN SMALL LETTER S WITH ACUTE	*/
				case 0x015C: str="^S"	; break; /* LATIN CAPITAL LETTER S WITH CIRCUMFLEX	*/
				case 0x015D: str="^s"	; break; /* LATIN SMALL LETTER S WITH CIRCUMFLEX	*/
				case 0x015E: str="S"	; break; /* LATIN CAPITAL LETTER S WITH CEDILLA	*/
				case 0x015F: str="s"	; break; /* LATIN SMALL LETTER S WITH CEDILLA	*/
				case 0x0160: str="S"	; break; /* LATIN CAPITAL LETTER S WITH CARON	*/
				case 0x0161: str="s"	; break; /* LATIN SMALL LETTER S WITH CARON	*/
				case 0x0162: str="T"	; break; /* LATIN CAPITAL LETTER T WITH CEDILLA	*/
				case 0x0163: str="t"	; break; /* LATIN SMALL LETTER T WITH CEDILLA	*/
				case 0x0164: str="T"	; break; /* LATIN CAPITAL LETTER T WITH CARON	*/
				case 0x0165: str="t"	; break; /* LATIN SMALL LETTER T WITH CARON	*/
				case 0x0166: str="T"	; break; /* LATIN CAPITAL LETTER T WITH STROKE	*/
				case 0x0167: str="t"	; break; /* LATIN SMALL LETTER T WITH STROKE	*/
				case 0x0168: str="~U"	; break; /* LATIN CAPITAL LETTER U WITH TILDE	*/
				case 0x0169: str="~u"	; break; /* LATIN SMALL LETTER U WITH TILDE	*/
				case 0x016A: str="U"	; break; /* LATIN CAPITAL LETTER U WITH MACRON	*/
				case 0x016B: str="u"	; break; /* LATIN SMALL LETTER U WITH MACRON	*/
				case 0x016C: str="U"	; break; /* LATIN CAPITAL LETTER U WITH BREVE	*/
				case 0x016D: str="u"	; break; /* LATIN SMALL LETTER U WITH BREVE	*/
				case 0x016E: str="U"	; break; /* LATIN CAPITAL LETTER U WITH RING ABOVE	*/
				case 0x016F: str="u"	; break; /* LATIN SMALL LETTER U WITH RING ABOVE	*/
				case 0x0170: str="\"U"	; break; /* LATIN CAPITAL LETTER U WITH DOUBLE ACUTE	*/
				case 0x0171: str="\"u"	; break; /* LATIN SMALL LETTER U WITH DOUBLE ACUTE	*/
				case 0x0172: str="U"	; break; /* LATIN CAPITAL LETTER U WITH OGONEK	*/
				case 0x0173: str="u"	; break; /* LATIN SMALL LETTER U WITH OGONEK	*/
				case 0x0174: str="^W"	; break; /* LATIN CAPITAL LETTER W WITH CIRCUMFLEX	*/
				case 0x0175: str="^w"	; break; /* LATIN SMALL LETTER W WITH CIRCUMFLEX	*/
				case 0x0176: str="^Y"	; break; /* LATIN CAPITAL LETTER Y WITH CIRCUMFLEX	*/
				case 0x0177: str="^y"	; break; /* LATIN SMALL LETTER Y WITH CIRCUMFLEX	*/
				case 0x0178: str="\"Y"	; break; /* LATIN CAPITAL LETTER Y WITH DIAERESIS	*/
				case 0x0179: str="´Z"	; break; /* LATIN CAPITAL LETTER Z WITH ACUTE	*/
				case 0x017A: str="´z"	; break; /* LATIN SMALL LETTER Z WITH ACUTE	*/
				case 0x017B: str="Z"	; break; /* LATIN CAPITAL LETTER Z WITH DOT ABOVE	*/
				case 0x017C: str="z"	; break; /* LATIN SMALL LETTER Z WITH DOT ABOVE	*/
				case 0x017D: str="Z"	; break; /* LATIN CAPITAL LETTER Z WITH CARON	*/
				case 0x017E: str="z"	; break; /* LATIN SMALL LETTER Z WITH CARON	*/
				case 0x017F: str="S"	; break; /* LATIN SMALL LETTER LONG S	*/
				case 0x018F: str=""	; break; /* LATIN CAPITAL LETTER SCHWA	*/
				case 0x0192: str="f"	; break; /* LATIN SMALL LETTER F WITH HOOK	*/
				case 0x0218: str="S"	; break; /* LATIN CAPITAL LETTER S WITH COMMA BELOW	*/
				case 0x0219: str="s"	; break; /* LATIN SMALL LETTER S WITH COMMA BELOW	*/
				case 0x021A: str="T"	; break; /* LATIN CAPITAL LETTER T WITH COMMA BELOW	*/
				case 0x021B: str="t"	; break; /* LATIN SMALL LETTER T WITH COMMA BELOW	*/
				case 0x0259: str=""	; break; /* LATIN SMALL LETTER SCHWA	*/
				case 0x02C6: str="^"	; break; /* MODIFIER LETTER CIRCUMFLEX ACCENT	*/
				case 0x02C7: str=""	; break; /* CARON	*/
				case 0x02D8: str=""	; break; /* BREVE	*/
				case 0x02D9: str=""	; break; /* DOT ABOVE	*/
				case 0x02DA: str=""	; break; /* RING ABOVE	*/
				case 0x02DB: str=""	; break; /* OGONEK	*/
				case 0x02DC: str="~"	; break; /* SMALL TILDE	*/
				case 0x02DD: str="\""	; break; /* DOUBLE ACUTE ACCENT	*/
				case 0x0374: str=""	; break; /* GREEK NUMERAL SIGN	*/
				case 0x0375: str=""	; break; /* GREEK LOWER NUMERAL SIGN	*/
				case 0x037A: str=""	; break; /* GREEK YPOGEGRAMMENI	*/
				case 0x037E: str=""	; break; /* GREEK QUESTION MARK	*/
				case 0x0384: str=""	; break; /* GREEK TONOS	*/
				case 0x0385: str=""	; break; /* GREEK DIALYTIKA TONOS	*/
				case 0x0386: str=""	; break; /* GREEK CAPITAL LETTER ALPHA WITH TONOS	*/
				case 0x0387: str=""	; break; /* GREEK ANO TELEIA	*/
				case 0x0388: str=""	; break; /* GREEK CAPITAL LETTER EPSILON WITH TONOS	*/
				case 0x0389: str=""	; break; /* GREEK CAPITAL LETTER ETA WITH TONOS	*/
				case 0x038A: str=""	; break; /* GREEK CAPITAL LETTER IOTA WITH TONOS	*/
				case 0x038C: str=""	; break; /* GREEK CAPITAL LETTER OMICRON WITH TONOS	*/
				case 0x038E: str=""	; break; /* GREEK CAPITAL LETTER UPSILON WITH TONOS	*/
				case 0x038F: str=""	; break; /* GREEK CAPITAL LETTER OMEGA WITH TONOS	*/
				case 0x0390: str=""	; break; /* GREEK SMALL LETTER IOTA WITH DIALYTIKA AND TONOS	*/
				case 0x0391: str=""	; break; /* GREEK CAPITAL LETTER ALPHA	*/
				case 0x0392: str=""	; break; /* GREEK CAPITAL LETTER BETA	*/
				case 0x0393: str=""	; break; /* GREEK CAPITAL LETTER GAMMA	*/
				case 0x0394: str=""	; break; /* GREEK CAPITAL LETTER DELTA	*/
				case 0x0395: str=""	; break; /* GREEK CAPITAL LETTER EPSILON	*/
				case 0x0396: str=""	; break; /* GREEK CAPITAL LETTER ZETA	*/
				case 0x0397: str=""	; break; /* GREEK CAPITAL LETTER ETA	*/
				case 0x0398: str=""	; break; /* GREEK CAPITAL LETTER THETA	*/
				case 0x0399: str=""	; break; /* GREEK CAPITAL LETTER IOTA	*/
				case 0x039A: str=""	; break; /* GREEK CAPITAL LETTER KAPPA	*/
				case 0x039B: str=""	; break; /* GREEK CAPITAL LETTER LAMDA	*/
				case 0x039C: str=""	; break; /* GREEK CAPITAL LETTER MU	*/
				case 0x039D: str=""	; break; /* GREEK CAPITAL LETTER NU	*/
				case 0x039E: str=""	; break; /* GREEK CAPITAL LETTER XI	*/
				case 0x039F: str=""	; break; /* GREEK CAPITAL LETTER OMICRON	*/
				case 0x03A0: str=""	; break; /* GREEK CAPITAL LETTER PI	*/
				case 0x03A1: str=""	; break; /* GREEK CAPITAL LETTER RHO	*/
				case 0x03A3: str=""	; break; /* GREEK CAPITAL LETTER SIGMA	*/
				case 0x03A4: str=""	; break; /* GREEK CAPITAL LETTER TAU	*/
				case 0x03A5: str=""	; break; /* GREEK CAPITAL LETTER UPSILON	*/
				case 0x03A6: str=""	; break; /* GREEK CAPITAL LETTER PHI	*/
				case 0x03A7: str=""	; break; /* GREEK CAPITAL LETTER CHI	*/
				case 0x03A8: str=""	; break; /* GREEK CAPITAL LETTER PSI	*/
				case 0x03A9: str=""	; break; /* GREEK CAPITAL LETTER OMEGA	*/
				case 0x03AA: str=""	; break; /* GREEK CAPITAL LETTER IOTA WITH DIALYTIKA	*/
				case 0x03AB: str=""	; break; /* GREEK CAPITAL LETTER UPSILON WITH DIALYTIKA	*/
				case 0x03AC: str=""	; break; /* GREEK SMALL LETTER ALPHA WITH TONOS	*/
				case 0x03AD: str=""	; break; /* GREEK SMALL LETTER EPSILON WITH TONOS	*/
				case 0x03AE: str=""	; break; /* GREEK SMALL LETTER ETA WITH TONOS	*/
				case 0x03AF: str=""	; break; /* GREEK SMALL LETTER IOTA WITH TONOS	*/
				case 0x03B0: str=""	; break; /* GREEK SMALL LETTER UPSILON WITH DIALYTIKA AND TONOS	*/
				case 0x03B1: str=""	; break; /* GREEK SMALL LETTER ALPHA	*/
				case 0x03B2: str=""	; break; /* GREEK SMALL LETTER BETA	*/
				case 0x03B3: str=""	; break; /* GREEK SMALL LETTER GAMMA	*/
				case 0x03B4: str=""	; break; /* GREEK SMALL LETTER DELTA	*/
				case 0x03B5: str=""	; break; /* GREEK SMALL LETTER EPSILON	*/
				case 0x03B6: str=""	; break; /* GREEK SMALL LETTER ZETA	*/
				case 0x03B7: str=""	; break; /* GREEK SMALL LETTER ETA	*/
				case 0x03B8: str=""	; break; /* GREEK SMALL LETTER THETA	*/
				case 0x03B9: str=""	; break; /* GREEK SMALL LETTER IOTA	*/
				case 0x03BA: str=""	; break; /* GREEK SMALL LETTER KAPPA	*/
				case 0x03BB: str=""	; break; /* GREEK SMALL LETTER LAMDA	*/
				case 0x03BC: str=""	; break; /* GREEK SMALL LETTER MU	*/
				case 0x03BD: str=""	; break; /* GREEK SMALL LETTER NU	*/
				case 0x03BE: str=""	; break; /* GREEK SMALL LETTER XI	*/
				case 0x03BF: str=""	; break; /* GREEK SMALL LETTER OMICRON	*/
				case 0x03C0: str=""	; break; /* GREEK SMALL LETTER PI	*/
				case 0x03C1: str=""	; break; /* GREEK SMALL LETTER RHO	*/
				case 0x03C2: str=""	; break; /* GREEK SMALL LETTER FINAL SIGMA	*/
				case 0x03C3: str=""	; break; /* GREEK SMALL LETTER SIGMA	*/
				case 0x03C4: str=""	; break; /* GREEK SMALL LETTER TAU	*/
				case 0x03C5: str=""	; break; /* GREEK SMALL LETTER UPSILON	*/
				case 0x03C6: str=""	; break; /* GREEK SMALL LETTER PHI	*/
				case 0x03C7: str=""	; break; /* GREEK SMALL LETTER CHI	*/
				case 0x03C8: str=""	; break; /* GREEK SMALL LETTER PSI	*/
				case 0x03C9: str=""	; break; /* GREEK SMALL LETTER OMEGA	*/
				case 0x03CA: str=""	; break; /* GREEK SMALL LETTER IOTA WITH DIALYTIKA	*/
				case 0x03CB: str=""	; break; /* GREEK SMALL LETTER UPSILON WITH DIALYTIKA	*/
				case 0x03CC: str=""	; break; /* GREEK SMALL LETTER OMICRON WITH TONOS	*/
				case 0x03CD: str=""	; break; /* GREEK SMALL LETTER UPSILON WITH TONOS	*/
				case 0x03CE: str=""	; break; /* GREEK SMALL LETTER OMEGA WITH TONOS	*/
				case 0x0401: str=""	; break; /* CYRILLIC CAPITAL LETTER IO	*/
				case 0x0402: str=""	; break; /* CYRILLIC CAPITAL LETTER DJE	*/
				case 0x0403: str=""	; break; /* CYRILLIC CAPITAL LETTER GJE	*/
				case 0x0404: str=""	; break; /* CYRILLIC CAPITAL LETTER UKRAINIAN IE	*/
				case 0x0405: str=""	; break; /* CYRILLIC CAPITAL LETTER DZE	*/
				case 0x0406: str=""	; break; /* CYRILLIC CAPITAL LETTER BYELORUSSIAN-UKRAINIAN I	*/
				case 0x0407: str=""	; break; /* CYRILLIC CAPITAL LETTER YI	*/
				case 0x0408: str=""	; break; /* CYRILLIC CAPITAL LETTER JE	*/
				case 0x0409: str=""	; break; /* CYRILLIC CAPITAL LETTER LJE	*/
				case 0x040A: str=""	; break; /* CYRILLIC CAPITAL LETTER NJE	*/
				case 0x040B: str=""	; break; /* CYRILLIC CAPITAL LETTER TSHE	*/
				case 0x040C: str=""	; break; /* CYRILLIC CAPITAL LETTER KJE	*/
				case 0x040E: str=""	; break; /* CYRILLIC CAPITAL LETTER SHORT U	*/
				case 0x040F: str=""	; break; /* CYRILLIC CAPITAL LETTER DZHE	*/
				case 0x0410: str=""	; break; /* CYRILLIC CAPITAL LETTER A	*/
				case 0x0411: str=""	; break; /* CYRILLIC CAPITAL LETTER BE	*/
				case 0x0412: str=""	; break; /* CYRILLIC CAPITAL LETTER VE	*/
				case 0x0413: str=""	; break; /* CYRILLIC CAPITAL LETTER GHE	*/
				case 0x0414: str=""	; break; /* CYRILLIC CAPITAL LETTER DE	*/
				case 0x0415: str=""	; break; /* CYRILLIC CAPITAL LETTER IE	*/
				case 0x0416: str=""	; break; /* CYRILLIC CAPITAL LETTER ZHE	*/
				case 0x0417: str=""	; break; /* CYRILLIC CAPITAL LETTER ZE	*/
				case 0x0418: str=""	; break; /* CYRILLIC CAPITAL LETTER I	*/
				case 0x0419: str=""	; break; /* CYRILLIC CAPITAL LETTER SHORT I	*/
				case 0x041A: str=""	; break; /* CYRILLIC CAPITAL LETTER KA	*/
				case 0x041B: str=""	; break; /* CYRILLIC CAPITAL LETTER EL	*/
				case 0x041C: str=""	; break; /* CYRILLIC CAPITAL LETTER EM	*/
				case 0x041D: str=""	; break; /* CYRILLIC CAPITAL LETTER EN	*/
				case 0x041E: str=""	; break; /* CYRILLIC CAPITAL LETTER O	*/
				case 0x041F: str=""	; break; /* CYRILLIC CAPITAL LETTER PE	*/
				case 0x0420: str=""	; break; /* CYRILLIC CAPITAL LETTER ER	*/
				case 0x0421: str=""	; break; /* CYRILLIC CAPITAL LETTER ES	*/
				case 0x0422: str=""	; break; /* CYRILLIC CAPITAL LETTER TE	*/
				case 0x0423: str=""	; break; /* CYRILLIC CAPITAL LETTER U	*/
				case 0x0424: str=""	; break; /* CYRILLIC CAPITAL LETTER EF	*/
				case 0x0425: str=""	; break; /* CYRILLIC CAPITAL LETTER HA	*/
				case 0x0426: str=""	; break; /* CYRILLIC CAPITAL LETTER TSE	*/
				case 0x0427: str=""	; break; /* CYRILLIC CAPITAL LETTER CHE	*/
				case 0x0428: str=""	; break; /* CYRILLIC CAPITAL LETTER SHA	*/
				case 0x0429: str=""	; break; /* CYRILLIC CAPITAL LETTER SHCHA	*/
				case 0x042A: str=""	; break; /* CYRILLIC CAPITAL LETTER HARD SIGN	*/
				case 0x042B: str=""	; break; /* CYRILLIC CAPITAL LETTER YERU	*/
				case 0x042C: str=""	; break; /* CYRILLIC CAPITAL LETTER SOFT SIGN	*/
				case 0x042D: str=""	; break; /* CYRILLIC CAPITAL LETTER E	*/
				case 0x042E: str=""	; break; /* CYRILLIC CAPITAL LETTER YU	*/
				case 0x042F: str=""	; break; /* CYRILLIC CAPITAL LETTER YA	*/
				case 0x0430: str=""	; break; /* CYRILLIC SMALL LETTER A	*/
				case 0x0431: str=""	; break; /* CYRILLIC SMALL LETTER BE	*/
				case 0x0432: str=""	; break; /* CYRILLIC SMALL LETTER VE	*/
				case 0x0433: str=""	; break; /* CYRILLIC SMALL LETTER GHE	*/
				case 0x0434: str=""	; break; /* CYRILLIC SMALL LETTER DE	*/
				case 0x0435: str=""	; break; /* CYRILLIC SMALL LETTER IE	*/
				case 0x0436: str=""	; break; /* CYRILLIC SMALL LETTER ZHE	*/
				case 0x0437: str=""	; break; /* CYRILLIC SMALL LETTER ZE	*/
				case 0x0438: str=""	; break; /* CYRILLIC SMALL LETTER I	*/
				case 0x0439: str=""	; break; /* CYRILLIC SMALL LETTER SHORT I	*/
				case 0x043A: str=""	; break; /* CYRILLIC SMALL LETTER KA	*/
				case 0x043B: str=""	; break; /* CYRILLIC SMALL LETTER EL	*/
				case 0x043C: str=""	; break; /* CYRILLIC SMALL LETTER EM	*/
				case 0x043D: str=""	; break; /* CYRILLIC SMALL LETTER EN	*/
				case 0x043E: str=""	; break; /* CYRILLIC SMALL LETTER O	*/
				case 0x043F: str=""	; break; /* CYRILLIC SMALL LETTER PE	*/
				case 0x0440: str=""	; break; /* CYRILLIC SMALL LETTER ER	*/
				case 0x0441: str=""	; break; /* CYRILLIC SMALL LETTER ES	*/
				case 0x0442: str=""	; break; /* CYRILLIC SMALL LETTER TE	*/
				case 0x0443: str=""	; break; /* CYRILLIC SMALL LETTER U	*/
				case 0x0444: str=""	; break; /* CYRILLIC SMALL LETTER EF	*/
				case 0x0445: str=""	; break; /* CYRILLIC SMALL LETTER HA	*/
				case 0x0446: str=""	; break; /* CYRILLIC SMALL LETTER TSE	*/
				case 0x0447: str=""	; break; /* CYRILLIC SMALL LETTER CHE	*/
				case 0x0448: str=""	; break; /* CYRILLIC SMALL LETTER SHA	*/
				case 0x0449: str=""	; break; /* CYRILLIC SMALL LETTER SHCHA	*/
				case 0x044A: str=""	; break; /* CYRILLIC SMALL LETTER HARD SIGN	*/
				case 0x044B: str=""	; break; /* CYRILLIC SMALL LETTER YERU	*/
				case 0x044C: str=""	; break; /* CYRILLIC SMALL LETTER SOFT SIGN	*/
				case 0x044D: str=""	; break; /* CYRILLIC SMALL LETTER E	*/
				case 0x044E: str=""	; break; /* CYRILLIC SMALL LETTER YU	*/
				case 0x044F: str=""	; break; /* CYRILLIC SMALL LETTER YA	*/
				case 0x0451: str=""	; break; /* CYRILLIC SMALL LETTER IO	*/
				case 0x0452: str=""	; break; /* CYRILLIC SMALL LETTER DJE	*/
				case 0x0453: str=""	; break; /* CYRILLIC SMALL LETTER GJE	*/
				case 0x0454: str=""	; break; /* CYRILLIC SMALL LETTER UKRAINIAN IE	*/
				case 0x0455: str=""	; break; /* CYRILLIC SMALL LETTER DZE	*/
				case 0x0456: str=""	; break; /* CYRILLIC SMALL LETTER BYELORUSSIAN-UKRAINIAN I	*/
				case 0x0457: str=""	; break; /* CYRILLIC SMALL LETTER YI	*/
				case 0x0458: str=""	; break; /* CYRILLIC SMALL LETTER JE	*/
				case 0x0459: str=""	; break; /* CYRILLIC SMALL LETTER LJE	*/
				case 0x045A: str=""	; break; /* CYRILLIC SMALL LETTER NJE	*/
				case 0x045B: str=""	; break; /* CYRILLIC SMALL LETTER TSHE	*/
				case 0x045C: str=""	; break; /* CYRILLIC SMALL LETTER KJE	*/
				case 0x045E: str=""	; break; /* CYRILLIC SMALL LETTER SHORT U	*/
				case 0x045F: str=""	; break; /* CYRILLIC SMALL LETTER DZHE	*/
				case 0x0490: str=""	; break; /* CYRILLIC CAPITAL LETTER GHE WITH UPTURN	*/
				case 0x0491: str=""	; break; /* CYRILLIC SMALL LETTER GHE WITH UPTURN	*/
				case 0x05D0: str=""	; break; /* HEBREW LETTER ALEF	*/
				case 0x05D1: str=""	; break; /* HEBREW LETTER BET	*/
				case 0x05D2: str=""	; break; /* HEBREW LETTER GIMEL	*/
				case 0x05D3: str=""	; break; /* HEBREW LETTER DALET	*/
				case 0x05D4: str=""	; break; /* HEBREW LETTER HE	*/
				case 0x05D5: str=""	; break; /* HEBREW LETTER VAV	*/
				case 0x05D6: str=""	; break; /* HEBREW LETTER ZAYIN	*/
				case 0x05D7: str=""	; break; /* HEBREW LETTER HET	*/
				case 0x05D8: str=""	; break; /* HEBREW LETTER TET	*/
				case 0x05D9: str=""	; break; /* HEBREW LETTER YOD	*/
				case 0x05DA: str=""	; break; /* HEBREW LETTER FINAL KAF	*/
				case 0x05DB: str=""	; break; /* HEBREW LETTER KAF	*/
				case 0x05DC: str=""	; break; /* HEBREW LETTER LAMED	*/
				case 0x05DD: str=""	; break; /* HEBREW LETTER FINAL MEM	*/
				case 0x05DE: str=""	; break; /* HEBREW LETTER MEM	*/
				case 0x05DF: str=""	; break; /* HEBREW LETTER FINAL NUN	*/
				case 0x05E0: str=""	; break; /* HEBREW LETTER NUN	*/
				case 0x05E1: str=""	; break; /* HEBREW LETTER SAMEKH	*/
				case 0x05E2: str=""	; break; /* HEBREW LETTER AYIN	*/
				case 0x05E3: str=""	; break; /* HEBREW LETTER FINAL PE	*/
				case 0x05E4: str=""	; break; /* HEBREW LETTER PE	*/
				case 0x05E5: str=""	; break; /* HEBREW LETTER FINAL TSADI	*/
				case 0x05E6: str=""	; break; /* HEBREW LETTER TSADI	*/
				case 0x05E7: str=""	; break; /* HEBREW LETTER QOF	*/
				case 0x05E8: str=""	; break; /* HEBREW LETTER RESH	*/
				case 0x05E9: str=""	; break; /* HEBREW LETTER SHIN	*/
				case 0x05EA: str=""	; break; /* HEBREW LETTER TAV	*/
				case 0x1E02: str="B"	; break; /* LATIN CAPITAL LETTER B WITH DOT ABOVE	*/
				case 0x1E03: str="b"	; break; /* LATIN SMALL LETTER B WITH DOT ABOVE	*/
				case 0x1E0A: str="D"	; break; /* LATIN CAPITAL LETTER D WITH DOT ABOVE	*/
				case 0x1E0B: str="d"	; break; /* LATIN SMALL LETTER D WITH DOT ABOVE	*/
				case 0x1E1E: str="F"	; break; /* LATIN CAPITAL LETTER F WITH DOT ABOVE	*/
				case 0x1E1F: str="f"	; break; /* LATIN SMALL LETTER F WITH DOT ABOVE	*/
				case 0x1E40: str="M"	; break; /* LATIN CAPITAL LETTER M WITH DOT ABOVE	*/
				case 0x1E41: str="m"	; break; /* LATIN SMALL LETTER M WITH DOT ABOVE	*/
				case 0x1E56: str="P"	; break; /* LATIN CAPITAL LETTER P WITH DOT ABOVE	*/
				case 0x1E57: str="p"	; break; /* LATIN SMALL LETTER P WITH DOT ABOVE	*/
				case 0x1E60: str="S"	; break; /* LATIN CAPITAL LETTER S WITH DOT ABOVE	*/
				case 0x1E61: str="s"	; break; /* LATIN SMALL LETTER S WITH DOT ABOVE	*/
				case 0x1E6A: str="T"	; break; /* LATIN CAPITAL LETTER T WITH DOT ABOVE	*/
				case 0x1E6B: str="t"	; break; /* LATIN SMALL LETTER T WITH DOT ABOVE	*/
				case 0x1E80: str="`W"	; break; /* LATIN CAPITAL LETTER W WITH GRAVE	*/
				case 0x1E81: str="`w"	; break; /* LATIN SMALL LETTER W WITH GRAVE	*/
				case 0x1E82: str="´W"	; break; /* LATIN CAPITAL LETTER W WITH ACUTE	*/
				case 0x1E83: str="´w"	; break; /* LATIN SMALL LETTER W WITH ACUTE	*/
				case 0x1E84: str="\"W"	; break; /* LATIN CAPITAL LETTER W WITH DIAERESIS	*/
				case 0x1E85: str="\"w"	; break; /* LATIN SMALL LETTER W WITH DIAERESIS	*/
				case 0x1EF2: str="`Y"	; break; /* LATIN CAPITAL LETTER Y WITH GRAVE	*/
				case 0x1EF3: str="`y"	; break; /* LATIN SMALL LETTER Y WITH GRAVE	*/
				case 0x2010: str="-"	; break; /* HYPHEN	*/
				case 0x2011: str="-"	; break; /* NON-BREAKING HYPHEN	*/
				case 0x2012: str="-"	; break; /* FIGURE DASH	*/
				case 0x2013: str="-"	; break; /* EN DASH	*/
				case 0x2014: str="-"	; break; /* EM DASH	*/
				case 0x2015: str="-"	; break; /* HORIZONTAL BAR	*/
				case 0x2016: str="||"	; break; /* DOUBLE VERTICAL LINE	*/
				case 0x2017: str=""	; break; /* DOUBLE LOW LINE	*/
				case 0x2018: str="`"	; break; /* LEFT SINGLE QUOTATION MARK	*/
				case 0x2019: str="\'"	; break; /* RIGHT SINGLE QUOTATION MARK	*/
				case 0x201A: str="\'"	; break; /* SINGLE LOW-9 QUOTATION MARK	*/
				case 0x201B: str="\'"	; break; /* SINGLE HIGH-REVERSED-9 QUOTATION MARK	*/
				case 0x201C: str="\""	; break; /* LEFT DOUBLE QUOTATION MARK	*/
				case 0x201D: str="\""	; break; /* RIGHT DOUBLE QUOTATION MARK	*/
				case 0x201E: str="\""	; break; /* DOUBLE LOW-9 QUOTATION MARK	*/
				case 0x201F: str="\""	; break; /* DOUBLE HIGH-REVERSED-9 QUOTATION MARK	*/
				case 0x2020: str="+"	; break; /* DAGGER	*/
				case 0x2021: str=""	; break; /* DOUBLE DAGGER	*/
				case 0x2022: str="o"	; break; /* BULLET	*/
				case 0x2026: str="..."	; break; /* HORIZONTAL ELLIPSIS	*/
				case 0x2030: str="o/oo"	; break; /* PER MILLE SIGN	*/
				case 0x2032: str="´"	; break; /* PRIME	*/
				case 0x2033: str="´´"	; break; /* DOUBLE PRIME	*/
				case 0x2034: str="´´´"	; break; /* TRIPLE PRIME	*/
				case 0x2039: str="<"	; break; /* SINGLE LEFT-POINTING ANGLE QUOTATION MARK	*/
				case 0x203A: str=">"	; break; /* SINGLE RIGHT-POINTING ANGLE QUOTATION MARK	*/
				case 0x203E: str=""	; break; /* OVERLINE	*/
				case 0x20AC: str="EUR"	; break; /* EURO SIGN	*/
				case 0x2116: str="No."	; break; /* NUMERO SIGN	*/
				case 0x2122: str="TM"	; break; /* TRADE MARK SIGN	*/
				case 0x2126: str="Ohm"	; break; /* OHM SIGN	*/
				case 0x215B: str="1/8"	; break; /* VULGAR FRACTION ONE EIGHTH	*/
				case 0x215C: str="3/8"	; break; /* VULGAR FRACTION THREE EIGHTHS	*/
				case 0x215D: str="5/8"	; break; /* VULGAR FRACTION FIVE EIGHTHS	*/
				case 0x215E: str="7/8"	; break; /* VULGAR FRACTION SEVEN EIGHTHS	*/
				case 0x2190: str="<-"	; break; /* LEFTWARDS ARROW	*/
				case 0x2191: str="^"	; break; /* UPWARDS ARROW	*/
				case 0x2192: str="->"	; break; /* RIGHTWARDS ARROW	*/
				case 0x2193: str="V"	; break; /* DOWNWARDS ARROW	*/
				case 0x21D0: str="<="	; break; /* LEFTWARDS DOUBLE ARROW	*/
				case 0x21D2: str="=>"	; break; /* RIGHTWARDS DOUBLE ARROW	*/
				case 0x2212: str="-"	; break; /* MINUS SIGN	*/
				case 0x2215: str="/"	; break; /* DIVISION SLASH	*/
				case 0x2260: str="/="	; break; /* NOT EQUAL TO	*/
				case 0x2264: str="<="	; break; /* LESS-THAN OR EQUAL TO	*/
				case 0x2265: str=">="	; break; /* GREATER-THAN OR EQUAL TO	*/
				case 0x226A: str="<<"	; break; /* MUCH LESS-THAN	*/
				case 0x226B: str=">>"	; break; /* MUCH GREATER-THAN	*/
				case 0x2409: str=""	; break; /* SYMBOL FOR HORIZONTAL TABULATION	*/
				case 0x240A: str=""	; break; /* SYMBOL FOR LINE FEED	*/
				case 0x240B: str=""	; break; /* SYMBOL FOR VERTICAL TABULATION	*/
				case 0x240C: str=""	; break; /* SYMBOL FOR FORM FEED	*/
				case 0x240D: str=""	; break; /* SYMBOL FOR CARRIAGE RETURN	*/
				case 0x2424: str=""	; break; /* SYMBOL FOR NEWLINE	*/
				case 0x2500: str="-"	; break; /* BOX DRAWINGS LIGHT HORIZONTAL	*/
				case 0x2502: str="|"	; break; /* BOX DRAWINGS LIGHT VERTICAL	*/
				case 0x250C: str="+"	; break; /* BOX DRAWINGS LIGHT DOWN AND RIGHT	*/
				case 0x2510: str="+"	; break; /* BOX DRAWINGS LIGHT DOWN AND LEFT	*/
				case 0x2514: str="+"	; break; /* BOX DRAWINGS LIGHT UP AND RIGHT	*/
				case 0x2518: str="+"	; break; /* BOX DRAWINGS LIGHT UP AND LEFT	*/
				case 0x251C: str="+"	; break; /* BOX DRAWINGS LIGHT VERTICAL AND RIGHT	*/
				case 0x2524: str="+"	; break; /* BOX DRAWINGS LIGHT VERTICAL AND LEFT	*/
				case 0x252C: str="+"	; break; /* BOX DRAWINGS LIGHT DOWN AND HORIZONTAL	*/
				case 0x2534: str="+"	; break; /* BOX DRAWINGS LIGHT UP AND HORIZONTAL	*/
				case 0x253C: str="+"	; break; /* BOX DRAWINGS LIGHT VERTICAL AND HORIZONTAL	*/
				case 0x2592: str=""	; break; /* MEDIUM SHADE	*/
				case 0x25AE: str=""	; break; /* BLACK VERTICAL RECTANGLE	*/
				case 0x25C6: str=""	; break; /* BLACK DIAMOND	*/
				case 0x266A: str=""	; break; /* EIGHTH NOTE	*/
				default: str="?";
			}
			strcpy (outbuf, str);
			outbuf += strlen (str);
		}
	}

	excel_iconv_close(cd);
	return outbuf - outbuf_orig;
}
