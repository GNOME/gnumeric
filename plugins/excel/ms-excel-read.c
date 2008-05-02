/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * ms-excel-read.c: MS Excel import
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2007 Jody Goldberg
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#include "ms-pivot.h"

#include <workbook.h>
#include <workbook-view.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <sheet-merge.h>
#include <sheet-filter.h>
#include <cell.h>
#include <style.h>
#include <style-conditions.h>
#include <gnm-format.h>
#include <print-info.h>
#include <selection.h>
#include <validation.h>
#include <input-msg.h>
#include <parse-util.h>	/* for cell_name */
#include <ranges.h>
#include <expr.h>
#include <expr-name.h>
#include <value.h>
#include <hlink.h>
#include <application.h>
#include <command-context.h>
#include <sheet-object-cell-comment.h>
#include <sheet-object-widget.h>
#include <gnm-so-line.h>
#include <gnm-so-filled.h>
#include <gnm-so-polygon.h>
#include <sheet-object-graph.h>
#include <sheet-object-image.h>
#include <goffice/app/io-context.h>
#include <goffice/app/go-doc.h>
#include <goffice/utils/go-font.h>
#include <goffice/utils/go-units.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/graph/gog-style.h>

#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-msole-utils.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <locale.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "gnumeric:read"

typedef struct {
	ExcelReadSheet	 *esheet;
	char		 *name;
	guint32		  streamStartPos;
	unsigned	  index;
	MsBiffFileType	  type;
	GnmSheetType	  gnm_type;
	GnmSheetVisibility visibility;
} BiffBoundsheetData;

#define N_BYTES_BETWEEN_PROGRESS_UPDATES   0x1000

/*
 * Check whether the product of the first two arguments exceeds
 * the third.  The function should be overflow-proof.
 */
static gboolean
product_gt (size_t count, size_t itemsize, size_t space)
{
	return itemsize > 0 &&
		(count > G_MAXUINT / itemsize || count * itemsize > space);
}

static void
record_size_barf (size_t count, size_t itemsize, size_t space,
		  const char *locus)
{
	g_warning ("File is most likely corrupted.\n"
		   "(Requested %u*%u bytes, but only %u bytes left in record.\n"
		   "The problem occurred in %s.)",
		   (unsigned)count, (unsigned)itemsize,
		   (unsigned)space,
		   locus);
}

#define XL_NEED_BYTES(count) XL_NEED_ITEMS(count,1)

#define XL_NEED_ITEMS(count__,size__)					\
  do {									\
	  size_t count_ = (count__);					\
	  size_t size_ = (size__);					\
	  size_t space_ = q->length - (data - q->data);			\
	  if (G_UNLIKELY (product_gt (count_, size_, space_))) {	\
                record_size_barf (count_, size_, space_, G_STRFUNC);	\
		return;							\
          }								\
  } while (0)


/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_read_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

#define XL_GETROW(p)      (GSF_LE_GET_GUINT16(p->data + 0))
#define XL_GETCOL(p)      (GSF_LE_GET_GUINT16(p->data + 2))

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
/* 0x0f		"d-mmm-yy", */ NULL,	/* excel_read_init */
/* 0x10		"d-mmm", */ NULL,
/* 0x11 */	"mmm-yy",
/* 0x12 */	"h:mm AM/PM",
/* 0x13 */	"h:mm:ss AM/PM",
/* 0x14 */	"h:mm",
/* 0x15 */	"h:mm:ss",
/* 0x16		"m/d/yy h:mm", */ NULL,
/* 0x17 */	NULL, /* 0x17-0x24 reserved for intl versions */
/* 0x18 */	NULL,
/* 0x19 */	NULL,
/* 0x1a */	NULL,
/* 0x1b */	NULL,
/* 0x1c */	NULL,
/* 0x1d */	NULL,
/* 0x1e	*/	NULL,
/* 0x1f	*/	NULL,
/* 0x20	*/	NULL,
/* 0x21 */	NULL,
/* 0x22 */	NULL,
/* 0x23 */	NULL,
/* 0x24 */	NULL,
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

static MsBiffVersion
esheet_ver (ExcelReadSheet const *esheet)
{
	return esheet->container.importer->ver;
}

static void
gnm_xl_importer_set_version (GnmXLImporter *importer, MsBiffVersion ver)
{
	g_return_if_fail (NULL != importer);
	g_return_if_fail (MS_BIFF_V_UNKNOWN == importer->ver);
	importer->ver = ver;
}

static void
gnm_xl_importer_set_codepage (GnmXLImporter *importer, int codepage)
{
	GIConv str_iconv;
	if (codepage == 1200 || codepage == 1201)
		/* this is 'compressed' unicode.  unicode characters 0000->00FF
		 * which looks the same as 8859-1.  What does Little endian vs
		 * bigendian have to do with this.  There is only 1 byte, and it would
		 * certainly not be useful to keep the low byte as 0.
		 */
		str_iconv = g_iconv_open ("UTF-8", "ISO-8859-1");
	else
		str_iconv = gsf_msole_iconv_open_for_import (codepage);

	if (str_iconv == (GIConv)(-1)) {
		g_warning ("missing convertor for codepage %u\n"
			   "falling back to 1252", codepage);
		str_iconv = gsf_msole_iconv_open_for_import (1252);
	}

	if (importer->str_iconv != (GIConv)(-1))
		gsf_iconv_close (importer->str_iconv);
	importer->str_iconv = str_iconv;

	/* Store the codepage to make export easier, might
	 * cause problems with double stream files because
	 * we'll lose the codepage in the biff8 version */
	g_object_set_data (G_OBJECT (importer->wb), "excel-codepage",
		GINT_TO_POINTER (codepage));

	d (0, puts (gsf_msole_language_for_lid (
		gsf_msole_codepage_to_lid (codepage))););
}

static GOFormat *
excel_wb_get_fmt (GnmXLImporter *importer, unsigned idx)
{
	char const *ans = NULL;
	BiffFormatData const *d = g_hash_table_lookup (importer->format_table,
		GUINT_TO_POINTER (idx));

	if (d)
		ans = d->name;
	else if (idx <= 0x31) {
		ans = excel_builtin_formats[idx];
		if (!ans)
			g_printerr ("Foreign undocumented format\n");
	} else
		g_printerr ("Unknown format: 0x%x\n", idx);

	if (ans)
		return go_format_new_from_XL (ans);
	else
		return NULL;
}

static GnmCell *
excel_cell_fetch (BiffQuery *q, ExcelReadSheet *esheet)
{
	unsigned col, row;
	Sheet *sheet = esheet->sheet;

	XL_CHECK_CONDITION_VAL (q->length >= 4, NULL);

	col = XL_GETCOL (q);
	row = XL_GETROW (q);

	XL_CHECK_CONDITION_VAL (col < gnm_sheet_get_max_cols (sheet), NULL);
	XL_CHECK_CONDITION_VAL (row < gnm_sheet_get_max_rows (sheet), NULL);

	return sheet_cell_fetch (sheet, col, row);
}

static GnmExprTop const *
ms_sheet_parse_expr_internal (ExcelReadSheet *esheet, guint8 const *data, int length)
{
	GnmExprTop const *texpr;

	g_return_val_if_fail (length > 0, NULL);

	texpr = excel_parse_formula (&esheet->container, esheet, 0, 0,
				     data, length, 0 /* FIXME */,
				     FALSE, NULL);
	if (ms_excel_read_debug > 8) {
		char *tmp;
		GnmParsePos pp;
		Sheet *sheet = esheet->sheet;
		Workbook *wb = (sheet == NULL) ? esheet->container.importer->wb : NULL;

		tmp = gnm_expr_top_as_string (texpr,
					      parse_pos_init (&pp, wb, sheet, 0, 0),
					      gnm_conventions_default);
		puts (tmp);
		g_free (tmp);
	}

	return texpr;
}

static GnmExprTop const *
ms_sheet_parse_expr (MSContainer *container, guint8 const *data, int length)
{
	return ms_sheet_parse_expr_internal ((ExcelReadSheet *)container,
					     data, length);
}

static Sheet *
ms_sheet_get_sheet (MSContainer const *container)
{
	return ((ExcelReadSheet const *)container)->sheet;
}

static GOFormat *
ms_sheet_get_fmt (MSContainer const *container, unsigned indx)
{
	return excel_wb_get_fmt (container->importer, indx);
}

static GOColor
ms_sheet_map_color (ExcelReadSheet const *esheet, MSObj const *obj, MSObjAttrID id, GOColor default_val)
{
	gushort r, g, b;
	MSObjAttr *attr = ms_obj_attr_bag_lookup (obj->attrs, id);

	if (attr == NULL)
		return default_val;

	if ((~0x7ffffff) & attr->v.v_uint) {
		GnmColor *c = excel_palette_get (esheet->container.importer,
			(0x7ffffff & attr->v.v_uint));

		r = c->gdk_color.red >> 8;
		g = c->gdk_color.green >> 8;
		b = c->gdk_color.blue >> 8;
		style_color_unref (c);
	} else {
		r = (attr->v.v_uint)       & 0xff;
		g = (attr->v.v_uint >> 8)  & 0xff;
		b = (attr->v.v_uint >> 16) & 0xff;
	}

	return RGBA_TO_UINT (r,g,b,0xff);
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
			    GnmRange *range, float offset[4])
{
	/* NOTE :
	 * float const row_denominator = (ver >= MS_BIFF_V8) ? 256. : 1024.;
	 * damn damn damn
	 * chap03-1.xls suggests that XL95 uses 256 too
	 * Do we have any tests that confirm the docs contention of 1024 ?
	 */
	int	i;

	d (0,
	{
		fprintf (stderr,"anchored to %s\n", sheet->name_unquoted);
		gsf_mem_dump (raw_anchor, 18);
	});

	/* Ignore the first 2 bytes.  What are they ? */
	/* Dec/1/2000 JEG: I have not researched it, but this may have some
	 * flags indicating whether or not the object is anchored to the cell
	 */
	raw_anchor += 2;

	/* Words 0, 4, 8, 12: The row/col of the corners */
	/* Words 2, 6, 10, 14: distance from cell edge */
	for (i = 0; i < 4; i++, raw_anchor += 4) {
		int const pos  = GSF_LE_GET_GUINT16 (raw_anchor);
		int const nths = GSF_LE_GET_GUINT16 (raw_anchor + 2);

		d (2, {
			fprintf (stderr,"%d/%d cell %s from ",
				nths, (i & 1) ? 256 : 1024,
				(i & 1) ? "widths" : "heights");
			if (i & 1)
				fprintf (stderr,"row %d;\n", pos + 1);
			else
				fprintf (stderr,"col %s (%d);\n", col_name (pos), pos);
		});

		if (i & 1) { /* odds are rows */
			offset[i] = nths / 256.;
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

unsigned xl_pattern_to_line_type (guint16 pattern)
{
	static GOLineDashType const dash_map []= {
		GO_LINE_SOLID,
		GO_LINE_DASH,
		GO_LINE_DOT,
		GO_LINE_DASH_DOT,
		GO_LINE_DASH_DOT_DOT,
		GO_LINE_NONE
	};
	return (pattern >= G_N_ELEMENTS (dash_map))? GO_LINE_SOLID: dash_map[pattern];
}

static gboolean
ms_sheet_realize_obj (MSContainer *container, MSObj *obj)
{
	float offsets[4];
	gpointer label;
	PangoAttrList *markup;
	GnmRange range;
	ExcelReadSheet *esheet;
	MSObjAttr *attr, *flip_h, *flip_v;
	GODrawingAnchorDir direction;
	SheetObjectAnchor anchor;
	SheetObject *so;
	GogStyle *style;

	if (obj == NULL)
		return TRUE;
	if (obj->gnum_obj == NULL)
		return FALSE;
	so = obj->gnum_obj;

	g_return_val_if_fail (container != NULL, TRUE);
	esheet = (ExcelReadSheet *)container;

	/* our comment object is too weak.  This anchor is for the text box,
	 * we need to store the indicator */
	if (obj->excel_type == 0x19 &&
	    obj->comment_pos.col >= 0 && obj->comment_pos.row >= 0) {
		cell_comment_set_pos (CELL_COMMENT (obj->gnum_obj),
			&obj->comment_pos);
	} else {
		attr = ms_obj_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_ANCHOR);
		if (attr == NULL) {
			g_printerr ("MISSING anchor for obj %p with id %d of type %s\n", (void *)obj, obj->id, obj->excel_type_name);
			return TRUE;
		}

		if (ms_sheet_obj_anchor_to_pos (esheet->sheet, container->importer->ver,
						attr->v.v_ptr, &range, offsets))
			return TRUE;

		flip_h = ms_obj_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_FLIP_H);
		flip_v = ms_obj_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_FLIP_V);
		direction =
			((flip_h == NULL) ? GOD_ANCHOR_DIR_RIGHT : 0) |
			((flip_v == NULL) ? GOD_ANCHOR_DIR_DOWN : 0);

		sheet_object_anchor_init (&anchor, &range, offsets, direction);
		sheet_object_set_anchor (so, &anchor);
	}
	sheet_object_set_sheet (so, esheet->sheet);

	if (ms_obj_attr_get_ptr (obj->attrs, MS_OBJ_ATTR_TEXT, &label, FALSE))
		g_object_set (G_OBJECT (so), "text", label, NULL);

	markup = ms_obj_attr_get_markup (obj->attrs, MS_OBJ_ATTR_MARKUP, NULL, FALSE);
	if (markup != NULL)
		g_object_set (so, "markup", markup, NULL);

	switch (obj->excel_type) {
	case 0x00:
		break;

	case 0x01: /* Line */
	case 0x04: /* Arc */
		style = gog_style_new ();
		style->line.color = ms_sheet_map_color (esheet, obj,
			MS_OBJ_ATTR_OUTLINE_COLOR, RGBA_BLACK);
		style->line.width = ms_obj_attr_get_uint (obj->attrs,
			MS_OBJ_ATTR_OUTLINE_WIDTH, 0) / 256.;
		style->line.dash_type = ms_obj_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_OUTLINE_HIDE)
			? GO_LINE_NONE : xl_pattern_to_line_type (ms_obj_attr_get_int (obj->attrs, MS_OBJ_ATTR_OUTLINE_STYLE, 1));
		g_object_set (G_OBJECT (so), "style", style, NULL);
		g_object_unref (style);
		break;

	case 0x09:
		g_object_set (G_OBJECT (so), "points",
			ms_obj_attr_get_array (obj->attrs, MS_OBJ_ATTR_POLYGON_COORDS, NULL, TRUE),
			NULL);
		   /* fallthrough */

	case 0x02: /* rectangle */
	case 0x03: /* oval */
	case 0x06: /* TextBox */
	case 0x0E: /* Label */
		style = gog_style_new ();
		style->outline.color = ms_sheet_map_color (esheet, obj,
			MS_OBJ_ATTR_OUTLINE_COLOR, RGBA_BLACK);
		style->outline.width = ms_obj_attr_get_uint (obj->attrs,
			MS_OBJ_ATTR_OUTLINE_WIDTH, 0) / 256.;
		style->outline.dash_type = ms_obj_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_OUTLINE_HIDE)
			? GO_LINE_NONE : xl_pattern_to_line_type (ms_obj_attr_get_int (obj->attrs, MS_OBJ_ATTR_OUTLINE_STYLE, 1));
		style->fill.pattern.back = ms_sheet_map_color (esheet, obj,
			MS_OBJ_ATTR_FILL_COLOR, RGBA_WHITE);
		style->fill.pattern.fore = ms_sheet_map_color (esheet, obj,
			MS_OBJ_ATTR_FILL_BACKGROUND, RGBA_BLACK);
		style->fill.type = ms_obj_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_UNFILLED)
			? GOG_FILL_STYLE_NONE : GOG_FILL_STYLE_PATTERN;

		g_object_set (G_OBJECT (so), "style", style, NULL);
		g_object_unref (style);
		break;

	case 0x05: /* Chart */
		/* NOTE : We should not need to do anything for charts */
		break;

	case 0x07:	/* Button */
		break;

	case 0x08: { /* Picture */
		double crop_left = 0.0;
		double crop_top  = 0.0;
		double crop_right = 0.0;
		double crop_bottom = 0.0;

		if ((attr = ms_obj_attr_bag_lookup (obj->attrs,
			MS_OBJ_ATTR_BLIP_ID)) != NULL) {
			MSEscherBlip *blip = ms_container_get_blip (container,
				attr->v.v_uint - 1);
			if (blip != NULL) {
				sheet_object_image_set_image (SHEET_OBJECT_IMAGE (so),
					blip->type, blip->data, blip->data_len,
					!blip->needs_free);
				blip->needs_free = FALSE; /* image took over managing data */
			}
		} else if ((attr = ms_obj_attr_bag_lookup (obj->attrs,
			MS_OBJ_ATTR_IMDATA)) != NULL) {
			GdkPixbuf *pixbuf = GDK_PIXBUF (attr->v.v_object);

			if (pixbuf) {
				gchar *buf;
				gsize buf_size;

				gdk_pixbuf_save_to_buffer
					(pixbuf, &buf, &buf_size, "png",
					 NULL, NULL);
				if (buf_size > 0) {
					sheet_object_image_set_image
						(SHEET_OBJECT_IMAGE (so),
						 "png", buf, buf_size, FALSE);
				}
			}
		}
		if ((attr = ms_obj_attr_bag_lookup (obj->attrs,
		     MS_OBJ_ATTR_BLIP_CROP_LEFT)) != NULL)
			crop_left   = (double) attr->v.v_uint / 65536.;
		if ((attr = ms_obj_attr_bag_lookup (obj->attrs,
		     MS_OBJ_ATTR_BLIP_CROP_RIGHT)) != NULL)
			crop_right  = (double) attr->v.v_uint / 65536.;
		if ((attr = ms_obj_attr_bag_lookup (obj->attrs,
		     MS_OBJ_ATTR_BLIP_CROP_TOP)) != NULL)
			crop_top     = (double) attr->v.v_uint / 65536.;
		if ((attr = ms_obj_attr_bag_lookup (obj->attrs,
		     MS_OBJ_ATTR_BLIP_CROP_BOTTOM)) != NULL)
			crop_bottom = (double) attr->v.v_uint / 65536.;

		sheet_object_image_set_crop (SHEET_OBJECT_IMAGE (so),
			crop_left, crop_top, crop_right, crop_bottom);
		break;
	}

	case 0x0B:
	case 0x70:
		sheet_widget_checkbox_set_link (obj->gnum_obj,
			ms_obj_attr_get_expr (obj->attrs, MS_OBJ_ATTR_LINKED_TO_CELL, NULL, FALSE));
		break;

	case 0x0C:
		break;

	case 0x10:
	case 0x11:
		sheet_widget_adjustment_set_details (obj->gnum_obj,
			ms_obj_attr_get_expr (obj->attrs, MS_OBJ_ATTR_LINKED_TO_CELL, NULL, FALSE),
			ms_obj_attr_get_int (obj->attrs, MS_OBJ_ATTR_SCROLLBAR_VALUE, 0),
			ms_obj_attr_get_int (obj->attrs, MS_OBJ_ATTR_SCROLLBAR_MIN, 0),
			ms_obj_attr_get_int (obj->attrs, MS_OBJ_ATTR_SCROLLBAR_MAX, 100) - 1,
			ms_obj_attr_get_int (obj->attrs, MS_OBJ_ATTR_SCROLLBAR_INC, 1),
			ms_obj_attr_get_int (obj->attrs, MS_OBJ_ATTR_SCROLLBAR_PAGE, 10));
		break;

	case 0x12: /* List */
	case 0x14: /* Combo  */
		sheet_widget_list_base_set_links (obj->gnum_obj,
			ms_obj_attr_get_expr (obj->attrs, MS_OBJ_ATTR_LINKED_TO_CELL, NULL, FALSE),
			ms_obj_attr_get_expr (obj->attrs, MS_OBJ_ATTR_INPUT_FROM, NULL, FALSE));
		break;

	case 0x19: /* cell comment text box */
		break;

	default:
		g_warning ("EXCEL: unhandled excel object of type %s (0x%x) id = %d.",
			   obj->excel_type_name, obj->excel_type, obj->id);
		return TRUE;
	}

	return FALSE;
}

static SheetObject *
ms_sheet_create_obj (MSContainer *container, MSObj *obj)
{
	SheetObject *so = NULL;
	Workbook *wb;
	ExcelReadSheet *esheet;
	gpointer label;

	if (obj == NULL)
		return NULL;

	g_return_val_if_fail (container != NULL, NULL);

	esheet = (ExcelReadSheet *)container;
	wb = esheet->container.importer->wb;

	switch (obj->excel_type) {
	case 0x01: /* Line */
	case 0x04: /* Arc */
		so = g_object_new (GNM_SO_LINE_TYPE,
			"is-arrow", 0 != ms_obj_attr_get_int (obj->attrs, MS_OBJ_ATTR_ARROW_END, 0),
			NULL);
		break;

	case 0x00: /* draw the group border */
	case 0x02: /* Box */
	case 0x03: /* Oval */
	case 0x06: /* TextBox */
	case 0x0E: /* Label */
		so = g_object_new (GNM_SO_FILLED_TYPE,
			"is-oval", obj->excel_type == 3,
			NULL);
		if (ms_obj_attr_get_ptr (obj->attrs, MS_OBJ_ATTR_TEXT, &label, FALSE))
			g_object_set (G_OBJECT (so), "text", label, NULL);
		break;

	case 0x05: /* Chart */
		so = sheet_object_graph_new (NULL);
		break;

	/* Button */
	case 0x07: so = g_object_new (sheet_widget_button_get_type (), NULL);
		break;
	case 0x08: so = g_object_new (SHEET_OBJECT_IMAGE_TYPE, NULL); /* Picture */
		break;
	case 0x09: so = g_object_new (GNM_SO_POLYGON_TYPE, NULL);
		break;
	case 0x0B: so = g_object_new (sheet_widget_checkbox_get_type (), NULL);
		break;
	case 0x0C: so = g_object_new (sheet_widget_radio_button_get_type (), NULL);
		break;
	case 0x10: so = g_object_new (sheet_widget_spinbutton_get_type (), NULL);
		break;
	case 0x11: so = g_object_new (sheet_widget_scrollbar_get_type (), NULL);
		break;
	case 0x12: so = g_object_new (sheet_widget_list_get_type (), NULL);
		break;

	/* ignore combos associateed with filters */
	case 0x14:
		if (!obj->combo_in_autofilter)
			so = g_object_new (sheet_widget_combo_get_type (), NULL);

		/* ok, there are combos to go with the autofilter it can stay */
		else if (esheet != NULL)
			esheet->filter = NULL;
	break;

	case 0x19: so = g_object_new (cell_comment_get_type (), NULL);
		break;

	/* Gnumeric specific addition to handle toggle button controls */
	case 0x70: so = g_object_new (sheet_widget_toggle_button_get_type (), NULL);
		break;

	default:
		g_warning ("EXCEL: unhandled excel object of type %s (0x%x) id = %d.",
			   obj->excel_type_name, obj->excel_type, obj->id);
		return NULL;
	}

	return so;
}

/*
 * excel_init_margins
 * @esheet ExcelReadSheet
 *
 * Excel only saves margins when any of the margins differs from the
 * default. So we must initialize the margins to Excel's defaults, which
 * are:
 * Top, bottom:    1 in   - 72 pt
 * Left, right:    3/4 in - 48 pt
 * Header, footer: 1/2 in - 36 pt
 */
static void
excel_init_margins (ExcelReadSheet *esheet)
{
	PrintInformation *pi;
	double points;
	double short_points;

	g_return_if_fail (esheet != NULL);
	g_return_if_fail (esheet->sheet != NULL);
	g_return_if_fail (esheet->sheet->print_info != NULL);

	pi = esheet->sheet->print_info;
	print_info_set_edge_to_below_header (pi,GO_IN_TO_PT (1.0));
	print_info_set_edge_to_above_footer (pi,GO_IN_TO_PT (1.0));

	points = GO_IN_TO_PT (0.75);
	short_points = GO_IN_TO_PT (0.5);
	print_info_set_margins (pi, short_points, short_points, points, points);
}

static void
excel_shared_formula_free (XLSharedFormula *sf)
{
	if (sf != NULL) {
		g_free (sf->data);
		g_free (sf);
	}
}

static ExcelReadSheet *
excel_sheet_new (GnmXLImporter *importer, char const *sheet_name, GnmSheetType type)
{
	static MSContainerClass const vtbl = {
		&ms_sheet_realize_obj,
		&ms_sheet_create_obj,
		&ms_sheet_parse_expr,
		&ms_sheet_get_sheet,
		&ms_sheet_get_fmt,
		NULL
	};

	ExcelReadSheet *esheet = g_new (ExcelReadSheet, 1);
	Sheet *sheet;

	sheet = workbook_sheet_by_name (importer->wb, sheet_name);
	if (sheet == NULL) {
		sheet = sheet_new_with_type (importer->wb, sheet_name, type);
		workbook_sheet_attach (importer->wb, sheet);
		d (1, fprintf (stderr,"Adding sheet '%s'\n", sheet_name););
	}

	/* Flag a respan here in case nothing else does */
	sheet_flag_recompute_spans (sheet);

	esheet->sheet	= sheet;
	esheet->filter	= NULL;
	esheet->freeze_panes = FALSE;
	esheet->active_pane  = 3; /* The default */
	esheet->shared_formulae	= g_hash_table_new_full (
		(GHashFunc)&gnm_cellpos_hash, (GCompareFunc)&gnm_cellpos_equal,
		NULL, (GDestroyNotify) &excel_shared_formula_free);
	esheet->tables		= g_hash_table_new_full (
		(GHashFunc)&gnm_cellpos_hash, (GCompareFunc)&gnm_cellpos_equal,
		NULL, (GDestroyNotify) g_free);
	esheet->biff2_prev_xf_index = -1;

	excel_init_margins (esheet);
	ms_container_init (&esheet->container, &vtbl,
		&importer->container, importer);
	g_ptr_array_add (importer->excel_sheets, esheet);

	return esheet;
}

void
excel_unexpected_biff (BiffQuery *q, char const *state,
		       int debug_level)
{
#ifndef NO_DEBUG_EXCEL
	if (debug_level > 1) {
		g_warning ("Unexpected Opcode in %s: 0x%hx, length 0x%x\n",
			state, q->opcode, q->length);
		if (debug_level > 2)
			gsf_mem_dump (q->data, q->length);
	}
#endif
}

/**
 * excel_read_string_header :
 * @data : a pointer to the start of the string header
 * @maxlen : the length of the data area
 * @use_utf16 : Is the content in 8 or 16 bit chars
 * @n_markup : number of trailing markup records
 * @has_extended : Is there trailing extended string info (eg japanese PHONETIC)
 * @post_data_len :
 *
 * returns the length of the header (in bytes)
 **/
static guint32
excel_read_string_header (guint8 const *data, guint32 maxlen,
			  gboolean *use_utf16,
			  unsigned *n_markup,
			  gboolean *has_extended,
			  unsigned *post_data_len)
{
	guint8 header;
	guint32 len;

	if (G_UNLIKELY (maxlen < 1))
		goto error;

	header = GSF_LE_GET_GUINT8 (data);
	if (((header & 0xf2) != 0))
		goto error;

	*use_utf16 = (header & 0x1) != 0;

	if ((header & 0x8) != 0) {
		if (G_UNLIKELY (maxlen < 3))
			goto error;
		*n_markup = GSF_LE_GET_GUINT16 (data + 1);
		*post_data_len = *n_markup * 4; /* 4 bytes per */
		len = 3;
	} else {
		*n_markup = 0;
		*post_data_len = 0;
		len = 1;
	}

	*has_extended = (header & 0x4) != 0;
	if (*has_extended) {
		guint32 len_ext_rst;

		if (G_UNLIKELY (maxlen < len + 4))
			goto error;
		len_ext_rst = GSF_LE_GET_GUINT32 (data + len); /* A byte length */
		*post_data_len += len_ext_rst;
		len += 4;

		g_warning ("Extended string support unimplemented; "
			   "ignoring %u bytes\n", len_ext_rst);
	}

	return len;

 error:
	*use_utf16 = *has_extended = FALSE;
	*n_markup = 0;
	*post_data_len = 0;
	g_warning ("Invalid string record.");
	return 0;
}

char *
excel_get_chars (GnmXLImporter const *importer,
		 guint8 const *ptr, size_t length, gboolean use_utf16)
{
	char* ans;
	size_t i;

	if (use_utf16) {
		gunichar2 *uni_text = g_alloca (sizeof (gunichar2)*length);

		for (i = 0; i < length; i++, ptr += 2)
			uni_text [i] = GSF_LE_GET_GUINT16 (ptr);
		ans = g_utf16_to_utf8 (uni_text, length, NULL, NULL, NULL);
	} else {
		size_t outbytes = (length + 2) * 8;
		char *outbuf = g_new (char, outbytes + 1);
		char *ptr2 = (char *)ptr;

		ans = outbuf;
		g_iconv (importer->str_iconv,
			 &ptr2, &length, &outbuf, &outbytes);

		i = outbuf - ans;
		ans[i] = 0;
		ans = g_realloc (ans, i + 1);
	}
	return ans;
}

char *
excel_get_text (GnmXLImporter const *importer,
		guint8 const *pos, guint32 length,
		guint32 *byte_length, guint32 maxlen)
{
	char *ans;
	guint8 const *ptr;
	unsigned byte_len, trailing_data_len, n_markup, str_len_bytes;
	gboolean use_utf16, has_extended;

	if (byte_length == NULL)
		byte_length = &byte_len;

	if (importer->ver >= MS_BIFF_V8) {
		*byte_length = 1; /* the header */
		if (length == 0)
			return NULL;
		ptr = pos + excel_read_string_header
			(pos, maxlen,
			 &use_utf16, &n_markup, &has_extended,
			 &trailing_data_len);
		*byte_length += trailing_data_len;
	} else {
		*byte_length = 0; /* no header */
		if (length == 0)
			return NULL;
		trailing_data_len = 0;
		use_utf16 = has_extended = FALSE;
		n_markup = 0;
		ptr = pos;
	}

	str_len_bytes = (use_utf16 ? 2 : 1) * length;
	if (*byte_length > maxlen) {
		*byte_length = maxlen;
		length = 0;
	} else if (maxlen - *byte_length < str_len_bytes) {
		*byte_length = maxlen;
		length = (maxlen - *byte_length) / (use_utf16 ? 2 : 1);
	} else
		*byte_length += str_len_bytes;

	ans = excel_get_chars (importer, ptr, length, use_utf16);

	d (4, {
		fprintf (stderr,"String len %d, byte length %d: %s %s %s:\n",
			length, *byte_length,
			(use_utf16 ? "UTF16" : "1byte"),
			((n_markup > 0) ? "has markup" :""),
			(has_extended ? "has extended phonetic info" : ""));
		gsf_mem_dump (pos, *byte_length);
	});

	return ans;
}

