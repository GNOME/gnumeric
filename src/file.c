/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * file.c: File loading and saving routines
 *
 * Authors:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Zbigniew Chyla (cyba@gnome.pl)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "file.h"

#include "file-priv.h"
#include "io-context.h"
#include "command-context.h"
#include "sheet.h"
#include "application.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"
#include "gutils.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgnome/libgnome.h>
#include <gal/util/e-util.h>
#ifdef ENABLE_BONOBO
#include <bonobo/bonobo-exception.h>
#endif

static void
gnum_file_opener_init (GnumFileOpener *fo)
{
	fo->id = NULL;
	fo->description = NULL;
	fo->probe_func = NULL;
	fo->open_func = NULL;
}

static void
gnum_file_opener_finalize (GObject *obj)
{
	GnumFileOpener *fo;

	g_return_if_fail (IS_GNUM_FILE_OPENER (obj));

	fo = GNUM_FILE_OPENER (obj);
	g_free (fo->id);
	g_free (fo->description);

	G_OBJECT_CLASS (g_type_class_peek (G_TYPE_OBJECT))->finalize (obj);
}

static gboolean
gnum_file_opener_probe_real (GnumFileOpener const *fo, gchar const *file_name,
                             FileProbeLevel pl)
{
	if (fo->probe_func != NULL)
		return fo->probe_func (fo, file_name, pl);
	return FALSE;
}

static void
gnum_file_opener_open_real (GnumFileOpener const *fo, IOContext *io_context,
                            WorkbookView *wbv, gchar const *file_name)
{
	if (fo->open_func != NULL)
		fo->open_func (fo, io_context, wbv, file_name);
	else
		gnumeric_io_error_unknown (io_context);
}

static void
gnum_file_opener_class_init (GnumFileOpenerClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = gnum_file_opener_finalize;

	klass->probe = gnum_file_opener_probe_real;
	klass->open = gnum_file_opener_open_real;
}

E_MAKE_TYPE (gnum_file_opener, "GnumFileOpener", GnumFileOpener,
             gnum_file_opener_class_init, gnum_file_opener_init,
             G_TYPE_OBJECT)

/**
 * gnum_file_opener_setup:
 * @fo          : Newly created GnumFileOpener object
 * @id          : Optional ID of the opener (or NULL)
 * @description : Description of supported file format
 * @probe_func  : Optional pointer to "probe" function (or NULL)
 * @open_func   : Pointer to "open" function
 *
 * Sets up GnumFileOpener object, newly created with g_object_new function.
 * This is intended to be used only by GnumFileOpener derivates.
 * Use gnum_file_opener_new, if you want to create GnumFileOpener object.
 */
void
gnum_file_opener_setup (GnumFileOpener *fo, gchar const *id,
                        gchar const *description,
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
gnum_file_opener_new (gchar const *id,
                      gchar const *description,
                      GnumFileOpenerProbeFunc probe_func,
                      GnumFileOpenerOpenFunc open_func)
{
	GnumFileOpener *fo;

	fo = GNUM_FILE_OPENER (g_object_new (TYPE_GNUM_FILE_OPENER, NULL));
	gnum_file_opener_setup (fo, id, description, probe_func, open_func);

	return fo;
}

gchar const *
gnum_file_opener_get_id (GnumFileOpener const *fo)
{
	g_return_val_if_fail (IS_GNUM_FILE_OPENER (fo), FALSE);

	return fo->id;
}

gchar const *
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
gnum_file_opener_probe (GnumFileOpener const *fo, gchar const *file_name, FileProbeLevel pl)
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
                       WorkbookView *wbv, gchar const *file_name)
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
	fs->overwrite_files = TRUE;
	fs->format_level = FILE_FL_NEW;
	fs->save_scope = FILE_SAVE_WORKBOOK;
	fs->save_func = NULL;
}

static void
gnum_file_saver_finalize (GObject *obj)
{
	GnumFileSaver *fs;

	g_return_if_fail (IS_GNUM_FILE_SAVER (obj));

	fs = GNUM_FILE_SAVER (obj);
	g_free (fs->id);
	g_free (fs->extension);
	g_free (fs->description);

	G_OBJECT_CLASS (g_type_class_peek (G_TYPE_OBJECT))->finalize (obj);
}

static void
gnum_file_saver_save_real (GnumFileSaver const *fs, IOContext *io_context,
                           WorkbookView *wbv, gchar const *file_name)
{
	if (fs->save_func == NULL) {
		gnumeric_io_error_unknown (io_context);
		return;
	}

	fs->save_func (fs, io_context, wbv, file_name);
}

