/**
 * ms-excel.c: MS Excel support for Gnumeric
 *
 * Authors:
 *    Michael Meeks (michael@imaginator.com)
 *    Jody Goldberg (jgoldberg@home.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks, Jody Goldberg
 **/

#include <config.h>
#include "command-context.h"

#include "ms-formula-read.h"
#include "ms-excel-read.h"
#include "ms-obj.h"
#include "ms-chart.h"
#include "gnumeric-chart.h"
#include "ms-escher.h"
#include "print-info.h"
#include "selection.h"
#include "utils.h"	/* for cell_name */
#include "ranges.h"
#include "expr-name.h"
#include "style.h"
#include "application.h"
#include "workbook.h"
#include "ms-excel-util.h"
#include "ms-excel-xf.h"
#include "workbook-view.h"

/* #define NO_DEBUG_EXCEL */

/* Used in src/main.c to toggle debug messages on & off */
/*
 * As a convention
 * 0 = quiet, no experimental features.
 * 1 = enable experimental features
 * >1 increasing levels of detail.
 */
int ms_excel_read_debug    = 0;
int ms_excel_write_debug   = 0;
int ms_excel_formula_debug = 0;
int ms_excel_color_debug   = 0;
int ms_excel_chart_debug   = 0;
extern int gnumeric_debugging;

/* Forward references */
static ExcelSheet *ms_excel_sheet_new       (ExcelWorkbook *wb,
					     const char *name);
static void        ms_excel_workbook_attach (ExcelWorkbook *wb,
					     ExcelSheet *ans);

void
ms_excel_unexpected_biff (BiffQuery *q, char const *const state)
{
#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0) {
		printf ("Unexpected Opcode in %s : 0x%x, length 0x%x\n",
			state, q->opcode, q->length);
		if (ms_excel_read_debug > 2)
			ms_ole_dump (q->data, q->length);
	}
#endif
}


/**
 * Generic 16 bit int index pointer functions.
 **/
static guint
biff_guint16_hash (const guint16 *d)
{ return *d*2; }
static guint
biff_guint32_hash (const guint32 *d)
{ return *d*2; }

static gint
biff_guint16_equal (const guint16 *a, const guint16 *b)
{
	if (*a == *b) return 1;
	return 0;
}
static gint
biff_guint32_equal (const guint32 *a, const guint32 *b)
{
	if (*a == *b) return 1;
	return 0;
}

/**
 * This returns whether there is a header byte
 * and sets various flags from it
 **/
static gboolean
biff_string_get_flags (guint8 const *ptr,
		       gboolean *word_chars,
		       gboolean *extended,
		       gboolean *rich)
{
	guint8 header;

	header = MS_OLE_GET_GUINT8(ptr);
	/* I assume that this header is backwards compatible with raw ASCII */

	/* Its a proper Unicode header grbit byte */
	if (((header & 0xf2) == 0)) {
		*word_chars = (header & 0x1) != 0;
		*extended   = (header & 0x4) != 0;
		*rich       = (header & 0x8) != 0;
		return TRUE;
	} else { /* Some assumptions: FIXME ? */
		*word_chars = 0;
		*extended   = 0;
		*rich       = 0;
		return FALSE;
	}
}

/**
 *  This function takes a length argument as Biff V7 has a byte length
 * ( seemingly ).
 * it returns the length in bytes of the string in byte_length
 * or nothing if this is NULL.
 *  FIXME: see S59D47.HTM for full description
 **/
char *
biff_get_text (guint8 const *pos, guint32 length, guint32 *byte_length)
{
	guint32 lp;
	char *ans;
	guint8 const *ptr;
	guint32 byte_len;
	gboolean header;
	gboolean high_byte;
	static gboolean high_byte_warned = FALSE;
	gboolean ext_str;
	gboolean rich_str;

	if (!byte_length)
		byte_length = &byte_len;
	*byte_length = 0;

	if (!length)
	{
		/* FIXME FIXME FIXME : What about the 1 byte for the header ?
		 *                     The length may be wrong in this case.
		 */
		return 0;
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		printf ("String :\n");
		ms_ole_dump (pos, length+1);
	}
#endif

	ans = (char *) g_new (char, length + 2);

	header = biff_string_get_flags (pos,
					&high_byte,
					&ext_str,
					&rich_str);
	if (header) {
		ptr = pos + 1;
		(*byte_length)++;
	} else
		ptr = pos;

	/* A few friendly warnings */
	if (high_byte && !high_byte_warned) {
		printf ("FIXME: unicode support unimplemented: truncating\n");
		high_byte_warned = TRUE;
	}
	if (rich_str) { /* The data for this appears after the string */
		guint16 formatting_runs = MS_OLE_GET_GUINT16(ptr);
		static int warned = FALSE;

		(*byte_length) += 2;
		(*byte_length) += formatting_runs*4; /* 4 bytes per */
		ptr+= 2;

		if (!warned)
			printf ("FIXME: rich string support unimplemented:"
				"discarding %d runs\n", formatting_runs);
		warned = TRUE;
	}
	if (ext_str) { /* NB this data always comes after the rich_str data */
		guint32 len_ext_rst = MS_OLE_GET_GUINT32(ptr); /* A byte length */
		static int warned = FALSE;

		(*byte_length) += 4 + len_ext_rst;
		ptr+= 4;

		if (!warned)
			printf ("FIXME: extended string support unimplemented:"
				"ignoring %d bytes\n", len_ext_rst);
		warned = TRUE;
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 4) {
		printf ("String len %d, byte length %d: %d %d %d:\n",
			length, (*byte_length), high_byte, rich_str, ext_str);
		ms_ole_dump (pos, *byte_length);
	}
#endif

	for (lp = 0; lp < length; lp++) {
		guint16 c;
		guint8  header;
		if (((header = MS_OLE_GET_GUINT8 (ptr)) & 0xf2) == 0) {
			static int already_warned = FALSE;
			high_byte  = (header & 0x1) != 0;
			ext_str    = (header & 0x4) != 0;
			rich_str   = (header & 0x8) != 0;
#if 0
			if (rich_str || ext_str)
				g_warning ("Panic: ahhh... sill string");
#endif
			/* This can wait until the big unicode clean ;-) */
			if (!already_warned) {
				g_warning ("EXCEL: we need to re-architecture string reading to support unicode & rich text.");
				already_warned = TRUE;
			}
			ptr+=1;
			lp--;
			(*byte_length) += 1;
		} else if (high_byte) {
			c = MS_OLE_GET_GUINT16 (ptr);
			ptr+=2;
			ans[lp] = (char)c;
			(*byte_length) += 2;
		} else {
			c = MS_OLE_GET_GUINT8 (ptr);
			ptr+=1;
			ans[lp] = (char)c;
			(*byte_length) += 1;
		}
	}
	if (lp > 0)
		ans[lp] = 0;
	else
		g_warning ("Warning unterminated string floating");
	return ans;
}

char const *
biff_get_error_text (const guint8 err)
{
	char const *buf;
	switch (err)
	{
	case 0:  buf = gnumeric_err_NULL;  break;
	case 7:  buf = gnumeric_err_DIV0;  break;
	case 15: buf = gnumeric_err_VALUE; break;
	case 23: buf = gnumeric_err_REF;   break;
	case 29: buf = gnumeric_err_NAME;  break;
	case 36: buf = gnumeric_err_NUM;   break;
	case 42: buf = gnumeric_err_NA;    break;
	default:
		buf = _("#UNKNOWN!"); break;
	}
	return buf;
}

/**
 * See S59D5D.HTM
 **/
BIFF_BOF_DATA *
ms_biff_bof_data_new (BiffQuery *q)
{
	BIFF_BOF_DATA *ans = g_new (BIFF_BOF_DATA, 1);

	if ((q->opcode & 0xff) == BIFF_BOF &&
	    (q->length >= 4)) {
		/*
		 * Determine type from boff
		 */
		switch (q->opcode >> 8) {
		case 0:
			ans->version = eBiffV2;
			break;
		case 2:
			ans->version = eBiffV3;
			break;
		case 4:
			ans->version = eBiffV4;
			break;
		case 8:	/*
			 * More complicated
			 */
			{
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_read_debug > 2) {
					printf ("Complicated BIFF version %d\n",
						MS_OLE_GET_GUINT16 (q->data));
					ms_ole_dump (q->data, q->length);
				}
#endif
				switch (MS_OLE_GET_GUINT16 (q->data))
				{
				case 0x0600:
					ans->version = eBiffV8;
					break;
				case 0x500:
					ans->version = eBiffV7;		/*
									 * OR ebiff7 : FIXME ? !
									 */
					break;
				default:
					printf ("Unknown BIFF sub-number in BOF %x\n", q->opcode);
					ans->version = eBiffVUnknown;
				}
			}
			break;
		default:
			printf ("Unknown BIFF number in BOF %x\n", q->opcode);
			ans->version = eBiffVUnknown;
			printf ("Biff version %d\n", ans->version);
		}
		switch (MS_OLE_GET_GUINT16 (q->data + 2)) {
		case 0x0005:
			ans->type = eBiffTWorkbook;
			break;
		case 0x0006:
			ans->type = eBiffTVBModule;
			break;
		case 0x0010:
			ans->type = eBiffTWorksheet;
			break;
		case 0x0020:
			ans->type = eBiffTChart;
			break;
		case 0x0040:
			ans->type = eBiffTMacrosheet;
			break;
		case 0x0100:
			ans->type = eBiffTWorkspace;
			break;
		default:
			ans->type = eBiffTUnknown;
			printf ("Unknown BIFF type in BOF %x\n", MS_OLE_GET_GUINT16 (q->data + 2));
			break;
		}
		/*
		 * Now store in the directory array:
		 */
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 2) {
			printf ("BOF %x, %d == %d, %d\n", q->opcode, q->length,
				ans->version, ans->type);
		}
#endif
	} else {
		printf ("Not a BOF !\n");
		ans->version = eBiffVUnknown;
		ans->type = eBiffTUnknown;
	}

	return ans;
}

void
ms_biff_bof_data_destroy (BIFF_BOF_DATA *data)
{
	g_free (data);
}

/**
 * See S59D61.HTM
 **/
static void
biff_boundsheet_data_new (ExcelWorkbook *wb, BiffQuery *q, eBiff_version ver)
{
	BiffBoundsheetData *ans = g_new (BiffBoundsheetData, 1);

	if (ver != eBiffV5 &&	/*
				 * Testing seems to indicate that Biff5 is compatibile with Biff7 here.
				 */
	    ver != eBiffV7 &&
	    ver != eBiffV8) {
		printf ("Unknown BIFF Boundsheet spec. Assuming same as Biff7 FIXME\n");
		ver = eBiffV7;
	}
	ans->streamStartPos = MS_OLE_GET_GUINT32 (q->data);
	switch (MS_OLE_GET_GUINT8 (q->data + 4)) {
	case 0:
		ans->type = eBiffTWorksheet;
		break;
	case 1:
		ans->type = eBiffTMacrosheet;
		break;
	case 2:
		ans->type = eBiffTChart;
		break;
	case 6:
		ans->type = eBiffTVBModule;
		break;
	default:
		printf ("Unknown sheet type : %d\n", MS_OLE_GET_GUINT8 (q->data + 4));
		ans->type = eBiffTUnknown;
		break;
	}
	switch ((MS_OLE_GET_GUINT8 (q->data + 5)) & 0x3) {
	case 00:
		ans->hidden = eBiffHVisible;
		break;
	case 01:
		ans->hidden = eBiffHHidden;
		break;
	case 02:
		ans->hidden = eBiffHVeryHidden;
		break;
	default:
		printf ("Unknown sheet hiddenness %d\n", (MS_OLE_GET_GUINT8 (q->data + 4)) & 0x3);
		ans->hidden = eBiffHVisible;
		break;
	}
	if (ver == eBiffV8) {
		int slen = MS_OLE_GET_GUINT16 (q->data + 6);
		ans->name = biff_get_text (q->data + 8, slen, NULL);
	} else {
		int slen = MS_OLE_GET_GUINT8 (q->data + 6);

		ans->name = biff_get_text (q->data + 7, slen, NULL);
	}

	/*
	 * printf ("Blocksheet : '%s', %d:%d offset %lx\n", ans->name, ans->type, ans->hidden, ans->streamStartPos);
	 */
	ans->index = (guint16)g_hash_table_size (wb->boundsheet_data_by_index);
	g_hash_table_insert (wb->boundsheet_data_by_index,
			     &ans->index, ans);
	g_hash_table_insert (wb->boundsheet_data_by_stream,
			     &ans->streamStartPos, ans);

	g_assert (ans->streamStartPos == MS_OLE_GET_GUINT32 (q->data));
	ans->sheet = ms_excel_sheet_new (wb, ans->name);
	ms_excel_workbook_attach (wb, ans->sheet);
}

static gboolean
biff_boundsheet_data_destroy (gpointer key, BiffBoundsheetData *d, gpointer userdata)
{
	g_free (d->name);
	g_free (d);
	return 1;
}

/**
 * NB. 'fount' is the correct, and original _English_
 **/
static void
biff_font_data_new (ExcelWorkbook *wb, BiffQuery *q)
{
	BiffFontData *fd = g_new (BiffFontData, 1);
	guint16 data;
	guint8 data1;

	fd->height = MS_OLE_GET_GUINT16 (q->data + 0);
	data = MS_OLE_GET_GUINT16 (q->data + 2);
	fd->italic     = (data & 0x2) == 0x2;
	fd->struck_out = (data & 0x8) == 0x8;
	fd->color_idx  = MS_OLE_GET_GUINT16 (q->data + 4);
	fd->color_idx &= 0x7f; /* Undocumented but a good idea */
	fd->boldness   = MS_OLE_GET_GUINT16 (q->data + 6);
	data = MS_OLE_GET_GUINT16 (q->data + 8);
	switch (data) {
	case 0:
		fd->script = eBiffFSNone;
		break;
	case 1:
		fd->script = eBiffFSSuper;
		break;
	case 2:
		fd->script = eBiffFSSub;
		break;
	default:
		printf ("Unknown script %d\n", data);
		break;
	}

	data1 = MS_OLE_GET_GUINT8 (q->data + 10);
	switch (data1) {
	case 0:
		fd->underline = eBiffFUNone;
		break;
	case 1:
		fd->underline = eBiffFUSingle;
		break;
	case 2:
		fd->underline = eBiffFUDouble;
		break;
	case 0x21:
		fd->underline = eBiffFUSingleAcc;
		break;
	case 0x22:
		fd->underline = eBiffFUDoubleAcc;
		break;
	}
	fd->fontname = biff_get_text (q->data + 15,
				      MS_OLE_GET_GUINT8 (q->data + 14), NULL);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		printf ("Insert font '%s' size %d pts color %d\n",
			fd->fontname, fd->height / 20, fd->color_idx);
	}
#endif
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_color_debug > 3) {
			printf ("Font color = 0x%x\n", fd->color_idx);
		}
#endif
        fd->index = g_hash_table_size (wb->font_data);
	if (fd->index >= 4) /* Wierd: for backwards compatibility */
		fd->index++;
	g_hash_table_insert (wb->font_data, &fd->index, fd);
}

static gboolean
biff_font_data_destroy (gpointer key, BiffFontData *fd, gpointer userdata)
{
	g_free (fd->fontname);
	g_free (fd);
	return 1;
}

char *excel_builtin_formats[EXCEL_BUILTIN_FORMAT_LEN] = {
/* 0x00 */	"", /* General */
/* 0x01 */	"0",
/* 0x02 */	"0.00",
/* 0x03 */	"#,##0",
/* 0x04 */	"#,##0.00",
/* 0x05 */	"$#,##0_);($#,##0)",
/* 0x06 */	"$#,##0_);[Red]($#,##0)",
/* 0x07 */	"$#,##0.00_);($#,##0.00)",
/* 0x08 */	"$#,##0.00_);[Red]($#,##0.00)",
/* 0x09 */	"0%",
/* 0x0a */	"0.00%",
/* 0x0b */	"0.00E+00",
/* 0x0c */	"#",
/* 0x0d */	"#",
/* 0x0e */	"m/d/yy",
/* 0x0f */	"d-mmm-yy",
/* 0x10 */	"d-mmm",
/* 0x11 */	"mmm-yy",
/* 0x12 */	"h:mm",
/* 0x13 */	"h:mm:ss",
/* 0x14 */	"h:mm",
/* 0x15 */	"h:mm:ss",
/* 0x16 */	"m/d/yy",
	0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x17-0x24 reserved for intl versions */
/* 0x25 */	"(#,##0_);(#,##0)",
/* 0x26 */	"(#,##0_);[Red](#,##0)",
/* 0x27 */	"(#,##0.00_);(#,##0.00)",
/* 0x28 */	"(#,##0.00_);[Red](#,##0.00)",
/* 0x29 */	"_(* #,##0_);_(* (#,##0);_(* \"-\"_);_(@_)",
/* 0x2a */	"_($* #,##0_);_($* (#,##0);_($* \"-\"_);_(@_)",
/* 0x2b */	"_(* #,##0.00_);_(* (#,##0.00);_(* \"-\"??_);_(@_)",
/* 0x2c */	"_($* #,##0.00_);_($* (#,##0.00);_($* \"-\"??_);_(@_)",
/* 0x2d */	"mm:ss",
/* 0x2e */	"[h]:mm:ss",
/* 0x2f */	"mm:ss.0",
/* 0x30 */	"##0.0E+0",
/* 0x31 */	"@"
};

