/*
 * file.c: File loading and saving routines
 *
 * Authors:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Zbigniew Chyla (cyba@gnome.pl)
 */
#include <config.h>
#include <string.h>
#include <libgnome/libgnome.h>
#include <gal/util/e-util.h>
#ifdef ENABLE_BONOBO
#include <bonobo/bonobo-exception.h>
#endif
#include "file.h"
#include "io-context.h"
#include "command-context.h"
#include "gnumeric.h"
#include "sheet.h"
#include "application.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"
#include "gutils.h"
#include "file-priv.h"

/*
 * GnumFileOpener
 */

static void
gnum_file_opener_init (GnumFileOpener *fo)
{
	fo->id = NULL;
	fo->description = NULL;
	fo->probe_func = NULL;
	fo->open_func = NULL;
}

static void
gnum_file_opener_destroy (GtkObject *obj)
{
	GnumFileOpener *fo;

	g_return_if_fail (IS_GNUM_FILE_OPENER (obj));

	fo = GNUM_FILE_OPENER (obj);
	g_free (fo->id);
	g_free (fo->description);

	GTK_OBJECT_CLASS (gtk_type_class (GTK_TYPE_OBJECT))->destroy (obj);
}

static gboolean
gnum_file_opener_probe_real (GnumFileOpener const *fo, const gchar *file_name,
                             FileProbeLevel pl)
{
	if (fo->probe_func == NULL) {
		return FALSE;
	}

	return fo->probe_func (fo, file_name, pl);
}

static void
gnum_file_opener_open_real (GnumFileOpener const *fo, IOContext *io_context,
                            WorkbookView *wbv, const gchar *file_name)
{
	if (fo->open_func == NULL) {
		gnumeric_io_error_unknown (io_context);
		return;
	}

	fo->open_func (fo, io_context, wbv, file_name);
}

static void
gnum_file_opener_class_init (GnumFileOpenerClass *klass)
{
	GTK_OBJECT_CLASS (klass)->destroy = gnum_file_opener_destroy;

	klass->probe = gnum_file_opener_probe_real;
	klass->open = gnum_file_opener_open_real;
}

E_MAKE_TYPE (gnum_file_opener, "GnumFileOpener", GnumFileOpener, \
             gnum_file_opener_class_init, gnum_file_opener_init, \
             GTK_TYPE_OBJECT)

/**
 * gnum_file_opener_setup:
 * @fo          : Newly created GnumFileOpener object
 * @id          : Optional ID of the opener (or NULL)
 * @description : Description of supported file format
 * @probe_func  : Optional pointer to "probe" function (or NULL)
 * @open_func   : Pointer to "open" function
 *
 * Sets up GnumFileOpener object, newly created with gtk_type_new function.
 * This is intended to be used only by GnumFileOpener derivates.
 * Use gnum_file_opener_new, if you want to create GnumFileOpener object.
 */
void
gnum_file_opener_setup (GnumFileOpener *fo, const gchar *id,
                        const gchar *description,
                        GnumFileOpenerProbeFunc probe_func,
                        GnumFileOpenerOpenFunc open_func)
{
	g_return_if_fail (IS_GNUM_FILE_OPENER (fo));

	fo->id = g_strdup (id);
	fo->description = g_strdup (description);
	fo->probe_func = probe_func;
	fo->open_func = open_func;
}

/**
 * gnum_file_opener_new:
 * @id          : Optional ID of the opener (or NULL)
 * @description : Description of supported file format
 * @probe_func  : Optional pointer to "probe" function (or NULL)
 * @open_func   : Pointer to "open" function
 *
 * Creates new GnumFileOpener object. Optional @id will be used
 * after registering it with register_file_opener or
 * register_file_opener_as_importer function.
 *
 * Return value: newly created GnumFileOpener object.
 */
GnumFileOpener *
gnum_file_opener_new (const gchar *id,
                      const gchar *description,
                      GnumFileOpenerProbeFunc probe_func,
                      GnumFileOpenerOpenFunc open_func)
{
	GnumFileOpener *fo;

	fo = GNUM_FILE_OPENER (gtk_type_new (TYPE_GNUM_FILE_OPENER));
	gnum_file_opener_setup (fo, id, description, probe_func, open_func);

	return fo;
}

