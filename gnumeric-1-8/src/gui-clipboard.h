/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_GUI_CLIPBOARD_H_
# define _GNM_GUI_CLIPBOARD_H_

#include "gui-gnumeric.h"

G_BEGIN_DECLS

void x_request_clipboard (WBCGtk *wbcg, GnmPasteTarget const *pt);
gboolean x_claim_clipboard (WBCGtk *wbcg);
void x_store_clipboard_if_needed (Workbook *wb);

G_END_DECLS

#endif /* _GNM_GUI_CLIPBOARD_H_ */