/*
 * FIXME: This code falsely assumes that the builtin formats are
 * fixed. The builtins get translated to local currency formats. E.g.
 * Format data : 0x05 == '"kr"\ #,##0;"kr"\ \-#,##0'
*/
StyleFormat *
biff_format_data_lookup (ExcelWorkbook *wb, guint16 idx)
{
	char *ans = NULL;
	if (idx <= 0x31) {
		ans = excel_builtin_formats[idx];
		if (!ans)
			printf ("Foreign undocumented format\n");
	}

	if (!ans) {
		BiffFormatData *d = g_hash_table_lookup (wb->format_data,
							   &idx);
		if (!d) {
			printf ("Unknown format: 0x%x\n", idx);
			ans = 0;
		} else
			ans = d->name;
	}
	if (ans)
		return style_format_new (ans);
	else
		return NULL;
}

static gboolean
biff_format_data_destroy (gpointer key, BiffFormatData *d, gpointer userdata)
{
	g_free (d->name);
	g_free (d);
	return 1;
}

typedef struct {
	char const *name;
	gboolean    sheet_scope;
	gboolean    inserted;
	enum { BNDStore, BNDName } type;
	union {
		ExprName *name;
		struct {
			guint8   *data;
			guint16   len;
		} store;
	} v;
} BiffNameData;

static int externsheet = 0;

/*
 * We must not try and parse the data until we have
 * read all the sheets in ( for inter-sheet references in names ).
 */
static void
biff_name_data_new (ExcelWorkbook *wb, char const *name,
		    guint16 const sheet_index,
		    guint8 const *formula, guint16 const len,
		    gboolean const external,
		    gboolean const sheet_scope)
{
	BiffNameData *bnd = g_new (BiffNameData, 1);
	bnd->name        = name;
	bnd->sheet_scope = sheet_scope;
	bnd->type        = BNDStore;
	if (formula) {
		bnd->v.store.data = g_malloc (len);
		memcpy (bnd->v.store.data, formula, len);
		bnd->v.store.len  = len;
	} else {
		bnd->v.store.data = NULL;
		bnd->v.store.len  = 0;
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		printf ("%s : %x %x sheet=%d '%s'\n",
			external ? "EXTERNNAME" : "NAME",
			externsheet,
			wb->name_data->len, sheet_index, bnd->name);
	}
	if (ms_excel_read_debug > 2)
		ms_ole_dump (bnd->v.store.data, bnd->v.store.len);
#endif
	g_ptr_array_add (wb->name_data, bnd);
}

ExprTree *
biff_name_data_get_name (ExcelSheet *sheet, int idx)
{
	BiffNameData *bnd;
	GPtrArray    *a;

	g_return_val_if_fail (sheet, NULL);
	g_return_val_if_fail (sheet->wb, NULL);

	a = sheet->wb->name_data;

	if (a == NULL || idx < 0 || a->len <= idx ||
	    (bnd = g_ptr_array_index (a, idx)) == NULL) {
		g_warning ("EXCEL : %x (of %x) UNKNOWN name\n", idx, a->len);
		return expr_tree_new_constant(value_new_string("Unknown name"));

	}

	if (bnd->type == BNDStore && bnd->v.store.data) {
		char     *duff = "Some Error";
		ExprTree *tree = ms_excel_parse_formula (sheet->wb, sheet,
							 bnd->v.store.data,
							 0, 0, FALSE,
							 bnd->v.store.len,
							 NULL);

		if (!tree) { /* OK so it's a special 'AddIn' name */
			bnd->type   = BNDName;
			g_free (bnd->v.store.data);
			bnd->v.name = NULL;
		} else {
			bnd->type = BNDName;
			g_free (bnd->v.store.data);
			if (bnd->sheet_scope)
				bnd->v.name = expr_name_add (NULL, sheet->gnum_sheet,
							     bnd->name,
							     tree, &duff);
			else
				bnd->v.name = expr_name_add (sheet->wb->gnum_wb, NULL,
							     bnd->name,
							     tree, &duff);
			if (!bnd->v.name)
				printf ("Error: '%s' on name '%s'\n", duff,
					bnd->name);
#ifndef NO_DEBUG_EXCEL
			else if (ms_excel_read_debug > 1) {
				ParsePosition ep;
				parse_pos_init (&ep, sheet->wb->gnum_wb, 0, 0);
				printf ("Parsed name : '%s' = '%s'\n",
					bnd->name, tree
					? expr_decode_tree (tree, &ep)
					: "error");
			}
#endif
		}
	}
	bnd->inserted = TRUE;
	if (bnd->type == BNDName && bnd->v.name)
		return expr_tree_new_name (bnd->v.name);
	else
		return expr_tree_new_constant (value_new_string (bnd->name));
}

static void
biff_name_data_destroy (BiffNameData *bnd)
{
	g_return_if_fail (bnd);

	if (bnd->name)
		g_free ((char *)bnd->name);
	bnd->name    = NULL;
	if (bnd->type == BNDStore) {
		if (bnd->v.store.data)
			g_free (bnd->v.store.data);
	} /* else: bnd->v.name is held in the sheet */
	g_free (bnd);
}

EXCEL_PALETTE_ENTRY const excel_default_palette[EXCEL_DEF_PAL_LEN] = {
/* These were generated by creating a sheet and
 * modifying the 1st color cell and saving.  This
 * created a custom palette.  I then loaded the sheet
 * into gnumeric and dumped the results.
 */
	{  0,  0,  0}, {255,255,255},  {255,  0,  0},  {  0,255,  0},
	{  0,  0,255}, {255,255,  0},  {255,  0,255},  {  0,255,255},

	{128,  0,  0}, {  0,128,  0},  {  0,  0,128},  {128,128,  0},
	{128,  0,128}, {  0,128,128},  {192,192,192},  {128,128,128},

	{153,153,255}, {153, 51,102},  {255,255,204},  {204,255,255},
	{102,  0,102}, {255,128,128},  {  0,102,204},  {204,204,255},

	{  0,  0,128}, {255,  0,255},  {255,255,  0},  {  0,255,255},
	{128,  0,128}, {128,  0,  0},  {  0,128,128},  {  0,  0,255},

	{  0,204,255}, {204,255,255},  {204,255,204},  {255,255,153},
	{153,204,255}, {255,153,204},  {204,153,255},  {255,204,153},

	{ 51,102,255}, { 51,204,204},  {153,204,  0},  {255,204,  0},
	{255,153,  0}, {255,102,  0},  {102,102,153},  {150,150,150},

	{  0, 51,102}, { 51,153,102},  {  0, 51,  0},  { 51, 51,  0},
	{153, 51,  0}, {153, 51,102},  { 51, 51,153},  { 51, 51, 51}

};

static ExcelPalette *
ms_excel_default_palette ()
{
	static ExcelPalette *pal = NULL;

	if (!pal)
	{
		int entries = EXCEL_DEF_PAL_LEN;
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_color_debug > 3) {
			printf ("Creating default palette\n");
		}
#endif
		pal = (ExcelPalette *) g_malloc (sizeof (ExcelPalette));
		pal->length = entries;
		pal->red   = g_new (int, entries);
		pal->green = g_new (int, entries);
		pal->blue  = g_new (int, entries);
		pal->gnum_cols = g_new (StyleColor *, entries);

		while (--entries >= 0) {
			pal->red[entries]   = excel_default_palette[entries].r;
			pal->green[entries] = excel_default_palette[entries].g;
			pal->blue[entries]  = excel_default_palette[entries].b;
			pal->gnum_cols[entries] = NULL;
		}
	}

	return pal;
}

/* See: S59DC9.HTM */
static ExcelPalette *
ms_excel_palette_new (BiffQuery *q)
{
	int lp, len;
	ExcelPalette *pal;

	pal = (ExcelPalette *) g_malloc (sizeof (ExcelPalette));
	len = MS_OLE_GET_GUINT16 (q->data);
	pal->length = len;
	pal->red = g_new (int, len);
	pal->green = g_new (int, len);
	pal->blue = g_new (int, len);
	pal->gnum_cols = g_new (StyleColor *, len);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_color_debug > 3) {
		printf ("New palette with %d entries\n", len);
	}
#endif
	for (lp = 0; lp < len; lp++) {
		guint32 num = MS_OLE_GET_GUINT32 (q->data + 2 + lp * 4);

		/* NOTE the order of bytes is different from what one would
		 * expect */
		pal->blue[lp] = (num & 0x00ff0000) >> 16;
		pal->green[lp] = (num & 0x0000ff00) >> 8;
		pal->red[lp] = (num & 0x000000ff) >> 0;
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_color_debug > 5) {
			printf ("Colour %d : 0x%8x (%x,%x,%x)\n", lp,
				num, pal->red[lp], pal->green[lp], pal->blue[lp]);
		}
#endif
		pal->gnum_cols[lp] = NULL;
	}
	return pal;
}

static StyleColor *
black_or_white_contrast (StyleColor const * contrast)
{
	/* FIXME FIXME FIXME : This is a BIG guess */
	/* Is the contrast colour closer to black or white based
	 * on this VERY loose metric.
	 */
	unsigned const guess =
	    contrast->color.red +
	    contrast->color.green +
	    contrast->color.blue;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_color_debug > 1) {
		printf ("Contrast 0x%x 0x%x 0x%x : 0x%x\n",
			contrast->color.red,
			contrast->color.green,
			contrast->color.blue,
			guess);
	}
#endif
	/* guess the minimum hacked pseudo-luminosity */
	if (guess < (0x18000)) {
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_color_debug > 1)
			puts("Contrast is White");
#endif
		return style_color_white ();
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_color_debug > 1)
		puts("Contrast is Black");
#endif
	return style_color_black ();
}

StyleColor *
ms_excel_palette_get (ExcelPalette const *pal, gint idx)
{
	/* return black on failure */
	g_return_val_if_fail (pal != NULL, style_color_black ());

	/* NOTE : not documented but seems close
	 * If you find a normative reference please forward it.
	 *
	 * The color index field seems to use
	 *	8-63 = Palette index 0-55
	 *
	 * 	0 = black ?
	 * 	1 = white ?
	 *	64, 65, 127 = auto contrast ?
	 *
	 *	64 appears to be associated with the the background colour
	 *	in the WINDOW2 record.
	 */

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_color_debug > 4) {
		printf ("Color Index %d\n", idx);
	}
#endif

	/* Black ? */
	if (idx == 0)
		return style_color_black ();
	/* White ? */
	if (idx == 1)
		return style_color_white ();

	idx -= 8;
	if (idx < 0 || pal->length <= idx) {
		g_warning ("EXCEL : color index (%d) is out of range (0..%d). Defaulting to black",
			   idx + 8, pal->length);
		return style_color_black();
	}

	if (pal->gnum_cols[idx] == NULL) {
		gushort r, g, b;
		/* scale 8 bit/color ->  16 bit/color by cloning */
		r = (pal->red[idx] << 8) | pal->red[idx];
		g = (pal->green[idx] << 8) | pal->green[idx];
		b = (pal->blue[idx] << 8) | pal->blue[idx];
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_color_debug > 1) {
			printf ("New color in slot %d : RGB= %x,%x,%x\n",
				idx, r, g, b);
		}
#endif
		pal->gnum_cols[idx] = style_color_new (r, g, b);
		g_return_val_if_fail (pal->gnum_cols[idx], style_color_black ());
	}

	style_color_ref (pal->gnum_cols[idx]);
	return pal->gnum_cols[idx];
}

static void
ms_excel_palette_destroy (ExcelPalette *pal)
{
	guint16 lp;

	g_free (pal->red);
	g_free (pal->green);
	g_free (pal->blue);
	for (lp = 0; lp < pal->length; lp++)
		if (pal->gnum_cols[lp])
			style_color_unref (pal->gnum_cols[lp]);
	g_free (pal->gnum_cols);
	g_free (pal);
}

/**
 * Search for a font record from its index in the workbooks font table
 * NB. index 4 is omitted supposedly for backwards compatiblity
 * Returns the font color if there is one.
 **/
static BiffFontData const *
ms_excel_get_font (ExcelSheet *sheet, guint16 font_idx)
{
	BiffFontData const *fd = g_hash_table_lookup (sheet->wb->font_data,
						      &font_idx);

	g_return_val_if_fail (fd != NULL, NULL); /* flag the problem */
	g_return_val_if_fail (fd->index != 4, NULL); /* should not exist */
	return fd;
}

static BiffXFData const *
ms_excel_get_xf (ExcelSheet *sheet, int const xfidx)
{
	BiffXFData *xf;
	GPtrArray const * const p = sheet->wb->XF_cell_records;

	g_return_val_if_fail (p != NULL, NULL);
	if (0 > xfidx || xfidx >= p->len) {
		g_warning ("XL : Xf index 0x%x is not in the range [0..0x%x)", xfidx, p->len);
		return NULL;
	}
	xf = g_ptr_array_index (p, xfidx);

	g_return_val_if_fail (xf, NULL);
	/* FIXME : What is the difference between cell & style formats ?? */
	/* g_return_val_if_fail (xf->xftype == eBiffXCell, NULL); */
	return xf;
}

/**
 * get_substitute_font
 * @fontname    The font name
 *
 * Tries to find a gnome font which matches the Excel font.
 * Returns the name of the substitute font if found. Otherwise returns NULL
 */
/* This is very ad hoc - throw it away when something better comes along */
static gchar *
get_substitute_font (gchar *fontname)
{
	char (*(*p)[2]);
	gchar *res = NULL;

	/* Strictly for testing */
	static char *temporary[][2] = {
		{ "Times New Roman", "Times"},
		{ "Arial",           "Helvetica"},
		{ "Courier New",     "Courier"},
		{ NULL }
	};
	for (p = temporary; (*p)[0]; p++)
		if (strcasecmp ((*p)[0], fontname) == 0) {
			res = (*p)[1];
			break;
		}

	return res;
}

static void
style_optimize (ExcelSheet *sheet, int col, int row)
{
	g_return_if_fail (sheet != NULL);

	if (col < 0) { /* Finish the job */
		sheet_style_optimize (sheet->gnum_sheet, sheet->style_optimize);
		return;
	}
	/*
	 * Generate a range inside which to optimise cell style regions.
	 */
	if (row > sheet->style_optimize.start.row + 2) {
		sheet_style_optimize (sheet->gnum_sheet, sheet->style_optimize);

		sheet->style_optimize.start.col = col;
		if (row > 0) /* Overlap upwards */
			sheet->style_optimize.start.row = row - 1;
		else
			sheet->style_optimize.start.row = 0;
		sheet->style_optimize.end.col   = col;
		sheet->style_optimize.end.row   = row;
	} else {
		if (col > sheet->style_optimize.end.col)
			sheet->style_optimize.end.col   = col;
		if (col < sheet->style_optimize.start.col)
			sheet->style_optimize.start.col = col;

		if (row > sheet->style_optimize.end.row)
			sheet->style_optimize.end.row   = row;
		if (row < sheet->style_optimize.end.row)
			sheet->style_optimize.start.row = row;
	}
}

static MStyle * const *
ms_excel_get_style_from_xf (ExcelSheet *sheet, guint16 xfidx)
{
	BiffXFData const *xf = ms_excel_get_xf (sheet, xfidx);
	BiffFontData const *fd;
	StyleColor	*pattern_color, *back_color, *font_color;
	int		 pattern_index,  back_index,  font_index;
	MStyle *mstyle;
	int i;
	char *subs_fontname;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_color_debug > 2) {
		printf ("XF index %d\n", xfidx);
	}
