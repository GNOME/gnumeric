/*
 * gnome-storage-ole.c: libole based Storage implementation
 *
 * Author:
 *   Michael Meeks <michael@imaginator.com>
 *
 * Problems:
 *
 * Directory and pathname stuff needs serious work.
 *
 */

#include <config.h>
#include <glib.h>

#include "ms-ole.h"
#include "gnome-storage-ole.h"
#include "gnome-stream-ole.h"

/*
 * Creates and activates the corba server 
 */
static GnomeStorage *
create_ole_server (const GnomeStorageOLE *ole)
{
	GnomeObject *object = GNOME_OBJECT(ole);
	POA_GNOME_Storage *servant;
	GNOME_Storage corba_storage;
	CORBA_Environment ev;

	servant = (POA_GNOME_Storage *) g_new0 (GnomeObjectServant, 1);
	servant->vepv = &gnome_storage_vepv;
	CORBA_exception_init (&ev);
	POA_GNOME_Storage__init ((PortableServer_Servant) servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION){
		g_free (servant);
		CORBA_exception_free (&ev);
		return NULL;
	}
	CORBA_exception_free (&ev);

	corba_storage = gnome_object_activate_servant(object, servant);
	return gnome_storage_construct(GNOME_STORAGE(ole), 
				       corba_storage);
}

static void
gnome_ole_destroy (GtkObject *object)
{
	GnomeStorageOLE *ole = GNOME_STORAGE_OLE (object);

	ms_ole_destroy (ole->f);
}


static GnomeStream *
real_create_stream (GnomeStorage *storage, const CORBA_char *path, 
		    CORBA_Environment *ev)
{
	GnomeStorageOLE *ole = GNOME_STORAGE_OLE (storage);

	return gnome_stream_ole_create (ole, path);
}

static GnomeStream *
real_open_stream (GnomeStorage *storage, const CORBA_char *path, 
		  GNOME_Storage_OpenMode mode, CORBA_Environment *ev)
{
	GnomeStorageOLE *ole = GNOME_STORAGE_OLE (storage);
	
	return gnome_stream_ole_open (ole, path, mode);
}


static GnomeStorage *
real_create_storage (GnomeStorage *storage, const CORBA_char *path, 
		     CORBA_Environment *ev)
{
/*	GnomeStorageOLE *ole = GNOME_STORAGE_OLE (storage); */
	GnomeStorageOLE *sole;
	MsOle *f;

	if (!(f = ms_ole_create (path))) {
		CORBA_exception_set(ev, CORBA_USER_EXCEPTION, 
				    ex_GNOME_Storage_NameExists, NULL);
		return NULL;
	}
	
	sole    = gtk_type_new (gnome_storage_ole_get_type ());
	sole->f = f;
	create_ole_server (sole);

	return GNOME_STORAGE (sole);
}

static GnomeStorage *
real_open_storage (GnomeStorage *storage, const CORBA_char *path, 
		   CORBA_Environment *ev)
{
/*	GnomeStorageOLE *ole = GNOME_STORAGE_OLE (storage); */
	GnomeStorageOLE *sole;
	MsOle *f;

	if (!(f = ms_ole_open (path))) {
		CORBA_exception_set(ev, CORBA_USER_EXCEPTION, 
				    ex_GNOME_Storage_NotFound, NULL);
		return NULL;
	}

	sole    = gtk_type_new (gnome_storage_ole_get_type ());
	sole->f = f;
	create_ole_server (sole);

	return GNOME_STORAGE (sole);
}

static void
real_copy_to (GnomeStorage *storage, GNOME_Storage target, 
	      CORBA_Environment *ev)
{	
	g_warning ("Not yet implemented");
}

static void
real_rename (GnomeStorage *storage, const CORBA_char *path, 
	     const CORBA_char *new_path, CORBA_Environment *ev)
{
/*	GnomeStorageOLE *ole = GNOME_STORAGE_OLE (storage); */

	g_warning ("No OLE rename yet");
/*	if (!ole_rename(ole->dir, path, new_path)) {
		CORBA_exception_set(ev, CORBA_USER_EXCEPTION, 
				    ex_GNOME_Storage_NotFound, NULL);
				    }*/
}

static void
real_commit (GnomeStorage *storage, CORBA_Environment *ev)
{
/*	GnomeStorageOLE *ole = GNOME_STORAGE_OLE (storage);

	if (ole->owner) return;

	ole_commit(ole->dir);*/
	g_warning ("Why bother comitting ?");
}


