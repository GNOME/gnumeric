#ifndef GNUMERIC_GUI_VALIDATION_H
#define GNUMERIC_GUI_VALIDATION_H

#include "workbook-control-gui.h"
#include "mstyle.h"

char     *validation_mstyle_get_title     (const MStyle *mstyle);
char     *validation_mstyle_get_msg       (const MStyle *mstyle);
char     *validation_mstyle_get_msg_subst (const MStyle *mstyle);
void      validation_mstyle_set_title_msg (MStyle *mstyle, const char *title,
					   const char *msg);

gboolean  validation_get_accept           (GtkWindow *parent, const MStyle *mstyle);

#endif /* GNUMERIC_GUI_VALIDATION_H */