#endif

	g_return_val_if_fail (xf != NULL, NULL);

	/* If we've already done the conversion use the cached style */
	if (xf->mstyle[0] != NULL) {
		mstyle_ref (xf->mstyle[0]);
		if (xf->mstyle[1] != NULL)
			mstyle_ref (xf->mstyle[1]);
		if (xf->mstyle[2] != NULL)
			mstyle_ref (xf->mstyle[2]);
		return xf->mstyle;
	}

	/* Create a new style and fill it in */
	mstyle = mstyle_new ();

	/* Format */
	if (xf->style_format)
		mstyle_set_format (mstyle, xf->style_format->format);

	/* Alignment */
	mstyle_set_align_v     (mstyle, xf->valign);
	mstyle_set_align_h     (mstyle, xf->halign);
	mstyle_set_fit_in_cell (mstyle, xf->wrap);

	/* Font */
	fd = ms_excel_get_font (sheet, xf->font_idx);
	if (fd != NULL) {
		StyleUnderlineType underline = UNDERLINE_NONE;
		subs_fontname = get_substitute_font (fd->fontname);
		if (subs_fontname)
			mstyle_set_font_name   (mstyle, subs_fontname);
		else
			mstyle_set_font_name   (mstyle, fd->fontname);
		mstyle_set_font_size   (mstyle, fd->height / 20.0);
		mstyle_set_font_bold   (mstyle, fd->boldness >= 0x2bc);
		mstyle_set_font_italic (mstyle, fd->italic);
		mstyle_set_font_strike (mstyle, fd->struck_out);
		switch (fd->underline) {
		case eBiffFUSingle :
		case eBiffFUSingleAcc :
			underline = UNDERLINE_SINGLE;
			break;

		case eBiffFUDouble :
		case eBiffFUDoubleAcc :
			underline = UNDERLINE_DOUBLE;
			break;

		case eBiffFUNone :
			default :
			underline = UNDERLINE_NONE;
		}
		mstyle_set_font_uline  (mstyle, underline);

		font_index = fd->color_idx;
	} else
		font_index = 127; /* Default to White */

	/* Background */
	mstyle_set_pattern (mstyle, xf->fill_pattern_idx);

	/* Solid patterns seem to reverse the meaning */
	/*
	 * FIXME: Is this test correct? I fed Excel an XF record with
	 * fill_pattern_idx = 1, pat_backgnd_col = 1 (black),
	 * pat_foregnd_col = 0 (white). Excel displays  it with black
	 * background. Gnumeric displays it with white background.
	 * Can the "xf->pat_foregnd_col != 0" test be removed?
	 * - Jon Hellan
	 */
	if (xf->fill_pattern_idx == 1 && xf->pat_foregnd_col != 0) {
		pattern_index	= xf->pat_backgnd_col;
		back_index	= xf->pat_foregnd_col;
	} else {
		pattern_index	= xf->pat_foregnd_col;
		back_index	= xf->pat_backgnd_col;
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_color_debug > 4) {
		printf ("back = %d, pat = %d, font = %d, pat_style = %d\n",
			back_index, pattern_index, font_index, xf->fill_pattern_idx);
	}
#endif

	/* ICK : FIXME
	 * There must be a cleaner way of doing this
	 */

	/* Lets guess the state table for setting auto colours */
	if (font_index == 127)
	{
		/* The font is auto.  Lets look for info elsewhere */
		if (back_index == 64 || back_index == 65 || back_index == 0)
		{
			/* Everything is auto default to black text/pattern on white */
			/* FIXME : This should use the 'Normal' Style */
			if (pattern_index == 64 || pattern_index == 65 || pattern_index == 0)
			{
				back_color = style_color_white ();
				font_color = pattern_color = style_color_black ();
			} else
			{
				pattern_color =
					ms_excel_palette_get (sheet->wb->palette,
							      pattern_index);

				/* Contrast back to pattern, and font to back */
				/* FIXME : What is correct ?? */
				back_color = (back_index == 65)
				    ? style_color_white ()
				    : black_or_white_contrast (pattern_color);
				font_color = black_or_white_contrast (back_color);
			}
		} else
		{
			back_color = ms_excel_palette_get (sheet->wb->palette,
							   back_index);

			/* Contrast font to back */
			font_color = black_or_white_contrast (back_color);

			/* Pattern is auto contrast it to back */
			if (pattern_index == 64 || pattern_index == 65 || pattern_index == 0)
				pattern_color = font_color;
			else
				pattern_color =
					ms_excel_palette_get (sheet->wb->palette,
							      pattern_index);
		}
	} else
	{
		/* Use the font as a baseline */
		font_color = ms_excel_palette_get (sheet->wb->palette,
						   font_index);

		if (back_index == 64 || back_index == 65 || back_index == 0)
		{
			/* contrast back to font and pattern to back */
			if (pattern_index == 64 || pattern_index == 65 || pattern_index == 0)
			{
				/* Contrast back to font, and pattern to back */
				back_color = black_or_white_contrast (font_color);
				pattern_color = black_or_white_contrast (back_color);
			} else
			{
				pattern_color =
					ms_excel_palette_get (sheet->wb->palette,
							      pattern_index);

				/* Contrast back to pattern */
				back_color = black_or_white_contrast (pattern_color);
			}
		} else
		{
			back_color = ms_excel_palette_get (sheet->wb->palette,
							   back_index);

			/* Pattern is auto contrast it to back */
			if (pattern_index == 64 || pattern_index == 65 || pattern_index == 0)
				pattern_color = black_or_white_contrast (back_color);
			else
				pattern_color =
					ms_excel_palette_get (sheet->wb->palette,
							      pattern_index);
		}
	}

	g_return_val_if_fail (back_color && pattern_color && font_color, NULL);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_color_debug > 4) {
		printf ("back = #%02x%02x%02x, pat = #%02x%02x%02x, font = #%02x%02x%02x, pat_style = %d\n",
			back_color->red>>8, back_color->green>>8, back_color->blue>>8,
			pattern_color->red>>8, pattern_color->green>>8, pattern_color->blue>>8,
			font_color->red>>8, font_color->green>>8, font_color->blue>>8,
			xf->fill_pattern_idx);
	}
#endif
	/*
	 * This is riddled with leaking StyleColor references !
	 */
	mstyle_set_color (mstyle, MSTYLE_COLOR_FORE, font_color);
	mstyle_set_color (mstyle, MSTYLE_COLOR_BACK, back_color);
	mstyle_set_color (mstyle, MSTYLE_COLOR_PATTERN, pattern_color);

	/* Borders */
	for (i = 0; i < STYLE_ORIENT_MAX; i++) {
		int const color_index = xf->border_color[i];
		/* Handle auto colours */
		StyleColor *color = (color_index == 64 || color_index == 65 || color_index == 127)
#if 0
			/* FIXME : This does not choose well, hard code to black for now */
			? black_or_white_contrast (back_color)
#endif
			? style_color_black ()
			: ms_excel_palette_get (sheet->wb->palette,
						color_index);
		if (xf->border_type [i] != STYLE_BORDER_NONE) {
			MStyle *tmp = mstyle;
			MStyleElementType t;

			if (i == STYLE_BOTTOM) {
				t = MSTYLE_BORDER_TOP;
				mstyle_ref (((BiffXFData *)xf)->mstyle[1] = tmp = mstyle_new());
			} else if (i == STYLE_RIGHT) {
				t = MSTYLE_BORDER_LEFT;
				mstyle_ref (((BiffXFData *)xf)->mstyle[2] = tmp = mstyle_new());
			} else
				t = MSTYLE_BORDER_TOP + i;

			mstyle_set_border (tmp, t,
					   style_border_fetch (xf->border_type [i],
							       color, t));
		}
	}

	/* Set the cache (const_cast) */
	((BiffXFData *)xf)->mstyle[0] = mstyle;
	mstyle_ref (mstyle);
	return xf->mstyle;
}

static void
ms_excel_set_xf (ExcelSheet *sheet, int col, int row, guint16 xfidx)
{
#if UNDERSTAND_DUAL_BORDERS
	MStyleBorder const * b;
#endif
	Range   range;
	MStyle * const * const mstyle  =
	    ms_excel_get_style_from_xf (sheet, xfidx);
	if (mstyle == NULL)
		return;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_color_debug > 2) {
		printf ("%s!%s%d\n", sheet->gnum_sheet->name, col_name(col), row+1);
	}
#endif

	range.start.col = col;
	range.start.row = row;
	range.end       = range.start;

	sheet_style_attach (sheet->gnum_sheet, range, mstyle[0]);
#if UNDERSTAND_DUAL_BORDERS
	printf ("%s%d == %hd\n", col_name(col), row+1, xfidx);
	b = mstyle_get_border (mstyle[0], MSTYLE_BORDER_LEFT);
	printf ("Left = %d\n", b->line_type);
#endif
	if (mstyle[1] != NULL) {
		range.start.col = col;
		range.start.row = row+1;
		range.end       = range.start;
		sheet_style_attach (sheet->gnum_sheet, range, mstyle[1]);
	}
	if (mstyle[2] != NULL) {
#if UNDERSTAND_DUAL_BORDERS
		b = mstyle_get_border (mstyle[2], MSTYLE_BORDER_LEFT);
		printf ("Right = %d\n", b->line_type);
#endif
		range.start.col = col+1;
		range.start.row = row;
		range.end       = range.start;
		sheet_style_attach (sheet->gnum_sheet, range, mstyle[2]);
	}
	style_optimize (sheet, col, row);
}

static void
ms_excel_set_xf_segment (ExcelSheet *sheet, int start_col, int end_col, int row, guint16 xfidx)
{
	Range   range;
	MStyle * const * const mstyle  =
	    ms_excel_get_style_from_xf (sheet, xfidx);
	if (mstyle == NULL)
		return;

	range.start.col = start_col;
	range.start.row = row;
	range.end.col   = end_col;
	range.end.row   = row;
	sheet_style_attach (sheet->gnum_sheet, range, mstyle[0]);

	if (mstyle[1] != NULL) {
		range.start.col = start_col;
		range.start.row = row+1;
		range.end.col   = end_col;
		range.end.row   = row+1;
		sheet_style_attach (sheet->gnum_sheet, range, mstyle[1]);
	}
	if (mstyle[2] != NULL) {
		range.start.col = start_col+1;
		range.start.row = row;
		range.end.col   = end_col+1;
		range.end.row   = row;
		sheet_style_attach (sheet->gnum_sheet, range, mstyle[2]);
	}
}

static StyleBorderType
biff_xf_map_border (int b)
{
	switch (b) {
 	case 0: /* None */
 		return STYLE_BORDER_NONE;
 	case 1: /* Thin */
 		return STYLE_BORDER_THIN;
 	case 2: /* Medium */
 		return STYLE_BORDER_MEDIUM;
 	case 3: /* Dashed */
 		return STYLE_BORDER_DASHED;
 	case 4: /* Dotted */
 		return STYLE_BORDER_DOTTED;
 	case 5: /* Thick */
 		return STYLE_BORDER_THICK;
 	case 6: /* Double */
 		return STYLE_BORDER_DOUBLE;
 	case 7: /* Hair */
 		return STYLE_BORDER_HAIR;
 	case 8: /* Medium Dashed */
 		return STYLE_BORDER_MEDIUM_DASH;
 	case 9: /* Dash Dot */
 		return STYLE_BORDER_DASH_DOT;
 	case 10: /* Medium Dash Dot */
 		return STYLE_BORDER_MEDIUM_DASH_DOT;
 	case 11: /* Dash Dot Dot */
 		return STYLE_BORDER_DASH_DOT_DOT;
 	case 12: /* Medium Dash Dot Dot */
 		return STYLE_BORDER_MEDIUM_DASH_DOT_DOT;
 	case 13: /* Slanted Dash Dot*/
 		return STYLE_BORDER_SLANTED_DASH_DOT;
 	}
  	printf ("Unknown border style %d\n", b);
 	return STYLE_BORDER_NONE;
}

static int
excel_map_pattern_index_from_excel (int const i)
{
	static int const map_from_excel[] = {
		 0,
		 1,  3,  2,  4,  7,  8,
		 10, 9, 11, 12, 13, 14,
		15, 16, 17, 18,  5,  6
	};

	/* Default to Solid if out of range */
	g_return_val_if_fail (i >= 0 &&
			      i < (sizeof(map_from_excel)/sizeof(int)), 0);

	return map_from_excel[i];
}

/**
 * Parse the BIFF XF Data structure into a nice form, see S59E1E.HTM
 **/
