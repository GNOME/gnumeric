/* -*- mode: c; c-basic-offset: 8 -*- */
/**
 * streams.c: Gnome Basic streaming support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 2000 HelixCode, Inc
 **/

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <glib.h>
#include <gnome.h>

#include <gb/libgb.h>
#include <gb/gb-mmap-lex.h>
#include <gbrun/libgbrun.h>

#include "plugin.h"
#include "expr.h"
#include "func.h"

#include "streams.h"

static GtkObjectClass *lex_object_parent = NULL;

static gboolean
s_eof (GBLexerStream *ls)
{
	GBOleStream *ms = GB_OLE_STREAM (ls);

	return ms_ole_vba_eof (ms->vba);
}

static char
s_getc (GBLexerStream *ls)
{
	GBOleStream *ms = GB_OLE_STREAM (ls);

	return ms_ole_vba_getc (ms->vba);
}

static char
s_peek (GBLexerStream *ls)
{
	GBOleStream *ms = GB_OLE_STREAM (ls);

	return ms_ole_vba_peek (ms->vba);
}

static void
gb_ole_stream_destroy (GtkObject *object)
{
	GBOleStream *ms = GB_OLE_STREAM (object);

	ms_ole_vba_close (ms->vba);
	ms->vba = NULL;

	lex_object_parent->destroy (object);
}

static void
gb_ole_stream_class_init (GBOleStreamClass *klass)
{
	GBLexerStreamClass *lex_class;
	GtkObjectClass     *object_class;

	lex_object_parent = gtk_type_class (GB_TYPE_LEXER_STREAM);

	object_class = GTK_OBJECT_CLASS (klass);
	object_class->destroy = gb_ole_stream_destroy;

	lex_class = GB_LEXER_STREAM_CLASS (klass);

	lex_class->s_getc   = s_getc;
	lex_class->s_peek   = s_peek;
	lex_class->s_eof    = s_eof;
}

static void
gb_ole_stream_init (GBOleStream *ls)
{
}

GtkType
gb_ole_stream_get_type (void)
{
	static GtkType lex_type = 0;

	if (!lex_type) {
		static const GtkTypeInfo lex_info = {
			"GBOleStream",
			sizeof (GBOleStream),
			sizeof (GBOleStreamClass),
			(GtkClassInitFunc) gb_ole_stream_class_init,
			(GtkObjectInitFunc) gb_ole_stream_init,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		lex_type = gtk_type_unique (GB_TYPE_LEXER_STREAM, &lex_info);
	}

	return lex_type;
}

GBLexerStream *
gb_ole_stream_new (MsOleVba *vba)
{
	GBOleStream *ms = GB_OLE_STREAM (gtk_type_new (GB_TYPE_OLE_STREAM));

	ms->vba = vba;

	return GB_LEXER_STREAM (ms);
}


GBLexerStream *
gb_project_stream (gpointer *jody_broke_the_ctx, MsOle *f)
{
	MsOleStream   *s;
	GBLexerStream *proj_stream;
	guint8        *data;

	g_return_val_if_fail (f != NULL, NULL);

	if (ms_ole_stream_open (&s, f, "/_VBA_PROJECT_CUR", "PROJECT", 'r') !=
	    MS_OLE_ERR_OK) {
		g_warning ("No VBA found");
		return NULL;
	}

	data = g_malloc (s->size);
	if (!s->read_copy (s, data, s->size)) {
		g_warning ("Serious error reading project");
		return NULL;
	}

	proj_stream = gb_mmap_stream_new (data, s->size);

	ms_ole_stream_close (&s);

	return proj_stream;
}
