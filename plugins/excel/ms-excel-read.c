/**
 * ms-excel.c: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *    Jody Goldberh (jgoldberg@home.com)
 *
 * (C) 1998, 1999 Michael Meeks, Jody Goldberg
 **/

#include <config.h>

#include "ms-formula-read.h"
#include "ms-excel-read.h"
#include "ms-obj.h"
#include "ms-chart.h"
#include "gnumeric-chart.h"
#include "ms-escher.h"
#include "print-info.h"
#include "selection.h"
#include "border.h"
#include "utils.h"	/* for cell_name */

/* #define NO_DEBUG_EXCEL */

/* Used in src/main.c to toggle debug messages on & off */
/*
 * As a convention
 * 0 = quiet, no experimental features.
 * 1 = enable experimental features
 * >1 increasing levels of detail.
 */
int ms_excel_read_debug    = 0;
int ms_excel_formula_debug = 0;
int ms_excel_color_debug   = 0;
int ms_excel_chart_debug   = 0;
extern int gnumeric_debugging;

/* Forward references */
static ExcelSheet *ms_excel_sheet_new       (ExcelWorkbook *wb,
					     const char *name);
static void        ms_excel_workbook_attach (ExcelWorkbook *wb,
					     ExcelSheet *ans);

#define STYLE_TOP		(MSTYLE_BORDER_TOP	    - MSTYLE_BORDER_TOP)
#define STYLE_BOTTOM		(MSTYLE_BORDER_BOTTOM	    - MSTYLE_BORDER_TOP)
#define STYLE_LEFT		(MSTYLE_BORDER_LEFT	    - MSTYLE_BORDER_TOP)
#define STYLE_RIGHT		(MSTYLE_BORDER_RIGHT	    - MSTYLE_BORDER_TOP)
#define STYLE_DIAGONAL		(MSTYLE_BORDER_DIAGONAL     - MSTYLE_BORDER_TOP)
#define STYLE_REV_DIAGONAL	(MSTYLE_BORDER_REV_DIAGONAL - MSTYLE_BORDER_TOP)

/* TODO : enable diagonal */
#define STYLE_ORIENT_MAX 4

void
ms_excel_unexpected_biff (BiffQuery *q, char const *const state)
{
#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0) {
		printf ("Unexpected Opcode in %s : 0x%x, length 0x%x\n",
			state, q->opcode, q->length);
		if (ms_excel_read_debug > 2)
			dump (q->data, q->length);
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
		dump (pos, length);
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
		(*byte_length) += 2;
		printf ("FIXME: rich string support unimplemented: discarding %d runs\n", formatting_runs);
		(*byte_length) += formatting_runs*4; /* 4 bytes per */
		ptr+= 2;
	}
	if (ext_str) { /* NB this data always comes after the rich_str data */
		guint32 len_ext_rst = MS_OLE_GET_GUINT32(ptr); /* A byte length */
		(*byte_length) += 4 + len_ext_rst;
		ptr+= 4;
		printf ("FIXME: extended string support unimplemented: ignoring %d bytes\n", len_ext_rst);
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 4) {
		printf ("String len %d, byte length %d: %d %d %d:\n",
			length, (*byte_length), high_byte, rich_str, ext_str);
		dump (pos, *byte_length);
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
					dump (q->data, q->length);
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
	data = MS_OLE_GET_GUINT16 (q->data + 10);
	switch (data) {
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
		dump (bnd->v.store.data, bnd->v.store.len);
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
	}
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
			printf ("Creating default pallete\n");
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

StyleColor *
ms_excel_palette_get (ExcelPalette *pal, guint idx, StyleColor *contrast)
{
	g_assert (NULL != pal);

	/* NOTE : not documented but seems close
	 * If you find a normative reference please forward it.
	 *
	 * The color index field seems to use
	 *	8-63 = Palette index 0-55
	 *
	 *	0, 64, 127 = contrast ??
	 *	65 = White ??
	 */

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_color_debug > 4) {
		printf ("Color Index %d\n", idx);
	}
#endif
	if (idx == 0 || idx == 64 || idx == 127) {
		/* These seem to be some sort of automatic contract colors */
		if (contrast) {
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
			if (guess < (0x20000)) {
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_color_debug > 1) {
					puts("White");
				}
#endif
				return style_color_new (0xffff, 0xffff, 0xffff);
			}
		}
#ifndef NO_DEBUG_EXCEL
		else if (ms_excel_color_debug > 1) {
			puts("No contrast default to Black");
		}
#endif
		return style_color_new (0, 0, 0);
	} else if (idx == 65) {
		/* FIXME FIXME FIXME */
		/* These seem to be some sort of automatic contract colors */
		return style_color_new (0xffff, 0xffff, 0xffff);
	}

	idx -= 8;
	if (idx < pal->length && idx >= 0) {
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
			g_return_val_if_fail (pal->gnum_cols[idx], NULL);
		}
		style_color_ref (pal->gnum_cols[idx]);
		return pal->gnum_cols[idx];
	}
	return NULL;
}