static void
biff_xf_data_new (ExcelWorkbook *wb, BiffQuery *q, eBiff_version ver)
{
	BiffXFData *xf = g_new (BiffXFData, 1);
	guint32 data, subdata;

	xf->font_idx = MS_OLE_GET_GUINT16 (q->data);
	xf->format_idx = MS_OLE_GET_GUINT16 (q->data + 2);
	xf->style_format = (xf->format_idx > 0)
	    ? biff_format_data_lookup (wb, xf->format_idx) : NULL;

	data = MS_OLE_GET_GUINT16 (q->data + 4);
	xf->locked = (data & 0x0001) ? eBiffLLocked : eBiffLUnlocked;
	xf->hidden = (data & 0x0002) ? eBiffHHidden : eBiffHVisible;
	xf->xftype = (data & 0x0004) ? eBiffXStyle : eBiffXCell;
	xf->format = (data & 0x0008) ? eBiffFLotus : eBiffFMS;
	xf->parentstyle = (data & 0xfff0) >> 4;

	if (xf->xftype == eBiffXCell && xf->parentstyle != 0) {
		/* TODO Add support for parent styles
		 * XL implements a simple for of inheritance with styles.
		 * If a style's parent changes a value and the child has not
		 * overridden that value explicitly the child gets updated.
		 */
	}

	data = MS_OLE_GET_GUINT16 (q->data + 6);
	subdata = data & 0x0007;
	switch (subdata) {
	case 0:
		xf->halign = HALIGN_GENERAL;
		break;
	case 1:
		xf->halign = HALIGN_LEFT;
		break;
	case 2:
		xf->halign = HALIGN_CENTER;
		break;
	case 3:
		xf->halign = HALIGN_RIGHT;
		break;
	case 4:
		xf->halign = HALIGN_FILL;
		break;
	case 5:
		xf->halign = HALIGN_JUSTIFY;
		break;
	case 6:
		/*
		 * All adjacent blank cells with this type of alignment
		 * are merged into a single span.  cursor still behaves
		 * normally and the span is adjusted if contents are changed.
		 * Use center for now.
		 */
		xf->halign = HALIGN_CENTER_ACROSS_SELECTION;
		break;

	default:
		xf->halign = HALIGN_JUSTIFY;
		printf ("Unknown halign %d\n", subdata);
		break;
	}
	xf->wrap = (data & 0x0008) ? TRUE : FALSE;
	subdata = (data & 0x0070) >> 4;
	switch (subdata) {
	case 0:
		xf->valign = VALIGN_TOP;
		break;
	case 1:
		xf->valign = VALIGN_CENTER;
		break;
	case 2:
		xf->valign = VALIGN_BOTTOM;
		break;
	case 3:
		xf->valign = VALIGN_JUSTIFY;
		break;
	default:
		printf ("Unknown valign %d\n", subdata);
		break;
	}
	/*
	 * FIXME: ignored bit 0x0080
	 */
	if (ver == eBiffV8)
		xf->rotation = (data >> 8);
	else {
		subdata = (data & 0x0300) >> 8;
		switch (subdata) {
		case 0:
			xf->rotation = 0;
			break;
		case 1: /* vertical letters no rotation */
			xf->rotation = 255;
			break;
		case 2: /* 90deg anti-clock */
			xf->rotation = 90;
			break;
		case 3: /* 90deg clock */
			xf->rotation = 180;
			break;
		}
	}

	if (xf->rotation != 0) {
		static gboolean needs_warning = TRUE;
		if (needs_warning) {
			needs_warning = FALSE;
			g_warning ("EXCEL : rotated text is not supported yet.");
		}
	}

	if (ver == eBiffV8) {
		/* FIXME : This code seems irrelevant for merging.
		 * The undocumented record MERGECELLS appears to be the correct source.
		 * Nothing seems to set the merge flags.
		 * I've not seen examples of indent or shrink.
		 */
		static gboolean indent_warn = TRUE;
		static gboolean shrink_warn = TRUE;
		static gboolean merge_warn = TRUE;

		/* FIXME : What are the lower 8 bits Always 0 ?? */
		/* We need this to be able to support travel.xls */
		guint16 const data = MS_OLE_GET_GUINT16 (q->data + 8);
		int const indent = data & 0x0f;
		gboolean const shrink = (data & 0x10) ? TRUE : FALSE;
		gboolean const merge = (data & 0x20) ? TRUE : FALSE;

		if (indent != 0 && indent_warn) {
			indent_warn = FALSE;
			g_warning ("EXCEL : horizontal indent of %d (> 0) is not supported yet.",
				   indent);
		}
		if (shrink && shrink_warn) {
			shrink_warn = FALSE;
			g_warning ("EXCEL : Shrink to fit is not supported yet.");
		}
		if (merge && merge_warn) {
			merge_warn = FALSE;
			g_warning ("EXCEL : Merge cells is not supported yet.");
		}

		subdata = (data & 0x00C0) >> 10;
		switch (subdata) {
		case 0:
			xf->eastern = eBiffEContext;
			break;
		case 1:
			xf->eastern = eBiffEleftToRight;
			break;
		case 2:
			xf->eastern = eBiffErightToLeft;
			break;
		default:
			printf ("Unknown location %d\n", subdata);
			break;
		}
	}
	if (ver == eBiffV8) {	/*
				 * Very different
				 */
		int has_diagonals, diagonal_style;
		data = MS_OLE_GET_GUINT16 (q->data + 10);
		subdata = data;
		xf->border_type[STYLE_LEFT] = biff_xf_map_border (subdata & 0xf);
		subdata = subdata >> 4;
		xf->border_type[STYLE_RIGHT] = biff_xf_map_border (subdata & 0xf);
		subdata = subdata >> 4;
		xf->border_type[STYLE_TOP] = biff_xf_map_border (subdata & 0xf);
		subdata = subdata >> 4;
		xf->border_type[STYLE_BOTTOM] = biff_xf_map_border (subdata & 0xf);
		subdata = subdata >> 4;

		data = MS_OLE_GET_GUINT16 (q->data + 12);
		subdata = data;
		xf->border_color[STYLE_LEFT] = (subdata & 0x7f);
		subdata = subdata >> 7;
		xf->border_color[STYLE_RIGHT] = (subdata & 0x7f);
		subdata = (data & 0xc000) >> 14;
		has_diagonals = subdata & 0x3;

		data = MS_OLE_GET_GUINT32 (q->data + 14);
		subdata = data;
		xf->border_color[STYLE_TOP] = (subdata & 0x7f);
		subdata = subdata >> 7;
		xf->border_color[STYLE_BOTTOM] = (subdata & 0x7f);
		subdata = subdata >> 7;

		/* Assign the colors whether we have a border or not.  We will
		 * handle that later */
		xf->border_color[STYLE_DIAGONAL] =
		xf->border_color[STYLE_REV_DIAGONAL] = (subdata & 0x7f);

		/* Ok.  Now use the flag from above to assign borders */
		diagonal_style = biff_xf_map_border (((data & 0x01e00000) >> 21) & 0xf);
		xf->border_type[STYLE_DIAGONAL] = (has_diagonals & 0x2)
			?  diagonal_style : STYLE_BORDER_NONE;
		xf->border_type[STYLE_REV_DIAGONAL] = (has_diagonals & 0x1)
			?  diagonal_style : STYLE_BORDER_NONE;

		xf->fill_pattern_idx =
			excel_map_pattern_index_from_excel ((data>>26) & 0x3f);

		data = MS_OLE_GET_GUINT16 (q->data + 18);
		xf->pat_foregnd_col = (data & 0x007f);
		xf->pat_backgnd_col = (data & 0x3f80) >> 7;
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_color_debug > 2) {
			printf("Color f=0x%x b=0x%x pat=0x%x\n",
			       xf->pat_foregnd_col,
			       xf->pat_backgnd_col,
			       xf->fill_pattern_idx);
		}
#endif
	} else { /* Biff 7 */
		data = MS_OLE_GET_GUINT16 (q->data + 8);
		xf->pat_foregnd_col = (data & 0x007f);
		xf->pat_backgnd_col = (data & 0x1f80) >> 7;

		data = MS_OLE_GET_GUINT16 (q->data + 10);
		xf->fill_pattern_idx =
			excel_map_pattern_index_from_excel (data & 0x3f);

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_color_debug > 2) {
			printf("Color f=0x%x b=0x%x pat=0x%x\n",
			       xf->pat_foregnd_col,
			       xf->pat_backgnd_col,
			       xf->fill_pattern_idx);
		}
#endif
		/*
		 * Luckily this maps nicely onto the new set.
		 */
		xf->border_type[STYLE_BOTTOM] = biff_xf_map_border ((data & 0x1c0) >> 6);
		xf->border_color[STYLE_BOTTOM] = (data & 0xfe00) >> 9;

		data = MS_OLE_GET_GUINT16 (q->data + 12);
		subdata = data;
		xf->border_type[STYLE_TOP] = biff_xf_map_border (subdata & 0x07);
		subdata = subdata >> 3;
		xf->border_type[STYLE_LEFT] = biff_xf_map_border (subdata & 0x07);
		subdata = subdata >> 3;
		xf->border_type[STYLE_RIGHT] = biff_xf_map_border (subdata & 0x07);

		subdata = subdata >> 3;
		xf->border_color[STYLE_TOP] = subdata;

		data = MS_OLE_GET_GUINT16 (q->data + 14);
		subdata = data;
		xf->border_color[STYLE_LEFT] = (subdata & 0x7f);
		subdata = subdata >> 7;
		xf->border_color[STYLE_RIGHT] = (subdata & 0x7f);

		/* Init the diagonals which were not availabile in Biff7 */
		xf->border_type[STYLE_DIAGONAL] =
			xf->border_type[STYLE_REV_DIAGONAL] = 0;
		xf->border_color[STYLE_DIAGONAL] =
			xf->border_color[STYLE_REV_DIAGONAL] = 127;
	}

	/* Init the cache */
	xf->mstyle[0] = xf->mstyle[1] = xf->mstyle[2] = NULL;

	g_ptr_array_add (wb->XF_cell_records, xf);
#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 2) {
		printf ("XF : Font %d, Format %d, Fore %d, Back %d\n",
			xf->font_idx, xf->format_idx,
			xf->pat_foregnd_col, xf->pat_backgnd_col);
	}
#endif
}

static gboolean
biff_xf_data_destroy (BiffXFData *xf)
{
	int i;

	if (xf->style_format)
		style_format_unref (xf->style_format);
	for (i = 3; --i >= 0 ; )
		if (xf->mstyle[i])
			mstyle_unref (xf->mstyle[i]);
	g_free (xf);
	return 1;
}

static void
ms_excel_sheet_set_version (ExcelSheet *sheet, eBiff_version ver)
{
	sheet->ver = ver;
}

static void
ms_excel_sheet_insert (ExcelSheet *sheet, int xfidx,
		       int col, int row, const char *text)
{
	Cell *cell;

	ms_excel_set_xf (sheet, col, row, xfidx);

	cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);

	/* NB. cell_set_text _certainly_ strdups *text */
	if (text)
		cell_set_text_simple (cell, text);
	else
		cell_set_value (cell, value_new_empty ());
}

/* Shared formula support functions */
static guint
biff_shared_formula_hash (const BiffSharedFormulaKey *d)
{
	return (d->row<<16)+d->col;
}

static guint
biff_shared_formula_equal (const BiffSharedFormulaKey *a,
			   const BiffSharedFormulaKey *b)
{
	return (a->col == b->col && a->row == b->row);
}

static gboolean
biff_shared_formula_destroy (gpointer key, BiffSharedFormula *sf,
			     gpointer userdata)
{
	if (sf != NULL)
		g_free (sf->data);
	g_free (sf);
	return TRUE;
}

static gboolean
ms_excel_formula_shared (BiffQuery *q, ExcelSheet *sheet, Cell *cell)
{
	g_return_val_if_fail (ms_biff_query_next (q), FALSE);
	if (q->ls_op != BIFF_SHRFMLA && q->ls_op != BIFF_ARRAY) {
		printf ("EXCEL : unexpected record after a formula 0x%x in '%s'\n",
			q->opcode, cell_name (cell->col->pos, cell->row->pos));
		return FALSE;
	} else {
		gboolean const is_array = (q->ls_op == BIFF_ARRAY);
		guint16 const array_row_first = MS_OLE_GET_GUINT16(q->data + 0);
		guint16 const array_row_last = MS_OLE_GET_GUINT16(q->data + 2);
		guint8 const array_col_first = MS_OLE_GET_GUINT8(q->data + 4);
		guint8 const array_col_last = MS_OLE_GET_GUINT8(q->data + 5);
		guint8 *data =
			q->data + (is_array ? 14 : 10);
		guint16 const data_len =
			MS_OLE_GET_GUINT16(q->data + (is_array ? 12 : 8));
		ExprTree *expr = ms_excel_parse_formula (sheet->wb, sheet, data,
							 array_col_first,
							 array_row_first,
							 !is_array, data_len,
							 NULL);
		BiffSharedFormula *sf = g_new (BiffSharedFormula, 1);

		/*
		 * WARNING : Do NOT use the upper left corner as the hashkey.
		 *     For some bizzare reason XL appears to sometimes not
		 *     flag the formula as shared until later.
		 *  Use the location of the cell we are reading as the key.
		 */
		sf->key.col = cell->col->pos; /* array_col_first; */
		sf->key.row = cell->row->pos; /* array_row_first; */
		sf->is_array = is_array;
		if (data_len > 0) {
			sf->data = g_new (guint8, data_len);
			memcpy (sf->data, data, data_len);
		} else
			sf->data = NULL;
		sf->data_len = data_len;

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 1) {
			printf ("Shared formula, extent %s:%s\n",
				cell_name(array_col_first, array_row_first),
				cell_name(array_col_last, array_row_last));
		}
#endif

		/* Whack in the hash for later */
		g_hash_table_insert (sheet->shared_formulae, &sf->key, sf);

		g_return_val_if_fail (expr != NULL, FALSE);

		if (is_array)
			cell_set_array_formula (sheet->gnum_sheet,
						array_row_first,
						array_col_first,
						array_row_last,
						array_col_last, expr);
		else
		    cell_set_formula_tree_simple (cell, expr);

		expr_tree_unref (expr);
	}
	return TRUE;
}

/* FORMULA */
static void
ms_excel_read_formula (BiffQuery *q, ExcelSheet *sheet)
{
	/*
	 * NOTE : There must be _no_ path through this function that does
	 *        not set the cell value.
	 */

	/* Pre-retrieve incase this is a string */
	gboolean array_elem, is_string = FALSE;
	guint16 const xf_index = EX_GETXF (q);
	guint16 const col = EX_GETCOL (q);
	guint16 const row = EX_GETROW (q);
	Cell *cell;
	ExprTree *expr;
	Value *val = NULL;

	/* Set format */
	ms_excel_set_xf (sheet, col, row, xf_index);

	/* Then fetch Cell */
	cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0)
		printf ("Formula in %s%d;\n", col_name(col), row+1);
#endif

	/* TODO TODO TODO : Wishlist
	 * We should make an array of minimum sizes for each BIFF type
	 * and have this checking done there.
	 */
	if (q->length < 22) {
		printf ("FIXME: serious formula error: "
			"invalid FORMULA (0x%x) record with length %d (should >= 22)\n",
			q->opcode, q->length);
		cell_set_text (cell, "Formula error");
		return;
	}
	if (q->length < (22 + MS_OLE_GET_GUINT16 (q->data+20))) {
		printf ("FIXME: serious formula error: "
			"supposed length 0x%x, real len 0x%x\n",
			MS_OLE_GET_GUINT16 (q->data+20), q->length);
		cell_set_text (cell, "Formula error");
		return;
	}

	/*
	 * Get the current value so that we can format, do this BEFORE handling
	 * shared/array formulas or strings in case we need to go to the next
	 * record
	 */
	if (MS_OLE_GET_GUINT16 (q->data+12) != 0xffff) {
		double const num = BIFF_GETDOUBLE(q->data+6);
		val = value_new_float (num);
	} else {
		guint8 const val_type = MS_OLE_GET_GUINT8 (q->data+6);
		switch (val_type) {
		case 0 : /* String */
			is_string = TRUE;
			break;

		case 1 : /* Boolean */
		{
			guint8 const v = MS_OLE_GET_GUINT8 (q->data+8);
			val = value_new_bool (v ? TRUE : FALSE);
			break;
		}

		case 2 : /* Error */
		{
			EvalPosition ep;
			guint8 const v = MS_OLE_GET_GUINT8 (q->data+8);
			char const *const err_str =
			    biff_get_error_text (v);

			/* FIXME FIXME FIXME : Init ep */
			val = value_new_error (&ep, err_str);
			break;
		}

		case 3 : /* Empty */
			/* TODO TODO TODO
			 * This is undocumented and a big guess, but it seems
			 * accurate.
			 */
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 0) {
				printf ("%s:%s : has type 3 contents.  "
					"Is it an empty cell ?\n",
					sheet->gnum_sheet->name,
					cell_name (cell->col->pos, cell->row->pos));
				if (ms_excel_read_debug > 5)
					ms_ole_dump (q->data+6, 8);
			}
#endif
			val = value_new_empty ();
			break;

		default :
			printf ("Unknown type (%x) for cell's current val\n",
				val_type);
		};
	}

	/* Now try to parse the formula */
	expr = ms_excel_parse_formula (sheet->wb, sheet, (q->data + 22),
				       col, row,
				       FALSE, MS_OLE_GET_GUINT16 (q->data+20),
				       &array_elem);

	/* Error was flaged by parse_formula */
	if (expr != NULL) {
		/*
		 * FIXME FIXME FIXME : cell_set_formula_tree_simple queues
		 *                     things for recalc !!  and puts them in
		 *                     the WRONG order !!  This is doubly
		 *                     wrong, We should only recalc on load
		 *                     when the flag is set.
		 */
		cell_set_formula_tree_simple (cell, expr);
		expr_tree_unref (expr);
	} else if (!array_elem && !ms_excel_formula_shared (q, sheet, cell))
	{
		/*
		 * NOTE : Only the expression is screwed.
		 * The value and format can still be set.
		 */
		g_warning ("EXCEL : Shared formula problems");
	}

	if (is_string) {
		guint16 code;
		if (ms_biff_query_peek_next(q, &code) && code == BIFF_STRING) {
			char *v = NULL;
			if (ms_biff_query_next (q)) {
				/*
				 * NOTE : the Excel developers kit docs are
				 *        WRONG.  There is an article that
				 *        clarifies the behaviour to be the std
				 *        unicode format rather than the pure
				 *        length version the docs describe.
				 */
				guint16 const len = MS_OLE_GET_GUINT16(q->data);

				if (len > 0)
					v = biff_get_text (q->data + 2, len, NULL);
				else
					v = g_strdup(""); /* Pre-Biff8 seems to use len=0 */
			}
			if (v) {
				val = value_new_string (v);
				g_free (v);
			} else {
				g_warning ("EXCEL : invalid STRING record");
				val = value_new_string ("INVALID STRING");
			}
		} else {
			/*
			 * Docs say that there should be a STRING
			 * record here
			 */
			g_warning ("EXCEL : missing STRING record");
			val = value_new_string ("MISSING STRING");
		}
	}

	if (val == NULL) {
		g_warning ("EXCEL : Invalid state.  Missing Value?");
		val = value_new_string ("MISSING Value");
	}
	if (cell->value != NULL)
		value_release (cell->value);

	/* Set value */
	cell->value = val;
}

BiffSharedFormula *
ms_excel_sheet_shared_formula (ExcelSheet *sheet, int const col, int const row)
{
	BiffSharedFormulaKey k;
	k.col = col;
	k.row = row;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 5)
		printf ("FIND SHARED : %s%d\n", col_name(col), row+1);
#endif
	return g_hash_table_lookup (sheet->shared_formulae, &k);
}

