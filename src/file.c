/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * file.c: File loading and saving routines
 *
 * Authors:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Zbigniew Chyla (cyba@gnome.pl)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
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
#include "error-info.h"

#include <gsf/gsf-input.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-utils.h>
#include <string.h>
#include <goffice/utils/go-file.h>

static void
gnm_file_opener_init (GnmFileOpener *fo)
{
	fo->id = NULL;
	fo->description = NULL;
	fo->probe_func = NULL;
	fo->open_func = NULL;
}

static void
gnm_file_opener_finalize (GObject *obj)
{
	GnmFileOpener *fo;

	g_return_if_fail (IS_GNM_FILE_OPENER (obj));

	fo = GNM_FILE_OPENER (obj);
	g_free (fo->id);
	g_free (fo->description);
	g_slist_foreach (fo->suffixes, (GFunc)g_free, NULL);
	g_slist_free (fo->suffixes);
	g_slist_foreach (fo->mimes, (GFunc)g_free, NULL);
	g_slist_free (fo->mimes);

	G_OBJECT_CLASS (g_type_class_peek (G_TYPE_OBJECT))->finalize (obj);
}

static gboolean
gnm_file_opener_can_probe_real (GnmFileOpener const *fo, FileProbeLevel pl)
{
	return fo->probe_func != NULL;
}

static gboolean
gnm_file_opener_probe_real (GnmFileOpener const *fo, GsfInput *input,
                             FileProbeLevel pl)
{
	gboolean ret = FALSE;

	if (fo->probe_func != NULL) {
		ret =  fo->probe_func (fo, input, pl);
		gsf_input_seek (input, 0, G_SEEK_SET);
	}
	return ret;
}

static void
gnm_file_opener_open_real (GnmFileOpener const *fo, gchar const *opt_enc, 
			   IOContext *io_context,
			   WorkbookView *wbv, GsfInput *input)
{
	if (fo->open_func != NULL)
		if (fo->encoding_dependent)
			((GnmFileOpenerOpenFuncWithEnc)fo->open_func) 
				(fo, opt_enc, io_context, wbv, input);
		else
			fo->open_func (fo, io_context, wbv, input);
	else
		gnumeric_io_error_unknown (io_context);
}

static void
gnm_file_opener_class_init (GnmFileOpenerClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = gnm_file_opener_finalize;

	klass->can_probe = gnm_file_opener_can_probe_real;
	klass->probe = gnm_file_opener_probe_real;
	klass->open = gnm_file_opener_open_real;
}

GSF_CLASS (GnmFileOpener, gnm_file_opener,
	   gnm_file_opener_class_init, gnm_file_opener_init,
	   G_TYPE_OBJECT)

/**
 * gnm_file_opener_setup:
 * @fo          : Newly created GnmFileOpener object
 * @id          : Optional ID of the opener (or NULL)
 * @description : Description of supported file format
 * @encoding_dependent: whether the opener depends on an encoding sel.
 * @probe_func  : Optional pointer to "probe" function (or NULL)
 * @open_func   : Pointer to "open" function
 *
 * Sets up GnmFileOpener object, newly created with g_object_new function.
 * This is intended to be used only by GnmFileOpener derivates.
 * Use gnm_file_opener_new, if you want to create GnmFileOpener object.
 */
void
gnm_file_opener_setup (GnmFileOpener *fo, gchar const *id,
                        gchar const *description,
			GSList *suffixes,
			GSList *mimes,
		        gboolean encoding_dependent,
                        GnmFileOpenerProbeFunc probe_func,
                        GnmFileOpenerOpenFunc open_func)
{
	g_return_if_fail (IS_GNM_FILE_OPENER (fo));

	fo->id = g_strdup (id);
	fo->description = g_strdup (description);
	fo->suffixes = suffixes;
	fo->mimes = mimes;

	fo->encoding_dependent = encoding_dependent;
	fo->probe_func = probe_func;
	fo->open_func = open_func;
}

/**
 * gnm_file_opener_new:
 * @id          : Optional ID of the opener (or NULL)
 * @description : Description of supported file format
 * @probe_func  : Optional pointer to "probe" function (or NULL)
 * @open_func   : Pointer to "open" function
 *
 * Creates new GnmFileOpener object. Optional @id will be used
 * after registering it with gnm_file_opener_register function.
 *
 * Return value: newly created GnmFileOpener object.
 */
