/**
 * ms-excel.c: MS Excel support for Gnumeric
 *
 * Authors:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jgoldberg@home.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks, Jody Goldberg
 **/

#include <config.h>

#include "boot.h"
#include "ms-formula-read.h"
#include "ms-excel-read.h"
#include "ms-obj.h"
#include "ms-chart.h"
#include "gnumeric-chart.h"
#include "ms-escher.h"
#include "print-info.h"
#include "selection.h"
#include "parse-util.h"	/* for cell_name */
#include "ranges.h"
#include "expr-name.h"
#include "style.h"
#include "sheet-style.h"
#include "cell.h"
#include "sheet-merge.h"
#include "format.h"
#include "eval.h"
#include "value.h"
#include "gutils.h"
#include "sheet-object-cell-comment.h"
#include "application.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "ms-excel-util.h"
#include "ms-excel-xf.h"
#include "sheet-object-widget.h"
#include "sheet-object-graphic.h"
#ifdef ENABLE_BONOBO
#  include "sheet-object-container.h"
#endif

/* #define NO_DEBUG_EXCEL */

/* Forward references */
static ExcelSheet *ms_excel_sheet_new       (ExcelWorkbook *wb,
					     const char *name);
static void        ms_excel_workbook_attach (ExcelWorkbook *wb,
					     ExcelSheet *ans);
static void        margin_read (PrintUnit *pu, double val);

