#ifndef _GNM_GUI_CLIPBOARD_H_
# define _GNM_GUI_CLIPBOARD_H_

#include <gnumeric-fwd.h>

G_BEGIN_DECLS

void gnm_x_request_clipboard (WBCGtk *wbcg, GnmPasteTarget const *pt);
void gnm_x_store_clipboard_if_needed (Workbook *wb);

gboolean gnm_x_claim_clipboard (GdkDisplay *display);
void gnm_x_disown_clipboard (void);

GBytes *gui_clipboard_test (const char *fmt);

void gui_clipboard_init (void);
void gui_clipboard_shutdown (void);

G_END_DECLS

#endif /* _GNM_GUI_CLIPBOARD_H_ */
