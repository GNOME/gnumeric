/*
 * gnome-stream-ole.c: libole based Stream implementation
 *
 * Author:
 *   Michael Meeks <michael@imaginator.com>
 *
 */

#include <config.h>
#include <stdio.h>
#include <glib.h>

#include "ms-ole.h"
#include "gnome-stream-ole.h"

static GnomeStream *
create_stream_ole_server (const GnomeStreamOLE *stream_ole)
{
	GnomeObject *object = GNOME_OBJECT(stream_ole);
	POA_GNOME_Stream *servant;
	GNOME_Stream corba_stream;

	servant = (POA_GNOME_Stream *) g_new0 (GnomeObjectServant, 1);
	servant->vepv = &gnome_stream_vepv;
	POA_GNOME_Stream__init ((PortableServer_Servant) servant, &object->ev);
	if (object->ev._major != CORBA_NO_EXCEPTION){
		g_free (servant);
		return NULL;
	}
	corba_stream = gnome_object_activate_servant(object, servant);
	return GNOME_STREAM(gnome_object_construct(GNOME_OBJECT(stream_ole), 
						   corba_stream));
}

static void
gnome_stream_ole_destroy (GtkObject *object)
{
	GnomeStreamOLE *stream_ole = GNOME_STREAM_OLE (object);

	if (stream_ole->file) ole_file_close (stream_ole->file);
	if (stream_ole->storage) 
		gtk_object_unref (GTK_OBJECT (stream_ole->storage));    
}

static CORBA_long
real_write (GnomeStream *stream, const GNOME_Stream_iobuf *buffer,
	    CORBA_Environment *ev)
{
	GnomeStreamOLE *stream_ole = GNOME_STREAM_OLE (stream);
	CORBA_long len;
	
	len = stream_ole->file->write (stream_ole->file, buffer->_buffer, 
				       buffer->_length);

	return len;
}

static CORBA_long
real_read (GnomeStream *stream, CORBA_long count,
	   GNOME_Stream_iobuf ** buffer,
	   CORBA_Environment *ev)
{
	GnomeStreamOLE *stream_ole = GNOME_STREAM_OLE (stream);
	CORBA_octet *data;
	CORBA_long bytes;

	*buffer = GNOME_Stream_iobuf__alloc ();
	CORBA_sequence_set_release (*buffer, TRUE);
	data = CORBA_sequence_CORBA_octet_allocbuf (count);

	bytes = stream_ole->file->read_copy (stream_ole->file, data, count);

	(*buffer)->_buffer = data;
	(*buffer)->_length = bytes;

	return bytes;
}

static CORBA_long
real_seek (GnomeStream *stream, CORBA_long offset, CORBA_long whence,
	   CORBA_Environment *ev)
{
	GnomeStreamOLE *stream_ole = GNOME_STREAM_OLE (stream);
	MsOleSeek type;

	if (whence == SEEK_SET)
		type = MsOleSeekSet;
	else if (whence == SEEK_CUR)
		type = MsOleSeekCur;
	else {
		g_warning ("FIXME: Seek type unimplemented");
		return -1;
	}

	return stream_ole->dile->lseek (stream_ole->file, offset, whence);
}

static void
real_truncate (GnomeStream *stream, const CORBA_long new_size, 
	       CORBA_Environment *ev)
{
	GnomeStreamOLE *stream_ole = GNOME_STREAM_OLE (stream);

	if (ole_file_trunc (stream_ole->file, new_size)) {
		g_warning ("Signal exception!");
	}
}

static void
gnome_stream_ole_class_init (GnomeStreamOLEClass *class)
{
	GtkObjectClass *object_class = (GtkObjectClass *) class;
	GnomeStreamClass *sclass = GNOME_STREAM_CLASS (class);
	
	sclass->write    = real_write;
	sclass->read     = real_read;
	sclass->seek     = real_seek;
	sclass->truncate = real_truncate;

	object_class->destroy = gnome_stream_ole_destroy;
}

GtkType
gnome_stream_ole_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"IDL:GNOME/StreamOLE:1.0",
			sizeof (GnomeStreamOLE),
			sizeof (GnomeStreamOLEClass),
			(GtkClassInitFunc) gnome_stream_ole_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gnome_stream_get_type (), &info);
	}
  
	return type;
}

static GnomeStream *
gnome_stream_ole_open_create (GnomeStorageOLE *storage_ole,
			      const CORBA_char *path,
			      GNOME_Storage_OpenMode mode,
			      gboolean create)
{
	GnomeStreamOLE *stream_ole;
	gint flags;

	g_return_val_if_fail(storage_ole != NULL, NULL);
	g_return_val_if_fail (path != NULL, NULL);

	if (!(stream_ole = gtk_type_new (gnome_stream_ole_get_type ()))) 
		return NULL;

	flags = OLE_READ;
	if (mode&GNOME_Storage_WRITE) flags |= OLE_WRITE;
	if (create) flags |= OLE_CREATE;
  
	if (!(stream_ole->file=ole_file_open(storage_ole->dir, path, flags))) {
		gtk_object_destroy (GTK_OBJECT (stream_ole));
		return NULL;
	}
	
	stream_ole->storage = storage_ole;
	gtk_object_ref(GTK_OBJECT(storage_ole));
	create_stream_ole_server(stream_ole);

	return GNOME_STREAM (stream_ole);
}

GnomeStream *
gnome_stream_ole_open (GnomeStorageOLE *storage, const CORBA_char *path, 
			GNOME_Storage_OpenMode mode)
{
	return gnome_stream_ole_open_create(storage, path, mode, FALSE);
}

GnomeStream *
gnome_stream_ole_create (GnomeStorageOLE *storage, const CORBA_char *path)
{
	return gnome_stream_ole_open_create(storage, path, 0, TRUE);
}