/**
 * excel_get_text_fixme :
 * @importer :
 * @pos : pointer to the start of string information
 * @length : in _characters_
 * @byte_len : The number of bytes between @pos and the end of string data
 *
 * Returns a string which the caller is responsible for freeing
 **/
static char *
excel_get_text_fixme (GnmXLImporter const *importer,
		      guint8 const *pos, guint32 length, guint32 *byte_length)
{
	return excel_get_text (importer, pos, length, byte_length,
					  G_MAXUINT);
}

static char *
excel_biff_text (GnmXLImporter const *importer,
		 const BiffQuery *q, guint32 ofs, guint32 length)
{
	XL_CHECK_CONDITION_VAL (q->length >= ofs, NULL);

	return excel_get_text (importer, q->data + ofs, length,
				    NULL, q->length - ofs);
}

char *
excel_biff_text_1 (GnmXLImporter const *importer,
		   const BiffQuery *q, guint32 ofs)
{
	guint32 length;

	XL_CHECK_CONDITION_VAL (q->length > ofs, NULL);
	length = GSF_LE_GET_GUINT8 (q->data + ofs);
	ofs++;

	return excel_get_text (importer, q->data + ofs, length,
				    NULL, q->length - ofs);
}

char *
excel_biff_text_2 (GnmXLImporter const *importer,
		   const BiffQuery *q, guint32 ofs)
{
	guint32 length;

	XL_CHECK_CONDITION_VAL (q->length > ofs + 1, NULL);
	length = GSF_LE_GET_GUINT16 (q->data + ofs);
	ofs += 2;

	return excel_get_text (importer, q->data + ofs, length,
			       NULL, q->length - ofs);
}

typedef struct {
	unsigned first, last;
	PangoAttrList *accum;
} TXORun;

static gboolean
append_markup (PangoAttribute *src, TXORun *run)
{
	if (run->last > run->first) {
		PangoAttribute *dst = pango_attribute_copy (src);
		dst->start_index = run->first;	/* inclusive */
		dst->end_index = run->last;	/* exclusive */
		pango_attr_list_change (run->accum, dst);
	}
	return FALSE;
}

static GOFormat *
excel_read_LABEL_markup (BiffQuery *q, ExcelReadSheet *esheet,
			 char const *str, unsigned str_len)
{
	guint8 const * const end  = q->data + q->length;
	guint8 const *ptr = q->data + 8 + str_len;
	MSContainer const *c = &esheet->container;
	TXORun txo_run;
	unsigned n;

	txo_run.last = G_MAXINT;

	if (esheet_ver (esheet) >= MS_BIFF_V8) {
		XL_CHECK_CONDITION_VAL (ptr+2 <= end , NULL);
		n = 4 * GSF_LE_GET_GUINT16 (ptr);
		ptr += 2;

		XL_CHECK_CONDITION_VAL (ptr + n == end , NULL);

		txo_run.accum = pango_attr_list_new ();
		while (n > 0) {
			n -= 4;
			txo_run.first = g_utf8_offset_to_pointer (str,
				GSF_LE_GET_GUINT16 (ptr + n)) - str;
			pango_attr_list_filter (ms_container_get_markup (
				c, GSF_LE_GET_GUINT16 (ptr + n + 2)),
				(PangoAttrFilterFunc) append_markup, &txo_run);
			txo_run.last = txo_run.first;
		}
	} else {
		XL_CHECK_CONDITION_VAL (ptr+1 <= end , NULL);
		n = 2 * GSF_LE_GET_GUINT8 (ptr);
		ptr += 1;

		XL_CHECK_CONDITION_VAL (ptr + n == end , NULL);

		txo_run.accum = pango_attr_list_new ();
		while (n > 0) {
			n -= 2;
			txo_run.first = g_utf8_offset_to_pointer (str,
				GSF_LE_GET_GUINT8 (ptr + n)) - str;
			pango_attr_list_filter (ms_container_get_markup (
				c, GSF_LE_GET_GUINT8 (ptr + n + 1)),
				(PangoAttrFilterFunc) append_markup, &txo_run);
			txo_run.last = txo_run.first;
		}
	}
	return go_format_new_markup (txo_run.accum, FALSE);
}

/*
 * NB. Whilst the string proper is split, and whilst we get several headers,
 * it seems that the attributes appear in a single block after the end
 * of the string, which may also be split over continues.
 */
static guint32
sst_read_string (BiffQuery *q, MSContainer const *c,
		 ExcelStringEntry *res, guint32 offset)
{
	guint32  get_len, chars_left, total_len, total_end_len = 0;
	unsigned i, post_data_len, n_markup, total_n_markup = 0;
	gboolean use_utf16, has_extended;
	char    *str, *old_res, *res_str = NULL;

	offset    = ms_biff_query_bound_check (q, offset, 2);
	if (offset == (guint32)-1)
		return offset;
	XL_CHECK_CONDITION_VAL (offset < q->length, offset);
	total_len = GSF_LE_GET_GUINT16 (q->data + offset);
	offset += 2;
	do {
		offset = ms_biff_query_bound_check (q, offset, 1);
		if (offset == (guint32)-1) {
			g_free (res_str);
			return offset;
		}
		offset += excel_read_string_header
			(q->data + offset, q->length - offset,
			 &use_utf16, &n_markup, &has_extended,
			 &post_data_len);
		total_end_len += post_data_len;
		total_n_markup += n_markup;
		chars_left = (q->length - offset) / (use_utf16 ? 2 : 1);
		get_len = (chars_left > total_len) ? total_len : chars_left;
		total_len -= get_len;

		XL_CHECK_CONDITION_VAL (get_len >= 0, 0);

		str = excel_get_chars (c->importer,
			q->data + offset, get_len, use_utf16);
		offset += get_len * (use_utf16 ? 2 : 1);

		if (res_str != NULL) {
			old_res = res_str;
			res_str = g_strconcat (old_res, str, NULL);
			g_free (str);
			g_free (old_res);
		} else
			res_str = str;
	} while (total_len > 0);

	if (total_n_markup > 0) {
		TXORun txo_run;
		PangoAttrList  *prev_markup = NULL;

		txo_run.accum = pango_attr_list_new ();
		txo_run.first = 0;
		for (i = total_n_markup ; i-- > 0 ; offset += 4) {
			offset = ms_biff_query_bound_check (q, offset, 4);
			if (offset == (guint32)-1) {
				g_free (res_str);
				return offset;
			}
			if ((q->length - offset) >= 4) {
				txo_run.last = g_utf8_offset_to_pointer (res_str,
					GSF_LE_GET_GUINT16 (q->data+offset)) - res_str;
				if (prev_markup != NULL)
					pango_attr_list_filter (prev_markup,
						(PangoAttrFilterFunc) append_markup, &txo_run);
				txo_run.first = txo_run.last;
				prev_markup = ms_container_get_markup (
					c, GSF_LE_GET_GUINT16 (q->data + offset + 2));
			} else
				g_warning ("A TXO entry is across CONTINUEs.  We need to handle those properly");
		}
		txo_run.last = G_MAXINT;
		pango_attr_list_filter (prev_markup,
			(PangoAttrFilterFunc) append_markup, &txo_run);
		res->markup = go_format_new_markup (txo_run.accum, FALSE);

		total_end_len -= 4*total_n_markup;
	}

	res->content = gnm_string_get_nocopy (res_str);
	return offset + total_end_len;
}

static void
excel_read_SST (BiffQuery *q, GnmXLImporter *importer)
{
	guint32 offset;
	unsigned i;

	XL_CHECK_CONDITION (q->length >= 8);

	d (4, {
		fprintf (stderr, "SST total = %u, sst = %u\n",
			 GSF_LE_GET_GUINT32 (q->data + 0),
			 GSF_LE_GET_GUINT32 (q->data + 4));
		gsf_mem_dump (q->data, q->length);
	});

	importer->sst_len = GSF_LE_GET_GUINT32 (q->data + 4);
	XL_CHECK_CONDITION (importer->sst_len < INT_MAX / sizeof (ExcelStringEntry));

	importer->sst = g_new0 (ExcelStringEntry, importer->sst_len);

	offset = 8;
	for (i = 0; i < importer->sst_len; i++) {
		offset = sst_read_string (q, &importer->container, importer->sst + i, offset);
		if (offset == (guint32)-1)
			break;

		if (importer->sst[i].content == NULL)
			d (4, fprintf (stderr,"Blank string in table at 0x%x.\n", i););
#ifndef NO_DEBUG_EXCEL
		else if (ms_excel_read_debug > 4)
			puts (importer->sst[i].content->str);
#endif
	}
}

static void
excel_read_EXSST (BiffQuery *q, GnmXLImporter *importer)
{
	XL_CHECK_CONDITION (q->length >= 2);
	d (10, fprintf (stderr,"Bucketsize = %hu,\tnum buckets = %d\n",
		       GSF_LE_GET_GUINT16 (q->data), (q->length - 2) / 8););
}

static void
excel_read_1904 (BiffQuery *q, GnmXLImporter *importer)
{
	XL_CHECK_CONDITION (q->length >= 2);
	if (GSF_LE_GET_GUINT16 (q->data) == 1)
		workbook_set_1904 (importer->wb, TRUE);
}

GnmValue *
biff_get_error (GnmEvalPos const *pos, guint8 err)
{
	switch (err) {
	case 0:  return value_new_error_NULL (pos);
	case 7:  return value_new_error_DIV0 (pos);
	case 15: return value_new_error_VALUE (pos);
	case 23: return value_new_error_REF (pos);
	case 29: return value_new_error_NAME (pos);
	case 36: return value_new_error_NUM (pos);
	case 42: return value_new_error_NA (pos);
	default: return value_new_error (pos, _("#UNKNOWN!"));
	}
}