GnmFileOpener *
gnm_file_opener_new (gchar const *id,
                      gchar const *description,
		      GSList *suffixes,
		      GSList *mimes,
                      GnmFileOpenerProbeFunc probe_func,
                      GnmFileOpenerOpenFunc open_func)
{
	GnmFileOpener *fo;

	fo = GNM_FILE_OPENER (g_object_new (TYPE_GNM_FILE_OPENER, NULL));
	gnm_file_opener_setup (fo, id, description, suffixes, mimes, FALSE,
			       probe_func, open_func);

	return fo;
}

/**
 * gnm_file_opener_new_with_enc:
 * @id          : Optional ID of the opener (or NULL)
 * @description : Description of supported file format 
 * @probe_func  : Optional pointer to "probe" function (or NULL)
 * @open_func   : Pointer to "open" function    
 *   
 * Creates new GnmFileOpener object. Optional @id will be used
 * after registering it with gnm_file_opener_register function.
 *      
 * Return value: newly created GnmFileOpener object.
 */

GnmFileOpener *
gnm_file_opener_new_with_enc (gchar const *id,
		     gchar const *description,
		     GSList *suffixes,
		     GSList *mimes,
		     GnmFileOpenerProbeFunc probe_func,
		     GnmFileOpenerOpenFuncWithEnc open_func)
{
        GnmFileOpener *fo;

        fo = GNM_FILE_OPENER (g_object_new (TYPE_GNM_FILE_OPENER, NULL));
        gnm_file_opener_setup (fo, id, description, suffixes, mimes, TRUE,
                               probe_func, (GnmFileOpenerOpenFunc)open_func);
        return fo;
}




gchar const *
gnm_file_opener_get_id (GnmFileOpener const *fo)
{
	g_return_val_if_fail (IS_GNM_FILE_OPENER (fo), NULL);

	return fo->id;
}

gchar const *
gnm_file_opener_get_description (GnmFileOpener const *fo)
{
	g_return_val_if_fail (IS_GNM_FILE_OPENER (fo), NULL);

	return fo->description;
}

gboolean 
gnm_file_opener_is_encoding_dependent (GnmFileOpener const *fo)
{
        g_return_val_if_fail (IS_GNM_FILE_OPENER (fo), FALSE);
	
	return fo->encoding_dependent;
}

gboolean
gnm_file_opener_can_probe (GnmFileOpener const *fo, FileProbeLevel pl)
{
	g_return_val_if_fail (IS_GNM_FILE_OPENER (fo), FALSE);

	return GNM_FILE_OPENER_METHOD (fo, can_probe) (fo, pl);
}

GSList const *
gnm_file_opener_get_suffixes (GnmFileOpener const *fo)
{
	g_return_val_if_fail (IS_GNM_FILE_OPENER (fo), NULL);
	return fo->suffixes;
}
GSList const *
gnm_file_opener_get_mimes (GnmFileOpener const *fo)
{
	g_return_val_if_fail (IS_GNM_FILE_OPENER (fo), NULL);
	return fo->mimes;
}


/**
 * gnm_file_opener_probe:
 * @fo      : GnmFileOpener object
 * @input   : The input source
 *
 * Checks if a given file is supported by the opener.
 *
 * Return value: TRUE, if the opener can read given file and FALSE
 *               otherwise.
 */
gboolean
gnm_file_opener_probe (GnmFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	g_return_val_if_fail (IS_GNM_FILE_OPENER (fo), FALSE);
	g_return_val_if_fail (GSF_IS_INPUT (input), FALSE);

#if 0
	g_print ("Trying format %s at level %d...\n",
		 gnm_file_opener_get_id (fo),
		 (int)pl);
#endif
	return GNM_FILE_OPENER_METHOD (fo, probe) (fo, input, pl);
}

/**
 * gnm_file_opener_open:
 * @fo          : GnmFileOpener object
 * @opt_enc     : Optional encoding
 * @io_context  : Context for i/o operation
 * @wbv         : Workbook View
 * @input       : Gsf input stream
 *
 * Reads content of @file_name file into workbook @wbv is attached to.
 * Results are reported using @io_context object, use
 * gnumeric_io_error_occurred to find out if operation was successful.
 * The state of @wbv and its workbook is undefined if operation fails, you
 * should destroy them in that case.
 */
