/*
 * excel-gb-range.c
 *
 * Gnome Basic Interpreter Form functions.
 *
 * Author:
 *      Michael Meeks  <michael@helixcode.com>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "workbook.h"
#include "ranges.h"
#include "sheet.h"
#include "cell.h"
#include "parse-util.h"
#include "commands.h"
#include "workbook-control-corba.h"
#include "selection.h"

#include <gbrun/libgbrun.h>

#include "excel-gb-range.h"
#include "excel-gb-interior.h"
#include "common.h"

#define ITEM_NAME "VB.Range"

enum {
	FIRST_ARG = 0,
	VALUE,
	TEXT
};

static gboolean
excel_gb_range_set_arg (GBRunEvalContext *ec,
			GBRunObject      *object,
			int               property,
			GBValue          *val)
{
	ExcelGBRange *range = EXCEL_GB_RANGE (object);

	switch (property) {

	case VALUE: {
		Value *value;
		Cell    *cell;

		value = gb_to_value (val);
		if (!value) {
			gbrun_exception_firev (ec, "Can't convert value");
			return FALSE;
		}

		cell = sheet_cell_fetch (range->sheet,
					 range->range.start.col,
					 range->range.start.row);

		sheet_cell_set_value (cell, value, NULL);

		return TRUE;
	}

	case TEXT: {
		Cell  *cell;

		cell = sheet_cell_fetch (range->sheet,
					 range->range.start.col,
					 range->range.start.row);
		sheet_cell_set_text (cell, val->v.s->str);

		return TRUE;
	}

	default:
		g_warning ("Unhandled property '%d'", property);
		return FALSE;
	}
}

static GBValue *
excel_gb_range_get_arg (GBRunEvalContext *ec,
			GBRunObject      *object,
			int               property)
{
	ExcelGBRange *range = EXCEL_GB_RANGE (object);

	g_warning ("Get arg");

	switch (property) {

	case VALUE: {
		Cell    *cell;
		GBValue *val;

		cell = sheet_cell_get (range->sheet,
				       range->range.start.col,
				       range->range.start.row);

		if (!cell)
			return gb_value_new_empty ();

		val = value_to_gb (cell->value);

		if (!val)
			return gbrun_exception_firev (ec, "Can't convert cell value");

		return val;
	}

	case TEXT: {
		Cell    *cell;
		char    *txt;
		GBValue *val;

		cell = sheet_cell_get (range->sheet,
				       range->range.start.col,
				       range->range.start.row);

		if (!cell)
			return gb_value_new_empty ();

		txt = cell_get_rendered_text (cell);
		val = gb_value_new_string_chars (txt);
		g_free (txt);

		if (!val)
			return gbrun_exception_firev (ec, "Can't convert cell value");

		return val;
	}

	default:
		g_warning ("Unhandled property '%d'", property);
		return NULL;
	}
}

static GBValue *
excel_gb_range_select (GBRunEvalContext *ec,
		       GBRunObject      *object,
		       GBValue         **args)
{
	ExcelGBRange   *range = EXCEL_GB_RANGE (object);

	sv_selection_add_range (range->sheet,
				range->range.start.col,
				range->range.start.row,
				range->range.start.col,
				range->range.start.row,
				range->range.end.col,
				range->range.end.row);

	return gb_value_new_empty ();
}

static GBValue *
excel_gb_range_clear (GBRunEvalContext *ec,
		      GBRunObject      *object,
		      GBValue         **args)
{
/*	ExcelGBRange *range = EXCEL_GB_RANGE (object);*/

	g_warning ("We need a way to cope with the command context");

	return gb_value_new_empty ();
}

static GBValue *
excel_gb_range_interior (GBRunEvalContext *ec,
			 GBRunObject      *object,
			 GBValue         **args)
{
	ExcelGBRange    *range = EXCEL_GB_RANGE (object);
	ExcelGBInterior *interior;

	interior = excel_gb_interior_new (range->sheet, range->range);

	if (interior)
		return gb_value_new_object (GB_OBJECT (interior));
	else
		return NULL;
}

static void
excel_gb_range_class_init (GBRunObjectClass *klass)
{
	GBRunObjectClass *gbrun_class = (GBRunObjectClass *) klass;

	gbrun_class->set_arg = excel_gb_range_set_arg;
	gbrun_class->get_arg = excel_gb_range_get_arg;

	gbrun_object_add_property (
		gbrun_class, "value", 0, VALUE);

	gbrun_object_add_property (
		gbrun_class, "text", gb_type_string, VALUE);

	gbrun_object_add_method_arg (gbrun_class, "sub;clear;.;n",
				     excel_gb_range_clear);

	gbrun_object_add_method_arg (gbrun_class, "sub;select;.;n",
				     excel_gb_range_select);

	gbrun_object_add_method_arg (gbrun_class, "func;interior;.;range;n",
				     excel_gb_range_interior);
	/*
	 * Delete, HasFormula, Row, Col, Activate, WorkSheet
	 */
}

GtkType
excel_gb_range_get_type (void)
{
	static GtkType object_type = 0;

	if (!object_type) {
		static const GtkTypeInfo object_info = {
			ITEM_NAME,
			sizeof (ExcelGBRange),
			sizeof (ExcelGBRangeClass),
			(GtkClassInitFunc)  excel_gb_range_class_init,
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

ExcelGBRange *
excel_gb_range_new (GBRunEvalContext *ec,
		    Sheet            *sheet,
		    Range             area)
{
	ExcelGBRange *range;

	range = gtk_type_new (EXCEL_TYPE_GB_RANGE);

	range->sheet = sheet;
	range->range = area;

	return range;
}

ExcelGBRange *
excel_gb_range_new_ref (GBRunEvalContext *ec,
			Sheet            *sheet,
			const char       *text)
{
	Range         tmp;
	int           len;

	if (!parse_cell_name (text, &tmp.start, FALSE, &len)) {
		gbrun_exception_firev (ec, "Invalid range '%s'", text);
		return NULL;
	}

	if (text [len] == ':') {
		if (!parse_cell_name (text + len + 1, &tmp.end, &tmp.end, TRUE, NULL)) {
			gbrun_exception_firev (ec, "Invalid range '%s'", text);
			return NULL;
		}
	} else
		tmp.end = tmp.start;

	return excel_gb_range_new (ec, sheet, tmp);
}
