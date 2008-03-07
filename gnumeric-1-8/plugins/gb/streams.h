/**
 * streams.h: Gnome Basic streaming support
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 **/
#ifndef STREAMS_OLE_H
#define STREAMS_OLE_H

#include <libole2/ms-ole.h>
#include <libole2/ms-ole-vba.h>

#include "gb/gb-lex.h"

#define GB_TYPE_OLE_STREAM            (gb_ole_stream_get_type ())
#define GB_OLE_STREAM(obj)            (GTK_CHECK_CAST ((obj), GB_TYPE_OLE_STREAM, GBOleStream))
#define GB_OLE_STREAM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GB_TYPE_OLE_STREAM, GBOleOleClass))
#define GB_IS_OLE_STREAM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GB_TYPE_OLE_STREAM))
#define GB_IS_OLE_STREAM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GB_TYPE_OLE_STREAM))

typedef struct _GBOleStream      GBOleStream;
typedef struct _GBOleStreamClass GBOleStreamClass;

struct _GBOleStream {
	GBLexerStream  stream;

	MsOleVba      *vba;
};

struct _GBOleStreamClass {
	GBLexerStreamClass klass;
};

GtkType        gb_ole_stream_get_type (void);
GBLexerStream *gb_ole_stream_new      (MsOleVba *vba);

GBLexerStream *gb_project_stream      (gpointer *jody_broke_the_ctx, MsOle *f);

#endif