MsBiffBofData *
ms_biff_bof_data_new (BiffQuery *q)
{
	MsBiffBofData *ans = g_new (MsBiffBofData, 1);

	if (q->length >= 4) {

		/* Determine type from BOF */
		switch (q->opcode) {
		case BIFF_BOF_v0:	ans->version = MS_BIFF_V2; break;
		case BIFF_BOF_v2:	ans->version = MS_BIFF_V3; break;
		case BIFF_BOF_v4:	ans->version = MS_BIFF_V4; break;
		case BIFF_BOF_v8:
			d (2, {
				fprintf (stderr,"Complicated BIFF version 0x%x\n",
					GSF_LE_GET_GUINT16 (q->non_decrypted_data));
				gsf_mem_dump (q->non_decrypted_data, q->length);
			});

			switch (GSF_LE_GET_GUINT16 (q->non_decrypted_data)) {
			case 0x0600: ans->version = MS_BIFF_V8;
				     break;
			case 0x0500: /* * OR ebiff7: FIXME ? !  */
				     ans->version = MS_BIFF_V7;
				     break;
			default:
				g_printerr ("Unknown BIFF sub-number 0x%X in BOF %x\n",
					 GSF_LE_GET_GUINT16 (q->non_decrypted_data), q->opcode);
				ans->version = MS_BIFF_V_UNKNOWN;
			}
			break;

		default:
			g_printerr ("Unknown BIFF number in BOF %x\n", q->opcode);
			ans->version = MS_BIFF_V_UNKNOWN;
			g_printerr ("Biff version %d\n", ans->version);
		}
		switch (GSF_LE_GET_GUINT16 (q->non_decrypted_data + 2)) {
		case 0x0005: ans->type = MS_BIFF_TYPE_Workbook; break;
		case 0x0006: ans->type = MS_BIFF_TYPE_VBModule; break;
		case 0x0010: ans->type = MS_BIFF_TYPE_Worksheet; break;
		case 0x0020: ans->type = MS_BIFF_TYPE_Chart; break;
		case 0x0040: ans->type = MS_BIFF_TYPE_Macrosheet; break;
		case 0x0100: ans->type = MS_BIFF_TYPE_Workspace; break;
		default:
			ans->type = MS_BIFF_TYPE_Unknown;
			g_printerr ("Unknown BIFF type in BOF %x\n", GSF_LE_GET_GUINT16 (q->non_decrypted_data + 2));
			break;
		}
		/* Now store in the directory array: */
		d (2, fprintf (stderr,"BOF %x, %d == %d, %d\n", q->opcode, q->length,
			      ans->version, ans->type););
	} else {
		g_printerr ("Not a BOF !\n");
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
excel_read_BOUNDSHEET (BiffQuery *q, GnmXLImporter *importer)
{
	BiffBoundsheetData *bs;
	char const *default_name = "Unknown%d";

	bs = g_new0 (BiffBoundsheetData, 1);
	bs->gnm_type = GNM_SHEET_DATA;

	if (importer->ver <= MS_BIFF_V4) {
		bs->streamStartPos = 0; /* Excel 4 doesn't tell us */
		bs->type = MS_BIFF_TYPE_Worksheet;
		default_name = _("Sheet%d");
		bs->visibility = GNM_SHEET_VISIBILITY_VISIBLE;
		bs->name = excel_biff_text_1 (importer, q, 0);
	} else {
		if (importer->ver > MS_BIFF_V8)
			g_printerr ("Unknown BIFF Boundsheet spec. Assuming same as Biff7 FIXME\n");
		bs->streamStartPos = GSF_LE_GET_GUINT32 (q->non_decrypted_data);

		/* NOTE : MS Docs appear wrong.  It is visiblity _then_ type */
		switch (GSF_LE_GET_GUINT8 (q->data + 5)) {
		case 0: bs->type = MS_BIFF_TYPE_Worksheet;
			default_name = _("Sheet%d");
			break;
		case 1: bs->type = MS_BIFF_TYPE_Macrosheet;
			bs->gnm_type = GNM_SHEET_XLM;
			default_name = _("Macro%d");
			break;
		case 2: bs->type = MS_BIFF_TYPE_Chart;
			bs->gnm_type = GNM_SHEET_OBJECT;
			default_name = _("Chart%d");
			break;
		case 6: bs->type = MS_BIFF_TYPE_VBModule;
			default_name = _("Module%d");
			break;
		default:
			g_printerr ("Unknown boundsheet type: %d\n", GSF_LE_GET_GUINT8 (q->data + 4));
			bs->type = MS_BIFF_TYPE_Unknown;
		}
		switch ((GSF_LE_GET_GUINT8 (q->data + 4)) & 0x3) {
		case 0: bs->visibility = GNM_SHEET_VISIBILITY_VISIBLE;
			break;
		case 1: bs->visibility = GNM_SHEET_VISIBILITY_HIDDEN;
			break;
		case 2: bs->visibility = GNM_SHEET_VISIBILITY_VERY_HIDDEN;
			break;
		default:
			g_printerr ("Unknown sheet hiddenness %d\n", (GSF_LE_GET_GUINT8 (q->data + 4)) & 0x3);
			bs->visibility = GNM_SHEET_VISIBILITY_VISIBLE;
		}

		/* TODO: find some documentation on this.
		* Sample data and OpenCalc imply that the docs are incorrect.  It
		* seems like the name length is 1 byte.  Loading sample sheets in
		* other locales universally seem to treat the first byte as a length
		* and the second as the unicode flag header.
		*/
		bs->name = excel_biff_text_1 (importer, q, 6);
	}

	/* TODO: find some documentation on this.
	 * It appears that if the name is null it defaults to Sheet%d?
	 * However, we have only one test case and no docs.
	 */
	if (bs->name == NULL)
		bs->name = g_strdup_printf (default_name,
			importer->boundsheet_sheet_by_index->len);

	switch (bs->type) {
	case MS_BIFF_TYPE_Worksheet :
	case MS_BIFF_TYPE_Macrosheet :
	case MS_BIFF_TYPE_Chart :
		bs->esheet = excel_sheet_new (importer, bs->name, bs->gnm_type);

		if (bs->esheet && bs->esheet->sheet)
			g_object_set (bs->esheet->sheet,
				      "visibility", bs->visibility,
				      NULL);
		break;
	default :
		bs->esheet = NULL;
	}

	bs->index = importer->boundsheet_sheet_by_index->len;
	g_ptr_array_add (importer->boundsheet_sheet_by_index, bs->esheet ? bs->esheet->sheet : NULL);
	g_hash_table_insert (importer->boundsheet_data_by_stream,
		GUINT_TO_POINTER (bs->streamStartPos), bs);

	d (1, fprintf (stderr,"Boundsheet: %d) '%s' %p, %d:%d\n", bs->index,
		       bs->name, bs->esheet, bs->type, bs->visibility););
}

static void
biff_boundsheet_data_destroy (BiffBoundsheetData *d)
{
	g_free (d->name);
	g_free (d);
}

static void
excel_read_FORMAT (BiffQuery *q, GnmXLImporter *importer)
{
	MsBiffVersion const ver = importer->ver;
	BiffFormatData *d;

	if (ver >= MS_BIFF_V7) {
		XL_CHECK_CONDITION (q->length >= 4);

		d = g_new (BiffFormatData, 1);
		d->idx = GSF_LE_GET_GUINT16 (q->data);
		d->name = (ver >= MS_BIFF_V8)
			? excel_biff_text_2 (importer, q, 2)
			: excel_biff_text_1 (importer, q, 2);
	} else {
		XL_CHECK_CONDITION (q->length >= 3);

		d = g_new (BiffFormatData, 1);
		/* no usable index */
		d->idx = g_hash_table_size (importer->format_table);
		d->name = (ver >= MS_BIFF_V4)
			? excel_biff_text_1 (importer, q, 2)
			: excel_biff_text_1 (importer, q, 0);
	}

	d (3, fprintf (stderr, "Format data: 0x%x == '%s'\n", d->idx, d->name););

	g_hash_table_insert (importer->format_table, GUINT_TO_POINTER (d->idx), d);
}

static void
excel_read_FONT (BiffQuery *q, GnmXLImporter *importer)
{
	MsBiffVersion const ver = importer->ver;
	ExcelFont *fd = g_new (ExcelFont, 1);
	guint16 data;
	guint8 data1;

	XL_CHECK_CONDITION (q->length >= 4);
	fd->height = GSF_LE_GET_GUINT16 (q->data + 0);
	data = GSF_LE_GET_GUINT16 (q->data + 2);
	fd->italic     = (data & 0x2) == 0x2;
	fd->struck_out = (data & 0x8) == 0x8;
	if (ver <= MS_BIFF_V2) {
		guint16 opcode;
		fd->boldness  = (data & 0x1) ? 0x2bc : 0x190;
		fd->underline = (data & 0x4) ? UNDERLINE_SINGLE : UNDERLINE_NONE;
		fd->script = GO_FONT_SCRIPT_STANDARD;
		fd->fontname = excel_biff_text_1 (importer, q, 4);
		if (ms_biff_query_peek_next (q, &opcode) &&
		    opcode == BIFF_FONT_COLOR) {
			ms_biff_query_next (q);
			fd->color_idx  = GSF_LE_GET_GUINT16 (q->data);
		} else
			fd->color_idx  = 0x7f;
	} else if (ver <= MS_BIFF_V4) /* Guess */ {
		XL_CHECK_CONDITION (q->length >= 6);
		fd->color_idx  = GSF_LE_GET_GUINT16 (q->data + 4);
		fd->boldness  = (data & 0x1) ? 0x2bc : 0x190;
		fd->underline = (data & 0x4) ? UNDERLINE_SINGLE : UNDERLINE_NONE;
		fd->script = GO_FONT_SCRIPT_STANDARD;
		fd->fontname = excel_biff_text_1 (importer, q, 6);
	} else {
		XL_CHECK_CONDITION (q->length >= 11);
		fd->color_idx  = GSF_LE_GET_GUINT16 (q->data + 4);
		fd->boldness   = GSF_LE_GET_GUINT16 (q->data + 6);
		data = GSF_LE_GET_GUINT16 (q->data + 8);
		switch (data) {
		case 0: fd->script = GO_FONT_SCRIPT_STANDARD; break;
		case 1: fd->script = GO_FONT_SCRIPT_SUPER; break;
		case 2: fd->script = GO_FONT_SCRIPT_SUB; break;
		default:
			g_printerr ("Unknown script %d\n", data);
			break;
		}

		data1 = GSF_LE_GET_GUINT8 (q->data + 10);
		switch (data1) {
		case 0:    fd->underline = UNDERLINE_NONE; break;
		case 1:    fd->underline = UNDERLINE_SINGLE; break;
		case 2:	   fd->underline = UNDERLINE_DOUBLE; break;
		case 0x21: fd->underline = UNDERLINE_SINGLE; break;	/* single accounting */
		case 0x22: fd->underline = UNDERLINE_DOUBLE; break;	/* double accounting */
		}
		fd->fontname = excel_biff_text_1 (importer, q, 14);
	}
	fd->color_idx &= 0x7f; /* Undocumented but a good idea */

	if (fd->fontname == NULL) {
		/* Note sure why -- see #418868.  */
		fd->fontname = g_strdup ("Arial");
	}

	fd->attrs = NULL;
	fd->go_font = NULL;

        fd->index = g_hash_table_size (importer->font_data);
	if (fd->index >= 4) /* Weird: for backwards compatibility */
		fd->index++;
	g_hash_table_insert (importer->font_data, GINT_TO_POINTER (fd->index), fd);

	d (1, fprintf (stderr,"Insert font '%s' (%d) size %d pts color %d\n",
		      fd->fontname, fd->index, fd->height / 20, fd->color_idx););
	d (3, fprintf (stderr,"Font color = 0x%x\n", fd->color_idx););
}

static void
excel_font_free (ExcelFont *fd)
{
	if (NULL != fd->attrs) {
		pango_attr_list_unref (fd->attrs);
		fd->attrs = NULL;
	}
	if (NULL != fd->go_font) {
		go_font_unref (fd->go_font);
		fd->go_font = NULL;
	}
	g_free (fd->fontname);
	g_free (fd);
}

static void
biff_format_data_destroy (BiffFormatData *d)
{
	g_free (d->name);
	g_free (d);
}

/** Default color table for BIFF5/BIFF7. */
ExcelPaletteEntry const excel_default_palette_v7 [] = {
	{  0,  0,  0}, {255,255,255}, {255,  0,  0}, {  0,255,  0},
	{  0,  0,255}, {255,255,  0}, {255,  0,255}, {  0,255,255},

	{128,  0,  0}, {  0,128,  0}, {  0,  0,128}, {128,128,  0},
	{128,  0,128}, {  0,128,128}, {192,192,192}, {128,128,128},

	{128,128,255}, {128, 32, 96}, {255,255,192}, {160,224,224},
	{ 96,  0,128}, {255,128,128}, {  0,128,192}, {192,192,255},

	{  0,  0,128}, {255,  0,255}, {255,255,  0}, {  0,255,255},
	{128,  0,128}, {128,  0,  0}, {  0,128,128}, {  0,  0,255},

	{  0,204,255}, {105,255,255}, {204,255,204}, {255,255,153},
	{166,202,240}, {204,156,204}, {204,153,255}, {227,227,227},

	{ 51,102,255}, { 51,204,204}, { 51,153, 51}, {153,153, 51},
	{153,102, 51}, {153,102,102}, {102,102,153}, {150,150,150},

	{ 51, 51,204}, { 51,102,102}, {  0, 51,  0}, { 51, 51,  0},
	{102, 51,  0}, {153, 51,102}, { 51, 51,153}, { 66, 66, 66}
};

ExcelPaletteEntry const excel_default_palette_v8 [] = {
	{  0,  0,  0}, {255,255,255}, {255,  0,  0}, {  0,255,  0},
	{  0,  0,255}, {255,255,  0}, {255,  0,255}, {  0,255,255},

	{128,  0,  0}, {  0,128,  0}, {  0,  0,128}, {128,128,  0},
	{128,  0,128}, {  0,128,128}, {192,192,192}, {128,128,128},

	{153,153,255}, {153, 51,102}, {255,255,204}, {204,255,255},
	{102,  0,102}, {255,128,128}, {  0,102,204}, {204,204,255},

	{  0,  0,128}, {255,  0,255}, {255,255,  0}, {  0,255,255},
	{128,  0,128}, {128,  0,  0}, {  0,128,128}, {  0,  0,255},

	{  0,204,255}, {204,255,255}, {204,255,204}, {255,255,153},
	{153,204,255}, {255,153,204}, {204,153,255}, {255,204,153},

	{ 51,102,255}, { 51,204,204}, {153,204,  0}, {255,204,  0},
	{255,153,  0}, {255,102,  0}, {102,102,153}, {150,150,150},

	{  0, 51,102}, { 51,153,102}, {  0, 51,  0}, { 51, 51,  0},
	{153, 51,  0}, {153, 51,102}, { 51, 51,153}, { 51, 51, 51}
};

GnmColor *
excel_palette_get (GnmXLImporter *importer, gint idx)
{
	ExcelPalette *pal;

	/* return black on failure */
	g_return_val_if_fail (importer != NULL, style_color_black ());

	if (NULL == (pal = importer->palette)) {
		int entries = EXCEL_DEF_PAL_LEN;
			ExcelPaletteEntry const *defaults = (importer->ver >= MS_BIFF_V8)
			? excel_default_palette_v8 : excel_default_palette_v7;

			pal = importer->palette = g_new (ExcelPalette, 1);
		pal->length = entries;
		pal->red   = g_new (int, entries);
		pal->green = g_new (int, entries);
		pal->blue  = g_new (int, entries);
		pal->gnm_colors = g_new (GnmColor *, entries);

		while (--entries >= 0) {
			pal->red[entries]   = defaults[entries].r;
			pal->green[entries] = defaults[entries].g;
			pal->blue[entries]  = defaults[entries].b;
			pal->gnm_colors[entries] = NULL;
		}
	}

	/* NOTE: not documented but seems close
	 * If you find a normative reference please forward it.
	 *
	 * The color index field seems to use
	 *	8-63 = Palette index 0-55
	 *	64  = auto pattern, auto border
	 *      65  = auto background
	 *      127 = auto font
	 *
	 *      65 is always white, and 127 always black. 64 is black
	 *      if the fDefaultHdr flag in WINDOW2 is unset, otherwise it's
	 *      the grid color from WINDOW2.
	 */

	d (4, fprintf (stderr,"Color Index %d\n", idx););

	if (idx == 1 || idx == 65)
		return style_color_white ();
	switch (idx) {
	case 0:   /* black */
	case 64 : /* system text ? */
	case 81 : /* tooltip text */
	case 0x7fff : /* system text ? */
		return style_color_black ();
	case 1 :  /* white */
	case 65 : /* system back ? */
		return style_color_white ();

	case 80 : /* tooltip background */
		return style_color_new_gdk (&gs_yellow);

	case 2 : return style_color_new_i8 (0xff,    0,    0); /* red */
	case 3 : return style_color_new_i8 (   0, 0xff,    0); /* green */
	case 4 : return style_color_new_i8 (   0,    0, 0xff); /* blue */
	case 5 : return style_color_new_i8 (0xff, 0xff,    0); /* yellow */
	case 6 : return style_color_new_i8 (0xff,    0, 0xff); /* magenta */
	case 7 : return style_color_new_i8 (   0, 0xff, 0xff); /* cyan */
	default :
		 break;
	}

	idx -= 8;
	if (idx < 0 || pal->length <= idx) {
		g_warning ("EXCEL: color index (%d) is out of range (8..%d). Defaulting to black",
			   idx + 8, pal->length+8);
		return style_color_black ();
	}

	if (pal->gnm_colors[idx] == NULL) {
		pal->gnm_colors[idx] =
			style_color_new_i8 ((guint8) pal->red[idx],
					    (guint8) pal->green[idx],
					    (guint8) pal->blue[idx]);
		g_return_val_if_fail (pal->gnm_colors[idx],
				      style_color_black ());
		d (5, {
			GnmColor *c = pal->gnm_colors[idx];
			fprintf (stderr,"New color in slot %d: RGB= %x,%x,%x\n",
				idx, c->gdk_color.red, c->gdk_color.green, c->gdk_color.blue);
		});
	}

	style_color_ref (pal->gnm_colors[idx]);
	return pal->gnm_colors[idx];
}

static void
excel_palette_destroy (ExcelPalette *pal)
{
	guint16 lp;

	g_free (pal->red);
	g_free (pal->green);
	g_free (pal->blue);
	for (lp = 0; lp < pal->length; lp++)
		if (pal->gnm_colors[lp])
			style_color_unref (pal->gnm_colors[lp]);
	g_free (pal->gnm_colors);
	g_free (pal);
}

static void
excel_read_PALETTE (BiffQuery *q, GnmXLImporter *importer)
{
	int lp, len;
	ExcelPalette *pal;

	XL_CHECK_CONDITION (q->length >= 2);
	len = GSF_LE_GET_GUINT16 (q->data);
	XL_CHECK_CONDITION (q->length >= 2u + len * 4u);

	pal = g_new (ExcelPalette, 1);
	pal->length = len;
	pal->red = g_new (int, len);
	pal->green = g_new (int, len);
	pal->blue = g_new (int, len);
	pal->gnm_colors = g_new (GnmColor *, len);

	d (3, fprintf (stderr,"New palette with %d entries\n", len););

	for (lp = 0; lp < len; lp++) {
		guint32 num = GSF_LE_GET_GUINT32 (q->data + 2 + lp * 4);

		/* NOTE the order of bytes is different from what one would
		 * expect */
		pal->blue[lp] = (num & 0x00ff0000) >> 16;
		pal->green[lp] = (num & 0x0000ff00) >> 8;
		pal->red[lp] = (num & 0x000000ff) >> 0;
		d (5, fprintf (stderr,"Colour %d: 0x%8x (%x,%x,%x)\n", lp,
			      num, pal->red[lp], pal->green[lp], pal->blue[lp]););

		pal->gnm_colors[lp] = NULL;
	}
	if (importer->palette)
		excel_palette_destroy (importer->palette);
	importer->palette = pal;

}

/**
 * Search for a font record from its index in the workbooks font table
 * NB. index 4 is omitted supposedly for backwards compatiblity
 * Returns the font color if there is one.
 **/
ExcelFont const *
excel_font_get (GnmXLImporter const *importer, unsigned font_idx)
{
	ExcelFont const *fd = g_hash_table_lookup (
		importer->font_data, GINT_TO_POINTER (font_idx));

	g_return_val_if_fail (fd != NULL, NULL); /* flag the problem */
	g_return_val_if_fail (fd->index != 4, NULL); /* should not exist */

	return fd;
}

GOFont const *
excel_font_get_gofont (ExcelFont const *efont)
{
	if (NULL == efont->go_font) {
		PangoFontDescription *desc = pango_font_description_new ();

#warning FINISH when GOFont is smarter
		pango_font_description_set_family (desc, efont->fontname);
		pango_font_description_set_weight (desc, efont->boldness);
		pango_font_description_set_style (desc,
			efont->italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
		pango_font_description_set_size (desc,
			efont->height * PANGO_SCALE / 20);

		((ExcelFont *)efont)->go_font = go_font_new_by_desc (desc);
	}
	return efont->go_font;
}

static BiffXFData const *
excel_get_xf (ExcelReadSheet *esheet, unsigned xfidx)
{
	GPtrArray const * const p = esheet->container.importer->XF_cell_records;

	g_return_val_if_fail (p != NULL, NULL);

	if (esheet_ver (esheet) == MS_BIFF_V2) {
		/* ignore the replicated info that comes with the index
		 * we've already parsed the XF record */
		xfidx &= 0x3f;
		if (xfidx == 0x3f) {
			if (esheet->biff2_prev_xf_index < 0) {
				g_warning ("extension xf with no preceding old_xf record, using default as fallback");
				xfidx = 15;
			} else
				xfidx = esheet->biff2_prev_xf_index;
		}
	}

	if (xfidx >= p->len) {
		XL_CHECK_CONDITION_VAL (p->len > 0, NULL);
		g_warning ("XL: Xf index 0x%X is not in the range[0..0x%X)", xfidx, p->len);
		xfidx = 0;
	}
	/* FIXME: when we can handle styles too deal with this correctly */
	/* g_return_val_if_fail (xf->xftype == MS_BIFF_X_CELL, NULL); */
	return g_ptr_array_index (p, xfidx);
}

/* Adds a ref the result */
static GnmStyle *
excel_get_style_from_xf (ExcelReadSheet *esheet, BiffXFData const *xf)
{
	ExcelFont const *fd;
	GnmColor *pattern_color, *back_color, *font_color;
	int	  pattern_index,  back_index,  font_index;
	GnmStyle *mstyle;
	int i;

	if (xf == NULL)
		return NULL;

	/* If we've already done the conversion use the cached style */
	if (xf->mstyle != NULL) {
		gnm_style_ref (xf->mstyle);
		return xf->mstyle;
	}

	/* Create a new style and fill it in */
	mstyle = gnm_style_new_default ();

	/* Format */
	if (xf->style_format)
		gnm_style_set_format (mstyle, xf->style_format);

	/* protection */
	gnm_style_set_contents_locked (mstyle, xf->locked);
	gnm_style_set_contents_hidden (mstyle, xf->hidden);

	/* Alignment */
	gnm_style_set_align_v   (mstyle, xf->valign);
	gnm_style_set_align_h   (mstyle, xf->halign);
	gnm_style_set_wrap_text (mstyle, xf->wrap_text);
	gnm_style_set_shrink_to_fit (mstyle, xf->shrink_to_fit);
	gnm_style_set_indent    (mstyle, xf->indent);
	gnm_style_set_rotation  (mstyle, xf->rotation);
	gnm_style_set_text_dir  (mstyle, xf->text_dir);

	/* Font */
	fd = excel_font_get (esheet->container.importer, xf->font_idx);
	if (fd != NULL) {
		gnm_style_set_font_name   (mstyle, fd->fontname);
		gnm_style_set_font_size   (mstyle, fd->height / 20.0);
		gnm_style_set_font_bold   (mstyle, fd->boldness >= 0x2bc);
		gnm_style_set_font_italic (mstyle, fd->italic);
		gnm_style_set_font_strike (mstyle, fd->struck_out);
		gnm_style_set_font_script (mstyle, fd->script);
		gnm_style_set_font_uline  (mstyle, fd->underline);

		font_index = fd->color_idx;
	} else
		font_index = 127; /* Default to Black */

	/* Background */
	gnm_style_set_pattern (mstyle, xf->fill_pattern_idx);

	/* Solid patterns seem to reverse the meaning */
	if (xf->fill_pattern_idx == 1) {
		pattern_index	= xf->pat_backgnd_col;
		back_index	= xf->pat_foregnd_col;
	} else {
		pattern_index	= xf->pat_foregnd_col;
		back_index	= xf->pat_backgnd_col;
	}

	d (4, fprintf (stderr,"back = %d, pat = %d, font = %d, pat_style = %d\n",
		      back_index, pattern_index, font_index, xf->fill_pattern_idx););

	if (font_index == 127)
		font_color = style_color_auto_font ();
	else
		font_color = excel_palette_get (esheet->container.importer, font_index);

	switch (back_index) {
	case 64:
		back_color = sheet_style_get_auto_pattern_color (esheet->sheet);
		break;
	case 65:
		back_color = style_color_auto_back ();
		break;
	default:
		back_color = excel_palette_get (esheet->container.importer, back_index);
		break;
	}

	switch (pattern_index) {
	case 64:		/* Normal case for auto pattern color */
		pattern_color = sheet_style_get_auto_pattern_color
			(esheet->sheet);
		break;
	case 65:
		/* Mutated form, also observed in the wild, but only for
		solid fill. I. e.: this color is not visible. */
		pattern_color = style_color_auto_back ();
		break;
	default:
		pattern_color = excel_palette_get (esheet->container.importer, pattern_index);
		break;
	}

	g_return_val_if_fail (back_color && pattern_color && font_color, NULL);

	d (4, fprintf (stderr,"back = #%02x%02x%02x, pat = #%02x%02x%02x, font = #%02x%02x%02x, pat_style = %d\n",
		      back_color->gdk_color.red>>8, back_color->gdk_color.green>>8, back_color->gdk_color.blue>>8,
		      pattern_color->gdk_color.red>>8, pattern_color->gdk_color.green>>8, pattern_color->gdk_color.blue>>8,
		      font_color->gdk_color.red>>8, font_color->gdk_color.green>>8, font_color->gdk_color.blue>>8,
		      xf->fill_pattern_idx););

	gnm_style_set_font_color (mstyle, font_color);
	gnm_style_set_back_color (mstyle, back_color);
	gnm_style_set_pattern_color (mstyle, pattern_color);

	/* Borders */
	for (i = 0; i < STYLE_ORIENT_MAX; i++) {
		GnmStyle *tmp = mstyle;
		GnmStyleElement const t = MSTYLE_BORDER_TOP + i;
		GnmStyleBorderLocation const sbl = GNM_STYLE_BORDER_TOP + i;
		int const color_index = xf->border_color[i];
		GnmColor *color;

		switch (color_index) {
		case 64:
			color = sheet_style_get_auto_pattern_color
				(esheet->sheet);
			d (4, fprintf (stderr,"border with color_index=%d\n",
				      color_index););
			break;
		case 65:
			color = style_color_auto_back ();
			/* We haven't seen this yet.
			   We know that 64 and 127 occur in the wild */
			d (4, fprintf (stderr,"border with color_index=%d\n",
				      color_index););
			break;
		case 127:
			color = style_color_auto_font ();
			break;
		default:
			color = excel_palette_get (esheet->container.importer, color_index);
			break;
		}
		gnm_style_set_border (tmp, t,
				      gnm_style_border_fetch (xf->border_type[i],
							  color,
							  gnm_style_border_get_orientation (sbl)));
	}

	/* Set the cache (const_cast) */
	((BiffXFData *)xf)->mstyle = mstyle;
	gnm_style_ref (mstyle);
	return xf->mstyle;
}

static BiffXFData const *
excel_set_xf (ExcelReadSheet *esheet, BiffQuery *q)
{
	unsigned col, row;
	BiffXFData const *xf;
	GnmStyle *mstyle;
	Sheet *sheet = esheet->sheet;

	XL_CHECK_CONDITION_VAL (q->length >= 6, NULL);
	col = XL_GETCOL (q);
	row = XL_GETROW (q);
	xf = excel_get_xf (esheet, GSF_LE_GET_GUINT16 (q->data + 4));
	XL_CHECK_CONDITION_VAL (col < gnm_sheet_get_max_cols (sheet), xf);
	XL_CHECK_CONDITION_VAL (row < gnm_sheet_get_max_rows (sheet), xf);
	mstyle = excel_get_style_from_xf (esheet, xf);

	d (3, fprintf (stderr,"%s!%s%d = xf(0x%hx) = style (%p) [LEN = %u]\n", sheet->name_unquoted,
		 col_name (col), row + 1, GSF_LE_GET_GUINT16 (q->data + 4), mstyle, q->length););

	if (mstyle != NULL)
		sheet_style_set_pos (sheet, col, row, mstyle);
	return xf;
}

static void
excel_set_xf_segment (ExcelReadSheet *esheet,
		      int start_col, int end_col,
		      int start_row, int end_row, unsigned xfidx)
{
	GnmRange   range;
	GnmStyle *mstyle = excel_get_style_from_xf (esheet,
		excel_get_xf (esheet, xfidx));

	if (mstyle == NULL)
		return;

	range.start.col = start_col;
	range.start.row = start_row;
	range.end.col   = end_col;
	range.end.row   = end_row;
	sheet_style_set_range (esheet->sheet, &range, mstyle);

	d (2, {
		range_dump (&range, "");
		fprintf (stderr, " = xf(%d)\n", xfidx);
	});
}

static GnmStyleBorderType
biff_xf_map_border (int b)
{
	switch (b) {
	case 0: /* None */
		return GNM_STYLE_BORDER_NONE;
	case 1: /* Thin */
		return GNM_STYLE_BORDER_THIN;
	case 2: /* Medium */
		return GNM_STYLE_BORDER_MEDIUM;
	case 3: /* Dashed */
		return GNM_STYLE_BORDER_DASHED;
	case 4: /* Dotted */
		return GNM_STYLE_BORDER_DOTTED;
	case 5: /* Thick */
		return GNM_STYLE_BORDER_THICK;
	case 6: /* Double */
		return GNM_STYLE_BORDER_DOUBLE;
	case 7: /* Hair */
		return GNM_STYLE_BORDER_HAIR;
	case 8: /* Medium Dashed */
		return GNM_STYLE_BORDER_MEDIUM_DASH;
	case 9: /* Dash Dot */
		return GNM_STYLE_BORDER_DASH_DOT;
	case 10: /* Medium Dash Dot */
		return GNM_STYLE_BORDER_MEDIUM_DASH_DOT;
	case 11: /* Dash Dot Dot */
		return GNM_STYLE_BORDER_DASH_DOT_DOT;
	case 12: /* Medium Dash Dot Dot */
		return GNM_STYLE_BORDER_MEDIUM_DASH_DOT_DOT;
	case 13: /* Slanted Dash Dot*/
		return GNM_STYLE_BORDER_SLANTED_DASH_DOT;
	}
	g_printerr ("Unknown border style %d\n", b);
	return GNM_STYLE_BORDER_NONE;
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
	XL_CHECK_CONDITION_VAL (i >= 0 && i < (int)G_N_ELEMENTS (map_from_excel), 0);

	return map_from_excel[i];
}

static void
excel_read_XF_OLD (BiffQuery *q, GnmXLImporter *importer)
{
	BiffXFData *xf;
	guint16 data;
        guint8 subdata;

	d ( 2, fprintf(stderr, "XF # %d\n", importer->XF_cell_records->len); );
	d ( 2, gsf_mem_dump (q->data, q->length); );

	XL_CHECK_CONDITION (q->length > (importer->ver >= MS_BIFF_V3 ? 12 : 4));

	xf = g_new0 (BiffXFData, 1);
        xf->font_idx = q->data[0];
        xf->format_idx = (importer->ver >= MS_BIFF_V3)
		? q->data[1] : (q->data[2] & 0x3f);
	xf->style_format = (xf->format_idx > 0)
		? excel_wb_get_fmt (importer, xf->format_idx) : NULL;
	xf->is_simple_format = xf->style_format == NULL ||
		go_format_is_simple (xf->style_format);

        if (importer->ver >= MS_BIFF_V3) {
		xf->locked = (q->data[2] & 0x1) != 0;
		xf->hidden = (q->data[2] & 0x2) != 0;
		xf->xftype = (q->data[2] & 0x4)
			? MS_BIFF_X_STYLE : MS_BIFF_X_CELL;
	} else {
		xf->locked = (q->data[1] & 0x40) != 0;
		xf->hidden = (q->data[1] & 0x80) != 0;
		xf->xftype = MS_BIFF_X_CELL;
	}
	xf->parentstyle = 0; /* TODO extract for biff 3 and biff4 */
        xf->format = MS_BIFF_F_MS;
	xf->wrap_text = FALSE;
	xf->shrink_to_fit = FALSE;


        xf->halign = HALIGN_GENERAL;

	data = (importer->ver >= MS_BIFF_V3) ? q->data[4] : q->data[3];
	switch (data & 0x07) {
	default :
	case 0: xf->halign = HALIGN_GENERAL; break;
	case 1: xf->halign = HALIGN_LEFT; break;
	case 2: xf->halign = HALIGN_CENTER; break;
	case 3: xf->halign = HALIGN_RIGHT; break;
	case 4: xf->halign = HALIGN_FILL; break;
	case 5: xf->halign = HALIGN_JUSTIFY; break;
	case 6: xf->halign = HALIGN_CENTER_ACROSS_SELECTION; break;
	}

        xf->valign = VALIGN_BOTTOM;
        xf->rotation = 0;
        xf->indent = 0;
        xf->differences = 0;
	xf->text_dir = GNM_TEXT_DIR_CONTEXT;

        if (importer->ver >= MS_BIFF_V4) {
		xf->wrap_text = (data & 0x0008) != 0;
		switch (data & 0x30) {
		case 0x00: xf->valign = VALIGN_TOP; break;
		case 0x10: xf->valign = VALIGN_CENTER; break;
		default :
		case 0x20: xf->valign = VALIGN_BOTTOM; break;
		}
		switch (data & 0xc0) {
		case 0x00: xf->rotation = 0; break;
		case 0x40: xf->rotation = -1; break;
		case 0x80: xf->rotation = 90; break;
		case 0xc0: xf->rotation = 270; break;
		}
	} else if (importer->ver >= MS_BIFF_V3) {
		xf->wrap_text = (data & 0x0008) != 0;
		if (xf->wrap_text)
			xf->valign = VALIGN_TOP;
	}

	if (importer->ver >= MS_BIFF_V3) {
		data = GSF_LE_GET_GUINT16 (q->data + 6);
		xf->pat_backgnd_col = (data & 0xf800) >> 11;
		if (xf->pat_backgnd_col >= 24)
			xf->pat_backgnd_col += 40; /* Defaults */
		xf->pat_foregnd_col = (data & 0x07c0) >> 6;
		if (xf->pat_foregnd_col >= 24)
			xf->pat_foregnd_col += 40; /* Defaults */
		xf->fill_pattern_idx =
			excel_map_pattern_index_from_excel(data & 0x001f);

		data = GSF_LE_GET_GUINT8 (q->data + 10);
		xf->border_type[STYLE_BOTTOM] = biff_xf_map_border(data & 0x07);
		subdata = data >> 3;
		xf->border_color[STYLE_BOTTOM] = (subdata==24) ? 64 : subdata;
		data = GSF_LE_GET_GUINT8 (q->data + 8);
		xf->border_type[STYLE_TOP] = biff_xf_map_border(data & 0x07);
		subdata = data >> 3;
		xf->border_color[STYLE_TOP] = (subdata==24) ? 64 : subdata;
		data = GSF_LE_GET_GUINT8 (q->data + 9);
		xf->border_type[STYLE_LEFT] = biff_xf_map_border(data & 0x07);
		subdata = data >> 3;
		xf->border_color[STYLE_LEFT] = (subdata==24) ? 64 : subdata;
		data = GSF_LE_GET_GUINT8 (q->data + 11);
		xf->border_type[STYLE_RIGHT] = biff_xf_map_border (data & 0x07);
		subdata = data >> 3;
		xf->border_color[STYLE_RIGHT] = (subdata==24) ? 64 : subdata;
	} else /* MS_BIFF_V2 */ {
		xf->pat_foregnd_col = 0;
		xf->pat_backgnd_col = 1;

		data = q->data[3];
		xf->border_type[STYLE_LEFT]	= (data & 0x08) ? 1 : 0;
		xf->border_color[STYLE_LEFT]	= 0;
		xf->border_type[STYLE_RIGHT]	= (data & 0x10) ? 1: 0;
		xf->border_color[STYLE_RIGHT]	= 0;
		xf->border_type[STYLE_TOP]	= (data & 0x20) ? 1: 0;
		xf->border_color[STYLE_TOP]	= 0;
		xf->border_type[STYLE_BOTTOM]	= (data & 0x40) ? 1: 0;
		xf->border_color[STYLE_BOTTOM]	= 0;
		xf->fill_pattern_idx = (data & 0x80) ? 5: 0;
	}

        xf->border_type[STYLE_DIAGONAL] = 0;
        xf->border_color[STYLE_DIAGONAL] = 0;
        xf->border_type[STYLE_REV_DIAGONAL] = 0;
        xf->border_color[STYLE_REV_DIAGONAL] = 0;

        /* Init the cache */
        xf->mstyle = NULL;

        g_ptr_array_add (importer->XF_cell_records, xf);
}

static void
excel_read_XF (BiffQuery *q, GnmXLImporter *importer)
{
	BiffXFData *xf;
	guint32 data, subdata;

	XL_CHECK_CONDITION (q->length >= 8);  /* Check this */

	xf = g_new (BiffXFData, 1);
	xf->font_idx = GSF_LE_GET_GUINT16 (q->data);
	xf->format_idx = GSF_LE_GET_GUINT16 (q->data + 2);
	xf->style_format = (xf->format_idx > 0)
		? excel_wb_get_fmt (importer, xf->format_idx) : NULL;
	xf->is_simple_format = xf->style_format == NULL ||
		go_format_is_simple (xf->style_format);

	data = GSF_LE_GET_GUINT16 (q->data + 4);
	xf->locked = (data & 0x0001) != 0;
	xf->hidden = (data & 0x0002) != 0;
	xf->xftype = (data & 0x0004) ? MS_BIFF_X_STYLE : MS_BIFF_X_CELL;
	xf->format = (data & 0x0008) ? MS_BIFF_F_LOTUS : MS_BIFF_F_MS;
	xf->parentstyle = (data & 0xfff0) >> 4;

	if (xf->xftype == MS_BIFF_X_CELL && xf->parentstyle != 0) {
		/* TODO Add support for parent styles
		 * XL implements a simple form of inheritance with styles.
		 * If a style's parent changes a value and the child has not
		 * overridden that value explicitly the child gets updated.
		 */
	}

	data = GSF_LE_GET_GUINT16 (q->data + 6);
	subdata = data & 0x0007;
	switch (subdata) {
	case 0: xf->halign = HALIGN_GENERAL; break;
	case 1: xf->halign = HALIGN_LEFT; break;
	case 2: xf->halign = HALIGN_CENTER; break;
	case 3: xf->halign = HALIGN_RIGHT; break;
	case 4: xf->halign = HALIGN_FILL; break;
	case 5: xf->halign = HALIGN_JUSTIFY; break;
	case 6:
		/*
		 * All adjacent blank cells with this type of alignment
		 * are merged into a single span.  cursor still behaves
		 * normally and the span is adjusted if contents are changed.
		 * Use center for now.
		 */
		xf->halign = HALIGN_CENTER_ACROSS_SELECTION;
		break;

	/* no idea what this does */
	case 7 : xf->halign = HALIGN_DISTRIBUTED; break;

	default:
		xf->halign = HALIGN_JUSTIFY;
		g_printerr ("Unknown halign %d\n", subdata);
		break;
	}
	xf->wrap_text = (data & 0x0008) != 0;
	subdata = (data & 0x0070) >> 4;
	switch (subdata) {
	case 0: xf->valign = VALIGN_TOP; break;
	case 1: xf->valign = VALIGN_CENTER; break;
	case 2: xf->valign = VALIGN_BOTTOM; break;
	case 3: xf->valign = VALIGN_JUSTIFY; break;
	/* What does this do ?? */
	case 4: xf->valign = VALIGN_DISTRIBUTED; break;
	default:
		g_printerr ("Unknown valign %d\n", subdata);
		break;
	}

	if (importer->ver >= MS_BIFF_V8) {
		xf->rotation = (data >> 8);
		if (xf->rotation == 0xff)
			xf->rotation = -1;
		else if (xf->rotation > 90)
			xf->rotation = 360 + 90 - xf->rotation;
	} else {
		subdata = (data & 0x0300) >> 8;
		switch (subdata) {
		case 0: xf->rotation =  0;	break;
		case 1: xf->rotation = -1;	break;
		case 2: xf->rotation = 90;	break;
		case 3: xf->rotation = 270;	break;
		}
	}

	if (importer->ver >= MS_BIFF_V8) {
		guint16 const data = GSF_LE_GET_GUINT16 (q->data + 8);

		/* FIXME: This code seems irrelevant for merging.
		 * The undocumented record MERGECELLS appears to be the correct source.
		 * Nothing seems to set the merge flags.
		 */
		/* gboolean const merge = (data & 0x20) ? TRUE : FALSE; */

		xf->indent = data & 0x0f;
		xf->shrink_to_fit = (data & 0x10) ? TRUE : FALSE;

		subdata = (data & 0x00C0) >> 10;
		switch (subdata) {
		default: g_warning ("Unknown text direction %d.", subdata);
			/* Fall through.  */
		case 0: xf->text_dir = GNM_TEXT_DIR_CONTEXT; break;
		case 1: xf->text_dir = GNM_TEXT_DIR_LTR; break;
		case 2: xf->text_dir = GNM_TEXT_DIR_RTL; break;
		}
	} else {
		xf->text_dir = GNM_TEXT_DIR_CONTEXT;
		xf->shrink_to_fit = FALSE;
		xf->indent = 0;
	}

	xf->differences = data & 0xFC00;

	if (importer->ver >= MS_BIFF_V8) { /* Very different */
		int has_diagonals, diagonal_style;
		data = GSF_LE_GET_GUINT16 (q->data + 10);
		subdata = data;
		xf->border_type[STYLE_LEFT] = biff_xf_map_border (subdata & 0xf);
		subdata = subdata >> 4;
		xf->border_type[STYLE_RIGHT] = biff_xf_map_border (subdata & 0xf);
		subdata = subdata >> 4;
		xf->border_type[STYLE_TOP] = biff_xf_map_border (subdata & 0xf);
		subdata = subdata >> 4;
		xf->border_type[STYLE_BOTTOM] = biff_xf_map_border (subdata & 0xf);
		subdata = subdata >> 4;

		data = GSF_LE_GET_GUINT16 (q->data + 12);
		subdata = data;
		xf->border_color[STYLE_LEFT] = (subdata & 0x7f);
		subdata = subdata >> 7;
		xf->border_color[STYLE_RIGHT] = (subdata & 0x7f);
		subdata = (data & 0xc000) >> 14;
		has_diagonals = subdata & 0x3;

		data = GSF_LE_GET_GUINT32 (q->data + 14);
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
			?  diagonal_style : GNM_STYLE_BORDER_NONE;
		xf->border_type[STYLE_REV_DIAGONAL] = (has_diagonals & 0x1)
			?  diagonal_style : GNM_STYLE_BORDER_NONE;

		xf->fill_pattern_idx =
			excel_map_pattern_index_from_excel ((data>>26) & 0x3f);

		data = GSF_LE_GET_GUINT16 (q->data + 18);
		xf->pat_foregnd_col = (data & 0x007f);
		xf->pat_backgnd_col = (data & 0x3f80) >> 7;

		d (2, fprintf (stderr,"Color f=0x%x b=0x%x pat=0x%x\n",
			      xf->pat_foregnd_col,
			      xf->pat_backgnd_col,
			      xf->fill_pattern_idx););

	} else { /* Biff 7 */
		data = GSF_LE_GET_GUINT16 (q->data + 8);
		xf->pat_foregnd_col = (data & 0x007f);
		/* Documentation is wrong, background color is one bit more
		 * than documented */
		xf->pat_backgnd_col = (data & 0x3f80) >> 7;

		data = GSF_LE_GET_GUINT16 (q->data + 10);
		xf->fill_pattern_idx =
			excel_map_pattern_index_from_excel (data & 0x3f);

		d (2, fprintf (stderr,"Color f=0x%x b=0x%x pat=0x%x\n",
			      xf->pat_foregnd_col,
			      xf->pat_backgnd_col,
			      xf->fill_pattern_idx););

		/* Luckily this maps nicely onto the new set. */
		xf->border_type[STYLE_BOTTOM] = biff_xf_map_border ((data & 0x1c0) >> 6);
		xf->border_color[STYLE_BOTTOM] = (data & 0xfe00) >> 9;

		data = GSF_LE_GET_GUINT16 (q->data + 12);
		subdata = data;
		xf->border_type[STYLE_TOP] = biff_xf_map_border (subdata & 0x07);
		subdata = subdata >> 3;
		xf->border_type[STYLE_LEFT] = biff_xf_map_border (subdata & 0x07);
		subdata = subdata >> 3;
		xf->border_type[STYLE_RIGHT] = biff_xf_map_border (subdata & 0x07);

		subdata = subdata >> 3;
		xf->border_color[STYLE_TOP] = subdata;

		data = GSF_LE_GET_GUINT16 (q->data + 14);
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

	g_ptr_array_add (importer->XF_cell_records, xf);
	d (2, g_printerr ("XF(0x%04x): S=%d L=%d H=%d L=%d xty=%d Font=%d Fmt=%d Fg=%d Bg=%d Pat=%d\n",
			  importer->XF_cell_records->len - 1,
			  xf->is_simple_format, xf->locked, xf->hidden, xf->format,
			  xf->xftype,
			  xf->font_idx,
			  xf->format_idx,
			  xf->pat_foregnd_col,
			  xf->pat_backgnd_col,
			  xf->fill_pattern_idx););
}

static void
excel_read_XF_INDEX (BiffQuery *q, ExcelReadSheet *esheet)
{
	XL_CHECK_CONDITION (q->length >= 2);
	esheet->biff2_prev_xf_index = GSF_LE_GET_GUINT16 (q->data);
}

static void
biff_xf_data_destroy (BiffXFData *xf)
{
	if (xf->style_format) {
		go_format_unref (xf->style_format);
		xf->style_format = NULL;
	}
	if (xf->mstyle) {
		gnm_style_unref (xf->mstyle);
		xf->mstyle = NULL;
	}
	g_free (xf);
}

static GnmExprTop const *
excel_formula_shared (BiffQuery *q, ExcelReadSheet *esheet, GnmCell *cell)
{
	guint16 opcode, data_len, data_ofs, array_data_len;
	GnmRange r;
	gboolean is_array;
	GnmExprTop const *texpr;
	guint8 const *data;
	XLSharedFormula *sf;

	if (!ms_biff_query_peek_next (q, &opcode) ||
	    !(opcode == BIFF_SHRFMLA ||
	      opcode == BIFF_ARRAY_v0 || opcode == BIFF_ARRAY_v2 ||
	      opcode == BIFF_TABLE_v0 || opcode == BIFF_TABLE_v2)) {
		g_warning ("EXCEL: unexpected record '0x%x' after a formula in '%s'.",
			   opcode, cell_name (cell));
		return NULL;
	}
	ms_biff_query_next (q);

	XL_CHECK_CONDITION_VAL (q->length >= 6, NULL);
	r.start.row	= GSF_LE_GET_GUINT16 (q->data + 0);
	r.end.row	= GSF_LE_GET_GUINT16 (q->data + 2);
	r.start.col	= GSF_LE_GET_GUINT8 (q->data + 4);
	r.end.col	= GSF_LE_GET_GUINT8 (q->data + 5);

	if (opcode == BIFF_TABLE_v0 || opcode == BIFF_TABLE_v2) {
		XLDataTable *dt = g_new0 (XLDataTable, 1);
		GnmExprList *args = NULL;
		GnmCellRef   ref;
		guint16 const flags = GSF_LE_GET_GUINT16 (q->data + 6);

		d (2, { range_dump (&r, " <-- contains data table\n");
		   gsf_mem_dump (q->data, q->length); });

		dt->table = r;
		dt->c_in.row = GSF_LE_GET_GUINT16 (q->data + 8);
		dt->c_in.col = GSF_LE_GET_GUINT16 (q->data + 10);
		dt->r_in.row = GSF_LE_GET_GUINT16 (q->data + 12);
		dt->r_in.col = GSF_LE_GET_GUINT16 (q->data + 14);
		g_hash_table_insert (esheet->tables, &dt->table.start, dt);

		args = gnm_expr_list_append (args, gnm_expr_new_cellref (
			gnm_cellref_init (&ref, NULL,
				dt->c_in.col - r.start.col,
				dt->c_in.row - r.start.row, TRUE)));
		if (flags & 0x8) {
			args = gnm_expr_list_append (args, gnm_expr_new_cellref (
				gnm_cellref_init (&ref, NULL,
					dt->r_in.col - r.start.col,
					dt->r_in.row - r.start.row, TRUE)));
		} else {
			GnmExpr	const *missing = gnm_expr_new_constant (value_new_empty ());
			args = (flags & 4) ? gnm_expr_list_append (args, missing)
					   : gnm_expr_list_prepend (args, missing);
		}
		texpr = gnm_expr_top_new (gnm_expr_new_funcall (gnm_func_lookup ("table", NULL), args));
		gnm_cell_set_array_formula (esheet->sheet,
			r.start.col, r.start.row,
			r.end.col,   r.end.row,
			texpr);
		return NULL;
	}

	d (2, range_dump (&r, " <-- contains a shared formula\n"););

	is_array = (q->opcode != BIFF_SHRFMLA);

	data_ofs = (esheet_ver (esheet) > MS_BIFF_V4 && is_array) ? 14 : 10;
	XL_CHECK_CONDITION_VAL (q->length >= data_ofs, NULL);
	data = q->data + data_ofs;
	data_len = GSF_LE_GET_GUINT16 (q->data + (data_ofs - 2));
	XL_CHECK_CONDITION_VAL (q->length >= data_ofs + data_len, NULL);
	array_data_len = data_len > 0 ? q->length - (data_ofs + data_len) : 0;

	texpr = excel_parse_formula (&esheet->container, esheet,
				     r.start.col, r.start.row,
				     data, data_len, array_data_len,
				     !is_array, NULL);

	sf = g_new (XLSharedFormula, 1);

	/* WARNING: Do NOT use the upper left corner as the hashkey.
	 *     For some bizzare reason XL appears to sometimes not
	 *     flag the formula as shared until later.
	 *  Use the location of the cell we are reading as the key.
	 */
	sf->key = cell->pos;
	sf->is_array = is_array;
	sf->data = data_len > 0 ? g_memdup (data, data_len + array_data_len) : NULL;
	sf->data_len = data_len;
	sf->array_data_len = array_data_len;

	d (1, fprintf (stderr,"Shared formula, extent %s\n", range_as_string (&r)););

	g_hash_table_insert (esheet->shared_formulae, &sf->key, sf);

	g_return_val_if_fail (texpr != NULL, NULL);

	if (is_array) {
		gnm_cell_set_array_formula (esheet->sheet,
					    r.start.col, r.start.row,
					    r.end.col,   r.end.row,
					    texpr);
		return NULL;
	} else
		return texpr;
}

/*
 * NOTE: There must be _no_ path through this function that does not set the
 *	cell value.
 */
static void
excel_read_FORMULA (BiffQuery *q, ExcelReadSheet *esheet)
{
	/* Pre-retrieve incase this is a string */
	gboolean array_elem, is_string = FALSE;
	guint16 col, row, options;
	guint16 expr_length;
	guint offset;
	guint8 const *val_dat = q->data + 6;
	GnmExprTop const *texpr;
	GnmCell	 *cell;
	GnmValue *val = NULL;

	XL_CHECK_CONDITION (q->length >= 16);
	col = XL_GETCOL (q);
	row = XL_GETROW (q);
	options = GSF_LE_GET_GUINT16 (q->data + 14);

	excel_set_xf (esheet, q);

	cell = excel_cell_fetch (q, esheet);
	if (!cell)
		return;

	/* TODO : it would be nice to figure out how to allocate recalc tags.
	 * that would avoid the scary
	 *	'this file was calculated with a different version of XL'
	 * warning when exiting without changing. */
	d (1, fprintf (stderr,"Formula in %s!%s has recalc tag 0x%x;\n",
		      esheet->sheet->name_quoted, cell_name (cell),
		      GSF_LE_GET_GUINT32 (q->data + 16)););

	/* TODO TODO TODO: Wishlist
	 * We should make an array of minimum sizes for each BIFF type
	 * and have this checking done there.
	 */
	if (esheet_ver (esheet) >= MS_BIFF_V5) {
		XL_CHECK_CONDITION (q->length >= 22);
		expr_length = GSF_LE_GET_GUINT16 (q->data + 20);
		offset = 22;
	} else if (esheet_ver (esheet) >= MS_BIFF_V3) {
		XL_CHECK_CONDITION (q->length >= 18);
		expr_length = GSF_LE_GET_GUINT16 (q->data + 16);
		offset = 18;
	} else {
		XL_CHECK_CONDITION (q->length >= 17);
		expr_length = GSF_LE_GET_GUINT8 (q->data + 16);
		offset = 17;
		val_dat++;	/* compensate for the 3 byte style */
	}
	XL_CHECK_CONDITION (q->length >= offset + expr_length);

	/* Get the current value so that we can format, do this BEFORE handling
	 * shared/array formulas or strings in case we need to go to the next
	 * record */
	if (GSF_LE_GET_GUINT16 (val_dat + 6) != 0xffff) {
		val = value_new_float (gsf_le_get_double (val_dat));
	} else {
		guint8 const val_type = GSF_LE_GET_GUINT8 (val_dat);
		switch (val_type) {
		case 0: is_string = TRUE; break;
		case 1: val = value_new_bool (GSF_LE_GET_GUINT8 (val_dat + 2) != 0);
			break;
		case 2: val = biff_get_error (NULL, GSF_LE_GET_GUINT8 (val_dat + 2));
			break;
		case 3: val = value_new_empty (); /* Empty (Undocumented) */
			break;
		default:
			g_printerr ("Unknown type (%x) for cell's (%s) current val\n",
				val_type, cell_name (cell));
		}
	}

	texpr = excel_parse_formula (&esheet->container, esheet, col, row,
				     q->data + offset, expr_length,
				     q->length - (offset + expr_length),
				     FALSE, &array_elem);
#if 0
	/* dump the trailing array and natural language data */
	gsf_mem_dump (q->data + offset + expr_length,
		      q->length - offset - expr_length);
#endif

	/* Error was flaged by parse_formula */
	if (texpr == NULL && !array_elem)
		texpr = excel_formula_shared (q, esheet, cell);

	if (is_string) {
		guint16 opcode;
		if (ms_biff_query_peek_next (q, &opcode) &&
		    (opcode == BIFF_STRING_v0 || opcode == BIFF_STRING_v2)) {
			char *v = NULL;
			if (ms_biff_query_next (q)) {
				/*
				 * NOTE: the Excel developers kit docs are
				 *       WRONG.  There is an article that
				 *       clarifies the behaviour to be the std
				 *       unicode format rather than the pure
				 *       length version the docs describe.
				 *
				 * NOTE : Apparently some apps actually store a
				 *        0 length string record for an empty.
				 *        DAMN! this was us!  we were screwing
				 *        up when exporting ""
				 */
				guint16 const len = (q->length >= 2) ? GSF_LE_GET_GUINT16 (q->data) : 0;

				if (len > 0)
					v = excel_biff_text (esheet->container.importer, q, 2, len);
				else
					/*
					 * Pre-Biff8 seems to use len=0
					 * Should that be a string or an EMPTY?
					 */
					v = g_strdup ("");
			}
			if (v) {
				val = value_new_string_nocopy (v);
			} else {
				GnmEvalPos ep;
				val = value_new_error (eval_pos_init_cell (&ep, cell),
					"INVALID STRING");
				g_warning ("EXCEL: invalid STRING record in %s",
					cell_name (cell));
			}
		} else {
			/* There should be a STRING record here */
			GnmEvalPos ep;
			val = value_new_error (eval_pos_init_cell (&ep, cell),
				"MISSING STRING");
			g_warning ("EXCEL: missing STRING record for %s",
				cell_name (cell));
		}
	}

	/* We MUST have a value */
	if (val == NULL) {
		GnmEvalPos ep;
		val = value_new_error (eval_pos_init_cell (&ep, cell),
			"MISSING Value");
		g_warning ("EXCEL: Invalid state.  Missing Value in %s?",
			cell_name (cell));
	}

	if (gnm_cell_is_array (cell)) {
		/* Array expressions were already stored in the cells (without
		 * recalc), and without a value.  Handle either the first
		 * instance or the followers.
		 */
		gnm_cell_assign_value (cell, val);
	} else if (!gnm_cell_has_expr (cell)) {
		/* Just in case things screwed up, at least save the value */
		if (texpr != NULL) {
			gnm_cell_set_expr_and_value (cell, texpr, val, TRUE);
			gnm_expr_top_unref (texpr);
		} else
			gnm_cell_assign_value (cell, val);
	} else {
		/*
		 * NOTE: Only the expression is screwed.
		 * The value and format can still be set.
		 */
		g_warning ("EXCEL: Multiple expressions for cell %s!%s",
			   esheet->sheet->name_quoted, cell_name (cell));
		gnm_cell_set_value (cell, val);
		if (texpr)
			gnm_expr_top_unref (texpr);
	}

	/*
	 * 0x1 = AlwaysCalc
	 * 0x2 = CalcOnLoad
	 */
	if (options & 0x3)
		cell_queue_recalc (cell);
}

XLSharedFormula *
excel_sheet_shared_formula (ExcelReadSheet const *esheet,
			    GnmCellPos const    *key)
{
	g_return_val_if_fail (esheet != NULL, NULL);

	d (5, fprintf (stderr,"FIND SHARED: %s\n", cellpos_as_string (key)););

	return g_hash_table_lookup (esheet->shared_formulae, key);
}

XLDataTable *
excel_sheet_data_table (ExcelReadSheet const *esheet,
			GnmCellPos const    *key)
{
	g_return_val_if_fail (esheet != NULL, NULL);

	d (5, fprintf (stderr,"FIND DATA TABLE: %s\n", cellpos_as_string (key)););

	return g_hash_table_lookup (esheet->tables, key);
}

static void
excel_sheet_insert_val (ExcelReadSheet *esheet, BiffQuery *q,
			GnmValue *v)
{
	GnmCell *cell = excel_cell_fetch (q, esheet);

	if (cell) {
		BiffXFData const *xf = excel_set_xf (esheet, q);

		if (xf != NULL && xf->is_simple_format &&
		    VALUE_FMT (v) == NULL &&
		    !VALUE_IS_EMPTY (v) &&  /* We cannot set formats for */
		    !VALUE_IS_BOOLEAN (v)) /* singetons. */
			value_set_fmt (v, xf->style_format);
		gnm_cell_set_value (cell, v);
	} else
		value_release (v);
}

static void
excel_read_NOTE (BiffQuery *q, ExcelReadSheet *esheet)
{
	GnmCellPos pos;
	Sheet *sheet = esheet->sheet;
	unsigned row, col;

	XL_CHECK_CONDITION (q->length >= 4);

	row = XL_GETROW (q);
	col = XL_GETCOL (q);
	XL_CHECK_CONDITION (col < gnm_sheet_get_max_cols (sheet));
	XL_CHECK_CONDITION (row < gnm_sheet_get_max_rows (sheet));
	pos.row = XL_GETROW (q);
	pos.col = XL_GETCOL (q);

	if (esheet_ver (esheet) >= MS_BIFF_V8) {
		guint16 options, obj_id;
		gboolean hidden;
		MSObj *obj;
		char *author;

		XL_CHECK_CONDITION (q->length >= 8);
		options = GSF_LE_GET_GUINT16 (q->data + 4);
		hidden = (options & 0x2)==0;
		obj_id = GSF_LE_GET_GUINT16 (q->data + 6);

		/* Docs claim that only 0x2 is valid, all other flags should
		 * be 0 but we have seen examples with 0x100 'pusiuhendused juuli 2003.xls'
		 *
		 * docs mention   0x002 == hidden
		 * real life adds 0x080 == ???
		 * real life adds 0x100 == no indicator visible ??? */
		if (options & 0xe7d)
			g_warning ("unknown flag on NOTE record %hx", options);

		author = excel_biff_text_2 (esheet->container.importer, q, 8);
		d (1, fprintf (stderr,"Comment at %s%d id %d options"
			      " 0x%x hidden %d by '%s'\n",
			      col_name (pos.col), pos.row + 1,
			      obj_id, options, hidden, author););

		obj = ms_container_get_obj (&esheet->container, obj_id);
		if (obj != NULL) {
			cell_comment_author_set (CELL_COMMENT (obj->gnum_obj), author);
			obj->comment_pos = pos;
		} else {
			/* hmm, how did this happen ? we should have seen
			 * some escher records earlier */
			cell_set_comment (sheet, &pos, author, NULL);
		}
		g_free (author);
	} else {
		guint len;
		GString *comment;

		XL_CHECK_CONDITION (q->length >= 6);
		len = GSF_LE_GET_GUINT16 (q->data + 4);
		comment = g_string_sized_new (len);

		for (; len > 2048 ; len -= 2048) {
			guint16 opcode;

			g_string_append (comment,
					 excel_biff_text (esheet->container.importer,
							  q, 6, 2048));

			if (!ms_biff_query_peek_next (q, &opcode) ||
			    opcode != BIFF_NOTE || !ms_biff_query_next (q) ||
			    q->length < 4 ||
			    XL_GETROW (q) != 0xffff || XL_GETCOL (q) != 0) {
				g_warning ("Invalid Comment record");
				g_string_free (comment, TRUE);
				return;
			}
		}
		g_string_append (comment, excel_biff_text (esheet->container.importer, q, 6, len));

		d (2, fprintf (stderr,"Comment in %s%d: '%s'\n",
			      col_name (pos.col), pos.row + 1, comment->str););

		cell_set_comment (sheet, &pos, NULL, comment->str);
		g_string_free (comment, TRUE);
	}
}

static void
excel_sheet_destroy (ExcelReadSheet *esheet)
{
	if (esheet == NULL)
		return;
	if (esheet->shared_formulae != NULL) {
		g_hash_table_destroy (esheet->shared_formulae);
		esheet->shared_formulae = NULL;
	}
	if (esheet->tables != NULL) {
		g_hash_table_destroy (esheet->tables);
		esheet->tables = NULL;
	}

	/* There appear to be workbooks like guai.xls that have a filter NAME
	 * defined but no visible combos, so we remove a filter if it has no
	 * objects */
	if (esheet->filter != NULL) {
		gnm_filter_remove (esheet->filter);
		gnm_filter_free (esheet->filter);
		esheet->filter = NULL;
	}

	ms_container_finalize (&esheet->container);

	g_free (esheet);
}

static GnmExprTop const *
ms_wb_parse_expr (MSContainer *container, guint8 const *data, int length)
{
	ExcelReadSheet dummy_sheet;

	dummy_sheet.container.importer = (GnmXLImporter *)container;
	dummy_sheet.sheet = NULL;
	dummy_sheet.shared_formulae = NULL;
	dummy_sheet.tables = NULL;
	return ms_sheet_parse_expr_internal (&dummy_sheet, data, length);
}

static GOFormat *
ms_wb_get_fmt (MSContainer const *container, unsigned indx)
{
	return excel_wb_get_fmt (((GnmXLImporter *)container), indx);
}

static void
add_attr (PangoAttrList  *attr_list, PangoAttribute *attr)
{
	attr->start_index = 0;
	attr->end_index = 0;
	pango_attr_list_insert (attr_list, attr);
}

static PangoAttrList *
ms_wb_get_font_markup (MSContainer const *c, unsigned indx)
{
	GnmXLImporter *importer = (GnmXLImporter *)c;
	ExcelFont const *fd = excel_font_get (importer, indx);
	GnmColor *color;

	if (fd == NULL) { /* random fallback */
		fd = excel_font_get (importer, 0);
		if (NULL == fd)
			return NULL;
	}

	if (fd->attrs == NULL) {
		PangoAttrList *attrs;
		PangoUnderline underline = PANGO_UNDERLINE_NONE;

		switch (fd->underline) {
		case XLS_ULINE_SINGLE:
		case XLS_ULINE_SINGLE_ACC:
			underline = PANGO_UNDERLINE_SINGLE;
			break;

		case XLS_ULINE_DOUBLE:
		case XLS_ULINE_DOUBLE_ACC:
			underline = PANGO_UNDERLINE_DOUBLE;
			break;

		default: break;
		}

		attrs = pango_attr_list_new ();
		add_attr (attrs, pango_attr_family_new (fd->fontname));
		add_attr (attrs, pango_attr_size_new (fd->height * PANGO_SCALE / 20));
		add_attr (attrs, pango_attr_weight_new (fd->boldness));
		add_attr (attrs, pango_attr_style_new (fd->italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL));
		add_attr (attrs, pango_attr_strikethrough_new (fd->struck_out));
		add_attr (attrs, pango_attr_underline_new (underline));
		add_attr (attrs, pango_attr_rise_new (5000 * fd->script));
		if (fd->script != GO_FONT_SCRIPT_STANDARD)
			add_attr (attrs, pango_attr_scale_new (.5));

		color = (fd->color_idx == 127) ? style_color_black ()
			: excel_palette_get (importer, fd->color_idx);
		add_attr (attrs, pango_attr_foreground_new (
			color->gdk_color.red, color->gdk_color.green, color->gdk_color.blue));
		style_color_unref (color);

		((ExcelFont *)fd)->attrs = attrs;
	}

	return fd->attrs;
}

static GnmXLImporter *
gnm_xl_importer_new (IOContext *context, WorkbookView *wb_view)
{
	static MSContainerClass const vtbl = {
		NULL, NULL,
		&ms_wb_parse_expr,
		NULL,
		&ms_wb_get_fmt,
		&ms_wb_get_font_markup
	};

	GnmXLImporter *importer = g_new (GnmXLImporter, 1);

	importer->ver	  = MS_BIFF_V_UNKNOWN;
	importer->context = context;
	importer->wbv     = wb_view;
	importer->wb      = wb_view_get_workbook (wb_view);
	importer->str_iconv = (GIConv)(-1);
	gnm_xl_importer_set_codepage (importer, 1252); /* set a default */

	importer->expr_sharer = gnm_expr_sharer_new ();
	importer->v8.supbook     = g_array_new (FALSE, FALSE, sizeof (ExcelSupBook));
	importer->v8.externsheet = NULL;

	importer->names = g_ptr_array_new ();
	importer->num_name_records = 0;

	importer->boundsheet_sheet_by_index = g_ptr_array_new ();
	importer->boundsheet_data_by_stream = g_hash_table_new_full (
		g_direct_hash, g_direct_equal,
		NULL, (GDestroyNotify) biff_boundsheet_data_destroy);
	importer->font_data        = g_hash_table_new_full (
		g_direct_hash, g_direct_equal,
		NULL, (GDestroyNotify)excel_font_free);
	importer->excel_sheets     = g_ptr_array_new ();
	importer->XF_cell_records  = g_ptr_array_new ();
	importer->format_table      = g_hash_table_new_full (
		g_direct_hash, g_direct_equal,
		NULL, (GDestroyNotify)biff_format_data_destroy);
	importer->palette = NULL;
	importer->sst     = NULL;
	importer->sst_len = 0;

	ms_container_init (&importer->container, &vtbl, NULL, importer);
	return importer;
}

static void
excel_workbook_reset_style (GnmXLImporter *importer)
{
	unsigned i;

	g_hash_table_destroy (importer->font_data);
        importer->font_data        = g_hash_table_new_full (
		g_direct_hash, g_direct_equal,
                NULL, (GDestroyNotify)excel_font_free);

        for (i = 0; i < importer->XF_cell_records->len; i++)
                biff_xf_data_destroy (g_ptr_array_index (importer->XF_cell_records, i));
        g_ptr_array_free (importer->XF_cell_records, TRUE);
        importer->XF_cell_records  = g_ptr_array_new ();

	g_hash_table_destroy (importer->format_table);
        importer->format_table      = g_hash_table_new_full (
		g_direct_hash, g_direct_equal,
                NULL, (GDestroyNotify)biff_format_data_destroy);
}

static void
gnm_xl_importer_free (GnmXLImporter *importer)
{
	unsigned i, j;
	GSList *real_order = NULL;
	Sheet *sheet;
	GnmNamedExpr *nexpr;

	for (i = importer->boundsheet_sheet_by_index->len; i-- > 0 ; ) {
		sheet = g_ptr_array_index (importer->boundsheet_sheet_by_index, i);
		if (sheet != NULL)
			real_order = g_slist_prepend (real_order, sheet);
	}

	if (real_order != NULL) {
		workbook_sheet_reorder (importer->wb, real_order);
		g_slist_free (real_order);
	}

	gnm_expr_sharer_destroy (importer->expr_sharer);

	g_hash_table_destroy (importer->boundsheet_data_by_stream);
	importer->boundsheet_data_by_stream = NULL;
	g_ptr_array_free (importer->boundsheet_sheet_by_index, TRUE);
	importer->boundsheet_sheet_by_index = NULL;

	for (i = 0; i < importer->excel_sheets->len; i++)
		excel_sheet_destroy (g_ptr_array_index (importer->excel_sheets, i));
	g_ptr_array_free (importer->excel_sheets, TRUE);
	importer->excel_sheets = NULL;

	for (i = 0; i < importer->XF_cell_records->len; i++)
		biff_xf_data_destroy (g_ptr_array_index (importer->XF_cell_records, i));
	g_ptr_array_free (importer->XF_cell_records, TRUE);
	importer->XF_cell_records = NULL;

	g_hash_table_destroy (importer->font_data);
	importer->font_data = NULL;

	g_hash_table_destroy (importer->format_table);
	importer->format_table = NULL;

	if (importer->palette) {
		excel_palette_destroy (importer->palette);
		importer->palette = NULL;
	}

	for (i = 0; i < importer->v8.supbook->len; i++ ) {
		ExcelSupBook *sup = &(g_array_index (importer->v8.supbook,
						     ExcelSupBook, i));
		for (j = 0; j < sup->externname->len; j++ )
			expr_name_unref (g_ptr_array_index (sup->externname, j));
		g_ptr_array_free (sup->externname, TRUE);
	}
	g_array_free (importer->v8.supbook, TRUE);
	importer->v8.supbook = NULL;
	if (importer->v8.externsheet != NULL) {
		g_array_free (importer->v8.externsheet, TRUE);
		importer->v8.externsheet = NULL;
	}

	if (importer->sst != NULL) {
		unsigned i = importer->sst_len;
		while (i-- > 0) {
			if (importer->sst[i].content)
				gnm_string_unref (importer->sst[i].content);
			if (importer->sst[i].markup != NULL)
				go_format_unref (importer->sst[i].markup);
		}
		g_free (importer->sst);
	}

	for (i = importer->names->len; i-- > 0 ; )
		if (NULL != (nexpr = g_ptr_array_index (importer->names, i))) {
			/* NAME placeholders need removal, EXTERNNAME
			 * placeholders will no be active */
			if (nexpr->active && nexpr->is_placeholder && nexpr->ref_count == 2)
				expr_name_remove (nexpr);
			expr_name_unref (nexpr);
		}
	g_ptr_array_free (importer->names, TRUE);
	importer->names = NULL;

	if (importer->str_iconv != (GIConv)(-1)) {
		gsf_iconv_close (importer->str_iconv);
		importer->str_iconv = (GIConv)(-1);
	}

	ms_container_finalize (&importer->container);
	g_free (importer);
}

/**
 * Unpacks a MS Excel RK structure,
 **/
static GnmValue *
biff_get_rk (guint8 const *ptr)
{
	gint32 number;
	enum eType {
		eIEEE = 0, eIEEEx100 = 1, eInt = 2, eIntx100 = 3
	} type;

	number = GSF_LE_GET_GUINT32 (ptr);
	type = (number & 0x3);
	switch (type) {
	case eIEEE:
	case eIEEEx100:
	{
		guint8 tmp[8];
		gnm_float answer;
		int lp;

		/* Think carefully about big/little endian issues before
		   changing this code.  */
		for (lp = 0; lp < 4; lp++) {
			tmp[lp + 4]= (lp > 0) ? ptr[lp]: (ptr[lp] & 0xfc);
			tmp[lp] = 0;
		}

		answer = (gnm_float)gsf_le_get_double (tmp);
		return value_new_float (type == eIEEEx100 ? answer / 100 : answer);
	}
	case eInt:
		return value_new_int (number >> 2);
	case eIntx100:
		number >>= 2;
		if ((number % 100) == 0)
			return value_new_int (number / 100);
		else
			return value_new_float ((gnm_float)number / 100);
	}
	while (1) abort ();
}

static char const *
excel_builtin_name (guint8 const *ptr)
{
	switch (*ptr) {
	case 0x00: return "Consolidate_Area";
	case 0x01: return "Auto_Open";
	case 0x02: return "Auto_Close";
	case 0x03: return "Extract";
	case 0x04: return "Database";
	case 0x05: return "Criteria";
	case 0x06: return "Print_Area";
	case 0x07: return "Print_Titles";
	case 0x08: return "Recorder";
	case 0x09: return "Data_Form";
	case 0x0A: return "Auto_Activate";
	case 0x0B: return "Auto_Deactivate";
	case 0x0C: return "Sheet_Title";
	case 0x0D: return "_FilterDatabase";

	default:
		   g_warning ("Unknown builtin named expression %d", (int)*ptr);
	}
	return NULL;
}

static GnmNamedExpr *
excel_parse_name (GnmXLImporter *importer, Sheet *sheet, char *name,
		  guint8 const *expr_data, unsigned expr_len,
		  gboolean link_to_container,
		  GnmNamedExpr *stub)
{
	GnmParsePos pp;
	GnmNamedExpr *nexpr;
	GnmExprTop const *texpr = NULL;
	char *err = NULL;

	g_return_val_if_fail (name != NULL, NULL);

	/* expr_len == 0 seems to indicate a placeholder for an unknown name */
	if (expr_len != 0) {

		texpr = excel_parse_formula (&importer->container, NULL, 0, 0,
					     expr_data, expr_len, 0 /* FIXME? */,
					     TRUE, NULL);

		if (texpr == NULL) {
			gnm_io_warning (importer->context, _("Failure parsing name '%s'"), name);
			texpr = gnm_expr_top_new_constant (value_new_error_REF (NULL));
		} else d (2, {
			char *tmp;
			GnmParsePos pp;

			tmp = gnm_expr_top_as_string (texpr,
						      parse_pos_init (&pp, importer->wb, NULL, 0, 0),
						      gnm_conventions_default);
			g_printerr ( "%s\n", tmp);
			g_free (tmp);
		});
	}

	parse_pos_init (&pp, importer->wb, sheet, 0, 0);
	nexpr = expr_name_add (&pp, name,
			       texpr,
			       &err, link_to_container, stub);
	g_free (name);
	if (nexpr == NULL) {
		gnm_io_warning (importer->context, err);
		g_free (err);
		return NULL;
	}

	return nexpr;
}

static char *
excel_read_name_str (GnmXLImporter *importer,
		     guint8 const *data, unsigned *name_len, gboolean is_builtin)
{
	gboolean use_utf16, has_extended;
	unsigned trailing_data_len, n_markup;
	char *name = NULL;

	/* Lovely, they put suffixes on builtins.  Then those !#$^
	 * dipsticks  put the unicode header _before_ the builtin id
	 * and stored the id as a character (possibly two byte).
	 * NOTE : len is in _characters_ (not bytes) does not include
	 *	the header */
	if (is_builtin) {
		guint8 const *str = data;
		char const *builtin;

		if (importer->ver < MS_BIFF_V8) {
			use_utf16 = has_extended = FALSE;
			n_markup = trailing_data_len = 0;
		} else
			str += excel_read_string_header
				(str, G_MAXINT /* FIXME */,
				 &use_utf16, &n_markup, &has_extended,
				 &trailing_data_len);

		/* pull out the magic builtin enum */
		builtin = excel_builtin_name (str);
		str += use_utf16 ? 2 : 1;
		if (--(*name_len)) {
			char *tmp = excel_get_chars (importer, str, *name_len, use_utf16);
			name = g_strconcat (builtin, tmp, NULL);
			g_free (tmp);
			*name_len = (use_utf16 ? 2 : 1) * (*name_len);
		} else
			name = g_strdup (builtin);
		*name_len += str - data;
	} else /* converts char len to byte len, and handles header */
		name = excel_get_text_fixme (importer, data, *name_len, name_len);
	return name;
}

static void
excel_read_EXTERNNAME (BiffQuery *q, MSContainer *container)
{
	MsBiffVersion const ver = container->importer->ver;
	GnmNamedExpr		*nexpr = NULL;
	char *name = NULL;

	d (2, {
	   fprintf (stderr,"EXTERNNAME\n");
	   gsf_mem_dump (q->data, q->length); });

	/* use biff version to differentiate, not the record version because
	 * the version is the same for very old and new, with _v2 used for
	 * some intermediate variants */
	if (ver >= MS_BIFF_V7) {
		guint16 flags;
		guint32 namelen;

		XL_CHECK_CONDITION (q->length >= 7);
		flags = GSF_LE_GET_GUINT8 (q->data);
		namelen = GSF_LE_GET_GUINT8 (q->data + 6);

		switch (flags & 0x18) {
		case 0x00: /* external name */
			name = excel_read_name_str (container->importer, q->data + 7, &namelen, flags&1);
			if (name != NULL) {
				unsigned expr_len = 0;
				guint8 const *expr_data = NULL;
				if (7 + 2 + namelen <= q->length) {
					unsigned el = GSF_LE_GET_GUINT16 (q->data + 7 + namelen);
					if (7 + 2 + namelen + el <= q->length) {
						expr_len = el;
						expr_data = q->data + 9 + namelen;
					} else
						gnm_io_warning (container->importer->context,
							_("Incorrect expression for name '%s': content will be lost.\n"),
							name);
				}
				nexpr = excel_parse_name (container->importer, NULL,
					name, expr_data, expr_len, FALSE, NULL);
			}
			break;

		case 0x01: /* DDE */
			gnm_io_warning (container->importer->context,
				_("DDE links are not supported.\nName '%s' will be lost.\n"),
				name);
			break;

		case 0x10: /* OLE */
			gnm_io_warning (container->importer->context,
				_("OLE links are not supported.\nName '%s' will be lost.\n"),
				name);
			break;

		default:
			g_warning ("EXCEL: Invalid external name type. ('%s')", name);
			break;
		}
	} else if (ver >= MS_BIFF_V5) {
		XL_CHECK_CONDITION (q->length >= 7);

		name = excel_biff_text_1 (container->importer, q, 6);
		nexpr = excel_parse_name (container->importer, NULL,
			name, NULL, 0, FALSE, NULL);
	} else {
		XL_CHECK_CONDITION (q->length >= 3);

		name = excel_biff_text_1 (container->importer, q, 2);
		nexpr = excel_parse_name (container->importer, NULL,
			name, NULL, 0, FALSE, NULL);
	}

	/* nexpr is potentially NULL if there was an error */
	if (ver >= MS_BIFF_V8) {
		GnmXLImporter *importer = container->importer;
		ExcelSupBook const *sup;

		g_return_if_fail (importer->v8.supbook->len > 0);

		/* The name is associated with the last SUPBOOK records seen */
		sup = &(g_array_index (importer->v8.supbook, ExcelSupBook,
				       importer->v8.supbook->len-1));
		g_ptr_array_add (sup->externname, nexpr);
	} else {
		GPtrArray *externnames = container->v7.externnames;
		if (externnames == NULL)
			externnames = container->v7.externnames = g_ptr_array_new ();
		g_ptr_array_add (externnames, nexpr);
	}
}

/* Do some error checking to handle the magic name associated with an
 * autofilter in a sheet.  Do not make it an error.
 * We have lots of examples of things that are not autofilters.
 **/
static void
excel_prepare_autofilter (GnmXLImporter *importer, GnmNamedExpr *nexpr)
{
	if (nexpr->pos.sheet != NULL) {
		GnmValue *v = gnm_expr_top_get_range (nexpr->texpr);
		if (v != NULL) {
			GnmSheetRange r;
			gboolean valid = gnm_sheet_range_from_value (&r, v);
			value_release (v);

			if (valid) {
				unsigned   i;
				GnmFilter *filter;
				ExcelReadSheet *esheet;

				filter = gnm_filter_new (r.sheet, &r.range);
				expr_name_remove (nexpr);

				for (i = 0 ; i < importer->excel_sheets->len; i++) {
					esheet = g_ptr_array_index (importer->excel_sheets, i);
					if (esheet->sheet == r.sheet) {
						g_return_if_fail (esheet->filter == NULL);
						esheet->filter = filter;
					}
				}
			}
		}
	}
}

static void
excel_read_NAME (BiffQuery *q, GnmXLImporter *importer, ExcelReadSheet *esheet)
{
	MsBiffVersion const ver = importer->ver;
	GnmNamedExpr *nexpr = NULL;
	guint16  expr_len, sheet_index, flags = 0;
	guint8 const *data;
	gboolean builtin_name = FALSE;
	char *name = NULL;
	/* length in characters (not bytes) in the same pos for all versions */
	unsigned name_len;
	/* guint8  kb_shortcut	= GSF_LE_GET_GUINT8  (q->data + 2); */
	/* int fn_grp_idx = (flags & 0xfc0)>>6; */

	XL_CHECK_CONDITION (q->length >= 4);

	name_len = GSF_LE_GET_GUINT8  (q->data + 3);

	d (2, {
	   fprintf (stderr,"NAME\n");
	   gsf_mem_dump (q->data, q->length); });

	if (ver >= MS_BIFF_V2) {
		flags = GSF_LE_GET_GUINT16 (q->data);
		builtin_name = (flags & 0x0020) != 0;
	}

	/* use biff version to differentiate, not the record version because
	 * the version is the same for very old and new, with _v2 used for
	 * some intermediate variants */
	if (ver >= MS_BIFF_V8) {
		XL_CHECK_CONDITION (q->length >= 14);
		expr_len = GSF_LE_GET_GUINT16 (q->data + 4);
		sheet_index = GSF_LE_GET_GUINT16 (q->data + 8);
		data = q->data + 14;
	} else if (ver >= MS_BIFF_V7) {
		XL_CHECK_CONDITION (q->length >= 14);
		expr_len = GSF_LE_GET_GUINT16 (q->data + 4);
		/* opencalc docs claim 8 is the right one, XL docs say 6 == 8
		 * pivot.xls suggests that at least for local builtin names 6
		 * is correct and 8 is bogus for == biff7 */
		sheet_index = GSF_LE_GET_GUINT16 (q->data + 6);
		data = q->data + 14;
	} else if (ver >= MS_BIFF_V3) {
		XL_CHECK_CONDITION (q->length >= 6);
		expr_len = GSF_LE_GET_GUINT16 (q->data + 4);
		data = q->data + 6;
		sheet_index = 0; /* no sheets */
	} else {
		XL_CHECK_CONDITION (q->length >= 5);
		expr_len = GSF_LE_GET_GUINT8 (q->data + 4);
		data = q->data + 5;
		sheet_index = 0; /* no sheets */
	}

	name = excel_read_name_str (importer, data, &name_len, builtin_name);
	XL_NEED_BYTES (name_len);
	data += name_len;

	if (name != NULL) {
		Sheet *sheet = NULL;
		d (1, fprintf (stderr, "NAME : %s, sheet_index = %hu", name, sheet_index););
		if (sheet_index > 0) {
			/* NOTE : the docs lie the index for biff7 is
			 * indeed a reference to the externsheet
			 * however we have examples in biff8 that can
			 * only to be explained by a 1 based index to
			 * the boundsheets.  Which is not unreasonable
			 * given that these are local names */
			if (importer->ver >= MS_BIFF_V8) {
				if (sheet_index <= importer->boundsheet_sheet_by_index->len &&
				    sheet_index > 0)
					sheet = g_ptr_array_index (importer->boundsheet_sheet_by_index, sheet_index-1);
				else
					g_warning ("So much for that theory 2");
			} else
				sheet = excel_externsheet_v7 (&importer->container, sheet_index);
		}

		/* do we have a stub from a forward decl ? */
		if (importer->num_name_records < importer->names->len)
			nexpr = g_ptr_array_index (importer->names, importer->num_name_records);

		XL_NEED_BYTES (expr_len);
		nexpr = excel_parse_name (importer, sheet,
			name, data, expr_len, TRUE, nexpr);
		data += expr_len;

		/* Add a ref to keep it around after the excel-sheet/wb goes
		 * away.  externnames do not get references and are unrefed
		 * after import finishes, which destroys them if they are not
		 * in use. */
		if (nexpr != NULL) {
			expr_name_ref (nexpr);
			nexpr->is_hidden = (flags & 0x0001) ? TRUE : FALSE;

			/* Undocumented magic.
			 * XL stores a hidden name with the details of an autofilter */
			if (nexpr->is_hidden && !strcmp (nexpr->name->str, "_FilterDatabase"))
				excel_prepare_autofilter (importer, nexpr);
			/* g_warning ("flags = %hx, state = %s\n", flags, global ? "global" : "sheet"); */

			else if ((flags & 0xE) == 0xE) /* Function & VB-Proc & Proc */
				gnm_func_add_placeholder (importer->wb,
					nexpr->name->str, "VBA", TRUE);
		}
	}

	/* nexpr is potentially NULL if there was an error */
	if (importer->num_name_records < importer->names->len)
		g_ptr_array_index (importer->names, importer->num_name_records) = nexpr;
	else if (importer->num_name_records == importer->names->len)
		g_ptr_array_add (importer->names, nexpr);
	importer->num_name_records++;

	d (5, {
		guint8  menu_txt_len	= GSF_LE_GET_GUINT8  (q->data + 10);
		guint8  descr_txt_len	= GSF_LE_GET_GUINT8  (q->data + 11);
		guint8  help_txt_len	= GSF_LE_GET_GUINT8  (q->data + 12);
		guint8  status_txt_len= GSF_LE_GET_GUINT8  (q->data + 13);
		char *menu_txt;
		char *descr_txt;
		char *help_txt;
		char *status_txt;

		menu_txt = excel_get_text_fixme (importer, data, menu_txt_len, NULL);
		data += menu_txt_len;
		descr_txt = excel_get_text_fixme (importer, data, descr_txt_len, NULL);
		data += descr_txt_len;
		help_txt = excel_get_text_fixme (importer, data, help_txt_len, NULL);
		data += help_txt_len;
		status_txt = excel_get_text_fixme (importer, data, status_txt_len, NULL);

		g_printerr ("Name record: '%s', '%s', '%s', '%s', '%s'\n",
			name ? name : "(null)",
			menu_txt ? menu_txt : "(null)",
			descr_txt ? descr_txt : "(null)",
			help_txt ? help_txt : "(null)",
			status_txt ? status_txt : "(null)");

		if ((flags & 0x0001) != 0) g_printerr (" Hidden");
		if ((flags & 0x0002) != 0) g_printerr (" Function");
		if ((flags & 0x0004) != 0) g_printerr (" VB-Proc");
		if ((flags & 0x0008) != 0) g_printerr (" Proc");
		if ((flags & 0x0010) != 0) g_printerr (" CalcExp");
		if ((flags & 0x0020) != 0) g_printerr (" BuiltIn");
		if ((flags & 0x1000) != 0) g_printerr (" BinData");
		g_printerr ("\n");

		g_free (menu_txt);
		g_free (descr_txt);
		g_free (help_txt);
		g_free (status_txt);
	});
}

static void
excel_read_XCT (BiffQuery *q, GnmXLImporter *importer)
{
	guint16 last_col, opcode;
	guint8  const *data;
	unsigned len;
	int count;
	Sheet *sheet = NULL;
	GnmCell  *cell;
	GnmValue *v;
	GnmEvalPos ep;

	if (importer->ver >= MS_BIFF_V8) {
		guint16 supbook;

		XL_CHECK_CONDITION (q->length == 4);

		count   = GSF_LE_GET_GINT16 (q->data);
		supbook = GSF_LE_GET_GUINT16 (q->data+2);
	} else {
		XL_CHECK_CONDITION (q->length == 2);

		count = GSF_LE_GET_GINT16 (q->data);

	}
	if (count < 0) /* WHAT THE HECK DOES NEGATIVE MEAN ?? */
		count = -count;

	if (sheet != NULL)
		eval_pos_init_sheet (&ep, sheet);

	while (count-- > 0) {
		if (!ms_biff_query_peek_next (q, &opcode)) {
			g_warning ("Expected a CRN record");
			return;
		} else if (opcode != BIFF_CRN) {
			g_warning ("Expected a CRN record not a %hx", opcode);
			return;
		}
		ms_biff_query_next (q);

		XL_CHECK_CONDITION (q->length >= 4);

		ep.eval.col = GSF_LE_GET_GUINT8  (q->data+0);
		last_col    = GSF_LE_GET_GUINT8  (q->data+1);
		ep.eval.row = GSF_LE_GET_GUINT16 (q->data+2);

		/* ignore content for sheets that are already loaded */
		if (sheet == NULL)
			continue;

		for (data = q->data + 4; ep.eval.col <= last_col ; ep.eval.col++) {
			guint8 oper;
			XL_NEED_BYTES (1);

			oper = *data++;
			switch (oper) {
			case  1:
				XL_NEED_BYTES (8);
				v = value_new_float (GSF_LE_GET_DOUBLE (data));
				data += 8;
				break;
			case  2:
				XL_NEED_BYTES (1);
				len = *data++;
				v = value_new_string_nocopy (
					excel_get_text_fixme (importer, data, len, NULL));
				data += len;
				break;

			case 4:
				XL_NEED_BYTES (2);
				v = value_new_bool (GSF_LE_GET_GUINT16 (data) != 0);
				/* FIXME: 8?? */
				data += 8;
				break;

			case 16:
				XL_NEED_BYTES (2);
				v = biff_get_error (&ep, GSF_LE_GET_GUINT16 (data));
				/* FIXME: 8?? */
				data += 8;
				break;

			default :
				g_warning ("Unknown oper type 0x%x in a CRN record", oper);
				v = NULL;
			}

			if (v != NULL) {
				cell = sheet_cell_fetch (sheet, ep.eval.col, ep.eval.row);
				if (cell)
					gnm_cell_set_value (cell, v);
				else
					value_release (v);
			}
		}
	}
}

static XL_font_width const *
xl_find_fontspec (ExcelReadSheet *esheet, float *size20)
{
	/* Use the 'Normal' Style which is by definition the 0th */
	BiffXFData const *xf = excel_get_xf (esheet, 0);
	ExcelFont const *fd = (xf != NULL)
		? excel_font_get (esheet->container.importer, xf->font_idx)
		: NULL;
	*size20 = (fd != NULL) ? (fd->height / (20. * 10.)) : 1.;
	return xl_lookup_font_specs ((fd != NULL) ? fd->fontname : "Arial");
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

static void
excel_read_ROW (BiffQuery *q, ExcelReadSheet *esheet)
{
	guint16 row, height;
	guint16 flags = 0;
	guint16 flags2 = 0;
	guint16 xf;
	gboolean is_std_height;

	XL_CHECK_CONDITION (q->length >= (q->opcode == BIFF_ROW_v2 ? 16 : 8));

	row = GSF_LE_GET_GUINT16 (q->data);
#if 0
	/* Unnecessary info for now.
	 * do we want to preallocate based on this info?
	 */
	guint16 const start_col = GSF_LE_GET_GUINT16 (q->data + 2);
	guint16 const end_col = GSF_LE_GET_GUINT16 (q->data + 4) - 1;
#endif
	height = GSF_LE_GET_GUINT16 (q->data + 6);

	/* If the bit is on it indicates that the row is of 'standard' height.
	 * However the remaining bits still include the size.
	 */
	is_std_height = (height & 0x8000) != 0;

	if (q->opcode == BIFF_ROW_v2) {
		flags = GSF_LE_GET_GUINT16 (q->data + 12);
		flags2 = GSF_LE_GET_GUINT16 (q->data + 14);
	}
	xf = flags2 & 0xfff;

	d (1, {
		fprintf (stderr,"Row %d height 0x%x, flags=0x%x 0x%x;\n", row + 1, height, flags, flags2);
		if (is_std_height)
			fputs ("Is Std Height;\n", stderr);
		if (flags2 & 0x1000)
			fputs ("Top thick;\n", stderr);
		if (flags2 & 0x2000)
			fputs ("Bottom thick;\n", stderr);
	});

	/* TODO: Put mechanism in place to handle thick margins */
	/* TODO: Columns actually set the size even when it is the default.
	 *       Which approach is better?
	 */
	if (!is_std_height) {
		double hu = get_row_height_units (height);
		sheet_row_set_size_pts (esheet->sheet, row, hu,
			(flags & 0x40) ? TRUE : FALSE);
	}

	if (flags & 0x20)
		colrow_set_visibility (esheet->sheet, FALSE, FALSE, row, row);

	if (flags & 0x80) {
		if (xf != 0)
			excel_set_xf_segment (esheet,
					      0, gnm_sheet_get_max_cols (esheet->sheet) - 1,
					      row, row, xf);
		d (1, fprintf (stderr,"row %d has flags 0x%x a default style %hd;\n",
			      row + 1, flags, xf););
	}

	if ((unsigned)(flags & 0x17) > 0)
		colrow_set_outline (sheet_row_fetch (esheet->sheet, row),
			(unsigned)(flags & 0x7), flags & 0x10);
}

static void
excel_read_TAB_COLOR (BiffQuery *q, ExcelReadSheet *esheet)
{
	/* this is a guess, but the only field I see
	 * changing seems to be the colour.
	 */
#if 0
 0 | 62  8  0  0  0  0  0  0  0  0  0  0 14  0  0  0 | b...............
10 |     0  0  0 XX XX XX XX XX XX XX XX XX XX XX XX |  ...************

office 12 seems to add 8 bytes
#endif
	guint8 color_index;
	GnmColor *color;
	GnmColor *text_color;
	int contrast;

	XL_CHECK_CONDITION (q->length >= 20);

	/* be conservative for now, we have not seen a palette larger than 56
	 * so this is largely moot, this is probably a uint32
	 */
	color_index = GSF_LE_GET_GUINT8 (q->data + 16);
	color = excel_palette_get (esheet->container.importer, color_index);
	contrast = color->gdk_color.red + color->gdk_color.green + color->gdk_color.blue;
	if (contrast >= 0x18000)
		text_color = style_color_black ();
	else
		text_color = style_color_white ();
	g_object_set (esheet->sheet,
		      "tab-foreground", text_color,
		      "tab-background", color,
		      NULL);
	if (color != NULL) {
		d (1, fprintf (stderr,"%s tab colour = %04hx:%04hx:%04hx\n",
			      esheet->sheet->name_unquoted,
			      color->gdk_color.red, color->gdk_color.green, color->gdk_color.blue););
	}
	style_color_unref (text_color);
	style_color_unref (color);
}

static void
excel_read_COLINFO (BiffQuery *q, ExcelReadSheet *esheet)
{
	int i;
	float scale, width;
	guint16 const  firstcol	  = GSF_LE_GET_GUINT16 (q->data);
	guint16        lastcol	  = GSF_LE_GET_GUINT16 (q->data + 2);
	int            charwidths = GSF_LE_GET_GUINT16 (q->data + 4);
	guint16 const  xf	  = GSF_LE_GET_GUINT16 (q->data + 6);
	guint16 const  options	  = GSF_LE_GET_GUINT16 (q->data + 8);
	gboolean       hidden	  = (options & 0x0001) != 0;
	/*gboolean const customWidth= (options & 0x0002) != 0;	   undocumented */
	gboolean const bestFit    = (options & 0x0004) != 0;	/* undocumented */
	gboolean const collapsed  = (options & 0x1000) != 0;
	unsigned const outline_level = (unsigned)((options >> 8) & 0x7);
	XL_font_width const *spec = xl_find_fontspec (esheet, &scale);

	XL_CHECK_CONDITION (firstcol < gnm_sheet_get_max_cols (esheet->sheet));
	g_return_if_fail (spec != NULL);

	/* Widths appear to be quoted including margins and the leading
	 * gridline that gnumeric expects.  The charwidths here are not
	 * strictly linear.  So I measured in increments of -2 -1 0 1 2 around
	 * the default width when using each font @ 10pts as
	 * the Normal Style.   The pixel calculation is then reduced to
	 *
	 *     (default_size + ((quoted_width - baseline) / step))
	 *		* scale : fonts != 10pts
	 *		* 72/96 : value in pts so that zoom is not a factor
	 *
	 * NOTE: These measurements do NOT correspond to what is shown to the
	 * user */
	width = 8. * spec->defcol_unit +
		(float)(charwidths - spec->colinfo_baseline) / spec->colinfo_step;
	width *= scale * 72./96.;

	if (width <= 0) {	/* Columns are of default width */
		width = esheet->sheet->cols.default_style.size_pts;
		hidden = TRUE;
	} else if (width < 4) /* gnumeric can not draw without a margin */
		width = 4;

	d (1, {
		fprintf (stderr,"Column Formatting %s!%s of width "
		      "%hu/256 characters (%f pts)\n",
		      esheet->sheet->name_quoted,
		      cols_name (firstcol, lastcol), charwidths, width);
		fprintf (stderr,"Options 0x%hx, default style %hu\n", options, xf);
	});

	/* NOTE: seems like this is inclusive firstcol, inclusive lastcol */
	if (lastcol >= gnm_sheet_get_max_cols (esheet->sheet))
		lastcol = gnm_sheet_get_max_cols (esheet->sheet) - 1;
	for (i = firstcol; i <= lastcol; i++) {
		/* Kludge : we should really use
		 *	hard_size == customWidth && !bestFit
		 * but these flags are undocumented and gnumeric < 1.7.5 && OOo
		 * export them as 0.  So we are reduced to using */
		sheet_col_set_size_pts (esheet->sheet, i, width, !bestFit);
		if (outline_level > 0 || collapsed)
			colrow_set_outline (sheet_col_fetch (esheet->sheet, i),
				outline_level, collapsed);
	}

	if (xf != 0)
		excel_set_xf_segment (esheet, firstcol, lastcol,
				      0, gnm_sheet_get_max_rows (esheet->sheet) - 1, xf);

	if (hidden)
		colrow_set_visibility (esheet->sheet, TRUE, FALSE,
				       firstcol, lastcol);
}

/* Add a bmp header so that gdk-pixbuf can do the work */
static GdkPixbuf *
excel_read_os2bmp (BiffQuery *q, guint32 image_len)
{
	guint16 op;
	GError *err = NULL;
	GdkPixbufLoader *loader = NULL;
	GdkPixbuf	*pixbuf = NULL;
	gboolean ret = FALSE;
	guint8 bmphdr[14];
	guint bpp;
	guint offset;

	loader = gdk_pixbuf_loader_new_with_type ("bmp", &err);
	if (!loader)
		return NULL;
	bmphdr[0] = 'B';
	bmphdr[1] = 'M';
	GSF_LE_SET_GUINT32 (bmphdr + 2, image_len + sizeof bmphdr);
	GSF_LE_SET_GUINT16 (bmphdr + 6, 0);
	GSF_LE_SET_GUINT16 (bmphdr + 8, 0);
	bpp = GSF_LE_GET_GUINT16 (q->data + 18);
	switch (bpp) {
	case 24: offset = 0;       break;
	case 8:  offset = 256 * 3; break;
	case 4:  offset = 16 * 3;  break;
	default: offset = 2 * 3;   break;
	}
	offset += sizeof bmphdr + 12;
	GSF_LE_SET_GUINT32 (bmphdr + 10, offset);
	ret = gdk_pixbuf_loader_write (loader, bmphdr, sizeof bmphdr, &err);
	if (ret)
		ret = gdk_pixbuf_loader_write (loader, q->data+8,
					       q->length-8, &err);
	image_len += 8;
	while (ret &&  image_len > q->length &&
	       ms_biff_query_peek_next (q, &op) && op == BIFF_CONTINUE) {
		image_len -= q->length;
		ms_biff_query_next (q);
		ret = gdk_pixbuf_loader_write (loader, q->data, q->length,
					       &err);
	}
	gdk_pixbuf_loader_close (loader, ret ? &err : NULL);
	if (ret) {
		pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
		g_object_ref (pixbuf);
	} else {
		g_message ("Unable to read OS/2 BMP image: %s\n",
			   err->message);
		g_error_free (err);
	}
	g_object_unref (G_OBJECT (loader));
	return pixbuf;
}

/* When IMDATA or BG_PIC is bitmap, the format is OS/2 BMP, but the
 * 14 bytes header is missing.
 */
GdkPixbuf *
excel_read_IMDATA (BiffQuery *q, gboolean keep_image)
{
	guint16 op;
	guint32 image_len = GSF_LE_GET_GUINT32 (q->data + 4);

	GdkPixbuf	*pixbuf = NULL;

	guint16 const format   = GSF_LE_GET_GUINT16 (q->data);

	switch (format) {
	case 0x2: break;	/* Windows metafile/Mac pict */
	case 0x9:		/* OS/2 BMP sans header */
	{
		pixbuf = excel_read_os2bmp (q, image_len);
	}
	break;
	case 0xe: break;	/* Native format */
	default: break;		/* Unknown format */
	}

	/* Dump formats which weren't handled above to file */
	if (format != 0x9) {
		static int count = 0;
		FILE *f = NULL;
		char *file_name;
		char const *from_name;
		char const *format_name;
		guint16 const format   = GSF_LE_GET_GUINT16 (q->data);
		guint16 const from_env = GSF_LE_GET_GUINT16 (q->data + 2);

		switch (from_env) {
		case 1: from_name = "Windows"; break;
		case 2: from_name = "Macintosh"; break;
		default: from_name = "Unknown environment?"; break;
		}
		switch (format) {
		case 0x2:
		format_name = (from_env == 1) ? "windows metafile" : "mac pict";
		break;

		case 0xe: format_name = "'native format'"; break;
		default: format_name = "Unknown format?"; break;
		}

		d (1, {	/* WARNING KEEP THIS DEBUG THE SAME AS BELOW */
			fprintf (stderr,"Picture from %s in %s format\n",
				from_name, format_name);

			file_name = g_strdup_printf ("imdata%d", count++);
			f = g_fopen (file_name, "w");
			fwrite (q->data+8, 1, q->length-8, f);
			g_free (file_name);
		});

		image_len += 8;
		while (image_len > q->length &&
		       ms_biff_query_peek_next (q, &op) &&
		       op == BIFF_CONTINUE) {
			image_len -= q->length;
			ms_biff_query_next (q);
			d (1, {	/* WARNING KEEP THIS DEBUG THE SAME AS ABOVE */
				fwrite (q->data, 1, q->length, f);
			});
		}
		d (1, {	/* WARNING KEEP THIS DEBUG THE SAME AS ABOVE */
			fclose (f);
		});
	}

	return pixbuf;
}

static void
excel_read_SELECTION (BiffQuery *q, ExcelReadSheet *esheet)
{
	GnmCellPos edit_pos, tmp;
	unsigned pane_number, i, j, num_refs;
	guint8 *refs;
	SheetView *sv = sheet_get_view (esheet->sheet, esheet->container.importer->wbv);
	GnmRange r;

	XL_CHECK_CONDITION (q->length >= 9);
	pane_number = GSF_LE_GET_GUINT8 (q->data);
	edit_pos.row = GSF_LE_GET_GUINT16 (q->data + 1);
	edit_pos.col = GSF_LE_GET_GUINT16 (q->data + 3);
	j = GSF_LE_GET_GUINT16 (q->data + 5);
	num_refs = GSF_LE_GET_GUINT16 (q->data + 7);
	XL_CHECK_CONDITION (q->length >= 9 + 6 * num_refs);

	if (pane_number != esheet->active_pane)
		return;

	d (5, fprintf (stderr,"Start selection in pane #%d\n", pane_number););
	d (5, fprintf (stderr,"Cursor: %s in Ref #%d\n", cellpos_as_string (&edit_pos),
		      j););

	g_return_if_fail (sv != NULL);

	sv_selection_reset (sv);
	for (i = 0; i++ < num_refs ; ) {
		refs = q->data + 9 + 6 * (++j % num_refs);
		r.start.row = GSF_LE_GET_GUINT16 (refs + 0);
		r.end.row   = GSF_LE_GET_GUINT16 (refs + 2);
		r.start.col = GSF_LE_GET_GUINT8  (refs + 4);
		r.end.col   = GSF_LE_GET_GUINT8  (refs + 5);

		d (5, fprintf (stderr,"Ref %d = %s\n", i-1, range_as_string (&r)););

		tmp = (i == num_refs) ? edit_pos : r.start;
		sv_selection_add_full (sv,
			tmp.col, tmp.row,
			r.start.col, r.start.row,
			r.end.col, r.end.row);
	}

	d (5, fprintf (stderr,"Done selection\n"););
}

static void
excel_read_DEF_ROW_HEIGHT (BiffQuery *q, ExcelReadSheet *esheet)
{
	guint16 flags = 0;
	guint16 height = 0; /* must be 16 bit */
	double height_units;

	if (q->opcode != BIFF_DEFAULTROWHEIGHT_v0) {
		XL_CHECK_CONDITION (q->length >= 4);
		flags  = GSF_LE_GET_GUINT16 (q->data);
		height = GSF_LE_GET_GUINT16 (q->data + 2);
	} else {
		XL_CHECK_CONDITION (q->length >= 2);
		height = GSF_LE_GET_GUINT16 (q->data);
		height &= 0x7fff; /* there seems to be a flag in the top bit */
	}

	height_units = get_row_height_units (height);
	d (2, {
		fprintf (stderr,"Default row height %3.3g;\n", height_units);
		if (flags & 0x04)
			fprintf (stderr," + extra space above;\n");
		if (flags & 0x08)
			fprintf (stderr," + extra space below;\n");
	});

	sheet_row_set_default_size_pts (esheet->sheet, height_units);
}

static void
excel_read_DEF_COL_WIDTH (BiffQuery *q, ExcelReadSheet *esheet)
{
	guint16 charwidths;
	float scale;
	XL_font_width const *spec = xl_find_fontspec (esheet, &scale);

	XL_CHECK_CONDITION (q->length >= 2);
	charwidths = GSF_LE_GET_GUINT16 (q->data);
	d (0, fprintf (stderr,"Default column width %hu characters\n", charwidths););

	/* According to the tooltip the default width is 8.43 character widths
	 * and 64 pixels wide (Arial 10) which appears to include margins, and
	 * the leading gridline That is saved as 8 char widths for
	 * DEL_COL_WIDTH and 9.14 widths for COLINFO */
	sheet_col_set_default_size_pts (esheet->sheet,
		charwidths * spec->defcol_unit * scale * 72./96.);
}

/* we could get this implicitly from the cols/rows
 * but this is faster
 */
static void
excel_read_GUTS (BiffQuery *q, ExcelReadSheet *esheet)
{
	int col_gut, row_gut;

	XL_CHECK_CONDITION (q->length == 8);

	/* ignore the specification of how wide/tall the gutters are */
	row_gut = GSF_LE_GET_GUINT16 (q->data + 4);
	d (2, fprintf (stderr, "row_gut = %d", row_gut););
	if (row_gut >= 1)
		row_gut--;
	col_gut = GSF_LE_GET_GUINT16 (q->data + 6);
	d (2, fprintf (stderr, "col_gut = %d", col_gut););
	if (col_gut >= 1)
		col_gut--;
	sheet_colrow_gutter (esheet->sheet, TRUE, col_gut);
	sheet_colrow_gutter (esheet->sheet, FALSE, row_gut);
}

/* Map a BIFF4 SETUP paper size number to the equivalent libgnomeprint paper
 * name or width and height.
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

static void
excel_read_SETUP (BiffQuery *q, ExcelReadSheet *esheet)
{
	PrintInformation *pi = esheet->sheet->print_info;
	guint16  flags;
	gboolean rotate_paper = FALSE;
	gboolean portrait_orientation = TRUE;

	XL_CHECK_CONDITION (q->length >= 12);

	flags = GSF_LE_GET_GUINT16 (q->data + 10);
	pi->print_across_then_down = (flags & 0x1) != 0;
	pi->print_black_and_white  = (flags & 0x8) != 0;

	if (0 == (flags & 0x4)) {
		guint16 papersize = GSF_LE_GET_GUINT16 (q->data + 0);

		d (2, fprintf (stderr,"Paper size %hu\n", papersize););

		if (papersize < G_N_ELEMENTS (paper_size_table)) {
			guchar *paper_name = (guchar *)paper_size_table[papersize].gp_name;
			rotate_paper = paper_size_table[papersize].rotated;
			if (paper_name != NULL) {
				print_info_set_paper (pi, paper_name);
			}
		}

		pi->scaling.percentage.x = pi->scaling.percentage.y =
			GSF_LE_GET_GUINT16 (q->data + 2);
		pi->start_page	     = GSF_LE_GET_GUINT16 (q->data + 4);
		pi->scaling.dim.cols = GSF_LE_GET_GUINT16 (q->data + 6);
		pi->scaling.dim.rows = GSF_LE_GET_GUINT16 (q->data + 8);
		if (pi->scaling.percentage.x < 1. || pi->scaling.percentage.x > 1000.) {
			if (pi->scaling.percentage.x != 0) {
				/* 0 seems to be 'auto' */
				g_warning ("setting invalid print scaling (%f) to 100%%",
					   pi->scaling.percentage.x);
			}
			pi->scaling.percentage.x = pi->scaling.percentage.y = 100.;
		}
	}
	if (esheet_ver (esheet) == MS_BIFF_V4 || 0 == (flags & 0x40))
		portrait_orientation = (flags & 0x2) != 0;
	if (rotate_paper)
		portrait_orientation = !portrait_orientation;

	print_info_set_paper_orientation (pi, portrait_orientation
					  ? GTK_PAGE_ORIENTATION_PORTRAIT
					  : GTK_PAGE_ORIENTATION_LANDSCAPE);


	if (esheet_ver (esheet) > MS_BIFF_V4) {
		XL_CHECK_CONDITION (q->length >= 34);

		pi->print_as_draft = (flags & 0x10) != 0;
		pi->comment_placement = (flags & 0x20)
			? PRINT_COMMENTS_IN_PLACE : PRINT_COMMENTS_NONE;
		print_info_set_margin_header (pi,
			GO_IN_TO_PT (gsf_le_get_double (q->data + 16)));
		print_info_set_margin_footer (pi,
			GO_IN_TO_PT (gsf_le_get_double (q->data + 24)));
		if (0 == (flags & 0x4))
			pi->n_copies = GSF_LE_GET_GUINT16 (q->data + 32);
		d (2, fprintf (stderr,"resolution %hu vert. res. %hu\n",
			       GSF_LE_GET_GUINT16 (q->data + 12),
			       GSF_LE_GET_GUINT16 (q->data + 14)););
	}

	if (esheet_ver (esheet) >= MS_BIFF_V8) {
		if ((flags & 0x200) && pi->comment_placement == PRINT_COMMENTS_IN_PLACE)
			pi->comment_placement = PRINT_COMMENTS_AT_END;
		switch ((flags >> 10) & 3) {
		case 0 : pi->error_display = PRINT_ERRORS_AS_DISPLAYED; break;
		case 1 : pi->error_display = PRINT_ERRORS_AS_BLANK; break;
		case 2 : pi->error_display = PRINT_ERRORS_AS_DASHES; break;
		case 3 : pi->error_display = PRINT_ERRORS_AS_NA; break;
		}
	}
}

static void
excel_read_MULRK (BiffQuery *q, ExcelReadSheet *esheet)
{
	guint32 col, row, lastcol;
	guint8 const *ptr = q->data;
	GnmValue *v;
	BiffXFData const *xf;
	GnmStyle *mstyle;

	XL_CHECK_CONDITION (q->length >= 4 + 6 + 2);

	row = GSF_LE_GET_GUINT16 (q->data);
	col = GSF_LE_GET_GUINT16 (q->data + 2);
	ptr += 4;
	lastcol = GSF_LE_GET_GUINT16 (q->data + q->length - 2);

	XL_CHECK_CONDITION (lastcol >= col);

	if (q->length != 4 + 6 * (lastcol - col + 1) + 2) {
		g_warning ("MULRK with strange size.");
		lastcol = col + (q->length - (4 + 6 + 2)) / 6 - 1;
	}

	for (; col <= lastcol ; col++) {
		GnmCell *cell;
		/* 2byte XF, 4 byte RK */
		v = biff_get_rk (ptr + 2);
		xf = excel_get_xf (esheet, GSF_LE_GET_GUINT16 (ptr));
		mstyle = excel_get_style_from_xf (esheet, xf);
		if (mstyle != NULL)
			sheet_style_set_pos (esheet->sheet, col, row, mstyle);
		if (xf && xf->is_simple_format)
			value_set_fmt (v, xf->style_format);
		cell = sheet_cell_fetch (esheet->sheet, col, row);
		if (cell)
			gnm_cell_set_value (cell, v);
		else
			value_release (v);
		ptr += 6;
	}
}

static void
excel_read_MULBLANK (BiffQuery *q, ExcelReadSheet *esheet)
{
	/* This is an educated guess, docs are not terribly clear */
	int firstcol = XL_GETCOL (q);
	int const row = XL_GETROW (q);
	guint8 const *ptr = (q->data + q->length - 2);
	int lastcol = GSF_LE_GET_GUINT16 (ptr);
	int i, range_end, prev_xf, xf_index;
	d (0, {
		fprintf (stderr,"Cells in row %d are blank starting at col %s until col ",
			row + 1, col_name (firstcol));
		fprintf (stderr,"%s;\n",
			col_name (lastcol));
	});

	if (lastcol < firstcol) {
		int tmp = firstcol;
		firstcol = lastcol;
		lastcol = tmp;
	}

	range_end = i = lastcol;
	prev_xf = -1;
	do {
		ptr -= 2;
		xf_index = GSF_LE_GET_GUINT16 (ptr);
		d (2, {
			fprintf (stderr," xf (%s) = 0x%x", col_name (i), xf_index);
			if (i == firstcol)
				fprintf (stderr,"\n");
		});

		if (prev_xf != xf_index) {
			if (prev_xf >= 0)
				excel_set_xf_segment (esheet, i + 1, range_end,
						      row, row, prev_xf);
			prev_xf = xf_index;
			range_end = i;
		}
	} while (--i >= firstcol);
	excel_set_xf_segment (esheet, firstcol, range_end,
			      row, row, prev_xf);
	d (2, fprintf (stderr,"\n"););
}

static guint8 const *
excel_read_range (GnmRange *r, guint8 const *data)
{
	r->start.row = GSF_LE_GET_GUINT16 (data);
	r->end.row = GSF_LE_GET_GUINT16   (data + 2);
	r->start.col = GSF_LE_GET_GUINT16 (data + 4);
	r->end.col = GSF_LE_GET_GUINT16   (data + 6);
	d (4, range_dump (r, "\n"););

	return data + 8;
}

/*
 * No documentation exists for this record, but this makes
 * sense given the other record formats.
 */
static void
excel_read_MERGECELLS (BiffQuery *q, ExcelReadSheet *esheet)
{
	int num_merged = GSF_LE_GET_GUINT16 (q->data);
	guint8 const *data = q->data + 2;
	GnmRange r;
	GSList *overlap;

	XL_CHECK_CONDITION (q->length == (unsigned int)(2 + 8 * num_merged));

	while (num_merged-- > 0) {
		data = excel_read_range (&r, data);
		overlap = gnm_sheet_merge_get_overlap (esheet->sheet, &r);
		if (overlap) {
			GnmRange *r2 = (GnmRange *) overlap->data;

			/* Unmerge r2, then merge (r U r2) */
			gnm_sheet_merge_remove (esheet->sheet, r2,
				 GO_CMD_CONTEXT (esheet->container.importer->context));
			r = range_union (&r, r2);
			g_slist_free (overlap);
		}
		gnm_sheet_merge_add (esheet->sheet, &r, FALSE,
			 GO_CMD_CONTEXT (esheet->container.importer->context));
	}
}

static void
excel_read_DIMENSIONS (BiffQuery *q, GnmXLImporter *importer)
{
	GnmRange r;

	if (importer->ver >= MS_BIFF_V8) {
		XL_CHECK_CONDITION (q->length >= 12);
		r.start.row = GSF_LE_GET_GUINT32 (q->data);
		r.end.row   = GSF_LE_GET_GUINT32 (q->data + 4);
		r.start.col = GSF_LE_GET_GUINT16 (q->data + 8);
		r.end.col   = GSF_LE_GET_GUINT16 (q->data + 10);
	} else {
		XL_CHECK_CONDITION (q->length >= 8);
		excel_read_range (&r, q->data);
	}

	d (1, fprintf (stderr,"Dimension = %s\n", range_as_string (&r)););
}

static MSContainer *
sheet_container (ExcelReadSheet *esheet)
{
	ms_container_set_blips (&esheet->container, esheet->container.importer->container.blips);
	return &esheet->container;
}

static gboolean
excel_read_sheet_PROTECT (BiffQuery *q, ExcelReadSheet *esheet)
{
	gboolean is_protected = TRUE;

	/* MS Docs fail to mention that in some stream this
	 * record can have size zero.  I assume the in that
	 * case its existence is the flag.  */
	if (q->length >= 2)
		is_protected = (1 == GSF_LE_GET_GUINT16 (q->data));

	esheet->sheet->is_protected = is_protected;

	return is_protected;
}

static gboolean
excel_read_workbook_PROTECT (BiffQuery *q, WorkbookView *wb_view)
{
	gboolean is_protected = TRUE;

	/* MS Docs fail to mention that in some stream this
	 * record can have size zero.  I assume the in that
	 * case its existence is the flag.  */
	if (q->length >= 2)
		is_protected = (1 == GSF_LE_GET_GUINT16 (q->data));

	wb_view->is_protected = is_protected;

	return is_protected;
}

static void
excel_read_WSBOOL (BiffQuery *q, ExcelReadSheet *esheet)
{
	guint16 options;

	XL_CHECK_CONDITION (q->length == 2);

	options = GSF_LE_GET_GUINT16 (q->data);
	/* 0x0001 automatic page breaks are visible */
	/* 0x0010 the sheet is a dialog sheet */
	/* 0x0020 automatic styles are not applied to an outline */
	esheet->sheet->outline_symbols_below = 0 != (options & 0x040);
	esheet->sheet->outline_symbols_right = 0 != (options & 0x080);
	if (NULL != esheet->sheet->print_info)
		esheet->sheet->print_info->scaling.type =
			(options & 0x100) ? PRINT_SCALE_FIT_PAGES : PRINT_SCALE_PERCENTAGE;

	/* 0x0200 biff 3-4 0 == save external linked values, 1 == do not save */
	/* XL docs wrong 0xc00 no 0x600, OOo docs wrong no distinct row vs col */
	esheet->sheet->display_outlines      = 0 != (options & 0xc00);

	/* Biff4 0x3000 window arrangement
	 *     0b == tiled
	 *     1b == arrange horiz
	 *    10b == arrange vert
	 *    11b == cascade */

	/* biff 4-8 0x4000, 0 == std expr eval, 1 == alt expr eval ? */
	/* biff 4-8 0x8000, 0 == std fmla entry, 1 == alt fmla entry ? */
}

static void
excel_read_CALCCOUNT (BiffQuery *q, GnmXLImporter *importer)
{
	guint16 count;

	XL_CHECK_CONDITION (q->length == 2);

	count = GSF_LE_GET_GUINT16 (q->data);
	workbook_iteration_max_number (importer->wb, count);
}

static void
excel_read_CALCMODE (BiffQuery *q, GnmXLImporter *importer)
{
	XL_CHECK_CONDITION (q->length == 2);
	workbook_set_recalcmode (importer->wb,
		GSF_LE_GET_GUINT16 (q->data) != 0);
}

static void
excel_read_DELTA (BiffQuery *q, GnmXLImporter *importer)
{
	double tolerance;

	XL_CHECK_CONDITION (q->length == 8);

	tolerance = gsf_le_get_double (q->data);
	workbook_iteration_tolerance (importer->wb, tolerance);
}

static void
excel_read_ITERATION (BiffQuery *q, GnmXLImporter *importer)
{
	guint16 enabled;

	XL_CHECK_CONDITION (q->length == 2);

	enabled = GSF_LE_GET_GUINT16 (q->data);
	workbook_iteration_enabled (importer->wb, enabled != 0);
}

static void
excel_read_PANE (BiffQuery *q, ExcelReadSheet *esheet, WorkbookView *wb_view)
{
	if (esheet->freeze_panes) {
		guint16 x = GSF_LE_GET_GUINT16 (q->data + 0);
		guint16 y = GSF_LE_GET_GUINT16 (q->data + 2);
		guint16 rwTop = GSF_LE_GET_GUINT16 (q->data + 4);
		guint16 colLeft = GSF_LE_GET_GUINT16 (q->data + 6);
		SheetView *sv = sheet_get_view (esheet->sheet, esheet->container.importer->wbv);
		GnmCellPos frozen, unfrozen;

		esheet->active_pane = GSF_LE_GET_GUINT16 (q->data + 8);
		if (esheet->active_pane > 3) {
			g_warning ("Invalid pane '%u' selected", esheet->active_pane);
			esheet->active_pane = 3;
		}

		g_return_if_fail (sv != NULL);

		frozen = unfrozen = sv->initial_top_left;
		if (x > 0)
			unfrozen.col += x;
		else
			colLeft = sv->initial_top_left.col;
		if (y > 0)
			unfrozen.row += y;
		else
			rwTop = sv->initial_top_left.row;
		sv_freeze_panes (sv, &frozen, &unfrozen);
		sv_set_initial_top_left (sv, colLeft, rwTop);
	} else {
		g_warning ("EXCEL : no support for split panes yet (%s)", esheet->sheet->name_unquoted);
	}
}

static void
excel_read_WINDOW2 (BiffQuery *q, ExcelReadSheet *esheet, WorkbookView *wb_view)
{
	SheetView *sv = sheet_get_view (esheet->sheet, esheet->container.importer->wbv);
	guint16 top_row    = 0;
	guint16 left_col   = 0;
	guint32 biff_pat_col;
	gboolean set_grid_color;

	if (q->opcode == BIFF_WINDOW2_v2) {
		guint16 const options    = GSF_LE_GET_GUINT16 (q->data + 0);

		XL_CHECK_CONDITION (q->length >= 10);

		esheet->sheet->display_formulas	= ((options & 0x0001) != 0);
		esheet->sheet->hide_grid	= ((options & 0x0002) == 0);
		esheet->sheet->hide_col_header  =
		esheet->sheet->hide_row_header	= ((options & 0x0004) == 0);
		esheet->freeze_panes		= ((options & 0x0008) != 0);
		esheet->sheet->hide_zero	= ((options & 0x0010) == 0);
		set_grid_color = (options & 0x0020) == 0;
		g_object_set (esheet->sheet, "text-is-rtl", (options & 0x0040) != 0, NULL);

		top_row      = GSF_LE_GET_GUINT16 (q->data + 2);
		left_col     = GSF_LE_GET_GUINT16 (q->data + 4);
		biff_pat_col = GSF_LE_GET_GUINT32 (q->data + 6);

		d (0, if (options & 0x0200) fprintf (stderr,"Sheet flag selected\n"););
		if (options & 0x0400)
			wb_view_sheet_focus (wb_view, esheet->sheet);

		if (esheet_ver (esheet) >= MS_BIFF_V8 && q->length >= 14) {
			d (2, {
				guint16 const pageBreakZoom = GSF_LE_GET_GUINT16 (q->data + 10);
				guint16 const normalZoom = GSF_LE_GET_GUINT16 (q->data + 12);
				fprintf (stderr,"%hx %hx\n", normalZoom, pageBreakZoom);
			});
		}
	} else {
		XL_CHECK_CONDITION (q->length >= 14);

		esheet->sheet->display_formulas	= (q->data[0] != 0);
		esheet->sheet->hide_grid	= (q->data[1] == 0);
		esheet->sheet->hide_col_header  =
		esheet->sheet->hide_row_header	= (q->data[2] == 0);
		esheet->freeze_panes		= (q->data[3] != 0);
		esheet->sheet->hide_zero	= (q->data[4] == 0);
		set_grid_color			= (q->data[9] == 0);

		top_row      = GSF_LE_GET_GUINT16 (q->data + 5);
		left_col     = GSF_LE_GET_GUINT16 (q->data + 7);
		biff_pat_col = GSF_LE_GET_GUINT32 (q->data + 10);
	}

	if (set_grid_color) {
		GnmColor *pattern_color;
		if (esheet_ver (esheet) >= MS_BIFF_V8) {
			/* Get style color from palette*/
			pattern_color = excel_palette_get (
				esheet->container.importer,
				biff_pat_col & 0x7f);
		} else {
			guint8 r, g, b;

			r = (guint8) biff_pat_col;
			g = (guint8) (biff_pat_col >> 8);
			b = (guint8) (biff_pat_col >> 16);
			pattern_color = style_color_new_i8 (r, g, b);
		}
		d (2, fprintf (stderr,"auto pattern color "
			      "0x%x 0x%x 0x%x\n",
			      pattern_color->gdk_color.red,
			      pattern_color->gdk_color.green,
			      pattern_color->gdk_color.blue););
		sheet_style_set_auto_pattern_color (
			esheet->sheet, pattern_color);
	}

	g_return_if_fail (sv != NULL);

	/* until we import multiple views unfreeze just in case a previous view
	 * had frozen */
	sv_freeze_panes (sv, NULL, NULL);

	/* NOTE : This is top left of screen even if frozen, modify when
	 *        we read PANE */
	sv_set_initial_top_left (sv, left_col, top_row);
}

static void
excel_read_CF_border (GnmStyleCond *cond, ExcelReadSheet *esheet,
		      GnmStyleBorderLocation type,
		      unsigned xl_pat_index, unsigned xl_color_index)
{
	gnm_style_set_border (cond->overlay, GNM_STYLE_BORDER_LOCATION_TO_STYLE_ELEMENT (type),
		gnm_style_border_fetch (biff_xf_map_border (xl_pat_index),
			excel_palette_get (esheet->container.importer,
					   xl_color_index),
			gnm_style_border_get_orientation (type)));
}

static void
excel_read_CF (BiffQuery *q, ExcelReadSheet *esheet, GnmStyleConditions *sc)
{
	guint8 const type	= GSF_LE_GET_GUINT8  (q->data + 0);
	guint8 const op		= GSF_LE_GET_GUINT8  (q->data + 1);
	guint16 const expr0_len	= GSF_LE_GET_GUINT16 (q->data + 2);
	guint16 const expr1_len	= GSF_LE_GET_GUINT16 (q->data + 4);
	guint32 const flags	= GSF_LE_GET_GUINT32 (q->data + 6);
	unsigned offset;
	GnmStyleCond	cond;

	d (1, {
		gsf_mem_dump (q->data+6, 6);
		fprintf (stderr,"cond type = %d, op type = %d, flags = 0x%08x\n", (int)type, (int)op, flags);
	});
	switch (type) {
	case 1 :
		switch (op) {
		case 0x01 : cond.op = GNM_STYLE_COND_BETWEEN;	break;
		case 0x02 : cond.op = GNM_STYLE_COND_NOT_BETWEEN;	break;
		case 0x03 : cond.op = GNM_STYLE_COND_EQUAL;	break;
		case 0x04 : cond.op = GNM_STYLE_COND_NOT_EQUAL;	break;
		case 0x05 : cond.op = GNM_STYLE_COND_GT;		break;
		case 0x06 : cond.op = GNM_STYLE_COND_LT;		break;
		case 0x07 : cond.op = GNM_STYLE_COND_GTE;		break;
		case 0x08 : cond.op = GNM_STYLE_COND_LTE;		break;
		default:
			g_warning ("EXCEL : Unknown condition (%d) for conditional format in sheet %s.",
				   op, esheet->sheet->name_unquoted);
			return;
		}
		break;
	case 2 : cond.op = GNM_STYLE_COND_CUSTOM;
		 break;

	default :
		g_warning ("EXCEL : Unknown condition type (%d) for format in sheet %s.",
			   (int)type, esheet->sheet->name_unquoted);
		return;
	}

	cond.texpr[0] = (expr0_len <= 0)
		? NULL
		: ms_sheet_parse_expr_internal
			(esheet,
			 q->data + q->length - expr0_len - expr1_len,
			 expr0_len);
	cond.texpr[1] = (expr1_len <= 0)
		? NULL
		: ms_sheet_parse_expr_internal
			(esheet,
			 q->data + q->length - expr1_len,
			 expr1_len);

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
	 *	uint8 : 0x3f == no background elements,
	 *		0x3b == background
	 *		0x3a == background & pattern
	 *		0x38 == background & pattern & pattern colour
	 *	uint8 : 0x04 = font | 0x10 = border | 0x20 = colour
	 *	0x02 : ?
	 *	0x00 : ?
	 *
	 * font   == 118
	 * border == 8
	 * colour == 4
	 *	Similar to XF from biff7
	 */

	cond.overlay = gnm_style_new ();

	offset =  6  /* CF record header */ + 6; /* format header */
	if (flags & 0x04000000) { /* font */
		guint32 size, colour;
		guint8  tmp8, font_flags;
		guint8 const *data = q->data + offset + 64;

		if (0xFFFFFFFF != (size = GSF_LE_GET_GUINT32 (data)))
			gnm_style_set_font_size	(cond.overlay, size / 20.);
		if (0xFFFFFFFF != (colour = GSF_LE_GET_GUINT32 (data + 16)))
			gnm_style_set_font_color (cond.overlay,
				excel_palette_get (esheet->container.importer,
					colour));

		tmp8  = GSF_LE_GET_GUINT8  (data + 4);
		font_flags = GSF_LE_GET_GUINT8  (data + 24);
		if (0 == (font_flags & 2)) {
			gnm_style_set_font_italic (cond.overlay, 0 != (tmp8 & 2));
			gnm_style_set_font_bold   (cond.overlay,
				GSF_LE_GET_GUINT16 (data + 8) >= 0x2bc);
		}
		if (0 == (font_flags & 0x80))
			gnm_style_set_font_strike (cond.overlay, 0 != (tmp8 & 0x80));

		if (0 == GSF_LE_GET_GUINT8  (data + 28)) {
			switch (GSF_LE_GET_GUINT8  (data + 10)) {
			default : g_printerr ("Unknown script %d\n", GSF_LE_GET_GUINT8 (data));
				  /* fall through */
			case 0: gnm_style_set_font_script (cond.overlay, GO_FONT_SCRIPT_STANDARD); break;
			case 1: gnm_style_set_font_script (cond.overlay, GO_FONT_SCRIPT_SUPER); break;
			case 2: gnm_style_set_font_script (cond.overlay, GO_FONT_SCRIPT_SUB); break;
			}
		}
		if (0 == GSF_LE_GET_GUINT8  (data + 32)) {
			switch (GSF_LE_GET_GUINT8  (data + 12)) {
			default :
			case 0:
				gnm_style_set_font_uline  (cond.overlay,
					UNDERLINE_NONE);
				break;
			case 1: case 0x21:
				gnm_style_set_font_uline  (cond.overlay,
					UNDERLINE_SINGLE);
				break;
			case 2: case 0x22:
				gnm_style_set_font_uline  (cond.overlay,
					UNDERLINE_DOUBLE);
				break;
			}
		}

		d (3, {
			puts ("Font");
			gsf_mem_dump (data, 54);
		});

		offset += 118;
	}

	if (flags & 0x10000000) { /* borders */
		guint16 patterns = GSF_LE_GET_GUINT16 (q->data + offset);
		guint32 colours  = GSF_LE_GET_GUINT32 (q->data + offset + 2);
		if (0 == (flags & 0x0400))
			excel_read_CF_border (&cond, esheet, GNM_STYLE_BORDER_LEFT,
				(patterns >>  0) & 0xf,
				(colours  >>  0) & 0x7f);
		if (0 == (flags & 0x0800))
			excel_read_CF_border (&cond, esheet, GNM_STYLE_BORDER_RIGHT,
				(patterns >>  4) & 0xf,
				(colours  >>  7) & 0x7f);
		if (0 == (flags & 0x1000))
			excel_read_CF_border (&cond, esheet, GNM_STYLE_BORDER_TOP,
				(patterns >>  8) & 0xf,
				(colours  >> 16) & 0x7f);
		if (0 == (flags & 0x2000))
			excel_read_CF_border (&cond, esheet, GNM_STYLE_BORDER_BOTTOM,
				(patterns >> 12) & 0xf,
				(colours  >> 23) & 0x7f);

		/* I wonder what the last two bytes are.  future growth ? */
		offset += 8;
	}

	if (flags & 0x20000000) { /* pattern */
		guint32 background_flags = GSF_LE_GET_GUINT32 (q->data + offset);
		int pattern = 0;

		if (0 == (flags & 0x10000))
			gnm_style_set_pattern (cond.overlay,
				pattern = excel_map_pattern_index_from_excel (
					(background_flags >> 10) & 0x3F));
		if (0 == (flags & 0x20000))
			gnm_style_set_pattern_color (cond.overlay,
				excel_palette_get (esheet->container.importer,
					(background_flags >> 16) & 0x7F));
		if (0 == (flags & 0x40000))
			gnm_style_set_back_color (cond.overlay,
				excel_palette_get (esheet->container.importer,
					(background_flags >> 23) & 0x7F));

		offset += 4;
	}

	XL_CHECK_CONDITION (q->length == offset + expr0_len + expr1_len);

	d (1, gnm_style_dump (cond.overlay););

	gnm_style_conditions_insert (sc, &cond, -1);
}

static void
excel_read_CONDFMT (BiffQuery *q, ExcelReadSheet *esheet)
{
	guint16 num_fmts, num_areas;
	unsigned i;
	guint8 const *data;
	GnmStyleConditions *sc;
	GnmStyle *style;
	GnmRange  region;
	GSList   *ptr, *regions = NULL;

	XL_CHECK_CONDITION (q->length >= 14);

	num_fmts = GSF_LE_GET_GUINT16 (q->data + 0);
	num_areas = GSF_LE_GET_GUINT16 (q->data + 12);

	d (1, fprintf (stderr,"Num areas == %hu\n", num_areas););
#if 0
	/* The bounding box or the region containing all conditional formats.
	 * It seems like this region is 0,0 -> 0xffff,0xffff when there are no
	 * regions.
	 */
	if (num_areas > 0)
		excel_read_range (&region, q->data+4);
#endif

	data = q->data + 14;
	for (i = 0 ; i < num_areas && (data+8) <= (q->data + q->length) ; i++) {
		data = excel_read_range (&region, data);
		regions = g_slist_prepend (regions, range_dup (&region));
	}

	XL_CHECK_CONDITION (data == q->data + q->length);

	sc = gnm_style_conditions_new ();
	for (i = 0 ; i < num_fmts ; i++) {
		guint16 next;
		if (!ms_biff_query_peek_next (q, &next) || next != BIFF_CF) {
			g_warning ("EXCEL: missing CF record");
			return;
		}
		ms_biff_query_next (q);
		excel_read_CF (q, esheet, sc);
	}

	style = gnm_style_new ();
	gnm_style_set_conditions (style, sc);
	for (ptr = regions ; ptr != NULL ; ptr = ptr->next) {
		gnm_style_ref (style);
		sheet_style_apply_range	(esheet->sheet, ptr->data, style);
		g_free (ptr->data);
	}
	gnm_style_unref (style);
	g_slist_free (regions);
}

static void
excel_read_DV (BiffQuery *q, ExcelReadSheet *esheet)
{
	GnmExprTop const *texpr1 = NULL;
	GnmExprTop const *texpr2 = NULL;
	int		 expr1_len,     expr2_len;
	char *input_msg, *error_msg, *input_title, *error_title;
	guint32	options, len;
	guint8 const *data, *expr1_dat, *expr2_dat;
	guint8 const *end = q->data + q->length;
	int i, col, row;
	GnmRange r;
	ValidationStyle style;
	ValidationType  type;
	ValidationOp    op;
	GSList *ptr, *ranges = NULL;
	GnmStyle *mstyle;

	XL_CHECK_CONDITION (q->length >= 4);
	options	= GSF_LE_GET_GUINT32 (q->data);
	data = q->data + 4;

	XL_CHECK_CONDITION (data+3 <= end);
	input_title = excel_get_text_fixme (esheet->container.importer, data + 2,
		GSF_LE_GET_GUINT16 (data), &len);
	data += len + 2;

	XL_CHECK_CONDITION (data+3 <= end);
	error_title = excel_get_text_fixme (esheet->container.importer, data + 2,
		GSF_LE_GET_GUINT16 (data), &len);
	data += len + 2;

	XL_CHECK_CONDITION (data+3 <= end);
	input_msg = excel_get_text_fixme (esheet->container.importer, data + 2,
		GSF_LE_GET_GUINT16 (data), &len);
	data += len + 2;

	XL_CHECK_CONDITION (data+3 <= end);
	error_msg = excel_get_text_fixme (esheet->container.importer, data + 2,
		GSF_LE_GET_GUINT16 (data), &len);
	data += len + 2;

	d (1, {
		fprintf (stderr,"Input Title : '%s'\n", input_title);
		fprintf (stderr,"Input Msg   : '%s'\n", input_msg);
		fprintf (stderr,"Error Title : '%s'\n", error_title);
		fprintf (stderr,"Error Msg   : '%s'\n", error_msg);
	});

	XL_CHECK_CONDITION (data+2 <= end);
	expr1_len = GSF_LE_GET_GUINT16 (data);
	d (5, fprintf (stderr,"Unknown1 = %hx\n", GSF_LE_GET_GUINT16 (data+2)););
	expr1_dat = data  + 4;	/* TODO : What are the missing 2 bytes ? */
	data += expr1_len + 4;

	XL_CHECK_CONDITION (data+2 <= end);
	expr2_len = GSF_LE_GET_GUINT16 (data);
	d (5, fprintf (stderr,"Unknown2 = %hx\n", GSF_LE_GET_GUINT16 (data+2)););
	expr2_dat = data  + 4;	/* TODO : What are the missing 2 bytes ? */
	data += expr2_len + 4;

	XL_CHECK_CONDITION (data+2 < end);
	i = GSF_LE_GET_GUINT16 (data);
	for (data += 2; i-- > 0 ;) {
		XL_CHECK_CONDITION (data+8 <= end);
		data = excel_read_range (&r, data);
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
		g_warning ("EXCEL : Unknown constraint type %d", options & 0x0f);
		return;
	}

	switch ((options >> 4) & 0x07) {
	case 0 : style = VALIDATION_STYLE_STOP; break;
	case 1 : style = VALIDATION_STYLE_WARNING; break;
	case 2 : style = VALIDATION_STYLE_INFO; break;
	default :
		g_warning ("EXCEL : Unknown validation style %d",
			   (options >> 4) & 0x07);
		return;
	}
	if (!(options & 0x80000))
		style = VALIDATION_STYLE_NONE;

	if (type == VALIDATION_TYPE_CUSTOM || type == VALIDATION_TYPE_IN_LIST)
		op = VALIDATION_OP_NONE;
	else
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
			g_warning ("EXCEL : Unknown constraint operator %d",
				   (options >> 20) & 0x0f);
			return;
		}

	if (ranges != NULL) {
		GnmRange const *r = ranges->data;
		col = r->start.col;
		row = r->start.row;
	} else
		col = row = 0;

	if (expr1_len > 0)
		texpr1 = excel_parse_formula (&esheet->container, esheet,
					      col, row,
					      expr1_dat, expr1_len, 0 /* FIXME */,
					      TRUE, NULL);

	if (expr2_len > 0)
		texpr2 = excel_parse_formula (&esheet->container, esheet,
					      col, row,
					      expr2_dat, expr2_len, 0 /* FIXME */,
					      TRUE, NULL);

	d (1, fprintf (stderr,"style = %d, type = %d, op = %d\n",
		       style, type, op););

	mstyle = gnm_style_new ();
	gnm_style_set_validation
		(mstyle,
		 validation_new (style, type, op, error_title, error_msg,
				 texpr1,
				 texpr2,
				 options & 0x0100, 0 == (options & 0x0200)));
	if (options & 0x40000)
		gnm_style_set_input_msg (mstyle,
			gnm_input_msg_new (input_msg, input_title));

	for (ptr = ranges; ptr != NULL ; ptr = ptr->next) {
		GnmRange *r = ptr->data;
		gnm_style_ref (mstyle);
		sheet_style_apply_range (esheet->sheet, r, mstyle);
		d (1, range_dump (r, "\n"););
		g_free (r);
	}
	g_slist_free (ranges);
	gnm_style_unref (mstyle);
	g_free (input_msg);
	g_free (error_msg);
	g_free (input_title);
	g_free (error_title);
}

