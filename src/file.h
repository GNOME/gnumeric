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
	FILE_PROBE_FILE_NAME,    /* Test only file name, don't read file contents */
	FILE_PROBE_CONTENT_FAST, /* Read only small parts of the file */
	FILE_PROBE_CONTENT_FULL, /* Read the whole file if it's necessary */
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
 * GnumFileOpener
 */

typedef struct _GnumFileOpenerClass GnumFileOpenerClass;

#define TYPE_GNUM_FILE_OPENER             (gnum_file_opener_get_type ())
#define GNUM_FILE_OPENER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GNUM_FILE_OPENER, GnumFileOpener))
#define IS_GNUM_FILE_OPENER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_GNUM_FILE_OPENER))

typedef gboolean (*GnumFileOpenerProbeFunc) (GnumFileOpener const *fo,
					     GsfInput *input,
                                             FileProbeLevel pl);
typedef void     (*GnumFileOpenerOpenFunc) (GnumFileOpener const *fo,
                                            IOContext *io_context,
                                            WorkbookView *wbv,
                                            GsfInput *input);

GType gnum_file_opener_get_type (void);

GnumFileOpener *gnum_file_opener_new (const gchar *id,
                                      const gchar *description,
                                      GnumFileOpenerProbeFunc probe_func,
                                      GnumFileOpenerOpenFunc open_func);

gboolean     gnum_file_opener_probe (GnumFileOpener const *fo, GsfInput *input,
                                     FileProbeLevel pl);
void         gnum_file_opener_open (GnumFileOpener const *fo, IOContext *io_context,
                                    WorkbookView *wbv, GsfInput *input);
const gchar *gnum_file_opener_get_id (GnumFileOpener const *fo);
const gchar *gnum_file_opener_get_description (GnumFileOpener const *fo);

/*
 * GnumFileSaver
 */

typedef struct _GnumFileSaver GnumFileSaver;
typedef struct _GnumFileSaverClass GnumFileSaverClass;

#define TYPE_GNUM_FILE_SAVER             (gnum_file_saver_get_type ())
#define GNUM_FILE_SAVER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GNUM_FILE_SAVER, GnumFileSaver))
#define IS_GNUM_FILE_SAVER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_GNUM_FILE_SAVER))

typedef void (*GnumFileSaverSaveFunc) (GnumFileSaver const *fs,
                                       IOContext *io_context,
                                       WorkbookView *wbv,
                                       const gchar *file_name);
GType gnum_file_saver_get_type (void);

GnumFileSaver *gnum_file_saver_new (const gchar *id,
                                    const gchar *extension,
                                    const gchar *description,
                                    FileFormatLevel level,
                                    GnumFileSaverSaveFunc save_func);

void          gnum_file_saver_set_save_scope (GnumFileSaver *fs, FileSaveScope scope);
FileSaveScope gnum_file_saver_get_save_scope (GnumFileSaver *fs);

void         gnum_file_saver_save (GnumFileSaver const *fs, IOContext *io_context,
                                   WorkbookView *wbv, const gchar *file_name);
void         gnum_file_saver_set_overwrite_files (GnumFileSaver *fs,
                                                  gboolean overwrite);
gboolean     gnum_file_saver_fix_file_name (GnumFileSaver const *fs,
                                            const gchar *file_name,
					    gchar **new_file_name);
const gchar *gnum_file_saver_get_id (GnumFileSaver const *fs);
const gchar *gnum_file_saver_get_extension (GnumFileSaver const *fs);
const gchar *gnum_file_saver_get_mime_type (GnumFileSaver const *fs);
const gchar *gnum_file_saver_get_description (GnumFileSaver const *fs);
FileFormatLevel gnum_file_saver_get_format_level (GnumFileSaver const *fs);

/*
 *
 */

void register_file_opener (GnumFileOpener *fo, gint priority);
void register_file_opener_as_importer (GnumFileOpener *fo);
void register_file_opener_as_importer_as_default (GnumFileOpener *fo, gint priority);
void unregister_file_opener (GnumFileOpener *fo);
void unregister_file_opener_as_importer (GnumFileOpener *fo);
void register_file_saver (GnumFileSaver *fs);
void register_file_saver_as_default (GnumFileSaver *fs, gint priority);
void unregister_file_saver (GnumFileSaver *fs);

GnumFileSaver *get_default_file_saver (void);
GnumFileSaver *get_file_saver_for_mime_type (const gchar *mime_type);
GnumFileOpener *get_file_opener_by_id (const gchar *id);
GnumFileSaver *get_file_saver_by_id (const gchar *id);
GnumFileOpener *get_default_file_importer (void);

GList *get_file_savers (void);
GList *get_file_openers (void);
GList *get_file_importers (void);

#endif /* GNUMERIC_FILE_H */