void
ms_excel_unexpected_biff (BiffQuery *q, char const *state,
			  int debug_level)
{
#ifndef NO_DEBUG_EXCEL
	if (debug_level > 0) {
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
{
	return *d*2;
}

static guint
biff_guint32_hash (const guint32 *d)
{
	return *d*2;
}

static gint
biff_guint16_equal (const guint16 *a, const guint16 *b)
{
	if (*a == *b)
		return 1;
	return 0;
}
static gint
biff_guint32_equal (const guint32 *a, const guint32 *b)
{
	if (*a == *b)
		return 1;
	return 0;
}

/**
 * This returns whether there is a header byte
 * and sets various flags from it
 **/
static gboolean
biff_string_get_flags (const guint8 *ptr,
		       gboolean *word_chars,
		       gboolean *extended,
		       gboolean *rich)
{
	guint8 header;

	header = MS_OLE_GET_GUINT8 (ptr);
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

static void
get_xtn_lens (guint32 *pre_len, guint32 *end_len, const guint8 *ptr, gboolean ext_str, gboolean rich_str)
{
	*end_len = 0;
	*pre_len = 0;

	if (rich_str) { /* The data for this appears after the string */
		guint16 formatting_runs = MS_OLE_GET_GUINT16 (ptr);
		static int warned = FALSE;

		(*end_len) += formatting_runs * 4; /* 4 bytes per */
		(*pre_len) += 2;
		ptr        += 2;

		if (!warned)
			printf ("FIXME: rich string support unimplemented:"
				"discarding %d runs\n", formatting_runs);
		warned = TRUE;
	}
	if (ext_str) { /* NB this data always comes after the rich_str data */
		guint32 len_ext_rst = MS_OLE_GET_GUINT32 (ptr); /* A byte length */
		static int warned = FALSE;

		(*end_len) += len_ext_rst;
		(*pre_len) += 4;

		if (!warned)
			printf ("FIXME: extended string support unimplemented:"
				"ignoring %u bytes\n", len_ext_rst);
		warned = TRUE;
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
biff_get_text (const guint8 *pos, guint32 length, guint32 *byte_length)
{
	guint32 lp;
	char *ans;
	const guint8 *ptr;
	guint32 byte_len;
	gboolean header;
	gboolean high_byte;
	static gboolean high_byte_warned = FALSE;
	gboolean ext_str;
	gboolean rich_str;

	if (!byte_length)
		byte_length = &byte_len;
	*byte_length = 0;

	if (!length) {
		/* FIXME FIXME FIXME : What about the 1 byte for the header ?
		 *                     The length may be wrong in this case.
		 */
		return 0;
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 5) {
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

	{
		guint32 pre_len, end_len;

		get_xtn_lens (&pre_len, &end_len, ptr, ext_str, rich_str);
		ptr += pre_len;
		(*byte_length) += pre_len + end_len;
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

		if (high_byte) {
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

static char *
get_utf8_chars (const char *ptr, guint len, gboolean high_byte)
{
	int    i;
	char *ans = g_new (char, len + 1);

	for (i = 0; i < len; i++) {
		guint16 c;

		if (high_byte) {
			c = MS_OLE_GET_GUINT16 (ptr);
			ptr+=2;
			ans [i] = (char)c;
		} else {
			c = MS_OLE_GET_GUINT8 (ptr);
			ptr+=1;
			ans [i] = (char)c;
		}
	}
	ans [i] = '\0';

	return ans;
}

static guint32
sst_bound_check (BiffQuery *q, guint32 offset)
{
	if (offset >= q->length) {
		guint32 d = offset - q->length;
		guint16 opcode;

		if (!ms_biff_query_peek_next (q, &opcode) ||
		    opcode != BIFF_CONTINUE)
			return 0;

		if (!ms_biff_query_next (q))
			return 0;

		return d;
	} else
		return offset;
}

/*
 * NB. Whilst the string proper is split, and whilst we get several headers,
 * it seems that the attributes appear in a single block after the end
 * of the string, which may also be split over continues.
 */
static guint32
get_string (char **output, BiffQuery *q, guint32 offset, MsBiffVersion ver)
{
	guint32  new_offset;
	guint32  total_len;
	guint32  total_end_len;
	/* Will be localy scoped when gdb gets its act together */
		gboolean header;
		gboolean high_byte;
		gboolean ext_str = FALSE;
		gboolean rich_str = FALSE;
		guint32  chars_left;
		guint32  pre_len, end_len;
		guint32  get_len;
		char    *str;

	g_return_val_if_fail (q != NULL &&
			      q->data != NULL &&
			      output != NULL &&
			      offset < q->length, 0);

	*output       = NULL;
	total_len     = MS_OLE_GET_GUINT16 (q->data + offset);
	new_offset    = offset + 2;
	total_end_len = 0;

	do {
		new_offset = sst_bound_check (q, new_offset);

		header = biff_string_get_flags (q->data + new_offset,
						&high_byte,
						&ext_str,
						&rich_str);
		if (!header) {
			g_warning ("Seriously broken string with no header 0x%x", *(q->data + new_offset));
			ms_ole_dump (q->data + new_offset, q->length - new_offset);
			return 0;
		}

		new_offset++;

		get_xtn_lens (&pre_len, &end_len, q->data + new_offset, ext_str, rich_str);
		total_end_len += end_len;

		/* the - end_len is an educated guess based on insufficient data */
		chars_left = (q->length - new_offset - pre_len) / (high_byte?2:1);
		if (chars_left > total_len)
			get_len = total_len;
		else
			get_len = chars_left;
		total_len -= get_len;
		g_assert (get_len >= 0);

		/* FIXME: split this simple bit out of here, it makes more sense damnit */
		str = get_utf8_chars (q->data + new_offset + pre_len, get_len, high_byte);
		new_offset += pre_len + get_len * (high_byte?2:1);

		if (!(*output))
			*output = str;
		else {
			char *old_output = *output;
			*output = g_strconcat (*output, str, NULL);
			g_free (str);
			g_free (old_output);
		}

	} while (total_len > 0);

	return sst_bound_check (q, new_offset + total_end_len);
}

static void
read_sst (ExcelWorkbook *wb, BiffQuery *q, MsBiffVersion ver)
{
	guint32 offset;
	int     k;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug>4) {
		printf ("SST\n");
		ms_ole_dump (q->data, q->length);
	}
#endif
	wb->global_string_max = MS_OLE_GET_GUINT32 (q->data + 4);
	wb->global_strings = g_new (char *, wb->global_string_max);

	offset = 8;

	for (k = 0; k < wb->global_string_max; k++) {
		offset = get_string (&wb->global_strings [k], q, offset, ver);

		if (!wb->global_strings [k]) {
#ifdef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 4)
				printf ("Blank string in table at : 0x%x with length %d\n",
					k, byte_len);
#endif
		}
#ifdef NO_DEBUG_EXCEL
		else if (ms_excel_read_debug > 4)
			puts (wb->global_strings[k]);
#endif
	}
}

char const *
biff_get_error_text (const guint8 err)
{
	char const *buf;
	switch (err) {
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
MsBiffBofData *
ms_biff_bof_data_new (BiffQuery *q)
{
	MsBiffBofData *ans = g_new (MsBiffBofData, 1);

	if ((q->opcode & 0xff) == BIFF_BOF &&
	    (q->length >= 4)) {
		/*
		 * Determine type from boff
		 */
		switch (q->opcode >> 8) {
		case 0: ans->version = MS_BIFF_V2;
			break;
		case 2: ans->version = MS_BIFF_V3;
			break;
		case 4: ans->version = MS_BIFF_V4;
			break;
		case 8: /* More complicated */
		{
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 2) {
				printf ("Complicated BIFF version 0x%x\n",
					MS_OLE_GET_GUINT16 (q->data));
				ms_ole_dump (q->data, q->length);
			}
#endif
			switch (MS_OLE_GET_GUINT16 (q->data)) {
			case 0x0600: ans->version = MS_BIFF_V8;
				     break;
			case 0x500: /* * OR ebiff7 : FIXME ? !  */
				     ans->version = MS_BIFF_V7;
				     break;
			default:
				printf ("Unknown BIFF sub-number in BOF %x\n", q->opcode);
				ans->version = MS_BIFF_V_UNKNOWN;
			}
			break;
		}

		default:
			printf ("Unknown BIFF number in BOF %x\n", q->opcode);
			ans->version = MS_BIFF_V_UNKNOWN;
			printf ("Biff version %d\n", ans->version);
		}
		switch (MS_OLE_GET_GUINT16 (q->data + 2)) {
		case 0x0005:
			ans->type = MS_BIFF_TYPE_Workbook;
			break;
		case 0x0006:
			ans->type = MS_BIFF_TYPE_VBModule;
			break;
		case 0x0010:
			ans->type = MS_BIFF_TYPE_Worksheet;
			break;
		case 0x0020:
			ans->type = MS_BIFF_TYPE_Chart;
			break;
		case 0x0040:
			ans->type = MS_BIFF_TYPE_Macrosheet;
			break;
		case 0x0100:
			ans->type = MS_BIFF_TYPE_Workspace;
			break;
		default:
			ans->type = MS_BIFF_TYPE_Unknown;
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
		ans->version = MS_BIFF_V_UNKNOWN;
		ans->type = MS_BIFF_TYPE_Unknown;
	}

	return ans;
}

void
ms_biff_bof_data_destroy (MsBiffBofData *data)
{
	g_free (data);
}

/**
 * See S59D61.HTM
 **/
static void
biff_boundsheet_data_new (ExcelWorkbook *wb, BiffQuery *q, MsBiffVersion ver)
{
	BiffBoundsheetData *ans = g_new (BiffBoundsheetData, 1);

	if (ver != MS_BIFF_V5 &&/*
				 * Testing seems to indicate that Biff5 is compatibile with Biff7 here.
				 */
	    ver != MS_BIFF_V7 &&
	    ver != MS_BIFF_V8) {
		printf ("Unknown BIFF Boundsheet spec. Assuming same as Biff7 FIXME\n");
		ver = MS_BIFF_V7;
	}
	ans->streamStartPos = MS_OLE_GET_GUINT32 (q->data);
	switch (MS_OLE_GET_GUINT8 (q->data + 4)) {
	case 0: ans->type = MS_BIFF_TYPE_Worksheet;
		break;
	case 1: ans->type = MS_BIFF_TYPE_Macrosheet;
		break;
	case 2: ans->type = MS_BIFF_TYPE_Chart;
		break;
	case 6: ans->type = MS_BIFF_TYPE_VBModule;
		break;
	default:
		printf ("Unknown sheet type : %d\n", MS_OLE_GET_GUINT8 (q->data + 4));
		ans->type = MS_BIFF_TYPE_Unknown;
	}
	switch ((MS_OLE_GET_GUINT8 (q->data + 5)) & 0x3) {
	case 0: ans->hidden = MS_BIFF_H_VISIBLE;
		break;
	case 1: ans->hidden = MS_BIFF_H_HIDDEN;
		break;
	case 2: ans->hidden = MS_BIFF_H_VERY_HIDDEN;
		break;
	default:
		printf ("Unknown sheet hiddenness %d\n", (MS_OLE_GET_GUINT8 (q->data + 4)) & 0x3);
		ans->hidden = MS_BIFF_H_VISIBLE;
	}
	if (ver == MS_BIFF_V8) {
		int slen = MS_OLE_GET_GUINT16 (q->data + 6);
		ans->name = biff_get_text (q->data + 8, slen, NULL);
	} else {
		int slen = MS_OLE_GET_GUINT8 (q->data + 6);
		ans->name = biff_get_text (q->data + 7, slen, NULL);
	}

	/* TODO : find some documentation on this.
	 * It appears that if the name is null it defaults to Sheet%d ?
	 * However, we have only one test case and no docs.
	 */
	if (ans->name == NULL) {
		ans->name = g_strdup_printf (_("Sheet%d"),
			g_hash_table_size (wb->boundsheet_data_by_index));
	}

#if 0
	printf ("Blocksheet : '%s', %d:%d offset %lx\n", ans->name, ans->type,
		ans->hidden, ans->streamStartPos);
#endif
	ans->index = (guint16)g_hash_table_size (wb->boundsheet_data_by_index);
	g_hash_table_insert (wb->boundsheet_data_by_index,  &ans->index, ans);
	g_hash_table_insert (wb->boundsheet_data_by_stream, &ans->streamStartPos, ans);

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
		fd->script = MS_BIFF_F_S_NONE;
		break;
	case 1:
		fd->script = MS_BIFF_F_S_SUPER;
		break;
	case 2:
		fd->script = MS_BIFF_F_S_SUB;
		break;
	default:
		printf ("Unknown script %d\n", data);
		break;
	}

	data1 = MS_OLE_GET_GUINT8 (q->data + 10);
	switch (data1) {
	case 0:
		fd->underline = MS_BIFF_F_U_NONE;
		break;
	case 1:
		fd->underline = MS_BIFF_F_U_SINGLE;
		break;
	case 2:
		fd->underline = MS_BIFF_F_U_DOUBLE;
		break;
	case 0x21:
		fd->underline = MS_BIFF_F_U_SINGLE_ACC;
		break;
	case 0x22:
		fd->underline = MS_BIFF_F_U_DOUBLE_ACC;
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
/* 0x00 */	"General",
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
/* 0x0c */	"# ?/?",
/* 0x0d */	"# ??/??",
/* 0x0e */	"m/d/yy",
/* 0x0f */	"d-mmm-yy",
/* 0x10 */	"d-mmm",
/* 0x11 */	"mmm-yy",
/* 0x12 */	"h:mm AM/PM",
/* 0x13 */	"h:mm:ss AM/PM",
/* 0x14 */	"h:mm",
/* 0x15 */	"h:mm:ss",
/* 0x16 */	"m/d/yy h:mm",
	0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x17-0x24 reserved for intl versions */
/* 0x25 */	"#,##0_);(#,##0)",
/* 0x26 */	"#,##0_);[Red](#,##0)",
/* 0x27 */	"#,##0.00_);(#,##0.00)",
/* 0x28 */	"#,##0.00_);[Red](#,##0.00)",
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
	BiffFormatData *d = g_hash_table_lookup (wb->format_data,
						 &idx);
	if (d)
		ans = d->name;
	else if (idx <= 0x31) {
		ans = excel_builtin_formats[idx];
		if (!ans)
			printf ("Foreign undocumented format\n");
	} else
		printf ("Unknown format: 0x%x\n", idx);

	if (ans)
		return style_format_new_XL (ans, FALSE);
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
	int  sheet_index;	/* -1 indicates workbook level */
	enum { BNDStore, BNDName } type;
	union {
		NamedExpression *name;
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
		    int sheet_index,
		    guint8 const *formula, guint16 len,
		    gboolean external)
{
	BiffNameData *bnd = g_new (BiffNameData, 1);
	bnd->name        = name;
	bnd->sheet_index = sheet_index;
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
		return expr_tree_new_constant (value_new_string ("Unknown name"));

	}

	if (bnd->type == BNDStore && bnd->v.store.data) {
		ExprTree *tree = ms_excel_parse_formula (sheet->wb, sheet,
							 bnd->v.store.data,
							 0, 0, FALSE,
							 bnd->v.store.len,
							 NULL);

		bnd->type = BNDName;
		g_free (bnd->v.store.data);

		if (tree) {
			char *duff = "Some Error";
			bnd->v.name = (bnd->sheet_index > 0)
				? expr_name_add (NULL, sheet->gnum_sheet,
						  bnd->name, tree, &duff)
				: expr_name_add (sheet->wb->gnum_wb, NULL,
						 bnd->name, tree, &duff);

			if (!bnd->v.name)
				printf ("Error: '%s' for name '%s'\n", duff,
					bnd->name);
#ifndef NO_DEBUG_EXCEL
			else if (ms_excel_read_debug > 1) {
				ParsePos ep;
				parse_pos_init (&ep, NULL, sheet->gnum_sheet, 0, 0);
				printf ("Parsed name : '%s' = '%s'\n",
					bnd->name, tree
					? expr_tree_as_string (tree, &ep)
					: "error");
			}
#endif
		} else
			bnd->v.name = NULL; /* OK so it's a special 'AddIn' name */
	}

	if (bnd->type == BNDName && bnd->v.name)
		return expr_tree_new_name (bnd->v.name);
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
ms_excel_default_palette (void)
{
	static ExcelPalette *pal = NULL;

	if (!pal) {
		int entries = EXCEL_DEF_PAL_LEN;
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_color_debug > 3) {
			printf ("Creating default palette\n");
		}
#endif
		pal = g_new (ExcelPalette, 1);
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

	pal = g_new (ExcelPalette, 1);
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
			puts ("Contrast is White");
#endif
		return style_color_white ();
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_color_debug > 1)
		puts ("Contrast is Black");
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
		return style_color_black ();
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
	/* FIXME : when we can handle styles too deal with this correctly */
	/* g_return_val_if_fail (xf->xftype == MS_BIFF_X_CELL, NULL); */
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
		if (g_strcasecmp ((*p)[0], fontname) == 0) {
			res = (*p)[1];
			break;
		}

	return res;
}

static MStyle *
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
	if (xf->mstyle != NULL) {
		mstyle_ref (xf->mstyle);
		return xf->mstyle;
	}

	/* Create a new style and fill it in */
	mstyle = mstyle_new_default ();

	/* Format */
	if (xf->style_format)
		mstyle_set_format (mstyle, xf->style_format);

	/* Alignment */
	mstyle_set_align_v   (mstyle, xf->valign);
	mstyle_set_align_h   (mstyle, xf->halign);
	mstyle_set_wrap_text (mstyle, xf->wrap);
	mstyle_set_indent    (mstyle, xf->indent);
	/* mstyle_set_orientation (mstyle, ); */

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
		case MS_BIFF_F_U_SINGLE :
		case MS_BIFF_F_U_SINGLE_ACC :
			underline = UNDERLINE_SINGLE;
			break;

		case MS_BIFF_F_U_DOUBLE :
		case MS_BIFF_F_U_DOUBLE_ACC :
			underline = UNDERLINE_DOUBLE;
			break;

		case MS_BIFF_F_U_NONE :
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
	if (font_index == 127) {
		/* The font is auto.  Lets look for info elsewhere */
		if (back_index == 64 || back_index == 65 || back_index == 0) {
			/* Everything is auto default to black text/pattern on white */
			/* FIXME : This should use the 'Normal' Style */
			if (pattern_index == 64 || pattern_index == 65 || pattern_index == 0) {
				back_color = style_color_white ();
				font_color = style_color_black ();
				style_color_ref ((pattern_color = font_color));
			} else {
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
		} else {
			back_color = ms_excel_palette_get (sheet->wb->palette,
							   back_index);

			/* Contrast font to back */
			font_color = black_or_white_contrast (back_color);

			/* Pattern is auto contrast it to back */
			if (pattern_index == 64 || pattern_index == 65 || pattern_index == 0)
				style_color_ref ((pattern_color = font_color));
			else
				pattern_color =
					ms_excel_palette_get (sheet->wb->palette,
							      pattern_index);
		}
	} else {
		/* Use the font as a baseline */
		font_color = ms_excel_palette_get (sheet->wb->palette,
						   font_index);

		if (back_index == 64 || back_index == 65 || back_index == 0) {
			/* contrast back to font and pattern to back */
			if (pattern_index == 64 || pattern_index == 65 || pattern_index == 0) {
				/* Contrast back to font, and pattern to back */
				back_color = black_or_white_contrast (font_color);
				pattern_color = black_or_white_contrast (back_color);
			} else {
				pattern_color =
					ms_excel_palette_get (sheet->wb->palette,
							      pattern_index);

				/* Contrast back to pattern */
				back_color = black_or_white_contrast (pattern_color);
			}
		} else {
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
		MStyle *tmp = mstyle;
		MStyleElementType const t = MSTYLE_BORDER_TOP + i;
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

		mstyle_set_border (tmp, t,
				   style_border_fetch (xf->border_type [i],
						       color, t));
	}

	/* Set the cache (const_cast) */
	((BiffXFData *)xf)->mstyle = mstyle;
	mstyle_ref (mstyle);
	return xf->mstyle;
}

static void
ms_excel_set_xf (ExcelSheet *sheet, int col, int row, guint16 xfidx)
{
	MStyle *const mstyle  = ms_excel_get_style_from_xf (sheet, xfidx);
	if (mstyle == NULL)
		return;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 2) {
		printf ("%s!%s%d = xf(%d)\n", sheet->gnum_sheet->name_unquoted,
			col_name (col), row+1, xfidx);
	}
#endif

	sheet_style_set_pos (sheet->gnum_sheet, col, row, mstyle);
}

static void
ms_excel_set_xf_segment (ExcelSheet *sheet, int start_col, int end_col, int row, guint16 xfidx)
{
	Range   range;
	MStyle * const mstyle  = ms_excel_get_style_from_xf (sheet, xfidx);
	if (mstyle == NULL)
		return;

	range.start.col = start_col;
	range.start.row = row;
	range.end.col   = end_col;
	range.end.row   = row;
	sheet_style_set_range (sheet->gnum_sheet, &range, mstyle);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 2) {
		range_dump (&range, "");
		fprintf (stderr, " = xf(%d)\n", xfidx);
	}
#endif
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
			      i < (sizeof (map_from_excel)/sizeof (int)), 0);

	return map_from_excel[i];
}

/**
 * Parse the BIFF XF Data structure into a nice form, see S59E1E.HTM
 **/
static void
biff_xf_data_new (ExcelWorkbook *wb, BiffQuery *q, MsBiffVersion ver)
{
	BiffXFData *xf = g_new (BiffXFData, 1);
	guint32 data, subdata;

	xf->font_idx = MS_OLE_GET_GUINT16 (q->data);
	xf->format_idx = MS_OLE_GET_GUINT16 (q->data + 2);
	xf->style_format = (xf->format_idx > 0)
	    ? biff_format_data_lookup (wb, xf->format_idx) : NULL;

	data = MS_OLE_GET_GUINT16 (q->data + 4);
	xf->locked = (data & 0x0001) ? MS_BIFF_L_LOCKED : MS_BIFF_L_UNLOCKED;
	xf->hidden = (data & 0x0002) ? MS_BIFF_H_HIDDEN : MS_BIFF_H_VISIBLE;
	xf->xftype = (data & 0x0004) ? MS_BIFF_X_STYLE : MS_BIFF_X_CELL;
	xf->format = (data & 0x0008) ? MS_BIFF_F_LOTUS : MS_BIFF_F_MS;
	xf->parentstyle = (data & 0xfff0) >> 4;

	if (xf->xftype == MS_BIFF_X_CELL && xf->parentstyle != 0) {
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
	if (ver == MS_BIFF_V8)
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

	if (ver == MS_BIFF_V8) {
		/* FIXME : This code seems irrelevant for merging.
		 * The undocumented record MERGECELLS appears to be the correct source.
		 * Nothing seems to set the merge flags.
		 */
		static gboolean shrink_warn = TRUE;

		/* FIXME : What are the lower 8 bits Always 0 ?? */
		/* We need this to be able to support travel.xls */
		const guint16 data = MS_OLE_GET_GUINT16 (q->data + 8);
		gboolean const shrink = (data & 0x10) ? TRUE : FALSE;
		/* gboolean const merge = (data & 0x20) ? TRUE : FALSE; */

		xf->indent = data & 0x0f;

		if (shrink && shrink_warn) {
			shrink_warn = FALSE;
			g_warning ("EXCEL : Shrink to fit is not supported yet.");
		}

		subdata = (data & 0x00C0) >> 10;
		switch (subdata) {
		case 0:
			xf->eastern = MS_BIFF_E_CONTEXT;
			break;
		case 1:
			xf->eastern = MS_BIFF_E_LEFT_TO_RIGHT;
			break;
		case 2:
			xf->eastern = MS_BIFF_E_RIGHT_TO_LEFT;
			break;
		default:
			printf ("Unknown location %d\n", subdata);
			break;
		}
	} else
		xf->indent = 0;

	if (ver == MS_BIFF_V8) {/*
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
			printf ("Color f=0x%x b=0x%x pat=0x%x\n",
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
			printf ("Color f=0x%x b=0x%x pat=0x%x\n",
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
	xf->mstyle = NULL;

	g_ptr_array_add (wb->XF_cell_records, xf);
#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 2) {
		printf ("XF(%d) : Font %d, Format %d, Fore %d, Back %d, Pattern = %d\n",
			wb->XF_cell_records->len,
			xf->font_idx,
			xf->format_idx,
			xf->pat_foregnd_col,
			xf->pat_backgnd_col,
			xf->fill_pattern_idx);
	}
#endif
}

static gboolean
biff_xf_data_destroy (BiffXFData *xf)
{
	if (xf->style_format) {
		style_format_unref (xf->style_format);
		xf->style_format = NULL;
	}
	if (xf->mstyle) {
		mstyle_unref (xf->mstyle);
		xf->mstyle = NULL;
	}
	g_free (xf);
	return 1;
}

static void
ms_excel_sheet_set_version (ExcelSheet *sheet, MsBiffVersion ver)
{
	sheet->container.ver = ver;
}

static void
ms_excel_sheet_insert (ExcelSheet *sheet, int xfidx,
		       int col, int row, const char *text)
{
	Cell *cell;

	ms_excel_set_xf (sheet, col, row, xfidx);

	if (text) {
		cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);
		cell_set_value (cell, value_new_string (text), NULL);
	}
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

static ExprTree *
ms_excel_formula_shared (BiffQuery *q, ExcelSheet *sheet, Cell *cell)
{
	g_return_val_if_fail (ms_biff_query_next (q), NULL);

	if (q->ls_op == BIFF_SHRFMLA || q->ls_op == BIFF_ARRAY) {
		gboolean const is_array = (q->ls_op == BIFF_ARRAY);
		const guint16 array_row_first = MS_OLE_GET_GUINT16 (q->data + 0);
		const guint16 array_row_last = MS_OLE_GET_GUINT16 (q->data + 2);
		const guint8 array_col_first = MS_OLE_GET_GUINT8 (q->data + 4);
		const guint8 array_col_last = MS_OLE_GET_GUINT8 (q->data + 5);
		guint8 *data =
			q->data + (is_array ? 14 : 10);
		const guint16 data_len =
			MS_OLE_GET_GUINT16 (q->data + (is_array ? 12 : 8));
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
		sf->key.col = cell->pos.col; /* array_col_first; */
		sf->key.row = cell->pos.row; /* array_row_first; */
		sf->is_array = is_array;
		if (data_len > 0) {
			sf->data = g_new (guint8, data_len);
			memcpy (sf->data, data, data_len);
		} else
			sf->data = NULL;
		sf->data_len = data_len;

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 1) {
			printf ("Shared formula, extent %s:",
				cell_coord_name (array_col_first, array_row_first));
			printf ("%s\n",
				cell_coord_name (array_col_last, array_row_last));
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
						array_col_last, expr, FALSE);
		return expr;
	}

	printf ("EXCEL : unexpected record after a formula 0x%x in '%s'\n",
		q->opcode, cell_name (cell));
	return NULL;
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
	const guint16 xf_index = EX_GETXF (q);
	const guint16 col      = EX_GETCOL (q);
	const guint16 row      = EX_GETROW (q);
	const guint16 options  = MS_OLE_GET_GUINT16 (q->data+14);
	Cell *cell;
	ExprTree *expr;
	Value *val = NULL;

	/* Set format */
	ms_excel_set_xf (sheet, col, row, xf_index);

	/* Then fetch Cell */
	cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0)
		printf ("Formula in %s%d;\n", col_name (col), row+1);
#endif

	/* TODO TODO TODO : Wishlist
	 * We should make an array of minimum sizes for each BIFF type
	 * and have this checking done there.
	 */
	if (q->length < 22) {
		printf ("FIXME: serious formula error: "
			"invalid FORMULA (0x%x) record with length %d (should >= 22)\n",
			q->opcode, q->length);
		cell_set_value (cell, value_new_error (NULL, "Formula Error"), NULL);
		return;
	}
	if (q->length < (22 + MS_OLE_GET_GUINT16 (q->data+20))) {
		printf ("FIXME: serious formula error: "
			"supposed length 0x%x, real len 0x%x\n",
			MS_OLE_GET_GUINT16 (q->data+20), q->length);
		cell_set_value (cell, value_new_error (NULL, "Formula Error"), NULL);
		return;
	}

	/*
	 * Get the current value so that we can format, do this BEFORE handling
	 * shared/array formulas or strings in case we need to go to the next
	 * record
	 */
	if (MS_OLE_GET_GUINT16 (q->data+12) != 0xffff) {
		double const num = gnumeric_get_le_double (q->data+6);
		val = value_new_float (num);
	} else {
		const guint8 val_type = MS_OLE_GET_GUINT8 (q->data+6);
		switch (val_type) {
		case 0 : /* String */
			is_string = TRUE;
			break;

		case 1 : /* Boolean */
		{
			const guint8 v = MS_OLE_GET_GUINT8 (q->data+8);
			val = value_new_bool (v ? TRUE : FALSE);
			break;
		}

		case 2 : /* Error */
		{
			EvalPos ep;
			const guint8 v = MS_OLE_GET_GUINT8 (q->data+8);
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
					sheet->gnum_sheet->name_unquoted,
					cell_name (cell));
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

	expr = ms_excel_parse_formula (sheet->wb, sheet, (q->data + 22),
				       col, row,
				       FALSE, MS_OLE_GET_GUINT16 (q->data+20),
				       &array_elem);

	/* Error was flaged by parse_formula */
	if (expr == NULL && !array_elem)
		expr = ms_excel_formula_shared (q, sheet, cell);

	if (is_string) {
		guint16 code;
		if (ms_biff_query_peek_next (q, &code) && code == BIFF_STRING) {
			char *v = NULL;
			if (ms_biff_query_next (q)) {
				/*
				 * NOTE : the Excel developers kit docs are
				 *        WRONG.  There is an article that
				 *        clarifies the behaviour to be the std
				 *        unicode format rather than the pure
				 *        length version the docs describe.
				 */
				const guint16 len = MS_OLE_GET_GUINT16 (q->data);

				if (len > 0)
					v = biff_get_text (q->data + 2, len, NULL);
				else
					/*
					 * Pre-Biff8 seems to use len=0
					 * Should that be a string or an EMPTY?
					 */
					v = g_strdup ("");
			}
			if (v) {
				val = value_new_string (v);
				g_free (v);
			} else {
				EvalPos pos;
				val = value_new_error (eval_pos_init_cell (&pos, cell), "INVALID STRING");
				g_warning ("EXCEL : invalid STRING record");
			}
		} else {
			/*
			 * Docs say that there should be a STRING
			 * record here
			 */
			EvalPos pos;
			val = value_new_error (eval_pos_init_cell (&pos, cell), "MISSING STRING");
			g_warning ("EXCEL : missing STRING record");
		}
	}

	/* We MUST have a value */
	if (val == NULL) {
		EvalPos pos;
		val = value_new_error (eval_pos_init_cell (&pos, cell), "MISSING Value");
		g_warning ("EXCEL : Invalid state.  Missing Value?");
	}

	if (cell_is_array (cell)) {
		/*
		 * Array expressions were already stored in the
		 * cells (without recalc) handle either the first instance
		 * or the followers.
		 */
		if (expr == NULL && !array_elem) {
			g_warning ("EXCEL : How does cell %s%d have an array expression ?",
				   col_name (cell->pos.col),
				   cell->pos.row);
			cell_set_value (cell, val, NULL);
		} else
			cell_assign_value (cell, val, NULL);
	} else if (!cell_has_expr (cell)) {
		cell_set_expr_and_value (cell, expr, val, NULL, TRUE);
		expr_tree_unref (expr);
	} else {
		/*
		 * NOTE : Only the expression is screwed.
		 * The value and format can still be set.
		 */
		g_warning ("EXCEL : Shared formula problems");
		cell_set_value (cell, val, NULL);
	}

	/*
	 * 0x1 = AlwaysCalc
	 * 0x2 = CalcOnLoad
	 */
	if (options & 0x3)
		dependent_queue_recalc (CELL_TO_DEP (cell));
}

BiffSharedFormula *
ms_excel_sheet_shared_formula (ExcelSheet *sheet, int const col, int const row)
{
	BiffSharedFormulaKey k;
	k.col = col;
	k.row = row;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 5)
		printf ("FIND SHARED : %s%d\n", col_name (col), row+1);
#endif
	return g_hash_table_lookup (sheet->shared_formulae, &k);
}

/**
 * ms_sheet_obj_anchor_to_pos:
 * @points	Array which receives anchor coordinates in points
 * @obj         The object
 * @sheet	The sheet
 *
 * Converts anchor coordinates in Excel units to points. Anchor
 * coordinates are x and y of upper left and lower right corner. Each
 * is expressed as a pair: Row/cell number + position within cell as
 * fraction of cell dimension.
 *
 * NOTE: According to docs, position within cell is expressed as
 * 1/1024 of cell dimension. However, this doesn't seem to be true
 * vertically, for Excel 97. We use 256 for >= XL97 and 1024 for
 * preceding.
  */
static gboolean
ms_sheet_obj_anchor_to_pos (Sheet const * sheet, MsBiffVersion const ver,
			    guint8 const *raw_anchor,
			    Range *range, float offset[4])
{
	float const row_denominator = (ver >= MS_BIFF_V8) ? 256. : 1024.;
	int	i;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_object_debug > 0)
		printf ("%s\n", sheet->name_unquoted);
#endif

	/* Ignore the first 2 bytes.  What are they ? */
	/* Dec/1/2000 JEG : I have not researched it, but this may have some
	 * flags indicating whether or not the object is acnhored to the cell
	 */
	raw_anchor += 2;

	/* Words 0, 4, 8, 12 : The row/col of the corners */
	/* Words 2, 6, 10, 14 : distance from cell edge */
	for (i = 0; i < 4; i++, raw_anchor += 4) {
		int const pos  = MS_OLE_GET_GUINT16 (raw_anchor);
		int const nths = MS_OLE_GET_GUINT16 (raw_anchor + 2);

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_object_debug > 1) {
			printf ("%d/%d cell %s from ",
				nths, (i & 1) ? 256 : 1024,
				(i & 1) ? "heights" : "widths");
			if (i & 1)
				printf ("row %d;\n", pos + 1);
			else
				printf ("col %s (%d);\n", col_name (pos), pos);
		}
#endif

		if (i & 1) { /* odds are rows */
			offset [i] = nths / row_denominator;
			if (i == 1)
				range->start.row = pos;
			else
				range->end.row = pos;
		} else {
			offset [i] = nths / 1024.;
			if (i == 0)
				range->start.col = pos;
			else
				range->end.col = pos;
		}
	}

	return FALSE;
}

static gboolean
ms_sheet_obj_realize (MSContainer *container, MSObj *obj)
{
	float offsets[4];
	Range range;
	ExcelSheet *sheet;

	if (obj == NULL)
		return TRUE;

	g_return_val_if_fail (container != NULL, TRUE);
	if (!obj->anchor_set) {
		printf ("MISSING anchor for obj 0x%p\n", obj);
		return TRUE;
	}

	sheet = (ExcelSheet *)container;

	if (ms_sheet_obj_anchor_to_pos (sheet->gnum_sheet, container->ver,
					obj->raw_anchor, &range, offsets))
		return TRUE;

	if (obj->gnum_obj != NULL) {
		SheetObjectAnchor anchor_types [4] = {
			SO_ANCHOR_PTS_FROM_COLROW_START,
			SO_ANCHOR_PTS_FROM_COLROW_START,
			SO_ANCHOR_PTS_FROM_COLROW_START,
			SO_ANCHOR_PTS_FROM_COLROW_START
		};
		sheet_object_set_sheet (SHEET_OBJECT (obj->gnum_obj),
					sheet->gnum_sheet);
		sheet_object_range_set (SHEET_OBJECT (obj->gnum_obj), &range,
					offsets, anchor_types);
	}

	return FALSE;
}

static GtkObject *
ms_sheet_obj_create (MSContainer *container, MSObj *obj)
{
	SheetObject *so = NULL;
	Sheet  *sheet;

	if (obj == NULL)
		return NULL;

	g_return_val_if_fail (container != NULL, NULL);

	sheet = ((ExcelSheet *)container)->gnum_sheet;

	switch (obj->excel_type) {
	case 0x01 : so = sheet_object_line_new (FALSE); break; /* Line */
	case 0x02 : so = sheet_object_box_new (FALSE);  break; /* Box */
	case 0x03 : so = sheet_object_box_new (TRUE);   break; /* Oval */
	case 0x05 : so = sheet_object_box_new (FALSE);  break; /* Chart */
	case 0x06 : so = sheet_widget_label_new (sheet);    break; /* TextBox */
	case 0x07 : so = sheet_widget_button_new (sheet);   break; /* Button */
	case 0x08 :
		    /* Picture */
#ifdef ENABLE_BONOBO
		    so = sheet_object_container_new (sheet);
#else
		    so = NULL;
		    {
			    static gboolean needs_warn = TRUE;
			    if (needs_warn) {
				    needs_warn = FALSE;
				    g_warning ("Images are not supported in non-bonobo version");
			    }
		    }
#endif
		    break;
	case 0x0B : so = sheet_widget_checkbox_new (sheet); break; /* CheckBox*/
	case 0x0C : so = sheet_widget_radio_button_new (sheet); break; /* OptionButton */
	case 0x0E : so = sheet_widget_label_new (sheet);    break; /* Label */
	case 0x12 : so = sheet_widget_list_new (sheet);     break; /* List */
	case 0x14 : so = sheet_widget_combo_new (sheet);    break; /* Combo */

	case 0x19 : /* Comment */
		/* TODO : we'll need a special widget for this */
		return NULL;

	default :
		g_warning ("EXCEL : unhandled excel object of type %s (0x%x) id = %d\n",
			   obj->excel_type_name, obj->excel_type, obj->id);
		return NULL;
	}

	return so ? GTK_OBJECT (so) : NULL;
}

static ExprTree *
ms_sheet_parse_expr_internal (ExcelSheet *e_sheet, guint8 const *data, int length)
{
	ParsePos pp;
	Workbook *wb;
	Sheet *sheet;
	ExprTree *expr;
	char *tmp;

	g_return_val_if_fail (length > 0, NULL);

	sheet = e_sheet->gnum_sheet;
	wb = (sheet == NULL) ? e_sheet->wb->gnum_wb : NULL;
	expr = ms_excel_parse_formula (e_sheet->wb, e_sheet, data,
				       0, 0, FALSE, length, NULL);
	tmp = expr_tree_as_string (expr, parse_pos_init (&pp, wb, sheet, 0, 0));
	puts (tmp);
	g_free (tmp);

	return expr;
}

static ExprTree *
ms_sheet_parse_expr (MSContainer *container, guint8 const *data, int length)
{
	return ms_sheet_parse_expr_internal ((ExcelSheet *)container,
					     data, length);
}

/*
 * ms_excel_init_margins
 * @sheet ExcelSheet
 *
 * Excel only saves margins when any of the margins differs from the
 * default. So we must initialize the margins to Excel's defaults, which
 * are:
 * Top, bottom:    1 in   - 72 pt
 * Left, right:    3/4 in - 48 pt
 * Header, footer: 1/2 in - 36 pt
 */
static void
ms_excel_init_margins (ExcelSheet *sheet)
{
	PrintInformation *pi;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (sheet->gnum_sheet != NULL);
	g_return_if_fail (sheet->gnum_sheet->print_info != NULL);

	pi = sheet->gnum_sheet->print_info;
	margin_read (&pi->margins.top, 1.0);
	margin_read (&pi->margins.bottom, 1.0);
	margin_read (&pi->margins.left, 0.75);
	margin_read (&pi->margins.right, 0.75);
	margin_read (&pi->margins.header, 0.5);
	margin_read (&pi->margins.footer, 0.5);
}

static ExcelSheet *
ms_excel_sheet_new (ExcelWorkbook *wb, char const *sheet_name)
{
	static MSContainerClass const vtbl = {
		&ms_sheet_obj_realize,
		&ms_sheet_obj_create,
		&ms_sheet_parse_expr
	};

	ExcelSheet *res = g_new (ExcelSheet, 1);
	Sheet *sheet = workbook_sheet_by_name (wb->gnum_wb, sheet_name);

	if (sheet == NULL)
		sheet = sheet_new (wb->gnum_wb, sheet_name);

	res->wb         = wb;
	res->gnum_sheet = sheet;
	res->base_char_width         = -1;
	res->base_char_width_default = -1;
	res->shared_formulae         =
		g_hash_table_new ((GHashFunc)biff_shared_formula_hash,
				  (GCompareFunc)biff_shared_formula_equal);

	ms_excel_init_margins (res);
	ms_container_init (&res->container, &vtbl, &wb->container);

	return res;
}

static void
ms_excel_sheet_insert_val (ExcelSheet *sheet, int xfidx,
			   int col, int row, Value *v)
{
	Cell *cell;
	BiffXFData const *xf = ms_excel_get_xf (sheet, xfidx);

	g_return_if_fail (v);
	g_return_if_fail (sheet);
	g_return_if_fail (xf);

	ms_excel_set_xf (sheet, col, row, xfidx);
	cell = sheet_cell_fetch (sheet->gnum_sheet, col, row);
	cell_set_value (cell, v, xf->style_format);
}

static void
ms_excel_sheet_insert_blank (ExcelSheet *sheet, int xfidx,
			     int col, int row)
{
	g_return_if_fail (sheet);

	ms_excel_set_xf (sheet, col, row, xfidx);
}

static void
ms_excel_read_comment (BiffQuery *q, ExcelSheet *sheet)
{
	CellPos	pos;

	pos.row = EX_GETROW (q);
	pos.col = EX_GETCOL (q);

	if (sheet->container.ver >= MS_BIFF_V8) {
		guint16  options = MS_OLE_GET_GUINT16 (q->data+4);
		gboolean hidden = (options&0x2)==0;
		guint16  obj_id  = MS_OLE_GET_GUINT16 (q->data+6);
		guint16  author_len = MS_OLE_GET_GUINT16 (q->data+8);
		char *author;

		if (options&0xffd)
			printf ("FIXME: Error in options\n");

		author = biff_get_text (author_len%2 ? q->data+11:q->data+10,
					author_len, NULL);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 1) {
			printf ("Comment at %s%d id %d options"
				" 0x%x hidden %d by '%s'\n",
				col_name (pos.col), pos.row+1,
				obj_id, options, hidden, author);
		}
#endif

		g_free (author);
	} else {
		guint len = MS_OLE_GET_GUINT16 (q->data+4);
		GString *comment = g_string_sized_new (len);

		for (; len > 2048 ; len -= 2048) {
			guint16 opcode;

			g_string_append (comment, biff_get_text (q->data+6, 2048, NULL));

			if (!ms_biff_query_peek_next (q, &opcode) ||
			    opcode != BIFF_NOTE || !ms_biff_query_next (q) ||
			    EX_GETROW (q) != 0xffff || EX_GETCOL (q) != 0) {
				g_warning ("Invalid Comment record");
				g_string_free (comment, TRUE);
				return;
			}
		}
		g_string_append (comment, biff_get_text (q->data+6, len, NULL));

#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 2)
			printf ("Comment in %s%d : '%s'\n",
				col_name (pos.col), pos.row+1, comment->str);
#endif

		cell_set_comment (sheet->gnum_sheet, &pos, NULL, comment->str);
		g_string_free (comment, FALSE);
	}
}

static void
ms_excel_sheet_destroy (ExcelSheet *sheet)
{
	if (sheet->shared_formulae != NULL) {
		g_hash_table_foreach_remove (sheet->shared_formulae,
					     (GHRFunc)biff_shared_formula_destroy,
					     sheet);
		g_hash_table_destroy (sheet->shared_formulae);
		sheet->shared_formulae = NULL;
	}

	if (sheet->gnum_sheet) {
		sheet_destroy (sheet->gnum_sheet);
		sheet->gnum_sheet = NULL;
	}
	ms_container_finalize (&sheet->container);

	g_free (sheet);
}

static ExprTree *
ms_wb_parse_expr (MSContainer *container, guint8 const *data, int length)
{
	ExcelSheet dummy_sheet;

	dummy_sheet.container.ver = container->ver;
	dummy_sheet.wb = (ExcelWorkbook *)container;
	dummy_sheet.gnum_sheet = NULL;
	dummy_sheet.shared_formulae = NULL;
	return ms_sheet_parse_expr_internal (&dummy_sheet, data, length);
}

static ExcelWorkbook *
ms_excel_workbook_new (MsBiffVersion ver)
{
	static MSContainerClass const vtbl = {
		NULL, NULL,
		&ms_wb_parse_expr
	};

	ExcelWorkbook *ans = g_new (ExcelWorkbook, 1);

	ms_container_init (&ans->container, &vtbl, NULL);
	ans->container.ver = ver;

	ans->extern_sheets = NULL;
	ans->num_extern_sheets = 0;
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
	ans->charts           = g_ptr_array_new ();
	ans->format_data      = g_hash_table_new ((GHashFunc)biff_guint16_hash,
						  (GCompareFunc)biff_guint16_equal);
	ans->palette          = ms_excel_default_palette ();
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

	workbook_sheet_attach (wb->gnum_wb, ans->gnum_sheet, NULL);
	g_ptr_array_add (wb->excel_sheets, ans);
}

static gboolean
ms_excel_workbook_detach (ExcelWorkbook *wb, ExcelSheet *ans)
{
	int    idx = 0;

	if (ans->gnum_sheet) {
		if (!workbook_sheet_detach (wb->gnum_wb, ans->gnum_sheet))
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

	for (lp = 0; lp < wb->charts->len; lp++)
		gnumeric_chart_destroy (g_ptr_array_index (wb->charts, lp));
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

	ms_container_finalize (&wb->container);
	g_free (wb);
}

/**
 * Unpacks a MS Excel RK structure,
 **/
static Value *
biff_get_rk (const guint8 *ptr)
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
			tmp[lp+4]= (lp>0)?ptr[lp]: (ptr[lp]&0xfc);
			tmp[lp]=0;
		}

		answer = gnumeric_get_le_double (tmp);
		return value_new_float (type == eIEEEx100 ? answer / 100 : answer);
	}
	case eInt:
		return value_new_int ((number>>2));
	case eIntx100:
		number >>= 2;
		if ((number % 100) ==0)
			return value_new_int (number/100);
		else
			return value_new_float (number/100.0);
	}
	while (1) abort ();
}

/*
 * ms_excel_read_name :
 * read a Name.  The workbook must be present, the sheet is optional.
 */
static void
ms_excel_read_name (BiffQuery *q, ExcelWorkbook *wb, ExcelSheet *sheet)
{
	guint16 fn_grp_idx;
	guint16 flags          = MS_OLE_GET_GUINT16 (q->data);
#if 0
	guint8  kb_shortcut    = MS_OLE_GET_GUINT8  (q->data + 2);
#endif
	guint8  name_len       = MS_OLE_GET_GUINT8  (q->data + 3);
	guint16 name_def_len;
	guint8 *name_def_data;
	int     const sheet_idx      = MS_OLE_GET_GUINT16 (q->data + 8);
	guint8  const menu_txt_len   = MS_OLE_GET_GUINT8  (q->data + 10);
	guint8  const descr_txt_len  = MS_OLE_GET_GUINT8  (q->data + 11);
	guint8  const help_txt_len   = MS_OLE_GET_GUINT8  (q->data + 12);
	guint8  const status_txt_len = MS_OLE_GET_GUINT8  (q->data + 13);
	char *name, *menu_txt, *descr_txt, *help_txt, *status_txt;
	const guint8 *ptr;

#if 0
	dump_biff (q);
#endif
	/* FIXME FIXME FIXME : Offsets have moved alot between versions.
	 *                     track down the details */
	if (wb->container.ver >= MS_BIFF_V8) {
		name_def_len   = MS_OLE_GET_GUINT16 (q->data + 4);
		name_def_data  = q->data + 15 + name_len;
		ptr = q->data + 14;
	} else if (wb->container.ver >= MS_BIFF_V7) {
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
		switch (*ptr) {
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

	biff_name_data_new (wb, name, sheet_idx-1,
			    name_def_data, name_def_len, FALSE);

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
ms_excel_externname (BiffQuery *q, ExcelWorkbook *wb, ExcelSheet *sheet)
{
	char const *name;
	guint8 *defn;
	guint16 defnlen;
	if (wb->container.ver >= MS_BIFF_V7) {
		guint16 flags = MS_OLE_GET_GUINT8 (q->data);
		guint32 namelen = MS_OLE_GET_GUINT8 (q->data+6);

		name = biff_get_text (q->data+7, namelen, &namelen);
		defn    = q->data+7 + namelen;
		defnlen = MS_OLE_GET_GUINT16 (defn);
		defn += 2;

		switch (flags & 0x18) {
		case 0x00 : /* external name */
		    break;
		case 0x01 : /* DDE */
			printf ("FIXME : DDE links are no supported.\n"
				"Name '%s' will be lost.\n", name);
			return;
		case 0x10 : /* OLE */
			printf ("FIXME : OLE links are no supported.\n"
				"Name '%s' will be lost.\n", name);
			return;
		default :
			g_warning ("EXCEL : Invalid external name type. ('%s')", name);
			return;
		}
	} else { /* Ancient Papyrus spec. */
		static guint8 data[] = { 0x1c, 0x17 }; /* Error : REF */
		defn = data;
		defnlen = 2;
		name = biff_get_text (q->data+1,
				      MS_OLE_GET_GUINT8 (q->data), NULL);
	}

	biff_name_data_new (wb, name, -1, defn, defnlen, TRUE);
}

/**
 * base_char_width_for_read:
 * @sheet	the Excel sheet
 *
 * Measures base character width for column sizing.
 */
static double
base_char_width_for_read (ExcelSheet *sheet,
			  int xf_index, gboolean is_default)
{
	BiffXFData const *xf = ms_excel_get_xf (sheet, xf_index);
	BiffFontData const *fd = (xf != NULL)
		? ms_excel_get_font (sheet, xf->font_idx)
		: NULL;
	/* default to Arial 10 */
	char const * name = (fd != NULL) ? fd->fontname : "Arial";
	double const size = (fd != NULL) ? fd->height : 20.* 10.;

	return lookup_font_base_char_width_new (name, size, is_default);
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
	const guint16 row = MS_OLE_GET_GUINT16 (q->data);
#if 0
	/* Unnecessary info for now.
	 * FIXME : do we want to preallocate baed on this info ?
	 */
	const guint16 start_col = MS_OLE_GET_GUINT16 (q->data+2);
	const guint16 end_col = MS_OLE_GET_GUINT16 (q->data+4) - 1;
#endif
	const guint16 height = MS_OLE_GET_GUINT16 (q->data+6);
	const guint16 flags = MS_OLE_GET_GUINT16 (q->data+12);
	const guint16 flags2 = MS_OLE_GET_GUINT16 (q->data+14);
	const guint16 xf = flags2 & 0xfff;

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

	if (flags & 0x20)
		colrow_set_visibility (sheet->gnum_sheet, FALSE, FALSE, row, row);

	if (flags & 0x80) {
		ms_excel_set_xf_segment (sheet, 0, SHEET_MAX_COLS-1, row, xf);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 1) {
			printf ("row %d has flags 0x%x a default style %hd;\n",
				row+1, flags, xf);
		}
#endif
	}
	sheet_col_row_set_outline_level (sheet->gnum_sheet, row, FALSE,
					 (unsigned)(flags&0x7),
					 flags & 0x10);
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
	const guint16 firstcol = MS_OLE_GET_GUINT16 (q->data);
	guint16       lastcol  = MS_OLE_GET_GUINT16 (q->data+2);
	guint16       width    = MS_OLE_GET_GUINT16 (q->data+4);
	guint16 const xf       = MS_OLE_GET_GUINT16 (q->data+6);
	guint16 const options  = MS_OLE_GET_GUINT16 (q->data+8);
	gboolean hidden = (options & 0x0001) ? TRUE : FALSE;
	gboolean const collapsed = (options & 0x1000) ? TRUE : FALSE;
	unsigned const outline_level = (unsigned)((options >> 8) & 0x7);

	g_return_if_fail (firstcol < SHEET_MAX_COLS);

	/* Widths are quoted including margins
	 * If the width is less than the minimum margins something is lying
	 * hide it and give it default width.
	 * NOTE : These measurements do NOT correspond to what is
	 * shown to the user
	 */
	if (width >= 4) {
		col_width = base_char_width_for_read (sheet, xf, FALSE) *
			width / 256.;
	} else {
		if (width > 0) 
			hidden = TRUE;
		/* Columns are of default width */
		col_width = sheet->gnum_sheet->cols.default_style.size_pts;
	}

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		printf ("Column Formatting from col %d to %d of width "
			"%hu/256 characters (%f pts)\n", firstcol, lastcol,
			width, col_width);
		printf ("Options %hd, default style %hd from col %d to %d\n",
			options, xf, firstcol, lastcol);
	}
#endif
	/* NOTE : seems like this is inclusive firstcol, inclusive lastcol */
	if (lastcol >= SHEET_MAX_COLS)
		lastcol = SHEET_MAX_COLS-1;
	for (lp = firstcol; lp <= lastcol; ++lp) {
		sheet_col_set_size_pts (sheet->gnum_sheet, lp, col_width, TRUE);
		sheet_col_row_set_outline_level (sheet->gnum_sheet, lp, TRUE,
						 outline_level, collapsed);
	}

	/* TODO : We should associate a style region with the columns */
	if (hidden)
		colrow_set_visibility (sheet->gnum_sheet, TRUE, FALSE,
					firstcol, lastcol);
}

void
ms_excel_read_imdata (BiffQuery *q)
{
	guint16 op;

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 1) {
		const guint16 from_env = MS_OLE_GET_GUINT16 (q->data+2);
		const guint16 format = MS_OLE_GET_GUINT16 (q->data+2);

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
	while (ms_biff_query_peek_next (q, &op) && op == BIFF_CONTINUE)
		ms_biff_query_next (q);
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
 			ms_excel_unexpected_biff (q, "Cell", ms_excel_read_debug);
		};
		return;
	}

	switch (q->ls_op) {
	case BIFF_BLANK:
	{
		const guint16 xf = EX_GETXF (q);
		const guint16 col = EX_GETCOL (q);
		const guint16 row = EX_GETROW (q);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 0)
		    printf ("Blank in %s%d xf = 0x%x;\n", col_name (col), row+1, xf);
#endif
		ms_excel_sheet_insert_blank (sheet, xf, col, row);
		break;
	}

	case BIFF_MULBLANK:
	{
		/* S59DA7.HTM is extremely unclear, this is an educated guess */
		int firstcol = EX_GETCOL (q);
		int const row = EX_GETROW (q);
		const guint8 *ptr = (q->data + q->length - 2);
		int lastcol = MS_OLE_GET_GUINT16 (ptr);
		int i, range_end, prev_xf, xf_index;
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 0) {
			printf ("Cells in row %d are blank starting at col %s until col ",
				row+1, col_name (firstcol));
			printf ("%s;\n",
				col_name (lastcol));
		}
#endif
		if (lastcol < firstcol) {
			int const tmp = firstcol;
			firstcol = lastcol;
			lastcol = tmp;
		}

		range_end = i = lastcol;
		prev_xf = -1;
		do {
			ptr -= 2;
			xf_index = MS_OLE_GET_GUINT16 (ptr);
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 2) {
				printf (" xf (%s) = 0x%x",
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
		const guint16 xf = EX_GETXF (q);
		const guint16 col = EX_GETCOL (q);
		const guint16 row = EX_GETROW (q);
		char *txt = biff_get_text (q->data + 8, EX_GETSTRLEN (q), NULL);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 0)
		    printf ("Rstring in %s%d xf = 0x%x;\n", col_name (col), row+1, xf);
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
		Value *v = value_new_float (gnumeric_get_le_double (q->data + 6));
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 2) {
			printf ("Read number %g\n",
				gnumeric_get_le_double (q->data + 6));
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
		Value *v = biff_get_rk (q->data+6);
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
		const guint8 *ptr = q->data;
		Value *v;

/*		printf ("MULRK\n");
		ms_ole_dump (q->data, q->length); */

		row = MS_OLE_GET_GUINT16 (q->data);
		col = MS_OLE_GET_GUINT16 (q->data+2);
		ptr+= 4;
		lastcol = MS_OLE_GET_GUINT16 (q->data+q->length-2);
/*		g_assert ((lastcol-firstcol)*6 == q->length-6 */
		g_assert (lastcol>=col);
		while (col<=lastcol) {
			/* 2byte XF, 4 byte RK */
			v = biff_get_rk (ptr+2);
			ms_excel_sheet_insert_val (sheet, MS_OLE_GET_GUINT16 (ptr),
						   col, row, v);
			col++;
			ptr+= 6;
		}
		break;
	}
	/* See: S59D9D.HTM */
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

	case BIFF_LABELSST: /* See: S59D9E.HTM */
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

	case BIFF_IMDATA :
	    ms_excel_read_imdata (q);
	    break;

	case BIFF_STANDARDWIDTH :
		/* What the heck is the 'standard width dialog' ? */
		break;

	default:
		switch (q->opcode) {
		case BIFF_BOOLERR: /* S59D5F.HTM */
		{
			Value *v;
			const guint8 val = MS_OLE_GET_GUINT8 (q->data + 6);
			if (MS_OLE_GET_GUINT8 (q->data + 7)) {
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
 			ms_excel_unexpected_biff (q, "Cell", ms_excel_read_debug);
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

	sheet_selection_reset (sheet->gnum_sheet);
	for (refs = q->data + 9; num_refs > 0; refs += 6, num_refs--) {
		int const start_row = MS_OLE_GET_GUINT16 (refs + 0);
		int const start_col = MS_OLE_GET_GUINT8 (refs + 4);
		int const end_row   = MS_OLE_GET_GUINT16 (refs + 2);
		int const end_col   = MS_OLE_GET_GUINT8 (refs + 5);
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
	const guint16 flags = MS_OLE_GET_GUINT16 (q->data);
	const guint16 height = MS_OLE_GET_GUINT16 (q->data+2);
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
	sheet_row_set_default_size_pts (sheet->gnum_sheet, height_units);
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
	const guint16 width = MS_OLE_GET_GUINT16 (q->data);
	double col_width;

	/* Use the 'Normal' Style which is by definition the 0th */
	if (sheet->base_char_width_default <= 0)
		sheet->base_char_width_default =
			base_char_width_for_read (sheet, 0, TRUE);

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
	col_width = width * sheet->base_char_width_default;

	sheet_col_set_default_size_pts (sheet->gnum_sheet, col_width);
}

static void
ms_excel_read_guts (BiffQuery *q, ExcelSheet *sheet)
{
	g_return_if_fail (q->length == 8);

	sheet_col_row_gutter_pts (sheet->gnum_sheet,
				  MS_OLE_GET_GUINT16 (q->data+2),
				  MS_OLE_GET_GUINT16 (q->data+6),
				  MS_OLE_GET_GUINT16 (q->data),
				  MS_OLE_GET_GUINT16 (q->data+4));
}

/*
 * No documentation exists for this record, but this makes
 * sense given the other record formats.
 */
static void
ms_excel_read_mergecells (BiffQuery *q, ExcelSheet *sheet)
{
	const guint16 num_merged = MS_OLE_GET_GUINT16 (q->data);
	const guint8 *ptr = q->data + 2;
	int i;

	/* Do an anal sanity check. Just in case we've
	 * mis-interpreted the format.
	 */
	g_return_if_fail (q->length == 2+8*num_merged);

	for (i = 0 ; i < num_merged ; ++i, ptr += 8) {
		Range r;
		r.start.row = MS_OLE_GET_GUINT16 (ptr);
		r.end.row = MS_OLE_GET_GUINT16 (ptr+2);
		r.start.col = MS_OLE_GET_GUINT16 (ptr+4);
		r.end.col = MS_OLE_GET_GUINT16 (ptr+6);
		sheet_merge_add (NULL, sheet->gnum_sheet, &r, FALSE);
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 1) {
			range_dump (&r, "\n");
		}
	}
#endif
}

static void
ms_excel_biff_dimensions (BiffQuery *q, ExcelWorkbook *wb)
{
	guint32 first_row;
	guint32 last_row;
	guint16 first_col;
	guint16 last_col;

	/* What the heck was a 0x00 ? */
	if (q->opcode != 0x200)
		return;

	if (wb->container.ver >= MS_BIFF_V8)
	{
		first_row = MS_OLE_GET_GUINT32 (q->data);
		last_row  = MS_OLE_GET_GUINT32 (q->data+4);
		first_col = MS_OLE_GET_GUINT16 (q->data+8);
		last_col  = MS_OLE_GET_GUINT16 (q->data+10);
	} else
	{
		first_row = MS_OLE_GET_GUINT16 (q->data);
		last_row  = MS_OLE_GET_GUINT16 (q->data+2);
		first_col = MS_OLE_GET_GUINT16 (q->data+4);
		last_col  = MS_OLE_GET_GUINT16 (q->data+6);
	}

	if (ms_excel_chart_debug > 0)
		printf ("Dimension = %s%d:%s%d\n",
			col_name (first_col), first_row+1,
			col_name (last_col), last_row+1);
}

static MSContainer *
sheet_container (ExcelSheet *sheet)
{
	ms_container_set_blips (&sheet->container, sheet->wb->container.blips);
	return &sheet->container;
}

static gboolean
ms_excel_read_PROTECT (BiffQuery *q, char const *obj_type)
{
	/* TODO : Use this information when gnumeric supports protection */
	gboolean is_protected = TRUE;

	/* MS Docs fail to mention that in some stream this
	 * record can have size zero.  I assume the in that
	 * case its existence is the flag.
	 */
#ifndef NO_DEBUG_EXCEL
	if (q->length > 0)
		is_protected = (1 == MS_OLE_GET_GUINT16 (q->data));
	if (ms_excel_read_debug > 1 && is_protected)
		printf ("%s is protected\n", obj_type);
#endif
	return is_protected;
}

static gboolean
ms_excel_read_sheet (BiffQuery *q, ExcelWorkbook *wb, WorkbookView *wb_view,
		     ExcelSheet *sheet)
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
			sheet->gnum_sheet->name_unquoted);
	}
#endif

	while (ms_biff_query_next (q)) {
#ifndef NO_DEBUG_EXCEL
		if (ms_excel_read_debug > 5) {
			printf ("Opcode : 0x%x\n", q->opcode);
		}
#endif
		if (q->ms_op == 0x10) {
			/* HACK : it seems that in older versions of XL the
			 * charts did not have a wrapper object.  the first
			 * record in the sequence of chart records was a
			 * CHART_UNITS followed by CHART_CHART.  We play off of
			 * that.  When we encounter a CHART_units record we
			 * jump to the chart handler which then starts parsing
			 * at the NEXT record.
			 */
			if (q->opcode == BIFF_CHART_units)
				ms_excel_chart (q, sheet_container (sheet), sheet->container.ver);
			else
				puts ("EXCEL : How are we seeing chart records in a sheet ?");
			continue;
		}

		switch (q->ls_op) {
		case BIFF_EOF:
			return TRUE;

		case BIFF_OBJ: /* See: ms-obj.c and S59DAD.HTM */
			ms_read_OBJ (q, sheet_container (sheet));
			break;

		case BIFF_SELECTION:
		    ms_excel_read_selection (sheet, q);
		    break;

		case BIFF_MS_O_DRAWING:
		case BIFF_MS_O_DRAWING_GROUP:
		case BIFF_MS_O_DRAWING_SELECTION:
			ms_escher_parse (q, sheet_container (sheet));
			break;

		case BIFF_NOTE: /* See: S59DAB.HTM */
			ms_excel_read_comment (q, sheet);
			break;

		case BIFF_PRINTGRIDLINES :
			pi->print_grid_lines = (MS_OLE_GET_GUINT16 (q->data) == 1);
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
			if (q->length) {
				char *const str =
					biff_get_text (q->data+1,
						       MS_OLE_GET_GUINT8 (q->data),
						       NULL);
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_read_debug > 2) {
					printf ("Header '%s'\n", str);
				}
#endif
				g_free (str);
			}
		}
		break;

		case BIFF_FOOTER: /* FIXME : S59D8D */
		{
			if (q->length) {
				char *const str =
					biff_get_text (q->data+1,
						       MS_OLE_GET_GUINT8 (q->data),
						       NULL);
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_read_debug > 2) {
					printf ("Footer '%s'\n", str);
				}
#endif
				g_free (str);
			}
		}
		break;

		case BIFF_LEFT_MARGIN:
			margin_read (&pi->margins.left,   gnumeric_get_le_double (q->data));
			break;

		case BIFF_RIGHT_MARGIN:
			margin_read (&pi->margins.right,  gnumeric_get_le_double (q->data));
			break;

		case BIFF_TOP_MARGIN:
			margin_read (&pi->margins.top,    gnumeric_get_le_double (q->data));
			break;

		case BIFF_BOTTOM_MARGIN:
			margin_read (&pi->margins.bottom, gnumeric_get_le_double (q->data));
			break;

		case BIFF_OBJPROTECT:
		case BIFF_PROTECT:
			ms_excel_read_PROTECT (q, "Sheet");
			break;

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

				margin_read (&pi->margins.header, gnumeric_get_le_double (q->data + 16));
				margin_read (&pi->margins.footer, gnumeric_get_le_double (q->data + 24));
			} else
				g_warning ("Duff BIFF_SETUP");
			break;

		case BIFF_SCL:
			if (q->length == 4) {
				/* Zoom stored as an Egyptian fraction */
				double const zoom = (double)MS_OLE_GET_GUINT16 (q->data) /
					MS_OLE_GET_GUINT16 (q->data + 2);

				sheet_set_zoom_factor (sheet->gnum_sheet, zoom, FALSE, FALSE);
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

		case BIFF_EXTERNNAME:
			ms_excel_externname (q, sheet->wb, sheet);
			break;
		case (BIFF_NAME & 0xff) : /* Why here and not as 18 */
			ms_excel_read_name (q, sheet->wb, sheet);
			break;

		default:
			switch (q->opcode) {
			case BIFF_CODENAME :
				break;

			case BIFF_WINDOW2:
			if (q->length >= 10) {
				const guint16 options    = MS_OLE_GET_GUINT16 (q->data + 0);
				guint16 top_row    = MS_OLE_GET_GUINT16 (q->data + 2);
				guint16 left_col   = MS_OLE_GET_GUINT16 (q->data + 4);

				sheet->gnum_sheet->display_formulas	= (options & 0x0001);
				sheet->gnum_sheet->hide_zero		= !(options & 0x0010);
				sheet->gnum_sheet->hide_grid 		= !(options & 0x0002);
				sheet->gnum_sheet->hide_col_header =
				    sheet->gnum_sheet->hide_row_header	= !(options & 0x0004);

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
					guint32 const grid_color = MS_OLE_GET_GUINT32 (q->data + 6);
					/* This is quicky fake code to express the idea */
					set_grid_and_header_color (get_color_from_index (grid_color));
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
					wb_view_sheet_focus (wb_view,
							     sheet->gnum_sheet);
			}
#ifndef NO_DEBUG_EXCEL
			if (q->length >= 14) {
				const guint16 pageBreakZoom = MS_OLE_GET_GUINT16 (q->data + 10);
				const guint16 normalZoom = MS_OLE_GET_GUINT16 (q->data + 12);

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
biff_get_externsheet_name (ExcelWorkbook *wb, guint16 idx, gboolean get_first)
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
 * see S59DEC.HTM
 */
static void
ms_excel_read_supporting_wb (MsBiffBofData *ver, BiffQuery *q)
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
		printf ("Supporting workbook with %d Tabs\n", numTabs);
		printf ("--> SUPBOOK VirtPath encoding = ");
		switch (encodeType) {
		case 0x00 : /* chEmpty */
			puts ("chEmpty");
			break;
		case 0x01 : /* chEncode */
			puts ("chEncode");
			break;
		case 0x02 : /* chSelf */
			puts ("chSelf");
			break;
		default :
			printf ("Unknown/Unencoded ??(%x '%c') %d\n",
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
		puts (name);
	}
#endif
}

/*
 * See: S59E17.HTM
 */
static void
ms_excel_read_window1 (BiffQuery *q, WorkbookView *wb_view)
{
	if (q->length >= 16) {
#if 0
				/* In 1/20ths of a point */
		const guint16 xPos    = MS_OLE_GET_GUINT16 (q->data + 0);
		const guint16 yPos    = MS_OLE_GET_GUINT16 (q->data + 2);
#endif
		const guint16 width   = MS_OLE_GET_GUINT16 (q->data + 4);
		const guint16 height  = MS_OLE_GET_GUINT16 (q->data + 6);
		const guint16 options = MS_OLE_GET_GUINT16 (q->data + 8);
#if 0
		const guint16 selTab  = MS_OLE_GET_GUINT16 (q->data + 10);
		const guint16 firstTab= MS_OLE_GET_GUINT16 (q->data + 12);
		const guint16 tabsSel = MS_OLE_GET_GUINT16 (q->data + 14);
		
		/* (width of tab)/(width of horizontal scroll bar) / 1000 */
		const guint16 ratio   = MS_OLE_GET_GUINT16 (q->data + 16);
#endif
		
		/* 
		 * We are sizing the window including the toolbars,
		 * menus, and notbook tabs.  Excel does not.
		 *
		 * NOTE : This is the size of the MDI sub-window, not the size of
		 * the containing excel window.
		 */
		wb_view_preferred_size (wb_view,
					.5 + width * application_display_dpi_get (TRUE) / (72. * 20.),
					.5 + height * application_display_dpi_get (FALSE) / (72. * 20.));

		if (options & 0x0001)
			printf ("Unsupported : Hidden workbook\n");
		if (options & 0x0002)
			printf ("Unsupported : Iconic workbook\n");
		wb_view->show_horizontal_scrollbar = (options & 0x0008);
		wb_view->show_vertical_scrollbar = (options & 0x0010);
		wb_view->show_notebook_tabs = (options & 0x0020);
	}
}

static void
ms_excel_externsheet (BiffQuery const *q, ExcelWorkbook *wb, MsBiffBofData *ver)
{
	g_return_if_fail (ver != NULL);

	/* FIXME : Clean this cruft.  I do not know what it was for, but it is definitely
	 * broken.
	 */
	++externsheet;

	if (ver->version == MS_BIFF_V8) {
		guint16 numXTI = MS_OLE_GET_GUINT16 (q->data);
		guint16 cnt;

		wb->num_extern_sheets = numXTI;
#if 0
		printf ("ExternSheet (%d entries)\n", numXTI);
		ms_ole_dump (q->data, q->length);
#endif

		wb->extern_sheets = g_new (BiffExternSheetData, numXTI+1);

		for (cnt = 0; cnt < numXTI; cnt++) {
			wb->extern_sheets[cnt].sup_idx   =  MS_OLE_GET_GUINT16 (q->data + 2 + cnt*6 + 0);
			wb->extern_sheets[cnt].first_tab =  MS_OLE_GET_GUINT16 (q->data + 2 + cnt*6 + 2);
			wb->extern_sheets[cnt].last_tab  =  MS_OLE_GET_GUINT16 (q->data + 2 + cnt*6 + 4);
#if 0
			printf ("SupBook : %d First sheet %d, Last sheet %d\n",
				wb->extern_sheets[cnt].sup_idx,
				wb->extern_sheets[cnt].first_tab,
				wb->extern_sheets[cnt].last_tab);
#endif
		}
	} else
		printf ("ExternSheet : only BIFF8 supported so far...\n");
}

void
ms_excel_read_workbook (IOContext *context, WorkbookView *wb_view,
                        MsOle *file)
{
	ExcelWorkbook *wb = NULL;
	MsOleStream *stream;
	MsOleErr     result;
	BiffQuery *q;
	MsBiffBofData *ver = NULL;
	int current_sheet = 0;
	char *problem_loading = NULL;

	/* Find that book file */
	/* Look for workbook before book so that we load the office97
	 * format rather than office5 when there are multiple streams. */
	result = ms_ole_stream_open (&stream, file, "/", "workbook", 'r');
	if (result != MS_OLE_ERR_OK) {
		ms_ole_stream_close (&stream);

		result = ms_ole_stream_open (&stream, file, "/", "book", 'r');
		if (result != MS_OLE_ERR_OK) {
			ms_ole_stream_close (&stream);
			gnumeric_io_error_read (context,
				 _("No book or workbook streams found."));
			return;
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
			switch (q->opcode) {
			case BIFF_DSF :
#ifndef NO_DEBUG_EXCEL
				if (ms_excel_read_debug > 0)
					printf ("Double Stream File : %s\n",
						(MS_OLE_GET_GUINT16 (q->data) == 1)
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
				ms_excel_unexpected_biff (q,"Workbook", ms_excel_read_debug);
			}
			continue;
		}

		switch (q->ls_op) {
		case BIFF_BOF : {
			/* The first BOF seems to be OK, the rest lie ? */
			MsBiffVersion vv = MS_BIFF_V_UNKNOWN;
			if (ver) {
				vv = ver->version;
				ms_biff_bof_data_destroy (ver);
			}
			ver = ms_biff_bof_data_new (q);
			if (vv != MS_BIFF_V_UNKNOWN)
				ver->version = vv;

			if (ver->type == MS_BIFF_TYPE_Workbook) {
				wb = ms_excel_workbook_new (ver->version);
				wb->gnum_wb = wb_view_workbook (wb_view);
				if (ver->version >= MS_BIFF_V8) {
					guint32 ver = MS_OLE_GET_GUINT32 (q->data + 4);
					if (ver == 0x4107cd18)
						printf ("Excel 2000 ?\n");
					else
						printf ("Excel 97 +\n");
				} else if (ver->version >= MS_BIFF_V7)
					printf ("Excel 95\n");
				else if (ver->version >= MS_BIFF_V5)
					printf ("Excel 5.x\n");
				else if (ver->version >= MS_BIFF_V4)
					printf ("Excel 4.x\n");
				else if (ver->version >= MS_BIFF_V3)
					printf ("Excel 3.x\n");
				else if (ver->version >= MS_BIFF_V2)
					printf ("Excel 2.x\n");
			} else if (ver->type == MS_BIFF_TYPE_Worksheet) {
				BiffBoundsheetData *bsh =
					g_hash_table_lookup (wb->boundsheet_data_by_stream,
							     &q->streamPos);
				if (!bsh)
					printf ("Sheet offset in stream of %x not found in list\n", q->streamPos);
				else {
					ExcelSheet *sheet = ms_excel_workbook_get_sheet (wb, current_sheet);
					gboolean    kill  = FALSE;

					ms_excel_sheet_set_version (sheet, ver->version);
					if (ms_excel_read_sheet (q, wb, wb_view, sheet)) {
						ms_container_realize_objs (sheet_container (sheet));

#if 0
						/* DO NOT DO THIS.
						 * this looks good in principle but is a nightmare for
						 * performance.  It causes the entire book to be recalculated
						 * for everysheet that is removed.
						 */
						if (sheet_is_pristine (sheet->gnum_sheet) &&
						    current_sheet > 0)
							kill = TRUE;
#endif
					} else
						kill = TRUE;

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
			} else if (ver->type == MS_BIFF_TYPE_Chart)
				ms_excel_chart (q, &wb->container, ver->version);
			else if (ver->type == MS_BIFF_TYPE_VBModule ||
				 ver->type == MS_BIFF_TYPE_Macrosheet) {
				/* Skip contents of Module, or MacroSheet */
				if (ver->type != MS_BIFF_TYPE_Macrosheet)
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

		case BIFF_FONT: /* see S59D8C.HTM */
			biff_font_data_new (wb, q);
			break;

		case BIFF_PRECISION:
		{
#if 0
			/* FIXME : implement in gnumeric */
			/* state of 'Precision as Displayed' option */
			const guint16 data = MS_OLE_GET_GUINT16 (q->data);
			gboolean const prec_as_displayed = (data == 0);
#endif
			break;
		}

		case BIFF_XF_OLD: /* see S59E1E.HTM */
		case BIFF_XF:
			biff_xf_data_new (wb, q, ver->version);
			break;

		case BIFF_SST: /* see S59DE7.HTM */
			read_sst (wb, q, ver->version);
			break;

		case BIFF_EXTSST: /* See: S59D84 */
			/* Can be safely ignored on read side */
			break;


		case BIFF_EXTERNSHEET: /* See: S59D82.HTM */
			ms_excel_externsheet (q, wb, ver);
			break;

		case BIFF_FORMAT: /* S59D8E.HTM */
		{
			BiffFormatData *d = g_new (BiffFormatData,1);
			/*				printf ("Format data 0x%x %d\n", q->ms_op, ver->version);
							ms_ole_dump (q->data, q->length);*/
			if (ver->version == MS_BIFF_V7) { /* Totaly guessed */
				d->idx = MS_OLE_GET_GUINT16 (q->data);
				d->name = biff_get_text (q->data+3, MS_OLE_GET_GUINT8 (q->data+2), NULL);
			} else if (ver->version == MS_BIFF_V8) {
				d->idx = MS_OLE_GET_GUINT16 (q->data);
				d->name = biff_get_text (q->data+4, MS_OLE_GET_GUINT16 (q->data+2), NULL);
			} else { /* FIXME: mythical old papyrus spec. */
				d->name = biff_get_text (q->data+1, MS_OLE_GET_GUINT8 (q->data), NULL);
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
					MS_OLE_GET_GUINT16 (q->data));
			}
#endif
			break;

		case BIFF_CODEPAGE : /* DUPLICATE 42 */
		{
			/* This seems to appear within a workbook */
			/* MW: And on Excel seems to drive the display
			   of currency amounts.  */
			const guint16 codepage = MS_OLE_GET_GUINT16 (q->data);
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 0) {
				switch (codepage) {
				case 437 :
					/* US.  */
					puts ("CodePage = IBM PC (US)");
					break;
				case 865 :
					puts ("CodePage = IBM PC (Denmark/Norway)");
					break;
				case 0x8000 :
					puts ("CodePage = Apple Macintosh");
					break;
				case 0x04e4 :
					puts ("CodePage = ANSI (Microsoft Windows)");
					break;
				case 0x04b0 :
					/* FIXME FIXME : This is a guess */
					puts ("CodePage = Auto");
					break;
				default :
					printf ("CodePage = UNKNOWN(%hx)\n",
					       codepage);
				};
			}
#endif
			break;
		}

		case BIFF_OBJPROTECT :
		case BIFF_PROTECT :
			ms_excel_read_PROTECT (q, "Workbook");
			break;

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
			ms_excel_externname (q, wb, NULL);
			break;

		case (BIFF_NAME & 0xff) : /* Why here and not as 18 */
			ms_excel_read_name (q, wb, NULL);
			break;

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
			if (MS_OLE_GET_GUINT16 (q->data) == 1)
				printf ("EXCEL : Warning workbook uses unsupported 1904 Date System\n"
					"dates will be incorrect\n");
			break;

		case BIFF_WINDOW1 :
			ms_excel_read_window1 (q, wb_view);
			break;

		case BIFF_SELECTION : /* 0, NOT 10 */
			break;

		case BIFF_DIMENSIONS :	/* 2, NOT 1,10 */
			ms_excel_biff_dimensions (q, wb);
			break;

		case BIFF_OBJ: /* See: ms-obj.c and S59DAD.HTM */
			ms_read_OBJ (q, &wb->container);
			break;

		case BIFF_SCL :
			break;

		case BIFF_MS_O_DRAWING:
		case BIFF_MS_O_DRAWING_GROUP:
		case BIFF_MS_O_DRAWING_SELECTION:
			ms_escher_parse (q, &wb->container);
			break;

		case BIFF_ADDMENU :
#ifndef NO_DEBUG_EXCEL
			if (ms_excel_read_debug > 1) {
				printf ("%smenu with %d sub items",
					(MS_OLE_GET_GUINT8 (q->data+6) == 1) ? "" : "Placeholder ",
					MS_OLE_GET_GUINT8 (q->data+5));
			}
#endif
			break;

		default:
			ms_excel_unexpected_biff (q,"Workbook", ms_excel_read_debug);
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

	if (wb) {
		/* Cleanup */
		ms_excel_workbook_destroy (wb);

		/* If we were forced to stop then the load failed */
		if (problem_loading != NULL) {
			gnumeric_io_error_read (context, problem_loading);
		}
		return;
	}

	gnumeric_io_error_read (context, _("Unable to locate valid MS Excel workbook"));
}


void
ms_excel_read_cleanup (void)
{
	ms_excel_palette_destroy (ms_excel_default_palette ());
}
