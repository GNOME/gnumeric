#ifndef GNUMERIC_STF_SEPARATED_H
#define GNUMERIC_STF_SEPARATED_H

#include <stdlib.h>
#include <errno.h>
#include <gnome.h>

#include "sheet.h"
#include "stf.h"

typedef enum {
	TST_TAB    = 1 << 0,
	TST_COLON  = 1 << 1,
	TST_COMMA  = 1 << 2,
	TST_SPACE  = 1 << 3,
	TST_CUSTOM = 1 << 4
} TextSeparator_t;

typedef struct {
	TextSeparator_t  separator;             /* Text separator(s) */
	char             custom;                /* Custom text separator */
	char             string;                /* String indicator */
	gboolean         duplicates;            /* See two text separator's as one? */
} SeparatedInfo_t;

gboolean    stf_separated_parse_sheet           (FileSource_t *src, SeparatedInfo_t *sepinfo);
gboolean    stf_separated_parse_sheet_partial   (FileSource_t *src, SeparatedInfo_t *sepinfo,
					         int fromline, int toline);
#endif