const gchar *
gnum_file_opener_get_id (GnumFileOpener const *fo)
{
	g_return_val_if_fail (IS_GNUM_FILE_OPENER (fo), FALSE);

	return fo->id;
}

const gchar *
gnum_file_opener_get_description (GnumFileOpener const *fo)
{
	g_return_val_if_fail (IS_GNUM_FILE_OPENER (fo), FALSE);

	return fo->description;
}

/**
 * gnum_file_opener_probe:
 * @fo          : GnumFileOpener object
 * @file_name   : File name
 *
 * Checks if a given file is supported by the opener.
 *
 * Return value: TRUE, if the opener can read given file and FALSE
 *               otherwise.
 */
gboolean
gnum_file_opener_probe (GnumFileOpener const *fo, const gchar *file_name, FileProbeLevel pl)
{
	g_return_val_if_fail (IS_GNUM_FILE_OPENER (fo), FALSE);
	g_return_val_if_fail (file_name != NULL, FALSE);

	return GNUM_FILE_OPENER_METHOD (fo, probe) (fo, file_name, pl);
}

/**
 * gnum_file_opener_open:
 * @fo          : GnumFileOpener object
 * @io_context  : Context for i/o operation
 * @wbv         : Workbook View
 * @file_name   : File name
 *
 * Reads content of @file_name file into workbook @wbv is attached to.
 * Results are reported using @io_context object, use
 * gnumeric_io_error_occurred to find out if operation was successful.
 * The state of @wbv and its workbook is undefined if operation fails, you
 * should destroy them in that case.
 */
void
gnum_file_opener_open (GnumFileOpener const *fo, IOContext *io_context,
                       WorkbookView *wbv, const gchar *file_name)
{
	g_return_if_fail (IS_GNUM_FILE_OPENER (fo));
	g_return_if_fail (file_name != NULL);

	GNUM_FILE_OPENER_METHOD (fo, open) (fo, io_context, wbv, file_name);
}

/*
 * GnumFileSaver
 */

static void
gnum_file_saver_init (GnumFileSaver *fs)
{
	fs->id = NULL;
	fs->extension = NULL;
	fs->mime_type = NULL;
	fs->description = NULL;
	fs->format_level = FILE_FL_NEW;
	fs->save_scope = FILE_SAVE_WORKBOOK;
	fs->save_func = NULL;
}

static void
gnum_file_saver_destroy (GtkObject *obj)
{
	GnumFileSaver *fs;

	g_return_if_fail (IS_GNUM_FILE_SAVER (obj));

	fs = GNUM_FILE_SAVER (obj);
	g_free (fs->id);
	g_free (fs->extension);
	g_free (fs->description);

	GTK_OBJECT_CLASS (gtk_type_class (GTK_TYPE_OBJECT))->destroy (obj);
}

static void
gnum_file_saver_save_real (GnumFileSaver const *fs, IOContext *io_context,
                           WorkbookView *wbv, const gchar *file_name)
{
	if (fs->save_func == NULL) {
		gnumeric_io_error_unknown (io_context);
		return;
	}

	fs->save_func (fs, io_context, wbv, file_name);
}

#ifdef ENABLE_BONOBO
static void
gnum_file_saver_save_to_stream_real (GnumFileSaver const *fs,
				     IOContext *io_context, 
				     WorkbookView *wbv, 
				     BonoboStream *stream, 
				     CORBA_Environment *ev)
{
	if (fs->save_to_stream_func == NULL) {
		gnumeric_io_error_unknown (io_context);
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_Bonobo_Stream_NotSupported, NULL);
		return;
	}

	fs->save_to_stream_func (fs, io_context, wbv, stream, ev);
}

gboolean
gnum_file_saver_supports_save_to_stream (GnumFileSaver const *fs)
{
	return (fs->save_to_stream_func != NULL);
}
#endif