#ifdef ENABLE_BONOBO
#define FILE_COPY_CHUNK_SIZE  0x10000

static void
gnum_file_saver_save_to_stream_real (GnumFileSaver const *fs,
                                     IOContext *io_context,
                                     WorkbookView *wbv,
                                     BonoboStream *stream,
                                     CORBA_Environment *ev)
{
	gchar *tmp_name;
	gint old_umask;
	gint fd;

	tmp_name = g_concat_dir_and_file (g_get_tmp_dir (), "gnumeric-file-XXXXXX");
	old_umask = umask (0077);
	fd = mkstemp (tmp_name);
	if (fd == -1) {
		gnumeric_io_error_info_set (io_context, error_info_new_str_with_details (
		                            _("Cannot create temporary file."),
		                            error_info_new_from_errno ()));
	} else {
		struct stat sbuf;

		io_progress_range_push (io_context, 0.0, 0.5);
		gnum_file_saver_save (fs, io_context, wbv, tmp_name);
		io_progress_range_pop (io_context);
		(void) unlink (tmp_name);

		if (gnumeric_io_error_occurred (io_context)) {
			gnumeric_io_error_push (io_context, error_info_new_str (
			                        _("Error saving to temporary file.")));
		} else if (fstat (fd, &sbuf) == -1) {
			gnumeric_io_error_info_set (io_context, error_info_new_str_with_details (
			                            _("Cannot get temporary file size."),
			                            error_info_new_from_errno ()));
		} else {
			Bonobo_Stream_iobuf *iobuf;
			gint offset;

			io_progress_message (io_context, _("Saving file to stream."));
			io_progress_range_push (io_context, 0.5, 1.0);
			iobuf = Bonobo_Stream_iobuf__alloc ();
			value_io_progress_set (io_context, sbuf.st_size, FILE_COPY_CHUNK_SIZE);
			offset = 0;
			while (1) {
				gchar buf[FILE_COPY_CHUNK_SIZE];
				gint read_bytes;

				value_io_progress_update (io_context, offset);
				read_bytes = read (fd, buf, FILE_COPY_CHUNK_SIZE);
				if (read_bytes == -1 && errno != EINTR) {
					gnumeric_io_error_info_set (io_context, error_info_new_str_with_details (
					                            _("Error reading temporary file."),
					                            error_info_new_from_errno ()));
					break;
				} else if (read_bytes == -1) {
					continue;
				} else if (read_bytes == 0) {
					break;
				}
				iobuf->_buffer = buf;
				iobuf->_length = read_bytes;
				Bonobo_Stream_write (BONOBO_OBJREF (stream), iobuf, ev);
				if (BONOBO_EX (ev)) {
					gnumeric_io_error_string (
					io_context, _("Exception occurred while saving to stream."));
					break;
				}
				offset += read_bytes;
			}
			io_progress_unset (io_context);
			CORBA_free (iobuf);
			io_progress_range_pop (io_context);
		}
		(void) close (fd);
	}
	g_free (tmp_name);
	(void) umask (old_umask);
}
#endif

static void
gnum_file_saver_class_init (GnumFileSaverClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = gnum_file_saver_finalize;

	klass->save = gnum_file_saver_save_real;
#ifdef ENABLE_BONOBO
	klass->save_to_stream = gnum_file_saver_save_to_stream_real;
#endif
}

E_MAKE_TYPE (gnum_file_saver, "GnumFileSaver", GnumFileSaver,
             gnum_file_saver_class_init, gnum_file_saver_init,
             G_TYPE_OBJECT)

/**
 * gnum_file_saver_setup:
 * @fs          : Newly created GnumFileSaver object
 * @id          : Optional ID of the saver (or NULL)
 * @extension   : Optional default extension of saved files (or NULL)
 * @description : Description of supported file format
 * @level       : File format level
 * @save_func   : Pointer to "save" function
 *
 * Sets up GnumFileSaver object, newly created with g_object_new function.
 * This is intended to be used only by GnumFileSaver derivates.
 * Use gnum_file_saver_new, if you want to create GnumFileSaver object.
 */
void
gnum_file_saver_setup (GnumFileSaver *fs, gchar const *id,
                       gchar const *extension,
                       gchar const *description,
                       FileFormatLevel level,
                       GnumFileSaverSaveFunc save_func)
{
	g_return_if_fail (IS_GNUM_FILE_SAVER (fs));

	fs->id = g_strdup (id);
	fs->mime_type = NULL;

#warning mime disabled
#if 0
	gchar *tmp = g_strdup_printf ("SomeFile.%s", extension);
	gnome_mime_type_or_default (tmp,
	    "application/application/x-gnumeric");
	g_free (tmp);
#endif
	fs->extension = g_strdup (extension);
	fs->description = g_strdup (description);
	fs->format_level = level;
	fs->save_func = save_func;
}

