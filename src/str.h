#ifndef GNUMERIC_STRING_H
#define GNUMERIC_STRING_H

#include "gnumeric.h"

struct _String {
	int        ref_count;
	char       *str;
};

void    string_init           (void);
void    string_shutdown       (void);

String *string_lookup         (const char *s);
String *string_get            (const char *s);
String *string_get_nocopy     (char *s);
String *string_ref            (String *);
void    string_unref          (String *);

#endif /* GNUMERIC_STRING_H */