static void
excel_read_DVAL (BiffQuery *q, ExcelReadSheet *esheet)
{
	guint16 options;
	guint32 input_coord_x, input_coord_y, drop_down_id, dv_count;
	unsigned i;

	XL_CHECK_CONDITION (q->length == 18);

	options	      = GSF_LE_GET_GUINT16 (q->data + 0);
	input_coord_x = GSF_LE_GET_GUINT32 (q->data + 2);
	input_coord_y = GSF_LE_GET_GUINT32 (q->data + 6);
	drop_down_id  = GSF_LE_GET_GUINT32 (q->data + 10);
	dv_count      = GSF_LE_GET_GUINT32 (q->data + 14);

	d (5, if (options & 0x1) fprintf (stderr,"DV input window is closed"););
	d (5, if (options & 0x2) fprintf (stderr,"DV input window is pinned"););
	d (5, if (options & 0x4) fprintf (stderr,"DV info has been cached ??"););

	for (i = 0 ; i < dv_count ; i++) {
		guint16 next;
		if (!ms_biff_query_peek_next (q, &next) || next != BIFF_DV) {
			g_warning ("EXCEL: missing DV record");
			return;
		}
		ms_biff_query_next (q);
		excel_read_DV (q, esheet);
	}
}

static void
excel_read_SHEETPROTECTION (BiffQuery *q, Sheet *sheet)
{
	guint16 flags;

	g_return_if_fail (sheet != NULL);

	/* 2003/2007	== 23
	 * XP		== 19 */
	XL_CHECK_CONDITION (q->length >= 19);

	if (q->length >= 23) {
		flags = GSF_LE_GET_GUINT16 (q->data + 19);
		sheet->protected_allow.edit_objects		= (flags & (1 << 0))  != 0;
		sheet->protected_allow.edit_scenarios		= (flags & (1 << 1))  != 0;
		sheet->protected_allow.cell_formatting		= (flags & (1 << 2))  != 0;
		sheet->protected_allow.column_formatting	= (flags & (1 << 3))  != 0;
		sheet->protected_allow.row_formatting		= (flags & (1 << 4))  != 0;
		sheet->protected_allow.insert_columns		= (flags & (1 << 5))  != 0;
		sheet->protected_allow.insert_rows		= (flags & (1 << 6))  != 0;
		sheet->protected_allow.insert_hyperlinks	= (flags & (1 << 7))  != 0;
		sheet->protected_allow.delete_columns		= (flags & (1 << 8))  != 0;
		sheet->protected_allow.delete_rows		= (flags & (1 << 9))  != 0;
		sheet->protected_allow.select_locked_cells	= (flags & (1 << 10)) != 0;
		sheet->protected_allow.sort_ranges		= (flags & (1 << 11)) != 0;
		sheet->protected_allow.edit_auto_filters	= (flags & (1 << 12)) != 0;
		sheet->protected_allow.edit_pivottable		= (flags & (1 << 13)) != 0;
		sheet->protected_allow.select_unlocked_cells	= (flags & (1 << 14)) != 0;
	}
}

