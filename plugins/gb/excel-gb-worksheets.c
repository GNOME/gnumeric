/*
 * excel-gb-worksheets.c
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

#include "excel-gb-worksheet.h"
#include "excel-gb-worksheets.h"
#include "excel-gb-range.h"
#include "excel-gb-selection.h"

#define ITEM_NAME "gb-worksheets"

static GBValue *
excel_gb_worksheets_add (GBRunEvalContext *ec,
			 GBRunObject      *object,
			 GBValue         **args)
{
	Sheet *sheet;
	ExcelGBWorksheets *ws = EXCEL_GB_WORKSHEETS (object);

	/* FIXME: Here things go pear shaped, we need to look at
	   name and value really. */

	sheet = workbook_sheet_add (ws->wb, NULL, TRUE);

	return gb_value_new_object (
		GB_OBJECT (excel_gb_worksheet_new (sheet)));
}

static void
excel_gb_worksheets_remove (GBRunEvalContext *ec,
			    GBRunCollection  *collection,
			    const char       *name)
{
	Sheet *sheet;
	ExcelGBWorksheets *ws = EXCEL_GB_WORKSHEETS (collection);

	g_warning ("The worksheet remove function may cause crashes when used incorrectly");

	/*
	 * FIXME:
	 * Below is the proposed implementation for this function, which
	 * is disabled right now, the problem is that Gnumeric crashes
	 * if a GB function on the sheet that is about to be deleted
	 * calls upon this function. Solution?
	 */

	sheet = workbook_sheet_by_name (ws->wb, name);
	if (sheet)
		workbook_sheet_delete (sheet);

}

/* Returns a list of allocated GBRunCollectionElements */
static GSList *
excel_gb_worksheets_enumerate (GBRunEvalContext *ec,
			       GBRunCollection  *collection)
{
	ExcelGBWorksheets *ws = EXCEL_GB_WORKSHEETS (collection);
	GList  *sheets, *l;
	GSList *ret = NULL;

	sheets = workbook_sheets (ws->wb);
	for (l = sheets; l; l = l->next) {
		Sheet *sheet = l->data;
		GBRunCollectionElement *e;

		e = gbrun_collection_element_new (
			GB_EVAL_CONTEXT (ec),
			sheet->name_unquoted,
			GB_OBJECT (excel_gb_worksheet_new (sheet)));

		ret = g_slist_prepend (ret, e);
	}
	g_list_free (sheets);

	return g_slist_reverse (ret);
}

static void
excel_gb_worksheets_class_init (GBRunObjectClass *klass)
{
	GBRunCollectionClass *c_class = (GBRunCollectionClass *) klass;

	c_class->remove = excel_gb_worksheets_remove;
	c_class->enumerate = excel_gb_worksheets_enumerate;

	gbrun_object_add_method_arg (
		klass, "func;add;.;worksheet;n",
		excel_gb_worksheets_add);
}

GtkType
excel_gb_worksheets_get_type (void)
{
	static GtkType object_type = 0;

	if (!object_type) {
		static const GtkTypeInfo object_info = {
			ITEM_NAME,
			sizeof (ExcelGBWorksheets),
			sizeof (ExcelGBWorksheetsClass),
			(GtkClassInitFunc)  excel_gb_worksheets_class_init,
			(GtkObjectInitFunc) NULL,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		object_type = gtk_type_unique (
			GBRUN_TYPE_COLLECTION, &object_info);
		g_type_class_peek (object_type);
	}

	return object_type;
}

ExcelGBWorksheets *
excel_gb_worksheets_new (Workbook *wb)
{
	ExcelGBWorksheets *ws = gtk_type_new (EXCEL_TYPE_GB_WORKSHEETS);

	ws->wb = wb;

	return ws;
}
