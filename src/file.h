#ifndef GNUMERIC_FILE_H
#define GNUMERIC_FILE_H

#include "sheet.h"

typedef gboolean  (*FileFormatProbe)(const char *filename);
typedef char *    (*FileFormatOpen) (Workbook *wb, char const * const filename);
typedef int       (*FileFormatSave) (Workbook *wb, char const * const filename);

typedef struct _FileOpener FileOpener;
typedef struct _FileSaver  FileSaver;

void file_format_register_open   (int             priority,
				  const char     *format_description,
				  FileFormatProbe probe_fn,
				  FileFormatOpen  open_fn);
void file_format_unregister_open (FileFormatProbe probe, FileFormatOpen open);

void file_format_register_save   (char           *extension,
				  const char     *format_description,
				  FileFormatSave save_fn);
void file_format_unregister_save (FileFormatSave save);

Workbook *workbook_import        (Workbook *parent_dlg, const char *filename);
char *    workbook_load_from     (Workbook *wb, const char *filename);

#endif /* GNUMERIC_FILE_H */
