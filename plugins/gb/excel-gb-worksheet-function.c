/*
 * excel-gb-worksheet-function.c
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
#include "func.h"

#include "common.h"
#include "excel-gb-worksheet-function.h"

#define ITEM_NAME "gb-worksheetfunction"

static GBObjectClass *parent_class = NULL;

static GBValue *
excel_gb_worksheet_function_deref (GBEvalContext  *ec,
				   GBObject       *object,
				   const GBObjRef *ref,
				   gboolean        try_deref)
{
	ExcelGBWorksheetFunction *funcs = EXCEL_GB_WORKSHEET_FUNCTION (object);
	GnmFunc *fd;

	if ((fd = gnm_func_lookup (ref->name, funcs->sheet->workbook))) {
		GPtrArray *args = g_ptr_array_new ();
		EvalPos    ep;
		Value     *ret;
		GBValue   *gb_ret;
		GSList    *l;

		/* FIXME: do we want to do a function_def_get_arg_type here ? */
		for (l = ref->parms; l; l = l->next) {
			GBValue *val = gb_eval_context_eval (ec, l->data);

			if (!val) /* FIXME: Evil leaky error */
				return NULL;

			g_ptr_array_add (args, gb_to_value (val));
			gb_value_destroy (val);
		}

		/* FIXME: where should we be located ? */
		eval_pos_init_sheet (&ep, funcs->sheet);

		ret = function_def_call_with_values (
			&ep, fd, args->len, (Value **)args->pdata);

		if (ret) {
			gb_ret = value_to_gb (ret);
			value_release (ret);
			return gb_ret;
		} else
			return NULL;
	} else
		return parent_class->deref (ec, object, ref, try_deref);
}

static void
excel_gb_worksheet_function_class_init (GBRunObjectClass *klass)
{
	GBObjectClass *gb_class = (GBObjectClass *) klass;

	parent_class = g_type_class_peek (gbrun_object_get_type ());

	gb_class->deref = excel_gb_worksheet_function_deref;
}

GtkType
excel_gb_worksheet_function_get_type (void)
{
	static GtkType object_type = 0;

	if (!object_type) {
		static const GtkTypeInfo object_info = {
			ITEM_NAME,
			sizeof (ExcelGBWorksheetFunction),
			sizeof (ExcelGBWorksheetFunctionClass),
			(GtkClassInitFunc)  excel_gb_worksheet_function_class_init,
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

ExcelGBWorksheetFunction *
excel_gb_worksheet_function_new (Sheet *sheet)
{
	ExcelGBWorksheetFunction *funcs;

	g_return_val_if_fail (sheet != NULL, NULL);

	funcs = gtk_type_new (EXCEL_TYPE_GB_WORKSHEET_FUNCTION);
	funcs->sheet = sheet;

	return funcs;
}
