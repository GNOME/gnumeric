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

	range = excel_gb_range_new (
		ec, EXCEL_GB_WORKSHEET (object)->sheet,
		args [0]->v.s->str);

	if (range)
		return gb_value_new_object (GB_OBJECT (range));
	else
		return NULL;
}

static void
excel_gb_worksheet_class_init (GBRunObjectClass *klass)
{
	GBRunObjectClass *gbrun_class = (GBRunObjectClass *) klass;

	gbrun_object_add_method_arg (gbrun_class, "func;range;range,string;range;n",
				     excel_gb_worksheet_range);
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
