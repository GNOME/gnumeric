#ifndef GNUMERIC_SEARCH_H
#define GNUMERIC_SEARCH_H

#include "gnumeric.h"

typedef enum { SRE_fail = 0,
	       SRE_skip,
	       SRE_query,
	       SRE_error,
	       SRE_string } SearchReplaceError;

struct _SearchReplace {
	char *search_text;
	char *replace_text;

	gboolean is_regexp;
	gboolean ignore_case;
	gboolean query;

	gboolean replace_strings;
	gboolean replace_other_values;
	gboolean replace_expressions;
	gboolean replace_comments;

	SearchReplaceError error_behaviour;

};

SearchReplace *search_replace_new (void);
void search_replace_free (SearchReplace *sr);
SearchReplace *search_replace_copy (const SearchReplace *sr);

void search_replace (Workbook *wb, const SearchReplace *sr);

#endif
