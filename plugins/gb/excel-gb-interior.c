/*
 * excel-gb-interior.c
 *
 * Gnome Basic Interpreter Form functions.
 *
 * Author:
 *      Thomas Meeks  <thomas@imaginator.com>
 */

#include <config.h>
#include "gnumeric.h"
#include "workbook.h"
#include "ranges.h"
#include "sheet.h"
#include "cell.h"
#include "parse-util.h"
#include "workbook-control-corba.h"
#include "selection.h"
#include "commands.h"

#include "style-border.h"
#include "style-color.h"
#include "sheet-style.h"
#include "mstyle.h"

#include <gbrun/libgbrun.h>
#include <gb/gb-constants.h>
#include "../excel/excel.h"
#include "excel-gb-interior.h"
#include "common.h"

#define ITEM_NAME "gb-interior"

enum {
	FIRST_ARG = 0,
	COLOR,
	COLOR_INDEX,
	PATTERN,
	APPLICATION,
	PATTERN_COLOR,
	PATTERN_COLOR_INDEX
};

static StyleColor *
convert_color_to_rgb (long initial)
{
	int r = (initial & GB_C_Red) << 8;
	int g = initial & GB_C_Green;
	int b = (initial & GB_C_Blue) >> 8;

	return style_color_new (r, g, b);
}

static long
convert_rgb_to_color (int r, int g, int b)
{
	long color = 0;

	color |= (b & 0xff00) << 8;
	color |= (g & 0xff00);
	color |= (r & 0xff00) >> 8;

	return color;
}

static StyleColor *
color_from_palette (int idx)
{
	EXCEL_PALETTE_ENTRY e;

	if (idx > EXCEL_DEF_PAL_LEN || idx < 0)
		return NULL;

	e = excel_default_palette [idx];

	return style_color_new (e.r << 8, e.g << 8, e.b << 8);
}

static int
palette_from_color (StyleColor *color)
{
	int i;
	int r = color->red >> 8;
	int g = color->green >> 8;
	int b = color->blue >> 8;

	g_return_val_if_fail (color != NULL, 0);

	for (i = 0; i < EXCEL_DEF_PAL_LEN; i++) {
		EXCEL_PALETTE_ENTRY e = excel_default_palette [i];

/*		g_message ("R:%d,%d;G:%d,%d;B:%d,%d", e.r, r, e.g, g, e.b, b);*/

		if (e.r == r && e.g == g && e.b == b)
			return i;
	};

	return -1;
}


static void
real_set_style (Sheet *sheet, Range *range, MStyle *style)
{
	sheet_apply_style (sheet, range, style);
}

static gboolean
excel_gb_interior_set_arg (GBRunEvalContext *ec,
			   GBRunObject      *object,
			   int               property,
			   GBValue          *val)
{
	ExcelGBInterior *interior = EXCEL_GB_INTERIOR (object);
	MStyle          *style;

	switch (property) {

	case COLOR:
		style = mstyle_new ();
		mstyle_set_color (style, MSTYLE_COLOR_FORE,
				  convert_color_to_rgb (val->v.l));
		real_set_style (interior->sheet, &interior->range, style);
		return TRUE;

	case COLOR_INDEX: {
		StyleColor *color = color_from_palette (val->v.i);

		if (!color) {
			gbrun_exception_firev (
				ec, "Invalid color index '%s'", val->v.i);
			return FALSE;
		}

		style = mstyle_new ();
		mstyle_set_color (style, MSTYLE_COLOR_FORE, color);
		real_set_style (interior->sheet, &interior->range, style);
		return TRUE;
	}

	case PATTERN:
		style = mstyle_new ();
		mstyle_set_pattern (style, val->v.i);
		real_set_style (interior->sheet, &interior->range, style);
		return TRUE;

	case PATTERN_COLOR:
		style = mstyle_new ();
		mstyle_set_color (style, MSTYLE_COLOR_BACK,
				  convert_color_to_rgb (val->v.l));
		real_set_style (interior->sheet, &interior->range, style);
		return TRUE;

	case PATTERN_COLOR_INDEX: {
		StyleColor *color = color_from_palette (val->v.i);

		if (!color) {
			gbrun_exception_firev (
				ec, "Invalid pattern color index '%s'", val->v.i);
			return FALSE;
		}

		style = mstyle_new ();
		mstyle_set_color (style, MSTYLE_COLOR_BACK, color);
		real_set_style (interior->sheet, &interior->range, style);
		return TRUE;
	}

	default:
		g_warning ("Unhandled property '%d'", property);
		return FALSE;
	}
}

