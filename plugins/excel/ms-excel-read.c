/* vim: set sw=8: */
/**
 * ms-excel.c: MS Excel support for Gnumeric
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2002 Michael Meeks, Jody Goldberg
 * unicode and national language support (C) 2001 by Vlad Harchev <hvv@hippo.ru>
 **/
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <string.h>

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
#include <sheet-view.h>
#include <sheet-style.h>
#include <sheet-merge.h>
#include <sheet-filter.h>
#include <cell.h>
#include <style.h>
#include <format.h>
#include <formats.h>
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
#include <gutils.h>
#include <application.h>
#include <io-context.h>
#include <command-context.h>
#include <sheet-object-cell-comment.h>
#include <sheet-object-widget.h>
#include <sheet-object-graphic.h>
#include <sheet-object-image.h>
#include <gnumeric-graph.h>

#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-msole-utils.h>
#include <locale.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "gnumeric:read"

#define N_BYTES_BETWEEN_PROGRESS_UPDATES   0x1000

/* #define NO_DEBUG_EXCEL */
#ifndef NO_DEBUG_EXCEL
#define d(level, code)	do { if (ms_excel_read_debug > level) { code } } while (0)
#else
#define d(level, code)
#endif

static GIConv current_workbook_iconv = NULL;

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

static GIConv
excel_iconv_open_for_import (guint codepage)
{
	if (codepage == 1200 || codepage == 1201)
		/* this is 'compressed' unicode.  unicode characters 0000->00FF
		 * which looks the same as 8859-1.  What does Little endian vs
		 * bigendian have to do with this.  There is only 1 byte, and it would
		 * certainly not be useful to keep the low byte as 0.
		 */
		return g_iconv_open ("UTF-8", "ISO-8859-1");
	return gsf_msole_iconv_open_for_import (codepage);
}

static StyleFormat *
excel_wb_get_fmt (ExcelWorkbook *ewb, guint16 idx)
{
	char const *ans = NULL;
	BiffFormatData const *d = g_hash_table_lookup (ewb->format_data, &idx);

	if (d)
		ans = d->name;
	else if (idx <= 0x31) {
		ans = excel_builtin_formats[idx];
		if (!ans)
			fprintf (stderr,"Foreign undocumented format\n");
	} else
		fprintf (stderr,"Unknown format: 0x%x\n", idx);

	if (ans)
		return style_format_new_XL (ans, FALSE);
	else
		return NULL;
}

static GnmExpr const *
ms_sheet_parse_expr_internal (ExcelSheet *esheet, guint8 const *data, int length)
{
	GnmExpr const *expr;

	g_return_val_if_fail (length > 0, NULL);

	expr = excel_parse_formula (&esheet->container, esheet, 0, 0,
		data, length, FALSE, NULL);
#if 0
	{
		char *tmp;
		ParsePos pp;
		Sheet *sheet = esheet->sheet;
		Workbook *wb = (sheet == NULL) ? esheet->container.ewb->gnum_wb : NULL;

		tmp = gnm_expr_as_string (expr, parse_pos_init (&pp, wb, sheet, 0, 0));
		puts (tmp);
		g_free (tmp);
	}
#endif

	return expr;
}

static GnmExpr const *
ms_sheet_parse_expr (MSContainer *container, guint8 const *data, int length)
{
	return ms_sheet_parse_expr_internal ((ExcelSheet *)container,
					     data, length);
}

static Sheet *
ms_sheet_get_sheet (MSContainer const *container)
{
	return ((ExcelSheet const *)container)->sheet;
}

static StyleFormat *
ms_sheet_get_fmt (MSContainer const *container, guint16 indx)
{
	return excel_wb_get_fmt (container->ewb, indx);
}

static StyleColor *
ms_sheet_map_color (ExcelSheet const *esheet, MSObj const *obj, MSObjAttrID id)
{
	gushort r, g, b;
	MSObjAttr *attr = ms_obj_attr_bag_lookup (obj->attrs, id);

	if (attr == NULL)
		return NULL;

	if ((~0x7ffffff) & attr->v.v_uint)
		return excel_palette_get (esheet->container.ewb->palette,
			(0x7ffffff & attr->v.v_uint));

	r = (attr->v.v_uint)       & 0xff;
	g = (attr->v.v_uint >> 8)  & 0xff;
	b = (attr->v.v_uint >> 16) & 0xff;

	return style_color_new_i8 (r, g, b);
}

static SheetObject *
ms_sheet_create_image (MSObj *obj, MSEscherBlip *blip)
{
	SheetObject *so;
	MSObjAttr *crop_left_attr = ms_obj_attr_bag_lookup
		(obj->attrs, MS_OBJ_ATTR_BLIP_CROP_LEFT);
	MSObjAttr *crop_top_attr = ms_obj_attr_bag_lookup
		(obj->attrs, MS_OBJ_ATTR_BLIP_CROP_TOP);
	MSObjAttr *crop_right_attr = ms_obj_attr_bag_lookup
		(obj->attrs, MS_OBJ_ATTR_BLIP_CROP_RIGHT);
	MSObjAttr *crop_bottom_attr = ms_obj_attr_bag_lookup
		(obj->attrs, MS_OBJ_ATTR_BLIP_CROP_BOTTOM);
	double crop_left_val = 0.0;
	double crop_top_val = 0.0;
	double crop_right_val = 0.0;
	double crop_bottom_val = 0.0;

	so = sheet_object_image_new (blip->type, blip->data, blip->data_len,
				     !blip->needs_free);

	if (!so)
		return NULL;

	if (crop_left_attr)
		crop_left_val   = (double) crop_left_attr->v.v_uint / 65536.;
	if (crop_top_attr)
		crop_top_val    = (double) crop_top_attr->v.v_uint / 65536.;
	if (crop_right_attr)
		crop_right_val  = (double) crop_right_attr->v.v_uint / 65536.;
	if (crop_bottom_attr)
		crop_bottom_val = (double) crop_bottom_attr->v.v_uint / 65536.;
	sheet_object_image_set_crop (SHEET_OBJECT_IMAGE (so),
				     crop_left_val, crop_top_val,
				     crop_right_val, crop_bottom_val);

	return so;
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
	/* NOTE :
	 * float const row_denominator = (ver >= MS_BIFF_V8) ? 256. : 1024.;
	 * damn damn damn
	 * chap03-1.xls suggests that XL95 uses 256 too
	 * Do we have any tests that confirm the docs contention of 1024 ?
	 */
	float const row_denominator = 256.;
	int	i;

	d (0, fprintf (stderr,"%s\n", sheet->name_unquoted););

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
				(i & 1) ? "heights" : "widths");
			if (i & 1)
				fprintf (stderr,"row %d;\n", pos + 1);
			else
				fprintf (stderr,"col %s (%d);\n", col_name (pos), pos);
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
	char const *label;
	Range range;
	ExcelSheet *esheet;
	MSObjAttr *anchor;

	if (obj == NULL)
		return TRUE;

	g_return_val_if_fail (container != NULL, TRUE);
	esheet = (ExcelSheet *)container;

	anchor = ms_obj_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_ANCHOR);
	if (anchor == NULL) {
		fprintf (stderr,"MISSING anchor for obj %p\n", (void *)obj);
		return TRUE;
	}

	if (ms_sheet_obj_anchor_to_pos (esheet->sheet, container->ver,
					anchor->v.v_ptr, &range, offsets))
		return TRUE;

	if (obj->gnum_obj != NULL) {
		static SheetObjectAnchorType const anchor_types[4] = {
			SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
			SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
			SO_ANCHOR_PERCENTAGE_FROM_COLROW_START,
			SO_ANCHOR_PERCENTAGE_FROM_COLROW_START
		};
		MSObjAttr *flip_h = ms_obj_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_FLIP_H);
		MSObjAttr *flip_v = ms_obj_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_FLIP_V);
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
					esheet->sheet);

		/* cannot be done until we have set the sheet */
		if (obj->excel_type == 0x0B) {
			sheet_widget_checkbox_set_link (SHEET_OBJECT (obj->gnum_obj),
				ms_obj_attr_get_expr (obj, MS_OBJ_ATTR_CHECKBOX_LINK, NULL));
		} else if (obj->excel_type == 0x11) {
			sheet_widget_scrollbar_set_details (SHEET_OBJECT (obj->gnum_obj),
				ms_obj_attr_get_expr (obj, MS_OBJ_ATTR_SCROLLBAR_LINK, NULL),
				0,
				ms_obj_attr_get_int  (obj, MS_OBJ_ATTR_SCROLLBAR_MIN, 0),
				ms_obj_attr_get_int  (obj, MS_OBJ_ATTR_SCROLLBAR_MAX, 100),
				ms_obj_attr_get_int  (obj, MS_OBJ_ATTR_SCROLLBAR_INC, 1),
				ms_obj_attr_get_int  (obj, MS_OBJ_ATTR_SCROLLBAR_PAGE, 10));
		}

		label = ms_obj_attr_get_ptr (obj, MS_OBJ_ATTR_TEXT, NULL);
		if (label != NULL) {
			SheetObject *so = SHEET_OBJECT (obj->gnum_obj);
			switch (obj->excel_type) {
			case 0x07: sheet_widget_button_set_label (so, label);
				   break;
			case 0x0B: sheet_widget_checkbox_set_label (so, label);
				   break;
			case 0x0C: sheet_widget_radio_button_set_label(so, label);
				   break;
			default:
				   break;
			};
		}
	}

	return FALSE;
}

static GObject *
ms_sheet_create_obj (MSContainer *container, MSObj *obj)
{
	SheetObject *so = NULL;
	Workbook *wb;
	ExcelSheet const *esheet;

	if (obj == NULL)
		return NULL;

	g_return_val_if_fail (container != NULL, NULL);

	esheet = (ExcelSheet const *)container;
	wb = esheet->container.ewb->gnum_wb;

	switch (obj->excel_type) {
	case 0x01: { /* Line */
		StyleColor *color;
		MSObjAttr *is_arrow = ms_obj_attr_bag_lookup (obj->attrs,
			MS_OBJ_ATTR_ARROW_END);
		so = sheet_object_line_new (is_arrow != NULL); break;

		color = ms_sheet_map_color (esheet, obj,
			MS_OBJ_ATTR_FILL_COLOR);
		if (color != NULL)
			sheet_object_graphic_fill_color_set (so, color);
		break;
	}
	case 0x02:
	case 0x03: { /* Box or Oval */
		StyleColor *fill_color = NULL;
		StyleColor *outline_color;

		so = sheet_object_box_new (obj->excel_type == 3);
		if (ms_obj_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_FILLED))
			fill_color = ms_sheet_map_color (esheet, obj,
				MS_OBJ_ATTR_FILL_COLOR);
		outline_color = ms_sheet_map_color (esheet, obj,
			MS_OBJ_ATTR_OUTLINE_COLOR);
		sheet_object_graphic_fill_color_set (so, fill_color);
		if (outline_color)
			sheet_object_filled_outline_color_set (so, outline_color);
		break;
	}

	case 0x05: { /* Chart */
		so = SHEET_OBJECT (gnm_graph_new (wb));
		break;
	}

	case 0x0E: /* Label */
	case 0x06: { /* TextBox */
		StyleColor *fill_color = NULL;
		StyleColor *outline_color;

		so = g_object_new (sheet_object_text_get_type (), NULL);
		if (ms_obj_attr_bag_lookup (obj->attrs, MS_OBJ_ATTR_FILLED))
			fill_color = ms_sheet_map_color (esheet, obj,
				MS_OBJ_ATTR_FILL_COLOR);
		outline_color = ms_sheet_map_color (esheet, obj,
			MS_OBJ_ATTR_OUTLINE_COLOR);
		sheet_object_graphic_fill_color_set (so, fill_color);
		if (outline_color)
			sheet_object_filled_outline_color_set (so, outline_color);
		sheet_object_text_set_text (so, 
			ms_obj_attr_get_ptr (obj, MS_OBJ_ATTR_TEXT, (char *)""));
		break;
	}

	/* Button */
	case 0x07: so = g_object_new (sheet_widget_button_get_type (), NULL);
		break;
	case 0x08: { /* Picture */
		MSObjAttr *blip_id = ms_obj_attr_bag_lookup (obj->attrs,
			MS_OBJ_ATTR_BLIP_ID);

		if (blip_id != NULL) {
			MSEscherBlip *blip = ms_container_get_blip (container,
				blip_id->v.v_uint - 1);
			if (blip != NULL) {
				so = ms_sheet_create_image (obj, blip);
				blip->needs_free = FALSE; /* image took over managing data */
			}
		}

		/* replace blips we don't know how to handle with rectangles */
		if (so == NULL)
			so = sheet_object_box_new (FALSE);  /* placeholder */
		break;
	}
	case 0x09: so = g_object_new (sheet_object_polygon_get_type (), NULL);
		   sheet_object_polygon_set_points (SHEET_OBJECT (so),
			ms_obj_attr_get_array (obj, MS_OBJ_ATTR_POLYGON_COORDS, NULL));
		   sheet_object_polygon_fill_color_set (so, 
			ms_sheet_map_color (esheet, obj, MS_OBJ_ATTR_FILL_COLOR));
		   sheet_object_polygon_outline_color_set (so, 
			ms_sheet_map_color (esheet, obj, MS_OBJ_ATTR_OUTLINE_COLOR));
		   break;

	case 0x0B: so = g_object_new (sheet_widget_checkbox_get_type (), NULL);
		break;
	case 0x0C: so = g_object_new (sheet_widget_radio_button_get_type (), NULL);
		break;
	case 0x10: so = sheet_object_box_new (FALSE);  break; /* Spinner */
	case 0x11: so = g_object_new (sheet_widget_scrollbar_get_type (), NULL);
		break;
	case 0x12: so = g_object_new (sheet_widget_list_get_type (), NULL);
		break;

	/* ignore combos associateed with filters */
	case 0x14:
		if (!obj->ignore_combo_in_filter)
			so = g_object_new (sheet_widget_combo_get_type (), NULL);
	break;

	case 0x19: /* Comment */
		/* TODO: we'll need a special widget for this */
		return NULL;

	default:
		g_warning ("EXCEL: unhandled excel object of type %s (0x%x) id = %d.",
			   obj->excel_type_name, obj->excel_type, obj->id);
		return NULL;
	}

	return so ? G_OBJECT (so) : NULL;
}

static double
inches_to_points (double inch)
{
	return inch * 72.0;
}


static void
excel_print_unit_init_inch (PrintUnit *pu, double val)
{
	const GnomePrintUnit *uinch = gnome_print_unit_get_by_abbreviation ("in");
	pu->points = inches_to_points (val);
	pu->desired_display = uinch; /* FIXME: should be more global */
}

/*
 * excel_init_margins
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
excel_init_margins (ExcelSheet *esheet)
{
	PrintInformation *pi;
	double points;
	double short_points;

	g_return_if_fail (esheet != NULL);
	g_return_if_fail (esheet->sheet != NULL);
	g_return_if_fail (esheet->sheet->print_info != NULL);

	pi = esheet->sheet->print_info;
	excel_print_unit_init_inch (&pi->margins.top, 1.0);
	excel_print_unit_init_inch (&pi->margins.bottom, 1.0);

	points = inches_to_points (0.75);
	short_points = inches_to_points (0.5);
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
static void
excel_data_table_free (XLDataTable *dt)
{
	if (dt != NULL) {
		g_free (dt->data);
		g_free (dt);
	}
}

static ExcelSheet *
excel_sheet_new (ExcelWorkbook *ewb, char const *sheet_name)
{
	static MSContainerClass const vtbl = {
		&ms_sheet_realize_obj,
		&ms_sheet_create_obj,
		&ms_sheet_parse_expr,
		&ms_sheet_get_sheet,
		&ms_sheet_get_fmt
	};

	ExcelSheet *esheet = g_new (ExcelSheet, 1);
	Sheet *sheet;

	sheet = workbook_sheet_by_name (ewb->gnum_wb, sheet_name);
	if (sheet == NULL) {
		sheet = sheet_new (ewb->gnum_wb, sheet_name);
		workbook_sheet_attach (ewb->gnum_wb, sheet, NULL);
		d (1, fprintf (stderr,"Adding sheet sheet '%s'\n", sheet_name););
	}
	/* in case nothing forces a spanning flag it here so that spans will
	 * regenerater later.
	 */
	sheet_flag_recompute_spans (sheet);

	esheet->sheet	= sheet;
	esheet->freeze_panes	= FALSE;
	esheet->shared_formulae	= g_hash_table_new_full (
		(GHashFunc)&cellpos_hash, (GCompareFunc)&cellpos_cmp,
		NULL, (GDestroyNotify) &excel_shared_formula_free);
	esheet->tables		= g_hash_table_new_full (
		(GHashFunc)&cellpos_hash, (GCompareFunc)&cellpos_cmp,
		NULL, (GDestroyNotify) &excel_data_table_free);

	excel_init_margins (esheet);
	ms_container_init (&esheet->container, &vtbl, &ewb->container,
			   ewb, ewb->container.ver);
	g_ptr_array_add (ewb->excel_sheets, esheet);

	return esheet;
}

void
excel_unexpected_biff (BiffQuery *q, char const *state,
		       int debug_level)
{
#ifndef NO_DEBUG_EXCEL
	if (debug_level > 1) {
		g_warning ("Unexpected Opcode in %s: 0x%x, length 0x%x\n",
			state, q->opcode, q->length);
		if (debug_level > 2)
			gsf_mem_dump (q->data, q->length);
	}
#endif
}


/**
 * Generic 16 bit int index pointer functions.
 **/
static guint
biff_guint16_hash (guint16 const *d)
{
	return *d * 2;
}

static guint
biff_guint32_hash (guint32 const *d)
{
	return *d * 2;
}

