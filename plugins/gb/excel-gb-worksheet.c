/*
 * excel-gb-worksheet.c
 *
 * Gnome Basic Interpreter Form functions.
 *
 * Author:
 *      Michael Meeks  <michael@ximian.com>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "workbook.h"
#include "sheet.h"

#include <gbrun/libgbrun.h>
#include <gbrun/gbrun-value.h>

#include "excel-gb-worksheet-function.h"
#include "excel-gb-worksheet.h"
#include "excel-gb-range.h"
#include "excel-gb-selection.h"
#include "excel-gb-context.h"

#define ITEM_NAME "gb-worksheet"

enum {
	FIRST_ARG = 0,
	NAME = 1
};

static GBValue *
excel_gb_worksheet_range (GBRunEvalContext *ec,
			  GBRunObject      *object,
			  GBValue         **args)
{
	ExcelGBRange *range;

	GB_IS_VALUE (ec, args [0], GB_VALUE_STRING);

	range = excel_gb_range_new_ref (
		ec, EXCEL_GB_WORKSHEET (object)->sheet,
		args [0]->v.s->str);

	if (range)
		return gb_value_new_object (GB_OBJECT (range));
	else
		return NULL;
}

static GBValue *
excel_gb_worksheet_activecell (GBRunEvalContext *ec,
			       GBRunObject      *object,
			       GBValue         **args)
{
	Sheet        *sheet;
	ExcelGBRange *range;
	Range         tmp;

	sheet = EXCEL_GB_WORKSHEET (object)->sheet;

	tmp.start = sheet->edit_pos;
	tmp.end   = tmp.start;

	range = excel_gb_range_new (ec, sheet, tmp);

	if (range)
		return gb_value_new_object (GB_OBJECT (range));
	else
		return NULL;
}

static gboolean
excel_gb_worksheet_set_arg (GBRunEvalContext *ec,
			    GBRunObject      *object,
			    int               property,
			    GBValue          *val)
{
	ExcelGBWorksheet *worksheet = EXCEL_GB_WORKSHEET (object);
	ExcelGBContext *context = EXCEL_GB_CONTEXT (ec);

	switch (property) {

	case NAME: {
		Sheet *sheet = worksheet->sheet;
		workbook_sheet_rename (context->control,
				       sheet->workbook,
				       sheet->name_unquoted,
				       val->v.s->str);
		return TRUE;
	}

	default:
		g_warning ("Unhandled property '%d'", property);
		return FALSE;
	}
}

static GBValue *
excel_gb_worksheet_get_arg (GBRunEvalContext *ec,
			    GBRunObject      *object,
			    int               property)
{
	ExcelGBWorksheet *worksheet = EXCEL_GB_WORKSHEET (object);

	switch (property) {

	case NAME: {
		return (gb_value_new_string_chars (worksheet->sheet->name_unquoted));
		break;
	}
	default:
		g_warning ("Unhandled property '%d'", property);
		return NULL;
	}
}

static GBValue *
excel_gb_worksheet_selection (GBRunEvalContext *ec,
			      GBRunObject      *object,
			      GBValue         **args)
{
	Sheet            *sheet;
	ExcelGBSelection *selection;

	sheet = EXCEL_GB_WORKSHEET (object)->sheet;

	selection = excel_gb_selection_new (sheet);

	if (selection)
		return gb_value_new_object (GB_OBJECT (selection));
	else
		return NULL;
}

static GBValue *
excel_gb_worksheet_cells (GBRunEvalContext *ec,
			  GBRunObject      *object,
			  GBValue         **args)
{
	Sheet        *sheet;
	ExcelGBRange *range;
	Range         tmp;

	sheet = EXCEL_GB_WORKSHEET (object)->sheet;

	tmp.start.col = args [0]->v.l;
	tmp.start.row = args [1]->v.l;
	tmp.end   = tmp.start;

	range = excel_gb_range_new (ec, sheet, tmp);

	if (range)
		return gb_value_new_object (GB_OBJECT (range));
	else
		return NULL;
}

static GBValue *
excel_gb_worksheet_function (GBRunEvalContext *ec,
			     GBRunObject      *object,
			     GBValue         **args)
{
	ExcelGBWorksheet *worksheet = EXCEL_GB_WORKSHEET (object);

	return gb_value_new_object (
		GB_OBJECT (excel_gb_worksheet_function_new (worksheet->sheet)));
}

static void
excel_gb_worksheet_class_init (GBRunObjectClass *klass)
{
	GBRunObjectClass *gbrun_class = (GBRunObjectClass *) klass;

	gbrun_class->set_arg = excel_gb_worksheet_set_arg;
	gbrun_class->get_arg = excel_gb_worksheet_get_arg;

	gbrun_object_add_method_arg (gbrun_class, "func;range;range,string;range;n",
				     excel_gb_worksheet_range);

	gbrun_object_add_method_arg (gbrun_class, "func;activecell;.;range;n",
				     excel_gb_worksheet_activecell);

	gbrun_object_add_method_arg (gbrun_class, "func;selection;.;range;n",
				     excel_gb_worksheet_selection);

	gbrun_object_add_method_arg (gbrun_class, "func;cells;col,long;row,long;range;n",
				     excel_gb_worksheet_cells);

	gbrun_object_add_method_arg (gbrun_class, "func;worksheetfunction;.;worksheetfunction;n",
				     excel_gb_worksheet_function);
	gbrun_object_add_property (gbrun_class, "name",
				   gb_type_string, NAME);
}

GtkType
excel_gb_worksheet_get_type (void)
{
	static GtkType object_type = 0;

	if (!object_type) {
		static const GtkTypeInfo object_info = {
			ITEM_NAME,
			sizeof (ExcelGBWorksheet),
			sizeof (ExcelGBWorksheetClass),
			(GtkClassInitFunc)  excel_gb_worksheet_class_init,
			(GtkObjectInitFunc) NULL,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		object_type = gtk_type_unique (GBRUN_TYPE_OBJECT, &object_info);
		g_type_class_peek (object_type);
	}

	return object_type;
}

ExcelGBWorksheet *
excel_gb_worksheet_new (Sheet *sheet)
{
	ExcelGBWorksheet *app = gtk_type_new (EXCEL_TYPE_GB_WORKSHEET);

	app->sheet = sheet;

	return app;
}
