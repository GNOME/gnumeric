/*
 * file.c: File loading and saving routines
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include "gnumeric.h"
#include "xml-io.h"
#include "file.h"
#include "sheet.h"
#include "application.h"
#include "io-context-priv.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"

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

/**
 * file_format_register_open:
 * @priority: The priority at which this file open handler is registered
 * @desc: a description of this file format
 * @probe_fn: A routine that would probe if the file is of a given type.
 * @open_fn: A routine that would load the code
 * @user_data: A pointer to user data
 *
 * The priority is used to give it a higher precendence to a format.
 * The higher the priority, the sooner it will be tried, gnumeric registers
 * its XML-based format at priority 50.
 *
 * If the probe_fn is NULL, we consider it a format importer, so that
 * it gets only listed in the "Import..." menu, and it is not auto-probed
 * at file open time.
 */
FileOpenerId
file_format_register_open (int priority, const char *desc,
                           FileFormatProbe probe_fn, FileFormatOpen open_fn,
                           gpointer user_data)
{
	static FileOpenerId last_opener_id = 0;
	FileOpener *fo = g_new (FileOpener, 1);

	g_return_val_if_fail (open_fn != NULL, FILE_OPENER_ID_INVAID);

	last_opener_id++;
	fo->priority = priority;
	fo->format_description = desc ? g_strdup (desc) : NULL;
	fo->probe = probe_fn;
	fo->open  = open_fn;
	fo->user_data = user_data;
	fo->opener_id = last_opener_id;

	gnumeric_file_openers = g_list_insert_sorted (gnumeric_file_openers, fo, file_priority_sort);

	return fo->opener_id;
}

/**
 * file_format_unregister_open:
 * @opener_id: Opener ID
 *
 * This function is used to remove a registered format opener from gnumeric
 */
void
file_format_unregister_open (FileOpenerId opener_id)
{
	GList *l;

	for (l = gnumeric_file_openers; l; l = l->next){
		FileOpener *fo = (FileOpener *) l->data;

		if (fo->opener_id == opener_id) {
			gnumeric_file_openers = g_list_remove_link (gnumeric_file_openers, l);
			g_list_free_1 (l);
			if (fo->format_description)
				g_free (fo->format_description);
			g_free (fo);
			return;
		}
	}
}

/**
 * file_format_register_save:
 * @extension: An extension that is usually used by this format
 * @format_description: A description of this format
 * @level: The file format level
 * @save_fn: A function that should be used to save
 * @user_data: A pointer to user data
 *
 * This routine registers a file format save routine with Gnumeric
 */
FileOpenerId
file_format_register_save (char *extension, const char *format_description,
                           FileFormatLevel level, FileFormatSave save_fn,
                           gpointer user_data)
{
	static FileSaverId last_saver_id = 0;
	FileSaver *fs = g_new (FileSaver, 1);

	g_return_val_if_fail (save_fn != NULL, FILE_OPENER_ID_INVAID);

	last_saver_id++;
	fs->extension = extension;
	fs->format_description =
		format_description ? g_strdup (format_description) : NULL;
	fs->level = level;
	fs->save  = save_fn;
	fs->user_data = user_data;
	fs->saver_id = last_saver_id;

	gnumeric_file_savers = g_list_append (gnumeric_file_savers, fs);

	return fs->saver_id;
}

/**
 * cb_unregister_save:
 * @wb:   Workbook
 * @save: ID of the format saver which will be removed
 *
 * Set file format level to manual for workbooks which had this saver set.
 */
static void
cb_unregister_save (Workbook *wb, FileSaverId *file_saver_id)
{
	if (wb->file_saver_id == *file_saver_id) {
		wb->file_format_level = FILE_FL_MANUAL;
		wb->file_saver_id = FILE_SAVER_ID_INVAID;
	}
}

/**
 * file_format_unregister_save:
 * @saver_id: Saver ID
 *
 * This function is used to remove a registered format saver from gnumeric
 */
void
file_format_unregister_save (FileSaverId file_saver_id)
{
	GList *l;

	application_workbook_foreach ((WorkbookCallback) cb_unregister_save, &file_saver_id);

	for (l = gnumeric_file_savers; l; l = l->next){
		FileSaver *fs = (FileSaver *) l->data;

		if (fs->saver_id == file_saver_id) {
			gnumeric_file_savers = g_list_remove_link (gnumeric_file_savers, l);
			g_list_free_1 (l);
			if (fs->format_description)
				g_free (fs->format_description);
			g_free (fs);
			break;
		}
	}
}

GList *
file_format_get_savers ()
{
	return gnumeric_file_savers;
}

GList *
file_format_get_openers ()
{
	return gnumeric_file_openers;
}

FileSaver *
get_file_saver_by_id (FileSaverId file_saver_id)
{
	FileSaver *found_file_saver = NULL;
	GList *l;

	for (l = gnumeric_file_savers; l != NULL; l = l->next) {
		FileSaver *fs;

		fs = (FileSaver *) l->data;
		if (fs->saver_id == file_saver_id) {
			found_file_saver = fs;
			break;
		}
	}

	return found_file_saver;
}

FileOpener *
get_file_opener_by_id (FileOpenerId file_opener_id)
{
	FileOpener *found_file_opener = NULL;
	GList *l;

	for (l = gnumeric_file_openers; l != NULL; l = l->next) {
		FileOpener *fo;

		fo = (FileOpener *) l->data;
		if (fo->opener_id == file_opener_id) {
			found_file_opener = fo;
			break;
		}
	}

	return found_file_opener;
}