static ExcelSheet *
ms_excel_sheet_new (ExcelWorkbook *wb, const char *name)
{
	ExcelSheet *ans = (ExcelSheet *) g_malloc (sizeof (ExcelSheet));

	ans->gnum_sheet = sheet_new (wb->gnum_wb, name);

	sheet_set_zoom_factor (ans->gnum_sheet, 1.);
	ans->wb         = wb;
	ans->obj_queue  = NULL;

	ans->shared_formulae =
	    g_hash_table_new ((GHashFunc)biff_shared_formula_hash,
			      (GCompareFunc)biff_shared_formula_equal);

	ans->style_optimize.start.col = 0;
	ans->style_optimize.start.row = 0;
	ans->style_optimize.end.col   = 0;
	ans->style_optimize.end.row   = 0;
	ans->base_char_width          = -1;
	ans->base_char_width_default  = -1;

	return ans;
}

static void
ms_excel_sheet_insert_val (ExcelSheet *sheet, int xfidx,
			   int col, int row, Value *v)
{
	Cell *cell;
	g_return_if_fail (v);
	g_return_if_fail (sheet);

	ms_excel_set_xf (sheet, col, row, xfidx);
	cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);
	cell_set_value_simple (cell, v);
}

static void
ms_excel_sheet_insert_blank (ExcelSheet *sheet, int xfidx,
			     int col, int row)
{
	g_return_if_fail (sheet);

	ms_excel_set_xf (sheet, col, row, xfidx);
}

static void
ms_excel_sheet_set_comment (ExcelSheet *sheet, int col, int row, char *text)
{
	if (text) {
		Cell *cell = sheet_cell_get (sheet->gnum_sheet, col, row);
		if (!cell) {
			cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);
			cell_set_value (cell, value_new_empty ());
		}
		cell_set_comment (cell, text);
	}
}

static void
ms_excel_sheet_append_comment (ExcelSheet *sheet, int col, int row, char *text)
{
	if (text) {
		Cell *cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);
		if (!cell->value)
			cell_set_text (cell, "");
		if (cell->comment && cell->comment->comment &&
		    cell->comment->comment->str) {
			char *txt = g_strconcat (cell->comment->comment->str, text, NULL);
			cell_set_comment (cell, txt);
			g_free (txt);
		}
	}
}

static void
ms_excel_sheet_destroy (ExcelSheet *sheet)
{
	g_hash_table_foreach_remove (sheet->shared_formulae,
				     (GHRFunc)biff_shared_formula_destroy,
				     sheet);
	g_hash_table_destroy (sheet->shared_formulae);
	sheet->shared_formulae = NULL;

	if (sheet->gnum_sheet)
		sheet_destroy (sheet->gnum_sheet);
	sheet->gnum_sheet = NULL;
	ms_excel_sheet_destroy_objs (sheet);

	g_free (sheet);
}

static ExcelWorkbook *
ms_excel_workbook_new (eBiff_version ver)
{
	ExcelWorkbook *ans = (ExcelWorkbook *) g_malloc (sizeof (ExcelWorkbook));

	ans->extern_sheets = NULL;
	ans->gnum_wb = NULL;
	/* Boundsheet data hashed twice */
	ans->boundsheet_data_by_stream = g_hash_table_new ((GHashFunc)biff_guint32_hash,
							   (GCompareFunc)biff_guint32_equal);
	ans->boundsheet_data_by_index  = g_hash_table_new ((GHashFunc)biff_guint16_hash,
							   (GCompareFunc)biff_guint16_equal);
	ans->font_data        = g_hash_table_new ((GHashFunc)biff_guint16_hash,
						  (GCompareFunc)biff_guint16_equal);
	ans->excel_sheets     = g_ptr_array_new ();
	ans->XF_cell_records  = g_ptr_array_new ();
	ans->name_data        = g_ptr_array_new ();
	ans->blips            = g_ptr_array_new ();
	ans->charts           = g_ptr_array_new ();
	ans->format_data      = g_hash_table_new ((GHashFunc)biff_guint16_hash,
						  (GCompareFunc)biff_guint16_equal);
	ans->palette          = ms_excel_default_palette ();
	ans->ver              = ver;
	ans->global_strings   = NULL;
	ans->global_string_max  = 0;
	ans->read_drawing_group = 0;

	return ans;
}

static void
ms_excel_workbook_attach (ExcelWorkbook *wb, ExcelSheet *ans)
{
	g_return_if_fail (wb);
	g_return_if_fail (ans);

	workbook_attach_sheet (wb->gnum_wb, ans->gnum_sheet);
	g_ptr_array_add (wb->excel_sheets, ans);
}

static gboolean
ms_excel_workbook_detach (ExcelWorkbook *wb, ExcelSheet *ans)
{
	int    idx = 0;

	if (ans->gnum_sheet) {
		if (!workbook_detach_sheet (wb->gnum_wb, ans->gnum_sheet, FALSE))
			return FALSE;
		/* Detaching the sheet deletes it */
		ans->gnum_sheet = NULL;
	}
	for (idx = 0; idx < wb->excel_sheets->len; idx++)
		if (g_ptr_array_index (wb->excel_sheets, idx) == ans) {
			g_ptr_array_index (wb->excel_sheets, idx) = NULL;
			return TRUE;
		}

	printf ("Sheet not in list of sheets !\n");
	return FALSE;
}

ExcelSheet *
ms_excel_workbook_get_sheet (ExcelWorkbook *wb, guint idx)
{
	if (idx < wb->excel_sheets->len)
		return g_ptr_array_index (wb->excel_sheets, idx);
	return NULL;
}

static void
ms_excel_workbook_destroy (ExcelWorkbook *wb)
{
	gint lp;

	g_hash_table_foreach_remove (wb->boundsheet_data_by_stream,
				     (GHRFunc)biff_boundsheet_data_destroy,
				     wb);
	g_hash_table_destroy (wb->boundsheet_data_by_index);
	g_hash_table_destroy (wb->boundsheet_data_by_stream);
	if (wb->XF_cell_records)
		for (lp = 0; lp < wb->XF_cell_records->len; lp++)
			biff_xf_data_destroy (g_ptr_array_index (wb->XF_cell_records, lp));
	g_ptr_array_free (wb->XF_cell_records, TRUE);

	if (wb->name_data)
		for (lp = 0; lp < wb->name_data->len; lp++)
			biff_name_data_destroy (g_ptr_array_index (wb->name_data, lp));
	g_ptr_array_free (wb->name_data, TRUE);

	for (lp = 0; lp < wb->blips->len; lp++)
		ms_escher_blip_destroy (g_ptr_array_index(wb->blips, lp));
	g_ptr_array_free (wb->blips, TRUE);
	wb->blips = NULL;

	for (lp = 0; lp < wb->charts->len; lp++)
		gnumeric_chart_destroy (g_ptr_array_index(wb->charts, lp));
	g_ptr_array_free (wb->charts, TRUE);
	wb->charts = NULL;

	g_hash_table_foreach_remove (wb->font_data,
				     (GHRFunc)biff_font_data_destroy,
				     wb);
	g_hash_table_destroy (wb->font_data);

	g_hash_table_foreach_remove (wb->format_data,
				     (GHRFunc)biff_format_data_destroy,
				     wb);
	g_hash_table_destroy (wb->format_data);

	if (wb->palette && wb->palette != ms_excel_default_palette ())
		ms_excel_palette_destroy (wb->palette);

	if (wb->extern_sheets)
		g_free (wb->extern_sheets);

	if (wb->global_strings) {
		int i;
		for (i = 0; i < wb->global_string_max; i++)
			g_free (wb->global_strings[i]);
		g_free (wb->global_strings);
	}

	g_free (wb);
}

/**
 * Unpacks a MS Excel RK structure,
 **/
static Value *
biff_get_rk (guint8 const *ptr)
{
	gint32 number;
	enum eType {
		eIEEE = 0, eIEEEx100 = 1, eInt = 2, eIntx100 = 3
	} type;

	number = MS_OLE_GET_GUINT32 (ptr);
	type = (number & 0x3);
	switch (type) {
	case eIEEE:
	case eIEEEx100:
	{
		guint8 tmp[8];
		double answer;
		int lp;

		/* Think carefully about big/little endian issues before
		   changing this code.  */
		for (lp = 0; lp < 4; lp++) {
			tmp[lp+4]=(lp>0)?ptr[lp]:(ptr[lp]&0xfc);
			tmp[lp]=0;
		}

		answer = BIFF_GETDOUBLE(tmp);
		return value_new_float (type == eIEEEx100 ? answer / 100 : answer);
	}
	case eInt:
		return value_new_int ((number>>2));
	case eIntx100:
		if (number%100==0)
			return value_new_int ((number>>2)/100);
		else
			return value_new_float ((number>>2)/100.0);
	}
	while (1) abort ();
}

/* FIXME: S59DA9.HTM */
static void
ms_excel_read_name (BiffQuery *q, ExcelSheet *sheet)
{
	guint16 fn_grp_idx;
	guint16 flags          = MS_OLE_GET_GUINT16 (q->data);
#if 0
	guint8  kb_shortcut    = MS_OLE_GET_GUINT8  (q->data + 2);
#endif
	guint8  name_len       = MS_OLE_GET_GUINT8  (q->data + 3);
	guint16 name_def_len;
	guint8 *name_def_data;
	guint16 sheet_idx      = MS_OLE_GET_GUINT16 (q->data + 8);
	guint8  menu_txt_len   = MS_OLE_GET_GUINT8  (q->data + 10);
	guint8  descr_txt_len  = MS_OLE_GET_GUINT8  (q->data + 11);
	guint8  help_txt_len   = MS_OLE_GET_GUINT8  (q->data + 12);
	guint8  status_txt_len = MS_OLE_GET_GUINT8  (q->data + 13);
	char *name, *menu_txt, *descr_txt, *help_txt, *status_txt;
	guint8 const *ptr;

#if 0
	dump_biff (q);
#endif
	/* FIXME FIXME FIXME : Offsets have moved alot between versions.
	 *                     track down the details */
	if (sheet->ver >= eBiffV8) {
		name_def_len   = MS_OLE_GET_GUINT16 (q->data + 4);
		name_def_data  = q->data + 15 + name_len;
		ptr = q->data + 14;
	} else if (sheet->ver >= eBiffV7) {
		name_def_len   = MS_OLE_GET_GUINT16 (q->data + 4);
		name_def_data  = q->data + 14 + name_len;
		ptr = q->data + 14;
	} else {
		name_def_len   = MS_OLE_GET_GUINT16 (q->data + 4);
		name_def_data  = q->data + 5 + name_len;
		ptr = q->data + 5;
	}

	/* FIXME FIXME FIXME : Disable for now */
	if (0 && name_len == 1 && *ptr <= 0x0c) {
		switch (*ptr)
		{
		case 0x00 :	name = "Consolidate_Area"; break;
		case 0x01 :	name = "Auto_Open"; break;
		case 0x02 :	name = "Auto_Close"; break;
		case 0x03 :	name = "Extract"; break;
		case 0x04 :	name = "Database"; break;
		case 0x05 :	name = "Criteria"; break;
		case 0x06 :	name = "Print_Area"; break;
		case 0x07 :	name = "Print_Titles"; break;
		case 0x08 :	name = "Recorder"; break;
		case 0x09 :	name = "Data_Form"; break;
		case 0x0a :	name = "Auto_Activate"; break;
		case 0x0b :	name = "Auto_Deactivate"; break;
		case 0x0c :	name = "Sheet_Title"; break;
		default :
				name = "ERROR ERROR ERROR.  This is impossible";
		}
		name = g_strdup (name);
	} else
		name = biff_get_text (ptr, name_len, NULL);
	ptr+= name_len + name_def_len;
	menu_txt = biff_get_text (ptr, menu_txt_len, NULL);
	ptr+= menu_txt_len;
	descr_txt = biff_get_text (ptr, descr_txt_len, NULL);
	ptr+= descr_txt_len;
	help_txt = biff_get_text (ptr, help_txt_len, NULL);
	ptr+= help_txt_len;
	status_txt = biff_get_text (ptr, status_txt_len, NULL);

	fn_grp_idx = (flags&0xfc0)>>6;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 5) {
		printf ("Name record : '%s', '%s', '%s', '%s', '%s'\n",
			name ? name : "(null)",
			menu_txt ? menu_txt : "(null)",
			descr_txt ? descr_txt : "(null)",
			help_txt ? help_txt : "(null)",
			status_txt ? status_txt : "(null)");
		ms_ole_dump (name_def_data, name_def_len);

		/* Unpack flags */
		if ((flags&0x0001) != 0)
			printf (" Hidden");
		if ((flags&0x0002) != 0)
			printf (" Function");
		if ((flags&0x0004) != 0)
			printf (" VB-Proc");
		if ((flags&0x0008) != 0)
			printf (" Proc");
		if ((flags&0x0010) != 0)
			printf (" CalcExp");
		if ((flags&0x0020) != 0)
			printf (" BuiltIn");
		if ((flags&0x1000) != 0)
			printf (" BinData");
		printf ("\n");
	}
#endif

	biff_name_data_new (sheet->wb, name, sheet_idx,
			    name_def_data, name_def_len,
			    FALSE, (sheet_idx != 0));

	if (menu_txt)
		g_free (menu_txt);
	if (descr_txt)
		g_free (descr_txt);
	if (help_txt)
		g_free (help_txt);
	if (status_txt)
		g_free (status_txt);
}

/* S59D7E.HTM */
static void
ms_excel_externname (BiffQuery *q, ExcelSheet *sheet)
{
	char const *name;
	guint8 *defn;
	guint16 defnlen;
	if (sheet->ver >= eBiffV7) {
		guint32 namelen  = MS_OLE_GET_GUINT8(q->data+6);
		name = biff_get_text (q->data+7, namelen, &namelen);
		defn     = q->data+7 + namelen;
		defnlen  = MS_OLE_GET_GUINT16(defn);
		defn += 2;
	} else { /* Ancient Papyrus spec. */
		static guint8 data[] = { 0x1c, 0x17 }; /* Error : REF */
		defn = data;
		defnlen = 2;
		name = biff_get_text (q->data+1,
				      MS_OLE_GET_GUINT8(q->data), NULL);
	}

	biff_name_data_new (sheet->wb, name, 0, defn, defnlen, TRUE, FALSE);
}

/**
 * init_base_char_width_for_read:
 * @sheet	the Excel sheet
 *
 * Measures base character width for column sizing.
 */
static void
init_base_char_width_for_read (ExcelSheet *sheet)
{
	/* Use the 'Normal' Style which is by definition the 0th */
	BiffXFData const *xf = ms_excel_get_xf (sheet, 0);
	BiffFontData const *fd = (xf != NULL)
		? ms_excel_get_font (sheet, xf->font_idx)
		: NULL;
	/* default to Arial 10 */
	char const * name = (fd != NULL) ? fd->fontname : "Arial";
	double const size = (fd != NULL) ? fd->height : 20.* 10.;

	sheet->base_char_width =
		lookup_font_base_char_width_new (name, size, FALSE);
	sheet->base_char_width_default =
		lookup_font_base_char_width_new (name, size, TRUE);
}

/**
 * get_base_char_width:
 * @sheet	the Excel sheet
 *
 * Returns base character width for column sizing. Uses cached value
 * if font alrady measured. Otherwise measure font.
 *
 * Excel uses the character width of the font in the "Normal" style.
 * The char width is based on the font in the "Normal" style.
 * This style is actually common to all sheets in the
 * workbook, but I find it more robust to treat it as a sheet
 * attribute.
 */
static double
get_base_char_width (ExcelSheet *sheet, gboolean const is_default)
{
	if (sheet->base_char_width <= 0)
		init_base_char_width_for_read (sheet);

	return is_default
		? sheet->base_char_width_default : sheet->base_char_width;
}

/**
 * get_row_height_units:
 * @height	height in Excel units
 *
 * Converts row height from Excel units to points. Returns height in points.
 *
 * Excel specifies row height in 1/20 of a point.
 *
 * What we now print out is just 0.5% shorter than theoretical
 * height. The height of what Excel prints out varies in mysterious
 * ways. Sometimes it is close to theoretical, sometimes it is a few %
 * shorter. I don't see any point in correcting for the 0.5% until we
 * know the whole story.
 */
static double
get_row_height_units (guint16 height)
{
	return 1. / 20. * height;
}

/**
 * ms_excel_read_row:
 * @q 		A BIFF query
 * @sheet	The Excel sheet
 *
 * Processes a BIFF row info (BIFF_ROW) record. See: S59DDB.HTM
 */
