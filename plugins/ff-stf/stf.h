#ifndef GNUMERIC_STF_H
#define GNUMERIC_STF_H

#include "sheet.h"
#include "plugin.h"

typedef struct {
	int        fd;                     /* File descriptor */
	char const *filename;              /* Filename */
	
	char const *data, *cur;     /* Memory mapped file and current position */
	int        len;             /* Length of the file */
	
	int   line;                 /* Current line */
	int   lines;                /* Number of lines calculated from *cur */
	int   totallines;           /* Total number of lines (*data) */
	Sheet *sheet;               /* Target workbook sheet */

	int rowcount;               /* Number of rows in sheet */
	int colcount;               /* Number of columns in sheet */
} FileSource_t;

PluginInitResult init_plugin (CommandContext *context, PluginData *pd);

#endif /* GNUMERIC_STF_H */








