#include <gnumeric-config.h>
#include <gnumeric.h>
#include <excel-gb-context.h>

static void
excel_gb_context_destroy (GtkObject *object)
{
	ExcelGBContext *ctx = EXCEL_GB_CONTEXT (object);

	gtk_object_unref (GTK_OBJECT (ctx->control));
}

static void
excel_gb_context_class_init (ExcelGBContextClass *klass)
{
	GtkObjectClass     *object_class;

	object_class          = (GtkObjectClass*) klass;
	object_class->destroy = excel_gb_context_destroy;
}

static void
excel_gb_context_init (ExcelGBContext *ec)
{
}

GtkType
excel_gb_context_get_type (void)
{
	static GtkType eval_type = 0;

	if (!eval_type) {
		static const GtkTypeInfo eval_info = {
			"ExcelGBContext",
			sizeof (ExcelGBContext),
			sizeof (ExcelGBContextClass),
			(GtkClassInitFunc) excel_gb_context_class_init,
			(GtkObjectInitFunc) excel_gb_context_init,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		eval_type = gtk_type_unique (GBRUN_TYPE_EVAL_CONTEXT,
					     &eval_info);
	}

	return eval_type;
}


GBEvalContext *
excel_gb_context_new_control (const char       *module_name,
			      GBRunSecurityFlag flags,
			      WorkbookControl  *control)
{
	ExcelGBContext   *ctx;
	GBRunEvalContext *ret;

	g_return_val_if_fail (control != NULL, NULL);

	ctx = gtk_type_new (EXCEL_TYPE_GB_CONTEXT);

	ret = gbrun_eval_context_construct (
		GBRUN_EVAL_CONTEXT (ctx), module_name, flags);

	gtk_object_ref (GTK_OBJECT (control));
	ctx->control = control;

	return GB_EVAL_CONTEXT (ret);
}

GBEvalContext *
excel_gb_context_new (const char       *module_name,
		      GBRunSecurityFlag flags)
{
	WorkbookControl  *ctl = gtk_type_new (WORKBOOK_CONTROL_TYPE);
	GBEvalContext *ret;

	ret = excel_gb_context_new_control (module_name, flags, ctl);

	gtk_object_unref (GTK_OBJECT (ctl));

	return ret;
}


WorkbookControl *
excel_gb_context_get_control (GBRunEvalContext *ec)
{
	ExcelGBContext *ctx = EXCEL_GB_CONTEXT (ec);

	g_return_val_if_fail (ctx != NULL, NULL);

	return ctx->control;
}
