/**
 * ms-summary.h: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/

#include "summary.h"

extern void ms_summary_read  (MsOle *f, SummaryInfo *sin);

extern void ms_summary_write (MsOle *f, SummaryInfo *sin);