static void
gnum_file_saver_class_init (GnumFileSaverClass *klass)
{
	GTK_OBJECT_CLASS (klass)->destroy = gnum_file_saver_destroy;

	klass->save = gnum_file_saver_save_real;
#ifdef ENABLE_BONOBO
	klass->save_to_stream = gnum_file_saver_save_to_stream_real;
#endif
}

E_MAKE_TYPE (gnum_file_saver, "GnumFileSaver", GnumFileSaver, \
             gnum_file_saver_class_init, gnum_file_saver_init, \
             GTK_TYPE_OBJECT)

/**
 * gnum_file_saver_setup:
 * @fs          : Newly created GnumFileSaver object
 * @id          : Optional ID of the saver (or NULL)
 * @extension   : Optional default extension of saved files (or NULL)
 * @description : Description of supported file format
 * @level       : File format level
 * @save_func   : Pointer to "save" function
#ifdef ENABLE_BONOBO
 * @save_to_stream_func: Pointer to "save to stream" function
#endif
 *
 * Sets up GnumFileSaver object, newly created with gtk_type_new function.
 * This is intended to be used only by GnumFileSaver derivates.
 * Use gnum_file_saver_new, if you want to create GnumFileSaver object.
 */
void
gnum_file_saver_setup (GnumFileSaver *fs, const gchar *id,
                       const gchar *extension,
                       const gchar *description,
                       FileFormatLevel level,
#ifdef ENABLE_BONOBO
		       GnumFileSaverSaveFunc save_func,
		       GnumFileSaverSaveToStreamFunc save_to_stream_func)
#else
                       GnumFileSaverSaveFunc save_func)
#endif
{
	gchar *tmp;

	g_return_if_fail (IS_GNUM_FILE_SAVER (fs));

	fs->id = g_strdup (id);
	tmp = g_strdup_printf ("SomeFile.%s", extension);
	fs->mime_type = gnome_mime_type_or_default (tmp,
					"application/application/x-gnumeric");
	g_free (tmp);
	fs->extension = g_strdup (extension);
	fs->description = g_strdup (description);
	fs->format_level = level;
	fs->save_func = save_func;
#ifdef ENABLE_BONOBO
	fs->save_to_stream_func = save_to_stream_func;
#endif
}

/**
 * gnum_file_saver_new:
 * @id          : Optional ID of the saver (or NULL)
 * @extension   : Optional default extension of saved files (or NULL)
 * @description : Description of supported file format
 * @level       : File format level
 * @save_func   : Pointer to "save" function
#ifdef ENABLE_BONOBO
 * @save_to_stream_func: Pointer to "save to stream" function
#endif
 *
 * Creates new GnumFileSaver object. Optional @id will be used
 * after registering it with register_file_saver or
 * register_file_saver_as_default function.
 *
 * Return value: newly created GnumFileSaver object.
 */
GnumFileSaver *
gnum_file_saver_new (const gchar *id,
                     const gchar *extension,
                     const gchar *description,
                     FileFormatLevel level,
#ifdef ENABLE_BONOBO
		     GnumFileSaverSaveFunc save_func,
		     GnumFileSaverSaveToStreamFunc save_to_stream_func)
#else
                     GnumFileSaverSaveFunc save_func)
#endif
{
	GnumFileSaver *fs;

	fs = GNUM_FILE_SAVER (gtk_type_new (TYPE_GNUM_FILE_SAVER));
#ifdef ENABLE_BONOBO
	gnum_file_saver_setup (fs, id, extension, description, level, save_func, save_to_stream_func);
#else
	gnum_file_saver_setup (fs, id, extension, description, level, save_func);
#endif

	return fs;
}

void
gnum_file_saver_set_save_scope (GnumFileSaver *fs, FileSaveScope scope)
{
	g_return_if_fail (IS_GNUM_FILE_SAVER (fs));
	g_return_if_fail (scope < FILE_SAVE_LAST);

	fs->save_scope = scope;
}

FileSaveScope
gnum_file_saver_get_save_scope (GnumFileSaver *fs)
{
	g_return_val_if_fail (IS_GNUM_FILE_SAVER (fs), FILE_SAVE_WORKBOOK);

	return fs->save_scope;
}

