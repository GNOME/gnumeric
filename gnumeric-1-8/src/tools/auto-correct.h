#ifndef AUTO_CORRECT_H
#define AUTO_CORRECT_H

#include "gnumeric.h"

typedef enum {
	AC_INIT_CAPS,
	AC_FIRST_LETTER,
	AC_NAMES_OF_DAYS,
	AC_REPLACE,
	AC_MAX_FEATURE
} AutoCorrectFeature;

void	 autocorrect_store_config   (void);
gboolean autocorrect_get_feature    (AutoCorrectFeature feat);
void	 autocorrect_set_feature    (AutoCorrectFeature feat, gboolean val);
GSList  *autocorrect_get_exceptions (AutoCorrectFeature feat);
void	 autocorrect_set_exceptions (AutoCorrectFeature feat, GSList const *list);

char    *autocorrect_tool 	 (char const *input);

#endif /* AUTO_CORRECT_H */