/***********************************************************************/

static guchar *
read_utf16_str (int word_len, guint8 const *data)
{
	int i;
	gunichar2 *uni_text = g_alloca (word_len * sizeof (gunichar2));

	/* be wary about endianness */
	for (i = 0 ; i < word_len ; i++, data += 2)
		uni_text [i] = GSF_LE_GET_GUINT16 (data);

	return (guchar *)g_utf16_to_utf8 (uni_text, word_len, NULL, NULL, NULL);
}

/*
 * XL (at least XL 2000) stores URLs exactly as input by the user. No
 * quoting, no mime encoding of email headers. If cgi parameters are
 * separated by '&', '&' is stored, not '&amp;'. An email subject in
 * cyrillic characters is stored as cyrillic characters, not as an
 * RFC 2047 MIME encoded header.
 */
static void
excel_read_HLINK (BiffQuery *q, ExcelReadSheet *esheet)
{
	static guint8 const stdlink_guid[] = {
		0xd0, 0xc9, 0xea, 0x79, 0xf9, 0xba, 0xce, 0x11,
		0x8c, 0x82, 0x00, 0xaa, 0x00, 0x4b, 0xa9, 0x0b,
		/* unknown */
		0x02, 0x00, 0x00, 0x00
	};
	static guint8 const url_guid[] = {
		0xe0, 0xc9, 0xea, 0x79, 0xf9, 0xba, 0xce, 0x11,
		0x8c, 0x82, 0x00, 0xaa, 0x00, 0x4b, 0xa9, 0x0b,
	};
	static guint8 const file_guid[] = {
		0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
	};
	GnmRange	r;
	guint32 options, len;
	guint16 next_opcode;
	guint8 const *data = q->data;
	guchar *label = NULL;
	guchar *target_base = NULL;
	guchar *mark = NULL;
	guchar *tip = NULL;
	GnmHLink *link = NULL;

	XL_CHECK_CONDITION (q->length > 32);

	r.start.row = GSF_LE_GET_GUINT16 (data +  0);
	r.end.row   = GSF_LE_GET_GUINT16 (data +  2);
	r.start.col = GSF_LE_GET_GUINT16 (data +  4);
	r.end.col   = GSF_LE_GET_GUINT16 (data +  6);
	options     = GSF_LE_GET_GUINT32 (data + 28);

	XL_CHECK_CONDITION (!memcmp (data + 8, stdlink_guid, sizeof (stdlink_guid)));

	data += 32;

	d (1, {
		range_dump (&r, "");
		fprintf (stderr, " = hlink options(0x%04x)\n", options);
	});

	if ((options & 0x14) == 0x14) {			/* label */
		XL_NEED_ITEMS (1, 4);
		len = GSF_LE_GET_GUINT32 (data);
		data += 4;
		XL_NEED_ITEMS (len, 2);
		label = read_utf16_str (len, data);
		data += len*2;
		d (1, fprintf (stderr, "label = %s\n", label););
	}

	if (options & 0x80) {				/* target_base */
		XL_NEED_ITEMS (1, 4);
		len = GSF_LE_GET_GUINT32 (data);
		data += 4;
		XL_NEED_ITEMS (len, 2);
		target_base = read_utf16_str (len, data);
		data += len*2;
		d (1, fprintf (stderr, "target_base = %s\n", target_base););
	}

	if (options & 0x8) {				/* 'text mark' */
		XL_NEED_ITEMS (1, 4);
		len = GSF_LE_GET_GUINT32 (data);
		data += 4;

		XL_NEED_ITEMS (len, 2);
		mark = read_utf16_str (len, data);
		data += len*2;
		d (1, fprintf (stderr, "mark = %s\n", mark););
	}

	if ((options & 0x163) == 0x003 && !memcmp (data, url_guid, sizeof (url_guid))) {
		guchar *url;

		data += sizeof (url_guid);
		XL_NEED_ITEMS (1, 4);
		len = GSF_LE_GET_GUINT32 (data);
		data += 4;

		XL_NEED_BYTES (len);
		url = read_utf16_str (len/2, data);
		if (NULL != url && 0 == g_ascii_strncasecmp (url,  "mailto:", 7))
			link = g_object_new (gnm_hlink_email_get_type (), NULL);
		else
			link = g_object_new (gnm_hlink_url_get_type (), NULL);
		gnm_hlink_set_target (link, url);
		g_free (url);

	/* File link */
	} else if ((options & 0x1e1) == 0x001 && !memcmp (data, file_guid, sizeof (file_guid))) {
		guchar  *path;
		GString *accum;
		int up;

		data += sizeof (file_guid);

		XL_NEED_BYTES (6);
		up  = GSF_LE_GET_GUINT16 (data + 0);
		len = GSF_LE_GET_GUINT32 (data + 2);
		d (1, fprintf (stderr,"# leading ../ %d len 0x%04x\n",
				 up, len););
		data += 6;

		XL_NEED_BYTES (len);
		data += len;

		XL_NEED_BYTES (16 + 12 + 6);
		data += 16 + 12;
		len = GSF_LE_GET_GUINT32 (data);
		data += 6;

		XL_NEED_BYTES (len);
		path = read_utf16_str (len/2, data);
		accum = g_string_new (NULL);
		while (up-- > 0)
			g_string_append (accum, "..\\");
		g_string_append (accum, path);
		g_free (path);
		link = g_object_new (gnm_hlink_external_get_type (), NULL);
		gnm_hlink_set_target (link, accum->str);
		g_string_free (accum, TRUE);

	/* UNC File link */
	} else if ((options & 0x1e3) == 0x103) {
		guchar  *path;

		XL_NEED_ITEMS (1, 4);
		len = GSF_LE_GET_GUINT32 (data);
		data += 4;

		XL_NEED_BYTES (len);
		path = read_utf16_str (len/2, data);
		link = g_object_new (gnm_hlink_external_get_type (), NULL);
		gnm_hlink_set_target (link, path);
		g_free (path);

	} else if ((options & 0x1eb) == 0x008) {
		link = g_object_new (gnm_hlink_cur_wb_get_type (), NULL);
		gnm_hlink_set_target (link, mark);
	} else {
		g_warning ("Unknown hlink type 0x%x", options);
	}

	if (ms_biff_query_peek_next (q, &next_opcode) &&
	    next_opcode == BIFF_LINK_TIP) {
		ms_biff_query_next (q);
		/* according to OO the bytes 2..10 are the range for the tip */
		tip = read_utf16_str ((q->length - 10)/ 2, q->data + 10);
	}

	if (link != NULL) {
		GnmStyle *style = gnm_style_new ();
		gnm_style_set_hlink (style, link);
		sheet_style_apply_range	(esheet->sheet, &r, style);
		if (tip != NULL)
			gnm_hlink_set_tip  (link, tip);
	}

	g_free (tip);
	g_free (mark);
	g_free (target_base);
	g_free (label);
}

