/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * formats.c: The default formats supported in Gnumeric
 *
 * For information on how to translate these format strings properly,
 * refer to the doc/translating.sgml file in the Gnumeric distribution.
 *
 * Author:
 *    Miguel de Icaza (miguel@kernel.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "formats.h"

#include "format.h"
#include <string.h>
#include <libgnome/gnome-i18n.h>

/* The various formats */
static char const * const
cell_format_general [] = {
	"General",
	NULL
};

static char const * const
cell_format_numbers [] = {
	"0",
	"0.00",
	"#,##0",
	"#,##0.00",
	"#,##0_);(#,##0)",
	"#,##0_);[Red](#,##0)",
	"#,##0.00_);(#,##0.00)",
	"#,##0.00_);[Red](#,##0.00)",
	"0.0",
	NULL
};

/* Some are generated */
static char const *
cell_format_currency [] = {
	NULL, /* "$#,##0", */
	NULL, /* "$#,##0_);($#,##0)", */
	NULL, /* "$#,##0_);[Red]($#,##0)", */
	NULL, /* "$#,##0.00", */
	NULL, /* "$#,##0.00_);($#,##0.00)", */
	NULL, /* "$#,##0.00_);[Red]($#,##0.00)", */
	NULL,
};

/* Some are generated */
static char const *
cell_format_account [] = {
	NULL, /* "_($* #,##0_);_($* (#,##0);_($* \"-\"_);_(@_)", */
	"_(* #,##0_);_(* (#,##0);_(* \"-\"_);_(@_)",
	NULL, /* "_($* #,##0.00_);_($* (#,##0.00);_($* \"-\"??_);_(@_)", */
	"_(* #,##0.00_);_(* (#,##0.00);_(* \"-\"??_);_(@_)",
	NULL
};

/*****************************************************************/
/* Some are generated						 */
/* WARNING WARNING WARNING : do not reorder these !! 		 */
/* the generated versions and the excel plugin assume this order */
static char const *
cell_format_date [] = {
	"m/d/yy",		/* 0 */
	"m/d/yyyy",		/* 1 */
	"d-mmm-yy",		/* 2 */
	"d-mmm-yyyy",		/* 3 */
	"d-mmm",		/* 4 */
	"d-mm",			/* 5 */
	"mmm/d",		/* 6 */
	"mm/d",			/* 7 */
	"mm/dd/yy",		/* 8 */
	"mm/dd/yyyy",		/* 9 */
	"mmm/dd/yy",		/* 10 */
	"mmm/dd/yyyy",		/* 11 */
	"mmm/ddd/yy",		/* 12 */
	"mmm/ddd/yyyy",		/* 13 */
	"mm/ddd/yy",		/* 14 */
	"mm/ddd/yyyy",		/* 15 */
	"mmm-yy",		/* 16 */
	"mmm-yyyy",		/* 17 */
	"mmmm-yy",		/* 18 */
	"mmmm-yyyy",		/* 19 */
	"m/d/yy h:mm",		/* 20 */
	"m/d/yyyy h:mm",	/* 21 */
	"yyyy/mm/d",		/* 22 */
	"yyyy/mmm/d",		/* 23 */
	"yyyy/mm/dd",		/* 24 */
	"yyyy/mmm/dd",		/* 25 */
	"yyyy-mm-d",		/* 26 */
	"yyyy-mmm-d",		/* 27 */
	"yyyy-mm-dd",		/* 28 */
	"yyyy-mmm-dd",		/* 29 */
	"yy",			/* 30 */
	"yyyy",			/* 31 */
	NULL
};

/*****************************************************/

/* Some are generated */
static char const *
cell_format_time [] = {
	"h:mm AM/PM",
	"h:mm:ss AM/PM",
	"h:mm",
	"h:mm:ss",
	"m/d/yy h:mm",
	"[h]:mm",		/* keep this before mm:ss so that 24:00 will */
	"[h]:mm:ss",		/* match an hour based format (bug #86338)   */
	"mm:ss",
	"mm:ss.0",
	"[mm]:ss",
	"[ss]",
	NULL
};

static char const * const
cell_format_percent [] = {
	"0%",
	"0.00%",
	NULL,
};

static char const * const
cell_format_fraction [] = {
	"# ?/?",
	"# ?" "?/?" "?",  /* Don't accidentally use trigraph.  */
	NULL
};

static char const * const
cell_format_science [] = {
	"0.00E+00",
	"##0.0E+0",
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

void
currency_date_format_init (void)
{
	gboolean precedes, space_sep;
	char const *curr = format_get_currency (&precedes, &space_sep);
	char *pre, *post, *pre_rep, *post_rep;

	if (precedes) {
		post_rep = post = (char *)"";
		pre_rep = (char *)"* ";
		pre = g_strconcat ("\"", curr,
				   (space_sep) ? "\" " : "\"", NULL);
	} else {
		pre_rep = pre = (char *)"";
		post_rep = (char *)"* ";
		post = g_strconcat ((space_sep) ? " \"" : "\"",
				    curr, "\"", NULL);
	}

	cell_format_currency [0] = g_strdup_printf (
		"%s#,##0%s",
		pre, post);
	cell_format_currency [1] = g_strdup_printf (
		"%s#,##0%s_);(%s#,##0%s)",
		pre, post, pre, post);
	cell_format_currency [2] = g_strdup_printf (
		"%s#,##0%s_);[Red](%s#,##0%s)",
		pre, post, pre, post);
	cell_format_currency [3] = g_strdup_printf (
		"%s#,##0.00%s",
		pre, post);
	cell_format_currency [4] = g_strdup_printf (
		"%s#,##0.00%s_);(%s#,##0.00%s)",
		pre, post, pre, post);
	cell_format_currency [5] = g_strdup_printf (
		"%s#,##0.00%s_);[Red](%s#,##0.00%s)",
		pre, post, pre, post);

	cell_format_account [0] = g_strdup_printf (
		"_(%s%s#,##0%s%s_);_(%s%s(#,##0)%s%s;_(%s%s\"-\"%s%s_);_(@_)",
		pre, pre_rep, post_rep, post,
		pre, pre_rep, post_rep, post,
		pre, pre_rep, post_rep, post);
	cell_format_account [2] = g_strdup_printf (
		"_(%s%s#,##0.00%s%s_);_(%s%s(#,##0.00)%s%s;_(%s%s\"-\"??%s%s_);_(@_)",
		pre, pre_rep, post_rep, post,
		pre, pre_rep, post_rep, post,
		pre, pre_rep, post_rep, post);

	g_free (*pre ? pre : post);

	if (!format_month_before_day ()) {
		cell_format_date [0]  = "d/m/yy";
		cell_format_date [1]  = "d/m/yyyy";
		cell_format_date [2]  = "mmm-d-yy";
		cell_format_date [3]  = "mmm-d-yyyy";
		cell_format_date [4]  = "mmm-d";
		cell_format_date [5]  = "mm-d";
		cell_format_date [6]  = "d/mmm";
		cell_format_date [7]  = "d/mm";
		cell_format_date [8]  = "dd/mm/yy";
		cell_format_date [9]  = "dd/mm/yyyy";
		cell_format_date [10] = "dd/mmm/yy";
		cell_format_date [11] = "dd/mmm/yyyy";
		cell_format_date [12] = "ddd/mmm/yy";
		cell_format_date [13] = "ddd/mmm/yyyy";
		cell_format_date [14] = "ddd/mm/yy";
		cell_format_date [15] = "ddd/mm/yyyy";
		cell_format_date [20] = "d/m/yy h:mm";
		cell_format_date [21] = "d/m/yyyy h:mm";

		cell_format_time [4]  = "d/m/yy h:mm";
	}
}

void
currency_date_format_shutdown (void)
{
}

CurrencySymbol const currency_symbols[] =
{
	{ "", "None" },	/* These first four elements */
	{ "$", "$" },	/* Must stay in this order */
	{ "Â£", "Â£" },	/* GBP */
	{ "Â¥", "Â¥" },	/* JPY */
	{ "â‚¬", "â‚¬" },	/* EUR */

	/* The first column has three letter acronyms
	 * for each currency.  They MUST start with '[$'
	 * The second column has the long names of the currencies.
	 */

	/* 2002/08/04 Updated to match iso 4217 */
	{ "[$AED]",	N_("United Arab Emirates, Dirhams") },
	{ "[$AFA]",	N_("Afghanistan, Afghanis") },
	{ "[$ALL]",	N_("Albania, Leke") },
	{ "[$AMD]",	N_("Armenia, Drams") },
	{ "[$ANG]",	N_("Netherlands Antilles, Guilders") },
	{ "[$AOA]",	N_("Angola, Kwanza") },
	{ "[$ARS]",	N_("Argentina, Pesos") },
	{ "[$AUD]",	N_("Australia, Dollars") },
	{ "[$AWG]",	N_("Aruba, Guilders") },
	{ "[$AZM]",	N_("Azerbaijan, Manats") },
	{ "[$BAM]",	N_("Bosnia and Herzegovina, Convertible Marka") },
	{ "[$BBD]",	N_("Barbados, Dollars") },
	{ "[$BDT]",	N_("Bangladesh, Taka") },
	{ "[$BGL]",	N_("Bulgaria, Leva") },
	{ "[$BHD]",	N_("Bahrain, Dinars") },
	{ "[$BIF]",	N_("Burundi, Francs") },
	{ "[$BMD]",	N_("Bermuda, Dollars") },
	{ "[$BND]",	N_("Brunei Darussalam, Dollars") },
	{ "[$BOB]",	N_("Bolivia, Bolivianos") },
	{ "[$BRL]",	N_("Brazil, Brazil Real") },
	{ "[$BSD]",	N_("Bahamas, Dollars") },
	{ "[$BTN]",	N_("Bhutan, Ngultrum") },
	{ "[$BWP]",	N_("Botswana, Pulas") },
	{ "[$BYR]",	N_("Belarus, Rubles") },
	{ "[$BZD]",	N_("Belize, Dollars") },
	{ "[$CAD]",	N_("Canada, Dollars") },
	{ "[$CDF]",	N_("Congo/Kinshasa, Congolese Francs") },
	{ "[$CHF]",	N_("Switzerland, Francs") },
	{ "[$CLP]",	N_("Chile, Pesos") },
	{ "[$CNY]",	N_("China, Yuan Renminbi") },
	{ "[$COP]",	N_("Colombia, Pesos") },
	{ "[$CRC]",	N_("Costa Rica, Colones") },
	{ "[$CUP]",	N_("Cuba, Pesos") },
	{ "[$CVE]",	N_("Cape Verde, Escudos") },
	{ "[$CYP]",	N_("Cyprus, Pounds") },
	{ "[$CZK]",	N_("Czech Republic, Koruny") },
	{ "[$DJF]",	N_("Djibouti, Francs") },
	{ "[$DKK]",	N_("Denmark, Kroner") },
	{ "[$DOP]",	N_("Dominican Republic, Pesos") },
	{ "[$DZD]",	N_("Algeria, Algeria Dinars") },
	{ "[$EEK]",	N_("Estonia, Krooni") },
	{ "[$EGP]",	N_("Egypt, Pounds") },
	{ "[$ERN]",	N_("Eritrea, Nakfa") },
	{ "[$ETB]",	N_("Ethiopia, Birr") },
	{ "[$EUR]",	N_("Euro Member Countries, Euro") },
	{ "[$FJD]",	N_("Fiji, Dollars") },
	{ "[$FKP]",	N_("Falkland Islands (Malvinas), Pounds") },
	{ "[$GBP]",	N_("United Kingdom, Pounds") },
	{ "[$GEL]",	N_("Georgia, Lari") },
	{ "[$GGP]",	N_("Guernsey, Pounds") },
	{ "[$GHC]",	N_("Ghana, Cedis") },
	{ "[$GIP]",	N_("Gibraltar, Pounds") },
	{ "[$GMD]",	N_("Gambia, Dalasi") },
	{ "[$GNF]",	N_("Guinea, Francs") },
	{ "[$GTQ]",	N_("Guatemala, Quetzales") },
	{ "[$GYD]",	N_("Guyana, Dollars") },
	{ "[$HKD]",	N_("Hong Kong, Dollars") },
	{ "[$HNL]",	N_("Honduras, Lempiras") },
	{ "[$HRK]",	N_("Croatia, Kuna") },
	{ "[$HTG]",	N_("Haiti, Gourdes") },
	{ "[$HUF]",	N_("Hungary, Forint") },
	{ "[$IDR]",	N_("Indonesia, Rupiahs") },
	{ "[$ILS]",	N_("Israel, New Shekels") },
	{ "[$IMP]",	N_("Isle of Man, Pounds") },
	{ "[$INR]",	N_("India, Rupees") },
	{ "[$IQD]",	N_("Iraq, Dinars") },
	{ "[$IRR]",	N_("Iran, Rials") },
	{ "[$ISK]",	N_("Iceland, Kronur") },
	{ "[$JEP]",	N_("Jersey, Pounds") },
	{ "[$JMD]",	N_("Jamaica, Dollars") },
	{ "[$JOD]",	N_("Jordan, Dinars") },
	{ "[$JPY]",	N_("Japan, Yen") },
	{ "[$KES]",	N_("Kenya, Shillings") },
	{ "[$KGS]",	N_("Kyrgyzstan, Soms") },
	{ "[$KHR]",	N_("Cambodia, Riels") },
	{ "[$KMF]",	N_("Comoros, Francs") },
	{ "[$KPW]",	N_("Korea (North), Won") },
	{ "[$KRW]",	N_("Korea (South), Won") },
	{ "[$KWD]",	N_("Kuwait, Dinars") },
	{ "[$KYD]",	N_("Cayman Islands, Dollars") },
	{ "[$KZT]",	N_("Kazakstan, Tenge") },
	{ "[$LAK]",	N_("Laos, Kips") },
	{ "[$LBP]",	N_("Lebanon, Pounds") },
	{ "[$LKR]",	N_("Sri Lanka, Rupees") },
	{ "[$LRD]",	N_("Liberia, Dollars") },
	{ "[$LSL]",	N_("Lesotho, Maloti") },
	{ "[$LTL]",	N_("Lithuania, Litai") },
	{ "[$LVL]",	N_("Latvia, Lati") },
	{ "[$LYD]",	N_("Libya, Dinars") },
	{ "[$MAD]",	N_("Morocco, Dirhams") },
	{ "[$MDL]",	N_("Moldova, Lei") },
	{ "[$MGF]",	N_("Madagascar, Malagasy Francs") },
	{ "[$MKD]",	N_("Macedonia, Denars") },
	{ "[$MMK]",	N_("Myanmar (Burma), Kyats") },
	{ "[$MNT]",	N_("Mongolia, Tugriks") },
	{ "[$MOP]",	N_("Macau, Patacas") },
	{ "[$MRO]",	N_("Mauritania, Ouguiyas") },
	{ "[$MTL]",	N_("Malta, Liri") },
	{ "[$MUR]",	N_("Mauritius, Rupees") },
	{ "[$MVR]",	N_("Maldives (Maldive Islands), Rufiyaa") },
	{ "[$MWK]",	N_("Malawi, Kwachas") },
	{ "[$MXN]",	N_("Mexico, Pesos") },
	{ "[$MYR]",	N_("Malaysia, Ringgits") },
	{ "[$MZM]",	N_("Mozambique, Meticais") },
	{ "[$NAD]",	N_("Namibia, Dollars") },
	{ "[$NGN]",	N_("Nigeria, Nairas") },
	{ "[$NIO]",	N_("Nicaragua, Gold Cordobas") },
	{ "[$NOK]",	N_("Norway, Krone") },
	{ "[$NPR]",	N_("Nepal, Nepal Rupees") },
	{ "[$NZD]",	N_("New Zealand, Dollars") },
	{ "[$OMR]",	N_("Oman, Rials") },
	{ "[$PAB]",	N_("Panama, Balboa") },
	{ "[$PEN]",	N_("Peru, Nuevos Soles") },
	{ "[$PGK]",	N_("Papua New Guinea, Kina") },
	{ "[$PHP]",	N_("Philippines, Pesos") },
	{ "[$PKR]",	N_("Pakistan, Rupees") },
	{ "[$PLN]",	N_("Poland, Zlotych") },
	{ "[$PYG]",	N_("Paraguay, Guarani") },
	{ "[$QAR]",	N_("Qatar, Rials") },
	{ "[$ROL]",	N_("Romania, Lei") },
	{ "[$RUR]",	N_("Russia, Rubles") },
	{ "[$RWF]",	N_("Rwanda, Rwanda Francs") },
	{ "[$SAR]",	N_("Saudi Arabia, Riyals") },
	{ "[$SBD]",	N_("Solomon Islands, Dollars") },
	{ "[$SCR]",	N_("Seychelles, Rupees") },
	{ "[$SDD]",	N_("Sudan, Dinars") },
	{ "[$SEK]",	N_("Sweden, Kronor") },
	{ "[$SGD]",	N_("Singapore, Dollars") },
	{ "[$SHP]",	N_("Saint Helena, Pounds") },
	{ "[$SIT]",	N_("Slovenia, Tolars") },
	{ "[$SKK]",	N_("Slovakia, Koruny") },
	{ "[$SLL]",	N_("Sierra Leone, Leones") },
	{ "[$SOS]",	N_("Somalia, Shillings") },
	{ "[$SPL]",	N_("Seborga, Luigini") },
	{ "[$SRG]",	N_("Suriname, Guilders") },
	{ "[$STD]",	N_("São Tome and Principe, Dobras") },
	{ "[$SVC]",	N_("El Salvador, Colones") },
	{ "[$SYP]",	N_("Syria, Pounds") },
	{ "[$SZL]",	N_("Swaziland, Emalangeni") },
	{ "[$THB]",	N_("Thailand, Baht") },
	{ "[$TJR]",	N_("Tajikistan, Rubles") },
	{ "[$TMM]",	N_("Turkmenistan, Manats") },
	{ "[$TND]",	N_("Tunisia, Dinars") },
	{ "[$TOP]",	N_("Tonga, Pa'anga") },
	{ "[$TRL]",	N_("Turkey, Liras") },
	{ "[$TTD]",	N_("Trinidad and Tobago, Dollars") },
	{ "[$TVD]",	N_("Tuvalu, Tuvalu Dollars") },
	{ "[$TWD]",	N_("Taiwan, New Dollars") },
	{ "[$TZS]",	N_("Tanzania, Shillings") },
	{ "[$UAH]",	N_("Ukraine, Hryvnia") },
	{ "[$UGX]",	N_("Uganda, Shillings") },
	{ "[$USD]",	N_("United States of America, Dollars") },
	{ "[$UYU]",	N_("Uruguay, Pesos") },
	{ "[$UZS]",	N_("Uzbekistan, Sums") },
	{ "[$VEB]",	N_("Venezuela, Bolivares") },
	{ "[$VND]",	N_("Viet Nam, Dong") },
	{ "[$VUV]",	N_("Vanuatu, Vatu") },
	{ "[$WST]",	N_("Samoa, Tala") },
	{ "[$XAF]",	N_("Communauté Financière Africaine BEAC, Francs") },
	{ "[$XAG]",	N_("Silver, Ounces") },
	{ "[$XAU]",	N_("Gold, Ounces") },
	{ "[$XCD]",	N_("East Caribbean Dollars") },
	{ "[$XDR]",	N_("International Monetary Fund (IMF) Special Drawing Rights") },
	{ "[$XOF]",	N_("Communauté Financière Africaine BCEAO, Francs") },
	{ "[$XPD]",	N_("Palladium Ounces") },
	{ "[$XPF]",	N_("Comptoirs Français du Pacifique Francs") },
	{ "[$XPT]",	N_("Platinum, Ounces") },
	{ "[$YER]",	N_("Yemen, Rials") },
	{ "[$YUM]",	N_("Yugoslavia, New Dinars") },
	{ "[$ZAR]",	N_("South Africa, Rand") },
	{ "[$ZMK]",	N_("Zambia, Kwacha") },
	{ "[$ZWD]",	N_("Zimbabwe, Zimbabwe Dollars") },

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
	int cur = -1;

	if (fmt_len < 1)
		return FMT_UNKNOWN;

	/* Check for prepended currency */
	switch (ptr[0]) {
	case '$' : cur = 1; break;
	case '£' : cur = 2; break;
	case '¥' : cur = 3; break;
	case '¤' : cur = 4; break;
	default :
	if (ptr[0] == '[' && ptr[1] == '$') {
		char const * const end = strchr (ptr, ']');

		if (end != NULL && end[1] == ' ') {
			/* FIXME : Look up the correct index */
			info->currency_symbol_index = 1;
			result = FMT_CURRENCY;
			ptr = end + 1;
		} else
			return FMT_UNKNOWN;
	}
	};

	if (cur > 0) {
		info->currency_symbol_index = cur;
		result = FMT_CURRENCY;
		++ptr;
	}

	/* Check for thousands separator */
	if (ptr[0] == '#') {
		if (ptr[1] == ',')
			++ptr;
		else
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
	if (ptr [0] == '.') {
		num_decimals = 0;
		ptr++;
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

	/* We have now handled decimals, and thousands separators */
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
		if (g_strncasecmp (_("[Red]"), ptr, 5) != 0)
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
cell_format_classify (StyleFormat const *sf, FormatCharacteristics *info)
{
	char const *fmt = sf->format;
	FormatFamily res;
	int i;

	g_return_val_if_fail (fmt != NULL, FMT_GENERAL);
	g_return_val_if_fail (info != NULL, FMT_GENERAL);

	/* Init the result to something sane */
	info->thousands_sep = FALSE;
	info->num_decimals = 2;
	info->negative_fmt = 0;
	info->list_element = 0;
	info->currency_symbol_index = 1; /* '$' */
	info->date_has_days = FALSE;
	info->date_has_months = FALSE;

	if (style_format_is_general (sf))
		return FMT_GENERAL;

	/* Can we parse it ? */
	if ((res = cell_format_is_number (fmt, info)) != FMT_UNKNOWN)
		return res;

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
	return FMT_UNKNOWN;
}
