/*
 * formats.c: The default formats supported in Gnumeric
 *
 * For information on how to translate these format strings properly,
 * refer to the doc/translating.sgml file in the Gnumeric distribution.
 *
 * Author:
 *    Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include "formats.h"
#include "format.h"

/* The various formats */
const char *cell_format_general [] = {
	N_("General"),
	NULL
};

const char *cell_format_numbers [] = {
	N_("0"),
	N_("0.00"),
	N_("#,##0"),
	N_("#,##0.00"),
	N_("#,##0_);(#,##0)"),
	N_("#,##0_);[Red](#,##0)"),
	N_("#,##0.00_);(#,##0.00)"),
	N_("#,##0.00_);[Red](#,##0.00)"),
	N_("0.0"),
	NULL
};

const char *cell_format_accounting [] = {
	N_("_($*#,##0_);_($*(#,##0);_($*\"-\"_);_(@_)"),
	N_("_(*$,$$0_);_(*(#,##0);_(*\"-\"_);_(@_)"),
	N_("_($*#,##0.00_);_($*(#,##0.00);_($*\"-\"??_);_(@_)"),
	N_("_(*#,##0.00_);_(*(#,##0.00);_(*\"-\"??_);_(@_)"),
	NULL
};

const char *cell_format_date [] = {
	N_("m/d/yy"),
	N_("m/d/yyyy"),
	N_("d-mmm-yy"),
	N_("d-mmm-yyyy"),
	N_("d-mmm"),
	N_("d-mm"),
	N_("mmm/d"),
	N_("mm/d"),
	N_("mm/dd/yy"),
	N_("mm/dd/yyyy"),
	N_("mmm/dd/yy"),
	N_("mmm/dd/yyyy"),
	N_("mmm/ddd/yy"),
	N_("mmm/ddd/yyyy"),
	N_("mm/ddd/yy"),
	N_("mm/ddd/yyyy"),
	N_("mmm-yy"),
	N_("mmm-yyyy"),
	N_("m/d/yy h:mm"),
	N_("m/d/yyyy h:mm"),
	N_("yyyy/mm/d"),
	N_("yyyy/mmm/d"),
	N_("yyyy/mm/dd"),
	N_("yyyy/mmm/dd"),
	N_("yyyy-mm-d"),
	N_("yyyy-mmm-d"),
	N_("yyyy-mm-dd"),
	N_("yyyy-mmm-dd"),
	NULL
};

const char *cell_format_hour [] = {
	N_("h:mm AM/PM"),
	N_("h:mm:ss AM/PM"),
	N_("h:mm"),
	N_("h:mm:ss"),
	N_("m/d/yy h:mm"),
	N_("mm:ss"),
	N_("mm:ss.0"),
	N_("[h]:mm:ss"),
	N_("[h]:mm"),
	N_("[mm]:ss"),
	N_("[ss]"),
	NULL
};

const char *cell_format_percent [] = {
	N_("0%"),
	N_("0.00%"),
	NULL,
};

const char *cell_format_fraction [] = {
	N_("# ?/?"),
	N_("# ??/??"),
	NULL
};

const char *cell_format_scientific [] = {
	N_("0.00E+00"),
	N_("##0.0E+0"),
	NULL
};

const char *cell_format_text [] = {
	"@",
	NULL,
};

const char *cell_format_money [] = {
	N_("$#,##0_);($#,##0)"),
	N_("$#,##0_);[Red]($#,##0)"),
	N_("$#,##0.00_);($#,##0.00)"),
	N_("$#,##0.00_);[Red]($#,##0.00)"),
	NULL,

};

/* Returns a+n if b[0..n-1] is a prefix to a */
static char const *
strncmp_inc (char const * const a, char const * const b, unsigned const n)
{
	if (strncmp (a, b, n) == 0)
		return a+n;
	return NULL;
}

/* Returns a+strlen(b) if b is a prefix to a */
static char const *
strcmp_inc (char const * const a, char const * const b)
{
	int const len = strlen (b);
	if (strncmp (a, b, len) == 0)
		return a+len;
	return NULL;
}

static gboolean
cell_format_is_number (char const * const fmt, FormatCharacteristics *info)
{
	gboolean has_sep = FALSE;
	int use_paren = 0;
	int use_red = 0;
	int num_decimals = 0;
	char const *ptr = fmt, *end;

	/* Check for thousands seperator */
	if (ptr[0] == '#') {
		ptr = strcmp_inc (ptr+1, format_get_thousand());
		if (ptr == NULL)
			return FALSE;
		ptr = strncmp_inc (ptr, "##", 2);
		if (ptr == NULL)
			return FALSE;
		has_sep = TRUE;
	}

	if (ptr[0] != '0')
		return FALSE;
	++ptr;

	/* Check for decimals */
	if (ptr[0] != ';' && ptr[0] != '_' && ptr[0]) {
		int count = 0;
		ptr = strcmp_inc (ptr, format_get_decimal());
		if (ptr == NULL)
			return FALSE;

		while (ptr[count] == '0')
			++count;

		if (ptr[count] != ';' && ptr[count] != '_' && ptr[count])
			return FALSE;
		num_decimals = count;
		ptr += count;
	}

	/* We have now handled decimals, and thousands seperators */
	info->thousands_sep = has_sep;
	info->num_decimals = num_decimals;
	info->negative_fmt = 0; /* Temporary, we may change this below */

	/* No special negative handling */
	if (ptr[0] == '\0')
		return TRUE;

	/* Save this position */
	end = ptr;

	/* Handle Trailing '_)' */
	if (ptr[0] == '_') {
		if (ptr[1] != ')')
			return FALSE;
		ptr += 2;
		use_paren = 2;
	}

	if (ptr[0] != ';')
		return FALSE;
	++ptr;

	if (ptr[0] == '[') {
		/* TODO : Do we handle 'Red' being translated ?? */
		if (g_strncasecmp (N_("[Red]"), ptr, 5) != 0)
			return FALSE;
		ptr += 5;
		use_red = 1;
	}

	if (use_paren) {
		if (ptr[0] != '(')
			return FALSE;
		++ptr;
	}

	/* The next segment should match the original */
	ptr = strncmp_inc (ptr, fmt, end-fmt);
	if (ptr == NULL)
		return FALSE;

	if (use_paren) {
		if (ptr[0] != ')')
			return FALSE;
		++ptr;
	}

	info->negative_fmt = use_paren + use_red;
	return TRUE;
}

FormatFamily
cell_format_classify (char const * const fmt, FormatCharacteristics *info)
{
	g_return_val_if_fail (fmt != NULL, FMT_GENERAL);
	g_return_val_if_fail (info != NULL, FMT_GENERAL);

	/* Is it General */
	if (g_strcasecmp (_("General"), fmt) == 0) {
		info->catalog_element = 0;
		return FMT_GENERAL;
	}

	if (cell_format_is_number (fmt, info))
		return FMT_NUMBER;

	return FMT_UNKNOWN;
}
