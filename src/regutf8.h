#ifndef GNUMERIC_REGUTF8_H
#define GNUMERIC_REGUTF8_H

#include <sys/types.h>
#include <regex.h>

/* -------------------------------------------------------------------------- */

#ifndef REG_EPAREN
#define REG_EPAREN REG_BADPAT
#endif

#ifndef REG_EBRACE
#define REG_EBRACE REG_BADPAT
#endif

#ifndef REG_EESCAPE
#define REG_EESCAPE REG_BADPAT
#endif

#ifndef REG_NOERROR
#define REG_NOERROR 0
#endif

#ifndef REG_OK
#define REG_OK REG_NOERROR
#endif

/* -------------------------------------------------------------------------- */

#ifdef HAVE_UTF8_REGEXP

/* We have a usable library.  Use it.  */
#define gnumeric_regex_t regex_t
#define gnumeric_regcomp regcomp
#define gnumeric_regexec regexec
#define gnumeric_regerror regerror
#define gnumeric_regfree regfree

#else

/* Use our poor man's routines.  */

typedef struct {
	size_t re_nsub;

	/* Internal fields below.  */
	regex_t theregexp;
	gboolean casefold;
	gboolean nosub;

	size_t srcparcount, parcount;
	size_t *parens;
} gnumeric_regex_t;

int gnumeric_regcomp (gnumeric_regex_t       *preg, char const *pattern, int cflags);
int gnumeric_regexec (gnumeric_regex_t const *preg, char const *string,
		      size_t nmatch, regmatch_t pmatch[], int eflags);
size_t gnumeric_regerror (int errcode, gnumeric_regex_t const *preg,
			  char *errbuf, size_t errbuf_size);
void gnumeric_regfree (gnumeric_regex_t *preg);

#endif

int gnumeric_regcomp_XL (gnumeric_regex_t *preg, char const *pattern, int cflags);

#endif
