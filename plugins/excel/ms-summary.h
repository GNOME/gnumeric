/**
 * ms-summary.h: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * (C) 1999, 2000 Michael Meeks
 **/

#include "summary.h"

extern void ms_summary_read  (MsOle *f, SummaryInfo *sin);

extern void ms_summary_write (MsOle *f, SummaryInfo *sin);