static int
do_load_from (WorkbookControl *wbc, WorkbookView *wbv,
	      const char *filename)
{
	GList *l;

	for (l = gnumeric_file_openers; l; l = l->next) {
		FileOpener const * const fo = l->data;

		if (fo->probe != NULL && (*fo->probe) (filename, fo->user_data)) {
			int result;

			/* FIXME : This is a placeholder */
			IOContext io_context;
			io_context.impl = wbc;

			result = (*fo->open) (&io_context, wbv, filename, fo->user_data);
			if (result == 0)
				workbook_set_dirty (wb_view_workbook (wbv), FALSE);
			return result;
		}
	}
	return -1;
}

/**
 * workbook_load_from:
 * @wbc:  The calling context.
 * @wbv:  A workbook view to load into.
 * @filename: gives the URI of the file.
 *
 * This function attempts to read the file into the supplied
 * workbook.
 *
 * Return value: success : 0
 *               failure : -1
 **/
int
workbook_load_from (WorkbookControl *wbc, WorkbookView *wbv,
		    const char *filename)
{
	int ret = do_load_from (wbc, wbv, filename);
	if (ret == -1)
		gnumeric_error_read (COMMAND_CONTEXT (wbc), NULL);
	return ret;
}

/**
 * workbook_try_read:
 * @filename: gives the URI of the file.
 *
 * This function attempts to read the file
 *
 * Return value: a fresh workbook and view loaded workbook on success
 *		or NULL on failure.
 **/
WorkbookView *
workbook_try_read (WorkbookControl *wbc, const char *filename)
{
	WorkbookView *new_view;
	int result;

	g_return_val_if_fail (filename != NULL, NULL);

	new_view = workbook_view_new (NULL);
	result = do_load_from (wbc, new_view, filename);
	if (result != 0) {
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

		/*
		 * render and calc size of unrendered cells, then calc spans
		 * for everything
		 */
		workbook_calc_spans (wb_view_workbook (new_wbv),
				     SPANCALC_RENDER|SPANCALC_RESIZE);

		/* FIXME : This should not be needed */
		workbook_set_dirty (wb_view_workbook (new_wbv), FALSE);

		sheet_update (wb_view_cur_sheet (new_wbv));
	}

	return new_wbv;
}

/**
 * workbook_read:
 * @filename: the file's URI
 *
 * This attempts to read a workbook, if the file doesn't
 * exist it will create a blank 1 sheet workbook, otherwise
 * it will flag a user error message.
 *
 * Return value: a pointer to a Workbook or NULL.
 **/
WorkbookView *
workbook_read (WorkbookControl *wbc, const char *filename)
{
	WorkbookView *new_view = NULL;
	char *template;

	g_return_val_if_fail (filename != NULL, NULL);

	if (g_file_exists (filename)) {
		template = g_strdup_printf (_("Could not read file %s\n%%s"),
					    filename);
		command_context_push_err_template (COMMAND_CONTEXT (wbc), template);
		new_view = workbook_try_read (wbc, filename);
		command_context_pop_err_template (COMMAND_CONTEXT (wbc));
		g_free (template);
	} else {
		Workbook *new_wb = workbook_new_with_sheets (1);

		workbook_set_saveinfo (new_wb, filename, FILE_FL_NEW,
		                       gnumeric_xml_get_saver_id ());
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
		  const char *name, FileSaver *saver)
{
	char *template;
	gboolean success = FALSE;
	Workbook *wb = wb_view_workbook (wb_view);

	/* FIXME : This is a placeholder */
	IOContext io_context;
	io_context.impl = wbc;

	template = g_strdup_printf (_("Could not save to file %s\n%%s"), name);
	command_context_push_err_template (COMMAND_CONTEXT (wbc), template);

	saver = insure_saver (saver);
	if (!saver) {
		gnumeric_error_save (COMMAND_CONTEXT (wbc),
				     _("There are no file savers loaded"));
		return FALSE;
	}

	/* Files are expected to be in standard C format.  */
	if (saver->save (&io_context, wb_view, name, saver->user_data) == 0) {
		workbook_set_saveinfo (wb, name, saver->level,
		                       saver->saver_id);
		workbook_set_dirty (wb, FALSE);
		success = TRUE;
	}

	command_context_pop_err_template (COMMAND_CONTEXT (wbc));
	g_free (template);

	return success;
}

gboolean
workbook_save (WorkbookControl *wbc, WorkbookView *wb_view)
{
	char *template;
	gboolean ret;
	FileSaver *file_saver;
	Workbook *wb = wb_view_workbook (wb_view);

	/* FIXME : This is a placeholder */
	IOContext io_context;
	io_context.impl = wbc;

	g_return_val_if_fail (wb_view != NULL, FALSE);

	template = g_strdup_printf (_("Could not save to file %s\n%%s"),
				    wb->filename);
	command_context_push_err_template (COMMAND_CONTEXT (wbc), template);
	if (wb->file_saver_id != FILE_SAVER_ID_INVAID) {
		file_saver = get_file_saver_by_id (wb->file_saver_id);
	} else {
		file_saver = get_file_saver_by_id (gnumeric_xml_get_saver_id ());
	}
	g_return_val_if_fail (file_saver != NULL, FALSE);
	ret = (file_saver->save (&io_context, wb_view, wb->filename, file_saver->user_data) == 0);
	if (ret) {
		workbook_set_dirty (wb, FALSE);
	}
	command_context_pop_err_template (COMMAND_CONTEXT (wbc));
	g_free (template);

	return ret;
}