static gint
biff_guint16_equal (guint16 const *a, guint16 const *b)
{
	if (*a == *b)
		return 1;
	return 0;
}

static gint
biff_guint32_equal (guint32 const *a, guint32 const *b)
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
biff_string_get_flags (guint8 const *ptr,
		       gboolean *word_chars,
		       gboolean *extended,
		       gboolean *rich)
{
	guint8 header;

	header = GSF_LE_GET_GUINT8 (ptr);
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
get_xtn_lens (guint32 *pre_len, guint32 *end_len, guint8 const *ptr, gboolean ext_str, gboolean rich_str)
{
	*end_len = 0;
	*pre_len = 0;

	if (rich_str) { /* The data for this appears after the string */
		guint16 formatting_runs = GSF_LE_GET_GUINT16 (ptr);

		(*end_len) += formatting_runs * 4; /* 4 bytes per */
		(*pre_len) += 2;
		ptr        += 2;

		fprintf (stderr,"rich string support unimplemented:"
			"discarding %d runs\n", formatting_runs);
	}
	if (ext_str) { /* NB this data always comes after the rich_str data */
		guint32 len_ext_rst = GSF_LE_GET_GUINT32 (ptr); /* A byte length */

		(*end_len) += len_ext_rst;
		(*pre_len) += 4;

		g_warning ("extended string support unimplemented:"
			   "ignoring %u bytes\n", len_ext_rst);
	}
}

static char *
get_chars (char const *ptr, guint length, gboolean use_utf16)
{
	char* ans;
	unsigned i;

	if (use_utf16) {
		gunichar2 *uni_text = g_alloca (sizeof (gunichar2)*length);

		for (i = 0; i < length; i++, ptr += 2)
			uni_text [i] = GSF_LE_GET_GUINT16 (ptr);
		ans = g_utf16_to_utf8 (uni_text, length, NULL, NULL, NULL);
	} else {
		size_t outbytes = (length + 2) * 8;
		char *outbuf = g_new (char, outbytes + 1);

		ans = outbuf;
		g_iconv (current_workbook_iconv,
			 (char **)&ptr, &length, &outbuf, &outbytes);

		i = outbuf - ans;
		ans [i] = 0;
		ans = g_realloc (ans, i + 1);
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
	guint8 const *ptr;
	guint32 byte_len;
	gboolean header;
	gboolean use_utf16;
	gboolean ext_str;
	gboolean rich_str;

	if (!byte_length)
		byte_length = &byte_len;
	*byte_length = 0;

	if (!length) {
		/* NOTE : This is WRONG in some cases.  There is also the 1
		 * 	byte header which is sometimes part of the count and
		 * 	sometimes not.
		 */
		return 0;
	}

	d (5, {
		fprintf (stderr,"String:\n");
		gsf_mem_dump (pos, length + 1);
	});

	header = biff_string_get_flags (pos,
					&use_utf16,
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
		fprintf (stderr,"String len %d, byte length %d: %d %d %d:\n",
			length, (*byte_length), use_utf16, rich_str, ext_str);
		gsf_mem_dump (pos, *byte_length);
	});


	if (!length) {
		ans = g_new (char, 2);
		g_warning ("Warning unterminated string floating.");
	} else {
		(*byte_length) += (use_utf16 ? 2 : 1)*length;
		ans = get_chars ((char *) ptr, length, use_utf16);
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
sst_read_string (char **output, BiffQuery *q, guint32 offset)
{
	guint32  new_offset;
	guint32  total_len;
	guint32  total_end_len;
	/* Will be localy scoped when gdb gets its act together */
		gboolean header;
		gboolean use_utf16;
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
	total_len     = GSF_LE_GET_GUINT16 (q->data + offset);
	new_offset    = offset + 2;
	total_end_len = 0;

	do {
		new_offset = sst_bound_check (q, new_offset);

		header = biff_string_get_flags (q->data + new_offset,
						&use_utf16,
						&ext_str,
						&rich_str);
		if (!header) {
			g_warning ("Seriously broken string with no header 0x%x", *(q->data + new_offset));
			gsf_mem_dump (q->data + new_offset, q->length - new_offset);
			return 0;
		}

		new_offset++;

		get_xtn_lens (&pre_len, &end_len, q->data + new_offset, ext_str, rich_str);
		total_end_len += end_len;

		/* the - end_len is an educated guess based on insufficient data */
		chars_left = (q->length - new_offset - pre_len) / (use_utf16 ? 2 : 1);
		if (chars_left > total_len)
			get_len = total_len;
		else
			get_len = chars_left;
		total_len -= get_len;
		g_assert (get_len >= 0);

		/* FIXME: split this simple bit out of here, it makes more sense damnit */
		str = get_chars ((char *)(q->data + new_offset + pre_len), get_len, use_utf16);
		new_offset += pre_len + get_len * (use_utf16 ? 2 : 1);

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
excel_read_1904 (BiffQuery *q, ExcelWorkbook *ewb)
{
	if (GSF_LE_GET_GUINT16 (q->data) == 1)
		gnm_io_warning_unsupported_feature (ewb->context, 
			_("Workbook uses unsupported 1904 Date System, Dates will be incorrect"));
}

static void
excel_read_SST (BiffQuery *q, ExcelWorkbook *ewb)
{
	guint32 offset;
	unsigned k;

	d (4, {
		fprintf (stderr, "SST total = %u, sst = %u\n",
			 GSF_LE_GET_GUINT32 (q->data + 0),
			 GSF_LE_GET_GUINT32 (q->data + 4));
		gsf_mem_dump (q->data, q->length);
	});

	ewb->global_string_max = GSF_LE_GET_GUINT32 (q->data + 4);
	ewb->global_strings = g_new (char *, ewb->global_string_max);

	offset = 8;

	for (k = 0; k < ewb->global_string_max; k++) {
		offset = sst_read_string (&ewb->global_strings[k], q, offset);

		if (!ewb->global_strings[k]) {
			d (4, fprintf (stderr,"Blank string in table at 0x%x.\n", k););
		}
#ifndef NO_DEBUG_EXCEL
		else if (ms_excel_read_debug > 4)
			puts (ewb->global_strings[k]);
#endif
	}
}

static void
excel_read_EXSST (BiffQuery *q, ExcelWorkbook *ewb)
{
	d (10, fprintf (stderr,"Bucketsize = %hu,\tnum buckets = %d\n",
		       GSF_LE_GET_GUINT16 (q->data), (q->length - 2) / 8););
}

char const *
biff_get_error_text (guint8 const err)
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
				fprintf (stderr,"Complicated BIFF version 0x%x\n",
					GSF_LE_GET_GUINT16 (q->non_decrypted_data));
				gsf_mem_dump (q->non_decrypted_data, q->length);
			});

			switch (GSF_LE_GET_GUINT16 (q->non_decrypted_data)) {
			case 0x0600: ans->version = MS_BIFF_V8;
				     break;
			case 0x500: /* * OR ebiff7: FIXME ? !  */
				     ans->version = MS_BIFF_V7;
				     break;
			default:
				fprintf (stderr,"Unknown BIFF sub-number in BOF %x\n", q->opcode);
				ans->version = MS_BIFF_V_UNKNOWN;
			}
			break;
		}

		default:
			fprintf (stderr,"Unknown BIFF number in BOF %x\n", q->opcode);
			ans->version = MS_BIFF_V_UNKNOWN;
			fprintf (stderr,"Biff version %d\n", ans->version);
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
			fprintf (stderr,"Unknown BIFF type in BOF %x\n", GSF_LE_GET_GUINT16 (q->non_decrypted_data + 2));
			break;
		}
		/* Now store in the directory array: */
		d (2, fprintf (stderr,"BOF %x, %d == %d, %d\n", q->opcode, q->length,
			      ans->version, ans->type););
	} else {
		fprintf (stderr,"Not a BOF !\n");
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
excel_read_BOUNDSHEET (BiffQuery *q, ExcelWorkbook *ewb, MsBiffVersion ver)
{
	BiffBoundsheetData *ans;
	char const *default_name = "Unknown%d";

	ans = g_new (BiffBoundsheetData, 1);

	if (ver <= MS_BIFF_V4) {
		ans->streamStartPos = 0; /* Excel 4 doesn't tell us */
		ans->type = MS_BIFF_TYPE_Worksheet;
		default_name = _("Sheet%d");
		ans->hidden = MS_BIFF_H_VISIBLE;
		ans->name = biff_get_text (q->data + 1,
			GSF_LE_GET_GUINT8 (q->data), NULL);
	} else {
		if (ver > MS_BIFF_V8)
			fprintf (stderr,"Unknown BIFF Boundsheet spec. Assuming same as Biff7 FIXME\n");
		ans->streamStartPos = GSF_LE_GET_GUINT32 (q->non_decrypted_data);
	
		switch (GSF_LE_GET_GUINT8 (q->data + 4)) {
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
			fprintf (stderr,"Unknown boundsheet type: %d\n", GSF_LE_GET_GUINT8 (q->data + 4));
			ans->type = MS_BIFF_TYPE_Unknown;
		}
		switch ((GSF_LE_GET_GUINT8 (q->data + 5)) & 0x3) {
		case 0: ans->hidden = MS_BIFF_H_VISIBLE;
			break;
		case 1: ans->hidden = MS_BIFF_H_HIDDEN;
			break;
		case 2: ans->hidden = MS_BIFF_H_VERY_HIDDEN;
			break;
		default:
			fprintf (stderr,"Unknown sheet hiddenness %d\n", (GSF_LE_GET_GUINT8 (q->data + 4)) & 0x3);
			ans->hidden = MS_BIFF_H_VISIBLE;
		}

		/* TODO: find some documentation on this.
	 	* Sample data and OpenCalc imply that the docs are incorrect.  It
	 	* seems like the name length is 1 byte.  Loading sample sheets in
	 	* other locales universally seem to treat the first byte as a length
	 	* and the second as the unicode flag header.
	 	*/
		ans->name = biff_get_text (q->data + 7,
			GSF_LE_GET_GUINT8 (q->data + 6), NULL);
	}

	/* TODO: find some documentation on this.
	 * It appears that if the name is null it defaults to Sheet%d?
	 * However, we have only one test case and no docs.
	 */
	if (ans->name == NULL)
		ans->name = g_strdup_printf (default_name,
			ewb->boundsheet_sheet_by_index->len);

	/* AARRRGGGG : This is useless XL calls chart tabs 'worksheet' too */
	/* if (ans->type == MS_BIFF_TYPE_Worksheet) { } */

	/* FIXME : Use this kruft instead */
	if (ans->hidden == MS_BIFF_H_VISIBLE)
		ans->sheet = excel_sheet_new (ewb, ans->name);
	else
		ans->sheet = NULL;

	ans->index = ewb->boundsheet_sheet_by_index->len;
	g_ptr_array_add (ewb->boundsheet_sheet_by_index, ans->sheet ? ans->sheet->sheet : NULL);
	g_hash_table_insert (ewb->boundsheet_data_by_stream, &ans->streamStartPos, ans);

	d (1, fprintf (stderr,"Boundsheet: %d) '%s' %p, %d:%d\n", ans->index,
		       ans->name, ans->sheet, ans->type, ans->hidden););
}

static void
biff_boundsheet_data_destroy (BiffBoundsheetData *d)
{
	g_free (d->name);
	g_free (d);
}

static void
excel_read_FORMAT (BiffQuery *q, ExcelWorkbook *ewb)
{
	BiffFormatData *d = g_new (BiffFormatData, 1);
	if (ewb->container.ver >= MS_BIFF_V8) {
		d->idx = GSF_LE_GET_GUINT16 (q->data);
		d->name = biff_get_text (q->data + 4, GSF_LE_GET_GUINT16 (q->data + 2), NULL);
	} else if (ewb->container.ver >= MS_BIFF_V7) { /* Total guess */
		d->idx = GSF_LE_GET_GUINT16 (q->data);
		d->name = biff_get_text (q->data + 3, GSF_LE_GET_GUINT8 (q->data + 2), NULL);
	} else if (ewb->container.ver >= MS_BIFF_V4) { /* Sample sheets suggest this */
		d->idx = g_hash_table_size (ewb->format_data) + 0x32;
		d->name = biff_get_text (q->data + 3, GSF_LE_GET_GUINT8 (q->data + 2), NULL);
	} else {
		d->idx = g_hash_table_size (ewb->format_data) + 0x32;
		d->name = biff_get_text (q->data + 1, GSF_LE_GET_GUINT8 (q->data), NULL);
	}

	d (2, printf ("Format data: 0x%x == '%s'\n", d->idx, d->name););
	g_hash_table_insert (ewb->format_data, &d->idx, d);
}

static void
excel_read_FONT (BiffQuery *q, ExcelWorkbook *ewb)
{
	BiffFontData *fd = g_new (BiffFontData, 1);
	guint16 data;
	guint8 data1;

	fd->height = GSF_LE_GET_GUINT16 (q->data + 0);
	data = GSF_LE_GET_GUINT16 (q->data + 2);
	fd->italic     = (data & 0x2) == 0x2;
	fd->struck_out = (data & 0x8) == 0x8;
	if (ewb->container.ver <= MS_BIFF_V2) /* Guess */ {
		fd->color_idx = 0x7f;
		fd->boldness = 0;
		fd->script = MS_BIFF_F_S_NONE;
		fd->underline = MS_BIFF_F_U_NONE;
		fd->fontname = biff_get_text (q->data + 5,
				      GSF_LE_GET_GUINT8 (q->data + 4), NULL);
	} else if (ewb->container.ver <= MS_BIFF_V4) /* Guess */ {
		fd->color_idx  = GSF_LE_GET_GUINT16 (q->data + 4);
		fd->color_idx &= 0x7f; /* Undocumented but a good idea */
		fd->boldness = 0;
		fd->script = MS_BIFF_F_S_NONE;
		fd->underline = MS_BIFF_F_U_NONE;
		fd->fontname = biff_get_text (q->data + 7,
				      GSF_LE_GET_GUINT8 (q->data + 6), NULL);
	} else {
		fd->color_idx  = GSF_LE_GET_GUINT16 (q->data + 4);
		fd->color_idx &= 0x7f; /* Undocumented but a good idea */
		fd->boldness   = GSF_LE_GET_GUINT16 (q->data + 6);
		data = GSF_LE_GET_GUINT16 (q->data + 8);
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
			fprintf (stderr,"Unknown script %d\n", data);
			break;
		}
	
		data1 = GSF_LE_GET_GUINT8 (q->data + 10);
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
				      GSF_LE_GET_GUINT8 (q->data + 14), NULL);
	}

	d (1, fprintf (stderr,"Insert font '%s' size %d pts color %d\n",
		      fd->fontname, fd->height / 20, fd->color_idx););
	d (3, fprintf (stderr,"Font color = 0x%x\n", fd->color_idx););

        fd->index = g_hash_table_size (ewb->font_data);
	if (fd->index >= 4) /* Weird: for backwards compatibility */
		fd->index++;
	g_hash_table_insert (ewb->font_data, &fd->index, fd);
}

static void
biff_font_data_destroy (BiffFontData *fd)
{
	g_free (fd->fontname);
	g_free (fd);
}

static void
biff_format_data_destroy (BiffFormatData *d)
{
	g_free (d->name);
	g_free (d);
}

