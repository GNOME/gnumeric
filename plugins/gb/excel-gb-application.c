/*
 * excel-gb-application.c
 *
 * Gnome Basic Interpreter Form functions.
 *
 * Author:
 *      Michael Meeks  <michael@helixcode.com>
 */

#include "gnumeric.h"
#include "workbook.h"
#include "sheet.h"

#include "excel-gb-application.h"
#include "excel-gb-worksheet-function.h"
#include "excel-gb-worksheet.h"
#include "excel-gb-range.h"

#define ITEM_NAME "gb-application"

static GBObjectClass *parent_class = NULL;

static GBValue *
worksheets_lookup (GBEvalContext *ec, Workbook *wb,
		   GBExpr *expr, gboolean try_deref)
{
	GList *sheets, *l;
	GBValue *val;
	GBValue *ret = NULL;

	if (!wb)
		return NULL;

	if (!(val = gbrun_eval_as (GBRUN_EVAL_CONTEXT (ec), expr, GB_VALUE_STRING)))
		return NULL;

	sheets = workbook_sheets (wb);
	
	for (l = sheets; l && !ret; l = l->next) {
		Sheet *sheet = l->data;

		if (!strcmp (sheet->name_unquoted, val->v.s->str))
			ret = gb_value_new_object (
				GB_OBJECT (excel_gb_worksheet_new (sheet)));
	}

	if (!ret)
		gbrun_exception_firev (GBRUN_EVAL_CONTEXT (ec),
				       "Can't address sheet '%s'", val->v.s->str);

	g_list_free (sheets);
	
	gb_value_destroy (val);

	return ret;
}

static GBValue *
excel_gb_application_deref (GBEvalContext  *ec,
			    GBObject       *object,
			    const GBObjRef *ref,
			    gboolean        try_deref)
{
	ExcelGBApplication *app = EXCEL_GB_APPLICATION (object);

	if (ref->name && !g_strcasecmp (ref->name, "Worksheets")) {
		if (ref->parms)
			return worksheets_lookup (
				ec, app->wb, ref->parms->data, try_deref);
		
		else {
			if (!try_deref)
				gbrun_exception_firev (GBRUN_EVAL_CONTEXT (ec),
						       "No index to worksheet collection");
			return NULL;
		}
	} else
		return parent_class->deref (ec, object, ref, try_deref);
}

static void
excel_gb_application_class_init (GBRunObjectClass *klass)
{
	GBObjectClass *gb_class = (GBObjectClass *) klass;
/*	GBRunObjectClass *gbrun_class = (GBRunObjectClass *) klass;*/

	parent_class = gtk_type_class (gbrun_object_get_type ());

	gb_class->deref = excel_gb_application_deref;
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
		gtk_type_class (object_type);
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
	excel_gb_application_get_type ();
}
