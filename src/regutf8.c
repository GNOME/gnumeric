/*
 * regutf8.c:  A poor man's UTF-8 regexp routines.
 *
 * We should test system libraris for UTF-8 handling...
 *
 * Author:
 *   Morten Welinder (terra@diku.dk)
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <regutf8.h>

#ifndef HAVE_UTF8_REGEXP

#ifdef TEST_REGUTF8
#include <stdio.h>
#endif


#define ISASCII(c) ((unsigned int)(c) <= 0x80)
#define REPEATCHAR(extp,c) \
  ((c) == '*' || ((extp) && ((c) == '+' || (c) == '{' || (c) == '?')))

/* Match a single UTF-8 encoded character.  Needs to be ()-free.  */
#define UTF8DOT "[\x01-\x7f\xc0-\xfd][\x80-\xbf]*"

/* -------------------------------------------------------------------------- */

#define MP_SRCEXT 1
#define MP_DSTEXT 2
#define MP_SUBEXP 4

static int
make_bracket (gnumeric_regex_t *preg, GString *dst, char **pp, int mpflags)
{
	const char *p = *pp;
	gboolean seen_character = FALSE;
	gboolean seen_not = FALSE;
	gboolean seen_ascii = FALSE;
	GString *ascii_part = g_string_new ("[");
	GString *multibyte_part = g_string_new (NULL);
	int res = REG_EBRACK;  /* For missing closing bracket.  */

	while (*p) {
		gunichar c = g_utf8_get_char (p);
		p = g_utf8_next_char (p);

		if (c == ']' && seen_character) {
			/* The end...  */

			g_string_append_c (ascii_part, ']');

			if (*multibyte_part->str) {
				g_string_append_c (dst, '(');
				if (seen_ascii) {
					g_string_append (dst, ascii_part->str);
					g_string_append_c (dst, '|');
				}
				g_string_append (dst, multibyte_part->str);
				g_string_append_c (dst, ')');
				preg->parcount++;
			} else {
				g_string_append (dst, ascii_part->str);
			}

			*pp = (char *)p;
			res = REG_OK;
			break;
		}

		if (c == '^' && !seen_character && !seen_not) {
			seen_not = TRUE;
			g_string_append_c (ascii_part, '^');
			continue;
		}

		seen_character = TRUE;

		if (seen_not && !ISASCII (c)) {
			/* Sorry, we cannot handle this.  */
			res = REG_BADPAT;
			break;
		}
		if (*p == '-') {
			gunichar c2;

			p++;
			c2 = g_utf8_get_char (p);
			if (c2 == 0) break;

			p = g_utf8_next_char (p);
			if (c2 == ']') {
				res = REG_ERANGE;
				break;
			}
			if (!ISASCII (c) || !ISASCII (c2)) {
				res = REG_ERANGE;
				break;
			}
			g_string_append_c (ascii_part, c);
			g_string_append_c (ascii_part, '-');
			g_string_append_c (ascii_part, c2);
			seen_ascii = TRUE;
		} else {
			if (ISASCII (c)) {
				g_string_append_c (ascii_part, c);
				seen_ascii = TRUE;
			} else {
				if ((mpflags & MP_DSTEXT) == 0) {
					/* We cannot handle this in plain mode.  */
					res = REG_BADPAT;
					break;
				}

				if (*multibyte_part->str)
					g_string_append_c (multibyte_part, '|');
				g_string_append_unichar (multibyte_part, c);
			}
		}
	}

	g_string_free (ascii_part, TRUE);
	g_string_free (multibyte_part, TRUE);
	return res;
}