static void
ms_excel_palette_destroy (ExcelPalette *pal)
{
	guint16 lp;

	g_free (pal->red);
	g_free (pal->green);
	g_free (pal->blue);
	for (lp=0;lp<pal->length;lp++)
		if (pal->gnum_cols[lp])
			style_color_unref (pal->gnum_cols[lp]);
	g_free (pal->gnum_cols);
	g_free (pal);
}

typedef struct _BiffXFData {
	guint16 font_idx;
	guint16 format_idx;
	StyleFormat *style_format;
	eBiff_hidden hidden;
	eBiff_locked locked;
	eBiff_xftype xftype;	/*  -- Very important field... */
	eBiff_format format;
	guint16 parentstyle;
	StyleHAlignFlags halign;
	StyleVAlignFlags valign;
	gboolean wrap;
	guint8 rotation;
	eBiff_eastern eastern;
	guint8 border_color[STYLE_ORIENT_MAX];
	StyleBorderType border_type[STYLE_ORIENT_MAX];
	eBiff_border_orientation border_orientation;
	StyleBorderType border_linestyle;
	guint8 fill_pattern_idx;
	guint8 pat_foregnd_col;
	guint8 pat_backgnd_col;
} BiffXFData;


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

static StyleColor *
ms_excel_get_stylefont (ExcelSheet *sheet, BiffXFData const *xf,
			MStyle *mstyle)
{
	BiffFontData const * fd = ms_excel_get_font (sheet, xf->font_idx);

	if (fd == NULL)
		return NULL;

	mstyle_set_font_name   (mstyle, fd->fontname);
	mstyle_set_font_size   (mstyle, fd->height / 20.0);
	mstyle_set_font_bold   (mstyle, fd->boldness >= 0x2bc);
	mstyle_set_font_italic (mstyle, fd->italic);

	return ms_excel_palette_get (sheet->wb->palette, fd->color_idx, NULL);
}

static BiffXFData const *
ms_excel_get_xf (ExcelSheet *sheet, int const xfidx)
{
	BiffXFData const *xf;
	GPtrArray const * const p = sheet->wb->XF_cell_records;

	g_return_val_if_fail (p && 0 <= xfidx && xfidx < p->len, NULL);
	xf = g_ptr_array_index (p, xfidx);

	g_return_val_if_fail (xf, NULL);
	/* FIXME : What is the difference between cell & style formats ?? */
	/* g_return_val_if_fail (xf->xftype == eBiffXCell, NULL); */
	return xf;
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

static void
ms_excel_set_xf (ExcelSheet *sheet, int col, int row, guint16 xfidx)
{
	BiffXFData const *xf = ms_excel_get_xf (sheet, xfidx);
	StyleColor *fore, *back, *basefore;
	int back_index;
	MStyle *mstyle;
	Range   range;

	g_return_if_fail (xf);

	mstyle = mstyle_new ();
	mstyle_set_align_v     (mstyle, xf->valign);
	mstyle_set_align_h     (mstyle, xf->halign);
	mstyle_set_fit_in_cell (mstyle, xf->wrap);

	basefore = ms_excel_get_stylefont (sheet, xf, mstyle);
	if (sheet->wb->palette) {
		int i;
 		for (i = 0; i < STYLE_ORIENT_MAX; i++) {
			MStyleBorder *border;
			border = border_fetch (xf->border_type [i],
					       ms_excel_palette_get (sheet->wb->palette,
								     xf->border_color[i],
								     NULL),
					       MSTYLE_BORDER_TOP + i);
			if (border)
				mstyle_set_border (mstyle, MSTYLE_BORDER_TOP + i, border);
		}
	}

	if (xf->style_format)
		mstyle_set_format (mstyle, xf->style_format->format);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_color_debug > 0) {
		printf ("%s : Pattern = %d\n",
			cell_name (col, row),
			xf->fill_pattern_idx);
	}
#endif

	if (!basefore) {
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_color_debug > 2) {
			printf ("Cell Color : '%s' : (%d, %d)\n",
				cell_name (col, row),
				xf->pat_foregnd_col, xf->pat_backgnd_col);
		}
#endif
		fore = ms_excel_palette_get (sheet->wb->palette,
					     xf->pat_foregnd_col, NULL);
		back_index = xf->pat_backgnd_col;
	} else {
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_color_debug > 2) {
			printf ("Cell Color : '%s' : (Fontcol, %d)\n",
				cell_name (col, row),
				xf->pat_foregnd_col);
		}
