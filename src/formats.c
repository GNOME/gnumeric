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
static char const * const
cell_format_general [] = {
	N_("General"),
	NULL
};

static char const * const
cell_format_numbers [] = {
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

static char const * const
cell_format_currency [] = {
	N_("$#,##0_);($#,##0)"),
	N_("$#,##0_);[Red]($#,##0)"),
	N_("$#,##0.00_);($#,##0.00)"),
	N_("$#,##0.00_);[Red]($#,##0.00)"),
	NULL,

};

static char const * const
cell_format_account [] = {
	N_("_($*#,##0_);_($*(#,##0);_($*\"-\"_);_(@_)"),
	N_("_(*#,##0_);_(*(#,##0);_(*\"-\"_);_(@_)"),
	N_("_($*#,##0.00_);_($*(#,##0.00);_($*\"-\"??_);_(@_)"),
	N_("_(*#,##0.00_);_(*(#,##0.00);_(*\"-\"??_);_(@_)"),
	NULL
};

static char const * const
cell_format_date [] = {
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

static char const * const
cell_format_time [] = {
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

static char const * const
cell_format_percent [] = {
	N_("0%"),
	N_("0.00%"),
	NULL,
};

static char const * const
cell_format_fraction [] = {
	N_("# ?/?"),
	N_("# ??/??"),
	NULL
};

static char const * const
cell_format_science [] = {
	N_("0.00E+00"),
	N_("##0.0E+0"),
	NULL
};

static char const *
cell_format_text [] = {
	"@",
	NULL,
};

char const * const * const
cell_formats [] = {
	cell_format_general,
	cell_format_numbers,
	cell_format_currency,
	cell_format_account,
	cell_format_date,
	cell_format_time,
	cell_format_percent,
	cell_format_fraction,
	cell_format_science,
	cell_format_text,
	NULL
};

CurrencySymbol const currency_symbols[] =
{
	{ "", "None" },	/* These first four elements */
	{ "$", "$" },	/* Must stay in this order */
	{ "£", "£" },	/* GBP */
	{ "¥", "¥" },	/* JPY */

	/* Be careful with fonts. This uses iso_8859_15 rather than
	 * the more common iso_8859_1 Not many fonts have a correct
	 * Euro symbol here.
	 */
	{ "¤", "¤" },	/* EUR */


	/* The first column has three letter acronyms
	 * for each currency.  They MUST start with '[$'
	 * The second column has the long names of the currencies.
	 */
	{N_("[$ADP]"), N_("Andorran peseta") },
	{N_("[$AED]"), N_("UAE dirham") },
	{N_("[$AFA]"), N_("Afghanistan afghani") },
	{N_("[$ALL]"), N_("Albanian lek") },
	{N_("[$ANG]"), N_("Netherlands Antillian guilder") },
	{N_("[$AON]"), N_("Angolan new kwanza") },
	{N_("[$ARA]"), N_("Argentine austral") },
	{N_("[$ATS]"), N_("Austrian schilling") },
	{N_("[$AUD]"), N_("Australian dollar") },
	{N_("[$AWG]"), N_("Aruban guilder") },
	{N_("[$BBD]"), N_("Barbados dollar") },
	{N_("[$BDT]"), N_("Bangladeshi taka") },
	{N_("[$BEF]"), N_("Belgian franc") },
	{N_("[$BGL]"), N_("Bulgarian lev") },
	{N_("[$BHD]"), N_("Bahraini dinar") },
	{N_("[$BIF]"), N_("Burundi franc") },
	{N_("[$BMD]"), N_("Bermudian dollar") },
	{N_("[$BND]"), N_("Brunei dollar") },
	{N_("[$BOB]"), N_("Bolivian boliviano") },
	{N_("[$BRE]"), N_("Brazilian cruzeiro") },
	{N_("[$BSD]"), N_("Bahamian dollar") },
	{N_("[$BTN]"), N_("Bhutan ngultrum") },
	{N_("[$BWP]"), N_("Botswanian pula") },
	{N_("[$BZD]"), N_("Belize dollar") },
	{N_("[$CAD]"), N_("Canadian dollar") },
	{N_("[$CHF]"), N_("Swiss franc") },
	{N_("[$CLP]"), N_("Chilean peso") },
	{N_("[$CNY]"), N_("Chinese yuan renminbi") },
	{N_("[$COP]"), N_("Colombian peso") },
	{N_("[$CRC]"), N_("Costa Rican colon") },
	{N_("[$CSK]"), N_("Czech Koruna") },
	{N_("[$CUP]"), N_("Cuban peso") },
	{N_("[$CVE]"), N_("Cape Verde escudo") },
	{N_("[$CYP]"), N_("Cyprus pound") },
	{N_("[$DEM]"), N_("Deutsche mark") },
	{N_("[$DJF]"), N_("Djibouti franc") },
	{N_("[$DKK]"), N_("Danish krone") },
	{N_("[$DOP]"), N_("Dominican peso") },
	{N_("[$DZD]"), N_("Algerian dinar") },
	{N_("[$ECS]"), N_("Ecuador sucre") },
	{N_("[$EGP]"), N_("Egyptian pound") },
	{N_("[$ESP]"), N_("Spanish peseta") },
	{N_("[$ETB]"), N_("Ethiopian birr") },
	{N_("[$EUR]"), N_("Euro") },
	{N_("[$FIM]"), N_("Finnish markka") },
	{N_("[$FJD]"), N_("Fiji dollar") },
	{N_("[$FKP]"), N_("Falkland Islands pound") },
	{N_("[$FRF]"), N_("French franc") },
	{N_("[$GBP]"), N_("UK pound sterling") },
	{N_("[$GHC]"), N_("Ghanaian cedi") },
	{N_("[$GIP]"), N_("Gibraltar pound") },
	{N_("[$GMD]"), N_("Gambian dalasi") },
	{N_("[$GNF]"), N_("Guinea franc") },
	{N_("[$GRD]"), N_("Greek drachma") },
	{N_("[$GTQ]"), N_("Guatemalan quetzal") },
	{N_("[$GWP]"), N_("Guinea-Bissau peso") },
	{N_("[$GYD]"), N_("Guyanan dollar") },
	{N_("[$HKD]"), N_("Hong Kong dollar") },
	{N_("[$HNL]"), N_("Honduran lempira") },
	{N_("[$HTG]"), N_("Haitian gourde") },
	{N_("[$HUF]"), N_("Hungarian forint") },
	{N_("[$IDR]"), N_("Indonesian rupiah") },
	{N_("[$IEP]"), N_("Irish punt") },
	{N_("[$IEX]"), N_("Irish pence ***") },
	{N_("[$ILS]"), N_("Israeli shekel") },
	{N_("[$INR]"), N_("Indian rupee") },
	{N_("[$IQD]"), N_("Iraqi dinar") },
	{N_("[$IRR]"), N_("Iranian rial") },
	{N_("[$ISK]"), N_("Iceland krona") },
	{N_("[$ITL]"), N_("Italian lira") },
	{N_("[$JMD]"), N_("Jamaican dollar") },
	{N_("[$JOD]"), N_("Jordanian dinar") },
	{N_("[$JPY]"), N_("Japanese yen") },
	{N_("[$KES]"), N_("Kenyan shilling") },
	{N_("[$KHR]"), N_("Kampuchean riel") },
	{N_("[$KMF]"), N_("Comoros franc") },
	{N_("[$KPW]"), N_("North Korean won") },
	{N_("[$KRW]"), N_("Republic of Korea won") },
	{N_("[$KWD]"), N_("Kuwaiti dinar") },
	{N_("[$KYD]"), N_("Cayman Islands dollar") },
	{N_("[$LAK]"), N_("Lao kip") },
	{N_("[$LBP]"), N_("Lebanese pound") },
	{N_("[$LKR]"), N_("Sri Lanka rupee") },
	{N_("[$LRD]"), N_("Liberian dollar") },
	{N_("[$LSL]"), N_("Lesotho loti") },
	{N_("[$LUF]"), N_("Luxembourg franc") },
	{N_("[$LYD]"), N_("Libyan dinar") },
	{N_("[$MAD]"), N_("Moroccan dirham") },
	{N_("[$MGF]"), N_("Malagasy franc") },
	{N_("[$MLF]"), N_("Mali franc") },
	{N_("[$MMK]"), N_("Myanmar kyat") },
	{N_("[$MNT]"), N_("Mongolian tugrik") },
	{N_("[$MOP]"), N_("Macau pataca") },
	{N_("[$MRO]"), N_("Mauritanian ouguiya") },
	{N_("[$MTL]"), N_("Maltese lira") },
	{N_("[$MUR]"), N_("Mauritius rupee") },
	{N_("[$MVR]"), N_("Maldive rupee") },
	{N_("[$MWK]"), N_("Malawi kwacha") },
	{N_("[$MXP]"), N_("Mexican peso") },
	{N_("[$MYR]"), N_("Malaysian ringgit") },
	{N_("[$MZM]"), N_("Mozambique metical") },
	{N_("[$NGN]"), N_("Nigerian naira") },
	{N_("[$NIO]"), N_("Nicaraguan cordoba oro") },
	{N_("[$NLG]"), N_("Netherlands guilder") },
	{N_("[$NOK]"), N_("Norwegian krone") },
	{N_("[$NPR]"), N_("Nepalese rupee") },
	{N_("[$NZD]"), N_("New Zealand dollar") },
	{N_("[$OMR]"), N_("Rial Omani") },
	{N_("[$PAB]"), N_("Panamanian balboa") },
	{N_("[$PEI]"), N_("Peruvian inti") },
	{N_("[$PGK]"), N_("Papua New Guinea kina") },
	{N_("[$PHP]"), N_("Philippino peso") },
	{N_("[$PKR]"), N_("Pakistan rupee") },
	{N_("[$PLZ]"), N_("Polish zloty") },
	{N_("[$PTE]"), N_("Portuguese escudo") },
	{N_("[$PYG]"), N_("Paraguay guarani") },
	{N_("[$QAR]"), N_("Qatari rial") },
	{N_("[$ROL]"), N_("Romanian leu") },
	{N_("[$RWF]"), N_("Rwanda franc") },
	{N_("[$SAR]"), N_("Saudi Arabian riyal") },
	{N_("[$SBD]"), N_("Solomon Islands dollar") },
	{N_("[$SCR]"), N_("Seychelles rupee") },
	{N_("[$SDP]"), N_("Sudanese pound") },
	{N_("[$SEK]"), N_("Swedish krona") },
	{N_("[$SGD]"), N_("Singapore dollar") },
	{N_("[$SHP]"), N_("St.Helena pound") },
	{N_("[$SLL]"), N_("Sierra Leone leone") },
	{N_("[$SOS]"), N_("Somali shilling") },
	{N_("[$SRG]"), N_("Suriname guilder") },
	{N_("[$STD]"), N_("Sao Tome and Principe dobra") },
	{N_("[$SUR]"), N_("USSR rouble") },
	{N_("[$SVC]"), N_("El Salvador colon") },
	{N_("[$SYP]"), N_("Syrian pound") },
	{N_("[$SZL]"), N_("Swaziland lilangeni") },
	{N_("[$THB]"), N_("Thai baht") },
	{N_("[$TND]"), N_("Tunisian dinar") },
	{N_("[$TOP]"), N_("Tongan pa'anga") },
	{N_("[$TPE]"), N_("East Timor escudo") },
	{N_("[$TRL]"), N_("Turkish lira") },
	{N_("[$TTD]"), N_("Trinidad and Tobago dollar") },
	{N_("[$TWD]"), N_("New Taiwan dollar") },
	{N_("[$TZS]"), N_("Tanzanian shilling") },
	{N_("[$UGX]"), N_("Ugandan shilling") },
	{N_("[$USD]"), N_("US dollar") },
	{N_("[$UYP]"), N_("Uruguayan peso") },
	{N_("[$VEB]"), N_("Venezuelan bolivar") },
	{N_("[$VND]"), N_("Vietnamese dong") },
	{N_("[$VUV]"), N_("Vanuatu vatu") },
	{N_("[$WST]"), N_("Samoan tala") },
	{N_("[$XCD]"), N_("East Caribbean dollar") },
	{N_("[$XEU]"), N_("European currency unit") },
	{N_("[$YDD]"), N_("Democratic Yemeni dinar") },
	{N_("[$YER]"), N_("Yemeni rial") },
	{N_("[$YUN]"), N_("New Yugoslavia dinar") },
	{N_("[$ZAL]"), N_("South African rand funds code") },
	{N_("[$ZAR]"), N_("South African rand") },
	{N_("[$ZMK]"), N_("Zambian kwacha") },
	{N_("[$ZRZ]"), N_("Zaire Zaire") },
	{N_("[$ZWD]"), N_("Zimbabwe dollar") },
	{ NULL, NULL }
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

static FormatFamily
cell_format_is_number (char const * const fmt, FormatCharacteristics *info)
{
	FormatFamily result = FMT_NUMBER;
	gboolean has_sep = FALSE;
	int use_paren = 0;
	int use_red = 0;
	int num_decimals = 0;
	char const *ptr = fmt, *end, *tmp;
	int const fmt_len = strlen (fmt);

	if (fmt_len < 1)
		return FMT_UNKNOWN;

	/* Check for prepended currency */
	if (ptr[0] == '$' || ptr[1] == '£') {
		info->currency_symbol_index = (ptr[0] == '$') ? 1 : 2;
		result = FMT_CURRENCY;
		++ptr;
	} else if (ptr[0] == '[' && ptr[1] == '$') {
		char const * const end = strchr (ptr, ']');

		if (end != NULL && end[1] == ' ') {
			/* FIXME : Look up the correct index */
			info->currency_symbol_index = 1;
			result = FMT_CURRENCY;
			ptr = end + 1;
		} else
			return FMT_UNKNOWN;
	}

	/* Check for thousands seperator */
	if (ptr[0] == '#') {
		ptr = strcmp_inc (ptr+1, format_get_thousand());
		if (ptr == NULL)
			return FMT_UNKNOWN;
		ptr = strncmp_inc (ptr, "##", 2);
		if (ptr == NULL)
			return FMT_UNKNOWN;
		has_sep = TRUE;
	}

	if (ptr[0] != '0')
		return FMT_UNKNOWN;
	++ptr;

	/* Check for decimals */
	tmp = strcmp_inc (ptr, format_get_decimal());
	if (tmp != NULL) {
		num_decimals = 0;
		ptr = tmp;
		while (ptr[num_decimals] == '0')
			++num_decimals;
		ptr += num_decimals;
	}

	if (ptr[0] == '%') {
		if (!has_sep && info->currency_symbol_index == 0) {
			info->num_decimals = num_decimals;
			return FMT_PERCENT;
		}
		return FMT_UNKNOWN;
	}
	if (NULL != (tmp = strcmp_inc (ptr, "E+00"))) {
		if (!has_sep && info->currency_symbol_index == 0 && *tmp == '\0') {
			info->num_decimals = num_decimals;
			return FMT_SCIENCE;
		}
		return FMT_UNKNOWN;
	}

	if (ptr[0] != ';' && ptr[0] != '_' && ptr[0])
		return FMT_UNKNOWN;

	/* We have now handled decimals, and thousands seperators */
	info->thousands_sep = has_sep;
	info->num_decimals = num_decimals;
	info->negative_fmt = 0; /* Temporary, we may change this below */

	/* No special negative handling */
	if (ptr[0] == '\0')
		return result;

	/* Save this position */
	end = ptr;

	/* Handle Trailing '_)' */
	if (ptr[0] == '_') {
		if (ptr[1] != ')')
			return FMT_UNKNOWN;
		ptr += 2;
		use_paren = 2;
	}

	if (ptr[0] != ';')
		return FMT_UNKNOWN;
	++ptr;

	if (ptr[0] == '[') {
		/* TODO : Do we handle 'Red' being translated ?? */
		if (g_strncasecmp (N_("[Red]"), ptr, 5) != 0)
			return FMT_UNKNOWN;
		ptr += 5;
		use_red = 1;
	}

	if (use_paren) {
		if (ptr[0] != '(')
			return FMT_UNKNOWN;
		++ptr;
	}

	/* The next segment should match the original */
	ptr = strncmp_inc (ptr, fmt, end-fmt);
	if (ptr == NULL)
		return FMT_UNKNOWN;

	if (use_paren) {
		if (ptr[0] != ')')
			return FMT_UNKNOWN;
		++ptr;
	}

	info->negative_fmt = use_paren + use_red;

	return result;
}

FormatFamily
cell_format_classify (char const * const fmt, FormatCharacteristics *info)
{
	FormatFamily res;
	int i;

	g_return_val_if_fail (fmt != NULL, FMT_GENERAL);
	g_return_val_if_fail (info != NULL, FMT_GENERAL);

	/* Init the result to something sane */
	info->thousands_sep = FALSE;
	info->num_decimals = 2;
	info->negative_fmt = 0;
	info->currency_symbol_index = 1; /* '$' */

	/* Is it in the lists */
	for (i = 0; cell_formats[i] != NULL ; ++i) {
		int j = 0;
		char const * const * elem = cell_formats[i];
		for (; elem[j] ; ++j)
			if (g_strcasecmp (_(elem[j]), fmt) == 0) {
				info->list_element = j;
				return i;
			}
	}

	/* Can we parse it ? */
	if ((res = cell_format_is_number (fmt, info)) != FMT_UNKNOWN)
		return res;

	return FMT_UNKNOWN;
}
