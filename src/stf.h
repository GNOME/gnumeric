#ifndef GNUMERIC_STF_H
#define GNUMERIC_STF_H

#include <gnumeric.h>
#include <goffice/app/goffice-app.h>

void stf_init (void);
void stf_text_to_columns (WorkbookControl *wbc, GOCmdContext *cc);

#endif /* GNUMERIC_STF_H */