#endif
		fore = basefore;
		back_index = xf->pat_foregnd_col;
	}

	/* Use contrasting colour for background if the fill pattern is
	 * 0 (transparent)
	 */
	if (xf->fill_pattern_idx == 0)
		back_index = 0;
	back = ms_excel_palette_get (sheet->wb->palette, back_index, fore);

	g_return_if_fail (back && fore);

	mstyle_set_color (mstyle, MSTYLE_COLOR_FORE, fore);
	mstyle_set_color (mstyle, MSTYLE_COLOR_BACK, back);

	range.start.col = col;
	range.start.row = row;
	range.end       = range.start;

	sheet_style_attach (sheet->gnum_sheet, range, mstyle);

	style_optimize (sheet, col, row);
}

static StyleBorderType
biff_xf_map_border (int b)
{
	switch (b) {
 	case 0: /* None */
 		return BORDER_NONE;
 	case 1: /* Thin */
 		return BORDER_THIN;
 	case 2: /* Medium */
 		return BORDER_MEDIUM;
 	case 3: /* Dashed */
 		return BORDER_DASHED;
 	case 4: /* Dotted */
 		return BORDER_DOTTED;
 	case 5: /* Thick */
 		return BORDER_THICK;
 	case 6: /* Double */
 		return BORDER_DOUBLE;
 	case 7: /* Hair */
 		return BORDER_HAIR;
 	case 8: /* Medium Dashed */
 		return BORDER_MEDIUM_DASH;
 	case 9: /* Dash Dot */
 		return BORDER_DASH_DOT;
 	case 10: /* Medium Dash Dot */
 		return BORDER_MEDIUM_DASH_DOT;
 	case 11: /* Dash Dot Dot */
 		return BORDER_DASH_DOT_DOT;
 	case 12: /* Medium Dash Dot Dot */
 		return BORDER_MEDIUM_DASH_DOT_DOT;
 	case 13: /* Slanted Dash Dot*/
 		return BORDER_SLANTED_DASH_DOT;
 	}
  	printf ("Unknown border style %d\n", b);
 	return BORDER_NONE;
}

static int
excel_map_pattern_index_from_excel (int const i)
{
	static int const map_from_excel[] = {
		 0,
		 1,  3,  2,  4,  7,  8,
		 9, 10, 11, 12, 13, 14,
		15, 16, 17, 18,  5,  6
	};

	/* Default to Auto if out of range */
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
	xf->parentstyle = (data >> 4);

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
		xf->halign = HALIGN_JUSTIFY;
		/*
		 * xf->halign = HALIGN_CENTRE_ACROSS_SELECTION;
		 */
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
		case 1:
			xf->rotation = 255;	/*
						 * vertical letters no rotation
						 */
			break;
		case 2:
			xf->rotation = 90;	/*
						 * 90deg anti-clock
						 */
			break;
		case 3:
			xf->rotation = 180;	/*
						 * 90deg clock
						 */
			break;
		}
	}

	if (ver == eBiffV8) {
		/*
		 * FIXME: Got bored and stop implementing everything, there is just too much !
		 */
		data = MS_OLE_GET_GUINT16 (q->data + 8);
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
		switch (subdata) {
		case 0:
			xf->border_orientation = eBiffBONone;
			break;
		case 1:
			xf->border_orientation = eBiffBODiagDown;
			break;
		case 2:
			xf->border_orientation = eBiffBODiagUp;
			break;
		case 3:
			xf->border_orientation = eBiffBODiagBoth;
			break;
		}

		data = MS_OLE_GET_GUINT32 (q->data + 14);
		subdata = data;
		xf->border_color[STYLE_TOP] = (subdata & 0x7f);
		subdata = subdata >> 7;
		xf->border_color[STYLE_BOTTOM] = (subdata & 0x7f);
		subdata = subdata >> 7;
		xf->border_linestyle = biff_xf_map_border ((data & 0x01e00000) >> 21);
		xf->fill_pattern_idx =
			excel_map_pattern_index_from_excel ((data>>26) & 0x3f);

		data = MS_OLE_GET_GUINT16 (q->data + 18);
		xf->pat_foregnd_col = (data & 0x007f);
		xf->pat_backgnd_col = (data & 0x3f80) >> 7;
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_color_debug > 2) {
			printf("Color f=%x b=%x\n",
			       xf->pat_foregnd_col,
			       xf->pat_backgnd_col);
		}