static GBValue *
excel_gb_interior_get_arg (GBRunEvalContext *ec,
			   GBRunObject      *object,
			   int               property)
{
	ExcelGBInterior *interior = EXCEL_GB_INTERIOR (object);
	int              col      = interior->range.start.col;
	int              row      = interior->range.end.col;
	MStyle          *style;

	switch (property) {
	case COLOR: {
		StyleColor *color;
		long realcolor;

		style = sheet_style_get (interior->sheet, col, row);
		color = mstyle_get_color (style, MSTYLE_COLOR_FORE);
		realcolor = convert_rgb_to_color (color->red, color->green, color->blue);

		return (gb_value_new_long (realcolor));
	}
	case COLOR_INDEX: {
		StyleColor *color;
		int index;

		style = sheet_style_get (interior->sheet, col, row);
		color = mstyle_get_color (style, MSTYLE_COLOR_FORE);

		index = palette_from_color (color);
		if (index == -1) {
			gbrun_exception_firev (
				ec, "Could not convert color to index (%d, %d, %d)",
				color->red, color->green, color->blue);
			return NULL;
		}

		return (gb_value_new_int (index));
	}
	case PATTERN: {
		int pattern;

		style = sheet_style_get (interior->sheet, col, row);

		pattern = mstyle_get_pattern (style);

		return (gb_value_new_int (pattern));
	}

	case PATTERN_COLOR: {
		StyleColor *color;
		long realcolor;

		style = sheet_style_get (interior->sheet, col, row);
		color = mstyle_get_color (style, MSTYLE_COLOR_BACK);
		realcolor = convert_rgb_to_color (color->red, color->green, color->blue);

		return (gb_value_new_long (realcolor));
	}
	case PATTERN_COLOR_INDEX: {
		StyleColor *color;
		int index;

		style = sheet_style_get (interior->sheet, col, row);
		color = mstyle_get_color (style, MSTYLE_COLOR_FORE);

		index = palette_from_color (color);
		if (index == -1) {
			gbrun_exception_firev (
				ec, "Could not convert pattern color to index (%d, %d, %d)",
				color->red, color->green, color->blue);
			return NULL;
		}

		return (gb_value_new_int (index));
	}
	default:
		g_warning ("Unhandled property '%d'", property);
		return NULL;
	}
}

static void
excel_gb_interior_class_init (GBRunObjectClass *klass)
{
	GBRunObjectClass *gbrun_class = (GBRunObjectClass *) klass;

	gbrun_class->set_arg = excel_gb_interior_set_arg;
	gbrun_class->get_arg = excel_gb_interior_get_arg;

	gbrun_object_add_property (gbrun_class, "color",
				   gb_type_long, COLOR);

	gbrun_object_add_property (gbrun_class, "colorindex",
				   gb_type_int, COLOR_INDEX);

	gbrun_object_add_property (gbrun_class, "pattern",
				   gb_type_int, PATTERN);

	gbrun_object_add_property (gbrun_class, "patterncolor",
				   gb_type_long, PATTERN_COLOR);

	gbrun_object_add_property (gbrun_class, "patterncolorindex",
				   gb_type_int, PATTERN_COLOR_INDEX);

	/*
	 * Delete, HasFormula, Row, Col, Activate, WorkSheet
	 */
}

GtkType
excel_gb_interior_get_type (void)
{
	static GtkType object_type = 0;

	if (!object_type) {
		static const GtkTypeInfo object_info = {
			ITEM_NAME,
			sizeof (ExcelGBInterior),
			sizeof (ExcelGBInteriorClass),
			(GtkClassInitFunc)  excel_gb_interior_class_init,
			(GtkObjectInitFunc) NULL,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		object_type = gtk_type_unique (GBRUN_TYPE_OBJECT, &object_info);
		gtk_type_class (object_type);
	}

	return object_type;
}

ExcelGBInterior *
excel_gb_interior_new (Sheet *sheet, Range range)
{
	ExcelGBInterior *interior = gtk_type_new (EXCEL_TYPE_GB_INTERIOR);

	interior->sheet = sheet;
	interior->range = range;

	return interior;
}
