/**
 * ms-summary.h: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1999, 2000 Michael Meeks
 **/

#ifndef GNUMERIC_MS_SUMMARY_H
#define GNUMERIC_MS_SUMMARY_H

#include "summary.h"

extern void ms_summary_read  (MsOle *f, SummaryInfo *sin);

extern void ms_summary_write (MsOle *f, SummaryInfo *sin);

#endif
