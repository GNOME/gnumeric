/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_GUI_FILE_H_
# define _GNM_GUI_FILE_H_

#include "gui-gnumeric.h"

G_BEGIN_DECLS

gboolean gui_file_save_as   (WBCGtk *wbcg, WorkbookView *wbv);
gboolean gui_file_save      (WBCGtk *wbcg, WorkbookView *wbv);
void     gui_file_open      (WBCGtk *wbcg,
			     char const *default_format);
void     gui_wb_view_show   (WBCGtk *wbcg, WorkbookView *wbv);
gboolean gui_file_read	    (WBCGtk *wbcg, char const *file_name,
			     GOFileOpener const *optional_format,
			     gchar const *optional_encoding);

G_END_DECLS

#endif /* _GNM_GUI_FILE_H_ */