#endif
	} else { /* Biff 7 */
		data = MS_OLE_GET_GUINT16 (q->data + 8);
		xf->pat_foregnd_col = (data & 0x007f);
		xf->pat_backgnd_col = (data & 0x1f80) >> 7;

		data = MS_OLE_GET_GUINT16 (q->data + 10);
		xf->fill_pattern_idx = data & 0x03f;
			excel_map_pattern_index_from_excel (data & 0x3f);
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
	}

	g_ptr_array_add (wb->XF_cell_records, xf);
#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 2) {
		printf ("XF : Fore %d, Back %d\n",
			xf->pat_foregnd_col, xf->pat_backgnd_col);
	}
#endif
}

static gboolean
biff_xf_data_destroy (BiffXFData *xf)
{
	if (xf->style_format)
		style_format_unref (xf->style_format);
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
		cell_set_text_simple (cell, "");
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
					dump (q->data+6, 8);
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
	ans->wb         = wb;
	ans->obj_queue  = NULL;

	ans->shared_formulae =
	    g_hash_table_new ((GHashFunc)biff_shared_formula_hash,
			      (GCompareFunc)biff_shared_formula_equal);

	ans->style_optimize.start.col = 0;
	ans->style_optimize.start.row = 0;
	ans->style_optimize.end.col   = 0;
	ans->style_optimize.end.row   = 0;

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
			cell_set_text_simple (cell, "");
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
	}
	for (idx=0;idx<wb->excel_sheets->len;idx++)
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
		for (lp=0;lp<wb->XF_cell_records->len;lp++)
			biff_xf_data_destroy (g_ptr_array_index (wb->XF_cell_records, lp));
	g_ptr_array_free (wb->XF_cell_records, TRUE);

	if (wb->name_data)
		for (lp=0;lp<wb->name_data->len;lp++)
			biff_name_data_destroy (g_ptr_array_index (wb->name_data, lp));
	g_ptr_array_free (wb->name_data, TRUE);

	for (lp=0;lp<wb->blips->len;lp++)
		ms_escher_blip_destroy (g_ptr_array_index(wb->blips, lp));
	g_ptr_array_free (wb->blips, TRUE);
	wb->blips = NULL;

	for (lp=0;lp<wb->charts->len;lp++)
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
		for (lp=0;lp<4;lp++) {
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
		dump (name_def_data, name_def_len);

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
		int i;
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
		for (i = lastcol; i >= firstcol ; --i) {
			ptr -= 2;
			ms_excel_sheet_insert_blank (sheet,
						   MS_OLE_GET_GUINT16 (ptr),
						   i, row);
		}
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

	/* See: S59DDB.HTM */
	case BIFF_ROW:
	{
		guint16 const row = MS_OLE_GET_GUINT16(q->data);
		guint16 const start_col = MS_OLE_GET_GUINT16(q->data+2);
		guint16 const end_col = MS_OLE_GET_GUINT16(q->data+4) - 1;
		guint16 const height = MS_OLE_GET_GUINT16(q->data+6);
		guint16 const flags = MS_OLE_GET_GUINT16(q->data+12);
		guint16 const xf = MS_OLE_GET_GUINT16(q->data+14) & 0xfff;
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 1)
			printf ("Row %d height 0x%x;\n", row+1, height);
#endif
		/* FIXME : the height is specified in 1/20 of a point.
		 * but we can not assume that 1pt = 1pixel.
		 * MS seems to assume that it is closer to 1point = .75 pixels
		 * verticaly.
		 */
		if ((height&0x8000) == 0)
			sheet_row_set_height (sheet->gnum_sheet, row,
					      height/(20 * .75), TRUE);

		if (flags & 0x80) {
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 1) {
				printf ("row %d has flags 0x%x a default style %hd from col %s - ",
					row+1, flags, xf, col_name(start_col));
				printf ("%s;\n", col_name(end_col));
			}
#endif
		}
		break;
	}

	/* See: S59D67.HTM */
	case BIFF_COLINFO:
	{
		int lp;
		int char_width = 1;
		BiffXFData const *xf = NULL;
		BiffFontData const *fd = NULL;
		guint16 const firstcol = MS_OLE_GET_GUINT16(q->data);
		guint16       lastcol  = MS_OLE_GET_GUINT16(q->data+2);
		guint16       width    = MS_OLE_GET_GUINT16(q->data+4);
		guint16 const cols_xf  = MS_OLE_GET_GUINT16(q->data+6);
		guint16 const options  = MS_OLE_GET_GUINT16(q->data+8);
		gboolean const hidden = (options & 0x0001) ? TRUE : FALSE;
#if 0
		gboolean const collapsed = (options & 0x1000) ? TRUE : FALSE;
		int const outline_level = (options >> 8) & 0x7;
#endif

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 1) {
			if (MS_OLE_GET_GUINT8(q->data+10) != 0)
				printf ("Odd Colinfo\n");
			printf ("Column Formatting from col %d to %d of width "
				"%f characters\n",
				firstcol, lastcol, width/256.0);
		}