void
gnm_file_opener_open (GnmFileOpener const *fo, gchar const *opt_enc,
		      IOContext *io_context,
		      WorkbookView *wbv, GsfInput *input)
{
	const char *input_name;

	g_return_if_fail (IS_GNM_FILE_OPENER (fo));
	g_return_if_fail (GSF_IS_INPUT (input));

	input_name = gsf_input_name (input);
	if (input_name) {
		/*
		 * When we open a file, the input get a name that is its
		 * absolute filename. 
		 */
		char *uri = go_shell_arg_to_uri (input_name);
		workbook_set_uri (wb_view_workbook (wbv), uri);
		g_free (uri);
	}
	GNM_FILE_OPENER_METHOD (fo, open) (fo, opt_enc, io_context, wbv, input);
}

/*
 * GnmFileSaver
 */

static void
gnm_file_saver_init (GnmFileSaver *fs)
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
gnm_file_saver_finalize (GObject *obj)
{
	GnmFileSaver *fs;

	g_return_if_fail (IS_GNM_FILE_SAVER (obj));

	fs = GNM_FILE_SAVER (obj);
	g_free (fs->id);
	g_free (fs->extension);
	g_free (fs->description);

	G_OBJECT_CLASS (g_type_class_peek (G_TYPE_OBJECT))->finalize (obj);
}

static void
gnm_file_saver_save_real (GnmFileSaver const *fs, IOContext *io_context,
                           WorkbookView const *wbv, GsfOutput *output)
{
	if (fs->save_func == NULL) {
		gnumeric_io_error_unknown (io_context);
		return;
	}

	fs->save_func (fs, io_context, wbv, output);
}

static void
gnm_file_saver_class_init (GnmFileSaverClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = gnm_file_saver_finalize;

	klass->save = gnm_file_saver_save_real;
}

GSF_CLASS (GnmFileSaver, gnm_file_saver,
	   gnm_file_saver_class_init, gnm_file_saver_init,
	   G_TYPE_OBJECT)

/**
 * gnm_file_saver_setup:
 * @fs          : Newly created GnmFileSaver object
 * @id          : Optional ID of the saver (or NULL)
 * @extension   : Optional default extension of saved files (or NULL)
 * @description : Description of supported file format
 * @level       : File format level
 * @save_func   : Pointer to "save" function
 *
 * Sets up GnmFileSaver object, newly created with g_object_new function.
 * This is intended to be used only by GnmFileSaver derivates.
 * Use gnm_file_saver_new, if you want to create GnmFileSaver object.
 */
void
gnm_file_saver_setup (GnmFileSaver *fs, gchar const *id,
                       gchar const *extension,
                       gchar const *description,
                       FileFormatLevel level,
                       GnmFileSaverSaveFunc save_func)
{
	g_return_if_fail (IS_GNM_FILE_SAVER (fs));

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
 * gnm_file_saver_new:
 * @id          : Optional ID of the saver (or NULL)
 * @extension   : Optional default extension of saved files (or NULL)
 * @description : Description of supported file format
 * @level       : File format level
 * @save_func   : Pointer to "save" function
 *
 * Creates new GnmFileSaver object. Optional @id will be used
 * after registering it with gnm_file_saver_register or
 * gnm_file_saver_register_as_default function.
 *
 * Return value: newly created GnmFileSaver object.
 */
GnmFileSaver *
gnm_file_saver_new (gchar const *id,
                     gchar const *extension,
                     gchar const *description,
                     FileFormatLevel level,
                     GnmFileSaverSaveFunc save_func)
{
	GnmFileSaver *fs;

	fs = GNM_FILE_SAVER (g_object_new (TYPE_GNM_FILE_SAVER, NULL));
	gnm_file_saver_setup (fs, id, extension, description, level, save_func);

	return fs;
}

void
gnm_file_saver_set_save_scope (GnmFileSaver *fs, FileSaveScope scope)
{
	g_return_if_fail (IS_GNM_FILE_SAVER (fs));
	g_return_if_fail (scope < FILE_SAVE_LAST);

	fs->save_scope = scope;
}

FileSaveScope
gnm_file_saver_get_save_scope (GnmFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNM_FILE_SAVER (fs), FILE_SAVE_WORKBOOK);

	return fs->save_scope;
}

gchar const *
gnm_file_saver_get_id (GnmFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNM_FILE_SAVER (fs), NULL);

	return fs->id;
}

gchar const *
gnm_file_saver_get_mime_type (GnmFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNM_FILE_SAVER (fs), NULL);

	return fs->mime_type;
}