const gchar *
gnum_file_saver_get_id (GnumFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNUM_FILE_SAVER (fs), FALSE);

	return fs->id;
}

const gchar *
gnum_file_saver_get_mime_type (GnumFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNUM_FILE_SAVER (fs), FALSE);

	return fs->mime_type;
}

const gchar *
gnum_file_saver_get_extension (GnumFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNUM_FILE_SAVER (fs), FALSE);

	return fs->extension;
}

const gchar *
gnum_file_saver_get_description (GnumFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNUM_FILE_SAVER (fs), FALSE);

	return fs->description;
}

FileFormatLevel
gnum_file_saver_get_format_level (GnumFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNUM_FILE_SAVER (fs), FILE_FL_NEW);

	return fs->format_level;
}

/**
 * gnum_file_saver_save:
 * @fs          : GnumFileSaver object
 * @io_context  : Context for i/o operation
 * @wbv         : Workbook View
 * @file_name   : File name
 *
 * Saves @wbv and workbook it's attached to into @file_name file.
 * Results are reported using @io_context object, use
 * gnumeric_io_error_occurred to find out if operation was successful.
 * It's possible that @file_name is created and contain some data if
 * operation fails, you should remove the file in that case.
 */
void
gnum_file_saver_save (GnumFileSaver const *fs, IOContext *io_context,
                      WorkbookView *wbv, const gchar *file_name)
{
	g_return_if_fail (IS_GNUM_FILE_SAVER (fs));
	g_return_if_fail (file_name != NULL);

	GNUM_FILE_SAVER_METHOD (fs, save) (fs, io_context, wbv, file_name);
}

#ifdef ENABLE_BONOBO
/**
 * gnum_file_saver_save_to_stream:
 * @fs		: GnumFileSaver object
 * @io_context	: Context for i/o operation
 * @wbv		: Workbook View
 * @stream	: Bonobo Stream
 * @ev		: CORBA Environment
 *
 * Saves @wbv and workbook it's attached to into the stream. Results are
 * reported through the environment variable, the i/o context is used only
 * for updating the progress bar.
 */
void
gnum_file_saver_save_to_stream (GnumFileSaver const *fs, IOContext *io_context,
		                WorkbookView *wbv, BonoboStream *stream,
				CORBA_Environment *ev)
{
	bonobo_return_if_fail (IS_GNUM_FILE_SAVER (fs), ev);
	
	GNUM_FILE_SAVER_METHOD (fs, save_to_stream) (fs, io_context, wbv,
						     stream, ev);
}
#endif

/**
 * gnum_file_saver_fix_file_name:
 * @fs          : GnumFileSaver object
 * @file_name   : File name
 *
 * Modifies given @file_name by adding default extension for file type
 * supported by @fs saver. If @fs has no default extension or @file_name
 * already has some extension, it just copies @file_name.
 *
 * Return value: newly allocated string which you should free after use,
 *               containing (optionally) modified file name.
 */
gchar *
gnum_file_saver_fix_file_name (GnumFileSaver const *fs, const gchar *file_name)
{
	gchar *new_file_name;

	g_return_val_if_fail (IS_GNUM_FILE_SAVER (fs), NULL);
	g_return_val_if_fail (file_name != NULL, NULL);

	if (fs->extension != NULL && strchr (g_basename (file_name), '.') == NULL) {
		new_file_name = g_strconcat (file_name, ".", fs->extension, NULL);
	} else {
		new_file_name = g_strdup (file_name);
	}

	return new_file_name;
}


/*
 * ------
 */

typedef struct {
	gint priority;
	GnumFileSaver *saver;
} DefaultFileSaver;

static GHashTable *file_opener_id_hash = NULL,
                  *file_saver_id_hash = NULL;
static GList *file_opener_list = NULL, *file_opener_priority_list = NULL;
static GList *file_importer_list = NULL;
static GList *file_saver_list = NULL, *default_file_saver_list = NULL;

static gint
cmp_int_less_than (gconstpointer list_i, gconstpointer i)
{
	return !(GPOINTER_TO_INT (list_i) < GPOINTER_TO_INT (i));
}

