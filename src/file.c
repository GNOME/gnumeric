/*
 * file.c: File loading and saving routines
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <libgnome/libgnome.h>
#include "gnumeric.h"
#include "xml-io.h"
#include "file.h"
#include "sheet.h"
#include "application.h"
#include "io-context.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"

struct _FileOpener {
	int              priority;
	char            *format_description;
	FileFormatProbe  probe_func;
	FileFormatOpen   open_func;
	gpointer         user_data;
};

struct _FileSaver {
	char            *extension;
	char            *format_description;
	FileFormatLevel  level;
	FileFormatSave   save_func;
	gpointer         user_data;
};

/* A GList of FileOpener structures */
static GList *gnumeric_file_savers = NULL;
static GList *gnumeric_file_openers = NULL;

static gint
file_priority_sort (gconstpointer a, gconstpointer b)
{
	const FileOpener *fa = (const FileOpener *)a;
	const FileOpener *fb = (const FileOpener *)b;

	return fb->priority - fa->priority;
}

const gchar *
file_opener_get_format_description (FileOpener const *fo)
{
	g_return_val_if_fail (fo != NULL, NULL);

	return fo->format_description;
}

gboolean
file_opener_has_probe (FileOpener const *fo)
{
	g_return_val_if_fail (fo != NULL, FALSE);

	return fo->probe_func != NULL;
}

gboolean
file_opener_probe (FileOpener const *fo, const gchar *file_name)
{
	gboolean result;

	g_return_val_if_fail (fo != NULL, FALSE);
	g_return_val_if_fail (file_name != NULL, FALSE);

	if (fo->probe_func == NULL) {
		return FALSE;
	}
	result = fo->probe_func (fo, file_name);

	return result;
}

gboolean
file_opener_open (FileOpener const *fo, IOContext *context,
                       WorkbookView *wb_view, const gchar *file_name)
{
	gboolean result;

	g_return_val_if_fail (fo != NULL, FALSE);
	g_return_val_if_fail (file_name != NULL, FALSE);

	result = fo->open_func (fo, context, wb_view, file_name);

	return result;
}

void
file_saver_set_user_data (FileSaver *fs, gpointer user_data)
{
	g_return_if_fail (fs != NULL);

	fs->user_data = user_data;
}

gpointer
file_saver_get_user_data (FileSaver const *fs)
{
	g_return_val_if_fail (fs != NULL, NULL);

	return fs->user_data;
}

const gchar *
file_saver_get_extension (FileSaver const *fs)
{
	g_return_val_if_fail (fs != NULL, NULL);

	return fs->extension;
}

const gchar *
file_saver_get_format_description (FileSaver const *fs)
{
	g_return_val_if_fail (fs != NULL, NULL);

	return fs->format_description;
}

gboolean
file_saver_save (FileSaver const *fs, IOContext *context,
                      WorkbookView *wb_view, const gchar *file_name)
{
	gboolean result;

	g_return_val_if_fail (fs != NULL, FALSE);

	result = fs->save_func (fs, context, wb_view, file_name);

	return result;
}

void
file_opener_set_user_data (FileOpener *fo, gpointer user_data)
{
	g_return_if_fail (fo != NULL);

	fo->user_data = user_data;
}

gpointer
file_opener_get_user_data (FileOpener const *fo)
{
	g_return_val_if_fail (fo != NULL, NULL);

	return fo->user_data;
}

/**
 * file_format_register_open:
 * @priority: The priority at which this file open handler is registered
 * @desc: a description of this file format
 * @probe_fn: A routine that would probe if the file is of a given type.
 * @open_fn: A routine that would load the code
 *
 * The priority is used to give it a higher precendence to a format.
 * The higher the priority, the sooner it will be tried, gnumeric registers
 * its XML-based format at priority 50.
 *
 * If the probe_fn is NULL, we consider it a format importer, so that
 * it gets only listed in the "Import..." menu, and it is not auto-probed
 * at file open time.
 */
FileOpener *
file_format_register_open (gint priority, const gchar *desc,
                           FileFormatProbe probe_fn, FileFormatOpen open_fn)
{
	FileOpener *fo;

	g_return_val_if_fail (open_fn != NULL, NULL);

	fo = g_new (FileOpener, 1);
	fo->priority = priority;
	fo->format_description = g_strdup (desc);
	fo->probe_func = probe_fn;
	fo->open_func  = open_fn;
	fo->user_data = NULL;

	gnumeric_file_openers = g_list_insert_sorted (gnumeric_file_openers, fo, file_priority_sort);

	return fo;
}

