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

#if 0
typedef enum
{
    FMT_UNKNOWN = -1,

    FMT_GENERAL = 0,
    FMT_NUMBER,
    FMT_CURRENCY,
    FMT_ACCOUNT,
    FMT_DATE,
    FMT_TIME,
    FMT_PERCENT,
    FMT_FRACTION,
    FMT_SCIENCE,
    FMT_TEXT,
    FMT_SPECIAL,
} FormatFamily;

typdef struct
{
	gint	 catalog_element;

	gboolean thousands_sep;
	gint	 num_decimals;	/* 0 - 30 */
	gint	 negative_fmt;	/* 0 - 3 */
} FormatCharacteristics;

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

	/* All number formats begin with 0 or # .*/
	if (fmt[0] == '0' || fmt[0] == '#')
	{
		gboolean is_viable = TRUE;
		if (fmt[0] == '#') {
			prefix = g_strconcat ("#", format_get_thousand(), "###", NULL);
			g_free (prefix);
		}

		is_viable = TRUE;
		return FMT_NUMBER;
	}
}

#endif