/**
 * register_file_opener:
 * @fo          : GnumFileOpener object
 * @priority    : Opener's priority
 *
 * Adds @fo opener to the list of available file openers, making it
 * available for Gnumeric i/o routines. The opener is registered with given
 * @priority. The priority is used to determine the order in which openers
 * will be tried when reading a file. The higher the priority, the sooner it
 * will be tried. Default XML-based Gnumeric file opener is registered at
 * priority 50. Recommended range for @priority is [0, 100].
 * Reference count for the opener is incremented inside the function, but
 * you don't have to (and shouldn't) call gtk_object_unref on it if it's
 * floating object (for example, when you pass object newly created with
 * gnum_file_opener_new and not referenced anywhere).
 */
void
register_file_opener (GnumFileOpener *fo, gint priority)
{
	gint pos;
	const gchar *id;

	g_return_if_fail (IS_GNUM_FILE_OPENER (fo));
	g_return_if_fail (priority >=0 && priority <= 100);

	gtk_object_ref (GTK_OBJECT (fo));
	gtk_object_sink (GTK_OBJECT (fo));

	pos = g_list_index_custom (file_opener_priority_list,
	                           GINT_TO_POINTER (priority),
	                           cmp_int_less_than); 
	file_opener_priority_list = g_list_insert (
	                            file_opener_priority_list,
	                            GINT_TO_POINTER (priority), pos);
	file_opener_list = g_list_insert (file_opener_list, fo, pos);

	id = gnum_file_opener_get_id (fo);
	if (id != NULL) {
		if (file_opener_id_hash	== NULL) {
			file_opener_id_hash = g_hash_table_new (&g_str_hash, &g_str_equal);
		}
		g_hash_table_insert (file_opener_id_hash, (gpointer) id, fo);
	}
}

/**
 * register_file_opener_as_importer:
 * @fo          : GnumFileOpener object
 *
 * Adds @fo opener to the list of available file importers. The opener will
 * not be tried when reading files using Gnumeric i/o routines (unless you
 * call register_file_opener on it), but it will be available for the user
 * when importing the file and selecting file opener manually.
 * Reference count for the opener is incremented inside the function, but
 * you don't have to (and shouldn't) call gtk_object_unref on it if it's
 * floating object (for example, when you pass object newly created with
 * gnum_file_opener_new and not referenced anywhere).
 */
void
register_file_opener_as_importer (GnumFileOpener *fo)
{
	const gchar *id;

	g_return_if_fail (IS_GNUM_FILE_OPENER (fo));

	gtk_object_ref (GTK_OBJECT (fo));
	gtk_object_sink (GTK_OBJECT (fo));

	file_importer_list = g_list_prepend (file_importer_list, fo);

	id = gnum_file_opener_get_id (fo);
	if (id != NULL) {
		if (file_opener_id_hash	== NULL) {
			file_opener_id_hash = g_hash_table_new (&g_str_hash, &g_str_equal);
		}
		g_hash_table_insert (file_opener_id_hash, (gpointer) id, fo);
	}
}

/**
 * unregister_file_opener:
 * @fo          : GnumFileOpener object previously registered using
 *                register_file_opener
 *
 * Removes @fo opener from list of available file openers. Reference count
 * for the opener is decremented inside the function.
 */
void
unregister_file_opener (GnumFileOpener *fo)
{
	gint pos;
	GList *l;
	const gchar *id;

	g_return_if_fail (IS_GNUM_FILE_OPENER (fo));

	pos = g_list_index (file_opener_list, fo);
	g_return_if_fail (pos != -1);
	l = g_list_nth (file_opener_list, pos);
	file_opener_list = g_list_remove_link (file_opener_list, l);
	g_list_free_1 (l);
	l = g_list_nth (file_opener_priority_list, pos);
	file_opener_priority_list = g_list_remove_link (file_opener_priority_list, l);
	g_list_free_1 (l);

	id = gnum_file_opener_get_id (fo);
	if (id != NULL) {
		g_hash_table_remove (file_opener_id_hash, (gpointer) id);
		if (g_hash_table_size (file_opener_id_hash) == 0) {
			g_hash_table_destroy (file_opener_id_hash);
			file_opener_id_hash = NULL;
		}
	}

	gtk_object_unref (GTK_OBJECT (fo));
}


