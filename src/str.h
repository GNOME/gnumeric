#ifndef GNUMERIC_STRING_H
#define GNUMERIC_STRING_H

typedef struct {
	int        ref_count;
	char       *str;
} String;

void    string_init           (void);

String *string_lookup         (const char *s);
String *string_get            (const char *s);
String *string_ref            (String *);
void    string_unref          (String *);
void    string_unref_ptr      (String **);

#endif /* GNUMERIC_STRING_H */