static void
excel_read_CODENAME (BiffQuery *q, GnmXLImporter *importer, ExcelReadSheet *esheet)
{
	char *codename;
	GObject *obj;

	XL_CHECK_CONDITION (q->length >= 2);

	codename = excel_biff_text_2 (importer, q, 0);
	obj = esheet ? G_OBJECT (esheet->sheet) : G_OBJECT (importer->wb);
	g_object_set_data_full (obj, CODENAME_KEY, codename, g_free);
}

static void
excel_read_BG_PIC (BiffQuery *q,
		   ExcelReadSheet *esheet)
{
	/* undocumented, looks similar to IMDATA */
	GdkPixbuf *background = excel_read_IMDATA (q, TRUE);
	if (background != NULL)
		g_object_unref (background);
}

static GnmValue *
read_DOPER (guint8 const *doper, gboolean is_equal,
	    unsigned *str_len, GnmFilterOp *op)
{
	static GnmFilterOp const ops [] = {
		GNM_FILTER_OP_LT,
		GNM_FILTER_OP_EQUAL,
		GNM_FILTER_OP_LTE,
		GNM_FILTER_OP_GT,
		GNM_FILTER_OP_NOT_EQUAL,
		GNM_FILTER_OP_GTE
	};
	GnmValue *res = NULL;

	*str_len = 0;
	*op = GNM_FILTER_UNUSED;
	switch (doper[0]) {
	case 0: return NULL; /* ignore */

	case 2: res = biff_get_rk (doper + 2);
		break;
	case 4: res = value_new_float (GSF_LE_GET_DOUBLE (doper+2));
		break;
	case 6: *str_len = doper[6];
		break;

	case 8: if (doper[2])
			res = biff_get_error (NULL, doper[3]);
		else
			res = value_new_bool (doper[3] ? TRUE : FALSE);
		break;

	case 0xC: *op = GNM_FILTER_OP_BLANKS;
		return NULL;
	case 0xE: *op = GNM_FILTER_OP_NON_BLANKS;
		return NULL;
	}

	g_return_val_if_fail (doper[1] > 0 && doper[1] <=6, NULL);
	*op = ops [doper[1] - 1];

	return res;
}

