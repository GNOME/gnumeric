#ifndef GNUMERIC_STF_FIXED_H
#define GNUMERIC_STF_FIXED_H

#include <stdlib.h>
#include <errno.h>
#include <gnome.h>

#include "sheet.h"
#include "stf.h"

typedef struct {
	GArray *splitpos;       /* Positions where text will be split vertically */
	int splitposcnt;        /* Number of positions contained withit Splitpos */

	/* Used internally only */
	int linepos;            /* Position on line */         
	int rulepos;            /* Position in splitpos */
} ParseFixedInfo_t;

gboolean     stf_fixed_parse_sheet           (FileSource_t *src, ParseFixedInfo_t *fixinfo);
gboolean     stf_fixed_parse_sheet_partial   (FileSource_t *src, ParseFixedInfo_t *fixinfo,
					      int fromline, int toline);

#endif /* GNUMERIC_STF_FIXED_H */