/**
 * gnum_file_saver_new:
 * @id          : Optional ID of the saver (or NULL)
 * @extension   : Optional default extension of saved files (or NULL)
 * @description : Description of supported file format
 * @level       : File format level
 * @save_func   : Pointer to "save" function
 *
 * Creates new GnumFileSaver object. Optional @id will be used
 * after registering it with register_file_saver or
 * register_file_saver_as_default function.
 *
 * Return value: newly created GnumFileSaver object.
 */
GnumFileSaver *
gnum_file_saver_new (gchar const *id,
                     gchar const *extension,
                     gchar const *description,
                     FileFormatLevel level,
                     GnumFileSaverSaveFunc save_func)
{
	GnumFileSaver *fs;

	fs = GNUM_FILE_SAVER (g_object_new (TYPE_GNUM_FILE_SAVER, NULL));
	gnum_file_saver_setup (fs, id, extension, description, level, save_func);

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

gchar const *
gnum_file_saver_get_id (GnumFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNUM_FILE_SAVER (fs), FALSE);

	return fs->id;
}

gchar const *
gnum_file_saver_get_mime_type (GnumFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNUM_FILE_SAVER (fs), FALSE);

	return fs->mime_type;
}

gchar const *
gnum_file_saver_get_extension (GnumFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNUM_FILE_SAVER (fs), FALSE);

	return fs->extension;
}

gchar const *
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
                      WorkbookView *wbv, gchar const *file_name)
{
	g_return_if_fail (IS_GNUM_FILE_SAVER (fs));
	g_return_if_fail (file_name != NULL);

	if (!fs->overwrite_files && g_file_exists (file_name)) {
		ErrorInfo *save_error;

		save_error = error_info_new_str_with_details (
		_("Saving over old files of this type is disabled for safety."),
		error_info_new_str (
		_("You can turn this safety feature off by editing appropriate plugin.xml file.")));
		gnumeric_io_error_info_set (io_context, save_error);
		return;
	}

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
 * gnum_file_saver_set_overwrite_files:
 * @fs          : GnumFileSaver object
 * @overwrite   : A boolean value saying whether the saver should overwrite
 *                existing files.
 *
 * Changes behaviour of the saver when saving a file. If @overwrite is set
 * to TRUE, existing file will be overwritten. Otherwise, the saver will
 * report an error without saving anything.
 */
void
gnum_file_saver_set_overwrite_files (GnumFileSaver *fs, gboolean overwrite)
{
	g_return_if_fail (IS_GNUM_FILE_SAVER (fs));

	fs->overwrite_files = overwrite;
}

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
gnum_file_saver_fix_file_name (GnumFileSaver const *fs, gchar const *file_name)
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

typedef struct {
	gint priority;
	GnumFileOpener *opener;
} DefaultFileImporter;

static GHashTable *file_opener_id_hash = NULL,
                  *file_saver_id_hash = NULL;
static GList *file_opener_list = NULL, *file_opener_priority_list = NULL;
static GList *file_importer_list = NULL, *default_file_importer_list = NULL;
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
 * you don't have to (and shouldn't) call g_object_unref on it if it's
 * floating object (for example, when you pass object newly created with
 * gnum_file_opener_new and not referenced anywhere).
 */
void
register_file_opener (GnumFileOpener *fo, gint priority)
{
	gint pos;
	gchar const *id;

	g_return_if_fail (IS_GNUM_FILE_OPENER (fo));
	g_return_if_fail (priority >=0 && priority <= 100);

	pos = g_list_index_custom (file_opener_priority_list,
	                           GINT_TO_POINTER (priority),
	                           cmp_int_less_than);
	file_opener_priority_list = g_list_insert (
	                            file_opener_priority_list,
	                            GINT_TO_POINTER (priority), pos);
	file_opener_list = g_list_insert (file_opener_list, fo, pos);
	g_object_ref (G_OBJECT (fo));

	id = gnum_file_opener_get_id (fo);
	if (id != NULL) {
		if (file_opener_id_hash	== NULL)
			file_opener_id_hash = g_hash_table_new (&g_str_hash, &g_str_equal);
		g_hash_table_insert (file_opener_id_hash, (gpointer) id, fo);
	}
}

