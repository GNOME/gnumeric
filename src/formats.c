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
char const * const cell_format_general [] = {
	N_("General"),
	NULL
};

char const * const cell_format_numbers [] = {
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

char const * const cell_format_currency [] = {
	N_("$#,##0_);($#,##0)"),
	N_("$#,##0_);[Red]($#,##0)"),
	N_("$#,##0.00_);($#,##0.00)"),
	N_("$#,##0.00_);[Red]($#,##0.00)"),
	NULL,

};

char const * const cell_format_account [] = {
	N_("_($*#,##0_);_($*(#,##0);_($*\"-\"_);_(@_)"),
	N_("_(*#,##0_);_(*(#,##0);_(*\"-\"_);_(@_)"),
	N_("_($*#,##0.00_);_($*(#,##0.00);_($*\"-\"??_);_(@_)"),
	N_("_(*#,##0.00_);_(*(#,##0.00);_(*\"-\"??_);_(@_)"),
	NULL
};

char const * const cell_format_date [] = {
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

char const * const cell_format_time [] = {
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

char const * const cell_format_percent [] = {
	N_("0%"),
	N_("0.00%"),
	NULL,
};

char const * const cell_format_fraction [] = {
	N_("# ?/?"),
	N_("# ??/??"),
	NULL
};

char const * const cell_format_science [] = {
	N_("0.00E+00"),
	N_("##0.0E+0"),
	NULL
};

char const *cell_format_text [] = {
	"@",
	NULL,
};

char const * const * const cell_formats [] = {
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
	{ "", "None" },	/* These fist three elements */
	{ "$", "$" },	/* Must stay in this order */
	{ "£", "£" },

	{ "[$ADP]", "Andorran peseta" },
	{ "[$AED]", "UAE dirham" },
	{ "[$AFA]", "Afghanistan afghani" },
	{ "[$ALL]", "Albanian lek" },
	{ "[$ANG]", "Netherlands Antillian guilder" },
	{ "[$AON]", "Angolan new kwanza" },
	{ "[$ARA]", "Argentine austral" },
	{ "[$ATS]", "Austrian schilling" },
	{ "[$AUD]", "Australian dollar" },
	{ "[$AWG]", "Aruban guilder" },
	{ "[$BBD]", "Barbados dollar" },
	{ "[$BDT]", "Bangladeshi taka" },
	{ "[$BEF]", "Belgian franc" },
	{ "[$BGL]", "Bulgarian lev" },
	{ "[$BHD]", "Bahraini dinar" },
	{ "[$BIF]", "Burundi franc" },
	{ "[$BMD]", "Bermudian dollar" },
	{ "[$BND]", "Brunei dollar" },
	{ "[$BOB]", "Bolivian boliviano" },
	{ "[$BRE]", "Brazilian cruzeiro" },
	{ "[$BSD]", "Bahamian dollar" },
	{ "[$BTN]", "Bhutan ngultrum" },
	{ "[$BWP]", "Botswanian pula" },
	{ "[$BZD]", "Belize dollar" },
	{ "[$CAD]", "Canadian dollar" },
	{ "[$CHF]", "Swiss franc" },
	{ "[$CLP]", "Chilean peso" },
	{ "[$CNY]", "Chinese yuan renminbi" },
	{ "[$COP]", "Colombian peso" },
	{ "[$CRC]", "Costa Rican colon" },
	{ "[$CSK]", "Czech Koruna" },
	{ "[$CUP]", "Cuban peso" },
	{ "[$CVE]", "Cape Verde escudo" },
	{ "[$CYP]", "Cyprus pound" },
	{ "[$DEM]", "Deutsche mark" },
	{ "[$DJF]", "Djibouti franc" },
	{ "[$DKK]", "Danish krone" },
	{ "[$DOP]", "Dominican peso" },
	{ "[$DZD]", "Algerian dinar" },
	{ "[$ECS]", "Ecuador sucre" },
	{ "[$EGP]", "Egyptian pound" },
	{ "[$ESP]", "Spanish peseta" },
	{ "[$ETB]", "Ethiopian birr" },
	{ "[$FIM]", "Finnish markka" },
	{ "[$FJD]", "Fiji dollar" },
	{ "[$FKP]", "Falkland Islands pound" },
	{ "[$FRF]", "French franc" },
	{ "[$GBP]", "UK pound sterling" },
	{ "[$GHC]", "Ghanaian cedi" },
	{ "[$GIP]", "Gibraltar pound" },
	{ "[$GMD]", "Gambian dalasi" },
	{ "[$GNF]", "Guinea franc" },
	{ "[$GRD]", "Greek drachma" },
	{ "[$GTQ]", "Guatemalan quetzal" },
	{ "[$GWP]", "Guinea-Bissau peso" },
	{ "[$GYD]", "Guyanan dollar" },
	{ "[$HKD]", "Hong Kong dollar" },
	{ "[$HNL]", "Honduran lempira" },
	{ "[$HTG]", "Haitian gourde" },
	{ "[$HUF]", "Hungarian forint" },
	{ "[$IDR]", "Indonesian rupiah" },
	{ "[$IEP]", "Irish punt" },
	{ "[$IEX]", "Irish pence ***" },
	{ "[$ILS]", "Israeli shekel" },
	{ "[$INR]", "Indian rupee" },
	{ "[$IQD]", "Iraqi dinar" },
	{ "[$IRR]", "Iranian rial" },
	{ "[$ISK]", "Iceland krona" },
	{ "[$ITL]", "Italian lira" },
	{ "[$JMD]", "Jamaican dollar" },
	{ "[$JOD]", "Jordanian dinar" },
	{ "[$JPY]", "Japanese yen" },
	{ "[$KES]", "Kenyan shilling" },
	{ "[$KHR]", "Kampuchean riel" },
	{ "[$KMF]", "Comoros franc" },
	{ "[$KPW]", "North Korean won" },
	{ "[$KRW]", "Republic of Korea won" },
	{ "[$KWD]", "Kuwaiti dinar" },
	{ "[$KYD]", "Cayman Islands dollar" },
	{ "[$LAK]", "Lao kip" },
	{ "[$LBP]", "Lebanese pound" },
	{ "[$LKR]", "Sri Lanka rupee" },
	{ "[$LRD]", "Liberian dollar" },
	{ "[$LSL]", "Lesotho loti" },
	{ "[$LUF]", "Luxembourg franc" },
	{ "[$LYD]", "Libyan dinar" },
	{ "[$MAD]", "Moroccan dirham" },
	{ "[$MGF]", "Malagasy franc" },
	{ "[$MLF]", "Mali franc" },
	{ "[$MMK]", "Myanmar kyat" },
	{ "[$MNT]", "Mongolian tugrik" },
	{ "[$MOP]", "Macau pataca" },
	{ "[$MRO]", "Mauritanian ouguiya" },
	{ "[$MTL]", "Maltese lira" },
	{ "[$MUR]", "Mauritius rupee" },
	{ "[$MVR]", "Maldive rupee" },
	{ "[$MWK]", "Malawi kwacha" },
	{ "[$MXP]", "Mexican peso" },
	{ "[$MYR]", "Malaysian ringgit" },
	{ "[$MZM]", "Mozambique metical" },
	{ "[$NGN]", "Nigerian naira" },
	{ "[$NIO]", "Nicaraguan cordoba oro" },
	{ "[$NLG]", "Netherlands guilder" },
	{ "[$NOK]", "Norwegian krone" },
	{ "[$NPR]", "Nepalese rupee" },
	{ "[$NZD]", "New Zealand dollar" },
	{ "[$OMR]", "Rial Omani" },
	{ "[$PAB]", "Panamanian balboa" },
	{ "[$PEI]", "Peruvian inti" },
	{ "[$PGK]", "Papua New Guinea kina" },
	{ "[$PHP]", "Philippino peso" },
	{ "[$PKR]", "Pakistan rupee" },
	{ "[$PLZ]", "Polish zloty" },
	{ "[$PTE]", "Portuguese escudo" },
	{ "[$PYG]", "Paraguay guarani" },
	{ "[$QAR]", "Qatari rial" },
	{ "[$ROL]", "Romanian leu" },
	{ "[$RWF]", "Rwanda franc" },
	{ "[$SAR]", "Saudi Arabian riyal" },
	{ "[$SBD]", "Solomon Islands dollar" },
	{ "[$SCR]", "Seychelles rupee" },
	{ "[$SDP]", "Sudanese pound" },
	{ "[$SEK]", "Swedish krona" },
	{ "[$SGD]", "Singapore dollar" },
	{ "[$SHP]", "St.Helena pound" },
	{ "[$SLL]", "Sierra Leone leone" },
	{ "[$SOS]", "Somali shilling" },
	{ "[$SRG]", "Suriname guilder" },
	{ "[$STD]", "Sao Tome and Principe dobra" },
	{ "[$SUR]", "USSR rouble" },
	{ "[$SVC]", "El Salvador colon" },
	{ "[$SYP]", "Syrian pound" },
	{ "[$SZL]", "Swaziland lilangeni" },
	{ "[$THB]", "Thai baht" },
	{ "[$TND]", "Tunisian dinar" },
	{ "[$TOP]", "Tongan pa'anga" },
	{ "[$TPE]", "East Timor escudo" },
	{ "[$TRL]", "Turkish lira" },
	{ "[$TTD]", "Trinidad and Tobago dollar" },
	{ "[$TWD]", "New Taiwan dollar" },
	{ "[$TZS]", "Tanzanian shilling" },
	{ "[$UGX]", "Ugandan shilling" },
	{ "[$USD]", "US dollar" },
	{ "[$UYP]", "Uruguayan peso" },
	{ "[$VEB]", "Venezuelan bolivar" },
	{ "[$VND]", "Vietnamese dong" },
	{ "[$VUV]", "Vanuatu vatu" },
	{ "[$WST]", "Samoan tala" },
	{ "[$XCD]", "East Caribbean dollar" },
	{ "[$XEU]", "European currency unit" },
	{ "[$YDD]", "Democratic Yemeni dinar" },
	{ "[$YER]", "Yemeni rial" },
	{ "[$YUN]", "New Yugoslavia dinar" },
	{ "[$ZAL]", "South African rand funds code" },
	{ "[$ZAR]", "South African rand" },
	{ "[$ZMK]", "Zambian kwacha" },
	{ "[$ZRZ]", "Zaire Zaire" },
	{ "[$ZWD]", "Zimbabwe dollar" },
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
	if (ptr[0] == '$' || ptr[1] == "£") {
		info->currency_symbol_index = (ptr[0] == '$') ? 1 : 2;
		result = FMT_CURRENCY;
		++ptr;
	} else if (ptr[0] == '[' && ptr[1] == '$') {
		char const * const end = strchr (ptr, ']');

		if (end != NULL) {
			/* FIXME : Look up the correct index */
			info->currency_symbol_index = 1;
			result = FMT_CURRENCY;
			ptr = end + 1;
		}
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
