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

typedef gboolean  (*FileFormatProbe)(char const *filename);
typedef int       (*FileFormatOpen) (IOContext *context,
				     WorkbookView *wb_view,
				     char const *filename);
typedef int       (*FileFormatSave) (IOContext *context,
				     WorkbookView *wb_View,
				     char const *filename);

typedef struct _FileOpener FileOpener;
typedef struct _FileSaver  FileSaver;

struct _FileOpener {
	int             priority;
	char           *format_description;
	FileFormatProbe probe;
	FileFormatOpen  open;
};

struct _FileSaver {
	char            *extension;
	char            *format_description;
	FileFormatLevel level;
	FileFormatSave  save;
};

void file_format_unregister_open (FileFormatProbe probe, FileFormatOpen open);
void file_format_register_open   (int             priority,
				  const char     *format_description,
				  FileFormatProbe probe_fn,
				  FileFormatOpen  open_fn);

void file_format_unregister_save (FileFormatSave save);
void file_format_register_save   (char           *extension,
				  const char     *format_description,
				  FileFormatLevel level,
				  FileFormatSave save_fn);
GList *file_format_get_savers (void);
GList *file_format_get_openers (void);

gboolean      workbook_save_as   (WorkbookControl *wbcg, WorkbookView *,
				  const char *name, FileSaver *saver);
gboolean      workbook_save      (WorkbookControl *wbc, WorkbookView *);
WorkbookView *workbook_read      (WorkbookControl *context, const char *fname);
WorkbookView *workbook_try_read  (WorkbookControl *context, const char *fname);
int           workbook_load_from (WorkbookControl *context, WorkbookView *wbv,
				  const char *fname);
WorkbookView *file_finish_load (WorkbookControl *wbc, WorkbookView *new_wbv);


#endif /* GNUMERIC_FILE_H */