static gint
default_file_importer_cmp_priority (gconstpointer a, gconstpointer b)
{
	DefaultFileImporter const *dfi_a = a, *dfi_b = b;

	return dfi_b->priority - dfi_a->priority;
}

/**
 * register_file_opener_as_importer:
 * @fo          : GnumFileOpener object
 *
 * Adds @fo opener to the list of available file importers. The opener will
 * not be tried when reading files using Gnumeric i/o routines (unless you
 * call register_file_opener on it), but it will be available for the user
 * when importing the file and selecting file opener manually.
 */
void
register_file_opener_as_importer (GnumFileOpener *fo)
{
	gchar const *id;

	g_return_if_fail (IS_GNUM_FILE_OPENER (fo));

	file_importer_list = g_list_prepend (file_importer_list, fo);
	g_object_ref (G_OBJECT (fo));

	id = gnum_file_opener_get_id (fo);
	if (id != NULL) {
		if (file_opener_id_hash	== NULL)
			file_opener_id_hash = g_hash_table_new (&g_str_hash,
								&g_str_equal);
		g_hash_table_insert (file_opener_id_hash, (gpointer) id, fo);
	}
}

/**
 * register_file_opener_as_importer_as_default:
 * @fo          : GnumFileOpener object
 * @priority    : Opener's priority (as an importer)
 *
 * Adds @fo opener to the list of available file importers.
 * The opener is * also marked as default importer with given priority.
 * When Gnumeric needs default file importer, it chooses the one with the
 * highest priority. Recommended range for @priority is [0, 100].
 * The opener will not be tried when reading files using Gnumeric i/o
 * routines (unless you call register_file_opener on it), but it will be
 * available for the user when importing the file and selecting file opener
 * manually (in that case the default importer will be selected by default).
 */
void
register_file_opener_as_importer_as_default (GnumFileOpener *fo, gint priority)
{
	DefaultFileImporter	*dfi;

	g_return_if_fail (IS_GNUM_FILE_OPENER (fo));
	g_return_if_fail (priority >=0 && priority <= 100);

	register_file_opener_as_importer (fo);

	dfi = g_new (DefaultFileImporter, 1);
	dfi->priority = priority;
	dfi->opener = fo;
	default_file_importer_list = g_list_insert_sorted (
	                             default_file_importer_list, dfi,
	                             default_file_importer_cmp_priority);
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
	gchar const *id;

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

	g_object_unref (G_OBJECT (fo));
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
	gchar const *id;

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

	for (l = default_file_importer_list; l != NULL; l = l->next) {
		if (((DefaultFileImporter *) l->data)->opener == fo) {
			default_file_importer_list = g_list_remove_link (default_file_importer_list, l);
			g_list_free_1 (l);
			g_free (l->data);
			break;
		}
	}

	g_object_unref (G_OBJECT (fo));
}

/**
 * get_default_file_importer:
 *
 * Returns file opener registered as default importer with the highest
 * priority. Reference count for the saver is NOT incremented.
 *
 * Return value: GnumFileOpener object or NULL if default importer is not
 *               available.
 */
GnumFileOpener *
get_default_file_importer (void)
{
	if (default_file_importer_list == NULL) {
		return NULL;
	}

	return ((DefaultFileImporter *) default_file_importer_list->data)->opener;
}

static gint
default_file_saver_cmp_priority (gconstpointer a, gconstpointer b)
{
	DefaultFileSaver const *dfs_a = a, *dfs_b = b;

	return dfs_b->priority - dfs_a->priority;
}

/**
 * register_file_saver:
 * @fs          : GnumFileSaver object
 *
 * Adds @fs saver to the list of available file savers, making it
 * available for the user when selecting file format for save.
 */
void
register_file_saver (GnumFileSaver *fs)
{
	gchar const *id;

	g_return_if_fail (IS_GNUM_FILE_SAVER (fs));

	file_saver_list = g_list_prepend (file_saver_list, fs);
	g_object_ref (G_OBJECT (fs));

	id = gnum_file_saver_get_id (fs);
	if (id != NULL) {
		if (file_saver_id_hash	== NULL)
			file_saver_id_hash = g_hash_table_new (&g_str_hash,
							       &g_str_equal);
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
	gchar const *id;

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

	g_object_unref (G_OBJECT (fs));
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
	if (default_file_saver_list == NULL)
		return NULL;

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
get_file_saver_for_mime_type (gchar const *mime_type)
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
get_file_opener_by_id (gchar const *id)
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
get_file_saver_by_id (gchar const *id)
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