#endif
		/*
		 * FIXME FIXME FIXME
		 * 1) As a default 12 seems seems to match the sheet I
		 *    calibrated against.
		 * 2) the docs say charwidth not height. Assume that
		 *    width = 1.2 * height ?
		 */
		if ((xf = ms_excel_get_xf (sheet, cols_xf)) != NULL &&
		    (fd = ms_excel_get_font (sheet, xf->font_idx)))
			char_width = 1.2 *fd->height / 20.;
		else
			char_width = 12.;

		if (width>>8 == 0) {
			if (hidden)
				printf ("FIXME: Hidden column unimplemented\n");
			else
				printf ("FIXME: 0 sized column ???\n");

			/* FIXME : Make the magic default col width a define or function somewhere */
			width = 62;
		} else
			/* NOTE : Do NOT use *= we need to do the width*char_width before the division */
			width = (width * char_width) / 256.;

		/* FIXME : the width is specified in points (1/72 of an inch)
		 * but we can not assume that 1pt = 1pixel.
		 * MS seems to assume that it is closer to 1 point = .7 pixels
		 * horizontally. (NOTE : this is different from vertically)
		 */
		width *= .70;

		/* NOTE : seems like this is inclusive firstcol, inclusive lastcol */
		if (lastcol >= SHEET_MAX_COLS)
			lastcol = SHEET_MAX_COLS-1;
		for (lp = firstcol ; lp <= lastcol ; ++lp)
			sheet_col_set_width (sheet->gnum_sheet, lp,
					     width);
		break;
	}

	/* See: S59DDA.HTM */
	case BIFF_RK:
	{
		Value *v = biff_get_rk(q->data+6);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 2) {
			printf ("RK number : 0x%x, length 0x%x\n", q->opcode, q->length);
			dump (q->data, q->length);
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
		dump (q->data, q->length); */

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
		sheet_selection_append_range (sheet->gnum_sheet,
					      start_col, start_row,
					      start_col, start_row,
					      end_col, end_row);
	}
	sheet_cursor_set (sheet->gnum_sheet,
			  act_col, act_row, act_col, act_row, act_col, act_row);
#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		printf ("Done selection\n");
	}
#endif
}

