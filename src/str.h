#ifndef STRING_H
#define STRING_H

typedef struct {
	int        ref_count;
	char       *str;
} String;

void    string_init           (void);

String *string_lookup         (char *s);
String *string_get            (char *s);
String *string_ref            (String *);
void    string_unref          (String *);
void    string_unref_ptr      (String **);

#endif
