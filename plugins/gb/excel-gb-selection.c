/*
 * excel-gb-selection.c
 *
 * Gnome Basic Interpreter Form functions.
 *
 * Author:
 *      Thomas Meeks  <thomas@imaginator.com>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "workbook.h"
#include "sheet.h"
#include "selection.h"

#include <gbrun/libgbrun.h>
#include <gbrun/gbrun-value.h>

#include "excel-gb-selection.h"
#include "excel-gb-interior.h"

#define ITEM_NAME "gb-selection"

static GBValue *
excel_gb_selection_interior (GBRunEvalContext *ec,
			     GBRunObject      *object,
			     GBValue         **args)
{
	Sheet           *sheet;
	ExcelGBInterior *interior;
	const Range     *first_range;

	sheet = EXCEL_GB_SELECTION (object)->sheet;

	first_range = selection_first_range (sheet, NULL, NULL);

	interior = excel_gb_interior_new (sheet, *first_range);

	if (interior)
		return gb_value_new_object (GB_OBJECT (interior));
	else
		return NULL;
}

static void
excel_gb_selection_class_init (GBRunObjectClass *klass)
{
	GBRunObjectClass *gbrun_class = (GBRunObjectClass *) klass;

	gbrun_object_add_method_arg (gbrun_class, "func;interior;.;range;n",
				     excel_gb_selection_interior);
}

GtkType
excel_gb_selection_get_type (void)
{
	static GtkType object_type = 0;

	if (!object_type) {
		static const GtkTypeInfo object_info = {
			ITEM_NAME,
			sizeof (ExcelGBSelection),
			sizeof (ExcelGBSelectionClass),
			(GtkClassInitFunc)  excel_gb_selection_class_init,
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

ExcelGBSelection *
excel_gb_selection_new (Sheet *sheet)
{
	ExcelGBSelection *app = gtk_type_new (EXCEL_TYPE_GB_SELECTION);

	app->sheet = sheet;

	return app;
}