/**
 * unregister_file_opener_as_importer:
 * @fo          : GnumFileOpener object previously registered using
 *                register_file_opener_as_importer
 *
 * Removes @fo opener from list of available file importers. Reference count
 * for the opener is decremented inside the function.
 */
void
unregister_file_opener_as_importer (GnumFileOpener *fo)
{
	GList *l;
	const gchar *id;

	g_return_if_fail (IS_GNUM_FILE_OPENER (fo));

	l = g_list_find (file_importer_list, fo);
	g_return_if_fail (l != NULL);
	file_importer_list = g_list_remove_link (file_importer_list, l);
	g_list_free_1 (l);

	id = gnum_file_opener_get_id (fo);
	if (id != NULL) {
		g_hash_table_remove (file_opener_id_hash, (gpointer) id);
		if (g_hash_table_size (file_opener_id_hash) == 0) {
			g_hash_table_destroy (file_opener_id_hash);
			file_opener_id_hash = NULL;
		}
	}

	gtk_object_unref (GTK_OBJECT (fo));
}

static gint
default_file_saver_cmp_priority (gconstpointer a, gconstpointer b)
{
	const DefaultFileSaver *dfs_a = a, *dfs_b = b;

	return dfs_b->priority - dfs_a->priority;
}

/**
 * register_file_saver:
 * @fs          : GnumFileSaver object
 *
 * Adds @fs saver to the list of available file savers, making it
 * available for the user when selecting file format for save.
 * Reference count for the saver is incremented inside the function, but
 * you don't have to (and shouldn't) call gtk_object_unref on it if it's
 * floating object (for example, when you pass object newly created with
 * gnum_file_saver_new and not referenced anywhere).
 */
void
register_file_saver (GnumFileSaver *fs)
{
	const gchar *id;

	g_return_if_fail (IS_GNUM_FILE_SAVER (fs));

	gtk_object_ref (GTK_OBJECT (fs));
	gtk_object_sink (GTK_OBJECT (fs));

	file_saver_list = g_list_prepend (file_saver_list, fs);
	id = gnum_file_saver_get_id (fs);
	if (id != NULL) {
		if (file_saver_id_hash	== NULL) {
			file_saver_id_hash = g_hash_table_new (&g_str_hash, &g_str_equal);
		}
		g_hash_table_insert (file_saver_id_hash, (gpointer) id, fs);
	}
}

/**
 * register_file_saver_as_default:
 * @fs          : GnumFileSaver object
 * @priority    : Saver's priority
 *
 * Adds @fs saver to the list of available file savers, making it
 * available for the user when selecting file format for save.
 * The saver is also marked as default saver with given priority.
 * When Gnumeric needs default file saver, it chooses the one with the
 * highest priority. Recommended range for @priority is [0, 100].
 * Reference count for the saver is incremented inside the function, but
 * you don't have to (and shouldn't) call gtk_object_unref on it if it's
 * floating object (for example, when you pass object newly created with
 * gnum_file_saver_new and not referenced anywhere).
 */
void
register_file_saver_as_default (GnumFileSaver *fs, gint priority)
{
	DefaultFileSaver *dfs;

	g_return_if_fail (IS_GNUM_FILE_SAVER (fs));
	g_return_if_fail (priority >=0 && priority <= 100);

	register_file_saver (fs);

	dfs = g_new (DefaultFileSaver, 1);
	dfs->priority = priority;
	dfs->saver = fs;
	default_file_saver_list = g_list_insert_sorted (
	                          default_file_saver_list, dfs,
	                          default_file_saver_cmp_priority);
}

/**
 * unregister_file_saver:
 * @fs          : GnumFileSaver object previously registered using
 *                register_file_saver or register_file_saver_as_default
 *
 * Removes @fs saver from list of available file savers. Reference count
 * for the saver is decremented inside the function.
 */
