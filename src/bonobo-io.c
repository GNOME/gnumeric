/*
 * bonobo-io.c: Workbook IO using Bonobo storages.
 *
 * Author:
 *   Michael Meeks <michael@ximian.com>
 *
 * Copyright 2000, Helix Code, Inc.
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "bonobo-io.h"

#include "command-context.h"
#include "io-context.h"
#include "workbook-control-component.h"
#include "workbook-view.h"
#include "workbook.h"
#include <bonobo.h>

#include <gsf/gsf-input.h>
#include <gsf-gnome/gsf-input-bonobo.h>

#define GNM_TYPE_PERSIST_STREAM         (gnm_persist_stream_get_type ())
#define GNM_PERSIST_STREAM(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_PERSIST_STREAM, GnmPersistStream))
#define GNM_PERSIST_STREAM_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GNM_TYPE_PERSIST_STREAM, GnmPersistStreamClass))
#define GNM_IS_PERSIST_STREAM(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_PERSIST_STREAM))
#define GNM_IS_PERSIST_STREAM_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GNM_TYPE_PERSIST_STREAM))
#define GNM_PERSIST_STREAM_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GNM_TYPE_PERSIST_STREAM, GnmPersistStreamClass))

typedef struct {
	BonoboPersist __parent;

	/*< private >*/

	WorkbookControl *wbc;
} GnmPersistStream;

typedef struct {
	BonoboPersistClass __parent_class;

	POA_Bonobo_PersistStream__epv epv;
} GnmPersistStreamClass;

GType          gnm_persist_stream_get_type (void);

static void 
gnm_persist_stream_load (PortableServer_Servant  servant,
			 Bonobo_Stream           stream,
			 const CORBA_char       *type,
			 CORBA_Environment      *ev)
{
	BonoboObject    *object;
	WorkbookControl *wbc;
	WorkbookView    *wb_view;
	IOContext       *ioc;
	GsfInput        *input = NULL;
	Workbook        *old_wb;

	object        = bonobo_object_from_servant (servant);
	wbc = GNM_PERSIST_STREAM (object)->wbc;
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

static void
gnm_persist_stream_class_init (GnmPersistStreamClass *klass)
{
        BonoboPersistClass *persist_class = BONOBO_PERSIST_CLASS (klass);
 	POA_Bonobo_PersistStream__epv *epv = &klass->epv;

	persist_class->get_content_types = NULL;
 
	epv->load = gnm_persist_stream_load;
	epv->save = NULL;
}

static void
gnm_persist_stream_init (GnmPersistStream *stream)
{
	stream->wbc = NULL;
}

BONOBO_TYPE_FUNC_FULL (
	GnmPersistStream,         /* Glib class name */
	Bonobo_PersistStream, /* CORBA interface name */
	BONOBO_TYPE_PERSIST,  /* parent type */
	gnm_persist_stream);       /* local prefix ie. 'echo'_class_init */

BonoboObject *
gnm_persist_stream_new (WorkbookControl *wbc, const char* const iid)
{
	GnmPersistStream *stream;

	stream = GNM_PERSIST_STREAM (g_object_new (GNM_TYPE_PERSIST_STREAM,
						   NULL));
	bonobo_persist_construct (BONOBO_PERSIST (stream), iid);
	stream->wbc = wbc;

	return (BonoboObject*) stream;
}
