#ifndef GNUMERIC_WORKBOOK_PRIVATE_H
#define GNUMERIC_WORKBOOK_PRIVATE_H

#include "workbook.h"
#include <glib.h>

/*
 * Here we should put all the variables that are internal
 * to the workbook and that must not be accessed but by code
 * that runs the Workbook
 *
 * The reason for this is that pretty much all the code
 * depends on workbook.h, so for internals and to avoid
 * recompilation, we put the code here.
 *
 * This is ending up being a transition object for the eventual move to a
 * seprate Model View and controller.  It holds pieces that will be part of the
 * eventual controller objects.
 */
#ifdef WITH_BONOBO
#   include <bonobo.h>
#endif

struct _WorkbookPrivate {
#ifdef WITH_BONOBO
	/* The base object for the Workbook */
	BonoboObject bonobo_object;
#endif

	gboolean during_destruction;
	gboolean recursive_dirty_enabled;
};

WorkbookPrivate *workbook_private_new (void);
void             workbook_private_delete (WorkbookPrivate *wbp);

#endif /* GNUMERIC_WORKBOOK_PRIVATE_H */
