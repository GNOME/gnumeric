/*
 * GNOME Expression evaluation
 *
 * Author:
 *    Michael Meeks <michael@ximian.com>
 *
 * Copyright 2000, Helix Code, Inc.
 */

#ifndef EXCEL_GB_CONTEXT_H
#define EXCEL_GB_CONTEXT_H

#include <gbrun/gbrun-eval.h>

#include "workbook-control.h"

#define EXCEL_TYPE_GB_CONTEXT            (excel_gb_context_get_type ())
#define EXCEL_GB_CONTEXT(obj)            (GTK_CHECK_CAST ((obj), EXCEL_TYPE_GB_CONTEXT, ExcelGBContext))
#define EXCEL_GB_CONTEXT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCEL_TYPE_GB_CONTEXT, ExcelGBContextClass))
#define EXCEL_IS_GB_CONTEXT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCEL_TYPE_GB_CONTEXT))
#define EXCEL_IS_GB_CONTEXT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EXCEL_TYPE_GB_CONTEXT))

typedef struct {
	GBRunEvalContext parent;

	WorkbookControl *control;
} ExcelGBContext;

typedef struct {
	GBRunEvalContextClass klass;
} ExcelGBContextClass;

GtkType          excel_gb_context_get_type    (void);

GBEvalContext   *excel_gb_context_new         (const char       *module_name,
					       GBRunSecurityFlag flags);

GBEvalContext   *excel_gb_context_new_control (const char       *module_name,
					       GBRunSecurityFlag flags,
					       WorkbookControl  *control);

WorkbookControl *excel_gb_context_get_control (GBRunEvalContext *);


#endif /* EXCEL_GB_CONTEXT_H */