/**
 * file_format_unregister_open:
 * @fo: File opener
 *
 * This function is used to remove a registered format opener from gnumeric
 */
void
file_format_unregister_open (FileOpener *fo)
{
	gnumeric_file_openers = g_list_remove (gnumeric_file_openers, fo);
	if (fo->format_description) {
		g_free (fo->format_description);
	}
	g_free (fo);
}

/**
 * file_format_register_save:
 * @extension: An extension that is usually used by this format
 * @format_description: A description of this format
 * @level: The file format level
 * @save_fn: A function that should be used to save
 *
 * This routine registers a file format save routine with Gnumeric
 */
FileSaver *
file_format_register_save (char *extension, const char *format_description,
                           FileFormatLevel level, FileFormatSave save_fn)
{
	FileSaver *fs;

	g_return_val_if_fail (save_fn != NULL, NULL);

	fs = g_new (FileSaver, 1);
	fs->extension = extension;
	fs->format_description = g_strdup (format_description);
	fs->level = level;
	fs->save_func = save_fn;
	fs->user_data = NULL;

	gnumeric_file_savers = g_list_append (gnumeric_file_savers, fs);

	return fs;
}

/**
 * cb_unregister_save:
 * @wb: Workbook
 * @fs: Format saver which will be removed
 *
 * Set file format level to manual for workbooks which had this saver set.
 */
static void
cb_unregister_save (Workbook *wb, FileSaver *fs)
{
	if (wb->file_saver == fs) {
		wb->file_format_level = FILE_FL_MANUAL;
		wb->file_saver = NULL;
	}
}

/**
 * file_format_unregister_save:
 * @saver_id: Saver ID
 *
 * This function is used to remove a registered format saver from gnumeric
 */
void
file_format_unregister_save (FileSaver *fs)
{
	application_workbook_foreach ((WorkbookCallback) cb_unregister_save, fs);

	gnumeric_file_savers = g_list_remove (gnumeric_file_savers, fs);
	if (fs->format_description) {
		g_free (fs->format_description);
	}
	g_free (fs);
}

GList *
file_format_get_savers (void)
{
	return gnumeric_file_savers;
}

GList *
file_format_get_openers (void)
{
	return gnumeric_file_openers;
}

static gboolean
do_load_from (WorkbookControl *wbc, WorkbookView *wbv, const char *file_name)
{
	GList *l;

	for (l = gnumeric_file_openers; l; l = l->next) {
		FileOpener const * const fo = l->data;

		if (file_opener_probe (fo, file_name)) {
			gboolean success;
			IOContext *io_context;


			io_context = gnumeric_io_context_new (wbc);
			success = file_opener_open (fo, io_context, wbv, file_name);
			if (success) {
				Workbook *wb = wb_view_workbook (wbv);
				if (workbook_sheet_count (wb) > 0)
					workbook_set_dirty (wb, FALSE);
				else
					success = FALSE;
			}
			gnumeric_io_context_free (io_context);
			return success;
		}
	}
	return FALSE;
}

/**
 * workbook_load_from:
 * @wbc:  The calling context.
 * @wbv:  A workbook view to load into.
 * @file_name: gives the URI of the file.
 *
 * This function attempts to read the file into the supplied
 * workbook.
 *
 * Return value: success : TRUE
 *               failure : FALSE
 **/
gboolean
workbook_load_from (WorkbookControl *wbc, WorkbookView *wbv,
                    const gchar *file_name)
{
	gboolean success;

	success = do_load_from (wbc, wbv, file_name);
	if (!success) {
		gnumeric_error_read (COMMAND_CONTEXT (wbc), NULL);
	}

	return success;
}

/**
 * workbook_try_read:
 * @file_name: gives the URI of the file.
 *
 * This function attempts to read the file
 *
 * Return value: a fresh workbook and view loaded workbook on success
 *               or NULL on failure.
 **/
WorkbookView *
workbook_try_read (WorkbookControl *wbc, const char *file_name)
{
	WorkbookView *new_view;
	gboolean success;

	g_return_val_if_fail (file_name != NULL, NULL);

	new_view = workbook_view_new (NULL);
	success = do_load_from (wbc, new_view, file_name);
	if (!success) {
		Workbook *wb;

		gnumeric_error_read (COMMAND_CONTEXT (wbc), NULL);
		wb = wb_view_workbook (new_view);
		gtk_object_destroy (GTK_OBJECT (wb));
		return NULL;
	}

	return new_view;
}