static GNOME_Storage_directory_list *
real_list_contents (GnomeStorage *storage, const CORBA_char *path, 
		    CORBA_Environment *ev)
{
	GnomeStorageOLE *ole = GNOME_STORAGE_OLE (storage);
	GNOME_Storage_directory_list *list;
	MsOleDirectory *dir, *iter;
	int i;
	char **buf = NULL;

	dir = ms_ole_path_decode (ole->f, path);
	if (!dir) {
		CORBA_exception_set(ev, CORBA_USER_EXCEPTION, 
				    ex_GNOME_Storage_NotFound, NULL);
		return NULL;
	}
	ms_ole_directory_enter (dir);
	if (!dir) {
		CORBA_exception_set(ev, CORBA_USER_EXCEPTION, 
				    ex_GNOME_Storage_NotFound, NULL);
		return NULL;
	}

	iter = ms_ole_directory_copy (dir);
	while (ms_ole_directory_next (iter)) i++;

	list =  GNOME_Storage_directory_list__alloc();
	CORBA_sequence_set_release (list, TRUE); 
	buf = CORBA_sequence_CORBA_string_allocbuf(i + 1);
	
	i = 0;
	do {
		char *t = CORBA_string_alloc(strlen(dir->name));
		strcpy (t, dir->name);
		buf[i] = t;
		i++;
	} while (ms_ole_directory_next (dir));

	ms_ole_directory_destroy (dir);

	list->_length = i;
	list->_buffer = buf;

	return list;
}


static void
real_erase (GnomeStorage *storage, const CORBA_char *path,
	    CORBA_Environment *ev)
{
	GnomeStorageOLE *ole = GNOME_STORAGE_OLE (storage);
	
	MsOleDirectory *dir = ms_ole_path_decode (ole->f, path);
	if (!dir)
		CORBA_exception_set(ev, CORBA_USER_EXCEPTION, 
				    ex_GNOME_Storage_NotFound, NULL);
	else
		ms_ole_directory_unlink (dir);
}

static void
gnome_ole_class_init (GnomeStorageOLEClass *class)
{
	GtkObjectClass *object_class = (GtkObjectClass *) class;
	GnomeStorageClass *sclass = GNOME_STORAGE_CLASS (class);

	sclass->create_stream  = real_create_stream;
	sclass->open_stream    = real_open_stream;

	sclass->create_storage = real_create_storage;
	sclass->open_storage   = real_open_storage;
	sclass->copy_to        = real_copy_to;
	sclass->rename         = real_rename;
	sclass->commit         = real_commit;
	sclass->list_contents  = real_list_contents;
	sclass->erase          = real_erase;

	object_class->destroy = gnome_ole_destroy;
}

GtkType
gnome_storage_ole_get_type (void)
{
	static GtkType type = 0;
  
	if (!type){
		GtkTypeInfo info = {
			"IDL:GNOME/StorageOLE:1.0",
			sizeof (GnomeStorageOLE),
			sizeof (GnomeStorageOLEClass),
			(GtkClassInitFunc) gnome_ole_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gnome_storage_get_type (), &info);
	}

	return type;
}

/** 
 * gnome_ole_open:
 * @path: path to a file that represents the storage
 * @flags: flags
 * @mode: mode
 *
 * Opens a GnomeStorage object in @path. The storage opened is an activated
 * CORBA server for this storage.
 *
 * Returns the GnomeStorage GTK object.
 */
GnomeStorage *
gnome_storage_ole_open (const gchar *path, gint flags, gint mode)
{
	GnomeStorageOLE *sole;
	char m;

	sole = gtk_type_new (gnome_storage_ole_get_type ());

	if (flags == GNOME_SS_READ)
		m = 'r';
	else if (flags == GNOME_SS_WRITE)
		m = 'w';
	else {
		g_warning ("Unimplemented flag");
		return NULL;
	}
	if (!(sole->f = ms_ole_open (path))) {
		gtk_object_destroy (GTK_OBJECT (sole));
		return NULL;
	}
    
	if (!create_ole_server(sole)) {
		gtk_object_destroy (GTK_OBJECT (sole));
		return NULL;
	}

	return GNOME_STORAGE(sole);
}

/*
 * Shared library entry point
 */
GnomeStorage *
gnome_storage_driver_open (const gchar *path, gint flags, gint mode)
{
	return gnome_storage_ole_open (path, flags, mode);
}

