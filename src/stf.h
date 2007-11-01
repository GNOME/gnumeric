#ifndef GNUMERIC_STF_H
#define GNUMERIC_STF_H

#include <gnumeric.h>

void stf_init (void);
void stf_shutdown (void);
void stf_text_to_columns (WorkbookControl *wbc, GOCmdContext *cc);

#endif /* GNUMERIC_STF_H */
