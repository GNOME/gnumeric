/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_GUI_FILE_H_
# define _GNM_GUI_FILE_H_

#include "gui-gnumeric.h"

G_BEGIN_DECLS

typedef enum {
	FILE_SAVE_AS_SAVE,
	FILE_SAVE_AS_EXPORT,
	FILE_SAVE_AS_EXPORT_RANGE
} file_save_as_t;
typedef enum {
	FILE_OPEN_OPEN,
	FILE_OPEN_IMPORT,
} file_open_t;

gboolean gui_file_save_as   (WBCGtk *wbcg, WorkbookView *wbv,
			     file_save_as_t type,
			     char const *default_format);
gboolean gui_file_save      (WBCGtk *wbcg, WorkbookView *wbv);
void     gui_file_open      (WBCGtk *wbcg, file_open_t type,
			     char const *default_format);
void     gui_wb_view_show   (WBCGtk *wbcg, WorkbookView *wbv);
gboolean gui_file_read	    (WBCGtk *wbcg, char const *file_name,
			     GOFileOpener const *optional_format,
			     gchar const *optional_encoding);
gboolean gui_file_template  (WBCGtk *wbcg, char const *uri);

G_END_DECLS

#endif /* _GNM_GUI_FILE_H_ */
