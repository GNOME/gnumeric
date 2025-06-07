#ifndef _GNM_GUI_FILE_H_
# define _GNM_GUI_FILE_H_

#include <gnumeric-fwd.h>

G_BEGIN_DECLS

typedef enum {
	GNM_FILE_SAVE_AS_STYLE_SAVE,
	GNM_FILE_SAVE_AS_STYLE_EXPORT,
	GNM_FILE_SAVE_AS_STYLE_EXPORT_RANGE
} GnmFileSaveAsStyle;
typedef enum {
	GNM_FILE_OPEN_STYLE_OPEN,
	GNM_FILE_OPEN_STYLE_IMPORT,
} GnmFileOpenStyle;

gboolean gui_file_save_as   (WBCGtk *wbcg, WorkbookView *wbv,
			     GnmFileSaveAsStyle type,
			     char const *default_format,
			     gboolean from_save);
gboolean gui_file_save      (WBCGtk *wbcg, WorkbookView *wbv);
gboolean gui_file_export_repeat (WBCGtk *wbcg);
void     gui_file_open      (WBCGtk *wbcg, GnmFileOpenStyle type,
			     char const *default_format);
void     gui_wb_view_show   (WBCGtk *wbcg, WorkbookView *wbv);
WorkbookView *gui_file_read (WBCGtk *wbcg, char const *file_name,
			     GOFileOpener const *optional_format,
			     gchar const *optional_encoding);
gboolean gnm_gui_file_template  (WBCGtk *wbcg, char const *uri);

G_END_DECLS

#endif /* _GNM_GUI_FILE_H_ */