static void
ms_excel_read_row (BiffQuery *q, ExcelSheet *sheet)
{
	guint16 const row = MS_OLE_GET_GUINT16(q->data);
	guint16 const start_col = MS_OLE_GET_GUINT16(q->data+2);
	guint16 const end_col = MS_OLE_GET_GUINT16(q->data+4) - 1;
	guint16 const height = MS_OLE_GET_GUINT16(q->data+6);
	guint16 const flags = MS_OLE_GET_GUINT16(q->data+12);
	guint16 const flags2 = MS_OLE_GET_GUINT16(q->data+14);
	guint16 const xf = flags2 & 0xfff;

	/* If the bit is on it indicates that the row is of 'standard' height.
	 * However the remaining bits still include the size.
	 */
	gboolean const is_std_height = (height & 0x8000) != 0;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		printf ("Row %d height 0x%x;\n", row+1, height);
		if (is_std_height)
			puts ("Is Std Height");
		if (flags2 & 0x1000)
			puts ("Top thick");
		if (flags2 & 0x2000)
			puts ("Bottom thick");
	}
#endif

	/* TODO : Put mechanism in place to handle thick margins */
	/* TODO : Columns actually set the size even when it is the default.
	 *        Which approach is better ?
	 */
	/* TODO : We should store the default row style too.
	 *        Which has precedence rows or cols ??
	 */
	if (!is_std_height) {
		double hu = get_row_height_units (height);
		sheet_row_set_size_pts (sheet->gnum_sheet, row, hu, TRUE);
	}

	/* FIXME : We should associate a style region with the row segment */

	/* FIXME : I am not clear on the difference between collapsed, and dyn 0
	 * Use both for now */
	if (flags & 0x30)
		sheet_row_col_visible (sheet->gnum_sheet, FALSE, FALSE, row, 1);

	if (flags & 0x80) {
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 1) {
			printf ("row %d has flags 0x%x a default style %hd from col %s - ",
				row+1, flags, xf, col_name(start_col));
			printf ("%s;\n", col_name(end_col));
		}
#endif
	}
}

/**
 * ms_excel_read_colinfo:
 * @q 		A BIFF query
 * @sheet	The Excel sheet
 *
 * Processes a BIFF column info (BIFF_COLINFO) record. See: S59D67.HTM
 */
static void
ms_excel_read_colinfo (BiffQuery *q, ExcelSheet *sheet)
{
	int lp;
	float col_width;
	guint16 const firstcol = MS_OLE_GET_GUINT16(q->data);
	guint16       lastcol  = MS_OLE_GET_GUINT16(q->data+2);
	guint16       width    = MS_OLE_GET_GUINT16(q->data+4);
	guint16 const xf       = MS_OLE_GET_GUINT16(q->data+6);
	guint16 const options  = MS_OLE_GET_GUINT16(q->data+8);
	gboolean const hidden = (options & 0x0001) ? TRUE : FALSE;
#if 0
	gboolean const collapsed = (options & 0x1000) ? TRUE : FALSE;
	int const outline_level = (options >> 8) & 0x7;
#endif

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		printf ("Column Formatting from col %d to %d of width "
			"%f characters\n", firstcol, lastcol, width/256.0);
		printf ("Options %hd, default style %hd from col %d to %d\n",
			options, xf, firstcol, lastcol);
	}
#endif
	g_return_if_fail (firstcol < SHEET_MAX_COLS);

	if (width != 0) {
		/* Widths are quoted including margins
		 * NOTE : These measurements do NOT correspond to what is
		 * shown to the user
		 */
		col_width = get_base_char_width (sheet, FALSE) * width / 256;
	} else
		/* Columns are of default width */
		col_width = sheet->gnum_sheet->cols.default_style.size_pts;

	/* NOTE : seems like this is inclusive firstcol, inclusive lastcol */
	if (lastcol >= SHEET_MAX_COLS)
		lastcol = SHEET_MAX_COLS-1;
	for (lp = firstcol; lp <= lastcol; ++lp)
		sheet_col_set_size_pts (sheet->gnum_sheet, lp, col_width, TRUE);

	/* TODO : We should associate a style region with the columns */
	if (hidden)
		sheet_row_col_visible (sheet->gnum_sheet, TRUE, FALSE,
				       firstcol, lastcol-firstcol+1);
}

/**
 * Parse the cell BIFF tag, and act on it as neccessary
 * NB. Microsoft Docs give offsets from start of biff record, subtract 4 their docs.
 **/
static void
ms_excel_read_cell (BiffQuery *q, ExcelSheet *sheet)
{
	if (0x1 == q->ms_op) {
		switch (q->opcode) {
		case BIFF_DV :
		case BIFF_DVAL :
		{
			static gboolean needs_warning = TRUE;
			if (needs_warning) {
				printf ("TODO : Data validation has not been implemented\n");
				needs_warning = FALSE;
			}
			break;
		}

		default :
 			ms_excel_unexpected_biff (q, "Cell");
		};
		return;
	}

	switch (q->ls_op) {
	case BIFF_BLANK:
	{
		guint16 const xf = EX_GETXF (q);
		guint16 const col = EX_GETCOL (q);
		guint16 const row = EX_GETROW (q);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 0)
		    printf ("Blank in %s%d xf = 0x%x;\n", col_name(col), row+1, xf);
#endif
		ms_excel_sheet_insert_blank (sheet, xf, col, row);
		break;
	}

	case BIFF_MULBLANK:
	{
		/* S59DA7.HTM is extremely unclear, this is an educated guess */
		int firstcol = EX_GETCOL (q);
		int const row = EX_GETROW (q);
		guint8 const *ptr = (q->data + q->length - 2);
		int lastcol = MS_OLE_GET_GUINT16 (ptr);
		int i, range_end, prev_xf, xf_index;
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 0) {
			printf ("Cells in row %d are blank starting at col %s until col ",
				row+1, col_name(firstcol));
			printf ("%s;\n",
				col_name(lastcol));
		}
#endif
		if (lastcol < firstcol) {
			int const tmp = firstcol;
			firstcol = lastcol;
			lastcol = tmp;
		}

		range_end = i = lastcol;
		prev_xf = -1;
		do
		{
			ptr -= 2;
			xf_index = MS_OLE_GET_GUINT16 (ptr);
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 2) {
				printf (" xf(%s) = 0x%x",
					col_name (i), xf_index);
				if (i == firstcol)
					printf ("\n");
			}
#endif
			if (prev_xf != xf_index) {
				if (prev_xf >= 0)
					ms_excel_set_xf_segment (sheet, i + 1, range_end,
								 row, prev_xf);
				prev_xf = xf_index;
				range_end = i;
			}
		} while (--i >= firstcol);
		ms_excel_set_xf_segment (sheet, firstcol, range_end,
					 row, prev_xf);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 2) {
			printf ("\n");
		}
#endif
		break;
	}

	case BIFF_RSTRING: /* See: S59DDC.HTM */
	{
		guint16 const xf = EX_GETXF (q);
		guint16 const col = EX_GETCOL (q);
		guint16 const row = EX_GETROW (q);
		char *txt = biff_get_text (q->data + 8, EX_GETSTRLEN (q), NULL);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 0)
		    printf ("Rstring in %s%d xf = 0x%x;\n", col_name(col), row+1, xf);
#endif
		ms_excel_sheet_insert (sheet, xf, col, row, txt);
		g_free (txt);
		break;
	}

	/* S59D6D.HTM */
	case BIFF_DBCELL:
		/* Can be ignored on read side */
		break;

	/* S59DAC.HTM */
	case BIFF_NUMBER:
	{
		Value *v = value_new_float (BIFF_GETDOUBLE (q->data + 6));
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 2) {
			printf ("Read number %g\n",
				BIFF_GETDOUBLE (q->data + 6));
		}
#endif
		ms_excel_sheet_insert_val (sheet, EX_GETXF (q), EX_GETCOL (q),
					   EX_GETROW (q), v);
		break;
	}

	case BIFF_ROW:
		ms_excel_read_row (q, sheet);
		break;

	case BIFF_COLINFO:
		ms_excel_read_colinfo (q, sheet);
		break;

	/* See: S59DDA.HTM */
	case BIFF_RK:
	{
		Value *v = biff_get_rk(q->data+6);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 2) {
			printf ("RK number : 0x%x, length 0x%x\n", q->opcode, q->length);
			ms_ole_dump (q->data, q->length);
		}
#endif
		ms_excel_sheet_insert_val (sheet, EX_GETXF (q), EX_GETCOL (q),
					   EX_GETROW (q), v);
		break;
	}

	/* S59DA8.HTM */
	case BIFF_MULRK:
	{
		guint32 col, row, lastcol;
		guint8 const *ptr = q->data;
		Value *v;

/*		printf ("MULRK\n");
		ms_ole_dump (q->data, q->length); */

		row = MS_OLE_GET_GUINT16(q->data);
		col = MS_OLE_GET_GUINT16(q->data+2);
		ptr+= 4;
		lastcol = MS_OLE_GET_GUINT16(q->data+q->length-2);
/*		g_assert ((lastcol-firstcol)*6 == q->length-6 */
		g_assert (lastcol>=col);
		while (col<=lastcol)
		{ /* 2byte XF, 4 byte RK */
			v = biff_get_rk(ptr+2);
			ms_excel_sheet_insert_val (sheet, MS_OLE_GET_GUINT16(ptr),
						   col, row, v);
			col++;
			ptr+= 6;
		}
		break;
	}
	case BIFF_LABEL:
	{
		char *label;
		ms_excel_sheet_insert (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q),
				       (label = biff_get_text (q->data + 8, EX_GETSTRLEN (q), NULL)));
		g_free (label);
		break;
	}

	case BIFF_FORMULA: /* See: S59D8F.HTM */
 		ms_excel_read_formula (q, sheet);
		break;

	case BIFF_LABELSST:
	{
		guint32 const idx = MS_OLE_GET_GUINT32 (q->data + 6);

		if (sheet->wb->global_strings && idx < sheet->wb->global_string_max) {
			char const *str = sheet->wb->global_strings[idx];

			/* FIXME FIXME FIXME : Why would there be a NULL ??? */
			if (str == NULL)
				str = "";
			ms_excel_sheet_insert_val (sheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q),
						   value_new_string (str));
		} else
			printf ("string index 0x%x >= 0x%x\n",
				idx, sheet->wb->global_string_max);
                break;
	}

	case BIFF_EXTERNNAME:
		ms_excel_externname(q, sheet);
		break;

	case BIFF_NAME:
		ms_excel_read_name (q, sheet);
		break;

	case BIFF_IMDATA :
#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		guint16 const from_env = MS_OLE_GET_GUINT16 (q->data+2);
		guint16 const format = MS_OLE_GET_GUINT16 (q->data+2);

		char const * from_name, * format_name;
		switch (from_env) {
		case 1 : from_name = "Windows"; break;
		case 2 : from_name = "Macintosh"; break;
		default: from_name = "Unknown environment?"; break;
		};
		switch (format) {
		case 0x2 :
		    format_name = (from_env==1) ? "windows metafile" : "mac pict";
		    break;

		case 0x9 : format_name = "windows native bitmap"; break;
		case 0xe : format_name = "'native format'"; break;
		default: format_name = "Unknown format?"; break;
		};

		printf ("Picture from %s in %s format\n",
			from_name, format_name);
	}
#endif
	    break;

	case BIFF_STANDARDWIDTH :
		/* What the heck is the 'standard width dialog' ? */
		break;

	default:
		switch (q->opcode) {
		case BIFF_BOOLERR: /* S59D5F.HTM */
		{
			Value *v;
			guint8 const val = MS_OLE_GET_GUINT8(q->data + 6);
			if (MS_OLE_GET_GUINT8(q->data + 7)) {
				/* FIXME : Init EvalPos */
				v = value_new_error (NULL,
						     biff_get_error_text (val));
			} else
				v = value_new_bool (val);
			ms_excel_sheet_insert_val (sheet,
						   EX_GETXF (q), EX_GETCOL (q),
						   EX_GETROW (q), v);
			break;
		}
		default:
 			ms_excel_unexpected_biff (q, "Cell");
			break;
		}
	}
}

static void
margin_read (PrintUnit *pu, double val)
{
	pu->points = unit_convert (val, UNIT_INCH, UNIT_POINTS);
	pu->desired_display = UNIT_INCH; /* FIXME: should be more global */
}

/* S59DE2.HTM */
static void
ms_excel_read_selection (ExcelSheet *sheet, BiffQuery *q)
{
	int const pane_number	= MS_OLE_GET_GUINT8 (q->data);
	int const act_row	= MS_OLE_GET_GUINT16 (q->data + 1);
	int const act_col	= MS_OLE_GET_GUINT16 (q->data + 3);
	int num_refs		= MS_OLE_GET_GUINT16 (q->data + 7);
	guint8 *refs;


#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		printf ("Start selection\n");
		if (ms_excel_read_debug > 6)
			printf ("Cursor : %d %d\n", act_col, act_row);
	}
#endif
	if (pane_number != 3) {
		printf ("FIXME: no pane support\n");
		return;
	}

	sheet_selection_reset_only (sheet->gnum_sheet);
	for (refs = q->data + 9; num_refs > 0; refs += 6, num_refs--) {
		int const start_row = MS_OLE_GET_GUINT16(refs + 0);
		int const start_col = MS_OLE_GET_GUINT8(refs + 4);
		int const end_row   = MS_OLE_GET_GUINT16(refs + 2);
		int const end_col   = MS_OLE_GET_GUINT8(refs + 5);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 6)
			printf ("Ref %d = %d %d %d %d\n", num_refs,
				start_col, start_row, end_col, end_row);
#endif

		/* FIXME : This should not trigger a recalc */
		sheet_selection_add_range (sheet->gnum_sheet,
					   start_col, start_row,
					   start_col, start_row,
					   end_col, end_row);
	}
#if 0
	/* FIXME : Disable for now.  We need to reset the index of the
	 *         current selection range too.  This can do odd things
	 *         if the last range is NOT the currently selected range.
	 */
	sheet_cursor_move (sheet->gnum_sheet, act_col, act_row, FALSE, FALSE);
#endif

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		printf ("Done selection\n");
	}
#endif
}

/**
 * ms_excel_read_default_row_height:
 * @q 		A BIFF query
 * @sheet	The Excel sheet
 *
 * Processes a BIFF default row height (BIFF_DEFAULTROWHEIGHT) record.
 * See: S59D72.HTM
 */
static void
ms_excel_read_default_row_height (BiffQuery *q, ExcelSheet *sheet)
{
	guint16 const flags = MS_OLE_GET_GUINT16(q->data);
	guint16 const height = MS_OLE_GET_GUINT16(q->data+2);
	double height_units;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		printf ("Default row height 0x%x;\n", height);
		if (flags & 0x04)
			printf (" + extra space above;\n");
		if (flags & 0x08)
			printf (" + extra space below;\n");
	}
#endif
	height_units = get_row_height_units (height);
	sheet_row_set_default_size_pts (sheet->gnum_sheet, height_units, FALSE, FALSE);
}

/**
 * ms_excel_read_default_col_width:
 * @q 		A BIFF query
 * @sheet	The Excel sheet
 *
 * Processes a BIFF default column width (BIFF_DEFCOLWIDTH) record.
 * See: S59D73.HTM
 */
static void
ms_excel_read_default_col_width (BiffQuery *q, ExcelSheet *sheet)
{
	guint16 const width = MS_OLE_GET_GUINT16(q->data);
	double const char_width = get_base_char_width (sheet, TRUE);
	double col_width;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0)
		printf ("Default column width %hu characters\n", width);
#endif
	/*
	 * According to the tooltip the default width is 8.43 character widths
	 *   and does not include margins or the grid line.
	 * According to the saved data the default width is 8 character widths
	 *   includes the margins and grid line, but uses a different notion of
	 *   how big a char width is.
	 * According to saved data a column with the same size a the default has
	 *   9.00?? char widths.
	 */
	col_width = width * char_width;

	sheet_col_set_default_size_pts (sheet->gnum_sheet, col_width);
}

static void
ms_excel_read_guts (BiffQuery *q, ExcelSheet *sheet)
{
	g_return_if_fail (q->length == 8);
	{
		guint16 const row_gutter = MS_OLE_GET_GUINT16(q->data);
		guint16 const col_gutter = MS_OLE_GET_GUINT16(q->data+2);
		guint16 const max_row_outline = MS_OLE_GET_GUINT16(q->data+4);
		guint16 const max_col_outline = MS_OLE_GET_GUINT16(q->data+6);

		/* TODO : Use this information when gnumeric supports gutters,
		 *        and outlines */
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 1) {
			printf ("Gutters : row = %hu col = %hu\n"
				"Max outline : row %hu col %hu\n",
				row_gutter, col_gutter,
				max_row_outline, max_col_outline);
		}
#endif
	}
}

