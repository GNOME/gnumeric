#ifndef GNUMERIC_SEARCH_H
#define GNUMERIC_SEARCH_H

#include "gnumeric.h"

struct _SearchReplace {
	char *search_text;
	char *replace_text;
};

SearchReplace *search_replace_new (void);
void search_replace_free (SearchReplace *sr);
void search_replace (Workbook *wb, const SearchReplace *sr);

#endif
