#ifndef GNUMERIC_GUI_VALIDATION_H
#define GNUMERIC_GUI_VALIDATION_H

#include "gui-gnumeric.h"

int validation_get_accept (Validation const *v,
			   char const *title, char const *msg,
			   WorkbookControlGUI *wbcg);

#endif /* GNUMERIC_GUI_VALIDATION_H */