/*
 * No documentation exists for this record, but this makes
 * sense given the other record formats.
 */
static void
ms_excel_read_mergecells (BiffQuery *q, ExcelSheet *sheet)
{
	guint16 const num_merged = MS_OLE_GET_GUINT16(q->data);
	guint8 const *ptr = q->data + 2;
	int i;

	/* Do an anal sanity check. Just in case we've
	 * mis-interpreted the format.
	 */
	g_return_if_fail (q->length == 2+8*num_merged);

	for (i = 0 ; i < num_merged ; ++i, ptr += 8) {
		Range r;
		r.start.row = MS_OLE_GET_GUINT16(ptr);
		r.start.col = MS_OLE_GET_GUINT16(ptr+2);
		r.end.row = MS_OLE_GET_GUINT16(ptr+4);
		r.end.col = MS_OLE_GET_GUINT16(ptr+6);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 0) {
			printf ("EXCEL Unimplemented merge-cells : ");
			range_dump (&r);
		}
	}
#endif
}

static gboolean
ms_excel_read_sheet (ExcelSheet *sheet, BiffQuery *q, ExcelWorkbook *wb)
{
	PrintInformation *pi;

	g_return_val_if_fail (wb != NULL, FALSE);
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (sheet->gnum_sheet != NULL, FALSE);
	g_return_val_if_fail (sheet->gnum_sheet->print_info != NULL, FALSE);

	pi = sheet->gnum_sheet->print_info;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		printf ("----------------- '%s' -------------\n",
			sheet->gnum_sheet->name);
	}
#endif

	while (ms_biff_query_next (q)) {
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 5) {
			printf ("Opcode : 0x%x\n", q->opcode);
		}
#endif
		if (q->ms_op == 0x10) {
			puts ("EXCEL : How are we seeing chart records in a sheet ?");
			continue;
		}

		switch (q->ls_op) {
		case BIFF_EOF:
			style_optimize (sheet, -1, -1);
			return TRUE;

		case BIFF_OBJ: /* See: ms-obj.c and S59DAD.HTM */
			sheet->obj_queue = g_list_append (sheet->obj_queue,
							  ms_read_OBJ (q, wb, sheet->gnum_sheet));
			break;

		case BIFF_SELECTION:
		    ms_excel_read_selection (sheet, q);
		    break;

		case BIFF_MS_O_DRAWING:
		case BIFF_MS_O_DRAWING_GROUP:
		case BIFF_MS_O_DRAWING_SELECTION:
			ms_escher_parse (q, wb, sheet);
			break;

		case BIFF_NOTE: /* See: S59DAB.HTM */
		{
			guint16 row = EX_GETROW(q);
			guint16 col = EX_GETCOL(q);
			if ( sheet->ver >= eBiffV8 ) {
				guint16 options = MS_OLE_GET_GUINT16(q->data+4);
				guint16 obj_id  = MS_OLE_GET_GUINT16(q->data+6);
				guint16 author_len = MS_OLE_GET_GUINT16(q->data+8);
				char *author=biff_get_text(author_len%2?q->data+11:q->data+10,
							   author_len, NULL);
				int hidden;
				if (options&0xffd)
					printf ("FIXME: Error in options\n");
				hidden = (options&0x2)==0;
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_read_debug > 1) {
					printf ("Comment at %d,%d id %d options"
						" 0x%x hidden %d by '%s'\n",
						col, row, obj_id, options,
						hidden, author);
				}
#endif
			} else {
				guint16 author_len = MS_OLE_GET_GUINT16(q->data+4);
				char *text=biff_get_text(q->data+6, author_len, NULL);
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_read_debug > 1) {
					printf ("Comment at %d,%d '%s'\n",
						col, row, text);
				}
#endif

				if (row==0xffff && col==0)
					ms_excel_sheet_append_comment (sheet, col, row, text);
				else
					ms_excel_sheet_set_comment (sheet, col, row, text);
			}
			break;
		}

		case BIFF_PRINTGRIDLINES :
			pi->print_line_divisions = (MS_OLE_GET_GUINT16 (q->data) == 1);
			break;

		case BIFF_GRIDSET:
		case BIFF_INDEX:
		case BIFF_CALCMODE:
		case BIFF_CALCCOUNT:
		case BIFF_REFMODE:
		case BIFF_ITERATION:
		case BIFF_DELTA:
		case BIFF_SAVERECALC:
		case BIFF_PRINTHEADERS:
		case BIFF_COUNTRY:
		case BIFF_WSBOOL:
			break;

		case BIFF_DEFAULTROWHEIGHT:
			ms_excel_read_default_row_height (q, sheet);
			break;
		case BIFF_DEFCOLWIDTH:
			ms_excel_read_default_col_width (q, sheet);
			break;

		case BIFF_GUTS:
			ms_excel_read_guts (q, sheet);
			break;

		case BIFF_HEADER: /* FIXME : S59D94 */
		{
			if (q->length)
			{
				char *const str =
					biff_get_text (q->data+1,
						       MS_OLE_GET_GUINT8(q->data),
						       NULL);
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_read_debug > 2) {
					printf ("Header '%s'\n", str);
				}
#endif
				g_free(str);
			}
		}
		break;

		case BIFF_FOOTER: /* FIXME : S59D8D */
		{
			if (q->length) {
				char *const str =
					biff_get_text (q->data+1,
						       MS_OLE_GET_GUINT8(q->data),
						       NULL);
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_read_debug > 2) {
					printf ("Footer '%s'\n", str);
				}
#endif
				g_free(str);
			}
		}
		break;

		case BIFF_LEFT_MARGIN:
			margin_read (&pi->margins.left,   BIFF_GETDOUBLE (q->data));
			break;

		case BIFF_RIGHT_MARGIN:
			margin_read (&pi->margins.right,  BIFF_GETDOUBLE (q->data));
			break;

		case BIFF_TOP_MARGIN:
			margin_read (&pi->margins.top,    BIFF_GETDOUBLE (q->data));
			break;

		case BIFF_BOTTOM_MARGIN:
			margin_read (&pi->margins.bottom, BIFF_GETDOUBLE (q->data));
			break;

		case BIFF_OBJPROTECT:
		case BIFF_PROTECT:
		{
			/* TODO : Use this information when gnumeric supports protection */
			gboolean const is_protected = MS_OLE_GET_GUINT16(q->data) == 1;
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 1 && is_protected) {
				printf ("Sheet is protected\n");
			}
#endif
			break;
		}

		case BIFF_HCENTER:
			pi->center_horizontally = MS_OLE_GET_GUINT16 (q->data) == 0x1;
			break;

		case BIFF_VCENTER:
			pi->center_vertically   = MS_OLE_GET_GUINT16 (q->data) == 0x1;
			break;

		case BIFF_PLS:
			if (MS_OLE_GET_GUINT16 (q->data) == 0x00) {
				/*
				 * q->data + 2 -> q->data + q->length
				 * map to a DEVMODE structure see MS' SDK.
				 */
			} else if (MS_OLE_GET_GUINT16 (q->data) == 0x01) {
				/*
				 * q's data maps to a TPrint structure
				 * see Inside Macintosh Vol II p 149.
				 */
			}
			break;

		case BIFF_SETUP: /* See: S59DE3.HTM */
			if (q->length == 34) {
				guint16  grbit, fw, fh;
				gboolean valid;

				grbit = MS_OLE_GET_GUINT16 (q->data + 10);

/*				if ((grbit & 0x80) == 0x80) -- We probably can't map page->page accurately.
					printf ("Starting page number %d\n",
					MS_OLE_GET_GUINT16 (q->data +  4));*/

				valid = (grbit & 0x4) != 0x4;
				if (valid) {
					if ((grbit & 0x40) != 0x40) {
						if ((grbit & 0x2) == 0x2)
							pi->orientation = PRINT_ORIENT_VERTICAL;
						else
							pi->orientation = PRINT_ORIENT_HORIZONTAL;
					}
					/* FIXME: use this information */
/*					printf ("Paper size %d scale %d resolution %d vert. res. %d num copies %d\n",
						MS_OLE_GET_GUINT16 (q->data +  0),
						MS_OLE_GET_GUINT16 (q->data +  2),
						MS_OLE_GET_GUINT16 (q->data + 12),
						MS_OLE_GET_GUINT16 (q->data + 14),
						MS_OLE_GET_GUINT16 (q->data + 32));*/
				}

				if ((grbit & 0x1) == 0x1)
					pi->print_order = PRINT_ORDER_RIGHT_THEN_DOWN;
				else
					pi->print_order = PRINT_ORDER_DOWN_THEN_RIGHT;

				pi->print_black_and_white = (grbit & 0x8) == 0x8;
				pi->print_as_draft        = (grbit & 0x10) == 0x10;
				/* FIXME: print comments (grbit & 0x20) == 0x20 */

				fw = MS_OLE_GET_GUINT16 (q->data + 6);
				fh = MS_OLE_GET_GUINT16 (q->data + 8);
				if (fw > 0 && fh > 0) {
					pi->scaling.type = SIZE_FIT;
					pi->scaling.dim.cols = fw;
					pi->scaling.dim.rows = fh;
				}

				margin_read (&pi->margins.header, BIFF_GETDOUBLE (q->data + 16));
				margin_read (&pi->margins.footer, BIFF_GETDOUBLE (q->data + 24));
			} else
				g_warning ("Duff BIFF_SETUP");
			break;

		case BIFF_SCL:
			if (q->length == 4) {
				/* Zoom stored as an Egyptian fraction */
				double const zoom = (double)MS_OLE_GET_GUINT16 (q->data) /
					MS_OLE_GET_GUINT16 (q->data + 2);

				sheet_set_zoom_factor (sheet->gnum_sheet, zoom);
			} else
				g_warning ("Duff BIFF_SCL record");
			break;

		case BIFF_DIMENSIONS:	/* 2, NOT 1,10 */
			ms_excel_biff_dimensions (q, wb);
			break;

		case BIFF_SCENMAN:
		case BIFF_SCENARIO:
			break;

		case BIFF_MERGECELLS:
			ms_excel_read_mergecells (q, sheet);
			break;

		default:
			switch (q->opcode) {
			case BIFF_CODENAME :
				break;

			case BIFF_WINDOW2:
			if (q->length >= 10) {
				guint16 const options    = MS_OLE_GET_GUINT16(q->data + 0);
				guint16 top_row    = MS_OLE_GET_GUINT16(q->data + 2);
				guint16 left_col   = MS_OLE_GET_GUINT16(q->data + 4);

				sheet->gnum_sheet->display_formulas	= (options & 0x0001);
				sheet->gnum_sheet->display_zero		= (options & 0x0010);
				sheet->gnum_sheet->show_grid 		= (options & 0x0002);
				sheet->gnum_sheet->show_col_header =
				    sheet->gnum_sheet->show_row_header	= (options & 0x0004);

				/* The docs are unclear whether or not the counters
				 * are 0 or 1 based.  I'll assume 1 based but make the
				 * checks conditional just in case I'm wrong.
				 */
				if (top_row > 0) --top_row;
				if (left_col > 0) --left_col;
				sheet_make_cell_visible (sheet->gnum_sheet,
							 left_col, top_row);
#if 0
				if (!(options & 0x0008))
					printf ("Unsupported : frozen panes\n");
				if (!(options & 0x0020)) {
					guint32 const grid_color = MS_OLE_GET_GUINT32(q->data + 6);
					/* This is quicky fake code to express the idea */
					set_grid_and_header_color (get_color_from_index(grid_color));
#ifndef NO_DEBUG_EXCEL
					if (ms_excel_color_debug > 2) {
						printf ("Default grid & pattern color = 0x%hx\n",
							grid_color);
					}
#endif
				}
#endif

#ifndef NO_DEBUG_EXCEL
				if (ms_excel_read_debug > 0) {
					if (options & 0x0200)
						printf ("Sheet flag selected\n");
				}
#endif
				if (options & 0x0400)
					workbook_focus_sheet (sheet->gnum_sheet);
			}
#ifndef NO_DEBUG_EXCEL
			if (q->length >= 14) {
				guint16 const pageBreakZoom = MS_OLE_GET_GUINT16(q->data + 10);
				guint16 const normalZoom = MS_OLE_GET_GUINT16(q->data + 12);

				if (ms_excel_read_debug > 2)
					printf ("%hx %hx\n", normalZoom, pageBreakZoom);
#endif
			}
			break;

			default:
				ms_excel_read_cell (q, sheet);
				break;
			}
		}
	}

	printf ("Error, hit end without EOF\n");

	return FALSE;
}

Sheet *
biff_get_externsheet_name(ExcelWorkbook *wb, guint16 idx, gboolean get_first)
{
	BiffExternSheetData *bed;
	BiffBoundsheetData *bsd;
	guint16 index;

	if (idx>=wb->num_extern_sheets)
		return NULL;

	bed = &wb->extern_sheets[idx];
	index = get_first ? bed->first_tab : bed->last_tab;

	bsd = g_hash_table_lookup (wb->boundsheet_data_by_index, &index);
	if (!bsd || !bsd->sheet) {
		printf ("Duff sheet index %d\n", index);
		return NULL;
	}
	return bsd->sheet->gnum_sheet;
}

/*
 * see S59DEC.HM,
 */
static void
ms_excel_read_supporting_wb (BIFF_BOF_DATA *ver, BiffQuery *q)
{
	guint16	numTabs = MS_OLE_GET_GUINT16 (q->data);
	guint8 encodeType = MS_OLE_GET_GUINT8 (q->data + 2);

	/* TODO TODO TODO : Figure out what this is and if it is
	 * useful.  We always get a record length of FOUR ??
	 * even when there are supposedly 10 tabs...
	 * Is this related to EXTERNNAME???
	 */
#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0) {
		printf("Supporting workbook with %d Tabs\n", numTabs);
		printf("--> SUPBOOK VirtPath encoding = ");
		switch (encodeType)
		{
		case 0x00 : /* chEmpty */
			puts("chEmpty");
			break;
		case 0x01 : /* chEncode */
			puts("chEncode");
			break;
		case 0x02 : /* chSelf */
			puts("chSelf");
			break;
		default :
			printf("Unknown/Unencoded ??(%x '%c') %d\n",
			       encodeType, encodeType, q->length);
		};
		dump_biff (q);
	}
#endif

#if 0
	for (data = q->data + 2; numTabs-- > 0; ) {
		char *	name;
		guint32 byte_length, slen;
		slen = (guint32) MS_OLE_GET_GUINT16 (data);
		name = biff_get_text (data += 2, slen, &byte_length);
		puts(name);
	}
#endif
}