void
unregister_file_saver (GnumFileSaver *fs)
{
	GList *l;
	const gchar *id;

	g_return_if_fail (IS_GNUM_FILE_SAVER (fs));

	l = g_list_find (file_saver_list, fs);
	g_return_if_fail (l != NULL);
	file_saver_list = g_list_remove_link (file_saver_list, l);
	g_list_free_1 (l);

	id = gnum_file_saver_get_id (fs);
	if (id != NULL) {
		g_hash_table_remove (file_saver_id_hash, (gpointer) id);
		if (g_hash_table_size (file_saver_id_hash) == 0) {
			g_hash_table_destroy (file_saver_id_hash);
			file_saver_id_hash = NULL;
		}
	}

	for (l = default_file_saver_list; l != NULL; l = l->next) {
		if (((DefaultFileSaver *) l->data)->saver == fs) {
			default_file_saver_list = g_list_remove_link (default_file_saver_list, l);
			g_list_free_1 (l);
			g_free (l->data);
			break;
		}
	}

	gtk_object_unref (GTK_OBJECT (fs));
}

/**
 * get_default_file_saver:
 *
 * Returns file saver registered as default saver with the highest priority.
 * Reference count for the saver is NOT incremented.
 *
 * Return value: GnumFileSaver object or NULL if default saver is not
 *               available.
 */
GnumFileSaver *
get_default_file_saver (void)
{
	if (default_file_saver_list == NULL) {
		return NULL;
	}

	return ((DefaultFileSaver *) default_file_saver_list->data)->saver;
}

/**
 * get_file_saver_for_mime_type:
 * @mime_type: A mime type
 *
 * Returns a file saver that claims to save files with given mime type.
 *
 * Return value: GnumFileSaver object or NULL if no suitable file saver could
 *               be found.
 */
GnumFileSaver *
get_file_saver_for_mime_type (const gchar *mime_type)
{
	GList *l;

	for (l = file_saver_list; l != NULL; l = l->next) {
		if (!strcmp (gnum_file_saver_get_mime_type (l->data), mime_type)) {
			return (l->data);
		}
	}
	return (NULL);
}

/**
 * get_file_opener_by_id:
 * @id          : File opener's ID
 *
 * Searches for file opener with given @id, registered using
 * register_file_opener or register_file_opener_as_importer.
 *
 * Return value: GnumFileOpener object or NULL if opener cannot be found.
 */
GnumFileOpener *
get_file_opener_by_id (const gchar *id)
{
	g_return_val_if_fail (id != NULL, NULL);

	if (file_opener_id_hash == NULL) {
		return NULL;
	}

	return GNUM_FILE_OPENER (g_hash_table_lookup (file_opener_id_hash, id));
}

/**
 * get_file_saver_by_id:
 * @id          : File saver's ID
 *
 * Searches for file saver with given @id, registered using
 * register_file_saver or register_file_opener_as_default.
 *
 * Return value: GnumFileSaver object or NULL if saver cannot be found.
 */
GnumFileSaver *
get_file_saver_by_id (const gchar *id)
{
	g_return_val_if_fail (id != NULL, NULL);

	if (file_saver_id_hash == NULL) {
		return NULL;
	}

	return GNUM_FILE_SAVER (g_hash_table_lookup (file_saver_id_hash, id));
}

/**
 * get_file_savers:
 *
 * Returns the list of registered file savers (using register_file_saver or
 * register_file_saver_as_default).
 *
 * Return value: list of GnumFileSaver objects, which you shouldn't modify.
 */
GList *
get_file_savers (void)
{
	return file_saver_list;
}

/**
 * get_file_openers:
 *
 * Returns the list of registered file openers (using register_file_opener).
 *
 * Return value: list of GnumFileOpener objects, which you shouldn't modify.
 */
GList *
get_file_openers (void)
{
	return file_opener_list;
}

/**
 * get_file_importers:
 *
 * Returns the list of registered file importers (using
 * register_file_opener_as_importer).
 *
 * Return value: list of GnumFileOpener objects, which you shouldn't modify.
 */
GList *
get_file_importers (void)
{
	return file_importer_list;
}
