#ifndef GNUMERIC_FILE_H
#define GNUMERIC_FILE_H

#include "gnumeric.h"

typedef struct _FileOpener FileOpener;
typedef struct _FileSaver  FileSaver;

/*
 * File format levels. They are ordered. When we save a file, we
 * remember the name, but not if we already have a name at a higher level.
 * When created, workbooks are assigned a name at level FILE_FL_NEW.
 */
typedef enum {
	FILE_FL_NONE,	 	/* No name assigned, won't happen */
	FILE_FL_WRITE_ONLY,	/* Postscript etc, won't be remembered */
	FILE_FL_NEW,		/* Wb just created */
	FILE_FL_MANUAL,		/* Save gets punted to save as */
	FILE_FL_MANUAL_REMEMBER, /* Ditto, but remember in history */
	FILE_FL_AUTO		/* Save will save to this filename */
} FileFormatLevel;

typedef gboolean   (*FileFormatProbe)     (FileOpener const *fo,
                                           const gchar *file_name);
typedef gboolean   (*FileFormatOpen)      (FileOpener const *fo,
                                           IOContext *context,
                                           WorkbookView *wb_view,
                                           const gchar *file_name);
typedef gboolean   (*FileFormatSave)      (FileSaver const *fs,
                                           IOContext *context,
                                           WorkbookView *wb_view,
                                           const gchar *file_name);

const gchar *file_opener_get_format_description (FileOpener const *fo);
gboolean     file_opener_has_probe (FileOpener const *fo);
gboolean     file_opener_probe     (FileOpener const *fo, const gchar *file_name);
gboolean     file_opener_open      (FileOpener const *fo, IOContext *context,
                                    WorkbookView *wb_view, const gchar *file_name);
void         file_opener_set_user_data (FileOpener *fo, gpointer user_data);
gpointer     file_opener_get_user_data (FileOpener const *fo);

const gchar *file_saver_get_extension (FileSaver const *fs);
const gchar *file_saver_get_format_description (FileSaver const *fs);
gboolean     file_saver_save          (FileSaver const *fs, IOContext *context,
                                       WorkbookView *wb_view, const gchar *file_name);
void         file_saver_set_user_data (FileSaver *fs, gpointer user_data);
gpointer     file_saver_get_user_data (FileSaver const *fs);

FileOpener *file_format_register_open    (gint priority,
                                          const gchar *format_description,
                                          FileFormatProbe probe_fn,
                                          FileFormatOpen  open_fn);
void        file_format_unregister_open  (FileOpener *fo);

FileSaver  *file_format_register_save    (gchar *extension,
                                          const gchar *format_description,
                                          FileFormatLevel level,
                                          FileFormatSave save_fn);
void file_format_unregister_save         (FileSaver *fs);

GList      *file_format_get_savers (void);
GList      *file_format_get_openers (void);

gboolean      workbook_save_as   (WorkbookControl *wbcg, WorkbookView *,
                                  const char *name, FileSaver *saver);
gboolean      workbook_save      (WorkbookControl *wbc, WorkbookView *);
WorkbookView *workbook_read      (WorkbookControl *context, const char *fname);
WorkbookView *workbook_try_read  (WorkbookControl *context, const char *fname);
int           workbook_load_from (WorkbookControl *context, WorkbookView *wbv,
                                  const char *fname);
WorkbookView *file_finish_load (WorkbookControl *wbc, WorkbookView *new_wbv);


#endif /* GNUMERIC_FILE_H */