static gboolean
ms_excel_read_sheet (ExcelSheet *sheet, BiffQuery *q, ExcelWorkbook *wb)
{
	guint32 const blankSheetPos = q->streamPos + q->length + 4;
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
			if (q->streamPos == blankSheetPos) /* || sheet->blank) */ {
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_read_debug > 1)
					printf ("Blank sheet\n");
#endif
				if (ms_excel_workbook_detach (sheet->wb, sheet)) {
					ms_excel_sheet_destroy (sheet);
					sheet = NULL;
					return FALSE;
				} else
					printf ("Serious error detaching sheet '%s'\n",
						sheet->gnum_sheet->name);
			}
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
		case BIFF_GUTS:
		case BIFF_DEFAULTROWHEIGHT:
		case BIFF_COUNTRY:
		case BIFF_WSBOOL:
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
			/* TODO : What to do with this information ? */
			gboolean is_protected;
			is_protected = MS_OLE_GET_GUINT16(q->data) == 1;
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

		case BIFF_DEFCOLWIDTH:
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
		case BIFF_MERGECELLS:
			break;

		default:
			switch (q->opcode) {
			case BIFF_CODENAME :
				break;

			case BIFF_WINDOW2: /* FIXME: see S59E18.HTM */
			{
				int top_vis_row, left_vis_col;
				guint16 options;

				if (q->length < 6) {
					printf ("Duff window data");
					break;
				}

				options      = MS_OLE_GET_GUINT16(q->data + 0);
				top_vis_row  = MS_OLE_GET_GUINT16(q->data + 2);
				left_vis_col = MS_OLE_GET_GUINT16(q->data + 4);
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_read_debug > 0) {
					if (options & 0x0001)
						printf ("FIXME: Sheet display formulae\n");
					if (options & 0x0200)
						printf ("Sheet flag selected\n");
				}
#endif
				if (options & 0x0400) {
					workbook_focus_sheet (sheet->gnum_sheet);
					/* printf ("Sheet top in workbook\n"); */
				}
				break;
			}
			default:
				ms_excel_read_cell (q, sheet);
				break;
			}
		}
	}
	if (ms_excel_workbook_detach (sheet->wb, sheet))
		ms_excel_sheet_destroy (sheet);
	sheet = NULL;
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

gboolean
ms_excel_read_workbook (Workbook *workbook, MsOle *file)
{
	ExcelWorkbook *wb = NULL;
	MsOleStream *stream;
	MsOleErr     result;
	BiffQuery *q;
	BIFF_BOF_DATA *ver = 0;
	int current_sheet = 0;

	cell_deep_freeze_redraws ();

	/* Find that book file */
	result = ms_ole_stream_open (&stream, file, "/", "workbook", 'r');
	if (result != MS_OLE_ERR_OK) {
		ms_ole_stream_close (&stream);

		result = ms_ole_stream_open (&stream, file, "/", "book", 'r');
		if (result != MS_OLE_ERR_OK) {
			ms_ole_stream_close (&stream);
			g_warning ("No Excel stream found: wierd");
			return FALSE;
		}
	}

	q = ms_biff_query_new (stream);

	while (ms_biff_query_next (q)) {
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
					ms_excel_sheet_set_version (sheet, ver->version);
					if (ms_excel_read_sheet   (sheet, q, wb)) {
						ms_excel_sheet_realize_objs (sheet);
						ms_excel_sheet_destroy_objs (sheet);
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
				dump (q->data, q->length);
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
				   dump (q->data, q->length); */

				wb->extern_sheets = g_new (BiffExternSheetData, numXTI+1);

				for (cnt=0; cnt < numXTI; cnt++) {
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
							dump (q->data, q->length);*/
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
			/*				printf ("Format data : %d == '%s'\n", d->idx, d->name); */
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
			/* TODO : What to do with this information ? */
			gboolean is_protected;
			is_protected = MS_OLE_GET_GUINT16(q->data) == 1;
			break;
		}
			break;

		case BIFF_PASSWORD :
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
				printf ("Uses 1904 Date System\n");
			break;

		case BIFF_WINDOW1 : /* 0 NOT 1 */
			break;

		case (BIFF_WINDOW2 & 0xff) :
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

	cell_deep_thaw_redraws ();

	if (wb) {
		workbook_recalc (wb->gnum_wb);
		ms_excel_workbook_destroy (wb);
		return TRUE;
	}
	return FALSE;
}