static void
excel_read_AUTOFILTER (BiffQuery *q, ExcelReadSheet *esheet)
{
	guint16 flags;
	GnmFilterCondition *cond = NULL;
	GnmFilter *filter;

	/* XL only supports 1 filter per sheet */
	g_return_if_fail (esheet->sheet->filters != NULL);
	g_return_if_fail (esheet->sheet->filters->data != NULL);
	g_return_if_fail (esheet->sheet->filters->next == NULL);

	XL_CHECK_CONDITION (q->length >= 4);
	flags = GSF_LE_GET_GUINT16 (q->data + 2);

	filter = esheet->sheet->filters->data;

	if (esheet_ver (esheet) >= MS_BIFF_V8 && flags & 0x10)
		/* it's a top/bottom n */
		cond = gnm_filter_condition_new_bucket (
			    (flags & 0x20) ? TRUE  : FALSE,
			    (flags & 0x40) ? FALSE : TRUE,
			    (flags >> 7) & 0x1ff);

	if (cond == NULL) {
		unsigned     len0, len1;
		GnmFilterOp  op0,  op1;
		guint8 const *data;
		GnmValue *v0, *v1;

		XL_CHECK_CONDITION (q->length >= 24);
		v0 = read_DOPER (q->data + 4,  flags & 4, &len0, &op0);
		v1 = read_DOPER (q->data + 14, flags & 8, &len1, &op1);

		data = q->data + 24;
		if (len0 > 0) {
			v0 = value_new_string_nocopy (
				excel_get_text_fixme (esheet->container.importer, data, len0, NULL));
			data += len0;
		}
		if (len1 > 0)
			v1 = value_new_string_nocopy (
				excel_get_text_fixme (esheet->container.importer, data, len1, NULL));

		if (op1 == GNM_FILTER_UNUSED) {
			cond = gnm_filter_condition_new_single (op0, v0);
			if (v1 != NULL) value_release (v1); /* paranoia */
		} else {
			/* NOTE : Docs are backwards */
			cond = gnm_filter_condition_new_double (
				    op0, v0, (flags & 3) ? FALSE : TRUE, op1, v1);
		}
	}

	gnm_filter_set_condition (filter,
		GSF_LE_GET_GUINT16 (q->data), cond, FALSE);
}

void
excel_read_SCL (BiffQuery *q, Sheet *sheet)
{
	unsigned num, denom;

	XL_CHECK_CONDITION (q->length == 4);

	num = GSF_LE_GET_GUINT16 (q->data);
	denom = GSF_LE_GET_GUINT16 (q->data + 2);

	XL_CHECK_CONDITION (denom != 0);

	g_object_set (sheet, "zoom-factor", num / (double)denom, NULL);
}

/**
 * excel_externsheet_v8 :
 *
 * WARNING WARNING WARNING
 *
 * This function can and will return intentionally INVALID pointers in some
 * cases.  You need to check for (Sheet *)1
 * It should only happen for external names to deal with 'reference self'
 * supbook entries.  However, you never know.
 *
 * you also need to check for (Sheet *)2 which indicates deleted
 *
 * WARNING WARNING WARNING
 **/
ExcelExternSheetV8 const *
excel_externsheet_v8 (GnmXLImporter const *importer, guint16 i)
{
	d (2, fprintf (stderr, "externv8 %hd\n", i););

	g_return_val_if_fail (importer->v8.externsheet != NULL, NULL);

	if (i >= importer->v8.externsheet->len) {
		g_warning ("%hd >= %u\n", i, importer->v8.externsheet->len);
		return NULL;
	}

	return &(g_array_index (importer->v8.externsheet, ExcelExternSheetV8, i));
}

static Sheet *
supbook_get_sheet (GnmXLImporter *importer, gint16 sup_index, unsigned i)
{
	Sheet *sheet = NULL;

	if (sup_index < 0) {
		g_warning ("external references not supported yet.");
		return NULL;
	}

	/* 0xffff == deleted */
	if (i >= 0xffff)
		return (Sheet *)2; /* magic value */

	/* WARNING : 0xfffe record for local names kludge a solution */
	if (i == 0xfffe)
		return (Sheet *)1; /* magic value */

	g_return_val_if_fail ((unsigned)sup_index < importer->v8.supbook->len, NULL);

	/* supbook was self referential */
	if (g_array_index (importer->v8.supbook, ExcelSupBook, sup_index).type == EXCEL_SUP_BOOK_SELFREF) {
		g_return_val_if_fail (i < importer->boundsheet_sheet_by_index->len, NULL);
		sheet = g_ptr_array_index (importer->boundsheet_sheet_by_index, i);
		g_return_val_if_fail (IS_SHEET (sheet), NULL);
	} else {
		/* supbook was an external reference */
	}

	return sheet;
}

static void
excel_read_EXTERNSHEET_v8 (BiffQuery const *q, GnmXLImporter *importer)
{
	ExcelExternSheetV8 *v8;
	gint16 sup_index;
	unsigned i, num, first, last;

	XL_CHECK_CONDITION (importer->ver >= MS_BIFF_V8);
	g_return_if_fail (importer->v8.externsheet == NULL);

	XL_CHECK_CONDITION (q->length >= 2);
	num = GSF_LE_GET_GUINT16 (q->data);
	XL_CHECK_CONDITION (q->length >= 2 + num * 6);

	d (2, fprintf (stderr,"ExternSheet (%d entries)\n", num););
	d (10, gsf_mem_dump (q->data, q->length););

	importer->v8.externsheet = g_array_set_size (
		g_array_new (FALSE, FALSE, sizeof (ExcelExternSheetV8)), num);

	for (i = 0; i < num; i++) {
		sup_index = GSF_LE_GET_GINT16 (q->data + 2 + i * 6 + 0);
		first	  = GSF_LE_GET_GUINT16 (q->data + 2 + i * 6 + 2);
		last	  = GSF_LE_GET_GUINT16 (q->data + 2 + i * 6 + 4);

		d (2, fprintf (stderr,"ExternSheet: sup = %hd First sheet 0x%x, Last sheet 0x%x\n",
			      sup_index, first, last););

		v8 = &g_array_index(importer->v8.externsheet, ExcelExternSheetV8, i);
		v8->supbook = sup_index;
		v8->first = supbook_get_sheet (importer, sup_index, first);
		v8->last  = supbook_get_sheet (importer, sup_index, last);
		d (2, fprintf (stderr,"\tFirst sheet %p, Last sheet %p\n",
			      v8->first, v8->last););
	}
}

/**
 * excel_externsheet_v7 :
 *
 **/
Sheet *
excel_externsheet_v7 (MSContainer const *container, gint16 idx)
{
	GPtrArray const *externsheets;

	d (2, fprintf (stderr, "externv7 %hd\n", idx););

	externsheets = container->v7.externsheets;
	g_return_val_if_fail (externsheets != NULL, NULL);
	g_return_val_if_fail (idx > 0, NULL);
	g_return_val_if_fail (idx <= (int)externsheets->len, NULL);

	return g_ptr_array_index (externsheets, idx-1);
}

void
excel_read_EXTERNSHEET_v7 (BiffQuery const *q, MSContainer *container)
{
	Sheet *sheet = NULL;
	guint8 type;

	XL_CHECK_CONDITION (q->length >= 2);

	type = GSF_LE_GET_GUINT8 (q->data + 1);

	d (1, {
	   fprintf (stderr,"extern v7 %p\n", container);
	   gsf_mem_dump (q->data, q->length); });

	switch (type) {
	case 2: sheet = ms_container_sheet (container);
		if (sheet == NULL)
			g_warning ("What does this mean ?");
		break;

	/* Type 3 is undocumented magic.  It is used to forward declare sheet
	 * names in the current workbook */
	case 3: {
		unsigned len = GSF_LE_GET_GUINT8 (q->data);
		char *name;

		/* opencalc screws up its export, overstating
		 * the length by 1 */
		if (len + 2 > q->length)
			len = q->length - 2;

		name = excel_biff_text (container->importer, q, 2, len);

		if (name != NULL) {
			sheet = workbook_sheet_by_name (container->importer->wb, name);
			if (sheet == NULL) {
				/* There was a bug in 1.0.x export that spewed the quoted name
				 * includling internal backquoting */
				if (name[0] == '\'') {
					GString *fixed = g_string_new (NULL);
					if (NULL != go_strunescape (fixed, name) &&
					    NULL != (sheet = workbook_sheet_by_name (container->importer->wb, fixed->str))) {
						g_free (name);
						name = g_string_free (fixed, FALSE);
					} else
						g_string_free (fixed, TRUE);
				}

				if (sheet == NULL) {
					sheet = sheet_new (container->importer->wb, name);
					workbook_sheet_attach (container->importer->wb, sheet);
				}
			}
			g_free (name);
		}
		break;
	}
	case 4: /* undocumented.  Seems to be used as a placeholder for names */
		sheet = (Sheet *)1;
		break;

	case 0x3a : /* undocumented magic.  seems to indicate the sheet for an
		     * addin with functions.  01 3a
		     * the same as SUPBOOK
		     */
		if (*q->data == 1 && q->length == 2)
			break;

	default:
		/* Fix when we get placeholders to external workbooks */
		gsf_mem_dump (q->data, q->length);
		gnm_io_warning_unsupported_feature (container->importer->context,
			_("external references"));
	}

	if (container->v7.externsheets == NULL)
		container->v7.externsheets = g_ptr_array_new ();
	g_ptr_array_add (container->v7.externsheets, sheet);
}

static void
excel_read_EXTERNSHEET (BiffQuery const *q, GnmXLImporter *importer,
			const MsBiffBofData *ver)
{
	XL_CHECK_CONDITION (ver != NULL);

	if (ver->version >= MS_BIFF_V8)
		excel_read_EXTERNSHEET_v8 (q, importer);
	else
		excel_read_EXTERNSHEET_v7 (q, &importer->container);
}


/* FILEPASS, ask the user for a password if necessary
 * return value is an error string, or NULL for success
 */
static char *
excel_read_FILEPASS (BiffQuery *q, GnmXLImporter *importer)
{
	/* files with workbook protection are encrypted using a
	 * static password (why ?? ). */
	if (ms_biff_query_set_decrypt(q, importer->ver, "VelvetSweatshop"))
		return NULL;

	while (TRUE) {
		guint8 *passwd = go_cmd_context_get_password (
			GO_CMD_CONTEXT (importer->context),
			go_doc_get_uri (GO_DOC (importer->wb)));
		gboolean ok;

		if (passwd == NULL)
			return _("No password supplied");

		ok = ms_biff_query_set_decrypt (q, importer->ver, passwd);
		go_destroy_password (passwd);
		g_free (passwd);
		if (ok)
			return NULL;
	}
}

static void
excel_read_LABEL (BiffQuery *q, ExcelReadSheet *esheet, gboolean has_markup)
{
	GnmValue *v;
	guint in_len, str_len;
	gchar *txt;
	GnmCell *cell = excel_cell_fetch (q, esheet);

	if (!cell)
		return;

	XL_CHECK_CONDITION (q->length >= 8);
	in_len = (q->opcode == BIFF_LABEL_v0)
		? GSF_LE_GET_GUINT8 (q->data + 7)
		: GSF_LE_GET_GUINT16 (q->data + 6);
	XL_CHECK_CONDITION (q->length - 8 >= in_len);

	txt = excel_get_text_fixme (esheet->container.importer, q->data + 8,
		in_len, &str_len);

	d (0, fprintf (stderr,"%s in %s;\n",
		       has_markup ? "formatted string" : "string",
		       cell_name (cell)););

	excel_set_xf (esheet, q);
	if (txt != NULL) {
		GOFormat *fmt = NULL;
		if (has_markup)
			fmt = excel_read_LABEL_markup (q, esheet,
						       txt, str_len);

		/* might free txt, do not do this until after parsing markup */
		v = value_new_string_nocopy (txt);
		if (fmt != NULL) {
			value_set_fmt (v, fmt);
			go_format_unref (fmt);
		}
		gnm_cell_set_value (cell, v);
	}
}

static void
excel_read_BOOLERR (BiffQuery *q, ExcelReadSheet *esheet)
{
	unsigned base = (q->opcode == BIFF_BOOLERR_v0) ? 7 : 6;
	GnmValue *v;

	XL_CHECK_CONDITION (q->length >= base + 2);

	if (GSF_LE_GET_GUINT8 (q->data + base + 1)) {
		GnmEvalPos ep;
		eval_pos_init (&ep, esheet->sheet, XL_GETCOL (q), XL_GETROW (q));
		v = biff_get_error (&ep, GSF_LE_GET_GUINT8 (q->data + base));
	} else
		v = value_new_bool (GSF_LE_GET_GUINT8 (q->data + base));
	excel_sheet_insert_val (esheet, q, v);
}

/* hands the peculiar ampersand escaping, and returns the string _after_
 * &<target> to the end of the buffer.  It also stores \0 in place
 * of &<target>.  If not found return NULL and do nothing */
static char *
xl_hf_strstr (char *buf, char target)
{
	if (buf == NULL)
		return NULL;

	for (; *buf ; buf++)
		if (*buf == '&') {
			if (0 == buf[1])
				return NULL;
			if (target == buf[1]) {
				buf[0] = buf[1] = '\0';
				return buf + 2;
			}
			if ('&' == buf[1])
				buf++;
		}
	return NULL;
}

static void
excel_read_HEADER_FOOTER (GnmXLImporter const *importer,
			  BiffQuery *q, ExcelReadSheet *esheet,
			  gboolean is_header)
{
	PrintInformation *pi = 	esheet->sheet->print_info;

	if (q->length) {
		char *l, *c, *r, *str;

		if (importer->ver >= MS_BIFF_V8) {
			XL_CHECK_CONDITION (q->length >= 3);
			str = excel_biff_text_2 (importer, q, 0);
		} else {
			XL_CHECK_CONDITION (q->length >= 2);
			str = excel_biff_text_1 (importer, q, 0);
		}
		d (2, fprintf (stderr,"%s == '%s'\n", is_header ? "header" : "footer", str););

		r = xl_hf_strstr (str, 'R'); /* order is important */
		c = xl_hf_strstr (str, 'C');
		l = xl_hf_strstr (str, 'L');
		if (is_header) {
			if (pi->header != NULL)
				print_hf_free (pi->header);
			pi->header = print_hf_new (l, c, r);
		} else {
			if (pi->footer != NULL)
				print_hf_free (pi->footer);
			pi->footer = print_hf_new (l, c, r);
		}

		g_free (str);
	}
}

static void
excel_read_REFMODE (BiffQuery *q, ExcelReadSheet *esheet)
{
	guint16 mode;

	XL_CHECK_CONDITION (q->length >= 2);
	mode = GSF_LE_GET_GUINT16 (q->data);
	g_object_set (esheet->sheet, "use-r1c1", mode == 0, NULL);
}

static void
excel_read_PAGE_BREAK (BiffQuery *q, ExcelReadSheet *esheet, gboolean is_vert)
{
	unsigned i;
	unsigned step = (esheet_ver (esheet) >= MS_BIFF_V8) ? 6 : 2;
	guint16 count;
	GnmPageBreaks *breaks;

	XL_CHECK_CONDITION (q->length >= 2);
	count = GSF_LE_GET_GUINT16 (q->data);
	XL_CHECK_CONDITION (q->length >= 2 + count * step);
	breaks = gnm_page_breaks_new (count, is_vert);

	/* 1) Ignore the first/last info for >= biff8
	 * 2) Assume breaks are manual in the absence of any information  */
	for (i = 0; i < count ; i++) {
		gnm_page_breaks_append_break (breaks,
			GSF_LE_GET_GUINT16 (q->data + 2 + i*step), GNM_PAGE_BREAK_MANUAL);
#if 0
		g_printerr ("%d  %d:%d\n",
			    GSF_LE_GET_GUINT16 (q->data + 2 + i*step),
			    GSF_LE_GET_GUINT16 (q->data + 2 + i*step + 2),
			    GSF_LE_GET_GUINT16 (q->data + 2 + i*step + 4));
#endif
	}
	print_info_set_breaks (esheet->sheet->print_info, breaks);
}

static void
excel_read_INTEGER (BiffQuery *q, ExcelReadSheet *esheet)
{
	XL_CHECK_CONDITION (q->length >= 7 + 2);
	excel_sheet_insert_val
		(esheet, q,
		 value_new_int (GSF_LE_GET_GUINT16 (q->data + 7)));
}

static void
excel_read_NUMBER (BiffQuery *q, ExcelReadSheet *esheet, size_t ofs)
{
	XL_CHECK_CONDITION (q->length >= ofs + 8);
	excel_sheet_insert_val
		(esheet, q,
		 value_new_float (gsf_le_get_double (q->data + ofs)));
}

static void
excel_read_MARGIN (BiffQuery *q, ExcelReadSheet *esheet)
{
	double m;
	PrintInformation *pi = esheet->sheet->print_info;

	XL_CHECK_CONDITION (q->length >= 8);
	m = GO_IN_TO_PT (gsf_le_get_double (q->data));

	switch (q->opcode) {
	case BIFF_LEFT_MARGIN: print_info_set_margin_left (pi, m); break;
	case BIFF_RIGHT_MARGIN: print_info_set_margin_right (pi, m); break;
	case BIFF_TOP_MARGIN: print_info_set_edge_to_below_header (pi, m); break;
	case BIFF_BOTTOM_MARGIN: print_info_set_edge_to_above_footer (pi, m); break;
	default: g_assert_not_reached ();
	}
}


static void
excel_read_PRINTHEADERS (BiffQuery *q, ExcelReadSheet *esheet)
{
	XL_CHECK_CONDITION (q->length >= 2);
	esheet->sheet->print_info->print_titles =
		(GSF_LE_GET_GUINT16 (q->data) == 1);
}

static void
excel_read_PRINTGRIDLINES (BiffQuery *q, ExcelReadSheet *esheet)
{
	XL_CHECK_CONDITION (q->length >= 2);
	esheet->sheet->print_info->print_grid_lines =
		(GSF_LE_GET_GUINT16 (q->data) == 1);
}

static void
excel_read_HCENTER (BiffQuery *q, ExcelReadSheet *esheet)
{
	XL_CHECK_CONDITION (q->length >= 2);
	esheet->sheet->print_info->center_horizontally =
		GSF_LE_GET_GUINT16 (q->data) == 0x1;
}

static void
excel_read_VCENTER (BiffQuery *q, ExcelReadSheet *esheet)
{
	XL_CHECK_CONDITION (q->length >= 2);
	esheet->sheet->print_info->center_vertically =
		GSF_LE_GET_GUINT16 (q->data) == 0x1;
}

