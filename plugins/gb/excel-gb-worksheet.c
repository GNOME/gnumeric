/*
 * excel-gb-worksheet.c
 *
 * Gnome Basic Interpreter Form functions.
 *
 * Author:
 *      Michael Meeks  <michael@helixcode.com>
 */

#include "gnumeric.h"
#include "workbook.h"
#include "sheet.h"

#include <gbrun/libgbrun.h>
#include <gbrun/gbrun-value.h>

#include "excel-gb-worksheet-function.h"
#include "excel-gb-worksheet.h"
#include "excel-gb-range.h"

#define ITEM_NAME "gb-worksheet"

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

	tmp.start = sheet->cursor.edit_pos;
	tmp.end   = tmp.start;

	range = excel_gb_range_new (ec, sheet, tmp);

	if (range)
		return gb_value_new_object (GB_OBJECT (range));
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

	gbrun_object_add_method_arg (gbrun_class, "func;range;range,string;range;n",
				     excel_gb_worksheet_range);

	gbrun_object_add_method_arg (gbrun_class, "func;activecell;.;range;n",
				     excel_gb_worksheet_activecell);

	gbrun_object_add_method_arg (gbrun_class, "func;cells;col,long;row,long;range;n",
				     excel_gb_worksheet_cells);

	gbrun_object_add_method_arg (gbrun_class, "func;worksheetfunction;.;worksheetfunction;n",
				     excel_gb_worksheet_function);
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
		gtk_type_class (object_type);
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
