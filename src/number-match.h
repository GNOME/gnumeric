#ifndef GNUMERIC_NUMBER_MATCH
#define GNUMERIC_NUMBER_MATCH

int      format_match_define (char *format);
void     format_match_init   (void);
void     format_match_finish (void);
gboolean format_match        (char *s, double *v, char **format);

#endif
