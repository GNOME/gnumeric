/* vim: set sw=8: */
/**
 * ms-excel.c: MS Excel support for Gnumeric
 *
 * Authors:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-2001 Michael Meeks, Jody Goldberg
 * unicode and national language support (C) 2001 by Vlad Harchev <hvv@hippo.ru>
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>

#include "boot.h"
#include "ms-formula-read.h"
#include "ms-excel-read.h"
#include "ms-obj.h"
#include "ms-chart.h"
#include "ms-escher.h"
#include "ms-excel-util.h"
#include "ms-excel-xf.h"

#include <workbook.h>
#include <workbook-view.h>
#include <sheet-style.h>
#include <sheet-merge.h>
#include <cell.h>
#include <style.h>
#include <format.h>
#include <formats.h>
#include <print-info.h>
#include <selection.h>
#include <validation.h>
#include <parse-util.h>	/* for cell_name */
#include <ranges.h>
#include <expr-name.h>
#include <value.h>
#include <gutils.h>
#include <application.h>
#include <io-context.h>
#include <sheet-object-cell-comment.h>
#include <sheet-object-widget.h>
#include <sheet-object-graphic.h>
#ifdef ENABLE_BONOBO
#  include <sheet-object-container.h>
#  include <gnumeric-graph.h>
#endif

#include <libgnome/gnome-i18n.h>
#include <locale.h>

#define N_BYTES_BETWEEN_PROGRESS_UPDATES   0x1000

/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_read_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

static excel_iconv_t current_workbook_iconv = NULL;

/* Forward references */
static ExcelSheet *ms_excel_sheet_new (ExcelWorkbook *wb, char const *name);

