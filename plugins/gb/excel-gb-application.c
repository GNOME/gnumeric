/*
 * excel-gb-application.c
 *
 * Gnome Basic Interpreter Form functions.
 *
 * Author:
 *      Michael Meeks  <michael@helixcode.com>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "workbook.h"
#include "sheet.h"

#include "excel-gb-application.h"
#include "excel-gb-worksheet-function.h"
#include "excel-gb-worksheets.h"
#include "excel-gb-worksheet.h"
#include "excel-gb-range.h"

#define ITEM_NAME "gb-application"

enum {
	FIRST_ARG = 0,
	WORKSHEETS
};

static GBValue *
excel_gb_application_get_arg (GBRunEvalContext *ec,
			      GBRunObject      *object,
			      int               property)
{
	ExcelGBApplication *app = EXCEL_GB_APPLICATION (object);

	switch (property) {
	case WORKSHEETS:
		return gb_value_new_object (
			GB_OBJECT (excel_gb_worksheets_new (app->wb)));

	default:
		g_warning ("Unhandled property '%d'", property);
		return NULL;
	}
}

static void
excel_gb_application_class_init (GBRunObjectClass *klass)
{
	klass->get_arg = excel_gb_application_get_arg;

	gbrun_object_add_property_full (
		klass, "worksheets",
		excel_gb_worksheets_get_type (),
		WORKSHEETS, GBRUN_PROPERTY_READABLE);
}

GtkType
excel_gb_application_get_type (void)
{
	static GtkType object_type = 0;

	if (!object_type) {
		static const GtkTypeInfo object_info = {
			ITEM_NAME,
			sizeof (ExcelGBApplication),
			sizeof (ExcelGBApplicationClass),
			(GtkClassInitFunc)  excel_gb_application_class_init,
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

ExcelGBApplication *
excel_gb_application_new (Workbook *wb)
{
	ExcelGBApplication *app;

	g_return_val_if_fail (wb != NULL, NULL);

	app = gtk_type_new (EXCEL_TYPE_GB_APPLICATION);
	app->wb = wb;

	return app;
}

void
excel_gb_application_register_types ()
{
	excel_gb_worksheet_function_get_type ();
	excel_gb_range_get_type ();
	excel_gb_worksheet_get_type ();
	excel_gb_worksheets_get_type ();
	excel_gb_application_get_type ();
}
