#ifndef GNUMERIC_GUI_CLIPBOARD_H
#define GNUMERIC_GUI_CLIPBOARD_H

#include "gui-gnumeric.h"

void x_request_clipboard (WorkbookControlGUI *wbcg, PasteTarget const *pt);
gboolean x_claim_clipboard (WorkbookControlGUI *wbcg);

#endif /* GNUMERIC_GUI_CLIPBOARD_H */
