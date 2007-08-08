#ifndef GNUMERIC_GUI_CLIPBOARD_H
#define GNUMERIC_GUI_CLIPBOARD_H

#include "gui-gnumeric.h"

void x_request_clipboard (WBCGtk *wbcg, GnmPasteTarget const *pt);
gboolean x_claim_clipboard (WBCGtk *wbcg);
void x_store_clipboard_if_needed (Workbook *wb);

#endif /* GNUMERIC_GUI_CLIPBOARD_H */