ExcelPaletteEntry const excel_default_palette [] = {
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
excel_get_default_palette (void)
{
	static ExcelPalette *pal = NULL;

	if (!pal) {
		int entries = EXCEL_DEF_PAL_LEN;
		d (3, fprintf (stderr,"Creating default palette\n"););

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
static void
excel_read_PALETTE (BiffQuery *q, ExcelWorkbook *ewb)
{
	int lp, len;
	ExcelPalette *pal;

	pal = g_new (ExcelPalette, 1);
	len = GSF_LE_GET_GUINT16 (q->data);
	pal->length = len;
	pal->red = g_new (int, len);
	pal->green = g_new (int, len);
	pal->blue = g_new (int, len);
	pal->gnum_cols = g_new (StyleColor *, len);

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

		pal->gnum_cols[lp] = NULL;
	}
	ewb->palette = pal;
}

StyleColor *
excel_palette_get (ExcelPalette const *pal, gint idx)
{
	/* return black on failure */
	g_return_val_if_fail (pal != NULL, style_color_black ());

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
		pal->gnum_cols[idx] =
			style_color_new_i8 ((guint8) pal->red[idx],
					    (guint8) pal->green[idx],
					    (guint8) pal->blue[idx]);
		g_return_val_if_fail (pal->gnum_cols[idx],
				      style_color_black ());
		d (1, {
			StyleColor *sc = pal->gnum_cols[idx];
			fprintf (stderr,"New color in slot %d: RGB= %x,%x,%x\n",
				idx, sc->red, sc->green, sc->blue);
		});
	}

	style_color_ref (pal->gnum_cols[idx]);
	return pal->gnum_cols[idx];
}

static void
excel_palette_destroy (ExcelPalette *pal)
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
excel_get_font (ExcelSheet *esheet, guint16 font_idx)
{
	BiffFontData const *fd = g_hash_table_lookup (esheet->container.ewb->font_data,
						      &font_idx);

	g_return_val_if_fail (fd != NULL, NULL); /* flag the problem */
	g_return_val_if_fail (fd->index != 4, NULL); /* should not exist */
	return fd;
}

static BiffXFData const *
excel_get_xf (ExcelSheet *esheet, int xfidx)
{
	BiffXFData *xf;
	GPtrArray const * const p = esheet->container.ewb->XF_cell_records;

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

static MStyle *
excel_get_style_from_xf (ExcelSheet *esheet, guint16 xfidx)
{
	BiffXFData const *xf = excel_get_xf (esheet, xfidx);
	BiffFontData const *fd;
	StyleColor	*pattern_color, *back_color, *font_color;
	int		 pattern_index,  back_index,  font_index;
	MStyle *mstyle;
	int i;

	d (2, fprintf (stderr,"XF index %d\n", xfidx););

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
	mstyle_set_shrink_to_fit (mstyle, xf->shrink_to_fit);
	mstyle_set_indent    (mstyle, xf->indent);
	mstyle_set_rotation  (mstyle, xf->rotation);

	/* Font */
	fd = excel_get_font (esheet, xf->font_idx);
	if (fd != NULL) {
		StyleUnderlineType underline = UNDERLINE_NONE;
		char const *subs_fontname = fd->fontname;
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
		font_index = 127; /* Default to Black */

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

	d (4, fprintf (stderr,"back = %d, pat = %d, font = %d, pat_style = %d\n",
		      back_index, pattern_index, font_index, xf->fill_pattern_idx););

	if (font_index == 127)
		font_color = style_color_auto_font ();
	else
		font_color = excel_palette_get (esheet->container.ewb->palette,
						font_index);

	switch (back_index) {
	case 64:
		back_color = sheet_style_get_auto_pattern_color
			(esheet->sheet);
		break;
	case 65:
		back_color = style_color_auto_back ();
		break;
	default:
		back_color = excel_palette_get (esheet->container.ewb->palette,
						back_index);
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
		pattern_color = excel_palette_get (esheet->container.ewb->palette,
						   pattern_index);
		break;
	}

	g_return_val_if_fail (back_color && pattern_color && font_color, NULL);

	d (4, fprintf (stderr,"back = #%02x%02x%02x, pat = #%02x%02x%02x, font = #%02x%02x%02x, pat_style = %d\n",
		      back_color->red>>8, back_color->green>>8, back_color->blue>>8,
		      pattern_color->red>>8, pattern_color->green>>8, pattern_color->blue>>8,
		      font_color->red>>8, font_color->green>>8, font_color->blue>>8,
		      xf->fill_pattern_idx););

	mstyle_set_color (mstyle, MSTYLE_COLOR_FORE, font_color);
	mstyle_set_color (mstyle, MSTYLE_COLOR_BACK, back_color);
	mstyle_set_color (mstyle, MSTYLE_COLOR_PATTERN, pattern_color);

	/* Borders */
	for (i = 0; i < STYLE_ORIENT_MAX; i++) {
		MStyle *tmp = mstyle;
		MStyleElementType const t = MSTYLE_BORDER_TOP + i;
		int const color_index = xf->border_color[i];
		StyleColor *color;

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
			color = excel_palette_get (esheet->container.ewb->palette,
						   color_index);
			break;
		}
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
excel_set_xf (ExcelSheet *esheet, int col, int row, guint16 xfidx)
{
	MStyle *const mstyle = excel_get_style_from_xf (esheet, xfidx);

	d (2, fprintf (stderr,"%s!%s%d = xf(%d)\n", esheet->sheet->name_unquoted,
		      col_name (col), row + 1, xfidx););

	if (mstyle == NULL)
		return;

	sheet_style_set_pos (esheet->sheet, col, row, mstyle);
}

static void
excel_set_xf_segment (ExcelSheet *esheet,
			 int start_col, int end_col,
			 int start_row, int end_row, guint16 xfidx)
{
	Range   range;
	MStyle * const mstyle  = excel_get_style_from_xf (esheet, xfidx);

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
	fprintf (stderr,"Unknown border style %d\n", b);
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
 * Default XF Data for early worksheets with little (no?) style info
 **/
static void
excel_workbook_add_XF (ExcelWorkbook *ewb)
{
	BiffXFData *xf = g_new (BiffXFData, 1);

	xf->font_idx = 0;
	xf->format_idx = 0;
	xf->style_format = NULL;

	xf->locked = 0;
	xf->hidden = 0;
	xf->xftype = MS_BIFF_X_STYLE;
	xf->format = MS_BIFF_F_MS;
	xf->parentstyle = 0;

	xf->halign = HALIGN_GENERAL;
	xf->valign = VALIGN_TOP;
	xf->rotation = 0;
	xf->indent = 0;
	xf->differences = 0;
	xf->pat_foregnd_col = 0;
	xf->pat_backgnd_col = 0;
	xf->fill_pattern_idx = 0;
	xf->border_type[STYLE_BOTTOM] = 0;
	xf->border_color[STYLE_BOTTOM] = 0;
	xf->border_type[STYLE_TOP] = 0;
	xf->border_color[STYLE_TOP] = 0;
	xf->border_type[STYLE_LEFT] = 0;
	xf->border_color[STYLE_LEFT] = 0;
	xf->border_type[STYLE_RIGHT] = 0;
	xf->border_color[STYLE_RIGHT] = 0;
	xf->border_type[STYLE_DIAGONAL] = 0;
	xf->border_color[STYLE_DIAGONAL] = 0;
	xf->border_type[STYLE_REV_DIAGONAL] = 0;
	xf->border_color[STYLE_REV_DIAGONAL] = 0;

	/* Init the cache */
	xf->mstyle = NULL; 

	g_ptr_array_add (ewb->XF_cell_records, xf);
}

/**
 * Parse the BIFF XF Data structure into a nice form, see S59E1E.HTM
 **/
static void
excel_read_XF (BiffQuery *q, ExcelWorkbook *ewb, MsBiffVersion ver)
{
	BiffXFData *xf = g_new (BiffXFData, 1);
	guint32 data, subdata;

	xf->font_idx = GSF_LE_GET_GUINT16 (q->data);
	xf->format_idx = GSF_LE_GET_GUINT16 (q->data + 2);
	xf->style_format = (xf->format_idx > 0)
		? excel_wb_get_fmt (ewb, xf->format_idx) : NULL;

	data = GSF_LE_GET_GUINT16 (q->data + 4);
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

	data = GSF_LE_GET_GUINT16 (q->data + 6);
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
		fprintf (stderr,"Unknown halign %d\n", subdata);
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
		fprintf (stderr,"Unknown valign %d\n", subdata);
		break;
	}

	if (ver >= MS_BIFF_V8) {
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

	if (ver >= MS_BIFF_V8) {
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
		case 0: xf->eastern = MS_BIFF_E_CONTEXT; break;
		case 1: xf->eastern = MS_BIFF_E_LEFT_TO_RIGHT; break;
		case 2: xf->eastern = MS_BIFF_E_RIGHT_TO_LEFT; break;
		default:
			fprintf (stderr,"Unknown location %d\n", subdata);
			break;
		}
	} else {
		xf->shrink_to_fit = FALSE;
		xf->indent = 0;
	}

	xf->differences = data & 0xFC00;

	if (ver >= MS_BIFF_V8) { /* Very different */
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
			?  diagonal_style : STYLE_BORDER_NONE;
		xf->border_type[STYLE_REV_DIAGONAL] = (has_diagonals & 0x1)
			?  diagonal_style : STYLE_BORDER_NONE;

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

	g_ptr_array_add (ewb->XF_cell_records, xf);
	d (2, fprintf (stderr,"XF(0x%x): Font %d, Format %d, Fore %d, Back %d, Pattern = %d\n",
		      ewb->XF_cell_records->len - 1,
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
excel_sheet_insert (ExcelSheet *esheet, int xfidx,
		    int col, int row, char const *text)
{
	Cell *cell;

	excel_set_xf (esheet, col, row, xfidx);

	if (text) {
		cell = sheet_cell_fetch (esheet->sheet, col, row);
		cell_set_value (cell, value_new_string (text));
	}
}

static GnmExpr const *
excel_formula_shared (BiffQuery *q, ExcelSheet *esheet, Cell *cell)
{
	guint16 opcode, data_len;
	Range   r;
	gboolean is_array;
	GnmExpr const *expr;
	guint8 const *data;
	XLSharedFormula *sf;

	if (!ms_biff_query_peek_next (q, &opcode) ||
	    ((0xff & opcode) != BIFF_SHRFMLA && (0xff & opcode) != BIFF_ARRAY)) {
		g_warning ("EXCEL: unexpected record '0x%x' after a formula in '%s'.",
			   opcode, cell_name (cell));
		return NULL;
	}

	ms_biff_query_next (q);

	is_array = (q->ls_op == BIFF_ARRAY);
	r.start.row	= GSF_LE_GET_GUINT16 (q->data + 0);
	r.end.row	= GSF_LE_GET_GUINT16 (q->data + 2);
	r.start.col	= GSF_LE_GET_GUINT8 (q->data + 4);
	r.end.col	= GSF_LE_GET_GUINT8 (q->data + 5);

	data = q->data + (is_array ? 14 : 10);
	data_len = GSF_LE_GET_GUINT16 (q->data + (is_array ? 12 : 8));
	expr = excel_parse_formula (
		&esheet->container, esheet, r.start.col, r.start.row,
		data, data_len, !is_array, NULL);

	sf = g_new (XLSharedFormula, 1);

	/* WARNING: Do NOT use the upper left corner as the hashkey.
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

	d (1, fprintf (stderr,"Shared formula, extent %s\n", range_name (&r)););

	g_hash_table_insert (esheet->shared_formulae, &sf->key, sf);

	g_return_val_if_fail (expr != NULL, FALSE);

	if (is_array)
		cell_set_array_formula (esheet->sheet,
					r.start.col, r.start.row,
					r.end.col,   r.end.row,
					expr);
	return expr;
}

/* See: S59D8F.HTM */
static void
excel_read_FORMULA (BiffQuery *q, ExcelSheet *esheet)
{
	/*
	 * NOTE: There must be _no_ path through this function that does
	 *       not set the cell value.
	 */

	/* Pre-retrieve incase this is a string */
	gboolean array_elem, is_string = FALSE;
	guint16 const xf_index = EX_GETXF (q);
	guint16 const col      = EX_GETCOL (q);
	guint16 const row      = EX_GETROW (q);
	guint16 const options  = GSF_LE_GET_GUINT16 (q->data + 14);
	guint16 expr_length;
	guint offset, val_offset;
	GnmExpr const *expr;
	Cell	 *cell;
	Value	 *val = NULL;

	excel_set_xf (esheet, col, row, xf_index);

	cell = sheet_cell_fetch (esheet->sheet, col, row);
	g_return_if_fail (cell != NULL);

	d (0, fprintf (stderr,"Formula in %s!%s;\n",
		      cell->base.sheet->name_quoted, cell_name (cell)););

	/* TODO TODO TODO: Wishlist
	 * We should make an array of minimum sizes for each BIFF type
	 * and have this checking done there.
	 */
	if (esheet->container.ver >= MS_BIFF_V5) {
		expr_length = GSF_LE_GET_GUINT16 (q->data + 20);
		offset = 22; val_offset = 6;
	} else if (esheet->container.ver >= MS_BIFF_V3) {
		expr_length = GSF_LE_GET_GUINT16 (q->data + 16);
		offset = 18; val_offset = 6;
	} else {
		expr_length = GSF_LE_GET_GUINT8 (q->data + 16);
		offset = 17; val_offset = 7;
	}

	if (q->length < offset) {
		fprintf (stderr,"FIXME: serious formula error: "
			"invalid FORMULA (0x%x) record with length %d (should >= %d)\n",
			q->opcode, q->length, offset);
		cell_set_value (cell, value_new_error (NULL, "Formula Error"));
		return;
	}
	if (q->length < (unsigned)(offset + expr_length)) {
		fprintf (stderr,"FIXME: serious formula error: "
			"supposed length 0x%x, real len 0x%x\n",
                        expr_length, q->length - offset);
		cell_set_value (cell, value_new_error (NULL, "Formula Error"));
		return;
	}

	/*
	 * Get the current value so that we can format, do this BEFORE handling
	 * shared/array formulas or strings in case we need to go to the next
	 * record
	 */
	if (GSF_LE_GET_GUINT16 (q->data + 12) != 0xffff) {
		double const num = gsf_le_get_double (q->data + val_offset);
		val = value_new_float (num);
	} else {
		guint8 const val_type = GSF_LE_GET_GUINT8 (q->data + val_offset);
		switch (val_type) {
		case 0: /* String */
			is_string = TRUE;
			break;

		case 1: { /* Boolean */
			guint8 v = GSF_LE_GET_GUINT8 (q->data + val_offset + 2);
			val = value_new_bool (v ? TRUE : FALSE);
			break;
		}

		case 2: { /* Error */
			guint8 const v = GSF_LE_GET_GUINT8 (q->data + val_offset + 2);
			val = value_new_error (NULL, biff_get_error_text (v));
			break;
		}

		case 3: /* Empty */
			/* TODO TODO TODO
			 * This is undocumented and a big guess, but it seems
			 * accurate.
			 */
			d (0, {
				fprintf (stderr,"%s:%s: has type 3 contents.  "
					"Is it an empty cell?\n",
					esheet->sheet->name_unquoted,
					cell_name (cell));
				if (ms_excel_read_debug > 5)
					gsf_mem_dump (q->data + 6, 8);
			});

			val = value_new_empty ();
			break;

		default:
			fprintf (stderr,"Unknown type (%x) for cell's (%s) current val\n",
				val_type, cell_name (cell));
		};
	}

	expr = excel_parse_formula (&esheet->container, esheet, col, row,
		(q->data + offset), expr_length, FALSE, &array_elem);
#if 0
	/* dump the trailing array data */
	gsf_mem_dump (q->data + offset + expr_length,
		      q->length - offset - expr_length);
#endif

	/* Error was flaged by parse_formula */
	if (expr == NULL && !array_elem)
		expr = excel_formula_shared (q, esheet, cell);

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
				 *
				 * NOTE : Apparently some apps actually store a
				 *        0 length string record for an empty.
				 *        DAMN! this was us!  we were screwing
				 *        up when exporting ""
				 */
				guint16 const len = (q->data != NULL) ? GSF_LE_GET_GUINT16 (q->data) : 0;

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
				val = value_new_string_nocopy (v);
			} else {
				EvalPos ep;
				val = value_new_error (eval_pos_init_cell (&ep, cell),
					"INVALID STRING");
				g_warning ("EXCEL: invalid STRING record in %s",
					cell_name (cell));
			}
		} else {
			/* There should be a STRING record here */
			EvalPos ep;
			val = value_new_error (eval_pos_init_cell (&ep, cell),
				"MISSING STRING");
			g_warning ("EXCEL: missing STRING record for %s",
				cell_name (cell));
		}
	}

	/* We MUST have a value */
	if (val == NULL) {
		EvalPos ep;
		val = value_new_error (eval_pos_init_cell (&ep, cell),
			"MISSING Value");
		g_warning ("EXCEL: Invalid state.  Missing Value in %s?",
			cell_name (cell));
	}

	if (cell_is_array (cell)) {
		/* Array expressions were already stored in the cells (without
		 * recalc), and without a value.  Handle either the first
		 * instance or the followers.
		 */
		if (expr == NULL && !array_elem) {
			g_warning ("EXCEL: How does cell %s have an array expression?",
				   cell_name (cell));
			cell_set_value (cell, val);
		} else
			cell_assign_value (cell, val);
	} else if (!cell_has_expr (cell)) {
		/* Just in case things screwed up, at least save the value */
		if (expr != NULL) {
			cell_set_expr_and_value (cell, expr, val, TRUE);
			gnm_expr_unref (expr);
		} else
			cell_assign_value (cell, val);
	} else {
		/*
		 * NOTE: Only the expression is screwed.
		 * The value and format can still be set.
		 */
		g_warning ("EXCEL: Shared formula problems in %s!%s",
			   cell->base.sheet->name_quoted, cell_name (cell));
		cell_set_value (cell, val);
	}

	/*
	 * 0x1 = AlwaysCalc
	 * 0x2 = CalcOnLoad
	 */
	if (options & 0x3)
		cell_queue_recalc (cell);
}

XLSharedFormula *
excel_sheet_shared_formula (ExcelSheet const *esheet,
			       CellPos const    *key)
{
	g_return_val_if_fail (esheet != NULL, NULL);

	d (5, fprintf (stderr,"FIND SHARED: %s\n", cellpos_as_string (key)););

	return g_hash_table_lookup (esheet->shared_formulae, key);
}

XLDataTable *
excel_sheet_data_table (ExcelSheet const *esheet,
			CellPos const    *key)
{
	g_return_val_if_fail (esheet != NULL, NULL);

	d (5, fprintf (stderr,"FIND DATA TABLE: %s\n", cellpos_as_string (key)););

	return g_hash_table_lookup (esheet->tables, key);
}

static void
excel_sheet_insert_val (ExcelSheet *esheet, int xfidx,
			   int col, int row, Value *v)
{
	BiffXFData const *xf = excel_get_xf (esheet, xfidx);

	g_return_if_fail (v);
	g_return_if_fail (esheet);
	g_return_if_fail (xf);

	excel_set_xf (esheet, col, row, xfidx);
	value_set_fmt (v, xf->style_format);
	cell_set_value (sheet_cell_fetch (esheet->sheet, col, row), v);
}

static void
excel_sheet_insert_blank (ExcelSheet *esheet, int xfidx,
			     int col, int row)
{
	g_return_if_fail (esheet);

	excel_set_xf (esheet, col, row, xfidx);
}

/* See: S59DAB.HTM */
static void
excel_read_NOTE (BiffQuery *q, ExcelSheet *esheet)
{
	CellPos	pos;

	pos.row = EX_GETROW (q);
	pos.col = EX_GETCOL (q);

	if (esheet->container.ver >= MS_BIFF_V8) {
		guint16  options = GSF_LE_GET_GUINT16 (q->data + 4);
		gboolean hidden = (options & 0x2)==0;
		guint16  obj_id  = GSF_LE_GET_GUINT16 (q->data + 6);
		guint16  author_len = GSF_LE_GET_GUINT16 (q->data + 8);
		char *author;

		if (options & 0xffd)
			fprintf (stderr,"FIXME: Error in options\n");

		author = biff_get_text (author_len % 2 ? q->data + 11 : q->data + 10,
					author_len, NULL);
		d (1, fprintf (stderr,"Comment at %s%d id %d options"
			      " 0x%x hidden %d by '%s'\n",
			      col_name (pos.col), pos.row + 1,
			      obj_id, options, hidden, author););

		g_free (author);
	} else {
		guint len = GSF_LE_GET_GUINT16 (q->data + 4);
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

		d (2, fprintf (stderr,"Comment in %s%d: '%s'\n",
			      col_name (pos.col), pos.row + 1, comment->str););

		cell_set_comment (esheet->sheet, &pos, NULL, comment->str);
		g_string_free (comment, FALSE);
	}
}

static void
excel_sheet_destroy (ExcelSheet *esheet)
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

	ms_container_finalize (&esheet->container);

	g_free (esheet);
}

static GnmExpr const *
ms_wb_parse_expr (MSContainer *container, guint8 const *data, int length)
{
	ExcelSheet dummy_sheet;

	dummy_sheet.container.ver = container->ver;
	dummy_sheet.container.ewb = (ExcelWorkbook *)container;
	dummy_sheet.sheet = NULL;
	dummy_sheet.shared_formulae = NULL;
	dummy_sheet.tables = NULL;
	return ms_sheet_parse_expr_internal (&dummy_sheet, data, length);
}

static StyleFormat *
ms_wb_get_fmt (MSContainer const *container, guint16 indx)
{
	return excel_wb_get_fmt (((ExcelWorkbook *)container), indx);
}

static ExcelWorkbook *
excel_workbook_new (MsBiffVersion ver, IOContext *context, WorkbookView *wbv)
{
	static MSContainerClass const vtbl = {
		NULL, NULL,
		&ms_wb_parse_expr,
		NULL,
		&ms_wb_get_fmt,
	};

	ExcelWorkbook *ewb = g_new (ExcelWorkbook, 1);

	ewb->expr_sharer = expr_tree_sharer_new ();

	ms_container_init (&ewb->container, &vtbl, NULL, ewb, ver);

	ewb->context = context;
	ewb->wbv = wbv;

	ewb->v8.supbook     = g_array_new (FALSE, FALSE, sizeof (ExcelSupBook));
	ewb->v8.externsheet = NULL;

	ewb->gnum_wb = NULL;
	ewb->boundsheet_sheet_by_index = g_ptr_array_new ();
	ewb->boundsheet_data_by_stream = g_hash_table_new_full (
		(GHashFunc)biff_guint32_hash, (GCompareFunc)biff_guint32_equal,
		NULL, (GDestroyNotify) biff_boundsheet_data_destroy);
	ewb->font_data        = g_hash_table_new_full (
		(GHashFunc)biff_guint16_hash, (GCompareFunc)biff_guint16_equal,
		NULL, (GDestroyNotify)biff_font_data_destroy);
	ewb->excel_sheets     = g_ptr_array_new ();
	ewb->XF_cell_records  = g_ptr_array_new ();
	ewb->format_data      = g_hash_table_new_full (
		(GHashFunc)biff_guint16_hash, (GCompareFunc)biff_guint16_equal,
		NULL, (GDestroyNotify)biff_format_data_destroy);
	ewb->palette          = excel_get_default_palette ();
	ewb->global_strings   = NULL;
	ewb->global_string_max = 0;

	ewb->warn_unsupported_graphs = TRUE;
	return ewb;
}

static ExcelSheet *
excel_workbook_get_sheet (ExcelWorkbook const *ewb, guint idx)
{
	if (idx < ewb->excel_sheets->len)
		return g_ptr_array_index (ewb->excel_sheets, idx);
	return NULL;
}

static void
excel_workbook_destroy (ExcelWorkbook *ewb)
{
	unsigned i, j;
	GSList *real_order = NULL;
	Sheet *sheet;

	for (i = ewb->boundsheet_sheet_by_index->len; i-- > 0 ; ) {
		sheet = g_ptr_array_index (ewb->boundsheet_sheet_by_index, i);
		if (sheet != NULL)
			real_order = g_slist_prepend (real_order, sheet);
	}

	if (real_order != NULL) {
		workbook_sheet_reorder (ewb->gnum_wb, real_order,  NULL);
		g_slist_free (real_order);
	}

	expr_tree_sharer_destroy (ewb->expr_sharer);

	g_hash_table_destroy (ewb->boundsheet_data_by_stream);
	ewb->boundsheet_data_by_stream = NULL;
	g_ptr_array_free (ewb->boundsheet_sheet_by_index, TRUE);
	ewb->boundsheet_sheet_by_index = NULL;

	for (i = 0; i < ewb->excel_sheets->len; i++)
		excel_sheet_destroy (g_ptr_array_index (ewb->excel_sheets, i));
	g_ptr_array_free (ewb->excel_sheets, TRUE);
	ewb->excel_sheets = NULL;

	for (i = 0; i < ewb->XF_cell_records->len; i++)
		biff_xf_data_destroy (g_ptr_array_index (ewb->XF_cell_records, i));
	g_ptr_array_free (ewb->XF_cell_records, TRUE);
	ewb->XF_cell_records = NULL;

	g_hash_table_destroy (ewb->font_data);
	ewb->font_data = NULL;

	g_hash_table_destroy (ewb->format_data);
	ewb->format_data = NULL;

	if (ewb->palette && ewb->palette != excel_get_default_palette ()) {
		excel_palette_destroy (ewb->palette);
		ewb->palette = NULL;
	}

	for (i = 0; i < ewb->v8.supbook->len; i++ ) {
		ExcelSupBook *sup = &(g_array_index (ewb->v8.supbook,
						     ExcelSupBook, i));
		for (j = 0; j < sup->externname->len; j++ )
			expr_name_unref (g_ptr_array_index (sup->externname, j));
		g_ptr_array_free (sup->externname, TRUE);
	}
	g_array_free (ewb->v8.supbook, TRUE);
	ewb->v8.supbook = NULL;
	if (ewb->v8.externsheet != NULL) {
		g_array_free (ewb->v8.externsheet, TRUE);
		ewb->v8.externsheet = NULL;
	}

	if (ewb->global_strings) {
		unsigned i;
		for (i = 0; i < ewb->global_string_max; i++)
			g_free (ewb->global_strings[i]);
		g_free (ewb->global_strings);
	}

	ms_container_finalize (&ewb->container);
	g_free (ewb);
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

	number = GSF_LE_GET_GUINT32 (ptr);
	type = (number & 0x3);
	switch (type) {
	case eIEEE:
	case eIEEEx100:
	{
		guint8 tmp[8];
		gnum_float answer;
		int lp;

		/* Think carefully about big/little endian issues before
		   changing this code.  */
		for (lp = 0; lp < 4; lp++) {
			tmp[lp + 4]= (lp > 0) ? ptr[lp]: (ptr[lp] & 0xfc);
			tmp[lp] = 0;
		}

		answer = (gnum_float)gsf_le_get_double (tmp);
		return value_new_float (type == eIEEEx100 ? answer / 100 : answer);
	}
	case eInt:
		return value_new_int (number >> 2);
	case eIntx100:
		number >>= 2;
		if ((number % 100) == 0)
			return value_new_int (number / 100);
		else
			return value_new_float ((gnum_float)number / 100);
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
excel_parse_name (ExcelWorkbook *ewb, Sheet *sheet, char *name,
		  guint8 const *expr_data, unsigned expr_len,
		  gboolean link_to_container)
{
	ParsePos pp;
	GnmNamedExpr *nexpr;
	GnmExpr const *expr = NULL;
	char *err = NULL;

	g_return_val_if_fail (name != NULL, NULL);

	/* expr_len == 0 seems to indicate a placeholder for an unknown name */
	if (expr_len != 0) {
		expr = excel_parse_formula (&ewb->container, NULL, 0, 0,
			expr_data, expr_len, FALSE, NULL);
		if (expr == NULL) {
			gnm_io_warning (ewb->context, _("Failure parsing name '%s'"), name);
			expr = gnm_expr_new_constant (value_new_error (NULL,
				gnumeric_err_REF));
		} else d (2, {
			char *tmp;
			ParsePos pp;

			tmp = gnm_expr_as_string (expr, parse_pos_init (&pp, ewb->gnum_wb, NULL, 0, 0));
			fprintf (stderr, "%s\n", tmp);
			g_free (tmp);
		});
	}

	parse_pos_init (&pp, ewb->gnum_wb, sheet, 0, 0);
	nexpr = expr_name_add (&pp, name, expr, &err, link_to_container);
	g_free (name);
	if (nexpr == NULL) {
		gnm_io_warning (ewb->context, err);
		g_free (err);
		return NULL;
	}

	return nexpr;
}


static void
excel_read_EXTERNNAME (BiffQuery *q, MSContainer *container)
{
	GnmNamedExpr		*nexpr = NULL;
	char *name = NULL;

	d (2, {
	   fprintf (stderr,"EXTERNAME\n");
	   gsf_mem_dump (q->data, q->length); });

	if (container->ver >= MS_BIFF_V7) {
		guint16 flags = GSF_LE_GET_GUINT8 (q->data);
		guint32 namelen = GSF_LE_GET_GUINT8 (q->data + 6);

		switch (flags & 0x18) {
		case 0x00: /* external name */

			if (flags & 1)
				name = g_strdup (excel_builtin_name (q->data + 7));
			if (name == NULL)
				name = biff_get_text (q->data + 7, namelen, &namelen);
			if (name != NULL) {
				unsigned expr_len = GSF_LE_GET_GUINT16 (q->data + 7 + namelen);
				guint8 const *expr_data = q->data + 9 + namelen;
				nexpr = excel_parse_name (container->ewb, 0,
					name, expr_data, expr_len, FALSE);
			}
			break;

		case 0x01: /* DDE */
			gnm_io_warning (container->ewb->context,
				_("DDE links are no supported.\nName '%s' will be lost.\n"),
				name);
			break;

		case 0x10: /* OLE */
			gnm_io_warning (container->ewb->context,
				_("OLE links are no supported.\nName '%s' will be lost.\n"),
				name);
			break;

		default:
			g_warning ("EXCEL: Invalid external name type. ('%s')", name);
			break;
		}
	} else { /* Ancient Papyrus spec. 
		name = biff_get_text (q->data + 1,
			GSF_LE_GET_GUINT8 (q->data), NULL);
			*/
	}

	/* nexpr is potentially NULL if there was an error */
	if (container->ver >= MS_BIFF_V8) {
		ExcelWorkbook *ewb = container->ewb;
		ExcelSupBook const *sup;

		g_return_if_fail (ewb->v8.supbook->len > 0);

		/* The name is associated with the last SUPBOOK records seen */
		sup = &(g_array_index (ewb->v8.supbook, ExcelSupBook,
				       ewb->v8.supbook->len-1));
		g_ptr_array_add (sup->externname, nexpr);
	} else {
		GPtrArray *a = container->names;
		if (a == NULL)
			a = container->names = g_ptr_array_new ();
		g_ptr_array_add (a, nexpr);
	}
}

/* Do some error checking to handle the magic name associated with an
 * autofilter in a sheet */
static void
excel_prepare_autofilter (ExcelWorkbook *ewb, GnmNamedExpr *nexpr)
{
	if (nexpr->pos.sheet != NULL) {
		Value *v = gnm_expr_get_range (nexpr->expr_tree);
		if (v != NULL) {
			GlobalRange r;
			gboolean valid = value_to_global_range (v, &r);
			value_release (v);

			if (valid) {
				(void) gnm_filter_new (r.sheet, &r.range);
				return;
			}
		}
	}
	gnm_io_warning (ewb->context, _("Failure parsing AutoFilter."));
}

static void
excel_read_NAME (BiffQuery *q, ExcelWorkbook *ewb)
{
	GPtrArray *a;
	GnmNamedExpr *nexpr = NULL;
	guint16 flags		= GSF_LE_GET_GUINT16 (q->data);
	/*guint8  kb_shortcut	= GSF_LE_GET_GUINT8  (q->data + 2); */
	guint32 name_len	= GSF_LE_GET_GUINT8  (q->data + 3);
	guint16 expr_len	= GSF_LE_GET_GUINT16 (q->data + 4);
	gboolean const builtin_name = (flags & 0x0020) ? TRUE : FALSE;

	guint16 sheet_index = 0;
	guint8 const *expr_data, *ptr = q->data + 14;
	char *name = NULL;
	/* int fn_grp_idx = (flags & 0xfc0)>>6; */

	if (ewb->container.ver >= MS_BIFF_V8) {
		sheet_index = GSF_LE_GET_GUINT16 (q->data + 8);
		ptr = q->data + 14;
		/* #!%&@ The header is before the id byte, and the len does not
		 * include the header */
		if (builtin_name) {
			name = g_strdup (excel_builtin_name (ptr+1));
			name_len = 2;
		}
	} else if (ewb->container.ver >= MS_BIFF_V7) {
		/* opencalc docs claim 8 is the right one, XL docs say 6 == 8
		 * pivot.xls suggests that at least for local builtin names 6
		 * is correct and 8 is bogus for == biff7 */
		sheet_index = GSF_LE_GET_GUINT16 (q->data + 6);
		ptr = q->data + 14;
		if (builtin_name)
			name = g_strdup (excel_builtin_name (ptr));
	} else {
		ptr = q->data + 5;
		if (builtin_name)
			name = g_strdup (excel_builtin_name (ptr));
	}

	d (2, {
	   fprintf (stderr,"NAME\n");
	   gsf_mem_dump (q->data, q->length); });

	if (name == NULL)
		name = biff_get_text (ptr, name_len, &name_len);

	if (name != NULL) {
		Sheet *sheet = NULL;
		d (1,
		fprintf (stderr, "NAME : %s\n", name);
		   fprintf (stderr, "%hu\n", sheet_index););
		if (sheet_index > 0) {
			/* NOTE : the docs lie the index for biff7 is
			 * indeed a reference to the externsheet
			 * however we have examples in biff8 that can
			 * only to be explained by a 1 based index to
			 * the boundsheets.  Which is not unreasonable
			 * given that these are local names */
			if (ewb->container.ver >= MS_BIFF_V8) {
				if (sheet_index <= ewb->boundsheet_sheet_by_index->len &&
				    sheet_index > 0)
					sheet = g_ptr_array_index (ewb->boundsheet_sheet_by_index, sheet_index-1);
				else
					g_warning ("So much for that theory");
			} else
				sheet = excel_externsheet_v7 (&ewb->container, sheet_index);
		}

		expr_data  = ptr + name_len;
		nexpr = excel_parse_name (ewb, sheet,
			name, expr_data, expr_len, TRUE);

		/* Add a ref to keep it around after the excel-sheet/wb goes
		 * away.  externames do not get references and are unrefed
		 * after import finishes, which destroys them if they are not
		 * in use. */
		if (nexpr != NULL) {
			expr_name_ref (nexpr);
			nexpr->is_hidden = (flags & 0x0001) ? TRUE : FALSE;

			/* Undocumented magic.
			 * XL stores a hidden name with the details of an autofilter */
			if (nexpr->is_hidden && !strcmp (nexpr->name->str, "_FilterDatabase"))
				excel_prepare_autofilter (ewb, nexpr);
		}
	}

	/* nexpr is potentially NULL if there was an error,
	 * and NAMES are always at the workbook level */
	a = ewb->container.names;
	if (a == NULL)
		a = ewb->container.names = g_ptr_array_new ();
	g_ptr_array_add (a, nexpr);

	d (5, {
		guint8  menu_txt_len	= GSF_LE_GET_GUINT8  (q->data + 10);
		guint8  descr_txt_len	= GSF_LE_GET_GUINT8  (q->data + 11);
		guint8  help_txt_len	= GSF_LE_GET_GUINT8  (q->data + 12);
		guint8  status_txt_len= GSF_LE_GET_GUINT8  (q->data + 13);
		char *menu_txt;
		char *descr_txt;
		char *help_txt;
		char *status_txt;

		ptr += name_len + expr_len;
		menu_txt = biff_get_text (ptr, menu_txt_len, NULL);
		ptr += menu_txt_len;
		descr_txt = biff_get_text (ptr, descr_txt_len, NULL);
		ptr += descr_txt_len;
		help_txt = biff_get_text (ptr, help_txt_len, NULL);
		ptr += help_txt_len;
		status_txt = biff_get_text (ptr, status_txt_len, NULL);

		fprintf (stderr,"Name record: '%s', '%s', '%s', '%s', '%s'\n",
			name ? name : "(null)",
			menu_txt ? menu_txt : "(null)",
			descr_txt ? descr_txt : "(null)",
			help_txt ? help_txt : "(null)",
			status_txt ? status_txt : "(null)");

		if ((flags & 0x0001) != 0) fprintf (stderr," Hidden");
		if ((flags & 0x0002) != 0) fprintf (stderr," Function");
		if ((flags & 0x0004) != 0) fprintf (stderr," VB-Proc");
		if ((flags & 0x0008) != 0) fprintf (stderr," Proc");
		if ((flags & 0x0010) != 0) fprintf (stderr," CalcExp");
		if ((flags & 0x0020) != 0) fprintf (stderr," BuiltIn");
		if ((flags & 0x1000) != 0) fprintf (stderr," BinData");
		fprintf (stderr,"\n");

		if (menu_txt)
			g_free (menu_txt);
		if (descr_txt)
			g_free (descr_txt);
		if (help_txt)
			g_free (help_txt);
		if (status_txt)
			g_free (status_txt);
	});
}

static void
excel_read_XCT (BiffQuery *q, ExcelWorkbook *ewb)
{
	guint16 count, last_col, opcode;
	guint8  const *data;
	unsigned len;
	Sheet *sheet = NULL;
	Cell  *cell;
	Value *v;
	EvalPos ep;

	if (ewb->container.ver >= MS_BIFF_V8) {
		guint16 supbook;

		g_return_if_fail (q->length == 4);

		count   = GSF_LE_GET_GUINT16 (q->data);
		supbook = GSF_LE_GET_GUINT16 (q->data+2);
	} else {
		g_return_if_fail (q->length == 2);

		count = GSF_LE_GET_GUINT16 (q->data);

	}

	if (sheet != NULL)
		eval_pos_init_sheet (&ep, sheet);

	while (count-- > 0) {
		if (!ms_biff_query_peek_next (q, &opcode)) {
			g_warning ("Expected a CRN record");
			return;
		} else if (opcode != BIFF_CRN) {
			g_warning ("Expected a CRN record not a %hu", opcode);
			return;
		}
		ms_biff_query_next (q);

		g_return_if_fail (q->length >= 4);

		ep.eval.col = GSF_LE_GET_GUINT8  (q->data+0);
		last_col    = GSF_LE_GET_GUINT8  (q->data+1);
		ep.eval.row = GSF_LE_GET_GUINT16 (q->data+2);

		/* ignore content for sheets that are already loaded */
		if (sheet == NULL)
			continue;

		for (data = q->data + 4; ep.eval.col <= last_col ; ep.eval.col++) {
			g_return_if_fail (data + 1 - q->data <= (int)q->length);

			switch (*data) {
			case  1: v = value_new_float (GSF_LE_GET_DOUBLE (data+1));
				 data += 9;
				 break;
			case  2: len = data[1];
				 v = value_new_string_nocopy (
					biff_get_text (data + 2, len, NULL));
				 data += 2 + len;
				 break;

			case  4: v = value_new_bool (GSF_LE_GET_GUINT16 (data+1) != 0);
				 data += 9;
				 break;

			case 16: v = value_new_error (&ep,
					biff_get_error_text (GSF_LE_GET_GUINT16 (data+1)));
				 data += 9;
				 break;

			default :
				g_warning ("Unknown oper type 0x%x in a CRN record", (int)*data);
				data++;
				v = NULL;
			};

			if (v != NULL) {
				cell = sheet_cell_fetch (sheet, ep.eval.col, ep.eval.row);
				cell_set_value (cell, v);
			}
		}
	}
}

/**
 * base_char_width_for_read:
 * @esheet	the Excel sheet
 * @xf_index	the src of the font to use
 * @is_default
 *
 * Measures base character width for column sizing.
 */
static double
base_char_width_for_read (ExcelSheet *esheet,
			  int xf_index, gboolean is_default)
{
	BiffXFData const *xf = excel_get_xf (esheet, xf_index);
	BiffFontData const *fd = (xf != NULL)
		? excel_get_font (esheet, xf->font_idx)
		: NULL;
	/* default to Arial 10 */
	char const * name = (fd != NULL) ? fd->fontname : "Arial";
	double const size = (fd != NULL) ? fd->height : 20.* 10.;

	return lookup_font_base_char_width (name, size, is_default);
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
excel_read_ROW (BiffQuery *q, ExcelSheet *esheet)
{
	guint16 const row = GSF_LE_GET_GUINT16 (q->data);
#if 0
	/* Unnecessary info for now.
	 * do we want to preallocate baed on this info?
	 */
	guint16 const start_col = GSF_LE_GET_GUINT16 (q->data + 2);
	guint16 const end_col = GSF_LE_GET_GUINT16 (q->data + 4) - 1;
#endif
	guint16 const height = GSF_LE_GET_GUINT16 (q->data + 6);
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data + 12);
	guint16 const flags2 = GSF_LE_GET_GUINT16 (q->data + 14);
	guint16 const xf = flags2 & 0xfff;

	/* If the bit is on it indicates that the row is of 'standard' height.
	 * However the remaining bits still include the size.
	 */
	gboolean const is_std_height = (height & 0x8000) != 0;

	d (1, {
		fprintf (stderr,"Row %d height 0x%x;\n", row + 1, height);
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
		sheet_row_set_size_pts (esheet->sheet, row, hu, TRUE);
	}

	if (flags & 0x20)
		colrow_set_visibility (esheet->sheet, FALSE, FALSE, row, row);

	if (flags & 0x80) {
		if (xf != 0)
			excel_set_xf_segment (esheet, 0, SHEET_MAX_COLS - 1,
						 row, row, xf);
		d (1, fprintf (stderr,"row %d has flags 0x%x a default style %hd;\n",
			      row + 1, flags, xf););
	}

	if ((unsigned)(flags & 0x7) > 0)
		colrow_set_outline (sheet_row_fetch (esheet->sheet, row),
			(unsigned)(flags & 0x7), flags & 0x10);
}

static void
excel_read_tab_color (BiffQuery *q, ExcelSheet *esheet)
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
	StyleColor *text_color;
	int contrast;

	g_return_if_fail (q->length == 20);

	/* be conservative for now, we have not seen a pallete larger than 56
	 * so this is largely moot, this is probably a uint32
	 */
	color_index = GSF_LE_GET_GUINT8 (q->data + 16);
	color = excel_palette_get (esheet->container.ewb->palette, color_index);
	contrast = color->color.red + color->color.green + color->color.blue;
	if (contrast >= 0x18000)
		text_color = style_color_black ();
	else
		text_color = style_color_white ();
	sheet_set_tab_color (esheet->sheet, color, text_color);
	if (color != NULL) {
		d (1, fprintf (stderr,"%s tab colour = %04hx:%04hx:%04hx\n",
			      esheet->sheet->name_unquoted,
			      color->red, color->green, color->blue););
	}
}

static void
excel_read_COLINFO (BiffQuery *q, ExcelSheet *esheet)
{
	int lp;
	float col_width;
	guint16 const firstcol = GSF_LE_GET_GUINT16 (q->data);
	guint16       lastcol  = GSF_LE_GET_GUINT16 (q->data + 2);
	guint16       width    = GSF_LE_GET_GUINT16 (q->data + 4);
	guint16 const xf       = GSF_LE_GET_GUINT16 (q->data + 6);
	guint16 const options  = GSF_LE_GET_GUINT16 (q->data + 8);
	gboolean      hidden   = (options & 0x0001) ? TRUE : FALSE;
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
		col_width = esheet->sheet->cols.default_style.size_pts;
	}

	d (1, {
		fprintf (stderr,"Column Formatting %s!%s of width "
		      "%hu/256 characters (%f pts) of size %f\n",
		      esheet->sheet->name_quoted,
		      cols_name (firstcol, lastcol), width,  col_width,
		      base_char_width_for_read (esheet, xf, FALSE));
		fprintf (stderr,"Options %hd, default style %hd\n", options, xf);
	});

	/* NOTE: seems like this is inclusive firstcol, inclusive lastcol */
	if (lastcol >= SHEET_MAX_COLS)
		lastcol = SHEET_MAX_COLS - 1;
	for (lp = firstcol; lp <= lastcol; ++lp) {
		sheet_col_set_size_pts (esheet->sheet, lp, col_width, TRUE);
		if (outline_level > 0)
			colrow_set_outline (sheet_col_fetch (esheet->sheet, lp),
				outline_level, collapsed);
	}

	if (xf != 0)
		excel_set_xf_segment (esheet, firstcol, lastcol,
				      0, SHEET_MAX_ROWS - 1, xf);

	if (hidden)
		colrow_set_visibility (esheet->sheet, TRUE, FALSE,
				       firstcol, lastcol);
}

void
excel_read_IMDATA (BiffQuery *q)
{
	guint16 op;
	guint32 image_len = GSF_LE_GET_GUINT32 (q->data + 4);
	d (1,{
		char const *from_name;
		char const *format_name;
		guint16 const format   = GSF_LE_GET_GUINT16 (q->data);
		guint16 const from_env = GSF_LE_GET_GUINT16 (q->data + 2);

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

		fprintf (stderr,"Picture from %s in %s format\n",
			from_name, format_name);
	});

	image_len += 8;
	while (image_len > q->length &&
	       ms_biff_query_peek_next (q, &op) && op == BIFF_CONTINUE) {
		image_len -= q->length;
		ms_biff_query_next (q);
	}

	g_return_if_fail (image_len == q->length);
}

/* S59DE2.HTM */
static void
excel_read_SELECTION (BiffQuery *q, ExcelSheet *esheet)
{
	/* FIXME : pane_number will be relevant for split panes.
	 * because frozen panes are bound together this does not matter.
	 */
	/* int const pane_number	= GSF_LE_GET_GUINT8 (q->data); */

	CellPos edit_pos, tmp;
	/* the range containing the edit_pos */
	int i, j = GSF_LE_GET_GUINT16 (q->data + 5);
	int num_refs = GSF_LE_GET_GUINT16 (q->data + 7);
	guint8 *refs;
	SheetView *sv = sheet_get_view (esheet->sheet, esheet->container.ewb->wbv);
	Range r;

	edit_pos.row = GSF_LE_GET_GUINT16 (q->data + 1);
	edit_pos.col = GSF_LE_GET_GUINT16 (q->data + 3);

	d (5, fprintf (stderr,"Start selection\n"););
	d (5, fprintf (stderr,"Cursor: %s in Ref #%d\n", cellpos_as_string (&edit_pos),
		      j););

	sv_selection_reset (sv);
	for (i = 0; i++ < num_refs ; ) {
		refs = q->data + 9 + 6 * (++j % num_refs);
		r.start.row = GSF_LE_GET_GUINT16 (refs + 0);
		r.end.row   = GSF_LE_GET_GUINT16 (refs + 2);
		r.start.col = GSF_LE_GET_GUINT8  (refs + 4);
		r.end.col   = GSF_LE_GET_GUINT8  (refs + 5);

		d (5, fprintf (stderr,"Ref %d = %s\n", i-1, range_name (&r)););

		tmp = (i == num_refs) ? edit_pos : r.start;
		sv_selection_add_range (sv,
			tmp.col, tmp.row,
			r.start.col, r.start.row,
			r.end.col, r.end.row);
	}

	d (5, fprintf (stderr,"Done selection\n"););
}

static void
excel_read_DEF_ROW_HEIGHT (BiffQuery *q, ExcelSheet *esheet)
{
	guint16 flags = 0;
	guint16 height = 0;
	double height_units;

	if (q->length >= 4) {
		flags  = GSF_LE_GET_GUINT16 (q->data);
		height = GSF_LE_GET_GUINT16 (q->data + 2);
	} else if (q->length == 2) {
		g_warning("TODO: Decipher earlier 2 byte DEFAULTROWHEIGHT");
		return;
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
excel_read_DEF_COL_WIDTH (BiffQuery *q, ExcelSheet *esheet)
{
	guint16 const width = GSF_LE_GET_GUINT16 (q->data);
	double def_font_width, col_width;

	/* Use the 'Normal' Style which is by definition the 0th */
	def_font_width = base_char_width_for_read (esheet, 0, TRUE);

	d (0, fprintf (stderr,"Default column width %hu characters\n", width););

	/*
	 * According to the tooltip the default width is 8.43 character widths
	 *   and does not include margins or the grid line.
	 * According to the saved data the default width is 8 character widths
	 *   includes the margins and grid line, but uses a different notion of
	 *   how big a char width is.
	 * According to saved data a column with the same size a the default has
	 *   9.00? char widths.
	 */
	col_width = width * def_font_width;

	sheet_col_set_default_size_pts (esheet->sheet, col_width);
}

/* we could get this implicitly from the cols/rows
 * but this is faster
 */
static void
excel_read_GUTS (BiffQuery *q, ExcelSheet *esheet)
{
	int col_gut, row_gut;

	g_return_if_fail (q->length == 8);

	/* ignore the specification of how wide/tall the gutters are */
	row_gut = GSF_LE_GET_GUINT16 (q->data + 4);
	if (row_gut >= 1)
		row_gut--;
	col_gut = GSF_LE_GET_GUINT16 (q->data + 6);
	if (col_gut >= 1)
		col_gut--;
	sheet_colrow_gutter (esheet->sheet, TRUE, col_gut);
	sheet_colrow_gutter (esheet->sheet, FALSE, row_gut);
}

/* See: S59DE3.HTM */
static void
excel_read_SETUP (BiffQuery *q, ExcelSheet *esheet)
{
	PrintInformation *pi = esheet->sheet->print_info;
	guint16  grbit;

	g_return_if_fail (q->length == 34);

	grbit = GSF_LE_GET_GUINT16 (q->data + 10);

	pi->print_order = (grbit & 0x1)
		? PRINT_ORDER_RIGHT_THEN_DOWN
		: PRINT_ORDER_DOWN_THEN_RIGHT;

	/* If the extra info is valid use it */
	if ((grbit & 0x4) != 0x4) {
		pi->n_copies = GSF_LE_GET_GUINT16 (q->data + 32);
		/* 0x40 == orientation is set */
		if ((grbit & 0x40) != 0x40) {
			pi->orientation = (grbit & 0x2)
				? PRINT_ORIENT_VERTICAL
				: PRINT_ORIENT_HORIZONTAL;
		}
		pi->scaling.percentage = GSF_LE_GET_GUINT16 (q->data + 2);
		if (pi->scaling.percentage < 1. || pi->scaling.percentage > 1000.) {
			g_warning ("setting invalid print scaling (%f) to 100%%",
				   pi->scaling.percentage);
			pi->scaling.percentage = 100.;
		}

#if 0
		/* Useful somewhere ? */
		fprintf (stderr,"Paper size %hu resolution %hu vert. res. %hu\n",
			GSF_LE_GET_GUINT16 (q->data +  0),
			GSF_LE_GET_GUINT16 (q->data + 12),
			GSF_LE_GET_GUINT16 (q->data + 14));
#endif
	}

	pi->print_black_and_white = (grbit & 0x8) == 0x8;
	pi->print_as_draft        = (grbit & 0x10) == 0x10;
	pi->print_comments        = (grbit & 0x20) == 0x20;

#if 0
	/* We probably can't map page->page accurately. */
	if ((grbit & 0x80) == 0x80)
		fprintf (stderr,"Starting page number %d\n",
			GSF_LE_GET_GUINT16 (q->data +  4));
#endif

	/* We do not support SIZE_FIT yet */
	pi->scaling.type = PERCENTAGE;
#if 0
	{
		guint16  fw, fh;
		fw = GSF_LE_GET_GUINT16 (q->data + 6);
		fh = GSF_LE_GET_GUINT16 (q->data + 8);
		if (fw > 0 && fh > 0) {
			pi->scaling.type = SIZE_FIT;
			pi->scaling.dim.cols = fw;
			pi->scaling.dim.rows = fh;
		}
	}
#endif

	print_info_set_margin_header 
		(pi, inches_to_points (gsf_le_get_double (q->data + 16)));
	print_info_set_margin_footer 
		(pi, inches_to_points (gsf_le_get_double (q->data + 24)));
}

static void
excel_read_MULRK (BiffQuery *q, ExcelSheet *esheet)
{
	guint32 col, row, lastcol;
	guint8 const *ptr = q->data;
	Value *v;

	row = GSF_LE_GET_GUINT16 (q->data);
	col = GSF_LE_GET_GUINT16 (q->data + 2);
	ptr += 4;
	lastcol = GSF_LE_GET_GUINT16 (q->data + q->length - 2);

	for (; col <= lastcol ; col++) {
		/* 2byte XF, 4 byte RK */
		v = biff_get_rk (ptr + 2);
		excel_sheet_insert_val (esheet,
			GSF_LE_GET_GUINT16 (ptr), col, row, v);
		ptr += 6;
	}
}

static void
excel_read_MULBLANK (BiffQuery *q, ExcelSheet *esheet)
{
	/* S59DA7.HTM is extremely unclear, this is an educated guess */
	int firstcol = EX_GETCOL (q);
	int const row = EX_GETROW (q);
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
excel_read_range (Range *r, guint8 const *data)
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
excel_read_MERGECELLS (BiffQuery *q, ExcelSheet *esheet)
{
	int num_merged = GSF_LE_GET_GUINT16 (q->data);
	guint8 const *data = q->data + 2;
	Range r;

	g_return_if_fail (q->length == (unsigned int)(2 + 8 * num_merged));

	while (num_merged-- > 0) {
		data = excel_read_range (&r, data);
		sheet_merge_add (esheet->sheet, &r, FALSE,
			COMMAND_CONTEXT (esheet->container.ewb->context));
	}
}

static void
excel_read_DIMENSIONS (BiffQuery *q, ExcelWorkbook *ewb)
{
	Range r;

	/* What the heck was a 0x00 ? */
	if (q->opcode != 0x200)
		return;

	if (ewb->container.ver >= MS_BIFF_V8) {
		r.start.row = GSF_LE_GET_GUINT32 (q->data);
		r.end.row   = GSF_LE_GET_GUINT32 (q->data + 4);
		r.start.col = GSF_LE_GET_GUINT16 (q->data + 8);
		r.end.col   = GSF_LE_GET_GUINT16 (q->data + 10);
	} else
		excel_read_range (&r, q->data);

	d (0, fprintf (stderr,"Dimension = %s\n", range_name (&r)););
}

static MSContainer *
sheet_container (ExcelSheet *esheet)
{
	ms_container_set_blips (&esheet->container, esheet->container.ewb->container.blips);
	return &esheet->container;
}

static gboolean
excel_read_PROTECT (BiffQuery *q, char const *obj_type)
{
	/* TODO: Use this information when gnumeric supports protection */
	gboolean is_protected = TRUE;

	/* MS Docs fail to mention that in some stream this
	 * record can have size zero.  I assume the in that
	 * case its existence is the flag.
	 */
	if (q->length > 0)
		is_protected = (1 == GSF_LE_GET_GUINT16 (q->data));

	d (1,if (is_protected) fprintf (stderr,"%s is protected\n", obj_type););

	return is_protected;
}

static void
excel_read_WSBOOL (BiffQuery *q, ExcelSheet *esheet)
{
	guint16 options;

	g_return_if_fail (q->length == 2);

	options = GSF_LE_GET_GUINT16 (q->data);
	/* 0x0001 automatic page breaks are visible */
	/* 0x0010 the sheet is a dialog sheet */
	/* 0x0020 automatic styles are not applied to an outline */
	esheet->sheet->outline_symbols_below = 0 != (options & 0x040);
	esheet->sheet->outline_symbols_right = 0 != (options & 0x080);
	/* 0x0100 the Fit option is on (Page Setup dialog box, Page tab) */
	esheet->sheet->display_outlines      = 0 != (options & 0x600);
}

static void
excel_read_CALCCOUNT (BiffQuery *q, ExcelWorkbook *ewb)
{
	guint16 count;

	g_return_if_fail (q->length == 2);

	count = GSF_LE_GET_GUINT16 (q->data);
	workbook_iteration_max_number (ewb->gnum_wb, count);
}

static void
excel_read_CALCMODE (BiffQuery *q, ExcelWorkbook *ewb)
{
	g_return_if_fail (q->length == 2);
	workbook_autorecalc_enable (ewb->gnum_wb,
		GSF_LE_GET_GUINT16 (q->data) != 0);
}

static void
excel_read_DELTA (BiffQuery *q, ExcelWorkbook *ewb)
{
	double tolerance;

	/* samples/excel/dbfuns.xls has as sample of this record */
	if (q->opcode == BIFF_UNKNOWN_1)
		return;

	g_return_if_fail (q->length == 8);

	tolerance = gsf_le_get_double (q->data);
	workbook_iteration_tolerance (ewb->gnum_wb, tolerance);
}

static void
excel_read_ITERATION (BiffQuery *q, ExcelWorkbook *ewb)
{
	guint16 enabled;

	g_return_if_fail (q->length == 2);

	enabled = GSF_LE_GET_GUINT16 (q->data);
	workbook_iteration_enabled (ewb->gnum_wb, enabled != 0);
}

static void
excel_read_PANE (BiffQuery *q, ExcelSheet *esheet, WorkbookView *wb_view)
{
	if (esheet->freeze_panes) {
		guint16 x = GSF_LE_GET_GUINT16 (q->data + 0);
		guint16 y = GSF_LE_GET_GUINT16 (q->data + 2);
		guint16 rwTop = GSF_LE_GET_GUINT16 (q->data + 4);
		guint16 colLeft = GSF_LE_GET_GUINT16 (q->data + 6);
		SheetView *sv = sheet_get_view (esheet->sheet, esheet->container.ewb->wbv);
		CellPos frozen, unfrozen;

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
		g_warning ("EXCEL : no support for split panes yet");
	}
}

static void
excel_read_WINDOW2 (BiffQuery *q, ExcelSheet *esheet, WorkbookView *wb_view)
{
	SheetView *sv = sheet_get_view (esheet->sheet, esheet->container.ewb->wbv);

	if (esheet->container.ver == MS_BIFF_V2) {
		g_warning("TODO: Decipher Biff2 WINDOW2");
		gsf_mem_dump (q->data, q->length);
		return;
	}
	if (q->length >= 10) {
		guint16 const options    = GSF_LE_GET_GUINT16 (q->data + 0);
		/* coords are 0 based */
		guint16 top_row    = GSF_LE_GET_GUINT16 (q->data + 2);
		guint16 left_col   = GSF_LE_GET_GUINT16 (q->data + 4);
		guint32 const biff_pat_col = GSF_LE_GET_GUINT32 (q->data + 6);

		esheet->sheet->display_formulas	= (options & 0x0001) != 0;
		esheet->sheet->hide_grid	= (options & 0x0002) == 0;
		esheet->sheet->hide_col_header  =
		esheet->sheet->hide_row_header	= (options & 0x0004) == 0;
		esheet->freeze_panes		= (options & 0x0008) != 0;
		esheet->sheet->hide_zero	= (options & 0x0010) == 0;

		/* NOTE : This is top left of screen even if frozen, modify when
		 *        we read PANE
		 */
		sv_set_initial_top_left (sv, left_col, top_row);

		if (!(options & 0x0020)) {
			StyleColor *pattern_color;
			if (esheet->container.ver >= MS_BIFF_V8) {
				/* Get style color from palette*/
				pattern_color = excel_palette_get (
					esheet->container.ewb->palette,
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
				      pattern_color->color.red,
				      pattern_color->color.green,
				      pattern_color->color.blue););
			sheet_style_set_auto_pattern_color (
				esheet->sheet, pattern_color);
		}

		d (0, if (options & 0x0200) fprintf (stderr,"Sheet flag selected\n"););

		if (options & 0x0400)
			wb_view_sheet_focus (wb_view, esheet->sheet);
	}

	if (q->length >= 14) {
		d (2, {
			guint16 const pageBreakZoom = GSF_LE_GET_GUINT16 (q->data + 10);
			guint16 const normalZoom = GSF_LE_GET_GUINT16 (q->data + 12);
			fprintf (stderr,"%hx %hx\n", normalZoom, pageBreakZoom);
		});
	}
}

static void
excel_read_CF (BiffQuery *q, ExcelSheet *esheet)
{
	guint8 const type	= GSF_LE_GET_GUINT8 (q->data + 0);
	guint8 const op		= GSF_LE_GET_GUINT8 (q->data + 1);
	guint16 const expr1_len	= GSF_LE_GET_GUINT8 (q->data + 2);
	guint16 const expr2_len	= GSF_LE_GET_GUINT8 (q->data + 4);
	guint8 const fmt_type	= GSF_LE_GET_GUINT8 (q->data + 9);
	unsigned offset;
	GnmExpr const *expr1 = NULL, *expr2 = NULL;

	d (1, fprintf (stderr,"cond type = %d, op type = %d\n", (int)type, (int)op););
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
				   op, esheet->sheet->name_unquoted);
			return;
		}
		break;
	case 2 : cond1 = SCO_BOOLEAN_EXPR;
		 break;

	default :
		g_warning ("EXCEL : Unknown condition type (%d) for format in sheet %s.",
			   (int)type, esheet->sheet->name_unquoted);
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
	gsf_mem_dump (q->data+6, 6);

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
		gsf_mem_dump (q->data+offset, 118);

		offset += 118;
	}

	if (fmt_type & 0x10) { /* borders */
		puts ("Border");
		gsf_mem_dump (q->data+offset, 8);

		offset += 8;
	}

	if (fmt_type & 0x20) { /* pattern */
		/* TODO : use the head flags to conditionally set things
		 * FIXME : test this
		 */
		guint16 tmp = GSF_LE_GET_GUINT16 (q->data + offset);
		int pat_foregnd_col = (tmp & 0x007f);
		int pat_backgnd_col = (tmp & 0x1f80) >> 7;
		int fill_pattern_idx;

		tmp = GSF_LE_GET_GUINT16 (q->data + offset + 2);
		fill_pattern_idx =
			excel_map_pattern_index_from_excel ((tmp >> 10) & 0x3f);

		/* Solid patterns seem to reverse the meaning */
		if (fill_pattern_idx == 1) {
			int swap = pat_backgnd_col;
			pat_backgnd_col = pat_foregnd_col;
			pat_foregnd_col = swap;
		}

		fprintf (stderr,"fore = %d, back = %d, pattern = %d.\n",
			pat_foregnd_col,
			pat_backgnd_col,
			fill_pattern_idx);

		offset += 4;
	}


	g_return_if_fail (q->length == offset + expr1_len + expr2_len);
	gsf_mem_dump (q->data+6, 6);

	if (expr1 != NULL) gnm_expr_unref (expr1);
	if (expr2 != NULL) gnm_expr_unref (expr2);
#if 0
	fprintf (stderr,"%d == %d (%d + %d + %d) (0x%x)\n",
		q->length, offset + expr1_len + expr2_len,
		offset, expr1_len, expr2_len, fmt_type);
#endif
}

static void
excel_read_CONDFMT (BiffQuery *q, ExcelSheet *esheet)
{
	guint16 num_fmts, options, num_areas;
	Range  region;
	unsigned i;
	guint8 const *data;

	g_return_if_fail (q->length >= 14);

	num_fmts = GSF_LE_GET_GUINT16 (q->data + 0);
	options  = GSF_LE_GET_GUINT16 (q->data + 2);
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
	for (i = 0 ; i < num_areas && (data+8) <= (q->data + q->length) ; i++)
		data = excel_read_range (&region, data);

	g_return_if_fail (data == q->data + q->length);

	for (i = 0 ; i < num_fmts ; i++) {
		guint16 next;
		if (!ms_biff_query_peek_next (q, &next) || next != BIFF_CF) {
			g_warning ("EXCEL: missing CF record");
			return;
		}
		ms_biff_query_next (q);
		excel_read_CF (q, esheet);
	}
}

static void
excel_read_DV (BiffQuery *q, ExcelSheet *esheet)
{
	GnmExpr const   *expr1 = NULL, *expr2 = NULL;
	int      	 expr1_len,     expr2_len;
	char *input_msg, *error_msg, *input_title, *error_title;
	guint32	options, len;
	guint8 const *data, *expr1_dat, *expr2_dat;
	guint8 const *end = q->data + q->length;
	int i;
	Range r;
	ValidationStyle style;
	ValidationType  type;
	ValidationOp    op;
	GSList *ptr, *ranges = NULL;
	MStyle *mstyle;

	g_return_if_fail (q->length >= 4);
	options	= GSF_LE_GET_GUINT32 (q->data);
	data = q->data + 4;

	g_return_if_fail (data+3 <= end);
	input_title = biff_get_text (data + 2, GSF_LE_GET_GUINT8 (data), &len);
	data += len + 2; if (len == 0) data++;

	g_return_if_fail (data+3 <= end);
	error_title = biff_get_text (data + 2, GSF_LE_GET_GUINT8 (data), &len);
	data += len + 2; if (len == 0) data++;

	g_return_if_fail (data+3 <= end);
	input_msg = biff_get_text (data + 2, GSF_LE_GET_GUINT8 (data), &len);
	data += len + 2; if (len == 0) data++;

	g_return_if_fail (data+3 <= end);
	error_msg = biff_get_text (data + 2, GSF_LE_GET_GUINT8 (data), &len);
	data += len + 2; if (len == 0) data++;

	d (1, {
		fprintf (stderr,"Input Title : '%s'\n", input_title);
		fprintf (stderr,"Input Msg   : '%s'\n", input_msg);
		fprintf (stderr,"Error Title : '%s'\n", error_title);
		fprintf (stderr,"Error Msg   : '%s'\n", error_msg);
	});

	g_return_if_fail (data+2 <= end);
	expr1_len = GSF_LE_GET_GUINT16 (data);
	d (5, fprintf (stderr,"Unknown = %hx\n", GSF_LE_GET_GUINT16 (data+2)););
	expr1_dat = data  + 4;	/* TODO : What are the missing 2 bytes ? */
	data += expr1_len + 4;

	g_return_if_fail (data+2 <= end);
	expr2_len = GSF_LE_GET_GUINT16 (data);
	d (5, fprintf (stderr,"Unknown = %hx\n", GSF_LE_GET_GUINT16 (data+2)););
	expr2_dat = data  + 4;	/* TODO : What are the missing 2 bytes ? */
	data += expr2_len + 4;

	g_return_if_fail (data+2 < end);
	i = GSF_LE_GET_GUINT16 (data);
	for (data += 2; i-- > 0 ;) {
		g_return_if_fail (data+8 <= end);
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

	d (1, fprintf (stderr,"style = %d, type = %d, op = %d\n",
		       style, type, op););

	mstyle = mstyle_new ();
	mstyle_set_validation (mstyle,
		validation_new (style, type, op, error_title, error_msg,
			expr1, expr2, options & 0x0100, options & 0x0200));
	if (input_msg != NULL || input_title != NULL)
	mstyle_set_input_msg (mstyle,
		gnm_input_msg_new (input_msg, input_title));

	for (ptr = ranges; ptr != NULL ; ptr = ptr->next) {
		Range *r = ptr->data;
		mstyle_ref (mstyle);
		sheet_style_apply_range (esheet->sheet, r, mstyle);
		d (1, range_dump (r, "\n"););
		g_free (r);
	}
	g_slist_free (ranges);
	mstyle_unref (mstyle);
	g_free (input_msg);
	g_free (error_msg);
	g_free (input_title);
	g_free (error_title);
}

static void
excel_read_DVAL (BiffQuery *q, ExcelSheet *esheet)
{
	guint16 options;
	guint32 input_coord_x, input_coord_y, drop_down_id, dv_count;
	unsigned i;

	g_return_if_fail (q->length == 18);

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

static guchar *
read_utf16_str (int word_len, guint8 const *data)
{
	int i;
	gunichar2 *uni_text = g_alloca (word_len * sizeof (gunichar2));

	/* be wary about endianness */
	for (i = 0 ; i < word_len ; i++, data += 2)
		uni_text [i] = GSF_LE_GET_GUINT16 (data);

	return g_utf16_to_utf8 (uni_text, word_len, NULL, NULL, NULL);
}

static void
excel_read_HLINK (BiffQuery *q, ExcelSheet *esheet)
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
	Range	r;
	guint32 options, len;
	guint16 next_opcode;
	guint8 const *data = q->data;
	guchar *label = NULL;
	guchar *target = NULL;
	guchar *tip = NULL;
	GnmHLink *link = NULL;

	g_return_if_fail (q->length > 32);

	r.start.row = GSF_LE_GET_GUINT16 (data +  0);
	r.end.row   = GSF_LE_GET_GUINT16 (data +  2);
	r.start.col = GSF_LE_GET_GUINT16 (data +  4);
	r.end.col   = GSF_LE_GET_GUINT16 (data +  6);
	options     = GSF_LE_GET_GUINT32 (data + 28);

	g_return_if_fail (!memcmp (data + 8, stdlink_guid, sizeof (stdlink_guid)));

	data += 32;
	/* label */
	if (options & 0x14) {
		len = GSF_LE_GET_GUINT32 (data);
		data += 4;
		g_return_if_fail (data + len*2 - q->data <= (int)q->length);
		label = read_utf16_str (len, data);
		data += len*2;
	}

	/* target frame */
	if (options & 0x8) {
		len = GSF_LE_GET_GUINT32 (data);
		data += 4;
		g_return_if_fail (len*2 + data - q->data <= (int)q->length);
		target = read_utf16_str (len, data);
		data += len*2;
	}

	if ((options & 0x1e3) == 0x003 && !memcmp (data, url_guid, sizeof (url_guid))) {
		guchar *url;

		data += 4 + sizeof (url_guid);
		len = GSF_LE_GET_GUINT32 (data);

		g_return_if_fail (len + data - q->data <= (int)q->length);

		url = read_utf16_str (len/2, data);
		link = g_object_new (gnm_hlink_url_get_type (), NULL);
		gnm_hlink_set_target (link, url);
		g_free (url);
	} else if ((options & 0x1e1) == 0x001 && !memcmp (data, file_guid, sizeof (file_guid))) {
		range_dump (&r, " <-- local file\n");

		data += sizeof (file_guid);
		len = GSF_LE_GET_GUINT32 (data + 2);
		fprintf (stderr,"up count %hu len %hx\n", GSF_LE_GET_GUINT16 (data), len);
		data += 6;

		gsf_mem_dump (data, q->length - (data - q->data));

		g_return_if_fail (len + data - q->data <= (int)q->length);
		data += len;

	} else if ((options & 0x1e3) == 0x103) {
		range_dump (&r, " <-- unc file\n");
	} else if ((options & 0x1eb) == 0x008) {
		link = g_object_new (gnm_hlink_cur_wb_get_type (), NULL);
		gnm_hlink_set_target (link, target);
	} else {
		g_warning ("Unknown hlink type");
	}
	if (ms_biff_query_peek_next (q, &next_opcode) &&
	    next_opcode == BIFF_LINK_TIP) {
		ms_biff_query_next (q);
		tip = read_utf16_str ((q->length - 10)/ 2, q->data + 10);
	}

	if (link != NULL) {
		MStyle *style = mstyle_new ();
		mstyle_set_hlink (style, link);
		sheet_style_apply_range	(esheet->sheet, &r, style);
		if (tip != NULL)
			gnm_hlink_set_tip  (link, tip);
	}

	g_free (label);
	g_free (target);
	g_free (tip);
}

static void
excel_read_BG_PIC (BiffQuery *q, ExcelSheet *esheet)
{
	/* Looks like a bmp.  OpenCalc has a basic parser for 24 bit files */
}

static Value *
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
	Value *res;

	*str_len = 0;
	*op = GNM_FILTER_UNUSED;
	switch (doper [0]) {
	case 0: return NULL; /* ignore */

	case 2: res = biff_get_rk (doper + 2);
		break;
	case 4: res = value_new_float (GSF_LE_GET_DOUBLE (doper+2));
		break;
	case 6: *str_len = doper[6];
		break;

	case 8: if (doper [2])
			res = value_new_error (NULL,
				    biff_get_error_text (doper [3]));
		else
			res = value_new_bool (doper [3] ? TRUE : FALSE);
		break;

	case 0xC: *op = GNM_FILTER_OP_BLANKS;
		return NULL;
	case 0xE: *op = GNM_FILTER_OP_NON_BLANKS;
		return NULL;
	};

	g_return_val_if_fail (doper[1] > 0 && doper [1] <=6, NULL);
	*op = ops [doper [1] - 1];

	if (*op == GNM_FILTER_OP_EQUAL && !is_equal)
		*op = GNM_FILTER_OP_REGEXP_MATCH;

	return res;
}

static void
excel_read_AUTOFILTER (BiffQuery *q, ExcelSheet *esheet)
{
	guint16 const flags = GSF_LE_GET_GUINT16 (q->data + 2);
	GnmFilterCondition *cond = NULL;
	GnmFilter	   *filter;

	/* XL only supports 1 filter per sheet */
	g_return_if_fail (esheet->sheet->filters != NULL);
	g_return_if_fail (esheet->sheet->filters->data != NULL);
	g_return_if_fail (esheet->sheet->filters->next == NULL);

	filter = esheet->sheet->filters->data;

	if (esheet->container.ver >= MS_BIFF_V8 && flags & 0x10)
		/* its a top/bottom n */
		cond = gnm_filter_condition_new_bucket (
			    (flags & 0x20) ? TRUE  : FALSE,
			    (flags & 0x40) ? FALSE : TRUE,
			    (flags >> 7) & 0x1ff);

	if (cond == NULL) {
		unsigned     len0, len1;
		GnmFilterOp  op0,  op1;
		guint8 const *data;
		Value *v0 = read_DOPER (q->data + 4,  flags & 4, &len0, &op0);
		Value *v1 = read_DOPER (q->data + 14, flags & 8, &len1, &op1);

		data = q->data + 24;
		if (len0 > 0) {
			v0 = value_new_string_nocopy (
				biff_get_text (data, len0, NULL));
			data += len0;
		}
		if (len1 > 0) {
			v1 = value_new_string_nocopy (
				biff_get_text (data, len1, NULL));
		}

		if (op1 == GNM_FILTER_UNUSED) {
			cond = gnm_filter_condition_new_single (op0, v0);
			if (v1 != NULL) value_release (v1); /* paranoia */
		} else
			cond = gnm_filter_condition_new_double (
				    op0, v0, (flags & 1) ? TRUE : FALSE, op1, v1);
	}

	gnm_filter_set_condition (filter,
		GSF_LE_GET_GUINT16 (q->data), cond, FALSE);
}

static void
excel_read_SCL (BiffQuery *q, ExcelSheet *esheet)
{
	unsigned num, denom;

	g_return_if_fail (q->length == 4);

	num = GSF_LE_GET_GUINT16 (q->data);
	denom = GSF_LE_GET_GUINT16 (q->data + 2);

	g_return_if_fail (denom != 0);

	sheet_set_zoom_factor (esheet->sheet,
		((double)num)/((double)denom), FALSE, FALSE);
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
excel_externsheet_v8 (ExcelWorkbook const *ewb, gint16 i)
{
	d (2, fprintf (stderr, "externv8 %hd\n", i););

	g_return_val_if_fail (ewb->v8.externsheet != NULL, NULL);

	if (i >= (int)ewb->v8.externsheet->len) {
		g_warning ("%hd >= %u\n", i, ewb->v8.externsheet->len);
		return NULL;
	}

	return &(g_array_index (ewb->v8.externsheet, ExcelExternSheetV8, i));
}

static Sheet *
supbook_get_sheet (ExcelWorkbook *ewb, gint16 sup_index, unsigned i)
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

	g_return_val_if_fail ((unsigned)sup_index < ewb->v8.supbook->len, NULL);

	/* supbook was self referential */
	if (g_array_index (ewb->v8.supbook, ExcelSupBook, sup_index).wb == NULL) {
		g_return_val_if_fail (i < ewb->boundsheet_sheet_by_index->len, NULL);
		sheet = g_ptr_array_index (ewb->boundsheet_sheet_by_index, i);
		g_return_val_if_fail (IS_SHEET (sheet), NULL);
	} else {
		/* supbook was an external reference */
	}

	return sheet;
}

static void
excel_read_EXTERNSHEET_v8 (BiffQuery const *q, ExcelWorkbook *ewb)
{
	ExcelExternSheetV8 *v8;
	gint16 sup_index;
	unsigned i, num, first, last;

	g_return_if_fail (ewb->container.ver >= MS_BIFF_V8);
	g_return_if_fail (ewb->v8.externsheet == NULL);
	num = GSF_LE_GET_GUINT16 (q->data);

	d (2, fprintf (stderr,"ExternSheet (%d entries)\n", num););
	d (10, gsf_mem_dump (q->data, q->length););

	ewb->v8.externsheet = g_array_set_size (
		g_array_new (FALSE, FALSE, sizeof (ExcelExternSheetV8)), num);

	for (i = 0; i < num; i++) {
		sup_index = (gint16)GSF_LE_GET_GUINT16 (q->data + 2 + i * 6 + 0);
		first	  = GSF_LE_GET_GUINT16 (q->data + 2 + i * 6 + 2);
		last	  = GSF_LE_GET_GUINT16 (q->data + 2 + i * 6 + 4);

		d (2, fprintf (stderr,"ExternSheet: sup = %hd First sheet 0x%x, Last sheet 0x%x\n",
			      sup_index, first, last););

		v8 = &g_array_index(ewb->v8.externsheet, ExcelExternSheetV8, i);
		v8->supbook = sup_index;
		v8->first = supbook_get_sheet (ewb, sup_index, first);
		v8->last  = supbook_get_sheet (ewb, sup_index, last);
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

	externsheets = container->v7.externsheet;
	g_return_val_if_fail (externsheets != NULL, NULL);
	g_return_val_if_fail (idx > 0, NULL);
	g_return_val_if_fail (idx <= (int)externsheets->len, NULL);

	return g_ptr_array_index (externsheets, idx-1);
}

void
excel_read_EXTERNSHEET_v7 (BiffQuery const *q, MSContainer *container)
{
	Sheet *sheet = NULL;
	/* unsigned const len  = GSF_LE_GET_GUINT8 (q->data); */
	unsigned const type = GSF_LE_GET_GUINT8 (q->data + 1);

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
		guint8 len = GSF_LE_GET_GUINT8 (q->data);
		char *name;
		
		/* opencalc screws up its export, overstating
		 * the length by 1 */
		if ((unsigned)(len+2) > q->length)
			len = q->length - 2;

		name = biff_get_text (q->data + 2, len, NULL);
		if (name != NULL) {
			sheet = workbook_sheet_by_name (container->ewb->gnum_wb, name);
			if (sheet == NULL) {
				sheet = sheet_new (container->ewb->gnum_wb, name);
				workbook_sheet_attach (container->ewb->gnum_wb, sheet, NULL);
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
		gnm_io_warning_unsupported_feature (container->ewb->context,
			_("external references"));
	};

	if (container->v7.externsheet == NULL)
		container->v7.externsheet = g_ptr_array_new ();
	g_ptr_array_add (container->v7.externsheet, sheet);
}

static gboolean
excel_read_sheet (BiffQuery *q, ExcelWorkbook *ewb,
		  WorkbookView *wb_view, ExcelSheet *esheet)
{
	MStyle *mstyle;
	PrintInformation *pi;

	g_return_val_if_fail (ewb != NULL, FALSE);
	g_return_val_if_fail (esheet != NULL, FALSE);
	g_return_val_if_fail (esheet->sheet != NULL, FALSE);
	g_return_val_if_fail (esheet->sheet->print_info != NULL, FALSE);

	pi = esheet->sheet->print_info;

	d (1, fprintf (stderr,"----------------- '%s' -------------\n",
		      esheet->sheet->name_unquoted););

	/* Apply the default style */
	mstyle = excel_get_style_from_xf (esheet, 15);
	if (mstyle != NULL) {
		Range r;
		sheet_style_set_range (esheet->sheet,
			range_init_full_sheet (&r), mstyle);
	}

	for (; ms_biff_query_next (q) ;
	     value_io_progress_update (ewb->context, q->streamPos)) {

		d (5, fprintf (stderr,"Opcode: 0x%x\n", q->opcode););

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
				GObject *graph = gnm_graph_new (esheet->container.ewb->gnum_wb);
				ms_excel_chart (q, sheet_container (esheet),
						esheet->container.ver,
						graph);
			} else
				puts ("EXCEL: How are we seeing chart records in a sheet ?");
			continue;
		} else if (q->ms_op == 0x01) {
			switch (q->opcode) {
			case BIFF_CONDFMT: excel_read_CONDFMT (q, esheet); break;
			case BIFF_CF:
				g_warning ("Found a CF record without a CONDFMT ??");
				excel_read_CF (q, esheet);
				break;
			case BIFF_DVAL:		excel_read_DVAL (q, esheet);  break;
			case BIFF_HLINK:	excel_read_HLINK (q, esheet); break;
			case BIFF_CODENAME:
				break;
			case BIFF_DV:
				g_warning ("Found a DV record without a DVal ??");
				excel_read_DV (q, esheet);
				break;
			default:
				excel_unexpected_biff (q, "Sheet", ms_excel_read_debug);
			};
			continue;
		}

		switch (q->ls_op) {
		case BIFF_DIMENSIONS:	/* 2, NOT 1,10 */
			excel_read_DIMENSIONS (q, ewb);
			break;

		case BIFF_BLANK: {
			guint16 const xf = EX_GETXF (q);
			guint16 const col = EX_GETCOL (q);
			guint16 const row = EX_GETROW (q);
			d (0, fprintf (stderr,"Blank in %s%d xf = 0x%x;\n", col_name (col), row + 1, xf););

			excel_sheet_insert_blank (esheet, xf, col, row);
			break;
		}

		case BIFF_INTEGER: { /* Extinct in modern Excel */
			Value *v = value_new_int (GSF_LE_GET_GUINT16 (q->data + 7));
			excel_sheet_insert_val (esheet, EX_GETXF (q), EX_GETCOL (q),
						   EX_GETROW (q), v);
			break;
		}

		case BIFF_NUMBER: { /* S59DAC.HTM */
			Value *v;
			if (esheet->container.ver == MS_BIFF_V2) {
				v = value_new_float (gsf_le_get_double (q->data + 7));
			} else {
				v = value_new_float (gsf_le_get_double (q->data + 6));
			}
			d (2, fprintf (stderr,"Read number %g\n",
				      value_get_as_float (v)););

			excel_sheet_insert_val (esheet, EX_GETXF (q), EX_GETCOL (q),
						   EX_GETROW (q), v);
			break;
		}

		case BIFF_LABEL: { /* See: S59D9D.HTM */
			char *label;
			if (esheet->container.ver == MS_BIFF_V2) {
				label = biff_get_text (q->data + 8, GSF_LE_GET_GUINT8 (q->data + 7), NULL);
			} else {
				label = biff_get_text (q->data + 8, EX_GETSTRLEN (q), NULL);
			}
			excel_sheet_insert (esheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q), label);
			g_free (label);
			break;
		}

		case BIFF_BOOLERR: { /* S59D5F.HTM */
			Value *v;
			CellPos pos;
			guint8 const val = GSF_LE_GET_GUINT8 (q->data + 6);

			pos.col = EX_GETCOL (q);
			pos.row = EX_GETROW (q);

			if (GSF_LE_GET_GUINT8 (q->data + 7)) {
				EvalPos ep;
				v = value_new_error (
					eval_pos_init (&ep, esheet->sheet, &pos),
					biff_get_error_text (val));
			} else
				v = value_new_bool (val);
			excel_sheet_insert_val (esheet,
				EX_GETXF (q), pos.col, pos.row, v);
			break;
		}

		case BIFF_FORMULA:	excel_read_FORMULA (q, esheet);	break;
		/* case STRING : is handled elsewhere since it always follows FORMULA */
		case BIFF_ROW:	excel_read_ROW (q, esheet);		break;
		case BIFF_EOF:		return TRUE;

		/* NOTE : bytes 12 & 16 appear to require the non decrypted data */
		case BIFF_INDEX:	break;

		case BIFF_CALCCOUNT:	excel_read_CALCCOUNT (q, ewb);	break;
		case BIFF_CALCMODE:	excel_read_CALCMODE (q,ewb);	break;

		case BIFF_PRECISION : {
#if 0
			/* FIXME: implement in gnumeric */
			/* state of 'Precision as Displayed' option */
			guint16 const data = GSF_LE_GET_GUINT16 (q->data);
			gboolean const prec_as_displayed = (data == 0);
#endif
			break;
		}

		case BIFF_REFMODE:	break;
		case BIFF_DELTA:	excel_read_DELTA (q, ewb);	break;
		case BIFF_ITERATION:	excel_read_ITERATION (q, ewb);	break;
		case BIFF_PROTECT:	excel_read_PROTECT (q, "Sheet"); break;

		case BIFF_PASSWORD:
			if (q->length == 2) {
				d (2, fprintf (stderr,"sheet password '%hx'\n",
					      GSF_LE_GET_GUINT16 (q->data)););
			}
			break;



		case BIFF_HEADER: { /* FIXME: S59D94 */
			if (q->length) {
				char *str = biff_get_text (q->data + 1,
					GSF_LE_GET_GUINT8 (q->data), NULL);
				d (2, fprintf (stderr,"Header '%s'\n", str););
				g_free (str);
			}
		}
		break;

		case BIFF_FOOTER: { /* FIXME: S59D8D */
			if (q->length) {
				char *str = biff_get_text (q->data + 1,
					GSF_LE_GET_GUINT8 (q->data), NULL);
				d (2, fprintf (stderr,"Footer '%s'\n", str););
				g_free (str);
			}
		}
		break;

		case BIFF_EXTERNCOUNT: /* ignore */ break;
		case BIFF_EXTERNSHEET: /* These cannot be biff8 */
			excel_read_EXTERNSHEET_v7 (q, &esheet->container);
			break;

		case BIFF_VERTICALPAGEBREAKS:	break;
		case BIFF_HORIZONTALPAGEBREAKS:	break;

		case BIFF_NOTE:		excel_read_NOTE (q, esheet);	  	break;
		case BIFF_SELECTION:	excel_read_SELECTION (q, esheet);	break;
		case BIFF_EXTERNNAME:
			excel_read_EXTERNNAME (q, &esheet->container);
			break;
		case BIFF_DEFAULTROWHEIGHT:
			excel_read_DEF_ROW_HEIGHT (q, esheet);
			break;

		case BIFF_LEFT_MARGIN:
			print_info_set_margin_left 
				(pi, inches_to_points (gsf_le_get_double (q->data)));
			break;
		case BIFF_RIGHT_MARGIN:
			print_info_set_margin_right
				(pi, inches_to_points (gsf_le_get_double (q->data)));
			break;
		case BIFF_TOP_MARGIN:
			excel_print_unit_init_inch (&pi->margins.top,
				gsf_le_get_double (q->data));
			break;
		case BIFF_BOTTOM_MARGIN:
			excel_print_unit_init_inch (&pi->margins.bottom,
				gsf_le_get_double (q->data));
			break;

		case BIFF_PRINTHEADERS:
			break;

		case BIFF_PRINTGRIDLINES:
			pi->print_grid_lines = (GSF_LE_GET_GUINT16 (q->data) == 1);
			break;

		case BIFF_WINDOW1:	break; /* what does this do for a sheet ? */
		case BIFF_WINDOW2:	excel_read_WINDOW2 (q, esheet, wb_view); break;
		case BIFF_BACKUP:	break;
		case BIFF_PANE:		excel_read_PANE (q, esheet, wb_view);	 break;

		case BIFF_PLS:
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
			break;

		case BIFF_DEFCOLWIDTH:	excel_read_DEF_COL_WIDTH (q, esheet); break;

		case BIFF_OBJ:	ms_read_OBJ (q, sheet_container (esheet), NULL); break;

		case BIFF_SAVERECALC:	break;
		case BIFF_TAB_COLOR:	excel_read_tab_color (q, esheet);	break;
		case BIFF_OBJPROTECT:	excel_read_PROTECT (q, "Sheet");	break;
		case BIFF_COLINFO:	excel_read_COLINFO (q, esheet);		break;

		case BIFF_RK: { /* See: S59DDA.HTM */
			Value *v = biff_get_rk (q->data + 6);
			d (2, {
				fprintf (stderr,"RK number: 0x%x, length 0x%x\n", q->opcode, q->length);
				gsf_mem_dump (q->data, q->length);
			});

			excel_sheet_insert_val (esheet, EX_GETXF (q), EX_GETCOL (q),
						   EX_GETROW (q), v);
			break;
		}

		case BIFF_IMDATA:	excel_read_IMDATA (q); break;
		case BIFF_GUTS:		excel_read_GUTS (q, esheet);		break;
		case BIFF_WSBOOL:	excel_read_WSBOOL (q, esheet);		break;
		case BIFF_GRIDSET:		break;

		case BIFF_HCENTER:
			pi->center_horizontally = GSF_LE_GET_GUINT16 (q->data) == 0x1;
			break;
		case BIFF_VCENTER:
			pi->center_vertically   = GSF_LE_GET_GUINT16 (q->data) == 0x1;
			break;

		case BIFF_COUNTRY:		break;
		case BIFF_STANDARDWIDTH:	break; /* the 'standard width dialog' ? */
		case BIFF_FILTERMODE:		break;
		case BIFF_AUTOFILTERINFO:	break;
		case BIFF_AUTOFILTER:	excel_read_AUTOFILTER (q, esheet);	break;
		case BIFF_SCL:		excel_read_SCL (q, esheet);		break;
		case BIFF_SETUP:	excel_read_SETUP (q, esheet);		break;
		case BIFF_GCW:			break;
		case BIFF_SCENMAN:		break;
		case BIFF_SCENARIO:		break;
		case BIFF_MULRK: 	excel_read_MULRK (q, esheet);		break;
		case BIFF_MULBLANK: 	excel_read_MULBLANK (q, esheet);	break;

		case BIFF_RSTRING: { /* See: S59DDC.HTM */
			guint16 const xf = EX_GETXF (q);
			guint16 const col = EX_GETCOL (q);
			guint16 const row = EX_GETROW (q);
			char *txt = biff_get_text (q->data + 8, EX_GETSTRLEN (q), NULL);
			d (0, fprintf (stderr,"Rstring in %s%d xf = 0x%x;\n",
				      col_name (col), row + 1, xf););

			excel_sheet_insert (esheet, xf, col, row, txt);
			g_free (txt);
			break;
		}

		/* S59D6D.HTM,  Can be ignored on read side */
		case BIFF_DBCELL:						break;

		case BIFF_BG_PIC:	excel_read_BG_PIC (q, esheet);		break;
		case BIFF_MERGECELLS:	excel_read_MERGECELLS (q, esheet);	break;

		case BIFF_MS_O_DRAWING:
		case BIFF_MS_O_DRAWING_GROUP:
		case BIFF_MS_O_DRAWING_SELECTION:
			ms_escher_parse (q, sheet_container (esheet));
			break;
		case BIFF_PHONETIC:	break;

		case BIFF_LABELSST: { /* See: S59D9E.HTM */
			guint32 const idx = GSF_LE_GET_GUINT32 (q->data + 6);

			if (esheet->container.ewb->global_strings && idx < esheet->container.ewb->global_string_max) {
				char const *str = esheet->container.ewb->global_strings[idx];

				/* FIXME FIXME FIXME: Why would there be a NULL?  */
				if (str == NULL)
					str = "";
				excel_sheet_insert_val (esheet, EX_GETXF (q), EX_GETCOL (q), EX_GETROW (q),
							value_new_string (str));
			} else
				fprintf (stderr,"string index 0x%u >= 0x%x\n",
					idx, esheet->container.ewb->global_string_max);
			break;
		}

		/* Found in worksheet only in XLS <= BIFF 4 */
		case BIFF_XF_OLD:	excel_workbook_add_XF (ewb);	break;
		case BIFF_NAME:		excel_read_NAME (q, ewb);	break;
		case BIFF_FONT:		excel_read_FONT (q, ewb);	break;
		case BIFF_FORMAT:	excel_read_FORMAT (q, ewb);	break;
		case BIFF_1904:		excel_read_1904 (q, ewb);	break;

		default:
			excel_unexpected_biff (q, "Sheet", ms_excel_read_debug);
		}
	}

	fprintf (stderr,"Error, hit end without EOF\n");

	return FALSE;
}

/* see S59DEC.HTM */
static void
excel_read_SUPBOOK (BiffQuery *q, ExcelWorkbook *ewb)
{
	unsigned const numTabs = GSF_LE_GET_GUINT16 (q->data);
	unsigned len = GSF_LE_GET_GUINT16 (q->data + 2);
	unsigned i;
	guint32 byte_length;
	char *name;
	guint8 encodeType, *data;
	ExcelSupBook tmp;

	d (2, fprintf (stderr,"supbook %d has %d\n", ewb->v8.supbook->len, numTabs););

	tmp.externname = g_ptr_array_new ();
	tmp.wb = NULL;

	/* undocumented guess */
	if (q->length == 4 && len == 0x0401) {
		d (2, fprintf (stderr,"\t is self referential\n"););
		tmp.type = EXCEL_SUP_BOOK_SELFREF;
		g_array_append_val (ewb->v8.supbook, tmp);
		return;
	}
	if (q->length == 4 && len == 0x3A01) {
		d (2, fprintf (stderr,"\t is a plugin\n"););
		tmp.type = EXCEL_SUP_BOOK_PLUGIN;
		g_array_append_val (ewb->v8.supbook, tmp);
		return;
	}

	tmp.type = EXCEL_SUP_BOOK_STD;
	g_array_append_val (ewb->v8.supbook, tmp);
	encodeType = GSF_LE_GET_GUINT8 (q->data + 4);
	d (1, {
		fprintf (stderr,"Supporting workbook with %d Tabs\n", numTabs);
		fprintf (stderr,"--> SUPBOOK VirtPath encoding = ");
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
			fprintf (stderr,"Unknown/Unencoded?  (%x) %d\n",
				encodeType, len);
		};
	});

	gsf_mem_dump (q->data + 4 + 1, len);
	for (data = q->data + 4 + 1 + len, i = 0; i < numTabs ; i++) {
		len = GSF_LE_GET_GUINT16 (data);
		name = biff_get_text (data + 2, len, &byte_length);
		fprintf (stderr,"\t-> %s\n", name);
		g_free (name);
		data += byte_length + 2;
	}
}

/*
 * See: S59E17.HTM
 */
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
					.5 + width * application_display_dpi_get (TRUE) / (72. * 20.),
					.5 + height * application_display_dpi_get (FALSE) / (72. * 20.));

		if (options & 0x0001)
			fprintf (stderr,"Unsupported: Hidden workbook\n");
		if (options & 0x0002)
			fprintf (stderr,"Unsupported: Iconic workbook\n");
		wb_view->show_horizontal_scrollbar = (options & 0x0008);
		wb_view->show_vertical_scrollbar = (options & 0x0010);
		wb_view->show_notebook_tabs = (options & 0x0020);
	}
}

static ExcelWorkbook *
excel_read_BOF (BiffQuery	 *q,
		ExcelWorkbook	 *ewb,
		WorkbookView	 *wb_view,
		IOContext	 *context,
		MsBiffBofData	**version, int *current_sheet)
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
		ewb = excel_workbook_new (ver->version, context, wb_view);
		ewb->gnum_wb = wb_view_workbook (wb_view);
		if (ver->version >= MS_BIFF_V8) {
			guint32 ver = GSF_LE_GET_GUINT32 (q->data + 4);
			if (ver == 0x4107cd18)
				fprintf (stderr,"Excel 2000 ?\n");
			else
				fprintf (stderr,"Excel 97 +\n");
		} else if (ver->version >= MS_BIFF_V7)
			fprintf (stderr,"Excel 95\n");
		else if (ver->version >= MS_BIFF_V5)
			fprintf (stderr,"Excel 5.x\n");
		else if (ver->version >= MS_BIFF_V4)
			fprintf (stderr,"Excel 4.x\n");
		else if (ver->version >= MS_BIFF_V3)
			fprintf (stderr,"Excel 3.x - shouldn't happen\n");
		else if (ver->version >= MS_BIFF_V2)
			fprintf (stderr,"Excel 2.x - shouldn't happen\n");
	} else if (ver->type == MS_BIFF_TYPE_Worksheet && ewb == NULL) {
		/* Top level worksheets existed up to & including 4.x */
		ExcelSheet *esheet;
		ewb = excel_workbook_new (ver->version, context, wb_view);
		ewb->gnum_wb = wb_view_workbook (wb_view);
		if (ver->version >= MS_BIFF_V5)
			fprintf (stderr, "Excel 5+ - shouldn't happen\n");
		else if (ver->version >= MS_BIFF_V4)
			fprintf (stderr, "Excel 4.x single worksheet\n");
		else if (ver->version >= MS_BIFF_V3)
			fprintf (stderr, "Excel 3.x single worksheet\n");
		else if (ver->version >= MS_BIFF_V2)
			fprintf (stderr, "Excel 2.x single worksheet\n");

		esheet= excel_sheet_new (ewb, "Worksheet");
		excel_workbook_add_XF (ewb);
		excel_read_sheet (q, ewb, wb_view, esheet);

	} else if (ver->type == MS_BIFF_TYPE_Worksheet) {
		BiffBoundsheetData *bsh =
			g_hash_table_lookup (ewb->boundsheet_data_by_stream,
					     &q->streamPos);
		if (bsh  || ver->version == MS_BIFF_V4) {
			ExcelSheet *esheet = excel_workbook_get_sheet (ewb, *current_sheet);
			esheet->container.ver = ver->version;
			excel_read_sheet (q, ewb, wb_view, esheet);
			ms_container_realize_objs (sheet_container (esheet));

			(*current_sheet)++;
		} else
			fprintf (stderr,"Sheet offset in stream of %x not found in list\n", q->streamPos);
	} else if (ver->type == MS_BIFF_TYPE_Chart) {
		GObject *graph =
#if 0
			/* enable when we support workbooklevel objects */
			gnm_graph_new (ewb->gnum_wb);
#else
			NULL;
#endif
		ms_excel_chart (q, &ewb->container, ver->version,
				graph);
	} else if (ver->type == MS_BIFF_TYPE_VBModule ||
		 ver->type == MS_BIFF_TYPE_Macrosheet) {
		/* Skip contents of Module, or MacroSheet */
		if (ver->type != MS_BIFF_TYPE_Macrosheet)
			fprintf (stderr,"VB Module.\n");
		else
			fprintf (stderr,"XLM Macrosheet.\n");

		while (ms_biff_query_next (q) &&
		       q->opcode != BIFF_EOF)
		    ;
		if (q->opcode != BIFF_EOF)
			g_warning ("EXCEL: file format error.  Missing BIFF_EOF");
	} else if (ver->type == MS_BIFF_TYPE_Workspace) {
		/* Multiple sheets, XLW format from Excel 4.0 */
		ewb = excel_workbook_new (ver->version, context, wb_view);
		ewb->gnum_wb = wb_view_workbook (wb_view);
		excel_workbook_add_XF (ewb);
	} else
		fprintf (stderr,"Unknown BOF (%x)\n", ver->type);

	return ewb;
}

