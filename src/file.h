#ifndef GNUMERIC_FILE_H
#define GNUMERIC_FILE_H

#include "gnumeric.h"
#include <glib-object.h>
#include <gsf/gsf.h>

/*
 * File format levels. They are ordered. When we save a file, we
 * remember the name, but not if we already have a name at a higher level.
 * When created, workbooks are assigned a name at level FILE_FL_NEW.
 */
typedef enum {
	FILE_FL_NONE,            /* No name assigned, won't happen */
	FILE_FL_WRITE_ONLY,      /* PostScript etc, won't be remembered */
	FILE_FL_NEW,             /* Wb just created */
	FILE_FL_MANUAL,          /* Save gets punted to save as */
	FILE_FL_MANUAL_REMEMBER, /* Ditto, but remember in history */
	FILE_FL_AUTO,            /* Save will save to this filename */
	FILE_FL_LAST
} FileFormatLevel;

/*
 * File probe level tells file opener (its probe method to be exact), how
 * hard it should try to recognize the type of the file. File openers may
 * ignore this or support only some probe levels, but if specifies
 * "reccomened" behaviour.
 * Before opening any file we detect its type by calling probe for
 * every registered file opener (in order of priority) and passing
 * FILE_PROBE_FILE_NAME as probe level. If none of them recogizes the file,
 * we increase probe level and try again...
 */
typedef enum {
	FILE_PROBE_FILE_NAME,	/* Test only file name, don't read file contents */
	FILE_PROBE_CONTENT,	/* Read the whole file if it's necessary */
	FILE_PROBE_LAST
} FileProbeLevel;

/*
 * FileSaveScope specifies what information file saver can save in a file.
 * Many savers can save the whole workbook (with all sheets), but others
 * save only current sheet, usually because of file format limitations.
 */
typedef enum {
	FILE_SAVE_WORKBOOK,
	FILE_SAVE_SHEET,
	FILE_SAVE_LAST
} FileSaveScope;

/*
 * GnmFileOpener
 */

typedef struct _GnmFileOpenerClass GnmFileOpenerClass;

#define TYPE_GNM_FILE_OPENER             (gnm_file_opener_get_type ())
#define GNM_FILE_OPENER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GNM_FILE_OPENER, GnmFileOpener))
#define IS_GNM_FILE_OPENER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_GNM_FILE_OPENER))

typedef gboolean (*GnmFileOpenerProbeFunc) (GnmFileOpener const *fo,
					     GsfInput *input,
                                             FileProbeLevel pl);
typedef void     (*GnmFileOpenerOpenFunc) (GnmFileOpener const *fo,
                                            IOContext *io_context,
                                            WorkbookView *wbv,
                                            GsfInput *input);

GType gnm_file_opener_get_type (void);

GnmFileOpener *gnm_file_opener_new (const gchar *id,
                                      const gchar *description,
                                      GnmFileOpenerProbeFunc probe_func,
                                      GnmFileOpenerOpenFunc open_func);

gboolean     gnm_file_opener_probe (GnmFileOpener const *fo, GsfInput *input,
                                     FileProbeLevel pl);
void         gnm_file_opener_open (GnmFileOpener const *fo, IOContext *io_context,
                                    WorkbookView *wbv, GsfInput *input);
const gchar *gnm_file_opener_get_id (GnmFileOpener const *fo);
const gchar *gnm_file_opener_get_description (GnmFileOpener const *fo);

/*
 * GnmFileSaver
 */

typedef struct _GnmFileSaver GnmFileSaver;
typedef struct _GnmFileSaverClass GnmFileSaverClass;

#define TYPE_GNM_FILE_SAVER             (gnm_file_saver_get_type ())
#define GNM_FILE_SAVER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GNM_FILE_SAVER, GnmFileSaver))
#define IS_GNM_FILE_SAVER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_GNM_FILE_SAVER))

typedef void (*GnmFileSaverSaveFunc) (GnmFileSaver const *fs,
                                       IOContext *io_context,
                                       WorkbookView const *wbv,
                                       GsfOutput *output);
GType gnm_file_saver_get_type (void);

GnmFileSaver *gnm_file_saver_new (const gchar *id,
                                    const gchar *extension,
                                    const gchar *description,
                                    FileFormatLevel level,
                                    GnmFileSaverSaveFunc save_func);

void          gnm_file_saver_set_save_scope (GnmFileSaver *fs, FileSaveScope scope);
FileSaveScope gnm_file_saver_get_save_scope (GnmFileSaver *fs);

void         gnm_file_saver_save (GnmFileSaver const *fs, IOContext *io_context,
                                   WorkbookView const *wbv, GsfOutput *output);
void         gnm_file_saver_set_overwrite_files (GnmFileSaver *fs,
                                                  gboolean overwrite);
gboolean     gnm_file_saver_fix_file_name (GnmFileSaver const *fs,
                                            const gchar *file_name,
					    gchar **new_file_name);
const gchar *gnm_file_saver_get_id (GnmFileSaver const *fs);
const gchar *gnm_file_saver_get_extension (GnmFileSaver const *fs);
const gchar *gnm_file_saver_get_mime_type (GnmFileSaver const *fs);
const gchar *gnm_file_saver_get_description (GnmFileSaver const *fs);
FileFormatLevel gnm_file_saver_get_format_level (GnmFileSaver const *fs);

/*
 *
 */

void register_file_opener (GnmFileOpener *fo, gint priority);
void unregister_file_opener (GnmFileOpener *fo);
void register_file_saver (GnmFileSaver *fs);
void register_file_saver_as_default (GnmFileSaver *fs, gint priority);
void unregister_file_saver (GnmFileSaver *fs);

GnmFileSaver *get_default_file_saver (void);
GnmFileSaver *get_file_saver_for_mime_type (const gchar *mime_type);
GnmFileOpener *get_file_opener_by_id (const gchar *id);
GnmFileSaver *get_file_saver_by_id (const gchar *id);

GList *get_file_savers (void);
GList *get_file_openers (void);

#endif /* GNUMERIC_FILE_H */
