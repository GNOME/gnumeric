#ifndef GNUMERIC_FILE_H
#define GNUMERIC_FILE_H

#include "gnumeric.h"
#include "workbook-control-gui.h"

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
	FILE_FL_AUTO		/* Save will save to this filename */
} FileFormatLevel;

typedef gboolean   (*FileFormatProbe) (const gchar *filename, gpointer user_data);
typedef gint       (*FileFormatOpen) (IOContext *context,
                                      WorkbookView *wb_view,
                                      const gchar *filename,
                                      gpointer user_data);
typedef gint       (*FileFormatSave) (IOContext *context,
                                      WorkbookView *wb_View,
                                      const gchar *filename,
                                      gpointer user_data);

#ifdef G_HAVE_GINT64
	typedef guint64 FileOpenerId;
	typedef guint64 FileSaverId;
#else
	typedef gulong FileOpenerId;
	typedef gulong FileSaverId;
#endif

#define FILE_OPENER_ID_INVAID  0
#define FILE_SAVER_ID_INVAID   0

typedef struct _FileOpener FileOpener;
typedef struct _FileSaver  FileSaver;

struct _FileOpener {
	FileOpenerId    opener_id;
	int             priority;
	char           *format_description;
	FileFormatProbe probe;
	FileFormatOpen  open;
	gpointer        user_data;
};

struct _FileSaver {
	FileSaverId      saver_id;
	char            *extension;
	char            *format_description;
	FileFormatLevel level;
	FileFormatSave  save;
	gpointer        user_data;
};

void file_format_unregister_open (FileOpenerId opener_id);
FileOpenerId file_format_register_open   (gint priority,
                                          const gchar *format_description,
                                          FileFormatProbe probe_fn,
                                          FileFormatOpen  open_fn,
                                          gpointer user_data);

void file_format_unregister_save (FileSaverId saver_id);
FileSaverId file_format_register_save   (gchar *extension,
                                         const gchar *format_description,
                                         FileFormatLevel level,
                                         FileFormatSave save_fn,
                                         gpointer user_data);
GList *file_format_get_savers (void);
GList *file_format_get_openers (void);
FileOpener *get_file_opener_by_id (FileOpenerId file_opener_id);
FileSaver  *get_file_saver_by_id (FileSaverId file_saver_id);

gboolean      workbook_save_as   (WorkbookControl *wbcg, WorkbookView *,
                                  const char *name, FileSaver *saver);
gboolean      workbook_save      (WorkbookControl *wbc, WorkbookView *);
WorkbookView *workbook_read      (WorkbookControl *context, const char *fname);
WorkbookView *workbook_try_read  (WorkbookControl *context, const char *fname);
int           workbook_load_from (WorkbookControl *context, WorkbookView *wbv,
                                  const char *fname);
WorkbookView *file_finish_load (WorkbookControl *wbc, WorkbookView *new_wbv);


#endif /* GNUMERIC_FILE_H */