void
ms_excel_unexpected_biff (BiffQuery *q, char const *state,
			  int debug_level)
{
#ifndef NO_DEBUG_EXCEL
	if (debug_level > 0) {
		printf ("Unexpected Opcode in %s: 0x%x, length 0x%x\n",
			state, q->opcode, q->length);
		if (debug_level > 2)
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
	return *d * 2;
}

static guint
biff_guint32_hash (const guint32 *d)
{
	return *d * 2;
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
	} else { /* Some assumptions: FIXME? */
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

static char *
get_chars (char const *ptr, guint length, gboolean high_byte)
{
	char* ans;
	guint32 lp;

	if (high_byte) {
		wchar_t* wc = g_new (wchar_t, length + 2);
		size_t retlength;
		ans = g_new (char, (length + 2) * 8);

		for (lp = 0; lp < length; lp++) {
			guint16 c = MS_OLE_GET_GUINT16 (ptr);
			ptr += 2;
			wc[lp] = c;
		}

		retlength = excel_wcstombs (ans, wc, length);
		g_free (wc);
		if (retlength == (size_t)-1)
			retlength = 0;
		ans[retlength] = 0;
		ans = g_realloc (ans, retlength + 2);
	} else {
		size_t inbytes = length,
			outbytes = (length + 2) * 8,
			retlength;
		char* inbuf = g_new (char, length), *outbufptr;
		char const * inbufptr = inbuf;

		ans = g_new (char, outbytes + 1);
		outbufptr = ans;
		for (lp = 0; lp < length; lp++) {
			inbuf[lp] = MS_OLE_GET_GUINT8 (ptr);
			ptr++;
		}
		excel_iconv (current_workbook_iconv,
			     &inbufptr, &inbytes, &outbufptr, &outbytes);

		retlength = outbufptr-ans;
		ans[retlength] = 0;
		ans = g_realloc (ans, retlength + 1);
		g_free (inbuf);
	}
	return ans;
}

/**
 * This function takes a length argument as Biff V7 has a byte length
 * (seemingly).
 * it returns the length in bytes of the string in byte_length
 * or nothing if this is NULL.
 * FIXME: see S59D47.HTM for full description
 **/
char *
biff_get_text (guint8 const *pos, guint32 length, guint32 *byte_length)
{
	char *ans;
	const guint8 *ptr;
	guint32 byte_len;
	gboolean header;
	gboolean high_byte;
	gboolean ext_str;
	gboolean rich_str;

	if (!byte_length)
		byte_length = &byte_len;
	*byte_length = 0;

	if (!length) {
		/* FIXME FIXME FIXME: What about the 1 byte for the header?
		 *                     The length may be wrong in this case.
		 */
		return 0;
	}

	d (5, {
		printf ("String:\n");
		ms_ole_dump (pos, length + 1);
	});

	header = biff_string_get_flags (pos,
					&high_byte,
					&ext_str,
					&rich_str);
	if (header) {
		ptr = pos + 1;
		(*byte_length)++;
	} else
		ptr = pos;

	{
		guint32 pre_len, end_len;

		get_xtn_lens (&pre_len, &end_len, ptr, ext_str, rich_str);
		ptr += pre_len;
		(*byte_length) += pre_len + end_len;
	}


	d (4, {
		printf ("String len %d, byte length %d: %d %d %d:\n",
			length, (*byte_length), high_byte, rich_str, ext_str);
		ms_ole_dump (pos, *byte_length);
	});


	if (!length) {
		ans = g_new (char, 2);
		g_warning ("Warning unterminated string floating");
	} else {
		(*byte_length) += (high_byte ? 2 : 1)*length;
		ans = get_chars ((char *) ptr, length, high_byte);
	}
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
		chars_left = (q->length - new_offset - pre_len) / (high_byte ? 2 : 1);
		if (chars_left > total_len)
			get_len = total_len;
		else
			get_len = chars_left;
		total_len -= get_len;
		g_assert (get_len >= 0);

		/* FIXME: split this simple bit out of here, it makes more sense damnit */
		str = get_chars ((char *)(q->data + new_offset + pre_len), get_len, high_byte);
		new_offset += pre_len + get_len * (high_byte ? 2 : 1);

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
read_sst (BiffQuery *q, ExcelWorkbook *wb, MsBiffVersion ver)
{
	guint32 offset;
	unsigned k;

	d (4, {
		printf ("SST\n");
		ms_ole_dump (q->data, q->length);
	});

	wb->global_string_max = MS_OLE_GET_GUINT32 (q->data + 4);
	wb->global_strings = g_new (char *, wb->global_string_max);

	offset = 8;

	for (k = 0; k < wb->global_string_max; k++) {
		offset = get_string (&wb->global_strings[k], q, offset, ver);

		if (!wb->global_strings[k]) {
			d (4, printf ("Blank string in table at 0x%x.\n", k););
		}
#ifndef NO_DEBUG_EXCEL
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
			d (2, {
				printf ("Complicated BIFF version 0x%x\n",
					MS_OLE_GET_GUINT16 (q->data));
				ms_ole_dump (q->data, q->length);
			});

			switch (MS_OLE_GET_GUINT16 (q->data)) {
			case 0x0600: ans->version = MS_BIFF_V8;
				     break;
			case 0x500: /* * OR ebiff7: FIXME ? !  */
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
		case 0x0005: ans->type = MS_BIFF_TYPE_Workbook; break;
		case 0x0006: ans->type = MS_BIFF_TYPE_VBModule; break;
		case 0x0010: ans->type = MS_BIFF_TYPE_Worksheet; break;
		case 0x0020: ans->type = MS_BIFF_TYPE_Chart; break;
		case 0x0040: ans->type = MS_BIFF_TYPE_Macrosheet; break;
		case 0x0100: ans->type = MS_BIFF_TYPE_Workspace; break;
		default:
			ans->type = MS_BIFF_TYPE_Unknown;
			printf ("Unknown BIFF type in BOF %x\n", MS_OLE_GET_GUINT16 (q->data + 2));
			break;
		}
		/* Now store in the directory array: */
		d (2, printf ("BOF %x, %d == %d, %d\n", q->opcode, q->length,
			      ans->version, ans->type););
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
	unsigned idx = 0;

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

/**
 * See S59D61.HTM
 **/
static void
biff_boundsheet_data_new (BiffQuery *q, ExcelWorkbook *wb, MsBiffVersion ver)
{
	BiffBoundsheetData *ans;
	char *default_name = "Unknown%d";

	/* Testing seems to indicate that Biff5 is compatibile with Biff7 here. */
	if (ver != MS_BIFF_V5 && ver != MS_BIFF_V7 && ver != MS_BIFF_V8) {
		printf ("Unknown BIFF Boundsheet spec. Assuming same as Biff7 FIXME\n");
		ver = MS_BIFF_V7;
	}

	ans = g_new (BiffBoundsheetData, 1);
	ans->streamStartPos = MS_OLE_GET_GUINT32 (q->data);

	g_return_if_fail (ans->streamStartPos == MS_OLE_GET_GUINT32 (q->data));

	switch (MS_OLE_GET_GUINT8 (q->data + 4)) {
	case 0: ans->type = MS_BIFF_TYPE_Worksheet;
		default_name = _("Sheet%d");
		break;
	case 1: ans->type = MS_BIFF_TYPE_Macrosheet;
		default_name = _("Macro%d");
		break;
	case 2: ans->type = MS_BIFF_TYPE_Chart;
		default_name = _("Chart%d");
		break;
	case 6: ans->type = MS_BIFF_TYPE_VBModule;
		default_name = _("Module%d");
		break;
	default:
		printf ("Unknown boundsheet type: %d\n", MS_OLE_GET_GUINT8 (q->data + 4));
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

	/* TODO: find some documentation on this.
	 * Sample data and OpenCalc imply that the docs are incorrect.  It
	 * seems like the name lenght is 1 byte.  Loading sample sheets in
	 * other locales universally seem to treat the first byte as a length
	 * and the second as the unicode flag header.
	 */
	ans->name = biff_get_text (q->data + 7,
		MS_OLE_GET_GUINT8 (q->data + 6), NULL);

	/* TODO: find some documentation on this.
	 * It appears that if the name is null it defaults to Sheet%d?
	 * However, we have only one test case and no docs.
	 */
	if (ans->name == NULL)
		ans->name = g_strdup_printf (default_name,
			g_hash_table_size (wb->boundsheet_data_by_index));

	d (1, printf ("Boundsheet: '%s', %d:%d\n", ans->name, ans->type,
		       ans->hidden););

	ans->index = (guint16)g_hash_table_size (wb->boundsheet_data_by_index);
	g_hash_table_insert (wb->boundsheet_data_by_index,  &ans->index, ans);
	g_hash_table_insert (wb->boundsheet_data_by_stream, &ans->streamStartPos, ans);

	/* AARRRGGGG : This is useless XL calls chart tabs 'worksheet' too */
	/* if (ans->type == MS_BIFF_TYPE_Worksheet) */

	/* FIXME : Use this kruft instead */
	if (ans->hidden == MS_BIFF_H_VISIBLE) {
		ans->sheet = ms_excel_sheet_new (wb, ans->name);
		ms_excel_workbook_attach (wb, ans->sheet);
	} else
		ans->sheet = NULL;
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
biff_font_data_new (BiffQuery *q, ExcelWorkbook *wb)
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

	d (1, printf ("Insert font '%s' size %d pts color %d\n",
		      fd->fontname, fd->height / 20, fd->color_idx););
	d (3, printf ("Font color = 0x%x\n", fd->color_idx););

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

char const *excel_builtin_formats[EXCEL_BUILTIN_FORMAT_LEN] = {
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
/* 0x0d */	"# ?" "?/?" "?",  /* Don't accidentally use trigraph.  */
/* 0x0e		"m/d/yy" */ NULL,	/* locale specific, set in */
/* 0x0f		"d-mmm-yy", */ NULL,	/* ms_excel_read_init */
/* 0x10		"d-mmm", */ NULL,
/* 0x11 */	"mmm-yy",
/* 0x12 */	"h:mm AM/PM",
/* 0x13 */	"h:mm:ss AM/PM",
/* 0x14 */	"h:mm",
/* 0x15 */	"h:mm:ss",
/* 0x16		"m/d/yy h:mm", */ NULL,
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

static StyleFormat *
ms_excel_wb_get_fmt (ExcelWorkbook *wb, guint16 idx)
{
	char const *ans = NULL;
	BiffFormatData const *d = g_hash_table_lookup (wb->format_data, &idx);

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
 * read all the sheets in (for inter-sheet references in names).
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

	d (1, printf ("%s: %x %x sheet=%d '%s'\n",
		      external ? "EXTERNNAME" : "NAME",
		      externsheet,
		      wb->name_data->len, sheet_index, bnd->name);
	);
	if (ms_excel_read_debug > 2)
		ms_ole_dump (bnd->v.store.data, bnd->v.store.len);

	g_ptr_array_add (wb->name_data, bnd);
}

ExprTree *
biff_name_data_get_name (ExcelSheet const *esheet, int idx)
{
	BiffNameData *bnd;
	GPtrArray    *a;

	g_return_val_if_fail (esheet, NULL);
	g_return_val_if_fail (esheet->wb, NULL);

	a = esheet->wb->name_data;

	if (a == NULL || idx < 0 || (int)a->len <= idx ||
	    (bnd = g_ptr_array_index (a, idx)) == NULL) {
		g_warning ("EXCEL: %x (of %x) UNKNOWN name\n", idx, a->len);
		return expr_tree_new_constant (value_new_string ("Unknown name"));

	}

	if (bnd->type == BNDStore && bnd->v.store.data) {
		ExprTree *tree = ms_excel_parse_formula (esheet,
			bnd->v.store.data, 0, 0, FALSE,
			bnd->v.store.len, NULL);

		bnd->type = BNDName;
		g_free (bnd->v.store.data);

		if (tree) {
			ParsePos pp;
			if (bnd->sheet_index > 0)
				parse_pos_init (&pp, NULL, esheet->gnum_sheet, 0,0);
			else
				parse_pos_init (&pp, esheet->wb->gnum_wb, NULL, 0,0);

			bnd->v.name = expr_name_add (&pp, bnd->name, tree, NULL);

			if (!bnd->v.name)
				printf ("Error: for name '%s'\n", bnd->name);
#ifndef NO_DEBUG_EXCEL
			else if (ms_excel_read_debug > 1) {
				ParsePos ep;
				parse_pos_init (&ep, NULL, esheet->gnum_sheet, 0, 0);
				printf ("Parsed name: '%s' = '%s'\n",
					bnd->name, tree
					? expr_tree_as_string (tree, &ep)
					: "error");
			}
#endif
		} else
			bnd->v.name = NULL; /* OK so it's a special 'AddIn' name */
	}

	if (bnd->type == BNDName && bnd->v.name)
		return expr_tree_new_name (bnd->v.name, NULL, NULL);
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
		d (3, printf ("Creating default palette\n"););

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

	d (3, printf ("New palette with %d entries\n", len););

	for (lp = 0; lp < len; lp++) {
		guint32 num = MS_OLE_GET_GUINT32 (q->data + 2 + lp * 4);

		/* NOTE the order of bytes is different from what one would
		 * expect */
		pal->blue[lp] = (num & 0x00ff0000) >> 16;
		pal->green[lp] = (num & 0x0000ff00) >> 8;
		pal->red[lp] = (num & 0x000000ff) >> 0;
		d (5, printf ("Colour %d: 0x%8x (%x,%x,%x)\n", lp,
			      num, pal->red[lp], pal->green[lp], pal->blue[lp]););

		pal->gnum_cols[lp] = NULL;
	}
	return pal;
}

static StyleColor *
black_or_white_contrast (StyleColor const * contrast)
{
	/* FIXME FIXME FIXME: This is a BIG guess */
	/* Is the contrast colour closer to black or white based
	 * on this VERY loose metric.
	 */
	unsigned const guess =
	    contrast->color.red +
	    contrast->color.green +
	    contrast->color.blue;

	d (1, printf ("Contrast 0x%x 0x%x 0x%x: 0x%x\n",
		      contrast->color.red,
		      contrast->color.green,
		      contrast->color.blue,
		      guess););

	/* guess the minimum hacked pseudo-luminosity */
	if (guess < (0x18000)) {
		d (1, puts ("Contrast is White"););
		return style_color_white ();
	}

	d (1, puts ("Contrast is Black"););
	return style_color_black ();
}

StyleColor *
ms_excel_palette_get (ExcelPalette const *pal, gint idx)
{
	/* return black on failure */
	g_return_val_if_fail (pal != NULL, style_color_black ());

	/* NOTE: not documented but seems close
	 * If you find a normative reference please forward it.
	 *
	 * The color index field seems to use
	 *	8-63 = Palette index 0-55
	 *
	 *	0 = black?
	 *	1 = white?
	 *	64, 65, 127 = auto contrast?
	 *
	 *	64 appears to be associated with the the background colour
	 *	in the WINDOW2 record.
	 */

	d (4, printf ("Color Index %d\n", idx););

	/* Black ? */
	if (idx == 0)
		return style_color_black ();
	/* White ? */
	if (idx == 1)
		return style_color_white ();

	idx -= 8;
	if (idx < 0 || pal->length <= idx) {
		g_warning ("EXCEL: color index (%d) is out of range (0..%d). Defaulting to black",
			   idx + 8, pal->length);
		return style_color_black ();
	}

	if (pal->gnum_cols[idx] == NULL) {
		gushort r, g, b;
		/* scale 8 bit/color ->  16 bit/color by cloning */
		r = (pal->red[idx] << 8) | pal->red[idx];
		g = (pal->green[idx] << 8) | pal->green[idx];
		b = (pal->blue[idx] << 8) | pal->blue[idx];
		d (1, printf ("New color in slot %d: RGB= %x,%x,%x\n",
			      idx, r, g, b););

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
ms_excel_get_font (ExcelSheet *esheet, guint16 font_idx)
{
	BiffFontData const *fd = g_hash_table_lookup (esheet->wb->font_data,
						      &font_idx);

	g_return_val_if_fail (fd != NULL, NULL); /* flag the problem */
	g_return_val_if_fail (fd->index != 4, NULL); /* should not exist */
	return fd;
}

static BiffXFData const *
ms_excel_get_xf (ExcelSheet *esheet, int xfidx)
{
	BiffXFData *xf;
	GPtrArray const * const p = esheet->wb->XF_cell_records;

	g_return_val_if_fail (p != NULL, NULL);
	if (0 > xfidx || xfidx >= (int)p->len) {
		g_warning ("XL: Xf index 0x%X is not in the range[0..0x%X)", xfidx, p->len);
		xfidx = 0;
	}
	xf = g_ptr_array_index (p, xfidx);

	g_return_val_if_fail (xf, NULL);
	/* FIXME: when we can handle styles too deal with this correctly */
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
		{ "�ͣ� �Х����å�", "Kochi Gothic"},
		{ "�ͣ� �����å�",   "Kochi Gothic"},
		{ "�����å�",        "Kochi Gothic"},
		{ "MS UI Gothic",    "Kochi Gothic"},
		{ "�ͣ� ����ī",     "Kochi Mincho"},
		{ "�ͣ� ��ī",       "Kochi Mincho"},
		{ "��ī",            "Kochi Mincho"},
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
ms_excel_get_style_from_xf (ExcelSheet *esheet, guint16 xfidx)
{
	BiffXFData const *xf = ms_excel_get_xf (esheet, xfidx);
	BiffFontData const *fd;
	StyleColor	*pattern_color, *back_color, *font_color;
	int		 pattern_index,  back_index,  font_index;
	MStyle *mstyle;
	int i;
	char *subs_fontname;

	d (2, printf ("XF index %d\n", xfidx););

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

	/* protection */
	mstyle_set_content_locked (mstyle, xf->locked);
	mstyle_set_content_hidden (mstyle, xf->hidden);

	/* Alignment */
	mstyle_set_align_v   (mstyle, xf->valign);
	mstyle_set_align_h   (mstyle, xf->halign);
	mstyle_set_wrap_text (mstyle, xf->wrap_text);
	mstyle_set_indent    (mstyle, xf->indent);
	/* mstyle_set_orientation (mstyle, ); */

	/* Font */
	fd = ms_excel_get_font (esheet, xf->font_idx);
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
		case MS_BIFF_F_U_SINGLE:
		case MS_BIFF_F_U_SINGLE_ACC:
			underline = UNDERLINE_SINGLE;
			break;

		case MS_BIFF_F_U_DOUBLE:
		case MS_BIFF_F_U_DOUBLE_ACC:
			underline = UNDERLINE_DOUBLE;
			break;

		case MS_BIFF_F_U_NONE:
		default:
			underline = UNDERLINE_NONE;
		}
		mstyle_set_font_uline  (mstyle, underline);

		font_index = fd->color_idx;
	} else
		font_index = 127; /* Default to White */

	/* Background */
	mstyle_set_pattern (mstyle, xf->fill_pattern_idx);

	/* Solid patterns seem to reverse the meaning */
	if (xf->fill_pattern_idx == 1) {
		pattern_index	= xf->pat_backgnd_col;
		back_index	= xf->pat_foregnd_col;
	} else {
		pattern_index	= xf->pat_foregnd_col;
		back_index	= xf->pat_backgnd_col;
	}

	d (4, printf ("back = %d, pat = %d, font = %d, pat_style = %d\n",
		      back_index, pattern_index, font_index, xf->fill_pattern_idx););

	/* ICK: FIXME
	 * There must be a cleaner way of doing this
	 */

	/* Lets guess the state table for setting auto colours */
	if (font_index == 127) {
		/* The font is auto.  Lets look for info elsewhere */
		if (back_index == 64 || back_index == 65 || back_index == 0) {
			/* Everything is auto default to black text/pattern on white */
			/* FIXME: This should use the 'Normal' Style */
			if (pattern_index == 64 || pattern_index == 65 || pattern_index == 0) {
				back_color = style_color_white ();
				font_color = style_color_black ();
				style_color_ref ((pattern_color = font_color));
			} else {
				pattern_color =
					ms_excel_palette_get (esheet->wb->palette,
							      pattern_index);

				/* Contrast back to pattern, and font to back */
				/* FIXME: What is correct?  */
				back_color = (back_index == 65)
				    ? style_color_white ()
				    : black_or_white_contrast (pattern_color);
				font_color = black_or_white_contrast (back_color);
			}
		} else {
			back_color = ms_excel_palette_get (esheet->wb->palette,
							   back_index);

			/* Contrast font to back */
			font_color = black_or_white_contrast (back_color);

			/* Pattern is auto contrast it to back */
			if (pattern_index == 64 || pattern_index == 65 || pattern_index == 0)
				style_color_ref ((pattern_color = font_color));
			else
				pattern_color =
					ms_excel_palette_get (esheet->wb->palette,
							      pattern_index);
		}
	} else {
		/* Use the font as a baseline */
		font_color = ms_excel_palette_get (esheet->wb->palette,
						   font_index);

		if (back_index == 64 || back_index == 65 || back_index == 0) {
			/* contrast back to font and pattern to back */
			if (pattern_index == 64 || pattern_index == 65 || pattern_index == 0) {
				/* Contrast back to font, and pattern to back */
				back_color = black_or_white_contrast (font_color);
				pattern_color = black_or_white_contrast (back_color);
			} else {
				pattern_color =
					ms_excel_palette_get (esheet->wb->palette,
							      pattern_index);

				/* Contrast back to pattern */
				back_color = black_or_white_contrast (pattern_color);
			}
		} else {
			back_color = ms_excel_palette_get (esheet->wb->palette,
							   back_index);

			/* Pattern is auto contrast it to back */
			if (pattern_index == 64 || pattern_index == 65 || pattern_index == 0)
				pattern_color = black_or_white_contrast (back_color);
			else
				pattern_color =
					ms_excel_palette_get (esheet->wb->palette,
							      pattern_index);
		}
	}

	g_return_val_if_fail (back_color && pattern_color && font_color, NULL);

	d (4, printf ("back = #%02x%02x%02x, pat = #%02x%02x%02x, font = #%02x%02x%02x, pat_style = %d\n",
		      back_color->red>>8, back_color->green>>8, back_color->blue>>8,
		      pattern_color->red>>8, pattern_color->green>>8, pattern_color->blue>>8,
		      font_color->red>>8, font_color->green>>8, font_color->blue>>8,
		      xf->fill_pattern_idx););

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
			/* FIXME: This does not choose well, hard code to black for now */
			? black_or_white_contrast (back_color)
#endif
			? style_color_black ()
			: ms_excel_palette_get (esheet->wb->palette,
						color_index);

		mstyle_set_border (tmp, t,
				   style_border_fetch (xf->border_type[i],
						       color, t));
	}

	/* Set the cache (const_cast) */
	((BiffXFData *)xf)->mstyle = mstyle;
	mstyle_ref (mstyle);
	return xf->mstyle;
}

static void
ms_excel_set_xf (ExcelSheet *esheet, int col, int row, guint16 xfidx)
{
	MStyle *const mstyle = ms_excel_get_style_from_xf (esheet, xfidx);
	if (mstyle == NULL)
		return;

	d (2, printf ("%s!%s%d = xf(%d)\n", esheet->gnum_sheet->name_unquoted,
		      col_name (col), row + 1, xfidx););

	sheet_style_set_pos (esheet->gnum_sheet, col, row, mstyle);
}

static void
ms_excel_set_xf_segment (ExcelSheet *esheet,
			 int start_col, int end_col,
			 int start_row, int end_row, guint16 xfidx)
{
	Range   range;
	MStyle * const mstyle  = ms_excel_get_style_from_xf (esheet, xfidx);

	if (mstyle == NULL)
		return;

	range.start.col = start_col;
	range.start.row = start_row;
	range.end.col   = end_col;
	range.end.row   = end_row;
	sheet_style_set_range (esheet->gnum_sheet, &range, mstyle);

	d (2, {
		range_dump (&range, "");
		fprintf (stderr, " = xf(%d)\n", xfidx);
	});
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
			      i < (int)(sizeof (map_from_excel) / sizeof (int)), 0);

	return map_from_excel[i];
}

/**
 * Parse the BIFF XF Data structure into a nice form, see S59E1E.HTM
 **/
static void
biff_xf_data_new (BiffQuery *q, ExcelWorkbook *wb, MsBiffVersion ver)
{
	BiffXFData *xf = g_new (BiffXFData, 1);
	guint32 data, subdata;

	xf->font_idx = MS_OLE_GET_GUINT16 (q->data);
	xf->format_idx = MS_OLE_GET_GUINT16 (q->data + 2);
	xf->style_format = (xf->format_idx > 0)
		? ms_excel_wb_get_fmt (wb, xf->format_idx) : NULL;

	data = MS_OLE_GET_GUINT16 (q->data + 4);
	xf->locked = (data & 0x0001) != 0;
	xf->hidden = (data & 0x0002) != 0;
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
	xf->wrap_text = (data & 0x0008) != 0;
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
	if (ver >= MS_BIFF_V8)
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
			g_warning ("EXCEL: rotated text is not supported yet.");
		}
	}

	if (ver >= MS_BIFF_V8) {
		/* FIXME: This code seems irrelevant for merging.
		 * The undocumented record MERGECELLS appears to be the correct source.
		 * Nothing seems to set the merge flags.
		 */
		static gboolean shrink_warn = TRUE;

		/* FIXME: What are the lower 8 bits Always 0?  */
		/* We need this to be able to support travel.xls */
		const guint16 data = MS_OLE_GET_GUINT16 (q->data + 8);
		gboolean const shrink = (data & 0x10) ? TRUE : FALSE;
		/* gboolean const merge = (data & 0x20) ? TRUE : FALSE; */

		xf->indent = data & 0x0f;

		if (shrink && shrink_warn) {
			shrink_warn = FALSE;
			g_warning ("EXCEL: Shrink to fit is not supported yet.");
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

	if (ver >= MS_BIFF_V8) { /* Very different */
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

		d (2, printf ("Color f=0x%x b=0x%x pat=0x%x\n",
			      xf->pat_foregnd_col,
			      xf->pat_backgnd_col,
			      xf->fill_pattern_idx););

	} else { /* Biff 7 */
		data = MS_OLE_GET_GUINT16 (q->data + 8);
		xf->pat_foregnd_col = (data & 0x007f);
		xf->pat_backgnd_col = (data & 0x1f80) >> 7;

		data = MS_OLE_GET_GUINT16 (q->data + 10);
		xf->fill_pattern_idx =
			excel_map_pattern_index_from_excel (data & 0x3f);

		d (2, printf ("Color f=0x%x b=0x%x pat=0x%x\n",
			      xf->pat_foregnd_col,
			      xf->pat_backgnd_col,
			      xf->fill_pattern_idx););

		/* Luckily this maps nicely onto the new set. */
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
	d (2, printf ("XF(%d): Font %d, Format %d, Fore %d, Back %d, Pattern = %d\n",
		      wb->XF_cell_records->len - 1,
		      xf->font_idx,
		      xf->format_idx,
		      xf->pat_foregnd_col,
		      xf->pat_backgnd_col,
		      xf->fill_pattern_idx););
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
ms_excel_sheet_set_version (ExcelSheet *esheet, MsBiffVersion ver)
{
	esheet->container.ver = ver;
}

static void
ms_excel_sheet_insert (ExcelSheet *esheet, int xfidx,
		       int col, int row, char const *text)
{
	Cell *cell;

	ms_excel_set_xf (esheet, col, row, xfidx);

	if (text) {
		cell = sheet_cell_fetch (esheet->gnum_sheet, col, row);
		cell_set_value (cell, value_new_string (text), NULL);
	}
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
ms_excel_formula_shared (BiffQuery *q, ExcelSheet *esheet, Cell *cell)
{
	int has_next_record = ms_biff_query_next (q);

	g_return_val_if_fail (has_next_record, NULL);

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
		ExprTree *expr = ms_excel_parse_formula (esheet, data,
							 array_col_first,
							 array_row_first,
							 !is_array, data_len,
							 NULL);
		BiffSharedFormula *sf = g_new (BiffSharedFormula, 1);

		/*
		 * WARNING: Do NOT use the upper left corner as the hashkey.
		 *     For some bizzare reason XL appears to sometimes not
		 *     flag the formula as shared until later.
		 *  Use the location of the cell we are reading as the key.
		 */
		sf->key = cell->pos;
		sf->is_array = is_array;
		if (data_len > 0) {
			sf->data = g_new (guint8, data_len);
			memcpy (sf->data, data, data_len);
		} else
			sf->data = NULL;
		sf->data_len = data_len;

		d (1, {
			printf ("Shared formula, extent %s:",
				cell_coord_name (array_col_first, array_row_first));
			printf ("%s\n",
				cell_coord_name (array_col_last, array_row_last));
		});


		/* Whack in the hash for later */
		g_hash_table_insert (esheet->shared_formulae, &sf->key, sf);

		g_return_val_if_fail (expr != NULL, FALSE);

		if (is_array)
			cell_set_array_formula (esheet->gnum_sheet,
						array_col_first, array_row_first,
						array_col_last, array_row_last,
						expr);
		return expr;
	}

	g_warning ("EXCEL: unexpected record '0x%x' after a formula in '%s'\n",
		   q->opcode, cell_name (cell));
	return NULL;
}

/* FORMULA */
static void
ms_excel_read_formula (BiffQuery *q, ExcelSheet *esheet)
{
	/*
	 * NOTE: There must be _no_ path through this function that does
	 *       not set the cell value.
	 */

	/* Pre-retrieve incase this is a string */
	gboolean array_elem, is_string = FALSE;
	const guint16 xf_index = EX_GETXF (q);
	const guint16 col      = EX_GETCOL (q);
	const guint16 row      = EX_GETROW (q);
	const guint16 options  = MS_OLE_GET_GUINT16 (q->data + 14);
	Cell *cell;
	ExprTree *expr;
	Value *val = NULL;

	/* Set format */
	ms_excel_set_xf (esheet, col, row, xf_index);

	/* Then fetch Cell */
	cell = sheet_cell_fetch (esheet->gnum_sheet, col, row);

	d (0, printf ("Formula in %s!%s;\n",
		      cell->base.sheet->name_quoted, cell_name (cell)););

	/* TODO TODO TODO: Wishlist
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
	if (q->length < (unsigned)(22 + MS_OLE_GET_GUINT16 (q->data + 20))) {
		printf ("FIXME: serious formula error: "
			"supposed length 0x%x, real len 0x%x\n",
			MS_OLE_GET_GUINT16 (q->data + 20), q->length);
		cell_set_value (cell, value_new_error (NULL, "Formula Error"), NULL);
		return;
	}

	/*
	 * Get the current value so that we can format, do this BEFORE handling
	 * shared/array formulas or strings in case we need to go to the next
	 * record
	 */
	if (MS_OLE_GET_GUINT16 (q->data + 12) != 0xffff) {
		double const num = gnumeric_get_le_double (q->data + 6);
		val = value_new_float (num);
	} else {
		const guint8 val_type = MS_OLE_GET_GUINT8 (q->data + 6);
		switch (val_type) {
		case 0: /* String */
			is_string = TRUE;
			break;

		case 1: /* Boolean */
		{
			const guint8 v = MS_OLE_GET_GUINT8 (q->data + 8);
			val = value_new_bool (v ? TRUE : FALSE);
			break;
		}

		case 2: /* Error */
		{
			EvalPos ep;
			const guint8 v = MS_OLE_GET_GUINT8 (q->data + 8);
			char const *const err_str =
			    biff_get_error_text (v);

			/* FIXME FIXME FIXME: Init ep */
			val = value_new_error (&ep, err_str);
			break;
		}

		case 3: /* Empty */
			/* TODO TODO TODO
			 * This is undocumented and a big guess, but it seems
			 * accurate.
			 */
			d (0, {
				printf ("%s:%s: has type 3 contents.  "
					"Is it an empty cell?\n",
					esheet->gnum_sheet->name_unquoted,
					cell_name (cell));
				if (ms_excel_read_debug > 5)
					ms_ole_dump (q->data + 6, 8);
			});

			val = value_new_empty ();
			break;

		default:
			printf ("Unknown type (%x) for cell's (%s) current val\n",
				val_type, cell_name (cell));
		};
	}

	expr = ms_excel_parse_formula (esheet, (q->data + 22),
				       col, row,
				       FALSE, MS_OLE_GET_GUINT16 (q->data + 20),
				       &array_elem);

	/* Error was flaged by parse_formula */
	if (expr == NULL && !array_elem)
		expr = ms_excel_formula_shared (q, esheet, cell);

	if (is_string) {
		guint16 code;
		if (ms_biff_query_peek_next (q, &code) && (0xff & code) == BIFF_STRING) {
			char *v = NULL;
			if (ms_biff_query_next (q)) {
				/*
				 * NOTE: the Excel developers kit docs are
				 *       WRONG.  There is an article that
				 *       clarifies the behaviour to be the std
				 *       unicode format rather than the pure
				 *       length version the docs describe.
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
				g_warning ("EXCEL: invalid STRING record in %s",
					   cell_name (cell));
			}
		} else {
			/*
			 * Docs say that there should be a STRING
			 * record here
			 */
			EvalPos pos;
			val = value_new_error (eval_pos_init_cell (&pos, cell), "MISSING STRING");
			g_warning ("EXCEL: missing STRING record for %s",
				   cell_name (cell));
		}
	}

	/* We MUST have a value */
	if (val == NULL) {
		EvalPos pos;
		val = value_new_error (eval_pos_init_cell (&pos, cell), "MISSING Value");
		g_warning ("EXCEL: Invalid state.  Missing Value in %s?",
			   cell_name (cell));
	}

	if (cell_is_array (cell)) {
		/* Array expressions were already stored in the cells (without
		 * recalc), and without a value.  Handle either the first
		 * instance or the followers.
		 */
		if (expr == NULL && !array_elem) {
			g_warning ("EXCEL: How does cell %s have an array expression ?",
				   cell_name (cell));
			cell_set_value (cell, val, NULL);
		} else
			cell_assign_value (cell, val, NULL);
	} else if (!cell_has_expr (cell)) {
		cell_set_expr_and_value (cell, expr, val, NULL, TRUE);
		expr_tree_unref (expr);
	} else {
		/*
		 * NOTE: Only the expression is screwed.
		 * The value and format can still be set.
		 */
		g_warning ("EXCEL: Shared formula problems in %s!%s",
			   cell->base.sheet->name_quoted, cell_name (cell));
		cell_set_value (cell, val, NULL);
	}

	/*
	 * 0x1 = AlwaysCalc
	 * 0x2 = CalcOnLoad
	 */
	if (options & 0x3)
		cell_queue_recalc (cell);
}

BiffSharedFormula *
ms_excel_sheet_shared_formula (ExcelSheet const *esheet,
			       CellPos const    *key)
{
	d (5, printf ("FIND SHARED: %s\n", cell_pos_name (key)););

	return g_hash_table_lookup (esheet->shared_formulae, key);
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

	d (0, printf ("%s\n", sheet->name_unquoted););

	/* Ignore the first 2 bytes.  What are they ? */
	/* Dec/1/2000 JEG: I have not researched it, but this may have some
	 * flags indicating whether or not the object is acnhored to the cell
	 */
	raw_anchor += 2;

	/* Words 0, 4, 8, 12: The row/col of the corners */
	/* Words 2, 6, 10, 14: distance from cell edge */
	for (i = 0; i < 4; i++, raw_anchor += 4) {
		int const pos  = MS_OLE_GET_GUINT16 (raw_anchor);
		int const nths = MS_OLE_GET_GUINT16 (raw_anchor + 2);

		d (2, {
			printf ("%d/%d cell %s from ",
				nths, (i & 1) ? 256 : 1024,
				(i & 1) ? "heights" : "widths");
			if (i & 1)
				printf ("row %d;\n", pos + 1);
			else
				printf ("col %s (%d);\n", col_name (pos), pos);
		});

		if (i & 1) { /* odds are rows */
			offset[i] = nths / row_denominator;
			if (i == 1)
				range->start.row = pos;
			else
				range->end.row = pos;
		} else {
			offset[i] = nths / 1024.;
			if (i == 0)
				range->start.col = pos;
			else
				range->end.col = pos;
		}
	}

	return FALSE;
}

static gboolean
ms_sheet_realize_obj (MSContainer *container, MSObj *obj)
{
	float offsets[4];
	Range range;
	ExcelSheet *esheet;
	MSObjAttr *anchor;

	if (obj == NULL)
		return TRUE;

	g_return_val_if_fail (container != NULL, TRUE);
	esheet = (ExcelSheet *)container;

	anchor = ms_object_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_ANCHOR);
	if (anchor == NULL) {
		printf ("MISSING anchor for obj %p\n", obj);
		return TRUE;
	}

	if (ms_sheet_obj_anchor_to_pos (esheet->gnum_sheet, container->ver,
					anchor->v.v_ptr, &range, offsets))
		return TRUE;

	if (obj->gnum_obj != NULL) {
		static SheetObjectAnchorType const anchor_types[4] = {
			SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
			SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
			SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
			SO_ANCHOR_PERCENTAGE_FROM_COLROW_START
		};
		MSObjAttr *flip_h = ms_object_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_FLIP_H);
		MSObjAttr *flip_v = ms_object_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_FLIP_V);
		SheetObjectDirection direction =
			((flip_h == NULL) ? SO_DIR_RIGHT : 0) |
			((flip_v == NULL) ? SO_DIR_DOWN : 0);
		SheetObjectAnchor anchor;
		sheet_object_anchor_init (&anchor, &range,
					  offsets, anchor_types,
					  direction);
		sheet_object_anchor_set (SHEET_OBJECT (obj->gnum_obj),
					 &anchor);
		sheet_object_set_sheet (SHEET_OBJECT (obj->gnum_obj),
					esheet->gnum_sheet);

		/* can not be done until we have set the sheet */
		if (obj->excel_type == 0x0B) {
			sheet_widget_checkbox_set_link (SHEET_OBJECT (obj->gnum_obj),
				ms_object_attr_get_expr (obj, MS_OBJ_ATTR_CHECKBOX_LINK, NULL));
		} else if (obj->excel_type == 0x11) {
			sheet_widget_scrollbar_set_details (SHEET_OBJECT (obj->gnum_obj),
				ms_object_attr_get_expr (obj, MS_OBJ_ATTR_SCROLLBAR_LINK, NULL),
				0,
				ms_object_attr_get_int  (obj, MS_OBJ_ATTR_SCROLLBAR_MIN, 0),
				ms_object_attr_get_int  (obj, MS_OBJ_ATTR_SCROLLBAR_MAX, 100),
				ms_object_attr_get_int  (obj, MS_OBJ_ATTR_SCROLLBAR_INC, 1),
				ms_object_attr_get_int  (obj, MS_OBJ_ATTR_SCROLLBAR_PAGE, 10));
		}
	}

	return FALSE;
}

static StyleColor *
ms_sheet_map_color (ExcelSheet const *esheet, MSObj const *obj, MSObjAttrID id)
{
	gushort r, g, b;
	MSObjAttr *attr = ms_object_attr_bag_lookup (obj->attrs, id);

	if (attr == NULL)
		return NULL;

	if ((~0x7ffffff) & attr->v.v_uint)
		return ms_excel_palette_get (esheet->wb->palette,
			(0x7ffffff & attr->v.v_uint));

	r = (attr->v.v_uint)       & 0xff;
	g = (attr->v.v_uint >> 8)  & 0xff;
	b = (attr->v.v_uint >> 16) & 0xff;

	/* scale 8 bit/color ->  16 bit/color by cloning */
	return style_color_new ((r << 8) | r, (g << 8) | g, (b << 8) | b);
}

static GtkObject *
ms_sheet_create_obj (MSContainer *container, MSObj *obj)
{
	SheetObject *so = NULL;
	Sheet  *sheet;
	ExcelSheet const *esheet;

	if (obj == NULL)
		return NULL;

	g_return_val_if_fail (container != NULL, NULL);

	esheet = (ExcelSheet const *)container;
	sheet = esheet->gnum_sheet;

	switch (obj->excel_type) {
	case 0x01: { /* Line */
		MSObjAttr *is_arrow = ms_object_attr_bag_lookup (obj->attrs,
			MS_OBJ_ATTR_ARROW_END);
		so = sheet_object_line_new (is_arrow != NULL); break;
		break;
	}
	case 0x02:
	case 0x03: { /* Box or Oval */
		StyleColor *fill_color = NULL;
		so = sheet_object_box_new (obj->excel_type == 3);
		if (ms_object_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_FILLED))
			fill_color = ms_sheet_map_color (esheet, obj,
				MS_OBJ_ATTR_FILL_COLOR);
		sheet_object_graphic_fill_color_set (so, fill_color);
		break;
	}

	case 0x05: { /* Chart */
#ifdef ENABLE_BONOBO
		so = SHEET_OBJECT (gnm_graph_new (sheet->workbook));
#else
		so = sheet_object_box_new (FALSE);  /* placeholder */
		if (esheet->wb->warn_unsupported_graphs) {
			/* TODO : Use IOContext when available */
			esheet->wb->warn_unsupported_graphs = FALSE;
			g_warning ("Graphs are not supported in non-bonobo version");
		}
#endif
		break;
	}
	case 0x06: so = sheet_widget_label_new (sheet);    break; /* TextBox */
	case 0x07: so = sheet_widget_button_new (sheet);   break; /* Button */
	case 0x08: { /* Picture */
#ifdef ENABLE_BONOBO
		MSObjAttr *blip_id = ms_object_attr_bag_lookup (obj->attrs,
			MS_OBJ_ATTR_BLIP_ID);

		if (blip_id != NULL) {
			MSEscherBlip const *blip =
				ms_container_get_blip (container, blip_id->v.v_uint - 1);

			if (blip != NULL) {
				SheetObjectBonobo *sob;

				so = sheet_object_container_new (sheet->workbook);
				sob = SHEET_OBJECT_BONOBO (so);

				if (sheet_object_bonobo_set_object_iid (sob, blip->obj_id)) {
					CORBA_Environment ev;

					CORBA_exception_init (&ev);
					sheet_object_bonobo_load_persist_stream (
						sob, blip->stream, &ev);
					if (ev._major != CORBA_NO_EXCEPTION) {
						g_warning ("Failed to load '%s' from "
							   "stream: %s", blip->obj_id,
							   bonobo_exception_get_text (&ev));
						gtk_object_unref (GTK_OBJECT (so));
						so = NULL;
					}
					CORBA_exception_free (&ev);
				} else {
					g_warning ("Could not set object iid '%s'!",
						   blip->obj_id);
					gtk_object_unref (GTK_OBJECT (so));
					so = NULL;
				}
			}
		}
#else
		if (esheet->wb->warn_unsupported_images) {
			/* TODO : Use IOContext when available */
			esheet->wb->warn_unsupported_images = FALSE;
			g_warning ("Images are not supported in non-bonobo version");
		}
#endif
		/* replace blips we don't know how to handle with rectangles */
		if (so == NULL)
			so = sheet_object_box_new (FALSE);  /* placeholder */
		break;
	}
	case 0x0B: so = sheet_widget_checkbox_new (sheet); break;
	case 0x0C: so = sheet_widget_radio_button_new (sheet); break;
	case 0x0E: so = sheet_widget_label_new (sheet);    break;
	case 0x10: so = sheet_object_box_new (FALSE);  break; /* Spinner */
	case 0x11: so = sheet_widget_scrollbar_new (sheet); break;
	case 0x12: so = sheet_widget_list_new (sheet);     break;
	case 0x14: so = sheet_widget_combo_new (sheet);    break;

	case 0x19: /* Comment */
		/* TODO: we'll need a special widget for this */
		return NULL;

	default:
		g_warning ("EXCEL: unhandled excel object of type %s (0x%x) id = %d\n",
			   obj->excel_type_name, obj->excel_type, obj->id);
		return NULL;
	}

	return so ? GTK_OBJECT (so) : NULL;
}

static ExprTree *
ms_sheet_parse_expr_internal (ExcelSheet *esheet, guint8 const *data, int length)
{
	ExprTree *expr;

	g_return_val_if_fail (length > 0, NULL);

	expr = ms_excel_parse_formula (esheet, data,
				       0, 0, FALSE, length, NULL);
#if 0
	{
		char *tmp;
		ParsePos pp;
		Sheet *sheet = esheet->gnum_sheet;
		Workbook *wb = (sheet == NULL) ? esheet->wb->gnum_wb : NULL;

		tmp = expr_tree_as_string (expr, parse_pos_init (&pp, wb, sheet, 0, 0));
		puts (tmp);
		g_free (tmp);
	}
#endif

	return expr;
}

static ExprTree *
ms_sheet_parse_expr (MSContainer *container, guint8 const *data, int length)
{
	return ms_sheet_parse_expr_internal ((ExcelSheet *)container,
					     data, length);
}

static Sheet  *
ms_sheet_sheet (MSContainer const *container)
{
	return ((ExcelSheet const *)container)->gnum_sheet;
}

static StyleFormat *
ms_sheet_get_fmt (MSContainer const *container, guint16 indx)
{
	return ms_excel_wb_get_fmt (((ExcelSheet const *)container)->wb, indx);
}

static void
ms_excel_print_unit_init_inch (PrintUnit *pu, double val)
{
	pu->points = unit_convert (val, UNIT_INCH, UNIT_POINTS);
	pu->desired_display = UNIT_INCH; /* FIXME: should be more global */
}

/*
 * ms_excel_init_margins
 * @esheet ExcelSheet
 *
 * Excel only saves margins when any of the margins differs from the
 * default. So we must initialize the margins to Excel's defaults, which
 * are:
 * Top, bottom:    1 in   - 72 pt
 * Left, right:    3/4 in - 48 pt
 * Header, footer: 1/2 in - 36 pt
 */
static void
ms_excel_init_margins (ExcelSheet *esheet)
{
	PrintInformation *pi;

	g_return_if_fail (esheet != NULL);
	g_return_if_fail (esheet->gnum_sheet != NULL);
	g_return_if_fail (esheet->gnum_sheet->print_info != NULL);

	pi = esheet->gnum_sheet->print_info;
	ms_excel_print_unit_init_inch (&pi->margins.top, 1.0);
	ms_excel_print_unit_init_inch (&pi->margins.bottom, 1.0);
	ms_excel_print_unit_init_inch (&pi->margins.left, 0.75);
	ms_excel_print_unit_init_inch (&pi->margins.right, 0.75);
	ms_excel_print_unit_init_inch (&pi->margins.header, 0.5);
	ms_excel_print_unit_init_inch (&pi->margins.footer, 0.5);
}

static ExcelSheet *
ms_excel_sheet_new (ExcelWorkbook *wb, char const *sheet_name)
{
	static MSContainerClass const vtbl = {
		&ms_sheet_realize_obj,
		&ms_sheet_create_obj,
		&ms_sheet_parse_expr,
		&ms_sheet_sheet,
		&ms_sheet_get_fmt
	};

	ExcelSheet *esheet = g_new (ExcelSheet, 1);
	Sheet *sheet = workbook_sheet_by_name (wb->gnum_wb, sheet_name);

	if (sheet == NULL)
		sheet = sheet_new (wb->gnum_wb, sheet_name);

	esheet->wb         = wb;
	esheet->gnum_sheet = sheet;
	esheet->base_char_width         = -1;
	esheet->base_char_width_default = -1;
	esheet->freeze_panes		= FALSE;
	esheet->shared_formulae         =
		g_hash_table_new ((GHashFunc)&cellpos_hash,
				  (GCompareFunc)&cellpos_cmp);

	ms_excel_init_margins (esheet);
	ms_container_init (&esheet->container, &vtbl, &wb->container);

	/* in case nothing forces a spanning, do it here so that any new content
	 * will get spanned.
	 */
	sheet_flag_recompute_spans (sheet);

	return esheet;
}

static void
ms_excel_sheet_insert_val (ExcelSheet *esheet, int xfidx,
			   int col, int row, Value *v)
{
	Cell *cell;
	BiffXFData const *xf = ms_excel_get_xf (esheet, xfidx);

	g_return_if_fail (v);
	g_return_if_fail (esheet);
	g_return_if_fail (xf);

	ms_excel_set_xf (esheet, col, row, xfidx);
	cell = sheet_cell_fetch (esheet->gnum_sheet, col, row);
	cell_set_value (cell, v, xf->style_format);
}

static void
ms_excel_sheet_insert_blank (ExcelSheet *esheet, int xfidx,
			     int col, int row)
{
	g_return_if_fail (esheet);

	ms_excel_set_xf (esheet, col, row, xfidx);
}

static void
ms_excel_read_comment (BiffQuery *q, ExcelSheet *esheet)
{
	CellPos	pos;

	pos.row = EX_GETROW (q);
	pos.col = EX_GETCOL (q);

	if (esheet->container.ver >= MS_BIFF_V8) {
		guint16  options = MS_OLE_GET_GUINT16 (q->data + 4);
		gboolean hidden = (options & 0x2)==0;
		guint16  obj_id  = MS_OLE_GET_GUINT16 (q->data + 6);
		guint16  author_len = MS_OLE_GET_GUINT16 (q->data + 8);
		char *author;

		if (options & 0xffd)
			printf ("FIXME: Error in options\n");

		author = biff_get_text (author_len % 2 ? q->data + 11 : q->data + 10,
					author_len, NULL);
		d (1, printf ("Comment at %s%d id %d options"
			      " 0x%x hidden %d by '%s'\n",
			      col_name (pos.col), pos.row + 1,
			      obj_id, options, hidden, author););

		g_free (author);
	} else {
		guint len = MS_OLE_GET_GUINT16 (q->data + 4);
		GString *comment = g_string_sized_new (len);

		for (; len > 2048 ; len -= 2048) {
			guint16 opcode;

			g_string_append (comment, biff_get_text (q->data + 6, 2048, NULL));

			if (!ms_biff_query_peek_next (q, &opcode) ||
			    opcode != BIFF_NOTE || !ms_biff_query_next (q) ||
			    EX_GETROW (q) != 0xffff || EX_GETCOL (q) != 0) {
				g_warning ("Invalid Comment record");
				g_string_free (comment, TRUE);
				return;
			}
		}
		g_string_append (comment, biff_get_text (q->data + 6, len, NULL));

		d (2, printf ("Comment in %s%d: '%s'\n",
			      col_name (pos.col), pos.row + 1, comment->str););

		cell_set_comment (esheet->gnum_sheet, &pos, NULL, comment->str);
		g_string_free (comment, FALSE);
	}
}

static void
ms_excel_sheet_destroy (ExcelSheet *esheet)
{
	if (esheet->shared_formulae != NULL) {
		g_hash_table_foreach_remove (esheet->shared_formulae,
					     (GHRFunc)biff_shared_formula_destroy,
					     esheet);
		g_hash_table_destroy (esheet->shared_formulae);
		esheet->shared_formulae = NULL;
	}

	if (esheet->gnum_sheet) {
		sheet_destroy (esheet->gnum_sheet);
		esheet->gnum_sheet = NULL;
	}
	ms_container_finalize (&esheet->container);

	g_free (esheet);
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

static StyleFormat *
ms_wb_get_fmt (MSContainer const *container, guint16 indx)
{
	return ms_excel_wb_get_fmt (((ExcelWorkbook *)container), indx);
}

static ExcelWorkbook *
ms_excel_workbook_new (MsBiffVersion ver)
{
	static MSContainerClass const vtbl = {
		NULL, NULL,
		&ms_wb_parse_expr,
		NULL,
		&ms_wb_get_fmt,
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
	ans->format_data      = g_hash_table_new ((GHashFunc)biff_guint16_hash,
						  (GCompareFunc)biff_guint16_equal);
	ans->palette          = ms_excel_default_palette ();
	ans->global_strings   = NULL;
	ans->global_string_max  = 0;
	ans->read_drawing_group = 0;

	ans->warn_unsupported_images = TRUE;
	ans->warn_unsupported_graphs = TRUE;
	return ans;
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
	unsigned lp;

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
		unsigned i;
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
			tmp[lp + 4]= (lp > 0) ? ptr[lp]: (ptr[lp] & 0xfc);
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
 * ms_excel_read_name:
 * read a Name.  The workbook must be present, the sheet is optional.
 */
static void
ms_excel_read_name (BiffQuery *q, ExcelWorkbook *wb, ExcelSheet *esheet)
{
#if defined(excel95)
Opcode 0x 18 :            NAME, length 0x24 (=36)
       0 |  0  0  0  4 12  0  1  0  1  0  0  0  0  0 62 6f | ..............bo
      10 | 62 6f 3a ff ff  0  0  0  0  0  0  1  0  0  0  0 | bo:.............
      20 |  0  1  0  1 XX XX XX XX XX XX XX XX XX XX XX XX | ....************
       0 |  0  0  0  4 12  0  2  0  1  0  0  0  0  0 62 6f | ..............bo
      10 | 62 6f 3a fe ff  0  0  0  0  0  0  1  0  1  0  1 | bo:.............
      20 |  0  1  0  1 XX XX XX XX XX XX XX XX XX XX XX XX | ....************
       0 |  0  0  0  4 12  0  3  0  1  0  0  0  0  0 62 6f | ..............bo
      10 | 62 6f 3a fd ff  0  0  0  0  0  0  1  0  2  0  2 | bo:.............
      20 |  0  1  0  1 XX XX XX XX XX XX XX XX XX XX XX XX | ....************
#elif defined(excel2k)
Opcode 0x 18 :            NAME, length 0x1a (=26)
       0 |  0  0  0  4  7  0  0  0  1  0  0  0  0  0  0 62 | ...............b
      10 | 6f 62 6f 3a  0  0  1  0  1  0 XX XX XX XX XX XX | obo:......******
       0 |  0  0  0  4  7  0  0  0  2  0  0  0  0  0  0 62 | ...............b
      10 | 6f 62 6f 3a  1  0  1  0  1  0 XX XX XX XX XX XX | obo:......******
       0 |  0  0  0  4  7  0  0  0  3  0  0  0  0  0  0 62 | ...............b
      10 | 6f 62 6f 3a  2  0  1  0  1  0 XX XX XX XX XX XX | obo:......******
#endif
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
	/* FIXME FIXME FIXME: Offsets have moved alot between versions.
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

	/* FIXME FIXME FIXME: Disable for now */
	if (0 && name_len == 1 && *ptr <= 0x0c) {
		switch (*ptr) {
		case 0x00: name = "Consolidate_Area"; break;
		case 0x01: name = "Auto_Open"; break;
		case 0x02: name = "Auto_Close"; break;
		case 0x03: name = "Extract"; break;
		case 0x04: name = "Database"; break;
		case 0x05: name = "Criteria"; break;
		case 0x06: name = "Print_Area"; break;
		case 0x07: name = "Print_Titles"; break;
		case 0x08: name = "Recorder"; break;
		case 0x09: name = "Data_Form"; break;
		case 0x0a: name = "Auto_Activate"; break;
		case 0x0b: name = "Auto_Deactivate"; break;
		case 0x0c: name = "Sheet_Title"; break;
		default:   name = "ERROR ERROR ERROR.  This is impossible";
		}
		name = g_strdup (name);
	} else
		name = biff_get_text (ptr, name_len, NULL);
	ptr += name_len + name_def_len;
	menu_txt = biff_get_text (ptr, menu_txt_len, NULL);
	ptr += menu_txt_len;
	descr_txt = biff_get_text (ptr, descr_txt_len, NULL);
	ptr += descr_txt_len;
	help_txt = biff_get_text (ptr, help_txt_len, NULL);
	ptr += help_txt_len;
	status_txt = biff_get_text (ptr, status_txt_len, NULL);

	fn_grp_idx = (flags & 0xfc0)>>6;

	d (5, {
		printf ("Name record: '%s', '%s', '%s', '%s', '%s'\n",
			name ? name : "(null)",
			menu_txt ? menu_txt : "(null)",
			descr_txt ? descr_txt : "(null)",
			help_txt ? help_txt : "(null)",
			status_txt ? status_txt : "(null)");
		ms_ole_dump (name_def_data, name_def_len);

		/* Unpack flags */
		if ((flags & 0x0001) != 0)
			printf (" Hidden");
		if ((flags & 0x0002) != 0)
			printf (" Function");
		if ((flags & 0x0004) != 0)
			printf (" VB-Proc");
		if ((flags & 0x0008) != 0)
			printf (" Proc");
		if ((flags & 0x0010) != 0)
			printf (" CalcExp");
		if ((flags & 0x0020) != 0)
			printf (" BuiltIn");
		if ((flags & 0x1000) != 0)
			printf (" BinData");
		printf ("\n");
	});

	biff_name_data_new (wb, name, sheet_idx - 1,
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
ms_excel_externname (BiffQuery *q, ExcelWorkbook *wb, ExcelSheet *esheet)
{
	char const *name;
	guint8 *defn;
	guint16 defnlen;
	if (wb->container.ver >= MS_BIFF_V7) {
		guint16 flags = MS_OLE_GET_GUINT8 (q->data);
		guint32 namelen = MS_OLE_GET_GUINT8 (q->data + 6);

		name = biff_get_text (q->data + 7, namelen, &namelen);
		defn    = q->data + 7 + namelen;
		defnlen = MS_OLE_GET_GUINT16 (defn);
		defn += 2;

		switch (flags & 0x18) {
		case 0x00: /* external name */
		    break;
		case 0x01: /* DDE */
			printf ("FIXME: DDE links are no supported.\n"
				"Name '%s' will be lost.\n", name);
			return;
		case 0x10: /* OLE */
			printf ("FIXME: OLE links are no supported.\n"
				"Name '%s' will be lost.\n", name);
			return;
		default:
			g_warning ("EXCEL: Invalid external name type. ('%s')", name);
			return;
		}
	} else { /* Ancient Papyrus spec. */
		static guint8 data[] = { 0x1c, 0x17 }; /* Error: REF */
		defn = data;
		defnlen = 2;
		name = biff_get_text (q->data + 1,
				      MS_OLE_GET_GUINT8 (q->data), NULL);
	}

	biff_name_data_new (wb, name, -1, defn, defnlen, TRUE);
}

/**
 * base_char_width_for_read:
 * @esheet	the Excel sheet
 *
 * Measures base character width for column sizing.
 */
static double
base_char_width_for_read (ExcelSheet *esheet,
			  int xf_index, gboolean is_default)
{
	BiffXFData const *xf = ms_excel_get_xf (esheet, xf_index);
	BiffFontData const *fd = (xf != NULL)
		? ms_excel_get_font (esheet, xf->font_idx)
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
 * @q		A BIFF query
 * @esheet	The Excel sheet
 *
 * Processes a BIFF row info (BIFF_ROW) record. See: S59DDB.HTM
 */
static void
ms_excel_read_row (BiffQuery *q, ExcelSheet *esheet)
{
	const guint16 row = MS_OLE_GET_GUINT16 (q->data);
#if 0
	/* Unnecessary info for now.
	 * FIXME: do we want to preallocate baed on this info?
	 */
	const guint16 start_col = MS_OLE_GET_GUINT16 (q->data + 2);
	const guint16 end_col = MS_OLE_GET_GUINT16 (q->data + 4) - 1;
#endif
	const guint16 height = MS_OLE_GET_GUINT16 (q->data + 6);
	const guint16 flags = MS_OLE_GET_GUINT16 (q->data + 12);
	const guint16 flags2 = MS_OLE_GET_GUINT16 (q->data + 14);
	const guint16 xf = flags2 & 0xfff;

	/* If the bit is on it indicates that the row is of 'standard' height.
	 * However the remaining bits still include the size.
	 */
	gboolean const is_std_height = (height & 0x8000) != 0;

	d (1, {
		printf ("Row %d height 0x%x;\n", row + 1, height);
		if (is_std_height)
			puts ("Is Std Height");
		if (flags2 & 0x1000)
			puts ("Top thick");
		if (flags2 & 0x2000)
			puts ("Bottom thick");
	});

	/* TODO: Put mechanism in place to handle thick margins */
	/* TODO: Columns actually set the size even when it is the default.
	 *       Which approach is better?
	 */
	if (!is_std_height) {
		double hu = get_row_height_units (height);
		sheet_row_set_size_pts (esheet->gnum_sheet, row, hu, TRUE);
	}

	if (flags & 0x20)
		colrow_set_visibility (esheet->gnum_sheet, FALSE, FALSE, row, row);

	if (flags & 0x80) {
		if (xf != 0)
			ms_excel_set_xf_segment (esheet, 0, SHEET_MAX_COLS - 1,
						 row, row, xf);
		d (1, printf ("row %d has flags 0x%x a default style %hd;\n",
			      row + 1, flags, xf););
	}

	if ((unsigned)(flags & 0x7) > 0)
		colrow_set_outline (sheet_row_fetch (esheet->gnum_sheet, row),
			(unsigned)(flags & 0x7), flags & 0x10);
}

static void
ms_excel_read_tab_color (BiffQuery *q, ExcelSheet *esheet)
{
	/* this is a guess, but the only field I see
	 * changing seems to be the colour.
	 */
#if 0
 0 | 62  8  0  0  0  0  0  0  0  0  0  0 14  0  0  0 | b...............
10 |     0  0  0 XX XX XX XX XX XX XX XX XX XX XX XX |  ...************
#endif
	guint8 color_index;
	StyleColor *color;

	g_return_if_fail (q->length == 20);

	/* be conservative for now, we have not seen a pallete larger than 56
	 * so this is largely moot, this is probably a uint32
	 */
	color_index = MS_OLE_GET_GUINT8 (q->data + 16);
	color = ms_excel_palette_get (esheet->wb->palette, color_index);
	sheet_set_tab_color (esheet->gnum_sheet, color);

	if (color != NULL) {
		d (1, printf ("%s tab colour = %04hx:%04hx:%04hx\n",
			      esheet->gnum_sheet->name_unquoted,
			      color->red, color->green, color->blue););
	}
}

/**
 * ms_excel_read_colinfo:
 * @q		A BIFF query
 * @esheet	The Excel sheet
 *
 * Processes a BIFF column info (BIFF_COLINFO) record. See: S59D67.HTM
 */
static void
ms_excel_read_colinfo (BiffQuery *q, ExcelSheet *esheet)
{
	int lp;
	float col_width;
	const guint16 firstcol = MS_OLE_GET_GUINT16 (q->data);
	guint16       lastcol  = MS_OLE_GET_GUINT16 (q->data + 2);
	guint16       width    = MS_OLE_GET_GUINT16 (q->data + 4);
	guint16 const xf       = MS_OLE_GET_GUINT16 (q->data + 6);
	guint16 const options  = MS_OLE_GET_GUINT16 (q->data + 8);
	gboolean hidden = (options & 0x0001) ? TRUE : FALSE;
	gboolean const collapsed = (options & 0x1000) ? TRUE : FALSE;
	unsigned const outline_level = (unsigned)((options >> 8) & 0x7);

	g_return_if_fail (firstcol < SHEET_MAX_COLS);

	/* Widths are quoted including margins
	 * If the width is less than the minimum margins something is lying
	 * hide it and give it default width.
	 * NOTE: These measurements do NOT correspond to what is
	 * shown to the user
	 */
	if (width >= 4) {
		col_width = base_char_width_for_read (esheet, xf, FALSE) *
			width / 256.;
	} else {
		if (width > 0)
			hidden = TRUE;
		/* Columns are of default width */
		col_width = esheet->gnum_sheet->cols.default_style.size_pts;
	}

	d (1, {
		printf ("Column Formatting from col %d to %d of width "
			"%hu/256 characters (%f pts)\n", firstcol, lastcol,
			width, col_width);
		printf ("Options %hd, default style %hd from col %d to %d\n",
			options, xf, firstcol, lastcol);
	});

	/* NOTE: seems like this is inclusive firstcol, inclusive lastcol */
	if (lastcol >= SHEET_MAX_COLS)
		lastcol = SHEET_MAX_COLS - 1;
	for (lp = firstcol; lp <= lastcol; ++lp) {
		sheet_col_set_size_pts (esheet->gnum_sheet, lp, col_width, TRUE);
		if (outline_level > 0)
			colrow_set_outline (sheet_col_fetch (esheet->gnum_sheet, lp),
				outline_level, collapsed);
	}

	if (xf != 0)
		ms_excel_set_xf_segment (esheet, firstcol, lastcol,
					 0, SHEET_MAX_ROWS - 1, xf);

	if (hidden)
		colrow_set_visibility (esheet->gnum_sheet, TRUE, FALSE,
					firstcol, lastcol);
}

void
ms_excel_read_imdata (BiffQuery *q)
{
	guint16 op;

	d (1,{
		char const *from_name;
		char const *format_name;
		guint16 const from_env = MS_OLE_GET_GUINT16 (q->data + 2);
		guint16 const format = MS_OLE_GET_GUINT16 (q->data + 2);

		switch (from_env) {
		case 1: from_name = "Windows"; break;
		case 2: from_name = "Macintosh"; break;
		default: from_name = "Unknown environment?"; break;
		};
		switch (format) {
		case 0x2:
		format_name = (from_env == 1) ? "windows metafile" : "mac pict";
		break;

		case 0x9: format_name = "windows native bitmap"; break;
		case 0xe: format_name = "'native format'"; break;
		default: format_name = "Unknown format?"; break;
		};

		printf ("Picture from %s in %s format\n",
			from_name, format_name);
	});

	while (ms_biff_query_peek_next (q, &op) && op == BIFF_CONTINUE)
		ms_biff_query_next (q);
}

/* S59DE2.HTM */
static void
ms_excel_read_selection (BiffQuery *q, ExcelSheet *esheet)
{
	/* int const pane_number	= MS_OLE_GET_GUINT8 (q->data); */
	int const act_row	= MS_OLE_GET_GUINT16 (q->data + 1);
	int const act_col	= MS_OLE_GET_GUINT16 (q->data + 3);
	int num_refs		= MS_OLE_GET_GUINT16 (q->data + 7);
	guint8 *refs;

	d (1, printf ("Start selection\n"););
	d (6, printf ("Cursor: %d %d\n", act_col, act_row););

	/* FIXME : pane_number will be relevant for split panes.
	 * because frozen panes are bound together this does not matter.
	 */
	sheet_selection_reset (esheet->gnum_sheet);
	for (refs = q->data + 9; num_refs > 0; refs += 6, num_refs--) {
		int const start_row = MS_OLE_GET_GUINT16 (refs + 0);
		int const start_col = MS_OLE_GET_GUINT8 (refs + 4);
		int const end_row   = MS_OLE_GET_GUINT16 (refs + 2);
		int const end_col   = MS_OLE_GET_GUINT8 (refs + 5);
		d (6, printf ("Ref %d = %d %d %d %d\n", num_refs,
			      start_col, start_row, end_col, end_row););

		/* FIXME: This should not trigger a recalc */
		sheet_selection_add_range (esheet->gnum_sheet,
					   start_col, start_row,
					   start_col, start_row,
					   end_col, end_row);
	}
#if 0
	/* FIXME: Disable for now.  We need to reset the index of the
	 *         current selection range too.  This can do odd things
	 *         if the last range is NOT the currently selected range.
	 */
	sheet_cursor_move (esheet->gnum_sheet, act_col, act_row, FALSE, FALSE);
#endif

	d (1, printf ("Done selection\n"););
}

/**
 * ms_excel_read_default_row_height:
 * @q		A BIFF query
 * @esheet	The Excel sheet
 *
 * Processes a BIFF default row height (BIFF_DEFAULTROWHEIGHT) record.
 * See: S59D72.HTM
 */
static void
ms_excel_read_default_row_height (BiffQuery *q, ExcelSheet *esheet)
{
	const guint16 flags = MS_OLE_GET_GUINT16 (q->data);
	const guint16 height = MS_OLE_GET_GUINT16 (q->data + 2);
	double height_units;

	d (1, {
		printf ("Default row height 0x%x;\n", height);
		if (flags & 0x04)
			printf (" + extra space above;\n");
		if (flags & 0x08)
			printf (" + extra space below;\n");
	});

	height_units = get_row_height_units (height);
	sheet_row_set_default_size_pts (esheet->gnum_sheet, height_units);
}

/**
 * ms_excel_read_default_col_width:
 * @q		A BIFF query
 * @esheet	The Excel sheet
 *
 * Processes a BIFF default column width (BIFF_DEFCOLWIDTH) record.
 * See: S59D73.HTM
 */
static void
ms_excel_read_default_col_width (BiffQuery *q, ExcelSheet *esheet)
{
	const guint16 width = MS_OLE_GET_GUINT16 (q->data);
	double col_width;

	/* Use the 'Normal' Style which is by definition the 0th */
	if (esheet->base_char_width_default <= 0)
		esheet->base_char_width_default =
			base_char_width_for_read (esheet, 0, TRUE);

	d (0, printf ("Default column width %hu characters\n", width););

	/*
	 * According to the tooltip the default width is 8.43 character widths
	 *   and does not include margins or the grid line.
	 * According to the saved data the default width is 8 character widths
	 *   includes the margins and grid line, but uses a different notion of
	 *   how big a char width is.
	 * According to saved data a column with the same size a the default has
	 *   9.00? char widths.
	 */
	col_width = width * esheet->base_char_width_default;

	sheet_col_set_default_size_pts (esheet->gnum_sheet, col_width);
}

static void
ms_excel_read_guts (BiffQuery *q, ExcelSheet *esheet)
{
	int col_gut, row_gut;

	g_return_if_fail (q->length == 8);

	/* ignore the specification of how wide/tall the gutters are */

	row_gut = MS_OLE_GET_GUINT16 (q->data + 4);
	if (row_gut >= 1)
		row_gut--;
	col_gut = MS_OLE_GET_GUINT16 (q->data + 6);
	if (col_gut >= 1)
		col_gut--;
	sheet_colrow_gutter (esheet->gnum_sheet, TRUE, col_gut);
	sheet_colrow_gutter (esheet->gnum_sheet, FALSE, row_gut);
}

/* See: S59DE3.HTM */
static void
ms_excel_read_setup (BiffQuery *q, ExcelSheet *esheet)
{
	PrintInformation *pi = esheet->gnum_sheet->print_info;
	guint16  grbit;

	g_return_if_fail (q->length == 34);

	grbit = MS_OLE_GET_GUINT16 (q->data + 10);

	pi->print_order = (grbit & 0x1)
		? PRINT_ORDER_RIGHT_THEN_DOWN
		: PRINT_ORDER_DOWN_THEN_RIGHT;

	/* If the extra info is valid use it */
	if ((grbit & 0x4) != 0x4) {
		pi->n_copies = MS_OLE_GET_GUINT16 (q->data + 32);
		/* 0x40 == orientation is set */
		if ((grbit & 0x40) != 0x40) {
			pi->orientation = (grbit & 0x2)
				? PRINT_ORIENT_VERTICAL
				: PRINT_ORIENT_HORIZONTAL;
		}
		pi->scaling.percentage = MS_OLE_GET_GUINT16 (q->data + 2);
		if (pi->scaling.percentage < 1. || pi->scaling.percentage > 1000.) {
			g_warning ("setting invalid print scaling (%f) to 100%%",
				   pi->scaling.percentage);
			pi->scaling.percentage = 100.;
		}

#if 0
		/* Useful somewhere ? */
		printf ("Paper size %hu resolution %hu vert. res. %hu\n",
			MS_OLE_GET_GUINT16 (q->data +  0),
			MS_OLE_GET_GUINT16 (q->data + 12),
			MS_OLE_GET_GUINT16 (q->data + 14));
#endif
	}

	pi->print_black_and_white = (grbit & 0x8) == 0x8;
	pi->print_as_draft        = (grbit & 0x10) == 0x10;
	/* FIXME: print comments (grbit & 0x20) == 0x20 */

#if 0
	/* We probably can't map page->page accurately. */
	if ((grbit & 0x80) == 0x80)
		printf ("Starting page number %d\n",
			MS_OLE_GET_GUINT16 (q->data +  4));
#endif

	/* We do not support SIZE_FIT yet */
	pi->scaling.type = PERCENTAGE;
#if 0
	{
		guint16  fw, fh;
		fw = MS_OLE_GET_GUINT16 (q->data + 6);
		fh = MS_OLE_GET_GUINT16 (q->data + 8);
		if (fw > 0 && fh > 0) {
			pi->scaling.type = SIZE_FIT;
			pi->scaling.dim.cols = fw;
			pi->scaling.dim.rows = fh;
		}
	}
#endif

	ms_excel_print_unit_init_inch (&pi->margins.header,
		gnumeric_get_le_double (q->data + 16));
	ms_excel_print_unit_init_inch (&pi->margins.footer,
		gnumeric_get_le_double (q->data + 24));
}

static guint8 const *
ms_excel_read_range (Range *r, guint8 const *data)
{
	r->start.row = MS_OLE_GET_GUINT16 (data);
	r->end.row = MS_OLE_GET_GUINT16   (data + 2);
	r->start.col = MS_OLE_GET_GUINT16 (data + 4);
	r->end.col = MS_OLE_GET_GUINT16   (data + 6);
	d (4, range_dump (r, "\n"););

	return data + 8;
}

/*
 * No documentation exists for this record, but this makes
 * sense given the other record formats.
 */
static void
ms_excel_read_mergecells (BiffQuery *q, ExcelSheet *esheet)
{
	int num_merged = MS_OLE_GET_GUINT16 (q->data);
	guint8 const *data = q->data + 2;
	Range r;

	g_return_if_fail (q->length == (unsigned int)(2 + 8 * num_merged));

	while (num_merged-- > 0) {
		data = ms_excel_read_range (&r, data);
		sheet_merge_add (NULL, esheet->gnum_sheet, &r, FALSE);
	}
}

static void
ms_excel_biff_dimensions (BiffQuery *q, ExcelWorkbook *wb)
{
	Range r;

	/* What the heck was a 0x00 ? */
	if (q->opcode != 0x200)
		return;

	if (wb->container.ver >= MS_BIFF_V8)
	{
		r.start.row = MS_OLE_GET_GUINT32 (q->data);
		r.end.row   = MS_OLE_GET_GUINT32 (q->data + 4);
		r.start.col = MS_OLE_GET_GUINT16 (q->data + 8);
		r.end.col   = MS_OLE_GET_GUINT16 (q->data + 10);
	} else
		ms_excel_read_range (&r, q->data);

	d (0, printf ("Dimension = %s\n", range_name (&r)););
}

static MSContainer *
sheet_container (ExcelSheet *esheet)
{
	ms_container_set_blips (&esheet->container, esheet->wb->container.blips);
	return &esheet->container;
}

static gboolean
ms_excel_read_PROTECT (BiffQuery *q, char const *obj_type)
{
	/* TODO: Use this information when gnumeric supports protection */
	gboolean is_protected = TRUE;

	/* MS Docs fail to mention that in some stream this
	 * record can have size zero.  I assume the in that
	 * case its existence is the flag.
	 */
	if (q->length > 0)
		is_protected = (1 == MS_OLE_GET_GUINT16 (q->data));

	d (1,if (is_protected) printf ("%s is protected\n", obj_type););

	return is_protected;
}

static void
ms_excel_read_wsbool (BiffQuery *q, ExcelSheet *esheet)
{
	guint16 options;

	g_return_if_fail (q->length == 2);

	options = MS_OLE_GET_GUINT16 (q->data);
	/* 0x0001 automatic page breaks are visible */
	/* 0x0010 the sheet is a dialog sheet */
	/* 0x0020 automatic styles are not applied to an outline */
	esheet->gnum_sheet->outline_symbols_below = 0 != (options & 0x040);
	esheet->gnum_sheet->outline_symbols_right = 0 != (options & 0x080);
	/* 0x0100 the Fit option is on (Page Setup dialog box, Page tab) */
	esheet->gnum_sheet->display_outlines      = 0 != (options & 0x600);
}

static void
ms_excel_read_calccount (BiffQuery *q, ExcelWorkbook *wb)
{
	guint16 count;

	g_return_if_fail (q->length == 2);

	count = MS_OLE_GET_GUINT16 (q->data);
	workbook_iteration_max_number (wb->gnum_wb, count);
}

static void
ms_excel_read_delta (BiffQuery *q, ExcelWorkbook *wb)
{
	double tolerance;

	/* samples/excel/dbfuns.xls has as sample of this record */
	if (q->opcode == BIFF_UNKNOWN_1)
		return;

	g_return_if_fail (q->length == 8);

	tolerance = gnumeric_get_le_double (q->data);
	workbook_iteration_tolerance (wb->gnum_wb, tolerance);
}

static void
ms_excel_read_iteration (BiffQuery *q, ExcelWorkbook *wb)
{
	guint16 enabled;

	g_return_if_fail (q->length == 2);

	enabled = MS_OLE_GET_GUINT16 (q->data);
	workbook_iteration_enabled (wb->gnum_wb, enabled != 0);
}

static void
ms_excel_read_pane (BiffQuery *q, ExcelSheet *esheet, WorkbookView *wb_view)
{
	if (esheet->freeze_panes) {
		guint16 x = MS_OLE_GET_GUINT16 (q->data + 0);
		guint16 y = MS_OLE_GET_GUINT16 (q->data + 2);
		guint16 rwTop = MS_OLE_GET_GUINT16 (q->data + 4);
		guint16 colLeft = MS_OLE_GET_GUINT16 (q->data + 6);
		Sheet *sheet = esheet->gnum_sheet;
		CellPos frozen, unfrozen;

		frozen = unfrozen = sheet->initial_top_left;
		unfrozen.col += x; unfrozen.row += y;
		sheet_freeze_panes (sheet, &frozen, &unfrozen);
		sheet_set_initial_top_left (sheet, colLeft, rwTop);
	} else {
		g_warning ("EXCEL : no support for split panes yet");
	}
}

static void
ms_excel_read_window2 (BiffQuery *q, ExcelSheet *esheet, WorkbookView *wb_view)
{
	if (q->length >= 10) {
		const guint16 options    = MS_OLE_GET_GUINT16 (q->data + 0);
		/* coords are 0 based */
		guint16 top_row    = MS_OLE_GET_GUINT16 (q->data + 2);
		guint16 left_col   = MS_OLE_GET_GUINT16 (q->data + 4);

		esheet->gnum_sheet->display_formulas	= (options & 0x0001) != 0;
		esheet->gnum_sheet->hide_grid		= (options & 0x0002) == 0;
		esheet->gnum_sheet->hide_col_header =
		esheet->gnum_sheet->hide_row_header	= (options & 0x0004) == 0;
		esheet->freeze_panes			= (options & 0x0008) != 0;
		esheet->gnum_sheet->hide_zero		= (options & 0x0010) == 0;

		/* NOTE : This is top left of screen even if frozen, modify when
		 *        we read PANE
		 */
		sheet_set_initial_top_left (esheet->gnum_sheet, left_col, top_row);

#if 0
		if (!(options & 0x0020)) {
			guint32 const grid_color = MS_OLE_GET_GUINT32 (q->data + 6);
			/* This is quicky fake code to express the idea */
			set_grid_and_header_color (get_color_from_index (grid_color));
			d (2, printf ("Default grid & pattern color = 0x%hx\n",
				      grid_color););
		}
#endif

		d (0, if (options & 0x0200) printf ("Sheet flag selected\n"););

		if (options & 0x0400)
			wb_view_sheet_focus (wb_view, esheet->gnum_sheet);
	}

	if (q->length >= 14) {
		d (2, {
			const guint16 pageBreakZoom = MS_OLE_GET_GUINT16 (q->data + 10);
			const guint16 normalZoom = MS_OLE_GET_GUINT16 (q->data + 12);
			printf ("%hx %hx\n", normalZoom, pageBreakZoom);
		});
	}
}

static void
ms_excel_read_cf (BiffQuery *q, ExcelSheet *esheet)
{
	guint8 const type	= MS_OLE_GET_GUINT8 (q->data + 0);
	guint8 const op		= MS_OLE_GET_GUINT8 (q->data + 1);
	guint16 const expr1_len	= MS_OLE_GET_GUINT8 (q->data + 2);
	guint16 const expr2_len	= MS_OLE_GET_GUINT8 (q->data + 4);
	guint8 const fmt_type	= MS_OLE_GET_GUINT8 (q->data + 9);
	unsigned offset;
	ExprTree *expr1 = NULL, *expr2 = NULL;

	d(-1, printf ("cond type = %d, op type = %d\n", (int)type, (int)op););
#if 0
	switch (type) {
	case 1 :
		switch( op ) {
		case 0x01 : cond1 = SCO_GREATER_EQUAL;
			    cond2 = SCO_LESS_EQUAL;	break;
		case 0x02 : cond1 = SCO_LESS_EQUAL;
			    cond2 = SCO_GREATER_EQUAL;	break;
		case 0x03 : cond1 = SCO_EQUAL;		break;
		case 0x04 : cond1 = SCO_NOT_EQUAL;	break;
		case 0x05 : cond1 = SCO_GREATER;	break;
		case 0x06 : cond1 = SCO_LESS;		break;
		case 0x07 : cond1 = SCO_GREATER_EQUAL;	break;
		case 0x08 : cond1 = SCO_LESS_EQUAL;	break;
		default:
			g_warning ("EXCEL : Unknown condition (%d) for conditional format in sheet %s.",
				   op, esheet->gnum_sheet->name_unquoted);
			return;
		}
		break;
	case 2 : cond1 = SCO_BOOLEAN_EXPR;
		 break;

	default :
		g_warning ("EXCEL : Unknown condition type (%d) for format in sheet %s.",
			   (int)type, esheet->gnum_sheet->name_unquoted);
		return;
	};
#endif

	if (expr1_len > 0) {
		expr1 = ms_sheet_parse_expr_internal (esheet,
			q->data + q->length - expr1_len - expr2_len,
			expr1_len);
	}
	if (expr2_len > 0) {
		expr2 = ms_sheet_parse_expr_internal (esheet,
			q->data + q->length - expr2_len,
			expr2_len);
	}

	puts ("Header");
	ms_ole_dump (q->data+6, 6);

	/* UNDOCUMENTED : the format of the conditional format
	 * is unspecified.
	 *
	 * header == 6
	 *	0xff : I'll guess fonts
	 *	uint8 : 0xff = no border
	 *		0xf7 = R
	 *		0xfb = L
	 *		0xef = T
	 *		0xdf = B
	 *		0xc3 == T,L,B,R
	 *	uint8 : 0x3f == no pattern elements,
	 *		0x3b == fore colour
	 *		0x3a == fore colour & pattern
	 *		0x38 == fore_colour & pattern & pattern_color
	 *	uint8 : 0x04 = font | 0x10 = border | 0x20 = colour
	 *	0x02 : ?
	 *	0x00 : ?
	 * 
	 * font   == 118
	 * border == 8
	 * colour == 4
	 *	Similar to XF from biff7
	 */

	offset =  6  /* CF record header */ + 6; /* format header */

	if (fmt_type & 0x04) { /* font */
		puts ("Font");
		ms_ole_dump (q->data+offset, 118);

		offset += 118;
	}

	if (fmt_type & 0x10) { /* borders */
		puts ("Border");
		ms_ole_dump (q->data+offset, 8);

		offset += 8;
	}

	if (fmt_type & 0x20) { /* pattern */
		/* TODO : use the head flags to conditionally set things
		 * FIXME : test this
		 */
		guint16 tmp = MS_OLE_GET_GUINT16 (q->data + offset);
		int pat_foregnd_col = (tmp & 0x007f);
		int pat_backgnd_col = (tmp & 0x1f80) >> 7;
		int fill_pattern_idx;

		tmp = MS_OLE_GET_GUINT16 (q->data + offset + 2);
		fill_pattern_idx =
			excel_map_pattern_index_from_excel ((tmp >> 10) & 0x3f);

		/* Solid patterns seem to reverse the meaning */
		if (fill_pattern_idx == 1) {
			int swap = pat_backgnd_col;
			pat_backgnd_col = pat_foregnd_col;
			pat_foregnd_col = swap;
		}

		printf ("fore = %d, back = %d, pattern = %d.\n",
			pat_foregnd_col,
			pat_backgnd_col,
			fill_pattern_idx);

		offset += 4;
	}


	g_return_if_fail (q->length == offset + expr1_len + expr2_len);
	ms_ole_dump (q->data+6, 6);
#if 0
	printf ("%d == %d (%d + %d + %d) (0x%x)\n",
		q->length, offset + expr1_len + expr2_len,
		offset, expr1_len, expr2_len, fmt_type);
#endif
}

static void
ms_excel_read_condfmt (BiffQuery *q, ExcelSheet *esheet)
{
	guint16 num_fmts, options, num_areas;
	Range  region;
	unsigned i;
	guint8 const *data;
	
	g_return_if_fail (q->length >= 14);

	num_fmts = MS_OLE_GET_GUINT16 (q->data + 0);
	options  = MS_OLE_GET_GUINT16 (q->data + 2);
	num_areas = MS_OLE_GET_GUINT16 (q->data + 12);

	d(1, printf ("Num areas == %hu\n", num_areas););
#if 0
	/* The bounding box or the region containing all conditional formats.
	 * It seems like this region is 0,0 -> 0xffff,0xffff when there are no
	 * regions.
	 */
	if (num_areas > 0)
		ms_excel_read_range (&region, q->data+4);
#endif

	data = q->data + 14;
	for (i = 0 ; i < num_areas && (data+8) <= (q->data + q->length) ; i++)
		data = ms_excel_read_range (&region, data);

	g_return_if_fail (data == q->data + q->length);

	for (i = 0 ; i < num_fmts ; i++) {
		guint16 next;
		if (!ms_biff_query_peek_next (q, &next) || next != BIFF_CF) {
			g_warning ("EXCEL: missing CF record");
			return;
		}
		ms_biff_query_next (q);
		ms_excel_read_cf (q, esheet);
	}
}

static void
ms_excel_read_dv (BiffQuery *q, ExcelSheet *esheet)
{
	ExprTree *expr1 = NULL, *expr2 = NULL;
	int       expr1_len,     expr2_len;
	char *input_msg, *error_msg, *input_title, *error_title;
	guint32	options, len;
	guint8 const *data, *expr1_dat, *expr2_dat;
	int i;
	Range r;
	ValidationStyle style;
	ValidationType  type;
	ValidationOp    op;
	GSList *ptr, *ranges = NULL;
	MStyle *mstyle;

	g_return_if_fail (q->length >= 4);
	options	= MS_OLE_GET_GUINT32 (q->data);
	data = q->data + 4;

	g_return_if_fail (data < (q->data + q->length));
	input_title = biff_get_text (data + 2, MS_OLE_GET_GUINT8 (data), &len);
	data += len + 2;

	g_return_if_fail (data < (q->data + q->length));
	error_title = biff_get_text (data + 2, MS_OLE_GET_GUINT8 (data), &len);
	data += len + 2;

	g_return_if_fail (data < (q->data + q->length));
	input_msg = biff_get_text (data + 2, MS_OLE_GET_GUINT8 (data), &len);
	data += len + 2;

	g_return_if_fail (data < (q->data + q->length));
	error_msg = biff_get_text (data + 2, MS_OLE_GET_GUINT8 (data), &len);
	data += len + 2;

	d(1, {
		printf ("Input Title : '%s'\n", input_title);
		printf ("Input Msg   : '%s'\n", input_msg);
		printf ("Error Title : '%s'\n", error_title);
		printf ("Error Msg   : '%s'\n", error_msg);
	});

	expr1_len = MS_OLE_GET_GUINT16 (data);
	d (5, printf ("Unknown = %hu\n", MS_OLE_GET_GUINT16 (data+2)););
	expr1_dat = data  + 4;	/* TODO : What are the missing 2 bytes ? */
	data += expr1_len + 4;
	g_return_if_fail (data < (q->data + q->length));

	expr2_len = MS_OLE_GET_GUINT16 (data);
	d (5, printf ("Unknown = %hu\n", MS_OLE_GET_GUINT16 (data+2)););
	expr2_dat = data  + 4;	/* TODO : What are the missing 2 bytes ? */
	data += expr2_len + 4;
	g_return_if_fail (data < (q->data + q->length));
	len = MS_OLE_GET_GUINT16 (data);

	i = MS_OLE_GET_GUINT16 (data);
	for (data += 2; i-- > 0 ;) {
		data = ms_excel_read_range (&r, data);
		ranges = g_slist_prepend (ranges, range_dup (&r));
	}

	/* these enums align, but lets be explicit so that the filter
	 * is easier to read.
	 */
	switch (options & 0x0f) {
	case 0 : type = VALIDATION_TYPE_ANY;		break;
	case 1 : type = VALIDATION_TYPE_AS_INT;		break;
	case 2 : type = VALIDATION_TYPE_AS_NUMBER;	break;
	case 3 : type = VALIDATION_TYPE_IN_LIST;	break;
	case 4 : type = VALIDATION_TYPE_AS_DATE;	break;
	case 5 : type = VALIDATION_TYPE_AS_TIME;	break;
	case 6 : type = VALIDATION_TYPE_TEXT_LENGTH;	break;
	case 7 : type = VALIDATION_TYPE_CUSTOM;		break;
	default :
		g_warning ("EXCEL : Unknown contraint type %d", options & 0x0f);
		return;
	};

	switch ((options >> 4) & 0x07) {
	case 0 : style = VALIDATION_STYLE_STOP; break;
	case 1 : style = VALIDATION_STYLE_WARNING; break;
	case 2 : style = VALIDATION_STYLE_INFO; break;
	default :
		g_warning ("EXCEL : Unknown validation style %d",
			   (options >> 4) & 0x07);
		return;
	};

	switch ((options >> 20) & 0x0f) {
	case 0:	op = VALIDATION_OP_BETWEEN;	break;
	case 1:	op = VALIDATION_OP_NOT_BETWEEN; break;
	case 2:	op = VALIDATION_OP_EQUAL;	break;
	case 3:	op = VALIDATION_OP_NOT_EQUAL;	break;
	case 4:	op = VALIDATION_OP_GT;		break;
	case 5:	op = VALIDATION_OP_LT;		break;
	case 6:	op = VALIDATION_OP_GTE;		break;
	case 7:	op = VALIDATION_OP_LTE;		break;

	default :
		g_warning ("EXCEL : Unknown contraint operator %d",
			   (options >> 20) & 0x0f);
		return;
	};

	if (expr1_len > 0)
		expr1 = ms_sheet_parse_expr_internal (esheet,
			expr1_dat, expr1_len);
	if (expr2_len > 0)
		expr2 = ms_sheet_parse_expr_internal (esheet,
			expr2_dat, expr2_len);

	d (1, printf ("style = %d, type = %d, op = %d\n", 
		       style, type, op););

	mstyle = mstyle_new ();
	mstyle_set_validation (mstyle,
		validation_new (style, type, op, error_title, error_msg, 
			expr1, expr2, options & 0x0100, options & 0x0200));

	for (ptr = ranges; ptr != NULL ; ptr = ptr->next) {
		Range *r = ptr->data;
		mstyle_ref (mstyle);
		sheet_style_apply_range (esheet->gnum_sheet, r, mstyle);
		g_free (r);
	}
	g_slist_free (ranges);
	mstyle_unref (mstyle);
}

static void
ms_excel_read_dval (BiffQuery *q, ExcelSheet *esheet)
{
	guint16 options;
	guint32 input_coord_x, input_coord_y, drop_down_id, dv_count;
	unsigned i;

	g_return_if_fail (q->length == 18);

	options	      = MS_OLE_GET_GUINT16 (q->data + 0);
	input_coord_x = MS_OLE_GET_GUINT32 (q->data + 2);
	input_coord_y = MS_OLE_GET_GUINT32 (q->data + 6);
	drop_down_id  = MS_OLE_GET_GUINT32 (q->data + 10);
	dv_count      = MS_OLE_GET_GUINT32 (q->data + 14);

	d(5, if (options & 0x1) printf ("DV input window is closed"););
	d(5, if (options & 0x2) printf ("DV input window is pinned"););
	d(5, if (options & 0x4) printf ("DV info has been cached ??"););

	for (i = 0 ; i < dv_count ; i++) {
		guint16 next;
		if (!ms_biff_query_peek_next (q, &next) || next != BIFF_DV) {
			g_warning ("EXCEL: missing DV record");
			return;
		}
		ms_biff_query_next (q);
		ms_excel_read_dv (q, esheet);
	}
}

static void
ms_excel_read_bg_pic (BiffQuery *q, ExcelSheet *esheet)
{
	/* Looks like a bmp.  OpenCalc has a basic parser for 24 bit files */
}

static gboolean
ms_excel_read_sheet (BiffQuery *q, ExcelWorkbook *wb,
                     WorkbookView *wb_view, ExcelSheet *esheet,
		     IOContext *io_context)
{
	PrintInformation *pi;

	g_return_val_if_fail (wb != NULL, FALSE);
	g_return_val_if_fail (esheet != NULL, FALSE);
	g_return_val_if_fail (esheet->gnum_sheet != NULL, FALSE);
	g_return_val_if_fail (esheet->gnum_sheet->print_info != NULL, FALSE);

	pi = esheet->gnum_sheet->print_info;

	d (1, printf ("----------------- '%s' -------------\n",
		      esheet->gnum_sheet->name_unquoted););

	for (; ms_biff_query_next (q) ;
	     value_io_progress_update (io_context, q->streamPos)) {

		d (5, printf ("Opcode: 0x%x\n", q->opcode););

		if (q->ms_op == 0x10) {
			/* HACK: it seems that in older versions of XL the
			 * charts did not have a wrapper object.  the first
			 * record in the sequence of chart records was a
			 * CHART_UNITS followed by CHART_CHART.  We play off of
			 * that.  When we encounter a CHART_units record we
			 * jump to the chart handler which then starts parsing
			 * at the NEXT record.
			 */
			if (q->opcode == BIFF_CHART_units) {
				GnmGraph *graph =
#ifdef ENABLE_BONOBO
					gnm_graph_new (esheet->wb->gnum_wb);
#else
					NULL;
#endif
				ms_excel_chart (q, sheet_container (esheet),
						esheet->container.ver,
						GTK_OBJECT (graph));
			} else
				puts ("EXCEL: How are we seeing chart records in a sheet ?");
			continue;
		} else if (q->ms_op == 0x01) {
			switch (q->opcode) {
			case BIFF_CODENAME:
				break;
			case BIFF_CF:
				g_warning ("Found a CF record without a CONDFMT ??");
				ms_excel_read_cf (q, esheet);
				break;
			case BIFF_CONDFMT:
				ms_excel_read_condfmt (q, esheet);
				break;
			case BIFF_DV:
				g_warning ("Found a DV record without a DVal ??");
				ms_excel_read_dv (q, esheet);
				break;

			case BIFF_DVAL:
				ms_excel_read_dval (q, esheet);
				break;

			default:
				ms_excel_unexpected_biff (q, "Sheet", ms_excel_read_debug);
			};
			continue;
		}

		switch (q->ls_op) {
		case BIFF_DIMENSIONS:	/* 2, NOT 1,10 */
			ms_excel_biff_dimensions (q, wb);
			break;

		case BIFF_BLANK: {
			guint16 const xf = EX_GETXF (q);
			guint16 const col = EX_GETCOL (q);
			guint16 const row = EX_GETROW (q);
			d (0, printf ("Blank in %s%d xf = 0x%x;\n", col_name (col), row + 1, xf););

			ms_excel_sheet_insert_blank (esheet, xf, col, row);
			break;
		}

		case BIFF_NUMBER: { /* S59DAC.HTM */
			Value *v = value_new_float (gnumeric_get_le_double (q->data + 6));
			d (2, printf ("Read number %g\n",
				      gnumeric_get_le_double (q->data + 6)););

			ms_excel_sheet_insert_val (esheet, EX_GETXF (q), EX_GETCOL (q),
						   EX_GETROW (q), v);
			break;
		}

		case BIFF_LABEL: { /* See: S59D9D.HTM */
			char *label;
			ms_excel_sheet_insert (esheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q),
				(label = biff_get_text (q->data + 8, EX_GETSTRLEN (q), NULL)));
			g_free (label);
			break;
		}

		case BIFF_BOOLERR: { /* S59D5F.HTM */
			Value *v;
			const guint8 val = MS_OLE_GET_GUINT8 (q->data + 6);
			if (MS_OLE_GET_GUINT8 (q->data + 7)) {
				/* FIXME: Init EvalPos */
				v = value_new_error (NULL,
						     biff_get_error_text (val));
			} else
				v = value_new_bool (val);
			ms_excel_sheet_insert_val (esheet,
						   EX_GETXF (q), EX_GETCOL (q),
						   EX_GETROW (q), v);
			break;
		}

		case BIFF_FORMULA: /* See: S59D8F.HTM */
			ms_excel_read_formula (q, esheet);
			break;

		/* case STRING : is handled elsewhere since it always follows FORMULA */
		case BIFF_ROW:
			ms_excel_read_row (q, esheet);
			break;

		case BIFF_EOF:
			return TRUE;

		case BIFF_INDEX:
			break;

		case BIFF_CALCCOUNT:
			ms_excel_read_calccount (q, wb);
			break;

		case BIFF_CALCMODE:
			break;

		case BIFF_PRECISION : {
#if 0
			/* FIXME: implement in gnumeric */
			/* state of 'Precision as Displayed' option */
			const guint16 data = MS_OLE_GET_GUINT16 (q->data);
			gboolean const prec_as_displayed = (data == 0);
#endif
			break;
		}

		case BIFF_REFMODE:
			break;

		case BIFF_DELTA:
			ms_excel_read_delta (q, wb);
			break;

		case BIFF_ITERATION:
			ms_excel_read_iteration (q, wb);
			break;

		case BIFF_PROTECT:
			ms_excel_read_PROTECT (q, "Sheet");
			break;

		/* case BIFF_PASSWORD : handled at the workbook level */

		case BIFF_HEADER: { /* FIXME: S59D94 */
			if (q->length) {
				char *const str = biff_get_text (q->data + 1,
					MS_OLE_GET_GUINT8 (q->data), NULL);
				d (2, printf ("Header '%s'\n", str););
				g_free (str);
			}
		}
		break;

		case BIFF_FOOTER: { /* FIXME: S59D8D */
			if (q->length) {
				char *const str = biff_get_text (q->data + 1,
					MS_OLE_GET_GUINT8 (q->data), NULL);
				d (2, printf ("Footer '%s'\n", str););
				g_free (str);
			}
		}
		break;

		case BIFF_NAME:
			ms_excel_read_name (q, esheet->wb, esheet);
			break;

		case BIFF_NOTE: /* See: S59DAB.HTM */
			ms_excel_read_comment (q, esheet);
			break;

		case BIFF_SELECTION:
			ms_excel_read_selection (q, esheet);
			break;

		case BIFF_EXTERNNAME:
			ms_excel_externname (q, esheet->wb, esheet);
			break;

		case BIFF_DEFAULTROWHEIGHT:
			ms_excel_read_default_row_height (q, esheet);
			break;

		case BIFF_LEFT_MARGIN:
			ms_excel_print_unit_init_inch (&pi->margins.left,
				gnumeric_get_le_double (q->data));
			break;
		case BIFF_RIGHT_MARGIN:
			ms_excel_print_unit_init_inch (&pi->margins.right,
				gnumeric_get_le_double (q->data));
			break;
		case BIFF_TOP_MARGIN:
			ms_excel_print_unit_init_inch (&pi->margins.top,
				gnumeric_get_le_double (q->data));
			break;
		case BIFF_BOTTOM_MARGIN:
			ms_excel_print_unit_init_inch (&pi->margins.bottom,
				gnumeric_get_le_double (q->data));
			break;

		case BIFF_PRINTHEADERS:
			break;

		case BIFF_PRINTGRIDLINES:
			pi->print_grid_lines = (MS_OLE_GET_GUINT16 (q->data) == 1);
			break;

		case BIFF_WINDOW2:
			ms_excel_read_window2 (q, esheet, wb_view);
			break;

		case BIFF_BACKUP:
			break;

		case BIFF_PANE:
			ms_excel_read_pane (q, esheet, wb_view);
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

		case BIFF_DEFCOLWIDTH:
			ms_excel_read_default_col_width (q, esheet);
			break;

		case BIFF_OBJ: /* See: ms-obj.c and S59DAD.HTM */
			ms_read_OBJ (q, sheet_container (esheet), NULL);
			break;

		case BIFF_SAVERECALC:
			break;

		case BIFF_TAB_COLOR:
			ms_excel_read_tab_color (q, esheet);
			break;

		case BIFF_OBJPROTECT:
			ms_excel_read_PROTECT (q, "Sheet");
			break;

		case BIFF_COLINFO:
			ms_excel_read_colinfo (q, esheet);
			break;

		case BIFF_RK: { /* See: S59DDA.HTM */
			Value *v = biff_get_rk (q->data + 6);
			d (2, {
				printf ("RK number: 0x%x, length 0x%x\n", q->opcode, q->length);
				ms_ole_dump (q->data, q->length);
			});

			ms_excel_sheet_insert_val (esheet, EX_GETXF (q), EX_GETCOL (q),
						   EX_GETROW (q), v);
			break;
		}

		case BIFF_IMDATA:
		    ms_excel_read_imdata (q);
		    break;

		case BIFF_GUTS:
			/* we could get this implicitly from the cols/rows
			 * but this is faster
			 */
			ms_excel_read_guts (q, esheet);
			break;

		case BIFF_WSBOOL:
			ms_excel_read_wsbool (q, esheet);
			break;

		case BIFF_GRIDSET:
			break;

		case BIFF_HCENTER:
			pi->center_horizontally = MS_OLE_GET_GUINT16 (q->data) == 0x1;
			break;
		case BIFF_VCENTER:
			pi->center_vertically   = MS_OLE_GET_GUINT16 (q->data) == 0x1;
			break;

		case BIFF_COUNTRY:
			break;

		case BIFF_STANDARDWIDTH:
			/* the 'standard width dialog' ? */
			break;

		case BIFF_SCL:
			if (q->length == 4) {
				/* Zoom stored as an Egyptian fraction */
				double const zoom = (double)MS_OLE_GET_GUINT16 (q->data) /
					MS_OLE_GET_GUINT16 (q->data + 2);

				sheet_set_zoom_factor (esheet->gnum_sheet, zoom, FALSE, FALSE);
			} else
				g_warning ("Duff BIFF_SCL record");
			break;

		case BIFF_SETUP:
			ms_excel_read_setup (q, esheet);
			break;

		case BIFF_SCENMAN:
		case BIFF_SCENARIO:
			break;

		case BIFF_MULRK: { /* S59DA8.HTM */
			guint32 col, row, lastcol;
			const guint8 *ptr = q->data;
			Value *v;

			row = MS_OLE_GET_GUINT16 (q->data);
			col = MS_OLE_GET_GUINT16 (q->data + 2);
			ptr += 4;
			lastcol = MS_OLE_GET_GUINT16 (q->data + q->length - 2);

			for (; col <= lastcol ; col++) {
				/* 2byte XF, 4 byte RK */
				v = biff_get_rk (ptr + 2);
				ms_excel_sheet_insert_val (esheet,
					MS_OLE_GET_GUINT16 (ptr), col, row, v);
				ptr += 6;
			}
			break;
		}

		case BIFF_MULBLANK: {
			/* S59DA7.HTM is extremely unclear, this is an educated guess */
			int firstcol = EX_GETCOL (q);
			int const row = EX_GETROW (q);
			const guint8 *ptr = (q->data + q->length - 2);
			int lastcol = MS_OLE_GET_GUINT16 (ptr);
			int i, range_end, prev_xf, xf_index;
			d (0, {
				printf ("Cells in row %d are blank starting at col %s until col ",
					row + 1, col_name (firstcol));
				printf ("%s;\n",
					col_name (lastcol));
			});

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
				d (2, {
					printf (" xf (%s) = 0x%x",
						col_name (i), xf_index);
					if (i == firstcol)
						printf ("\n");
				});

				if (prev_xf != xf_index) {
					if (prev_xf >= 0)
						ms_excel_set_xf_segment (esheet, i + 1, range_end,
									 row, row, prev_xf);
					prev_xf = xf_index;
					range_end = i;
				}
			} while (--i >= firstcol);
			ms_excel_set_xf_segment (esheet, firstcol, range_end,
						 row, row, prev_xf);
			d (2, printf ("\n"););
			break;
		}

		case BIFF_RSTRING: { /* See: S59DDC.HTM */
			const guint16 xf = EX_GETXF (q);
			const guint16 col = EX_GETCOL (q);
			const guint16 row = EX_GETROW (q);
			char *txt = biff_get_text (q->data + 8, EX_GETSTRLEN (q), NULL);
			d (0, printf ("Rstring in %s%d xf = 0x%x;\n",
				      col_name (col), row + 1, xf););

			ms_excel_sheet_insert (esheet, xf, col, row, txt);
			g_free (txt);
			break;
		}

		case BIFF_DBCELL: /* S59D6D.HTM,  Can be ignored on read side */
			break;

		case BIFF_BG_PIC:
		      ms_excel_read_bg_pic (q, esheet);
		      break;

		case BIFF_MERGECELLS:
			ms_excel_read_mergecells (q, esheet);
			break;

		case BIFF_MS_O_DRAWING:
		case BIFF_MS_O_DRAWING_GROUP:
		case BIFF_MS_O_DRAWING_SELECTION:
			ms_escher_parse (q, sheet_container (esheet));
			break;

		case BIFF_LABELSST: { /* See: S59D9E.HTM */
			guint32 const idx = MS_OLE_GET_GUINT32 (q->data + 6);

			if (esheet->wb->global_strings && idx < esheet->wb->global_string_max) {
				char const *str = esheet->wb->global_strings[idx];

				/* FIXME FIXME FIXME: Why would there be a NULL?  */
				if (str == NULL)
					str = "";
				ms_excel_sheet_insert_val (esheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q),
							   value_new_string (str));
			} else
				printf ("string index 0x%u >= 0x%x\n",
					idx, esheet->wb->global_string_max);
			break;
		}

		default:
			ms_excel_unexpected_biff (q, "Sheet", ms_excel_read_debug);
		}
	}

	printf ("Error, hit end without EOF\n");

	return FALSE;
}

XLExternSheet const *
ms_excel_workboot_get_externsheets (ExcelWorkbook *wb, guint idx)
{
	g_return_val_if_fail (idx < wb->num_extern_sheets, NULL);
	return &wb->extern_sheets [idx];
}

/*
 * see S59DEC.HTM
 */
static void
ms_excel_read_supporting_wb (BiffQuery *q, MsBiffBofData *ver)
{
	guint16	numTabs = MS_OLE_GET_GUINT16 (q->data);
	guint8 encodeType = MS_OLE_GET_GUINT8 (q->data + 2);

	/* TODO TODO TODO: Figure out what this is and if it is
	 * useful.  We always get a record length of FOUR?
	 * even when there are supposedly 10 tabs...
	 * Is this related to EXTERNNAME?
	 */
	d (0, {
		printf ("Supporting workbook with %d Tabs\n", numTabs);
		printf ("--> SUPBOOK VirtPath encoding = ");
		switch (encodeType) {
		case 0x00: /* chEmpty */
			puts ("chEmpty");
			break;
		case 0x01: /* chEncode */
			puts ("chEncode");
			break;
		case 0x02: /* chSelf */
			puts ("chSelf");
			break;
		default:
			printf ("Unknown/Unencoded?  (%x '%c') %d\n",
				encodeType, encodeType, q->length);
		};
		dump_biff (q);
	});

#if 0
	for (data = q->data + 2; numTabs-- > 0; ) {
		char *	name;
		guint32 byte_length, slen;
		slen = MS_OLE_GET_GUINT16 (data);
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
		 * NOTE: This is the size of the MDI sub-window, not the size of
		 * the containing excel window.
		 */
		wb_view_preferred_size (wb_view,
					.5 + width * application_display_dpi_get (TRUE) / (72. * 20.),
					.5 + height * application_display_dpi_get (FALSE) / (72. * 20.));

		if (options & 0x0001)
			printf ("Unsupported: Hidden workbook\n");
		if (options & 0x0002)
			printf ("Unsupported: Iconic workbook\n");
		wb_view->show_horizontal_scrollbar = (options & 0x0008);
		wb_view->show_vertical_scrollbar = (options & 0x0010);
		wb_view->show_notebook_tabs = (options & 0x0020);
	}
}

static void
ms_excel_externsheet (BiffQuery const *q, ExcelWorkbook *wb, MsBiffBofData *ver)
{
	g_return_if_fail (ver != NULL);

	/* FIXME: Clean this cruft.  I do not know what it was for, but it is definitely
	 * broken.
	 */
	++externsheet;

	if (ver->version >= MS_BIFF_V8) {
		guint16 numXTI = MS_OLE_GET_GUINT16 (q->data);
		guint16 cnt;

		wb->num_extern_sheets = numXTI;
#if 0
		printf ("ExternSheet (%d entries)\n", numXTI);
		ms_ole_dump (q->data, q->length);
#endif

		wb->extern_sheets = g_new (XLExternSheet, numXTI + 1);

		for (cnt = 0; cnt < numXTI; cnt++) {
			wb->extern_sheets[cnt].sup_idx = MS_OLE_GET_GUINT16 (q->data + 2 + cnt * 6 + 0);
			wb->extern_sheets[cnt].first_sheet = MS_OLE_GET_GUINT16 (q->data + 2 + cnt * 6 + 2);
			wb->extern_sheets[cnt].last_sheet  = MS_OLE_GET_GUINT16 (q->data + 2 + cnt * 6 + 4);
#if 0
			printf ("SupBook: %d First sheet %d, Last sheet %d\n",
				wb->extern_sheets[cnt].sup_idx,
				wb->extern_sheets[cnt].first_tab,
				wb->extern_sheets[cnt].last_tab);
#endif
		}
	} else
		printf ("ExternSheet: only BIFF8 supported so far...\n");
}

static ExcelWorkbook *
ms_excel_read_bof (BiffQuery *q,
		   ExcelWorkbook *wb,
		   WorkbookView *wb_view,
		   IOContext *context,
		   MsBiffBofData **version, int *current_sheet)
{
	/* The first BOF seems to be OK, the rest lie ? */
	MsBiffVersion vv = MS_BIFF_V_UNKNOWN;
	MsBiffBofData *ver = *version;
	if (ver) {
		vv = ver->version;
		ms_biff_bof_data_destroy (ver);
	}
	*version = ver = ms_biff_bof_data_new (q);
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
		if (bsh) {
			ExcelSheet *esheet = ms_excel_workbook_get_sheet (wb, *current_sheet);
			gboolean    kill  = FALSE;

			ms_excel_sheet_set_version (esheet, ver->version);
			if (ms_excel_read_sheet (q, wb, wb_view, esheet, context)) {
				ms_container_realize_objs (sheet_container (esheet));

#if 0
				/* DO NOT DO THIS.
				 * this looks good in principle but is a nightmare for
				 * performance.  It causes the entire book to be recalculated
				 * for everysheet that is removed.
				 */
				if (sheet_is_pristine (esheet->gnum_sheet) &&
				    *current_sheet > 0)
					kill = TRUE;
#endif
			} else
				kill = TRUE;

			if (kill) {
				d (1, printf ("Blank or broken sheet %d\n", *current_sheet););

				if (ms_excel_workbook_detach (esheet->wb, esheet))
					ms_excel_sheet_destroy (esheet);
			}

			(*current_sheet)++;
		} else
			printf ("Sheet offset in stream of %x not found in list\n", q->streamPos);
	} else if (ver->type == MS_BIFF_TYPE_Chart) {
				GnmGraph *graph =
#if 0
					/* enable when we support workbooklevel objects */
					gnm_graph_new (wb->gnum_wb);
#else
					NULL;
#endif
		ms_excel_chart (q, &wb->container, ver->version,
				GTK_OBJECT (graph));
	} else if (ver->type == MS_BIFF_TYPE_VBModule ||
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
			g_warning ("EXCEL: file format error.  Missing BIFF_EOF");
	} else
		printf ("Unknown BOF (%x)\n", ver->type);

	return wb;
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

	io_progress_message (context, _("Reading file..."));
	value_io_progress_set (context, stream->size, N_BYTES_BETWEEN_PROGRESS_UPDATES);
	q = ms_biff_query_new (stream);

	while (problem_loading == NULL && ms_biff_query_next (q)) {
		d (5, printf ("Opcode: 0x%x\n", q->opcode););

		/* Catch Oddballs
		 * The heuristic seems to be that 'version 1' BIFF types
		 * are unique and not versioned.
		 */
		if (0x1 == q->ms_op) {
			switch (q->opcode) {
			case BIFF_DSF:
				d (0, printf ("Double Stream File: %s\n",
					      (MS_OLE_GET_GUINT16 (q->data) == 1)
					      ? "Yes" : "No"););
				break;
			case BIFF_XL9FILE:
				d (0, puts ("XL 2000 file"););
				break;

			case BIFF_REFRESHALL:
				break;

			case BIFF_USESELFS:
				break;

			case BIFF_TABID:
				break;

			case BIFF_PROT4REV:
				break;

			case BIFF_PROT4REVPASS:
				break;

			case BIFF_CODENAME: /* TODO: What to do with this name ? */
				break;

			case BIFF_SUPBOOK:
				ms_excel_read_supporting_wb (q, ver);
				break;

			default:
				ms_excel_unexpected_biff (q, "Workbook", ms_excel_read_debug);
			}
			continue;
		}

		switch (q->ls_op) {
		case BIFF_BOF:
			wb = ms_excel_read_bof (q, wb, wb_view, context, &ver, &current_sheet);
			break;

		case BIFF_EOF: /* FIXME: Perhaps we should finish here ? */
			d (0, printf ("End of worksheet spec.\n"););
			break;

		case BIFF_BOUNDSHEET:
			biff_boundsheet_data_new (q, wb, ver->version);
			break;

		case BIFF_PALETTE:
			wb->palette = ms_excel_palette_new (q);
			break;

		case BIFF_FONT: /* see S59D8C.HTM */
			biff_font_data_new (q, wb);
			break;

		case BIFF_XF_OLD: /* see S59E1E.HTM */
		case BIFF_XF:
			biff_xf_data_new (q, wb, ver->version);
			break;

		case BIFF_SST: /* see S59DE7.HTM */
			read_sst (q, wb, ver->version);
			break;

		case BIFF_EXTSST: /* See: S59D84 */
			/* Can be safely ignored on read side */
			break;


		case BIFF_EXTERNSHEET: /* See: S59D82.HTM */
			ms_excel_externsheet (q, wb, ver);
			break;

		case BIFF_FORMAT: { /* S59D8E.HTM */
			BiffFormatData *d = g_new (BiffFormatData, 1);
			/*				printf ("Format data 0x%x %d\n", q->ms_op, ver->version);
							ms_ole_dump (q->data, q->length);*/
			if (ver->version == MS_BIFF_V7) { /* Totaly guessed */
				d->idx = MS_OLE_GET_GUINT16 (q->data);
				d->name = biff_get_text (q->data + 3, MS_OLE_GET_GUINT8 (q->data + 2), NULL);
			} else if (ver->version >= MS_BIFF_V8) {
				d->idx = MS_OLE_GET_GUINT16 (q->data);
				d->name = biff_get_text (q->data + 4, MS_OLE_GET_GUINT16 (q->data + 2), NULL);
			} else { /* FIXME: mythical old papyrus spec. */
				d->name = biff_get_text (q->data + 1, MS_OLE_GET_GUINT8 (q->data), NULL);
				d->idx = g_hash_table_size (wb->format_data) + 0x32;
			}
			d (2, printf ("Format data: 0x%x == '%s'\n",
				      d->idx, d->name););

			g_hash_table_insert (wb->format_data, &d->idx, d);
			break;
		}

		case BIFF_EXTERNCOUNT: /* see S59D7D.HTM */
			d (0, printf ("%d external references\n",
				      MS_OLE_GET_GUINT16 (q->data)););
			break;

		case BIFF_CODEPAGE: { /* DUPLICATE 42 */
			/* This seems to appear within a workbook */
			/* MW: And on Excel seems to drive the display
			   of currency amounts.  */
			const guint16 codepage = MS_OLE_GET_GUINT16 (q->data);
			excel_iconv_close (current_workbook_iconv);
			current_workbook_iconv = excel_iconv_open_for_import (codepage);
			d (0, {
				switch (codepage) {
				case 437:
					/* US.  */
					puts ("CodePage = IBM PC (US)");
					break;
				case 865:
					puts ("CodePage = IBM PC (Denmark/Norway)");
					break;
				case 0x8000:
					puts ("CodePage = Apple Macintosh");
					break;
				case 0x04e4:
					puts ("CodePage = ANSI (Microsoft Windows)");
					break;
				case 0x04b0:
					/* FIXME FIXME: This is a guess */
					puts ("CodePage = Auto");
					break;
				default:
					printf ("CodePage = UNKNOWN(%hx)\n",
					       codepage);
				};
			});
			break;
		}

		case BIFF_OBJPROTECT:
		case BIFF_PROTECT:
			ms_excel_read_PROTECT (q, "Workbook");
			break;

		case BIFF_PASSWORD:
			break;

		case BIFF_FILEPASS:
			/* All records after this are encrypted */
			problem_loading = g_strdup (_("Password protected workbooks are not supported yet."));
			break;

		case BIFF_STYLE:
			break;

		case BIFF_WINDOWPROTECT:
			break;

		case BIFF_EXTERNNAME:
			ms_excel_externname (q, wb, NULL);
			break;

		case BIFF_NAME:
			ms_excel_read_name (q, wb, NULL);
			break;

		case BIFF_WRITEACCESS:
			break;
		case BIFF_HIDEOBJ:
			break;
		case BIFF_FNGROUPCOUNT:
			break;
		case BIFF_MMS:
			break;

		case BIFF_OBPROJ:
			/* Flags that the project has some VBA */
			break;

		case BIFF_BOOKBOOL:
			break;
		case BIFF_COUNTRY:
			break;

		case BIFF_INTERFACEHDR:
			break;

		case BIFF_INTERFACEEND:
			break;

		case BIFF_1904: /* 0, NOT 1 */
			if (MS_OLE_GET_GUINT16 (q->data) == 1)
				printf ("EXCEL: Warning workbook uses unsupported 1904 Date System\n"
					"dates will be incorrect\n");
			break;

		case BIFF_WINDOW1:
			ms_excel_read_window1 (q, wb_view);
			break;

		case BIFF_SELECTION: /* 0, NOT 10 */
			break;

		case BIFF_DIMENSIONS:	/* 2, NOT 1,10 */
			ms_excel_biff_dimensions (q, wb);
			break;

		case BIFF_OBJ: /* See: ms-obj.c and S59DAD.HTM */
			ms_read_OBJ (q, &wb->container, NULL);
			break;

		case BIFF_SCL:
			break;

		case BIFF_MS_O_DRAWING:
		case BIFF_MS_O_DRAWING_GROUP:
		case BIFF_MS_O_DRAWING_SELECTION:
			ms_escher_parse (q, &wb->container);
			break;

		case BIFF_ADDMENU:
			d (1, printf ("%smenu with %d sub items",
				      (MS_OLE_GET_GUINT8 (q->data + 6) == 1) ? "" : "Placeholder ",
				      MS_OLE_GET_GUINT8 (q->data + 5)););
			break;

		default:
			ms_excel_unexpected_biff (q, "Workbook", ms_excel_read_debug);
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
	io_progress_unset (context);

	d (1, printf ("finished read\n"););

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0
	    || ms_excel_formula_debug > 0
	    || ms_excel_chart_debug > 0) {
		fflush (stdout);
	}
#endif
	excel_iconv_close (current_workbook_iconv);
	current_workbook_iconv = 0;
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
ms_excel_read_init (void)
{
	excel_builtin_formats [0x0e] = cell_formats [FMT_DATE][0];
	excel_builtin_formats [0x0f] = cell_formats [FMT_DATE][2];
	excel_builtin_formats [0x10] = cell_formats [FMT_DATE][4];
	excel_builtin_formats [0x16] = cell_formats [FMT_DATE][20];
}

void
ms_excel_read_cleanup (void)
{
	ms_excel_palette_destroy (ms_excel_default_palette ());
}