void
excel_read_workbook (IOContext *context, WorkbookView *wb_view,
		     GsfInput *input, gboolean *is_double_stream_file)
{
	ExcelWorkbook *ewb = NULL;
	BiffQuery *q;
	MsBiffBofData *ver = NULL;
	int current_sheet = 0;
	char *problem_loading = NULL;
	gboolean stop_loading = FALSE;
	gboolean prev_was_eof = FALSE;

	io_progress_message (context, _("Reading file..."));
	value_io_progress_set (context, gsf_input_size (input), N_BYTES_BETWEEN_PROGRESS_UPDATES);
	q = ms_biff_query_new (input);

	/* default to ansi in case the file does not contain a CODEPAGE record */
	g_return_if_fail (current_workbook_iconv == NULL);
	current_workbook_iconv = excel_iconv_open_for_import (1252);

	*is_double_stream_file = FALSE;
	while (!stop_loading &&		  /* we have not hit the end */
	       problem_loading == NULL && /* there were no problems so far */
	       ms_biff_query_next (q)) {  /* we can load the record */

		d (5, fprintf (stderr,"Opcode: 0x%x\n", q->opcode););

		/* Catch Oddballs
		 * The heuristic seems to be that 'version 1' BIFF types
		 * are unique and not versioned.
		 */
		if (0x1 == q->ms_op) {
			switch (q->opcode) {
			case BIFF_DSF:		/* stored in the biff8 workbook */
			case BIFF_XL5MODIFY:	/* stored in the biff5/7 book */
				d (0, fprintf (stderr, "Double stream file : %d\n",
					       GSF_LE_GET_GUINT16 (q->data)););
				if (GSF_LE_GET_GUINT16 (q->data))
					*is_double_stream_file = TRUE;
				break;

			case BIFF_XL9FILE:
				d (0, puts ("XL 2000 file"););
				break;

			case BIFF_RECALCID:	break;
			case BIFF_REFRESHALL:	break;
			case BIFF_CODENAME:	break;
			case BIFF_PROT4REVPASS: break;

			case BIFF_USESELFS:	break;
			case BIFF_TABID:	break;
			case BIFF_PROT4REV:
				break;


			case BIFF_SUPBOOK:	excel_read_SUPBOOK (q, ewb); break;

			default:
				excel_unexpected_biff (q, "Workbook", ms_excel_read_debug);
			}
		} else switch (q->ls_op) {
		case BIFF_BOF:
			ewb = excel_read_BOF (q, ewb, wb_view, context, &ver, &current_sheet);
			break;

		case BIFF_EOF:
			prev_was_eof = TRUE;
			d (0, fprintf (stderr,"End of worksheet spec.\n"););
			break;

		case BIFF_FONT:		excel_read_FONT (q, ewb);			break;
		case BIFF_WINDOW1:	excel_read_WINDOW1 (q, wb_view);		break;
		case BIFF_BOUNDSHEET:	excel_read_BOUNDSHEET (q, ewb, ver->version);	break;
		case BIFF_PALETTE:	excel_read_PALETTE (q, ewb);			break;

		case BIFF_XF_OLD: /* see S59E1E.HTM */
		case BIFF_XF:		excel_read_XF (q, ewb, ver->version);		break;

		case BIFF_EXTERNCOUNT:	/* ignore */ break;
		case BIFF_EXTERNSHEET:	
			if (ver->version >= MS_BIFF_V8)
				excel_read_EXTERNSHEET_v8 (q, ewb);
			else
				excel_read_EXTERNSHEET_v7 (q, &ewb->container);
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

		case BIFF_FORMAT:	excel_read_FORMAT (q, ewb);			break;

		case BIFF_BACKUP: 	break;
		case BIFF_CODEPAGE: { /* DUPLICATE 42 */
			/* This seems to appear within a workbook */
			/* MW: And on Excel seems to drive the display
			   of currency amounts.  */
			unsigned const codepage = GSF_LE_GET_GUINT16 (q->data);
			gsf_iconv_close (current_workbook_iconv);
			current_workbook_iconv = excel_iconv_open_for_import (codepage);

			/* Store the codepage to make export easier, might
			 * cause problems with double stream files because
			 * we'll lose the codepage in the biff8 version */
			g_object_set_data (G_OBJECT (ewb->gnum_wb), "excel-codepage",
				GINT_TO_POINTER (codepage));

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
				case 1252:
					puts ("CodePage = ANSI (Microsoft Windows)");
					break;
				case 1200:
					puts ("CodePage = little endian unicode");
					break;
				default:
					fprintf (stderr,"CodePage = UNKNOWN(%hx)\n",
					       codepage);
				};
			});
			break;
		}

		case BIFF_OBJPROTECT:
		case BIFF_PROTECT:
			excel_read_PROTECT (q, "Workbook");
			break;

		case BIFF_PASSWORD:
			break;

		case BIFF_FILEPASS: /* All records after this are encrypted */
			/* files with workbook protection are encrypted using a
			 * static password (why ?? ).
			 */
			if (ms_biff_query_set_decrypt (q, "VelvetSweatshop"))
				break;
			do {
				char *passwd = cmd_context_get_password (COMMAND_CONTEXT (ewb->context),
					_("This file is encrypted"));
				if (passwd == NULL) {
					problem_loading = _("No password supplied");
					break;
				}
				if (!ms_biff_query_set_decrypt (q, passwd))
					problem_loading = _("Invalid password");
				g_free (passwd);
				if (problem_loading == NULL)
					break;
				problem_loading = NULL;
			} while (TRUE);
			break;

		case BIFF_STYLE:
			break;

		case BIFF_WINDOWPROTECT:
			break;

		case BIFF_EXTERNNAME:	excel_read_EXTERNNAME (q, &ewb->container); break;
		case BIFF_NAME:		excel_read_NAME (q, ewb);	break;
		case BIFF_XCT:		excel_read_XCT (q, ewb);	break;

		case BIFF_WRITEACCESS:	break;
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

		case BIFF_1904:		excel_read_1904 (q, ewb);	break;

		case BIFF_SELECTION: /* 0, NOT 10 */
			break;

		case BIFF_DIMENSIONS:	/* 2, NOT 1,10 */
			/* Check for padding */
			if (q->ms_op == 0 && prev_was_eof)
				stop_loading = TRUE;
			else
				excel_read_DIMENSIONS (q, ewb);
			break;

		case BIFF_OBJ:	ms_read_OBJ (q, &ewb->container, NULL); break;
		case BIFF_SCL:			break;
		case BIFF_TABIDCONF:		break;
		case BIFF_MS_O_DRAWING:
		case BIFF_MS_O_DRAWING_GROUP:
		case BIFF_MS_O_DRAWING_SELECTION:
			ms_escher_parse (q, &ewb->container);
			break;

		case BIFF_ADDMENU:
			d (1, fprintf (stderr,"%smenu with %d sub items",
				      (GSF_LE_GET_GUINT8 (q->data + 6) == 1) ? "" : "Placeholder ",
				      GSF_LE_GET_GUINT8 (q->data + 5)););
			break;

		case BIFF_SST:	   excel_read_SST (q, ewb);	break;
		case BIFF_EXTSST:  excel_read_EXSST (q, ewb);	break;

		default:
			excel_unexpected_biff (q, "Workbook", ms_excel_read_debug);
			break;
		}
		prev_was_eof = (q->ls_op == BIFF_EOF);
	}
	ms_biff_query_destroy (q);
	if (ver)
		ms_biff_bof_data_destroy (ver);
	io_progress_unset (context);

	d (1, fprintf (stderr,"finished read\n"););

#ifndef NO_DEBUG_EXCEL
	if (ms_excel_read_debug > 0 ||
	    ms_excel_formula_debug > 0 ||
	    ms_excel_chart_debug > 0) {
		fflush (stdout);
	}
#endif
	gsf_iconv_close (current_workbook_iconv);
	current_workbook_iconv = NULL;
	if (ewb != NULL) {
		excel_workbook_destroy (ewb);

		/* If we were forced to stop then the load failed */
		if (problem_loading != NULL)
			gnumeric_error_read (COMMAND_CONTEXT (context), problem_loading);
		return;
	}

	gnumeric_error_read (COMMAND_CONTEXT (context),
		_("Unable to locate valid MS Excel workbook"));
}


void
excel_read_init (void)
{
	excel_builtin_formats [0x0e] = cell_formats [FMT_DATE][0];
	excel_builtin_formats [0x0f] = cell_formats [FMT_DATE][2];
	excel_builtin_formats [0x10] = cell_formats [FMT_DATE][4];
	excel_builtin_formats [0x16] = cell_formats [FMT_DATE][20];
}

void
excel_read_cleanup (void)
{
	excel_palette_destroy (excel_get_default_palette ());
}
