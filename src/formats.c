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
const char *cell_format_numbers [] = {
	N_("General"),
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