int
ms_excel_read_workbook (CommandContext *context, Workbook *workbook,
			MsOle *file)
{
	ExcelWorkbook *wb = NULL;
	MsOleStream *stream;
	MsOleErr     result;
	BiffQuery *q;
	BIFF_BOF_DATA *ver = 0;
	int current_sheet = 0;
	char *problem_loading = NULL;

	cell_deep_freeze_redraws ();

	/* Find that book file */
	/* Look for workbook before book so that we load the office97
	 * format rather than office5 when there are multiple streams.  */
	result = ms_ole_stream_open (&stream, file, "/", "workbook", 'r');
	if (result != MS_OLE_ERR_OK) {
		ms_ole_stream_close (&stream);

		result = ms_ole_stream_open (&stream, file, "/", "book", 'r');
		if (result != MS_OLE_ERR_OK) {
			ms_ole_stream_close (&stream);
			gnumeric_error_read
				(context,
				 _("No book or workbook streams found."));
			return -1;
		}
	}

	q = ms_biff_query_new (stream);

	while (problem_loading == NULL && ms_biff_query_next (q)) {
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 5) {
			printf ("Opcode : 0x%x\n", q->opcode);
		}
#endif

		/* Catch Oddballs
		 * The heuristic seems to be that 'version 1' BIFF types
		 * are unique and not versioned.
		 */
		if (0x1 == q->ms_op) {
			switch (q->opcode)
			{
			case BIFF_DSF :
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_read_debug > 0)
					printf ("Double Stream File : %s\n",
						(MS_OLE_GET_GUINT16(q->data) == 1)
						? "Yes" : "No");
#endif
				break;

			case BIFF_REFRESHALL :
				break;

			case BIFF_USESELFS :
				break;

			case BIFF_TABID :
				break;

			case BIFF_PROT4REV :
				break;

			case BIFF_PROT4REVPASS :
				break;

			case BIFF_CODENAME :
				/* TODO : What to do with this name ? */
				break;

			case BIFF_SUPBOOK:
				ms_excel_read_supporting_wb (ver, q);
				break;

			default:
				ms_excel_unexpected_biff (q,"Workbook");
			}
			continue;
		}

		switch (q->ls_op)
		{
		case BIFF_BOF:
		{
			/* The first BOF seems to be OK, the rest lie ? */
			eBiff_version vv = eBiffVUnknown;
			if (ver) {
				vv = ver->version;
				ms_biff_bof_data_destroy (ver);
			}
			ver = ms_biff_bof_data_new (q);
			if (vv != eBiffVUnknown)
				ver->version = vv;

			if (ver->type == eBiffTWorkbook) {
				wb = ms_excel_workbook_new (ver->version);
				wb->gnum_wb = workbook;
				if (ver->version >= eBiffV8) {
					guint32 ver = MS_OLE_GET_GUINT32 (q->data + 4);
					if (ver == 0x4107cd18)
						printf ("Excel 2000 ?\n");
					else
						printf ("Excel 97 +\n");
				} else if (ver->version >= eBiffV7)
					printf ("Excel 95\n");
				else if (ver->version >= eBiffV5)
					printf ("Excel 5.x\n");
				else if (ver->version >= eBiffV4)
					printf ("Excel 4.x\n");
				else if (ver->version >= eBiffV3)
					printf ("Excel 3.x\n");
				else if (ver->version >= eBiffV2)
					printf ("Excel 2.x\n");
			} else if (ver->type == eBiffTWorksheet) {
				BiffBoundsheetData *bsh;

				bsh = g_hash_table_lookup (wb->boundsheet_data_by_stream,
							   &q->streamPos);
				if (!bsh)
					printf ("Sheet offset in stream of %x not found in list\n", q->streamPos);
				else
				{
					ExcelSheet *sheet = ms_excel_workbook_get_sheet (wb, current_sheet);
					gboolean    kill  = FALSE;

					ms_excel_sheet_set_version (sheet, ver->version);
					if (ms_excel_read_sheet (sheet, q, wb)) {
						ms_excel_sheet_realize_objs (sheet);

						if (sheet_is_pristine (sheet->gnum_sheet) &&
						    current_sheet > 0)
							kill = TRUE;
					} else
						kill = TRUE;

					ms_excel_sheet_destroy_objs (sheet);

					if (kill) {
#ifndef NO_DEBUG_EXCEL
						if (ms_excel_read_debug > 1)
							printf ("Blank or broken sheet %d\n", current_sheet);
#endif
						if (ms_excel_workbook_detach (sheet->wb, sheet))
							ms_excel_sheet_destroy (sheet);
					}

					current_sheet++;
				}
			} else if (ver->type == eBiffTChart)
				ms_excel_chart (q, wb, ver);
			else if (ver->type == eBiffTVBModule ||
				 ver->type == eBiffTMacrosheet) {
				/* Skip contents of Module, or MacroSheet */
				if (ver->type != eBiffTMacrosheet)
					printf ("VB Module.\n");
				else
					printf ("XLM Macrosheet.\n");

				while (ms_biff_query_next (q) &&
				       q->opcode != BIFF_EOF)
				    ;
				if (q->opcode != BIFF_EOF)
					g_warning ("EXCEL : file format error.  Missing BIFF_EOF");
			} else
				printf ("Unknown BOF (%x)\n",ver->type);
		}
		break;

		case BIFF_EOF: /* FIXME: Perhaps we should finish here ? */
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 0)
				printf ("End of worksheet spec.\n");
#endif
			break;

		case BIFF_BOUNDSHEET:
			biff_boundsheet_data_new (wb, q, ver->version);
			break;

		case BIFF_PALETTE:
			wb->palette = ms_excel_palette_new (q);
			break;

		case BIFF_FONT:	        /* see S59D8C.HTM */
			biff_font_data_new (wb, q);
			break;

		case BIFF_PRECISION:
		{
#if 0
			/* FIXME : implement in gnumeric */
			/* state of 'Precision as Displayed' option */
			guint16 const data = MS_OLE_GET_GUINT16(q->data);
			gboolean const prec_as_displayed = (data == 0);
#endif
			break;
		}

		case BIFF_XF_OLD: /* see S59E1E.HTM */
		case BIFF_XF:
			biff_xf_data_new (wb, q, ver->version);
			break;

		case BIFF_SST: /* see S59DE7.HTM */
		{
			guint32 length, k, tot_len;
			guint8 *tmp;

#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug>4) {
				printf ("SST\n");
				ms_ole_dump (q->data, q->length);
			}
#endif
			wb->global_string_max = MS_OLE_GET_GUINT32(q->data+4);
			wb->global_strings = g_new (char *, wb->global_string_max);

			tmp = q->data + 8;
			tot_len = 8;
			for (k = 0; k < wb->global_string_max; k++)
			{
				guint32 byte_len;
				length = MS_OLE_GET_GUINT16 (tmp);
				wb->global_strings[k] = biff_get_text (tmp+2, length, &byte_len);
				if (wb->global_strings[k] == NULL)
				{
#ifdef NO_DEBUG_EXCEL
					if (ms_excel_read_debug > 4)
						printf ("Blank string in table at : 0x%x with length %d\n",
							k, byte_len);
#endif
					/* FIXME FIXME FIXME : This works for unicode strings.  What biff versions
					 *                     default to non-unicode ?? */
					/* FIXME FIXME FIXME : Will this problem happen anywhere else ?? */
					byte_len = 1;
				}
#ifdef NO_DEBUG_EXCEL
				else if (ms_excel_read_debug > 4)
					puts (wb->global_strings[k]);
#endif

				tmp += byte_len + 2;
				tot_len += byte_len + 2;

				if (tot_len > q->length) {
					/*
					   This means that somehow, the string table has been split
					   Perhaps it is too big for a single biff record, or
					   perhaps excel is just cussid. Either way a big pain.
					 */
					printf ("FIXME: Serious SST overrun lost %d of 0x%x strings!\n",
						wb->global_string_max - k, wb->global_string_max);
/*						printf ("Last string was '%s' with length 0x%x 0x%x of 0x%x > 0x%x\n",
						(wb->global_strings[k-1] ? wb->global_strings[k-1] : "(null)"),
						length, byte_len, tot_len, q->length);*/
					wb->global_string_max = k;
					break;
				}
			}
			break;
		}

		case BIFF_EXTSST: /* See: S59D84 */
			/* Can be safely ignored on read side */
			break;


		case BIFF_EXTERNSHEET: /* See: S59D82.HTM */
			++externsheet;
			if (ver->version == eBiffV8) {
				guint16 numXTI = MS_OLE_GET_GUINT16(q->data);
				guint16 cnt;

				wb->num_extern_sheets = numXTI;
				/* printf ("ExternSheet (%d entries)\n", numXTI);
				   ms_ole_dump (q->data, q->length); */

				wb->extern_sheets = g_new (BiffExternSheetData, numXTI+1);

				for (cnt = 0; cnt < numXTI; cnt++) {
					wb->extern_sheets[cnt].sup_idx   =  MS_OLE_GET_GUINT16(q->data + 2 + cnt*6 + 0);
					wb->extern_sheets[cnt].first_tab =  MS_OLE_GET_GUINT16(q->data + 2 + cnt*6 + 2);
					wb->extern_sheets[cnt].last_tab  =  MS_OLE_GET_GUINT16(q->data + 2 + cnt*6 + 4);
					/* printf ("SupBook : %d First sheet %d, Last sheet %d\n", MS_OLE_GET_GUINT16(q->data + 2 + cnt*6 + 0),
					   MS_OLE_GET_GUINT16(q->data + 2 + cnt*6 + 2), MS_OLE_GET_GUINT16(q->data + 2 + cnt*6 + 4)); */
				}
			} else
				printf ("ExternSheet : only BIFF8 supported so far...\n");
			break;

		case BIFF_FORMAT: /* S59D8E.HTM */
		{
			BiffFormatData *d = g_new(BiffFormatData,1);
			/*				printf ("Format data 0x%x %d\n", q->ms_op, ver->version);
							ms_ole_dump (q->data, q->length);*/
			if (ver->version == eBiffV7) { /* Totaly guessed */
				d->idx = MS_OLE_GET_GUINT16(q->data);
				d->name = biff_get_text(q->data+3, MS_OLE_GET_GUINT8(q->data+2), NULL);
			} else if (ver->version == eBiffV8) {
				d->idx = MS_OLE_GET_GUINT16(q->data);
				d->name = biff_get_text(q->data+4, MS_OLE_GET_GUINT16(q->data+2), NULL);
			} else { /* FIXME: mythical old papyrus spec. */
				d->name = biff_get_text(q->data+1, MS_OLE_GET_GUINT8(q->data), NULL);
				d->idx = g_hash_table_size (wb->format_data) + 0x32;
			}
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 2) {
				printf ("Format data : 0x%x == '%s'\n",
					d->idx, d->name);
			}
#endif
			g_hash_table_insert (wb->format_data, &d->idx, d);
			break;
		}

		case BIFF_EXTERNCOUNT: /* see S59D7D.HTM */
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 0) {
				printf ("%d external references\n",
					MS_OLE_GET_GUINT16(q->data));
			}
#endif
			break;

		case BIFF_CODEPAGE : /* DUPLICATE 42 */
		{
			/* This seems to appear within a workbook */
			/* MW: And on Excel seems to drive the display
			   of currency amounts.  */
			guint16 const codepage = MS_OLE_GET_GUINT16 (q->data);
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 0) {
				switch(codepage)
				{
				case 437 :
					/* US.  */
					puts("CodePage = IBM PC (US)");
					break;
				case 865 :
					puts("CodePage = IBM PC (Denmark/Norway)");
					break;
				case 0x8000 :
					puts("CodePage = Apple Macintosh");
					break;
				case 0x04e4 :
					puts("CodePage = ANSI (Microsoft Windows)");
					break;
				case 0x04b0 :
					/* FIXME FIXME : This is a guess */
					puts("CodePage = Auto");
					break;
				default :
					printf("CodePage = UNKNOWN(%hx)\n",
					       codepage);
				};
			}
#endif
			break;
		}

		case BIFF_OBJPROTECT :
		case BIFF_PROTECT :
		{
			/* TODO : Use this information when gnumeric supports protection */
			gboolean const is_protected = MS_OLE_GET_GUINT16(q->data) == 1;
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 1 && is_protected) {
				printf ("Sheet is protected\n");
			}
#endif
			break;
		}

		case BIFF_PASSWORD :
			break;

		case BIFF_FILEPASS :
			/* All records after this are encrypted */
			problem_loading = g_strdup (_("Password protected workbooks are not supported yet."));
			break;

		case (BIFF_STYLE & 0xff) : /* Why here and not as 93 */
			break;

		case BIFF_WINDOWPROTECT :
			break;

		case BIFF_EXTERNNAME :
		case (BIFF_NAME & 0xff) : /* Why here and not as 18 */
		{
			/* Create a pseudo-sheet */
			ExcelSheet sheet;
			sheet.wb = wb;
			sheet.ver = ver->version;
			sheet.gnum_sheet = NULL;
			sheet.shared_formulae = NULL;
			if (q->ls_op == (BIFF_EXTERNNAME&0xff))
				ms_excel_externname (q, &sheet);
			else
				ms_excel_read_name (q, &sheet);
			break;
		}

		case BIFF_BACKUP :
			break;
		case BIFF_WRITEACCESS :
			break;
		case BIFF_HIDEOBJ :
			break;
		case BIFF_FNGROUPCOUNT :
			break;
		case BIFF_MMS :
			break;

		case BIFF_OBPROJ :
			/* Flags that the project has some VBA */
			break;

		case BIFF_BOOKBOOL :
			break;
		case BIFF_COUNTRY :
			break;

		case BIFF_INTERFACEHDR :
			break;

		case BIFF_INTERFACEEND :
			break;

		case BIFF_1904 : /* 0, NOT 1 */
			if (MS_OLE_GET_GUINT16(q->data) == 1)
				printf ("EXCEL : Warning workbook uses unsupported 1904 Date System\n"
					"dates will be incorrect\n");
			break;

		case BIFF_WINDOW1 : /* 0 NOT 1 */
			if (q->length >= 16) {
#if 0
				/* In 1/20ths of a point */
				guint16 const xPos    = MS_OLE_GET_GUINT16(q->data + 0);
				guint16 const yPos    = MS_OLE_GET_GUINT16(q->data + 2);
#endif
				guint16 const width   = MS_OLE_GET_GUINT16(q->data + 4);
				guint16 const height  = MS_OLE_GET_GUINT32(q->data + 6);
				guint16 const options = MS_OLE_GET_GUINT32(q->data + 8);
#if 0
				guint16 const selTab  = MS_OLE_GET_GUINT32(q->data + 10);
				guint16 const firstTab= MS_OLE_GET_GUINT32(q->data + 12);
				guint16 const tabsSel = MS_OLE_GET_GUINT32(q->data + 14);

				/* (width of tab)/(width of horizontal scroll bar) / 1000 */
				guint16 const ratio   = MS_OLE_GET_GUINT32(q->data + 16);
#endif

				/* FIXME FIXME FIXME :
				 * We are sizing the window including the toolbars,
				 * menus, and notbook tabs.  Excel does not.
				 *
				 * NOTE : This is the size of the MDI sub-window, not the size of
				 * the containing excel window.
				 */
				workbook_view_set_size (wb->gnum_wb,
							.5 + width *
							application_display_dpi_get (TRUE) / (72. * 20.),
							.5 + height *
							application_display_dpi_get (FALSE) / (72. * 20.));

				if (options & 0x0001)
					printf ("Unsupported : Hidden workbook\n");
				if (options & 0x0002)
					printf ("Unsupported : Iconic workbook\n");
				wb->gnum_wb->show_horizontal_scrollbar = (options & 0x0008);
				wb->gnum_wb->show_vertical_scrollbar = (options & 0x0010);
				wb->gnum_wb->show_notebook_tabs = (options & 0x0020);
				workbook_view_pref_visibility (wb->gnum_wb);
			}
			break;

		case BIFF_SELECTION : /* 0, NOT 10 */
			break;

		case BIFF_DIMENSIONS :	/* 2, NOT 1,10 */
			ms_excel_biff_dimensions (q, wb);
			break;

		case BIFF_OBJ: /* See: ms-obj.c and S59DAD.HTM */
			/* FIXME : What does it mean to have an object
			 * outside a sheet ???? */
			ms_obj_realize(ms_read_OBJ (q, wb, NULL),
				       wb, NULL);
			break;

		case BIFF_SCL :
			break;

		case BIFF_MS_O_DRAWING:
		case BIFF_MS_O_DRAWING_GROUP:
		case BIFF_MS_O_DRAWING_SELECTION:
			ms_escher_parse (q, wb, NULL);
			break;

		case BIFF_ADDMENU :
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 1) {
				printf ("%smenu with %d sub items",
					(MS_OLE_GET_GUINT8(q->data+6) == 1) ? "" : "Placeholder ",
					MS_OLE_GET_GUINT8(q->data+5));
			}
#endif
			break;

		default:
			ms_excel_unexpected_biff (q,"Workbook");
			break;
		}
		/* NO Code here unless you modify the handling
		 * of Odd Balls Above the switch
		 */
	}
	ms_biff_query_destroy (q);
	if (ver)
		ms_biff_bof_data_destroy (ver);
	ms_ole_stream_close (&stream);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		printf ("finished read\n");
	}
#endif
#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0
	    || ms_excel_formula_debug > 0
	    || ms_excel_color_debug   > 0
	    || ms_excel_chart_debug > 0) {
		fflush (stdout);
	}
#endif

	cell_deep_thaw_redraws ();

	if (wb) {
		Workbook *workbook = wb->gnum_wb;

		/* Cleanup */
		ms_excel_workbook_destroy (wb);

		/* If we were forced to stop then the load failed */
		if (problem_loading != NULL) {
			gnumeric_error_read (context, problem_loading);
			return -1;
		}
		workbook_recalc (workbook);
		return 0;
	}

	gnumeric_error_read (context, _("Unable to locate valid MS Excel workbook"));
	return -1;
}