gchar const *
gnm_file_saver_get_extension (GnmFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNM_FILE_SAVER (fs), NULL);

	return fs->extension;
}

gchar const *
gnm_file_saver_get_description (GnmFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNM_FILE_SAVER (fs), NULL);

	return fs->description;
}

FileFormatLevel
gnm_file_saver_get_format_level (GnmFileSaver const *fs)
{
	g_return_val_if_fail (IS_GNM_FILE_SAVER (fs), FILE_FL_NEW);

	return fs->format_level;
}

/**
 * gnm_file_saver_save:
 * @fs          : GnmFileSaver object
 * @io_context  : Context for i/o operation
 * @wbv         : Workbook View
 * @output      : Output stream
 *
 * Saves @wbv and the workbook it is attached to into @output stream.
 * Results are reported using @io_context object, use
 * gnumeric_io_error_occurred to find out if operation was successful.
 * It's possible that @file_name is created and contain some data if
 * operation fails, you should remove the file in that case.
 */
void
gnm_file_saver_save (GnmFileSaver const *fs, IOContext *io_context,
                      WorkbookView const *wbv, GsfOutput *output)
{
	char *file_name;

	g_return_if_fail (IS_GNM_FILE_SAVER (fs));
	g_return_if_fail (GSF_IS_OUTPUT (output));

	if (GSF_IS_OUTPUT_STDIO (output)) {
		file_name = (char *) gsf_output_name (output);
		g_return_if_fail (file_name != NULL);
		
		if (!fs->overwrite_files &&
		    g_file_test ((file_name), G_FILE_TEST_EXISTS)) {
			ErrorInfo *save_error;

			save_error = error_info_new_str_with_details (
				_("Saving over old files of this type is disabled for safety."),
				error_info_new_str (
					_("You can turn this safety feature off by editing appropriate plugin.xml file.")));
			gnumeric_io_error_info_set (io_context, save_error);
			return;
		}
	}

	GNM_FILE_SAVER_METHOD (fs, save) (fs, io_context, wbv, output);
}

/**
 * gnm_file_saver_set_overwrite_files:
 * @fs          : GnmFileSaver object
 * @overwrite   : A boolean value saying whether the saver should overwrite
 *                existing files.
 *
 * Changes behaviour of the saver when saving a file. If @overwrite is set
 * to TRUE, existing file will be overwritten. Otherwise, the saver will
 * report an error without saving anything.
 */
void
gnm_file_saver_set_overwrite_files (GnmFileSaver *fs, gboolean overwrite)
{
	g_return_if_fail (IS_GNM_FILE_SAVER (fs));

	fs->overwrite_files = overwrite;
}

/**
 * gnm_vrfy_uri_ext
 * @std_ext : Standard extension for the content type
 * @uri     : Uri
 * @new_uri : New uri
 *
 * Modifies given @uri by adding the extension @std_ext if needed.
 * If no @std_ext is given or @uri already has some extension,
 * it just copies @uri.
 *
 * Value in new_uri:  newly allocated string which you should free after 
 *                    use, containing (optionally) modified uri.
 *
 * Return Value:  FALSE if the uri has an extension not matching @std_ext
 */
gboolean
gnm_vrfy_uri_ext (gchar const *std_ext,
		  gchar const *uri,
		  gchar **new_uri)
{
	gchar *base;
	gchar *user_ext;
	gboolean res;

	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (new_uri != NULL, FALSE);

	res      = TRUE;
	base     = g_path_get_basename (uri);
	user_ext = strrchr (base, '.');
	if (std_ext != NULL && strlen (std_ext) > 0 && user_ext == NULL)
		*new_uri = g_strconcat (uri, ".", std_ext, NULL);
	else {
		if (user_ext != NULL && std_ext != NULL)
			res = !gnm_utf8_collate_casefold (user_ext + 1, std_ext);
		*new_uri = g_strdup (uri);
	}
	g_free (base);

	return res;
}


/*
 * ------
 */

typedef struct {
	gint priority;
	GnmFileSaver *saver;
} DefaultFileSaver;

static GHashTable *file_opener_id_hash = NULL,
                  *file_saver_id_hash = NULL;
static GList *file_opener_list = NULL, *file_opener_priority_list = NULL;
static GList *file_saver_list = NULL, *default_file_saver_list = NULL;

