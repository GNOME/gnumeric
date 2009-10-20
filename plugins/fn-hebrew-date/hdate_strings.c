/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * hdate_strings.c: convert libhdate internal numbers to readable hebrew strings.
 *
 * Author:
 *   Yaacov Zamir <kzamir@walla.co.il>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <gnm-i18n.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hdate.h"

#define MAX_HNUMBER_SIZE 25

/**
 @brief convert an integer to hebrew string UTF-8 (logical)
 
 @param n The int to convert
 @attention ( 0 < n < 10000)
 @warning uses a static string, so output should be copied away.
*/
void
hdate_int_to_hebrew (GString *res, int n)
{
	int oldlen = res->len;
	int length;
	static const char *digits[3][10] = {
		{" ", "א", "ב", "ג", "ד", "ה", "ו", "ז", "ח", "ט"},
		{"ט", "י", "כ", "ל", "מ", "נ", "ס", "ע", "פ", "צ"},
		{" ", "ק", "ר", "ש", "ת"}
	};

	/* sanity checks */
	if (n < 1 || n > 10000)
	{
		return;
	}

	if (n >= 1000)
	{
		g_string_append (res, digits[0][n / 1000]);
		n %= 1000;
	}
	while (n >= 400)
	{
		g_string_append (res, digits[2][4]);
		n -= 400;
	}
	if (n >= 100)
	{
		g_string_append (res, digits[2][n / 100]);
		n %= 100;
	}
	if (n >= 10)
	{
		if (n == 15 || n == 16)
			n -= 9;
		g_string_append (res, digits[1][n / 10]);
		n %= 10;
	}
	if (n > 0)
		g_string_append (res, digits[0][n]);

	length = g_utf8_strlen (res->str + oldlen, -1);

	/* add the ' and " to hebrew numbers */
	if (length <= 2)
		g_string_append_c (res, '\'');
	else
		g_string_insert_c (res, length - 2, '"');
}

/**
 @brief Return a static string, with name of hebrew month.

 @param month The number of the month 0..13 (0 - tishre, 12 - adar 1, 13 - adar 2).
 @warning uses a static string, so output should be copied away.
*/
const char *
hdate_get_hebrew_month_name (int month)
{
	static const char *heb_months[] = {
		N_("Tishre"), N_("Heshvan"), N_("Kislev"),
		N_("Tevet"), N_("Shvat"), N_("Adar"),
		N_("Nisan"), N_("Eyar"), N_("Sivan"), ("Tamuz"),
		N_("Av"), N_("Elul"), N_("Adar I"), N_("Adar II")
	};

	if (month < 0 || month > 13)
		return NULL;

	return heb_months[month];
}

/**
 @brief Return a static string, with name of hebrew month in hebrew.

 @param month The number of the month 0..13 (0 - tishre, 12 - adar 1, 13 - adar 2).
 @warning uses a static string, so output should be copied away.
*/
const char *
hdate_get_hebrew_month_name_heb (int month)
{
	static const char *h_heb_months[] = {
		N_("תשרי"), N_("חשון"), N_("כסלו"),
		N_("טבת"), N_("שבט"), N_("אדר"),
		N_("ניסן"), N_("איר"), N_("סיון"), N_("תמוז"),
		N_("אב"), N_("אלול"), N_("אדר א"), N_("אדר ב")
	};

	if (month < 0 || month > 13)
		return NULL;

	return h_heb_months[month];
}