static int
make_pattern (gnumeric_regex_t *preg, GString *dst, char **pp, int mpflags)
{
	const char *p = *pp;
	int res;
	gboolean srcext = (mpflags & MP_SRCEXT) != 0;
	gboolean dstext = (mpflags & MP_DSTEXT) != 0;

	while (*p) {
		gunichar c = g_utf8_get_char (p);

		if (dstext && !srcext) {
			if (c == '+' || c == '|' || c == '?' || c == '{') {
				/*
				 * We generate an extended regexp, so these
				 * characters have to be escaped.
				 */
				g_string_append_c (dst, '\\');
				g_string_append_c (dst, c);
				p++;
				continue;
			}
		}

		if ((mpflags & MP_SUBEXP) &&
		    (srcext ? (c == ')') : (c == '\\' && p[1] == ')'))) {
			if (c == '\\') p++;
			break;
		}

		if (srcext ? (c == '(') : (c == '\\' && p[1] == '(')) {
			if (c == '\\') p++;
			g_string_append (dst, dstext ? "(" : "\\(");
			p++;
			preg->srcparcount++;
			preg->parcount++;
			preg->parens = g_renew (size_t, preg->parens,
						preg->srcparcount + 1);
			preg->parens[preg->srcparcount] = preg->parcount;
			res = make_pattern (preg, dst, (char **)&p,
					    mpflags | MP_SUBEXP);
			if (res != REG_OK)
				return res;
			g_string_append (dst, dstext ? ")" : "\\)");
			continue;
		}

		if (REPEATCHAR (srcext, c)) {
			char *patend = dst->str + dst->len;

			if (dst->len && !ISASCII (patend[-1])) {
				/*
				 * Trouble.  We just had a multi-byte
				 * UTF-8 sequence.  We need to add a
				 * parenthesis.
				 */
				char *q = g_utf8_prev_char (patend);
				gunichar cq = g_utf8_get_char (q);

				g_string_truncate (dst, dst->len - (patend - q));
				g_string_append (dst, dstext ? "(" : "\\(");
				g_string_append_unichar (dst, cq);
				g_string_append (dst, dstext ? ")" : "\\)");
				preg->parcount++;
			}

			/*
			 * Copying the repeat sequence is trivial since it
			 * should not contain special characters apart from
			 * the first.
			 */
			g_string_append_c (dst, *p++);
			continue;
		}

		switch (c) {
		case '[':
			p++;
			res = make_bracket (preg, dst, (char **)&p, mpflags);
			if (res != REG_OK)
				return res;
			break;

		case '.':
			p++;
			if (REPEATCHAR (srcext, *p)) {
				g_string_append (dst, "(" UTF8DOT ")");
				preg->parcount++;
			} else
				g_string_append (dst, UTF8DOT);
			break;

		case '\\':
			g_string_append_c (dst, *p++);
			c = g_utf8_get_char (p);
			if (c == 0)
				return REG_EESCAPE;

			/*
			 * No support for back references.  (Further, only
			 * special characters which are all ascii can be
			 * escaped.  Otherwise, the simple add-a-() above
			 * would not work.)
			 */
			if (g_unichar_isdigit (c) || !ISASCII (c))
				return REG_BADPAT;

			g_string_append_unichar (dst, *p++);
			break;

		default:
			g_string_append_unichar (dst, c);
			p = g_utf8_next_char (p);
		}
	}

	if ((mpflags & MP_SUBEXP)) {
		if (*p == 0)
			return REG_EPAREN;
		p++;
	}

	*pp = (char *)p;
	return REG_OK;
}

/* -------------------------------------------------------------------------- */

int
gnumeric_regcomp (gnumeric_regex_t *preg, const char *pattern, int cflags)
{
	const char *pattern0;
	char *pattern_dup = NULL;
	GString *utf8pat = NULL;
	int res = REG_OK;
	gboolean extended = (cflags & REG_EXTENDED) != 0;
	gboolean casefold = (cflags & REG_ICASE) != 0;
	int mpflags;

	preg->casefold = casefold;
	preg->nosub = (cflags & REG_NOSUB) != 0;
	preg->parens = g_new (size_t, 1);
	preg->parens[0] = 0;
	preg->srcparcount = 0;
	preg->parcount = 0;

	if (casefold) {
		char *p;

		/* Not perfect...  Not even close.  */
		pattern = pattern_dup = g_strdup (pattern);
		for (p = pattern_dup; *p; p++)
			if (ISASCII (*p))
				*p = g_ascii_tolower (*p);

		cflags &= ~REG_ICASE;
	}

	utf8pat = g_string_new (NULL);
	mpflags = extended ? MP_SRCEXT | MP_DSTEXT : 0;
	pattern0 = pattern;
	res = make_pattern (preg, utf8pat, (char **)&pattern, mpflags);
	if (res == REG_BADPAT && !extended) {
		/* Retry as extended destination pattern.  */
		cflags |= REG_EXTENDED;
		mpflags |= MP_DSTEXT;
		res = make_pattern (preg, utf8pat, (char **)&pattern0, mpflags);
	}
	if (res != REG_OK) goto out;

#ifdef TEST_REGUTF8
	{
		size_t i;
		fprintf (stderr, "Intermediate pattern: \"%s\"\n", utf8pat->str);
		for (i = 0; i <= preg->srcparcount; i++)
			if (preg->parens[i] != i)
				fprintf (stderr, "  paridx[%d] = %d\n",
					 (int)i, (int)preg->parens[i]);
	}

#endif

	res = regcomp (&preg->theregexp, utf8pat->str, cflags);
	if (res != REG_OK) goto out;

 out:
	if (utf8pat)
		g_string_free (utf8pat, TRUE);
	g_free (pattern_dup);
	return res;
}

