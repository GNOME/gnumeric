/*
 * bonobo-io.c: Workbook IO using Bonobo storages.
 *
 * Author:
 *   Michael Meeks <michael@ximian.com>
 *
 * Copyright 2000, Helix Code, Inc.
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "bonobo-io.h"

#include "sheet-object-bonobo.h"
#include "sheet-object-container.h"
#include "command-context.h"
#include "io-context.h"
#include "workbook-control-component.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet.h"
#include "file.h"

#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <math.h>
#include <limits.h>
#include <bonobo.h>

#include <gsf/gsf-input.h>
#include <gsf-gnome/gsf-input-bonobo.h>

void
gnumeric_bonobo_read_from_stream (BonoboPersistStream       *ps,
				  Bonobo_Stream              stream,
				  Bonobo_Persist_ContentType type,
				  void                      *data,
				  CORBA_Environment         *ev)
{
	WorkbookControl *wbc;
	WorkbookView    *wb_view;
	IOContext       *ioc;
	GsfInput       *input = NULL;
	Workbook        *old_wb;

	g_return_if_fail (data != NULL);
	g_return_if_fail (IS_WORKBOOK_CONTROL_COMPONENT (data));

	wbc = WORKBOOK_CONTROL (data);
	ioc = gnumeric_io_context_new (COMMAND_CONTEXT (wbc));
	input = gsf_input_bonobo_new (stream, NULL);
	wb_view = wb_view_new_from_input  (input, NULL, ioc);

	if (gnumeric_io_error_occurred (ioc) || wb_view == NULL) {
		gnumeric_io_error_display (ioc);
		/* This may be a bad exception to throw, but they're all bad */
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_Bonobo_Persist_WrongDataType, NULL);
	}
	g_object_unref (G_OBJECT (ioc));
	if (BONOBO_EX (ev)) {
		return;
	}

	old_wb = wb_control_workbook (wbc);
	if (workbook_is_dirty (old_wb)) {
		/* No way to interact properly with user */
		g_warning ("Old workbook has unsaved changes.");
		/* FIXME: Do something about it when the viewer has a real
		 *        read only mode. For now, it doesn't mean a thing. */
		/* goto exit_error; */
	}
	g_object_ref (G_OBJECT (wbc));
	workbook_unref (old_wb);
	workbook_control_set_view (wbc, wb_view, NULL);
	workbook_control_init_state (wbc);
}

#if 0
static int
workbook_persist_file_load (BonoboPersistFile *ps, const CORBA_char *filename,
			    CORBA_Environment *ev, void *closure)
{
	WorkbookView *wbv = closure;

	return wb_view_open_file (filename, /* FIXME */ NULL, FALSE, NULL) ? 0 : -1;
}

static int
workbook_persist_file_save (BonoboPersistFile *ps, const CORBA_char *filename,
			    CORBA_Environment *ev, void *closure)
{
	WorkbookView *wbv = closure;
	GnmFileSaver *fs;

	fs = get_file_saver_by_id ("Gnumeric_XmlIO:gnum_xml");
	return wb_view_save_as (wbv, fs, filename, NULL /* FIXME */) ? 0 : -1;
}

static void
workbook_bonobo_setup (WorkbookPrivate *wbp)
{
	BonoboPersistFile *persist_file;

	/* FIXME : This is totaly broken.
	 * 1) it does not belong here at the workbook level
	 * 2) which view use ?
	 * 3) it should not be in this file.
	 */
	persist_file = bonobo_persist_file_new (
		workbook_persist_file_load,
		workbook_persist_file_save,
		wbv);
	bonobo_object_add_interface (
		BONOBO_OBJECT (wbp),
		BONOBO_OBJECT (persist_file));
}
#endif
