#ifndef GNUMERIC_FILE_H
#define GNUMERIC_FILE_H

#include <gtk/gtktypeutils.h>
#include "gnumeric.h"

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
 * GnumFileOpener
 */

typedef struct _GnumFileOpener GnumFileOpener;
typedef struct _GnumFileOpenerClass GnumFileOpenerClass;

#define TYPE_GNUM_FILE_OPENER             (gnum_file_opener_get_type ())
#define GNUM_FILE_OPENER(obj)             (GTK_CHECK_CAST ((obj), TYPE_GNUM_FILE_OPENER, GnumFileOpener))
#define GNUM_FILE_OPENER_CLASS(klass)     (GTK_CHECK_CLASS_CAST ((klass), TYPE_GNUM_FILE_OPENER, GnumFileOpenerClass))
#define IS_GNUM_FILE_OPENER(obj)          (GTK_CHECK_TYPE ((obj), TYPE_GNUM_FILE_OPENER))
#define IS_GNUM_FILE_OPENER_CLASS(klass)  (GTK_CHECK_CLASS_TYPE ((klass), TYPE_GNUM_FILE_OPENER))

typedef gboolean (*GnumFileOpenerProbeFunc) (GnumFileOpener const *fo,
                                             const gchar *file_name);
typedef void     (*GnumFileOpenerOpenFunc) (GnumFileOpener const *fo,
                                            IOContext *io_context,
                                            WorkbookView *wbv,
                                            const gchar *file_name);

GtkType gnum_file_opener_get_type (void);

GnumFileOpener *gnum_file_opener_new (const gchar *id,
                                      const gchar *description,
                                      GnumFileOpenerProbeFunc probe_func,
                                      GnumFileOpenerOpenFunc open_func);

gboolean     gnum_file_opener_probe (GnumFileOpener const *fo, const gchar *file_name);
void         gnum_file_opener_open (GnumFileOpener const *fo, IOContext *io_context,
                                    WorkbookView *wbv, const gchar *file_name);
const gchar *gnum_file_opener_get_id (GnumFileOpener const *fo);
const gchar *gnum_file_opener_get_description (GnumFileOpener const *fo);

/*
 * GnumFileSaver
 */

typedef struct _GnumFileSaver GnumFileSaver;
typedef struct _GnumFileSaverClass GnumFileSaverClass;

#define TYPE_GNUM_FILE_SAVER             (gnum_file_saver_get_type ())
#define GNUM_FILE_SAVER(obj)             (GTK_CHECK_CAST ((obj), TYPE_GNUM_FILE_SAVER, GnumFileSaver))
#define GNUM_FILE_SAVER_CLASS(klass)     (GTK_CHECK_CLASS_CAST ((klass), TYPE_GNUM_FILE_SAVER, GnumFileSaverClass))
#define IS_GNUM_FILE_SAVER(obj)          (GTK_CHECK_TYPE ((obj), TYPE_GNUM_FILE_SAVER))
#define IS_GNUM_FILE_SAVER_CLASS(klass)  (GTK_CHECK_CLASS_TYPE ((klass), TYPE_GNUM_FILE_SAVER))

typedef void (*GnumFileSaverSaveFunc) (GnumFileSaver const *fs,
                                       IOContext *io_context,
                                       WorkbookView *wbv,
                                       const gchar *file_name);

GtkType gnum_file_saver_get_type (void);

GnumFileSaver *gnum_file_saver_new (const gchar *id,
                                    const gchar *extension,
                                    const gchar *description,
                                    FileFormatLevel level,
                                    GnumFileSaverSaveFunc save_func);

void         gnum_file_saver_save (GnumFileSaver const *fs, IOContext *io_context,
                                   WorkbookView *wbv, const gchar *file_name);
gchar       *gnum_file_saver_fix_file_name (GnumFileSaver const *fs,
                                            const gchar *file_name);
const gchar *gnum_file_saver_get_id (GnumFileSaver const *fs);
const gchar *gnum_file_saver_get_extension (GnumFileSaver const *fs);
const gchar *gnum_file_saver_get_description (GnumFileSaver const *fs);
FileFormatLevel gnum_file_saver_get_format_level (GnumFileSaver const *fs);

/*
 *
 */

void register_file_opener (GnumFileOpener *fo, gint priority);
void register_file_opener_as_importer (GnumFileOpener *fo);
void unregister_file_opener (GnumFileOpener *fo);
void unregister_file_opener_as_importer (GnumFileOpener *fo);
void register_file_saver (GnumFileSaver *fs);
void register_file_saver_as_default (GnumFileSaver *fs, gint priority);
void unregister_file_saver (GnumFileSaver *fs);

GnumFileSaver *get_default_file_saver (void);
GnumFileOpener *get_file_opener_by_id (const gchar *id);
GnumFileSaver *get_file_saver_by_id (const gchar *id);

GList *get_file_savers (void);
GList *get_file_openers (void);
GList *get_file_importers (void);

#endif /* GNUMERIC_FILE_H */