WorkbookView *
file_finish_load (WorkbookControl *wbc, WorkbookView *new_wbv)
{
	if (new_wbv != NULL) {
		WorkbookControl *new_wbc;
		Workbook *old_wb = wb_control_workbook (wbc);

		/*
		 * If the current workbook is empty and untouched use its
		 * control to wrap the new book.
		 */
		if (workbook_is_pristine (old_wb)) {
			gtk_object_ref (GTK_OBJECT (wbc));
			workbook_unref (old_wb);
			workbook_control_set_view (wbc, new_wbv, NULL);
			workbook_control_sheets_init (wbc);
			new_wbc = wbc;
		} else
			new_wbc = wb_control_wrapper_new (wbc, new_wbv, NULL);

		workbook_recalc (wb_view_workbook (new_wbv));

		/* FIXME : This should not be needed */
		workbook_set_dirty (wb_view_workbook (new_wbv), FALSE);

		sheet_update (wb_view_cur_sheet (new_wbv));
	}

	return new_wbv;
}

/**
 * workbook_read:
 * @file_name: the file's URI
 *
 * This attempts to read a workbook, if the file doesn't
 * exist it will create a blank 1 sheet workbook, otherwise
 * it will flag a user error message.
 *
 * Return value: a pointer to a Workbook or NULL.
 **/
WorkbookView *
workbook_read (WorkbookControl *wbc, const char *file_name)
{
	WorkbookView *new_view = NULL;
	char *template;

	g_return_val_if_fail (file_name != NULL, NULL);

	if (g_file_exists (file_name)) {
		template = g_strdup_printf (_("Could not read file %s\n%%s"),
					    file_name);
		command_context_push_err_template (COMMAND_CONTEXT (wbc), template);
		new_view = workbook_try_read (wbc, file_name);
		command_context_pop_err_template (COMMAND_CONTEXT (wbc));
		g_free (template);
	} else {
		Workbook *new_wb = workbook_new_with_sheets (1);

		workbook_set_saveinfo (new_wb, file_name, FILE_FL_NEW,
		                       gnumeric_xml_get_saver ());
		new_view = workbook_view_new (new_wb);
	}

	return file_finish_load (wbc, new_view);
}

static FileSaver *
insure_saver (FileSaver *current)
{
	GList *l;

	if (current)
		return current;

	for (l = gnumeric_file_savers; l; l = l->next){
		return l->data;
	}
	g_assert_not_reached ();
	return NULL;
}

gboolean
workbook_save_as (WorkbookControl *wbc, WorkbookView *wb_view,
                  const char *name, FileSaver *fs)
{
	char *template;
	gboolean success = FALSE;
	Workbook *wb = wb_view_workbook (wb_view);
	IOContext *io_context;

	template = g_strdup_printf (_("Could not save to file %s\n%%s"), name);
	command_context_push_err_template (COMMAND_CONTEXT (wbc), template);

	fs = insure_saver (fs);
	if (!fs) {
		gnumeric_error_save (COMMAND_CONTEXT (wbc),
				     _("There are no file savers loaded"));
		return FALSE;
	}

	io_context = gnumeric_io_context_new (wbc);

	/* Files are expected to be in standard C format.  */
	if (file_saver_save (fs, io_context, wb_view, name)) {
		workbook_set_saveinfo (wb, name, fs->level, fs);
		workbook_set_dirty (wb, FALSE);
		success = TRUE;
	}

	command_context_pop_err_template (COMMAND_CONTEXT (wbc));
	g_free (template);

	gnumeric_io_context_free (io_context);
	return success;
}

gboolean
workbook_save (WorkbookControl *wbc, WorkbookView *wb_view)
{
	char *template;
	gboolean ret;
	FileSaver *fs;
	Workbook *wb = wb_view_workbook (wb_view);
	IOContext *io_context;

	g_return_val_if_fail (wb_view != NULL, FALSE);

	template = g_strdup_printf (_("Could not save to file %s\n%%s"),
				    wb->filename);
	command_context_push_err_template (COMMAND_CONTEXT (wbc), template);
	if (wb->file_saver == NULL) {
		fs = gnumeric_xml_get_saver ();
	} else {
		fs = wb->file_saver;
	}
	g_return_val_if_fail (fs != NULL, FALSE);

	io_context = gnumeric_io_context_new (wbc);
	ret = file_saver_save (fs, io_context, wb_view, wb->filename);
	if (ret) {
		workbook_set_dirty (wb, FALSE);
	}
	command_context_pop_err_template (COMMAND_CONTEXT (wbc));
	g_free (template);
	gnumeric_io_context_free (io_context);

	return ret;
}