static gint
cmp_int_less_than (gconstpointer list_i, gconstpointer i)
{
	return !(GPOINTER_TO_INT (list_i) < GPOINTER_TO_INT (i));
}

/**
 * gnm_file_opener_register:
 * @fo          : GnmFileOpener object
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
 * gnm_file_opener_new and not referenced anywhere).
 */
void
gnm_file_opener_register (GnmFileOpener *fo, gint priority)
{
	gint pos;
	gchar const *id;

	g_return_if_fail (IS_GNM_FILE_OPENER (fo));
	g_return_if_fail (priority >=0 && priority <= 100);

	pos = gnm_list_index_custom (file_opener_priority_list,
	                           GINT_TO_POINTER (priority),
	                           cmp_int_less_than);
	file_opener_priority_list = g_list_insert (
	                            file_opener_priority_list,
	                            GINT_TO_POINTER (priority), pos);
	file_opener_list = g_list_insert (file_opener_list, fo, pos);
	g_object_ref (G_OBJECT (fo));

	id = gnm_file_opener_get_id (fo);
	if (id != NULL) {
		if (file_opener_id_hash	== NULL)
			file_opener_id_hash = g_hash_table_new (&g_str_hash, &g_str_equal);
		g_hash_table_insert (file_opener_id_hash, (gpointer) id, fo);
	}
}

/**
 * gnm_file_opener_unregister:
 * @fo          : GnmFileOpener object previously registered using
 *                gnm_file_opener_register
 *
 * Removes @fo opener from list of available file openers. Reference count
 * for the opener is decremented inside the function.
 */
void
gnm_file_opener_unregister (GnmFileOpener *fo)
{
	gint pos;
	GList *l;
	gchar const *id;

	g_return_if_fail (IS_GNM_FILE_OPENER (fo));

	pos = g_list_index (file_opener_list, fo);
	g_return_if_fail (pos != -1);
	l = g_list_nth (file_opener_list, pos);
	file_opener_list = g_list_remove_link (file_opener_list, l);
	g_list_free_1 (l);
	l = g_list_nth (file_opener_priority_list, pos);
	file_opener_priority_list = g_list_remove_link (file_opener_priority_list, l);
	g_list_free_1 (l);

	id = gnm_file_opener_get_id (fo);
	if (id != NULL) {
		g_hash_table_remove (file_opener_id_hash, (gpointer) id);
		if (g_hash_table_size (file_opener_id_hash) == 0) {
			g_hash_table_destroy (file_opener_id_hash);
			file_opener_id_hash = NULL;
		}
	}

	g_object_unref (G_OBJECT (fo));
}

static gint
default_file_saver_cmp_priority (gconstpointer a, gconstpointer b)
{
	DefaultFileSaver const *dfs_a = a, *dfs_b = b;

	return dfs_b->priority - dfs_a->priority;
}

/**
 * gnm_file_saver_register:
 * @fs          : GnmFileSaver object
 *
 * Adds @fs saver to the list of available file savers, making it
 * available for the user when selecting file format for save.
 */
void
gnm_file_saver_register (GnmFileSaver *fs)
{
	gchar const *id;

	g_return_if_fail (IS_GNM_FILE_SAVER (fs));

	file_saver_list = g_list_prepend (file_saver_list, fs);
	g_object_ref (G_OBJECT (fs));

	id = gnm_file_saver_get_id (fs);
	if (id != NULL) {
		if (file_saver_id_hash	== NULL)
			file_saver_id_hash = g_hash_table_new (&g_str_hash,
							       &g_str_equal);
		g_hash_table_insert (file_saver_id_hash, (gpointer) id, fs);
	}
}

/**
 * gnm_file_saver_register_as_default:
 * @fs          : GnmFileSaver object
 * @priority    : Saver's priority
 *
 * Adds @fs saver to the list of available file savers, making it
 * available for the user when selecting file format for save.
 * The saver is also marked as default saver with given priority.
 * When Gnumeric needs default file saver, it chooses the one with the
 * highest priority. Recommended range for @priority is [0, 100].
 */
void
gnm_file_saver_register_as_default (GnmFileSaver *fs, gint priority)
{
	DefaultFileSaver *dfs;

	g_return_if_fail (IS_GNM_FILE_SAVER (fs));
	g_return_if_fail (priority >=0 && priority <= 100);

	gnm_file_saver_register (fs);

	dfs = g_new (DefaultFileSaver, 1);
	dfs->priority = priority;
	dfs->saver = fs;
	default_file_saver_list = g_list_insert_sorted (
	                          default_file_saver_list, dfs,
	                          default_file_saver_cmp_priority);
}