static void
excel_read_PLS (BiffQuery *q, ExcelReadSheet *esheet)
{
	XL_CHECK_CONDITION (q->length >= 2);
	if (GSF_LE_GET_GUINT16 (q->data) == 0x00) {
		/*
		 * q->data + 2 -> q->data + q->length
		 * map to a DEVMODE structure see MS' SDK.
		 */
	} else if (GSF_LE_GET_GUINT16 (q->data) == 0x01) {
		/*
		 * q's data maps to a TPrint structure
		 * see Inside Macintosh Vol II p 149.
		 */
	}
}

static void
excel_read_RK (BiffQuery *q, ExcelReadSheet *esheet)
{
	XL_CHECK_CONDITION (q->length >= 6 + 4);
	excel_sheet_insert_val (esheet, q,
				biff_get_rk (q->data + 6));
}

static void
excel_read_LABELSST (BiffQuery *q, ExcelReadSheet *esheet)
{
	unsigned i;

	XL_CHECK_CONDITION (q->length >= 6 + 4);
	i = GSF_LE_GET_GUINT32 (q->data + 6);

	if (esheet->container.importer->sst && i < esheet->container.importer->sst_len) {
		GnmValue *v;
		GnmString *str = esheet->container.importer->sst[i].content;
		/* ? Why would there be a NULL ? */
		if (NULL != str) {
			gnm_string_ref (str);
			v = value_new_string_str (str);
		} else
			v = value_new_string ("");
		if (esheet->container.importer->sst[i].markup != NULL)
			value_set_fmt (v, esheet->container.importer->sst[i].markup);
		excel_sheet_insert_val (esheet, q, v);
	} else
		g_warning ("string index 0x%u >= 0x%x\n",
			   i, esheet->container.importer->sst_len);
}


static gboolean
excel_read_sheet (BiffQuery *q, GnmXLImporter *importer,
		  WorkbookView *wb_view, ExcelReadSheet *esheet)
{
	MsBiffVersion const ver = importer->ver;

	g_return_val_if_fail (importer != NULL, FALSE);
	g_return_val_if_fail (esheet != NULL, FALSE);
	g_return_val_if_fail (esheet->sheet != NULL, FALSE);
	g_return_val_if_fail (esheet->sheet->print_info != NULL, FALSE);

	d (1, fprintf (stderr,"----------------- '%s' -------------\n",
		      esheet->sheet->name_unquoted););

	if (ver <= MS_BIFF_V4) {
		/* Style is per-sheet in early Excel - default TODO */
		excel_workbook_reset_style (importer);
	} else {
		/* Apply the default style */
		GnmStyle *mstyle = excel_get_style_from_xf (esheet,
			excel_get_xf (esheet, 15));
		if (mstyle != NULL) {
			GnmRange r;
			sheet_style_set_range (esheet->sheet,
				range_init_full_sheet (&r), mstyle);
		}
	}

	for (; ms_biff_query_next (q) ;
	     value_io_progress_update (importer->context, q->streamPos)) {

		d (5, fprintf (stderr,"Opcode: 0x%x\n", q->opcode););

		switch (q->opcode) {
		case BIFF_DIMENSIONS_v0: break; /* ignore ancient XL2 variant */
		case BIFF_DIMENSIONS_v2: excel_read_DIMENSIONS (q, importer); break;

		case BIFF_BLANK_v0:
		case BIFF_BLANK_v2: excel_set_xf (esheet, q); break;

		case BIFF_INTEGER: excel_read_INTEGER (q, esheet); break;
		case BIFF_NUMBER_v0: excel_read_NUMBER (q, esheet, 7); break;
		case BIFF_NUMBER_v2: excel_read_NUMBER (q, esheet, 6); break;

		case BIFF_LABEL_v0:
		case BIFF_LABEL_v2: excel_read_LABEL (q, esheet, FALSE); break;

		case BIFF_BOOLERR_v0:
		case BIFF_BOOLERR_v2: excel_read_BOOLERR (q, esheet); break;

		case BIFF_FORMULA_v0:
		case BIFF_FORMULA_v2:
		case BIFF_FORMULA_v4:	excel_read_FORMULA (q, esheet);	break;
		/* case STRING : is handled elsewhere since it always follows FORMULA */
		case BIFF_ROW_v0:
		case BIFF_ROW_v2:	excel_read_ROW (q, esheet);	break;
		case BIFF_EOF:		goto success;

		/* NOTE : bytes 12 & 16 appear to require the non decrypted data */
		case BIFF_INDEX_v0:
		case BIFF_INDEX_v2:	break;

		case BIFF_CALCCOUNT:	excel_read_CALCCOUNT (q, importer);	break;
		case BIFF_CALCMODE:	excel_read_CALCMODE (q,importer);	break;

		case BIFF_PRECISION :
#if 0
		{
			/* FIXME: implement in gnumeric */
			/* state of 'Precision as Displayed' option */
			guint16 const data = GSF_LE_GET_GUINT16 (q->data);
			gboolean const prec_as_displayed = (data == 0);
		}
#endif
			break;

		case BIFF_REFMODE:	excel_read_REFMODE (q, esheet); break;
		case BIFF_DELTA:	excel_read_DELTA (q, importer);	break;
		case BIFF_ITERATION:	excel_read_ITERATION (q, importer);	break;
		case BIFF_OBJPROTECT:
		case BIFF_PROTECT:	excel_read_sheet_PROTECT (q, esheet); break;

		case BIFF_PASSWORD:
			if (q->length == 2) {
				d (2, fprintf (stderr,"sheet password '%hx'\n",
					      GSF_LE_GET_GUINT16 (q->data)););
			}
			break;



		case BIFF_HEADER: excel_read_HEADER_FOOTER (importer, q, esheet, TRUE); break;
		case BIFF_FOOTER: excel_read_HEADER_FOOTER (importer, q, esheet, FALSE); break;

		case BIFF_EXTERNCOUNT: /* ignore */ break;
		case BIFF_EXTERNSHEET: /* These cannot be biff8 */
			excel_read_EXTERNSHEET_v7 (q, &esheet->container);
			break;

		case BIFF_VERTICALPAGEBREAKS:	excel_read_PAGE_BREAK (q, esheet, TRUE); break;
		case BIFF_HORIZONTALPAGEBREAKS:	excel_read_PAGE_BREAK (q, esheet, FALSE); break;

		case BIFF_NOTE:		excel_read_NOTE (q, esheet);		break;
		case BIFF_SELECTION:	excel_read_SELECTION (q, esheet);	break;
		case BIFF_EXTERNNAME_v0:
		case BIFF_EXTERNNAME_v2:
			excel_read_EXTERNNAME (q, &esheet->container);
			break;
		case BIFF_DEFAULTROWHEIGHT_v0:
		case BIFF_DEFAULTROWHEIGHT_v2:
			excel_read_DEF_ROW_HEIGHT (q, esheet);
			break;

		case BIFF_LEFT_MARGIN:
		case BIFF_RIGHT_MARGIN:
		case BIFF_TOP_MARGIN:
		case BIFF_BOTTOM_MARGIN:
			excel_read_MARGIN (q, esheet);
			break;

		case BIFF_PRINTHEADERS:
			excel_read_PRINTHEADERS (q, esheet);
			break;

		case BIFF_PRINTGRIDLINES:
			excel_read_PRINTGRIDLINES (q, esheet);
			break;

		case BIFF_WINDOW1:	break; /* what does this do for a sheet ? */
		case BIFF_WINDOW2_v0:
		case BIFF_WINDOW2_v2:
			excel_read_WINDOW2 (q, esheet, wb_view);
			break;
		case BIFF_BACKUP:	break;
		case BIFF_PANE:		excel_read_PANE (q, esheet, wb_view);	 break;

		case BIFF_PLS: excel_read_PLS (q, esheet); break;

		case BIFF_DEFCOLWIDTH:	excel_read_DEF_COL_WIDTH (q, esheet); break;

		case BIFF_OBJ:	ms_read_OBJ (q, sheet_container (esheet), NULL); break;

		case BIFF_SAVERECALC:	break;
		case BIFF_TAB_COLOR:	excel_read_TAB_COLOR (q, esheet);	break;
		case BIFF_COLINFO:	excel_read_COLINFO (q, esheet);		break;

		case BIFF_RK: excel_read_RK (q, esheet); break;

		case BIFF_IMDATA: {
			GdkPixbuf *pixbuf = excel_read_IMDATA (q, FALSE);
			if (pixbuf)
				g_object_unref (pixbuf);
			}
			break;
		case BIFF_GUTS:		excel_read_GUTS (q, esheet);		break;
		case BIFF_WSBOOL:	excel_read_WSBOOL (q, esheet);		break;
		case BIFF_GRIDSET:		break;

		case BIFF_HCENTER: excel_read_HCENTER (q, esheet); break;
		case BIFF_VCENTER: excel_read_VCENTER (q, esheet); break;

		case BIFF_COUNTRY:		break;
		case BIFF_STANDARDWIDTH:	break; /* the 'standard width dialog' ? */
		case BIFF_FILTERMODE:		break;
		case BIFF_AUTOFILTERINFO:	break;
		case BIFF_AUTOFILTER:	excel_read_AUTOFILTER (q, esheet);	break;
		case BIFF_SCL:		excel_read_SCL (q, esheet->sheet);	break;
		case BIFF_SETUP:	excel_read_SETUP (q, esheet);		break;
		case BIFF_GCW:			break;
		case BIFF_SCENMAN:		break;
		case BIFF_SCENARIO:		break;
		case BIFF_MULRK:	excel_read_MULRK (q, esheet);		break;
		case BIFF_MULBLANK:	excel_read_MULBLANK (q, esheet);	break;

		case BIFF_RSTRING:	excel_read_LABEL (q, esheet, TRUE);	break;

		case BIFF_DBCELL: break; /* Can be ignored on read side */

		case BIFF_BG_PIC:	excel_read_BG_PIC (q, esheet);		break;
		case BIFF_MERGECELLS:	excel_read_MERGECELLS (q, esheet);	break;

		case BIFF_MS_O_DRAWING:
		case BIFF_MS_O_DRAWING_GROUP:
		case BIFF_MS_O_DRAWING_SELECTION:
			ms_escher_parse (q, sheet_container (esheet), FALSE);
			break;
		case BIFF_PHONETIC:	break;

		case BIFF_LABELSST:
			excel_read_LABELSST (q, esheet);
			break;

		case BIFF_XF_OLD_v0:
		case BIFF_XF_OLD_v2:
		case BIFF_XF_OLD_v4:
			excel_read_XF_OLD (q, importer);
			break;
		case BIFF_XF_INDEX:
			excel_read_XF_INDEX (q, esheet);
			break;

		case BIFF_NAME_v0:
		case BIFF_NAME_v2:	excel_read_NAME (q, importer, esheet); break;
		case BIFF_FONT_v0:
		case BIFF_FONT_v2:	excel_read_FONT (q, importer);	break;
		case BIFF_FORMAT_v0:
		case BIFF_FORMAT_v4:	excel_read_FORMAT (q, importer);	break;
		case BIFF_STYLE:	break;
		case BIFF_1904:		excel_read_1904 (q, importer);	break;
		case BIFF_FILEPASS: {
			char *problem = excel_read_FILEPASS (q, importer);
			if (problem != NULL) {
				go_cmd_context_error_import (GO_CMD_CONTEXT (importer->context), problem);
				return FALSE;
			}
			break;
		}

		case BIFF_CONDFMT: excel_read_CONDFMT (q, esheet); break;
		case BIFF_CF:
			g_warning ("Found a CF record without a CONDFMT ??");
			break;
		case BIFF_DVAL:		excel_read_DVAL (q, esheet);  break;
		case BIFF_HLINK:	excel_read_HLINK (q, esheet); break;
		case BIFF_CODENAME:	excel_read_CODENAME (q, importer, esheet); break;
			break;
		case BIFF_DV:
			g_warning ("Found a DV record without a DVal ??");
			excel_read_DV (q, esheet);
			break;

		case BIFF_PIVOT_AUTOFORMAT:
			/* samples/excel/dbfuns.xls has as sample of this record
			 * and I added code in OOo */
			break;

		case BIFF_SHEETPROTECTION:
			excel_read_SHEETPROTECTION (q, esheet->sheet);
			break;

		/* HACK: it seems that in older versions of XL the
		 * charts did not have a wrapper object.  the first
		 * record in the sequence of chart records was a
		 * CHART_UNITS followed by CHART_CHART.  We play off of
		 * that.  When we encounter a CHART_units record we
		 * jump to the chart handler which then starts parsing
		 * at the NEXT record.
		 */
		case BIFF_CHART_units :
			ms_excel_chart_read (q, sheet_container (esheet),
				sheet_object_graph_new (NULL), NULL);
			break;

		default:
			excel_unexpected_biff (q, "Sheet", ms_excel_read_debug);
		}
	}

	g_printerr ("Error, hit end without EOF\n");

	return FALSE;

success :
	/* We need a sheet to extract styles, so store the workbook default as
	 * soon as we parse a sheet.  It is a kludge, but not terribly costly */
	g_object_set_data_full (G_OBJECT (importer->wb),
		"xls-default-style",
		excel_get_style_from_xf (esheet, excel_get_xf (esheet, 0)),
		(GDestroyNotify) gnm_style_unref);
	return TRUE;
}

/**
 * NOTE : MS Docs are incorrect
 *
 * unsigned short num_tabs;
 * unsigned short num_characters_in_book;
 * unsigned char  flag_for_unicode; 0 or 1
 * var encoded string stored as 1 or 2 byte characters
 **/
static void
excel_read_SUPBOOK (BiffQuery *q, GnmXLImporter *importer)
{
	unsigned numTabs, len;
	unsigned i;
	guint32 byte_length;
	gboolean is_2byte = FALSE;
	char *name;
	guint8 encodeType, *data;
	ExcelSupBook *new_supbook;

	XL_CHECK_CONDITION (q->length >= 4);
	numTabs = GSF_LE_GET_GUINT16 (q->data);
	len = GSF_LE_GET_GUINT16 (q->data + 2);

	d (2, fprintf (stderr,"supbook %d has %d\n", importer->v8.supbook->len, numTabs););

	i = importer->v8.supbook->len;
	g_array_set_size (importer->v8.supbook, i+1);
	new_supbook = &g_array_index (importer->v8.supbook, ExcelSupBook, i);

	new_supbook->externname = g_ptr_array_new ();
	new_supbook->wb = NULL;

	/* undocumented guess */
	if (q->length == 4 && len == 0x0401) {
		d (2, fprintf (stderr,"\t is self referential\n"););
		new_supbook->type = EXCEL_SUP_BOOK_SELFREF;
		return;
	}
	if (q->length == 4 && len == 0x3A01) {
		d (2, fprintf (stderr,"\t is a plugin\n"););
		new_supbook->type = EXCEL_SUP_BOOK_PLUGIN;
		return;
	}

	new_supbook->type = EXCEL_SUP_BOOK_STD;

	XL_CHECK_CONDITION (q->length >= 6);
	switch (GSF_LE_GET_GUINT8 (q->data + 4)) {
	case 0 : break; /* 1 byte locale compressed unicode for book name */
	case 1 : len *= 2; is_2byte = TRUE; break;	/* 2 byte unicode */
	default :
		 g_warning ("Invalid header on SUPBOOK record");
		 gsf_mem_dump (q->data, q->length);
		 return;
	}

	/* 5??? */
	XL_CHECK_CONDITION (len + 5 < q->length);

#warning create a workbook and sheets when we have a facility for merging things
	encodeType = GSF_LE_GET_GUINT8 (q->data + 5);
	d (1, fprintf (stderr,"Supporting workbook with %d Tabs\n", numTabs););
	switch (encodeType) {
	case 0x00:
		d (0, fprintf (stderr,"--> SUPBOOK VirtPath encoding = chEmpty"););
		break;
	case 0x01:
		d (0, fprintf (stderr,"--> SUPBOOK VirtPath encoding = chEncode"););
		break;
	case 0x02: /* chSelf */
		break;
	default:
		fprintf (stderr,"Unknown/Unencoded?  (%x) %d\n",
			encodeType, len);
	}
	d (1, {
	gsf_mem_dump (q->data + 4 + 1, len);
	for (data = q->data + 4 + 1 + len, i = 0; i < numTabs ; i++) {
		len = GSF_LE_GET_GUINT16 (data);
		name = excel_get_text_fixme (importer, data + 2, len, &byte_length);
		g_printerr ("\t-> %s\n", name);
		g_free (name);
		data += byte_length + 2;
	}});
}

static void
excel_read_WINDOW1 (BiffQuery *q, WorkbookView *wb_view)
{
	if (q->length >= 16) {
#if 0
				/* In 1/20ths of a point */
		guint16 const xPos    = GSF_LE_GET_GUINT16 (q->data + 0);
		guint16 const yPos    = GSF_LE_GET_GUINT16 (q->data + 2);
#endif
		guint16 const width   = GSF_LE_GET_GUINT16 (q->data + 4);
		guint16 const height  = GSF_LE_GET_GUINT16 (q->data + 6);
		guint16 const options = GSF_LE_GET_GUINT16 (q->data + 8);
#if 0
		/* duplicated in the WINDOW2 record */
		guint16 const selTab  = GSF_LE_GET_GUINT16 (q->data + 10);

		guint16 const firstTab= GSF_LE_GET_GUINT16 (q->data + 12);
		guint16 const tabsSel = GSF_LE_GET_GUINT16 (q->data + 14);

		/* (width of tab)/(width of horizontal scroll bar) / 1000 */
		guint16 const ratio   = GSF_LE_GET_GUINT16 (q->data + 16);
#endif

		/*
		 * We are sizing the window including the toolbars,
		 * menus, and notbook tabs.  Excel does not.
		 *
		 * NOTE: This is the size of the MDI sub-window, not the size of
		 * the containing excel window.
		 */
		wb_view_preferred_size (wb_view,
					.5 + width * gnm_app_display_dpi_get (TRUE) / (72. * 20.),
					.5 + height * gnm_app_display_dpi_get (FALSE) / (72. * 20.));

		if (options & 0x0001)
			g_printerr ("Unsupported: Hidden workbook\n");
		if (options & 0x0002)
			g_printerr ("Unsupported: Iconic workbook\n");
		wb_view->show_horizontal_scrollbar = (options & 0x0008);
		wb_view->show_vertical_scrollbar = (options & 0x0010);
		wb_view->show_notebook_tabs = (options & 0x0020);
	}
}

static void
excel_read_BOF (BiffQuery	 *q,
		GnmXLImporter	 *importer,
		WorkbookView	 *wb_view,
		IOContext	 *context,
		MsBiffBofData	**version, unsigned *current_sheet)
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
		gnm_xl_importer_set_version (importer, ver->version);
		if (ver->version >= MS_BIFF_V8) {
			guint32 ver = GSF_LE_GET_GUINT32 (q->data + 4);
			if (ver == 0x4107cd18)
				g_printerr ("Excel 2000 ?\n");
			else
				g_printerr ("Excel 97 +\n");
		} else if (ver->version >= MS_BIFF_V7)
			g_printerr ("Excel 95\n");
		else if (ver->version >= MS_BIFF_V5)
			g_printerr ("Excel 5.x\n");
		else if (ver->version >= MS_BIFF_V4)
			g_printerr ("Excel 4.x\n");
		else if (ver->version >= MS_BIFF_V3)
			g_printerr ("Excel 3.x - shouldn't happen\n");
		else if (ver->version >= MS_BIFF_V2)
			g_printerr ("Excel 2.x - shouldn't happen\n");
	} else if (ver->type == MS_BIFF_TYPE_Worksheet ||
		   ver->type == MS_BIFF_TYPE_Chart) {
		BiffBoundsheetData *bs = g_hash_table_lookup (
			importer->boundsheet_data_by_stream, GINT_TO_POINTER (q->streamPos));
		ExcelReadSheet *esheet;
		if (bs == NULL) {
			if (ver->version > MS_BIFF_V4) /* be anal */
				g_printerr ("Sheet offset in stream of 0x%x not found in list\n", q->streamPos);
			if (*current_sheet >= importer->excel_sheets->len) {
				esheet = excel_sheet_new (importer, "Worksheet", GNM_SHEET_DATA);
				/* Top level worksheets existed up to & including 4.x */
				gnm_xl_importer_set_version (importer, ver->version);
				if (ver->version >= MS_BIFF_V5)
					g_printerr ( ">= Excel 5 with no BOUNDSHEET ?? - shouldn't happen\n");
				else if (ver->version >= MS_BIFF_V4)
					g_printerr ( "Excel 4.x single worksheet\n");
				else if (ver->version >= MS_BIFF_V3)
					g_printerr ( "Excel 3.x single worksheet\n");
				else if (ver->version >= MS_BIFF_V2)
					g_printerr ( "Excel 2.x single worksheet\n");
			} else
				esheet = g_ptr_array_index (importer->excel_sheets, *current_sheet);
		} else
			esheet = bs->esheet;

		g_return_if_fail (esheet != NULL);
		(*current_sheet)++;

		if (ver->type == MS_BIFF_TYPE_Worksheet) {
			excel_read_sheet (q, importer, wb_view, esheet);
			ms_container_realize_objs (sheet_container (esheet));
			/* reverse the sheet objects satck order */
			esheet->sheet->sheet_objects = g_slist_reverse (esheet->sheet->sheet_objects);
		} else
			ms_excel_chart_read (q, sheet_container (esheet),
				sheet_object_graph_new (NULL),
				esheet->sheet);

	} else if (ver->type == MS_BIFF_TYPE_VBModule ||
		   ver->type == MS_BIFF_TYPE_Macrosheet) {
		/* Skip contents of Module, or MacroSheet */
		if (ver->type != MS_BIFF_TYPE_Macrosheet)
			g_printerr ("VB Module.\n");
		else {
			(*current_sheet)++;
			g_printerr ("XLM Macrosheet.\n");
		}

		while (ms_biff_query_next (q) && q->opcode != BIFF_EOF)
			d (5, ms_biff_query_dump (q););
		if (q->opcode != BIFF_EOF)
			g_warning ("EXCEL: file format error.  Missing BIFF_EOF");
	} else if (ver->type == MS_BIFF_TYPE_Workspace) {
		/* Multiple sheets, XLW format from Excel 4.0 */
		g_printerr ("Excel 4.x workbook\n");
		gnm_xl_importer_set_version (importer, ver->version);
	} else
		g_printerr ("Unknown BOF (%x)\n", ver->type);
}

static void
excel_read_CODEPAGE (BiffQuery *q, GnmXLImporter *importer)
{
	/* This seems to appear within a workbook */
	/* MW: And on Excel seems to drive the display
	   of currency amounts.  */
	XL_CHECK_CONDITION (q->length >= 2);
	gnm_xl_importer_set_codepage (importer,
				      GSF_LE_GET_GUINT16 (q->data));
}

void
excel_read_workbook (IOContext *context, WorkbookView *wb_view, GsfInput *input,
		     gboolean *is_double_stream_file)
{
	GnmXLImporter *importer;
	BiffQuery *q;
	MsBiffBofData *ver = NULL;
	unsigned current_sheet = 0;
	char *problem_loading = NULL;
	gboolean stop_loading = FALSE;
	gboolean prev_was_eof = FALSE;

	io_progress_message (context, _("Reading file..."));
	value_io_progress_set (context, gsf_input_size (input), N_BYTES_BETWEEN_PROGRESS_UPDATES);
	q = ms_biff_query_new (input);

	importer = gnm_xl_importer_new (context, wb_view);

	*is_double_stream_file = FALSE;
	if (ms_biff_query_next (q) &&
	    (q->opcode == BIFF_BOF_v0 ||
	     q->opcode == BIFF_BOF_v2 ||
	     q->opcode == BIFF_BOF_v4 ||
	     q->opcode == BIFF_BOF_v8))
		excel_read_BOF (q, importer, wb_view, context,
				&ver, &current_sheet);
	while (!stop_loading &&		  /* we have not hit the end */
	       problem_loading == NULL && /* there were no problems so far */
	       ms_biff_query_next (q)) {  /* we can load the record */

		d (5, fprintf (stderr,"Opcode: 0x%x\n", q->opcode););

		switch (q->opcode) {
		case BIFF_BOF_v0:
		case BIFF_BOF_v2:
		case BIFF_BOF_v4:
		case BIFF_BOF_v8:
			excel_read_BOF (q, importer, wb_view, context, &ver, &current_sheet);
			break;

		case BIFF_EOF:
			prev_was_eof = TRUE;
			d (0, fprintf (stderr,"End of worksheet spec.\n"););
			break;

		case BIFF_FONT_v0:
		case BIFF_FONT_v2:	excel_read_FONT (q, importer);			break;
		case BIFF_WINDOW1:	excel_read_WINDOW1 (q, wb_view);		break;
		case BIFF_BOUNDSHEET:	excel_read_BOUNDSHEET (q, importer);	break;
		case BIFF_PALETTE:	excel_read_PALETTE (q, importer);			break;

		case BIFF_XF_OLD_v0:
		case BIFF_XF_OLD_v2:
		case BIFF_XF_OLD_v4:	excel_read_XF_OLD (q, importer);	break;
		case BIFF_XF:		excel_read_XF (q, importer);		break;

		case BIFF_EXTERNCOUNT:	/* ignore */ break;
		case BIFF_EXTERNSHEET:
			excel_read_EXTERNSHEET (q, importer, ver);
			break;

		case BIFF_PRECISION : {
#if 0
			/* FIXME: implement in gnumeric */
			/* state of 'Precision as Displayed' option */
			guint16 const data = GSF_LE_GET_GUINT16 (q->data);
			gboolean const prec_as_displayed = (data == 0);
#endif
			break;
		}

		case BIFF_FORMAT_v0:
		case BIFF_FORMAT_v4:	excel_read_FORMAT (q, importer);			break;

		case BIFF_BACKUP:	break;
		case BIFF_CODEPAGE: /* DUPLICATE 42 */
			excel_read_CODEPAGE (q, importer);
			break;

		case BIFF_OBJPROTECT:
		case BIFF_PROTECT:
			excel_read_workbook_PROTECT (q, wb_view);
			break;

		case BIFF_PASSWORD:
			break;

		case BIFF_FILEPASS: /* All records after this are encrypted */
			problem_loading = excel_read_FILEPASS(q, importer);
			break;

		case BIFF_STYLE:
			break;

		case BIFF_WINDOWPROTECT:
			break;

		case BIFF_EXTERNNAME_v0:
		case BIFF_EXTERNNAME_v2: excel_read_EXTERNNAME (q, &importer->container); break;
		case BIFF_NAME_v0:
		case BIFF_NAME_v2:	excel_read_NAME (q, importer, NULL);	break;
		case BIFF_XCT:		excel_read_XCT (q, importer);	break;

		case BIFF_WRITEACCESS:
			break;

		case BIFF_HIDEOBJ:	break;
		case BIFF_FNGROUPCOUNT: break;
		case BIFF_MMS:		break;

		/* Flags that the project has some VBA */
		case BIFF_OBPROJ:	break;
		case BIFF_BOOKBOOL:	break;
		case BIFF_COUNTRY:	break;
		case BIFF_INTERFACEHDR: break;
		case BIFF_INTERFACEEND: break;
		case BIFF_TOOLBARHDR:	break;
		case BIFF_TOOLBAREND:	break;

		case BIFF_1904:		excel_read_1904 (q, importer);	break;

		case BIFF_SELECTION: /* 0, NOT 10 */
			break;

		case BIFF_DIMENSIONS_v0:
			/* Ignore files that pad the end with zeros */
			if (prev_was_eof) {
				stop_loading = TRUE;
				break;
			}
			/* fall through */
		case BIFF_DIMENSIONS_v2:
			excel_read_DIMENSIONS (q, importer);
			break;

		case BIFF_OBJ:	ms_read_OBJ (q, &importer->container, NULL); break;
		case BIFF_SCL:			break;
		case BIFF_TABIDCONF:		break;
		case BIFF_MS_O_DRAWING:
		case BIFF_MS_O_DRAWING_GROUP:
		case BIFF_MS_O_DRAWING_SELECTION:
			ms_escher_parse (q, &importer->container, FALSE);
			break;

		case BIFF_ADDMENU:
			d (1, fprintf (stderr,"%smenu with %d sub items",
				      (GSF_LE_GET_GUINT8 (q->data + 6) == 1) ? "" : "Placeholder ",
				      GSF_LE_GET_GUINT8 (q->data + 5)););
			break;

		case BIFF_SST:	   excel_read_SST (q, importer);	break;
		case BIFF_EXTSST:  excel_read_EXSST (q, importer);	break;

		case BIFF_DSF:		/* stored in the biff8 workbook */
		case BIFF_XL5MODIFY:	/* stored in the biff5/7 book */
		{
			gboolean dsf = (q->length >= 2 &&
					GSF_LE_GET_GUINT16 (q->data));
			d (0, fprintf (stderr, "Double stream file : %d\n",
				       dsf););
			if (dsf)
				*is_double_stream_file = TRUE;
			break;
		}

		case BIFF_XL9FILE:
			d (0, puts ("XL 2000 file"););
			break;

		case BIFF_RECALCID:	break;
		case BIFF_REFRESHALL:	break;
		case BIFF_CODENAME:	excel_read_CODENAME (q, importer, NULL); break;
		case BIFF_PROT4REVPASS: break;

		case BIFF_USESELFS:	break;
		case BIFF_TABID:	break;
		case BIFF_PROT4REV:
			break;


		case BIFF_SUPBOOK:	excel_read_SUPBOOK (q, importer); break;

		default:
			excel_unexpected_biff (q, "Workbook", ms_excel_read_debug);
			break;
		}
		/* check here in case any of the handlers read additional records */
		prev_was_eof = (q->opcode == BIFF_EOF);
	}

	excel_read_pivot_caches (importer, q, gsf_input_container (input));

	ms_biff_query_destroy (q);
	if (ver)
		ms_biff_bof_data_destroy (ver);
	io_progress_unset (context);

	d (1, fprintf (stderr,"finished read\n"););

	gnm_xl_importer_free (importer);

	/* If we were forced to stop then the load failed */
	if (problem_loading != NULL)
		go_cmd_context_error_import (GO_CMD_CONTEXT (context), problem_loading);
}


void
excel_read_init (void)
{
	excel_builtin_formats [0x0e] = go_format_builtins [GO_FORMAT_DATE][0];
	excel_builtin_formats [0x0f] = go_format_builtins [GO_FORMAT_DATE][2];
	excel_builtin_formats [0x10] = go_format_builtins [GO_FORMAT_DATE][4];
	excel_builtin_formats [0x16] = go_format_builtins [GO_FORMAT_DATE][20];
}

void
excel_read_cleanup (void)
{
}
