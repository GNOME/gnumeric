#ifndef GNUMERIC_FILE_H
#define GNUMERIC_FILE_H

#include "gnumeric.h"

/*
 * File format levels. They are ordered. When we save a file, we
 * remember the name, but not if we already have a name at a higher level.
 * When created, workbooks are assigned a name at level FILE_FL_NEW.
 */
typedef enum {
	FILE_FL_NONE,	 /* No name assigned, won't happen */
	FILE_FL_WRITE_ONLY, /* Postscript etc, won't be remembered */
	FILE_FL_NEW,	 /* Wb just created */
	FILE_FL_MANUAL, /* Save gets punted to save as */
	FILE_FL_AUTO /* Save will save to this filename */
} FileFormatLevel;

typedef gboolean  (*FileFormatProbe)(const char *filename);
typedef int       (*FileFormatOpen) (CommandContext *context,
				     Workbook *wb,
				     char const * const filename);
typedef int       (*FileFormatSave) (CommandContext *context,
				     Workbook *wb,
				     char const * const filename);

typedef struct _FileOpener FileOpener;
typedef struct _FileSaver  FileSaver;

void file_format_register_open   (int             priority,
				  const char     *format_description,
				  FileFormatProbe probe_fn,
				  FileFormatOpen  open_fn);
void file_format_unregister_open (FileFormatProbe probe, FileFormatOpen open);

void file_format_register_save   (char           *extension,
				  const char     *format_description,
				  FileFormatLevel level,
				  FileFormatSave save_fn);
void file_format_unregister_save (FileFormatSave save);

Workbook *workbook_import        (CommandContext *context,
				  Workbook *parent_dlg, const char *filename);
int       workbook_load_from     (CommandContext *context, Workbook *wb,
				  const char *filename);

#endif /* GNUMERIC_FILE_H */