/**
 * gnm_file_saver_unregister:
 * @fs          : GnmFileSaver object previously registered using
 *                gnm_file_saver_register or gnm_file_saver_register_as_default
 *
 * Removes @fs saver from list of available file savers. Reference count
 * for the saver is decremented inside the function.
 */
void
gnm_file_saver_unregister (GnmFileSaver *fs)
{
	GList *l;
	gchar const *id;

	g_return_if_fail (IS_GNM_FILE_SAVER (fs));

	l = g_list_find (file_saver_list, fs);
	g_return_if_fail (l != NULL);
	file_saver_list = g_list_remove_link (file_saver_list, l);
	g_list_free_1 (l);

	id = gnm_file_saver_get_id (fs);
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
			g_free (l->data);
			g_list_free_1 (l);
			break;
		}
	}

	g_object_unref (G_OBJECT (fs));
}

/**
 * gnm_file_saver_get_default:
 *
 * Returns file saver registered as default saver with the highest priority.
 * Reference count for the saver is NOT incremented.
 *
 * Return value: GnmFileSaver object or NULL if default saver is not
 *               available.
 */
GnmFileSaver *
gnm_file_saver_get_default (void)
{
	if (default_file_saver_list == NULL)
		return NULL;

	return ((DefaultFileSaver *) default_file_saver_list->data)->saver;
}

/**
 * gnm_file_saver_for_mime_type:
 * @mime_type: A mime type
 *
 * Returns a file saver that claims to save files with given mime type.
 *
 * Return value: GnmFileSaver object or NULL if no suitable file saver could
 *               be found.
 */
GnmFileSaver *
gnm_file_saver_for_mime_type (gchar const *mime_type)
{
	GList *l;

	for (l = file_saver_list; l != NULL; l = l->next) {
		if (!strcmp (gnm_file_saver_get_mime_type (l->data), mime_type)) {
			return (l->data);
		}
	}
	return (NULL);
}

/**
 * gnm_file_saver_for_file_name :
 * @file_name :
 *
 * Searches for file opener with given @filename, registered using
 * gnm_file_opener_register
 *
 * Return value: GnmFileOpener object or NULL if opener cannot be found.
 **/
GnmFileSaver *
gnm_file_saver_for_file_name (char const *file_name)
{
	GList *l;
	char const *extension = gsf_extension_pointer (file_name);

	for (l = file_saver_list; l != NULL; l = l->next)
		if (!strcmp (gnm_file_saver_get_extension (l->data), extension))
			return l->data;
	return NULL;
}

/**
 * gnm_file_opener_for_id:
 * @id          : File opener's ID
 *
 * Searches for file opener with given @id, registered using
 * gnm_file_opener_register
 *
 * Return value: GnmFileOpener object or NULL if opener cannot be found.
 */
GnmFileOpener *
gnm_file_opener_for_id (gchar const *id)
{
	g_return_val_if_fail (id != NULL, NULL);

	if (file_opener_id_hash == NULL)
		return NULL;
	return GNM_FILE_OPENER (g_hash_table_lookup (file_opener_id_hash, id));
}

/**
 * gnm_file_saver_for_id:
 * @id          : File saver's ID
 *
 * Searches for file saver with given @id, registered using
 * gnm_file_saver_register or register_file_opener_as_default.
 *
 * Return value: GnmFileSaver object or NULL if saver cannot be found.
 */
GnmFileSaver *
gnm_file_saver_for_id (gchar const *id)
{
	g_return_val_if_fail (id != NULL, NULL);

	if (file_saver_id_hash == NULL)
		return NULL;
	return GNM_FILE_SAVER (g_hash_table_lookup (file_saver_id_hash, id));
}

/**
 * get_file_savers:
 *
 * Returns the list of registered file savers (using gnm_file_saver_register or
 * gnm_file_saver_register_as_default).
 *
 * Return value: list of GnmFileSaver objects, which you shouldn't modify.
 */
GList *
get_file_savers (void)
{
	return file_saver_list;
}

/**
 * get_file_openers:
 *
 * Returns the list of registered file openers (using gnm_file_opener_register).
 *
 * Return value: list of GnmFileOpener objects, which you shouldn't modify.
 */
GList *
get_file_openers (void)
{
	return file_opener_list;
}