/* -------------------------------------------------------------------------- */

int
gnumeric_regexec (const gnumeric_regex_t *preg, const char *string,
		  size_t nmatch, regmatch_t pmatch[], int eflags)
{
	int res;
	regmatch_t *utf8pmatch;
	size_t utf8nmatch, i;
	char *string_dup = NULL;

	if (preg->nosub) nmatch = 0;
	utf8nmatch = nmatch ? preg->parcount + 1 : 0;
	utf8pmatch = nmatch ? g_new (regmatch_t, utf8nmatch) : NULL;

	if (preg->casefold) {
		char *p;

		/*
		 * Not perfect...  Not even close.
		 *
		 * Doing things this way prevents length-changes which
		 * would kill ()-matching.
		 */
		string = string_dup = g_strdup (string);
		for (p = string_dup; *p; p++)
			if (ISASCII (*p))
				*p = g_ascii_tolower (*p);
	}

	res = regexec (&preg->theregexp, string, utf8nmatch, utf8pmatch, eflags);
	if (res != REG_OK)
		goto out;

	for (i = 0; i < nmatch; i++) {
		if (i <= preg->srcparcount) {
			size_t j = preg->parens[i];
			pmatch[i].rm_so = utf8pmatch[j].rm_so;
			pmatch[i].rm_eo = utf8pmatch[j].rm_eo;
		} else {
			pmatch[i].rm_so = -1;
			pmatch[i].rm_eo = -1;
		}
	}

 out:
	g_free (utf8pmatch);
	g_free (string_dup);
	return res;
}

/* -------------------------------------------------------------------------- */

size_t
gnumeric_regerror (int errcode, const gnumeric_regex_t *preg,
		   char *errbuf, size_t errbuf_size)
{
	(void)preg;
	return regerror (errcode, NULL, errbuf, errbuf_size);
}

/* -------------------------------------------------------------------------- */

void
gnumeric_regfree (gnumeric_regex_t *preg)
{
	regfree (&preg->theregexp);
	g_free (preg->parens);
}

/* -------------------------------------------------------------------------- */

#ifdef TEST_REGUTF8

static void
test (const char *pattern, const char *string, int cflags)
{
	gnumeric_regex_t r;
	int res;
	int eflags = 0;
	regmatch_t match[100];
	size_t nmatch = sizeof (match) / sizeof (match[0]);

	fprintf (stderr, "Pattern: \"%s\"\n", pattern);
	fprintf (stderr, "String: \"%s\"\n", string);

	res = gnumeric_regcomp (&r, pattern, cflags);
	if (res != REG_OK) {
		char error[1024];
		gnumeric_regerror (res, &r, error, sizeof (error));
		fprintf (stderr,
			 "Pattern failed to compile: %s\n",
			 error);
		goto out;
	}

	res = gnumeric_regexec (&r, string, nmatch, match, eflags);
	if (res == REG_OK) {
		size_t i;
		fprintf (stderr, "Match\n");
		for (i = 0; i < nmatch; i++)
			if (match[i].rm_so != -1)
				fprintf (stderr, "  Match %d: %d-%d\n",
					 (int)i,
					 (int)match[i].rm_so, (int)match[i].rm_eo);
	} else if (res == REG_NOMATCH) {
		fprintf (stderr, "No match\n");
	} else {
		char error[1024];
		gnumeric_regerror (res, &r, error, sizeof (error));
		fprintf (stderr,
			 "Matching failed: %s\n",
			 error);
	}


 out:
	fprintf (stderr, "\n");
	gnumeric_regfree (&r);
}

int
main (int argc, char **argv)
{
	test ("test", "this is a test",0 );
	test ("te[sx]t", "this is a test", 0);
	test ("t([a-f])( )*[sx](t)", "this is a test", REG_EXTENDED);
	test ("this is\\( \\)*a", "this is a test", 0);
	test ("this is( )*a", "this is a test", REG_EXTENDED);

	test ("[aeiouy\xc3\xa5](n)", "h\xc3\xa5ndbold", REG_EXTENDED);
	/* If you have to ask...  */
	test ("[АБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ]",
	      "Я могу есть стекло, это мне не вредит.",
	      0);


	return 0;
}

#endif

#endif /* HAVE_UTF8_REGEXP */

