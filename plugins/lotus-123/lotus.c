/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * lotus.c: Lotus 123 support for Gnumeric
 *
 * Authors:
 *    See: README
 *    Michael Meeks (mmeeks@gnu.org)
 *    Stephen Wood (saw@genhomepage.com)
 *    Morten Welinder (terra@gnome.org)
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "lotus.h"
#include "lotus-types.h"
#include "lotus-formula.h"

#include <workbook.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <gutils.h>
#include <mstyle.h>
#include <style-color.h>
#include <parse-util.h>
#include <sheet-object-cell-comment.h>

#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>
#include <string.h>

#define LOTUS_DEBUG 0
#undef DEBUG_RLDB

/* ------------------------------------------------------------------------- */

static const guint16 lmbcs_group_1[256] = {
	0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
	0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
	0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
	0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
	0x00A8, 0x007E, 0x02DA, 0x005E, 0x0060, 0x00B4, 0x201C, 0x0027,
	0x2026, 0x2013, 0x2014, 0x2018, 0x2019, 0x0000, 0x2039, 0x203A,
	0x00A8, 0x007E, 0x02DA, 0x005E, 0x0060, 0x00B4, 0x201E, 0x201A,
	0x201D, 0x2017, 0x0000, 0x00A0, 0x0000, 0xFFFD, 0x0000, 0x0000,
	0x0152, 0x0153, 0x0178, 0x02D9, 0x02DA, 0x0000, 0x255E, 0x255F,
	0x258C, 0x2590, 0x25CA, 0x2318, 0xF8FF, 0xF8FE, 0x2126, 0x0000,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
	0x256A, 0x2561, 0x2562, 0x2556, 0x2555, 0x255C, 0x255B, 0x2567,
	0x0133, 0x0132, 0xF8FD, 0xF8FC, 0x0149, 0x0140, 0x013F, 0x00AF,
	0x02D8, 0x02DD, 0x02DB, 0x02C7, 0x007E, 0x005E, 0x0000, 0x0000,
	0x2020, 0x2021, 0x0126, 0x0127, 0x0166, 0x0167, 0x2122, 0x2113,
	0x014A, 0x014B, 0x0138, 0x0000, 0xF8FB, 0x2310, 0x20A4, 0x20A7,
	0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
	0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
	0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
	0x00FF, 0x00D6, 0x00DC, 0x00F8, 0x00A3, 0x00D8, 0x00D7, 0x0192,
	0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
	0x00BF, 0x00AE, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x00C1, 0x00C2, 0x00C0,
	0x00A9, 0x2563, 0x2551, 0x2557, 0x255D, 0x00A2, 0x00A5, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x00E3, 0x00C3,
	0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x00A4,
	0x00F0, 0x00D0, 0x00CA, 0x00CB, 0x00C8, 0x0131, 0x00CD, 0x00CE,
	0x00CF, 0x2518, 0x250C, 0x2588, 0x2584, 0x00A6, 0x00CC, 0x2580,
	0x00D3, 0x00DF, 0x00D4, 0x00D2, 0x00F5, 0x00D5, 0x00B5, 0x00FE,
	0x00DE, 0x00DA, 0x00DB, 0x00D9, 0x00FD, 0x00DD, 0x00AF, 0x00B4,
	0x00AD, 0x00B1, 0x2017, 0x00BE, 0x00B6, 0x00A7, 0x00F7, 0x00B8,
	0x00B0, 0x00A8, 0x00B7, 0x00B9, 0x00B3, 0x00B2, 0x25A0, 0x00A0
};

static const guint16 lmbcs_group_2[256] = {
	0x0000, 0x037A, 0x0385, 0x03AA, 0x03AB, 0x2015, 0x0384, 0x02BC,
	0x02BD, 0x203E, 0x0000, 0x0000, 0x0000, 0x0000, 0xF862, 0xF863,
	0xF864, 0xF865, 0x21D5, 0x215E, 0x215D, 0x215C, 0x215B, 0xF867,
	0x21D1, 0x21D3, 0x21D2, 0x21D0, 0xF868, 0x21D4, 0xF869, 0xF89F,
	0xF89E, 0xF89D, 0xF89C, 0xF89B, 0xF89A, 0xF899, 0xF898, 0xF897,
	0xF896, 0xF895, 0xF894, 0xF893, 0xF892, 0xF891, 0xF890, 0xF88F,
	0xF88E, 0xF88D, 0xF88C, 0xF88B, 0xF88A, 0xF889, 0xF888, 0xF887,
	0xF886, 0xF885, 0xF884, 0xF883, 0xF882, 0xF881, 0xF880, 0xF866,
	0x2220, 0x2207, 0xF87F, 0xF87E, 0xF87D, 0xF87C, 0xF87B, 0xF87A,
	0xF879, 0xF878, 0xF877, 0xF876, 0xF875, 0xF874, 0xF873, 0xF872,
	0x2202, 0x2135, 0x2111, 0x211C, 0xF871, 0xF870, 0xF86F, 0xF86E,
	0xF86D, 0xF86C, 0xF86B, 0x220B, 0x2208, 0x2209, 0x2286, 0x2287,
	0x2297, 0x2295, 0x2713, 0x22C0, 0x2201, 0x222B, 0x2200, 0x2203,
	0xF86A, 0x2032, 0x2033, 0x221E, 0x221D, 0x03C6, 0x222A, 0x2229,
	0x2261, 0x2245, 0x2265, 0x2264, 0x2320, 0x2321, 0x2260, 0x2248,
	0x2044, 0x2219, 0x2030, 0x221A, 0x207F, 0x2205, 0x2282, 0x2283,
	0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x0386, 0x00E7,
	0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x0388, 0x00C4, 0x0389,
	0x038A, 0x0000, 0x038C, 0x00F4, 0x00F6, 0x038E, 0x00FB, 0x00F9,
	0x038F, 0x00D6, 0x00DC, 0x03AC, 0x00A3, 0x03AD, 0x03AE, 0x03AF,
	0x03CA, 0x0390, 0x03CC, 0x03CD, 0x0391, 0x0392, 0x0393, 0x0394,
	0x0395, 0x0396, 0x0397, 0x00BD, 0x0398, 0x0399, 0x00AB, 0x00BB,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x039A, 0x039B, 0x039C,
	0x039D, 0x2563, 0x2551, 0x2557, 0x255D, 0x039E, 0x039F, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x03A0, 0x03A1,
	0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x03A3,
	0x03A4, 0x03A5, 0x03A6, 0x03A7, 0x03A8, 0x03A9, 0x03B1, 0x03B2,
	0x03B3, 0x2518, 0x250C, 0x2588, 0x2584, 0x03B4, 0x03B5, 0x2580,
	0x03B6, 0x03B7, 0x03B8, 0x03B9, 0x03BA, 0x03BB, 0x03BC, 0x03BD,
	0x03BE, 0x03BF, 0x03C0, 0x03C1, 0x03C3, 0x03C2, 0x03C4, 0x00B4,
	0x00AD, 0x00B1, 0x03C5, 0x03C6, 0x03C7, 0x00A7, 0x03C8, 0x00B8,
	0x00B0, 0x00A8, 0x03C9, 0x03CB, 0x03B0, 0x03CE, 0x25A0, 0x00A0
};

static const guint16 lmbcs_group_3[128] = {
	0x0000, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x0000, 0x2030, 0x0000, 0x2039, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0000, 0x203A, 0x0000, 0x0000, 0x0000, 0x0000,
	0x00A0, 0x0000, 0x00A2, 0x00A3, 0x20AA, 0x00A5, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x0000, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
	0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x0000, 0x00B9, 0x0000, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x0000,
	0x05B0, 0x05B1, 0x05B2, 0x05B3, 0x05B4, 0x05B5, 0x05B6, 0x05B7,
	0x05B8, 0x05B9, 0x0000, 0x05BB, 0x05BC, 0x05BD, 0x05BE, 0x05BF,
	0x05C0, 0x05C1, 0x05C2, 0x05C3, 0x05F0, 0x05F1, 0x05F2, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x05D0, 0x05D1, 0x05D2, 0x05D3, 0x05D4, 0x05D5, 0x05D6, 0x05D7,
	0x05D8, 0x05D9, 0x05DA, 0x05DB, 0x05DC, 0x05DD, 0x05DE, 0x05DF,
	0x05E0, 0x05E1, 0x05E2, 0x05E3, 0x05E4, 0x05E5, 0x05E6, 0x05E7,
	0x05E8, 0x05E9, 0x05EA, 0x0000, 0x0000, 0x200E, 0x200F, 0x0000
};

static const guint16 lmbcs_group_4[128] = {
	0x0000, 0x067E, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0000, 0x2039, 0x0152, 0x0686, 0x0698, 0x0000,
	0x06AF, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x0000, 0x2122, 0x0000, 0x203A, 0x0153, 0x200C, 0x200D, 0x0000,
	0x00A0, 0x060C, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x0000, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
	0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x00B9, 0x061B, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x061F,
	0x0000, 0x0621, 0x0622, 0x0623, 0x0624, 0x0625, 0x0626, 0x0627,
	0x0628, 0x0629, 0x062A, 0x062B, 0x062C, 0x062D, 0x062E, 0x062F,
	0x0630, 0x0631, 0x0632, 0x0633, 0x0634, 0x0635, 0x0636, 0x00D7,
	0x0637, 0x0638, 0x0639, 0x063A, 0x0640, 0x0641, 0x0642, 0x0643,
	0x00E0, 0x0644, 0x00E2, 0x0645, 0x0646, 0x0647, 0x0648, 0x00E7,
	0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x0649, 0x064A, 0x00EE, 0x00EF,
	0x064B, 0x064C, 0x064D, 0x064E, 0x00F4, 0x064F, 0x0650, 0x00F7,
	0x0651, 0x00F9, 0x0652, 0x00FB, 0x00FC, 0x200E, 0x200F, 0x0000
};

static const guint16 lmbcs_group_5[128] = {
	0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
	0x0000, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
	0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
	0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
	0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
	0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
	0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
	0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
	0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
	0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
	0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F
};

static const guint16 lmbcs_group_6[256] = {
	0x0000, 0x0101, 0x0108, 0x0109, 0x010A, 0x010B, 0x0112, 0x0113,
	0x0116, 0x0117, 0x011C, 0x011D, 0x0120, 0x0121, 0x0122, 0x0123,
	0x0124, 0x0125, 0x0128, 0x0129, 0x012A, 0x012B, 0x012E, 0x012F,
	0x0134, 0x0135, 0x0136, 0x0137, 0x013B, 0x013C, 0x0145, 0x0146,
	0x014C, 0x014D, 0x0156, 0x0157, 0x015C, 0x015D, 0x0168, 0x0169,
	0x016A, 0x016B, 0x016C, 0x016D, 0x0172, 0x0173, 0x0100, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x016F, 0x0107, 0x00E7,
	0x0142, 0x00EB, 0x0150, 0x0151, 0x00EE, 0x0179, 0x00C4, 0x0106,
	0x00C9, 0x0139, 0x013A, 0x00F4, 0x00F6, 0x013D, 0x013E, 0x015A,
	0x015B, 0x00D6, 0x00DC, 0x0164, 0x0165, 0x0141, 0x00D7, 0x010D,
	0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x0104, 0x0105, 0x017D, 0x017E,
	0x0118, 0x0119, 0x00AC, 0x017A, 0x010C, 0x015F, 0x00AB, 0x00BB,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x00C1, 0x00C2, 0x011A,
	0x015E, 0x2563, 0x2551, 0x2557, 0x255D, 0x017B, 0x017C, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x0102, 0x0103,
	0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x00A4,
	0x0111, 0x0110, 0x010E, 0x00CB, 0x010F, 0x0147, 0x00CD, 0x00CE,
	0x011B, 0x2518, 0x250C, 0x2588, 0x2584, 0x0162, 0x016E, 0x2580,
	0x00D3, 0x00DF, 0x00D4, 0x0143, 0x0144, 0x0148, 0x0160, 0x0161,
	0x0154, 0x00DA, 0x0155, 0x0170, 0x00FD, 0x00DD, 0x0163, 0x00B4,
	0x00AD, 0x02DD, 0x02DB, 0x02C7, 0x02D8, 0x00A7, 0x00F7, 0x00B8,
	0x00B0, 0x00A8, 0x02D9, 0x0171, 0x0158, 0x0159, 0x25A0, 0x00A0,
};

static const guint16 lmbcs_group_8[128] = {
	0x0000, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x0000, 0x0000,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x0000, 0x0178,
	0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
	0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
	0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
	0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
	0x011E, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
	0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x0130, 0x015E, 0x00DF,
	0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
	0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
	0x011F, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
	0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x0131, 0x015F, 0x00FF
};

static const guint16 lmbcs_group_b[128] = {
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2026, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x00A0, 0x0E01, 0x0E02, 0x0E03, 0x0E04, 0x0E05, 0x0E06, 0x0E07,
	0x0E08, 0x0E09, 0x0E0A, 0x0E0B, 0x0E0C, 0x0E0D, 0x0E0E, 0x0E0F,
	0x0E10, 0x0E11, 0x0E12, 0x0E13, 0x0E14, 0x0E15, 0x0E16, 0x0E17,
	0x0E18, 0x0E19, 0x0E1A, 0x0E1B, 0x0E1C, 0x0E1D, 0x0E1E, 0x0E1F,
	0x0E20, 0x0E21, 0x0E22, 0x0E23, 0x0E24, 0x0E25, 0x0E26, 0x0E27,
	0x0E28, 0x0E29, 0x0E2A, 0x0E2B, 0x0E2C, 0x0E2D, 0x0E2E, 0x0E2F,
	0x0E30, 0x0E31, 0x0E32, 0x0E33, 0x0E34, 0x0E35, 0x0E36, 0x0E37,
	0x0E38, 0x0E39, 0x0E3A, 0x0000, 0x0000, 0x0000, 0x0000, 0x0E3F,
	0x0E40, 0x0E41, 0x0E42, 0x0E43, 0x0E44, 0x0E45, 0x0E46, 0x0E47,
	0x0E48, 0x0E49, 0x0E4A, 0x0E4B, 0x0E4C, 0x0E4D, 0x0E4E, 0x0E4F,
	0x0E50, 0x0E51, 0x0E52, 0x0E53, 0x0E54, 0x0E55, 0x0E56, 0x0E57,
	0x0E58, 0x0E59, 0x0E5A, 0x0E5B, 0x0000, 0x0000, 0x0000, 0x0000
};

static const guint16 lmbcs_group_f[256] = {
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
	0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
	0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
	0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
	0x007F, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0xF8FA, 0xF8F9, 0xF8F8, 0xF8F7,
	0xF8F6, 0xF8F5, 0xF8F4, 0xF8F3, 0xF8F2, 0xF8F1, 0xF8F0, 0xF8EF,
	0xF8EE, 0xF8ED, 0xF8EC, 0xF8EB, 0xF8EA, 0xF8E9, 0xF8E8, 0xF8E7,
	0x0000, 0x0000, 0x0000, 0xF8E6, 0xF8E5, 0xF8E4, 0xF8E3, 0xF8E2,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0xF8E1, 0xF8E0, 0xF8DF, 0xF8DE, 0xF8DD, 0xF8DC, 0xF8DB, 0xF8DA,
	0xF8D9, 0xF8D8, 0xF8D7, 0xF8D6, 0xF8D5, 0xF8D4, 0xF8D3, 0xF8D2,
	0xF8D1, 0xF8D0, 0xF8CF, 0xF8CE, 0xF8CD, 0xF8CC, 0xF8CB, 0xF8CA,
	0xF8C9, 0xF8C8, 0xF8C7, 0xF8C6, 0xF8C5, 0xF8C4, 0xF8C3, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

/* ------------------------------------------------------------------------- */

static const guint8
lotus_color_table[240][3] = {
	{ 255, 255, 255 },		/* white		*/
	{ 255, 239, 206 },		/* vanilla		*/
	{ 255, 255, 194 },		/* parchment		*/
	{ 255, 255, 208 },		/* ivory		*/
	{ 224, 255, 191 },		/* pale green		*/
	{ 224, 255, 223 },		/* sea mist		*/
	{ 224, 255, 255 },		/* ice blue		*/
	{ 194, 239, 255 },		/* powder blue		*/
	{ 224, 241, 255 },		/* arctic blue		*/
	{ 224, 224, 255 },		/* lilac mist		*/
	{ 232, 224, 255 },		/* purple wash		*/
	{ 241, 224, 255 },		/* violet frost		*/
	{ 255, 224, 255 },		/* seashell		*/
	{ 255, 224, 245 },		/* rose pearl		*/
	{ 255, 224, 230 },		/* pale cherry		*/
	{ 255, 255, 255 },		/* white		*/
	{ 255, 225, 220 },		/* blush		*/
	{ 255, 225, 176 },		/* sand			*/
	{ 255, 255, 128 },		/* light yellow		*/
	{ 241, 241, 180 },		/* honeydew		*/
	{ 194, 255, 145 },		/* celery		*/
	{ 193, 255, 213 },		/* pale aqua		*/
	{ 193, 255, 255 },		/* pale blue		*/
	{ 161, 226, 255 },		/* crystal blue		*/
	{ 192, 225, 255 },		/* light cornflower	*/
	{ 191, 191, 255 },		/* pale lavender	*/
	{ 210, 191, 255 },		/* grape fizz		*/
	{ 225, 191, 255 },		/* pale plum		*/
	{ 255, 193, 253 },		/* pale pink		*/
	{ 255, 192, 228 },		/* pale rose		*/
	{ 255, 192, 206 },		/* rose quartz		*/
	{ 247, 247, 247 },		/* 5% gray		*/
	{ 255, 192, 182 },		/* red sand		*/
	{ 255, 194, 129 },		/* buff			*/
	{ 255, 255, 53 },		/* lemon		*/
	{ 241, 241, 128 },		/* pale lemon lime	*/
	{ 128, 255, 128 },		/* mint green		*/
	{ 130, 255, 202 },		/* pastel green		*/
	{ 128, 255, 255 },		/* pastel blue		*/
	{ 130, 224, 255 },		/* sapphire		*/
	{ 130, 192, 255 },		/* cornflower		*/
	{ 159, 159, 255 },		/* light lavender	*/
	{ 194, 159, 255 },		/* pale purple		*/
	{ 226, 159, 255 },		/* light orchid		*/
	{ 255, 159, 255 },		/* pink orchid		*/
	{ 255, 159, 207 },		/* apple blossom	*/
	{ 255, 159, 169 },		/* pink coral		*/
	{ 239, 239, 239 },		/* 10% gray		*/
	{ 255, 159, 159 },		/* light salmon		*/
	{ 255, 159, 113 },		/* light peach		*/
	{ 255, 255, 0 },		/* yellow		*/
	{ 224, 224, 116 },		/* avocado		*/
	{ 65, 255, 50 },		/* leaf green		*/
	{ 66, 255, 199 },		/* light aqua		*/
	{ 66, 255, 255 },		/* light turquoise  	*/
	{ 0, 191, 255 },		/* light cerulean	*/
	{ 82, 145, 239 },		/* azure		*/
	{ 128, 128, 255 },		/* lavender		*/
	{ 192, 130, 255 },		/* light purple		*/
	{ 224, 129, 255 },		/* dusty violet		*/
	{ 255, 127, 255 },		/* pink			*/
	{ 255, 130, 194 },		/* pastel pink		*/
	{ 255, 130, 160 },		/* pastel red		*/
	{ 225, 225, 225 },		/* 15% gray		*/
	{ 255, 128, 128 },		/* salmon		*/
	{ 255, 129, 65 },		/* peach		*/
	{ 255, 225, 24 },		/* mustard		*/
	{ 225, 225, 64 },		/* lemon lime		*/
	{ 0, 255, 0 },			/* neon green		*/
	{ 0, 255, 178 },		/* aqua			*/
	{ 0, 255, 255 },		/* turquoise		*/
	{ 0, 161, 224 },		/* cerulean		*/
	{ 33, 129, 255 },		/* wedgwood		*/
	{ 97, 129, 255 },		/* heather		*/
	{ 161, 96, 255 },		/* purple haze		*/
	{ 192, 98, 255 },		/* orchid		*/
	{ 255, 95, 255 },		/* flamingo		*/
	{ 255, 96, 175 },		/* cherry pink		*/
	{ 255, 96, 136 },		/* red coral		*/
	{ 210, 210, 210 },		/* 20% gray  (Windows)	*/
	{ 255, 64, 64 },		/* dark salmon		*/
	{ 255, 66, 30 },		/* dark peach		*/
	{ 255, 191, 24 },		/* gold			*/
	{ 225, 225, 0 },		/* yellow green		*/
	{ 0, 225, 0 },			/* light green		*/
	{ 0, 225, 173 },		/* caribbean		*/
	{ 0, 224, 224 },		/* dark pastel blue	*/
	{ 0, 130, 191 },		/* dark cerulean	*/
	{ 0, 128, 255 },		/* manganese blue	*/
	{ 65, 129, 255 },		/* lilac		*/
	{ 130, 66, 255 },		/* purple		*/
	{ 193, 64, 255 },		/* light red violet	*/
	{ 255, 66, 249 },		/* light magenta	*/
	{ 255, 64, 160 },		/* rose			*/
	{ 255, 64, 112 },		/* carnation pink	*/
	{ 192, 192, 192 },		/* 25% gray		*/
	{ 255, 31, 53 },		/* watermelon		*/
	{ 255, 31, 16 },		/* tangerine		*/
	{ 255, 129, 0 },		/* orange		*/
	{ 191, 191, 0 },		/* chartreuse		*/
	{ 0, 194, 0 },			/* green		*/
	{ 0, 193, 150 },		/* teal			*/
	{ 0, 193, 194 },		/* dark turquoise	*/
	{ 65, 129, 192 },		/* light slate blue	*/
	{ 0, 98, 225 },			/* medium blue		*/
	{ 65, 65, 255 },		/* dark lilac		*/
	{ 66, 0, 255 },			/* royal purple		*/
	{ 194, 0, 255 },		/* fuchsia		*/
	{ 255, 34, 255 },		/* confetti pink	*/
	{ 245, 43, 151 },		/* pale burgundy	*/
	{ 255, 34, 89 },		/* strawberry		*/
	{ 178, 178, 178 },		/* 30% gray		*/
	{ 224, 31, 37 },		/* rouge		*/
	{ 225, 32, 0 },			/* burnt orange		*/
	{ 226, 98, 0 },			/* dark orange		*/
	{ 161, 161, 0 },		/* light olive		*/
	{ 0, 160, 0 },			/* kelly green		*/
	{ 0, 159, 130 },		/* sea green		*/
	{ 0, 128, 128 },		/* aztec blue		*/
	{ 0, 96, 160 },			/* dusty blue		*/
	{ 0, 65, 194 },			/* blueberry		*/
	{ 0, 33, 191 },			/* violet         	*/
	{ 65, 0, 194 },			/* deep purple		*/
	{ 129, 0, 255 },		/* red violet		*/
	{ 255, 0, 255 },		/* hot pink		*/
	{ 255, 0, 128 },		/* dark rose		*/
	{ 255, 0, 65 },			/* poppy red		*/
	{ 162, 162, 162 },		/* 35% gray		*/
	{ 194, 0, 0 },			/* crimson		*/
	{ 255, 0, 0 },			/* red			*/
	{ 191, 65, 0 },			/* light brown		*/
	{ 128, 128, 0 },		/* olive		*/
	{ 0, 128, 0 },			/* dark green		*/
	{ 0, 130, 80 },			/* dark teal		*/
	{ 0, 96, 98 },			/* spruce		*/
	{ 0, 64, 128 },			/* slate blue		*/
	{ 0, 31, 226 },			/* navy blue		*/
	{ 64, 64, 194 },		/* blue violet		*/
	{ 64, 0, 162 },			/* amethyst		*/
	{ 96, 0, 161 },			/* dark red violet	*/
	{ 224, 0, 224 },		/* magenta		*/
	{ 223, 0, 127 },		/* light burgundy	*/
	{ 194, 0, 65 },			/* cherry red		*/
	{ 143, 143, 143 },		/* 40% gray		*/
	{ 160, 0, 0 },			/* dark crimson		*/
	{ 225, 0, 0 },			/* dark red		*/
	{ 161, 63, 0 },			/* hazelnut		*/
	{ 98, 98, 0 },			/* dark olive		*/
	{ 0, 96, 0 },			/* emerald		*/
	{ 0, 96, 60 },			/* malachite		*/
	{ 0, 64, 65 },			/* dark spruce		*/
	{ 0, 47, 128 },			/* steel blue		*/
	{ 0, 0, 255 },			/* blue			*/
	{ 32, 32, 160 },		/* iris			*/
	{ 34, 0, 161 },			/* grape		*/
	{ 64, 0, 128 },			/* plum			*/
	{ 161, 0, 159 },		/* dark magenta		*/
	{ 192, 0, 127 },		/* burgundy		*/
	{ 159, 0, 15 },			/* cranberry		*/
	{ 128, 128, 128 },		/* 50% gray		*/
	{ 96, 0, 0 },			/* mahogany		*/
	{ 194, 18, 18 },		/* brick		*/
	{ 130, 66, 0 },			/* dark brown		*/
	{ 66, 66, 0 },			/* deep olive		*/
	{ 0, 66, 0 },			/* dark emerald		*/
	{ 0, 64, 35 },			/* evergreen		*/
	{ 0, 50, 63 },			/* baltic blue		*/
	{ 0, 32, 96 },			/* blue denim		*/
	{ 0, 32, 194 },			/* cobalt blue		*/
	{ 34, 34, 192 },		/* dark iris		*/
	{ 0, 0, 128 },			/* midnight		*/
	{ 31, 0, 127 },			/* dark plum		*/
	{ 128, 0, 128 },		/* plum red		*/
	{ 130, 0, 64 },			/* dark burgundy	*/
	{ 128, 0, 0 },			/* scarlet		*/
	{ 95, 95, 95 },			/* 60% gray		*/
	{ 64, 0, 0 },			/* chestnut		*/
	{ 161, 31, 18 },		/* terra cotta		*/
	{ 96, 66, 0 },			/* umber		*/
	{ 33, 33, 0 },			/* amazon		*/
	{ 0, 33, 0 },			/* peacock green	*/
	{ 0, 32, 31 },			/* pine			*/
	{ 0, 32, 65 },			/* seal blue		*/
	{ 0, 32, 79 },			/* dark slate blue	*/
	{ 0, 0, 224 },			/* royal blue		*/
	{ 0, 0, 161 },			/* lapis		*/
	{ 0, 0, 97 },			/* dark grape		*/
	{ 31, 0, 98 },			/* aubergine		*/
	{ 64, 0, 95 },			/* dark plum red	*/
	{ 98, 0, 66 },			/* raspberry		*/
	{ 98, 0, 18 },			/* deep scarlet		*/
	{ 79, 79, 79 },			/* 70% gray		*/
	{ 32, 0, 0 },			/* burnt sienna		*/
	{ 98, 33, 0 },			/* milk chocolate	*/
	{ 65, 32, 0 },			/* burnt umber		*/
	{ 16, 16, 0 },			/* deep avocado		*/
	{ 0, 16, 0 },			/* deep forest		*/
	{ 0, 18, 12 },			/* dark pine		*/
	{ 0, 18, 31 },			/* dark metallic blue	*/
	{ 0, 16, 64 },			/* air force blue	*/
	{ 0, 0, 194 },			/* ultramarine		*/
	{ 0, 0, 175 },			/* prussian blue	*/
	{ 0, 0, 79 },			/* raisin		*/
	{ 0, 0, 64 },			/* eggplant		*/
	{ 32, 0, 66 },			/* boysenberry		*/
	{ 64, 0, 64 },			/* bordeaux		*/
	{ 65, 0, 18 },			/* ruby			*/
	{ 64, 64, 64 },			/* 75% gray		*/
	{ 208, 177, 161 },		/* red gray		*/
	{ 224, 161, 117 },		/* tan			*/
	{ 210, 176, 106 },		/* khaki		*/
	{ 192, 194, 124 },		/* putty		*/
	{ 130, 193, 104 },		/* bamboo green		*/
	{ 129, 192, 151 },		/* green gray		*/
	{ 127, 194, 188 },		/* baltic gray		*/
	{ 113, 178, 207 },		/* blue gray		*/
	{ 177, 177, 210 },		/* rain cloud		*/
	{ 159, 159, 224 },		/* lilac gray		*/
	{ 192, 161, 224 },		/* light purple gray	*/
	{ 226, 159, 222 },		/* light mauve		*/
	{ 239, 145, 235 },		/* light plum gray	*/
	{ 226, 159, 200 },		/* light burgundy gray	*/
	{ 241, 143, 188 },		/* rose gray		*/
	{ 47, 47, 47 },			/* 80% gray		*/
	{ 127, 96, 79 },		/* dark red gray	*/
	{ 161, 98, 82 },		/* dark tan		*/
	{ 128, 98, 16 },		/* safari		*/
	{ 130, 130, 63 },		/* olive gray		*/
	{ 63, 98, 31 },			/* jade			*/
	{ 60, 97, 62 },			/* dark green gray	*/
	{ 55, 96, 94 },			/* spruce gray		*/
	{ 16, 65, 96 },			/* dark blue gray	*/
	{ 66, 66, 130 },		/* atlantic gray	*/
	{ 98, 96, 161 },		/* dark lilac gray	*/
	{ 98, 65, 129 },		/* purple gray		*/
	{ 96, 49, 129 },		/* mauve		*/
	{ 96, 33, 98 },			/* plum gray		*/
	{ 98, 33, 82 },			/* burgundy gray	*/
	{ 129, 63, 98 },		/* dark rose gray	*/
	{ 0, 0, 0 }			/* black		*/
};

static GnmColor *
lotus_color (guint i)
{
	if (i > G_N_ELEMENTS (lotus_color_table))
		return NULL;
	return style_color_new_i8 (lotus_color_table[i][0],
				   lotus_color_table[i][1],
				   lotus_color_table[i][2]);
}

/* ------------------------------------------------------------------------- */

typedef struct _LotusRLDB LotusRLDB;

struct _LotusRLDB {
	int refcount;
	LotusRLDB *top;
	int ndims;

	int rll;
	int rest;

	/* Used only at top level */
	int *dims;  /* Reversed from file format, e.g., sheets*cols*rows */
	guint16 pending_id;
	GHashTable *definitions;

	/* Used only when ndims > 0 */
	GPtrArray *lower;

	/* Used only when ndims == 0 */
	GString *datanode;
};

static void
lotus_rldb_unref (LotusRLDB *rldb)
{
	int i;

	if (rldb->refcount-- > 1)
		return;

	if (rldb->lower) {
		for (i = rldb->lower->len; --i >= 0; ) {
			LotusRLDB *subrldb = g_ptr_array_index (rldb->lower, i);
			lotus_rldb_unref (subrldb);
		}
		g_ptr_array_free (rldb->lower, TRUE);
	}

	g_free (rldb->dims);

	if (rldb->datanode)
		g_string_free (rldb->datanode, TRUE);

	if (rldb->definitions)
		g_hash_table_destroy (rldb->definitions);

	g_free (rldb);
}

static void
lotus_rldb_ref (LotusRLDB *rldb)
{
	rldb->refcount++;
}

static LotusRLDB *
lotus_rldb_new (int ndims, const int *dims, LotusRLDB *top)
{
	LotusRLDB *res = g_new0 (LotusRLDB, 1);

	if (!top) {
		top = res;
		res->dims = g_memdup (dims, ndims * sizeof (*dims));
		res->definitions = g_hash_table_new_full
			(g_direct_hash,
			 g_direct_equal,
			 NULL,
			 (GDestroyNotify)lotus_rldb_unref);
	}

	res->refcount = 1;
	res->top = top;
	res->ndims = ndims;
	if (ndims > 0) {
		res->lower = g_ptr_array_new ();
		res->rest = top->dims[top->ndims - ndims];
	}

	return res;
}

static gboolean
lotus_rldb_full (LotusRLDB *rldb)
{
	return rldb->rest == 0;
}

static LotusRLDB *
lotus_rldb_open_child (LotusRLDB *rldb)
{
	LotusRLDB *last;

	if (rldb->ndims == 0 || rldb->lower->len == 0)
		return NULL;

	last = g_ptr_array_index (rldb->lower, rldb->lower->len - 1);
	return lotus_rldb_full (last) ? NULL : last;
}

static void
lotus_rldb_repeat (LotusRLDB *rldb, int rll)
{
	LotusRLDB *child;

	g_return_if_fail (rll > 0);
	g_return_if_fail (rldb->ndims > 0);

	child = lotus_rldb_open_child (rldb);
	if (child) {
		lotus_rldb_repeat (child, rll);
	} else {
#ifdef DEBUG_RLDB
		g_print ("%*sRepeat %d.\n", 3 - rldb->ndims, "", rll);
#endif
		g_return_if_fail (rll <= rldb->rest);
		child = lotus_rldb_new (rldb->ndims - 1,
					NULL,
					rldb->top);
		child->rll = rll;
		g_ptr_array_add (rldb->lower, child);
		if (rldb->top->pending_id) {
			lotus_rldb_ref (child);
			g_hash_table_insert (rldb->top->definitions,
					     GUINT_TO_POINTER ((guint)rldb->top->pending_id),
					     child);
			rldb->top->pending_id = 0;
		}
	}

	if (lotus_rldb_full (child)) {
		rldb->rest -= child->rll;
#ifdef DEBUG_RLDB
		g_print ("%*sNow %d left.\n", 3 - rldb->ndims, "",
			 rldb->rest);
#endif
	}
}

static void
lotus_rldb_data (LotusRLDB *rldb, gconstpointer p, size_t l)
{
	while (rldb->ndims > 0) {
		g_return_if_fail (rldb->lower->len > 0);
		rldb = g_ptr_array_index (rldb->lower, rldb->lower->len - 1);
		g_return_if_fail (rldb != NULL);
	}

	g_return_if_fail (rldb->datanode == NULL);
	rldb->datanode = g_string_sized_new (l);
	g_string_append_len (rldb->datanode, p, l);
}

static void
lotus_rldb_register_id (LotusRLDB *rldb, guint16 id)
{
	g_return_if_fail (id > 0);
	g_return_if_fail (rldb->pending_id == 0);

#ifdef DEBUG_RLDB
	g_print ("Defining id %d.\n", id);
#endif
	rldb->pending_id = id;
}

static void
lotus_rldb_use_id (LotusRLDB *rldb, guint16 id)
{
	LotusRLDB *child;

	child = lotus_rldb_open_child (rldb);
	if (child) {
		lotus_rldb_use_id (child, id);
	} else {
		child = g_hash_table_lookup (rldb->top->definitions,
					     GUINT_TO_POINTER ((guint)id));
		g_return_if_fail (child != NULL);
		g_return_if_fail (lotus_rldb_full (child));
#ifdef DEBUG_RLDB
		g_print ("Using id %d.\n", id);
#endif
		lotus_rldb_ref (child);
		g_ptr_array_add (rldb->lower, child);
	}

	if (lotus_rldb_full (child)) {
		rldb->rest -= child->rll;
#ifdef DEBUG_RLDB
		g_print ("%*sNow %d left.\n", 3 - rldb->ndims, "",
			 rldb->rest);
#endif
	}
}

typedef void (*LotusRLDB_3D_Handler) (LotusState *state,
				      const GnmSheetRange *r,
				      const guint8 *data,
				      size_t len);

static void
lotus_rldb_walk_3d (LotusRLDB *rldb3,
		    LotusState *state,
		    LotusRLDB_3D_Handler handler)
{
	int sheetcount = workbook_sheet_count (state->wb);
	int sno, srll;
	guint si, ri, ci;
	LotusRLDB *rldb2, *rldb1, *rldb0;
	GnmSheetRange r;
	const GString *data;

	g_return_if_fail (rldb3->ndims == 3);

	rldb2 = NULL;
	si = 0;
	srll = 0;
	for (sno = 0; sno < sheetcount; sno++) {
		if (srll == 0) {
			if (si >= rldb3->lower->len)
				break;
			rldb2 = g_ptr_array_index (rldb3->lower, si);
			si++;
			srll = rldb2->rll;
		}
		r.sheet = lotus_get_sheet (state->wb, sno);
		srll--;

		ci = 0;
		for (r.range.start.col = 0;
		     r.range.start.col < SHEET_MAX_COLS;
		     r.range.start.col = r.range.end.col + 1) {
			if (ci >= rldb2->lower->len)
				break;
			rldb1 = g_ptr_array_index (rldb2->lower, ci);
			ci++;
			r.range.end.col =
				MIN (SHEET_MAX_COLS - 1,
				     r.range.start.col + (rldb1->rll - 1));

			ri = 0;
			for (r.range.start.row = 0;
			     r.range.start.row < SHEET_MAX_ROWS;
			     r.range.start.row = r.range.end.row + 1) {
				if (ri >= rldb1->lower->len)
					break;
				rldb0 = g_ptr_array_index (rldb1->lower, ri);
				ri++;
				r.range.end.row =
					MIN (SHEET_MAX_ROWS - 1,
					     r.range.start.row + (rldb0->rll - 1));

				data = rldb0->datanode;
				handler (state, &r,
					 data ? data->str : NULL,
					 data ? data->len : 0);
			}
		}
	}
}

static void
lotus_set_style_cb (LotusState *state, const GnmSheetRange *r,
		    const guint8 *data, size_t len)
{
	guint sid;
	GnmStyle *style;

	if (len < 2)
		return;

	sid = GSF_LE_GET_GUINT16 (data);
	g_print ("Got style %d for %s!%s",
		 sid,
		 r->sheet->name_unquoted,
		 cellpos_as_string (&r->range.start));
	g_print (":%s\n",
		 cellpos_as_string (&r->range.end));

	style = g_hash_table_lookup (state->style_pool,
				     GUINT_TO_POINTER (sid));
	g_return_if_fail (style != NULL);

	gnm_style_ref (style);

	sheet_apply_style (r->sheet, &r->range, style);
}

static void
lotus_set_formats_cb (LotusState *state, const GnmSheetRange *r,
		      const guint8 *data, size_t len)
{
}

static void
lotus_set_borders_cb (LotusState *state, const GnmSheetRange *r,
		      const guint8 *data, size_t len)
{
}

static void
lotus_rldb_apply (LotusRLDB *rldb, int type, LotusState *state)
{
	switch (type) {
	case LOTUS_RLDB_STYLES:
		lotus_rldb_walk_3d (rldb, state, lotus_set_style_cb);
		break;

	case LOTUS_RLDB_FORMATS:
		lotus_rldb_walk_3d (rldb, state, lotus_set_formats_cb);
		break;

	case LOTUS_RLDB_BORDERS:
		lotus_rldb_walk_3d (rldb, state, lotus_set_borders_cb);
		break;

	default:
		g_assert_not_reached ();
	}
}

/* ------------------------------------------------------------------------- */

static const char *const
lotus_special_formats[16] = {
	"",
	"",
	"d-mmm-yy",
	"d-mmm",
	"mmm yy",
	"",
	"",
	"h:mm:ss AM/PM",			/* Need am/pm */
	"h:mm",
	"m/d/yy",
	"d/m/yy",
	"",
	"",
	"",
	"",
	""
};

static void
append_zeros (char *s, int n) {

	if (n > 0) {
		s = s + strlen (s);
		*s++ = '.';
		while (n--)
			*s++ = '0';
		*s = 0;
	}
}

static void
cell_set_format_from_lotus_format (GnmCell *cell, int fmt)
{
	int fmt_type  = (fmt >> 4) & 0x7;
	int precision = fmt&0xf;
	char fmt_string[100];

	switch (fmt_type) {

	case 0:			/* Float */
		strcpy (fmt_string, "0");
		append_zeros (fmt_string, precision);
		break;
	case 1:			/* Scientific */
		strcpy (fmt_string, "0");
		append_zeros (fmt_string, precision);
		strcat (fmt_string, "E+00");
		break;
	case 2:			/* Currency */
		strcpy (fmt_string, "$#,##0"); /* Should not force $ */
		append_zeros (fmt_string, precision);
		strcat (fmt_string, "_);[Red]($#,##0");
		append_zeros (fmt_string, precision);
		strcat (fmt_string, ")");
		break;
	case 3:			/* Float */
		strcpy (fmt_string, "0");
		append_zeros (fmt_string, precision);
		strcat (fmt_string, "%");
		break;
	case 4:			/* Comma */
		strcpy (fmt_string, "#,##0"); /* Should not force $ */
		append_zeros (fmt_string, precision);
		break;

	case 7:			/* Lotus special format */
		strcpy (fmt_string,  lotus_special_formats[precision]);
		break;

	default:
		strcpy (fmt_string, "");
		break;

	}
	if (fmt_string[0])
		cell_set_format (cell, fmt_string);
#if LOTUS_DEBUG > 0
	printf ("Format: %s\n", fmt_string);
#endif
}

typedef struct {
	GsfInput *input;
	guint16   type;
	guint16   len;
	guint8 const *data;
} record_t;


static void
report_record_size_error (LotusState *state, record_t *r)
{
	g_warning ("Record with type 0x%x has wrong length %d.",
		   r->type, r->len);
	/* FIXME: mark the error in the state instead. */
}

#define CHECK_RECORD_SIZE(cond)				\
    if (!(r->len cond)) {				\
	    report_record_size_error (state, r);	\
	    break;					\
    } else


static guint16
record_peek_next (record_t *r)
{
	guint8 const *header;
	guint16 type;

	g_return_val_if_fail (r != NULL, LOTUS_EOF);

	header = gsf_input_read (r->input, 2, NULL);
	if (header == NULL)
		return 0xffff;
	type = GSF_LE_GET_GUINT16 (header);
	gsf_input_seek (r->input, -2, G_SEEK_CUR);
	return type;
}

static gboolean
record_next (record_t *r)
{
	guint8 const *header;

	g_return_val_if_fail (r != NULL, FALSE);

	header = gsf_input_read (r->input, 4, NULL);
	if (header == NULL)
		return FALSE;

	r->type = GSF_LE_GET_GUINT16 (header);
	r->len  = GSF_LE_GET_GUINT16 (header + 2);

	r->data = (r->len == 0
		   ? (void *)""
		   : gsf_input_read (r->input, r->len, NULL));

#if LOTUS_DEBUG > 0
	g_print ("Record 0x%x length 0x%x\n", r->type, r->len);
	if (r->data)
		gsf_mem_dump (r->data, r->len);
#endif

	return (r->data != NULL);
}

static GnmCell *
insert_value (Sheet *sheet, guint32 col, guint32 row, GnmValue *val)
{
	GnmCell *cell;

	g_return_val_if_fail (val != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);

	cell = sheet_cell_fetch (sheet, col, row);

	cell_set_value (cell, val);

#if LOTUS_DEBUG > 0
	printf ("Inserting value at %s:\n",
		cell_name (cell));
	value_dump (val);
#endif
	return cell;
}

static Sheet *
attach_sheet (Workbook *wb, int idx)
{
	/*
	 * Yes, I do mean col_name.  Use that as an easy proxy for
	 * naming the sheets similarly to lotus.
	 */
	Sheet *sheet = sheet_new (wb, col_name (idx));

	/*
	 * In case nothing forces a spanning, do it here so that any new
	 * content will get spanned.
	 */
	sheet_flag_recompute_spans (sheet);

	workbook_sheet_attach (wb, sheet);

	return sheet;
}

static gboolean
lotus_read_old (LotusState *state, record_t *r)
{
	gboolean result = TRUE;
	int sheetidx = 0;
	GnmCell    *cell;
	GnmValue	*v;
	guint16  fmt;	/* Format code of Lotus Cell */

	do {
		switch (r->type) {
		case LOTUS_BOF:
			state->sheet = attach_sheet (state->wb, sheetidx++);
			break;

		case LOTUS_EOF:
			state->sheet = NULL;
			break;

		case LOTUS_INTEGER: {
			GnmValue *v = value_new_int (GSF_LE_GET_GINT16 (r->data + 5));
			int i = GSF_LE_GET_GUINT16 (r->data + 1);
			int j = GSF_LE_GET_GUINT16 (r->data + 3);
			fmt = *(guint8 *)(r->data);

			cell = insert_value (state->sheet, i, j, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
		case LOTUS_NUMBER: {
			GnmValue *v = value_new_float (gsf_le_get_double (r->data + 5));
			int i = GSF_LE_GET_GUINT16 (r->data + 1);
			int j = GSF_LE_GET_GUINT16 (r->data + 3);
			fmt = *(guint8 *)(r->data);

			cell = insert_value (state->sheet, i, j, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
		case LOTUS_LABEL: {
			/* one of '\', '''', '"', '^' */
/*			gchar format_prefix = *(r->data + 1 + 4);*/
			GnmValue *v = lotus_new_string (r->data + 6);
			int i = GSF_LE_GET_GUINT16 (r->data + 1);
			int j = GSF_LE_GET_GUINT16 (r->data + 3);
			cell = insert_value (state->sheet, i, j, v);
			if (cell) {
				fmt = *(guint8 *)(r->data);
				cell_set_format_from_lotus_format (cell, fmt);
			}
			break;
		}
		case LOTUS_FORMULA: {
			/* 5-12 = value */
			/* 13-14 = formula r->length */
			if (r->len >= 15) {
				int col = GSF_LE_GET_GUINT16 (r->data + 1);
				int row = GSF_LE_GET_GUINT16 (r->data + 3);
				int len = GSF_LE_GET_GINT16 (r->data + 13);
				GnmExpr const *expr;
				GnmParsePos pp;

				fmt = r->data[0];

#if DEBUG
				puts (cell_coord_name (col, row));
				gsf_mem_dump (r->data+5,8);
#endif
				if (r->len < (15+len))
					break;

				pp.eval.col = col;
				pp.eval.row = row;
				pp.sheet = state->sheet;
				pp.wb = pp.sheet->workbook;
				expr = lotus_parse_formula (state, &pp,
							    r->data + 15, len);

				v = NULL;
				if (0x7ff0 == (GSF_LE_GET_GUINT16 (r->data + 11) & 0x7ff8)) {
					/* I cannot find normative definition
					 * for when this is an error, an when
					 * a string, so we cheat, and peek
					 * at the next record.
					 */
					if (LOTUS_STRING == record_peek_next (r)) {
						record_next (r);
						v = lotus_new_string (r->data + 5);
					} else
						v = value_new_error_VALUE (NULL);
				} else
					v = value_new_float (gsf_le_get_double (r->data + 5));
				cell = sheet_cell_fetch (state->sheet, col, row);
				cell_set_expr_and_value (cell, expr, v, TRUE);

				gnm_expr_unref (expr);
				cell_set_format_from_lotus_format (cell, fmt);
			}
			break;
		}

		default:
			break;
		}
	} while (record_next (r));

	return result;
}

Sheet *
lotus_get_sheet (Workbook *wb, int i)
{
	g_return_val_if_fail (i >= 0 && i <= 255, NULL);

	while (i >= workbook_sheet_count (wb))
		workbook_sheet_add (wb, -1, FALSE);

	return workbook_sheet_by_index (wb, i);
}

char *
lotus_get_lmbcs (const char *data, int maxlen)
{
	GString *res = g_string_sized_new (maxlen + 2);
	const guint8 *p;
	const guint8 *theend;

	p = (const guint8 *)data;
	if (maxlen == -1)
		maxlen = strlen (data);
	theend = p + maxlen;

	while (p < theend) {
		switch (p[0]) {
		case 0:
			theend = p;
			break;

		case 0x01: {
			gunichar uc = lmbcs_group_1[p[1]];
			if (uc)
				g_string_append_unichar (res, uc);
			p += 2;
			break;
		}

		case 0x02: {
			gunichar uc = lmbcs_group_2[p[1]];
			if (uc)
				g_string_append_unichar (res, uc);
			p += 2;
			break;
		}

		case 0x03: {
			guint8 c = p[1];
			if (c >= 0x80) {
				gunichar uc = lmbcs_group_3[c - 0x80];
				if (uc)
					g_string_append_unichar (res, uc);
			}
			p += 2;
			break;
		}

		case 0x04: {
			guint8 c = p[1];
			if (c >= 0x80) {
				gunichar uc = lmbcs_group_4[c - 0x80];
				if (uc)
					g_string_append_unichar (res, uc);
			}
			p += 2;
			break;
		}

		case 0x05: {
			guint8 c = p[1];
			if (c >= 0x80) {
				gunichar uc = lmbcs_group_5[c - 0x80];
				if (uc)
					g_string_append_unichar (res, uc);
			}
			p += 2;
			break;
		}

		case 0x06: {
			gunichar uc = lmbcs_group_6[p[1]];
			if (uc)
				g_string_append_unichar (res, uc);
			p += 2;
			break;
		}

		case 0x08: {
			guint8 c = p[1];
			if (c >= 0x80) {
				gunichar uc = lmbcs_group_8[c - 0x80];
				if (uc)
					g_string_append_unichar (res, uc);
			}
			p += 2;
			break;
		}

		case 0x0b: {
			guint8 c = p[1];
			if (c >= 0x80) {
				gunichar uc = lmbcs_group_b[c - 0x80];
				if (uc)
					g_string_append_unichar (res, uc);
			}
			p += 2;
			break;
		}

		case 0x0f: {
			gunichar uc = lmbcs_group_f[p[1]];
			if (uc)
				g_string_append_unichar (res, uc);
			p += 2;
			break;
		}

		case 0x07: case 0x0c: case 0x0e: {
			unsigned code = (p[0] << 8) | p[1];
			g_warning ("Unhandled character 0x%04x", code);
			p += 2;
			break;
		}

		case 0x10: case 0x11: case 0x12: case 0x13:
		case 0x15: case 0x16: case 0x17: {
			unsigned code = (p[0] << 16) | (p[1] << 8) | p[2];
			g_warning ("Unhandled character 0x%06x", code);
			p += 3;
			/* See http://www.batutis.com/i18n/papers/lmbcs/ */
			break;
		}

		case 0x18: case 0x19: case 0x1a: case 0x1b:
		case 0x1c: case 0x1d: case 0x1e: case 0x1f:
			/* Ignore two bytes.  */
			p += 2;
			break;

		case 0x14: {
			/* Big-endian two-byte unicode with private-
			   use-area filled in by something.  */
			gunichar uc = (p[1] << 8) | p[2];
			if (uc >= 0xe000 && uc <= 0xf8ff) {
				g_warning ("Unhandled character 0x14%04x", uc);
			} else
				g_string_append_unichar (res, uc);
			p += 3;
			break;
		}

		default:
			if (p[0] <= 0x7f) {
				g_string_append_c (res, *p++);
			} else {
				/* Assume default group is 1.  */
				gunichar uc = lmbcs_group_1[*p++];
				if (uc)
					g_string_append_unichar (res, uc);
			}
			break;
		}
	}

	return g_string_free (res, FALSE);
}


static char *
lotus_get_cstr (const record_t *r, int ofs)
{
	if (ofs >= r->len)
		return NULL;
	else
		return lotus_get_lmbcs (r->data + ofs, r->len - ofs);
}

GnmValue *
lotus_new_string (gchar const *data)
{
	return value_new_string_nocopy
		(lotus_get_lmbcs (data, strlen (data)));
}

double
lotus_unpack_number (guint32 u)
{
	double v = (u >> 6);

	if (u & 0x20) v = 0 - v;
	if (u & 0x10)
		v = v / gnm_pow10 (u & 15);
	else
		v = v * gnm_pow10 (u & 15);

	return v;
}

static GnmValue *
get_lnumber (const record_t *r, int ofs)
{
	const guint8 *p;
	g_return_val_if_fail (ofs + 8 <= r->len, NULL);

	p = r->data + ofs;

	/* FIXME: Some special values indicate ERR, NA, BLANK, and string.  */

	if (1) {
		double v = gsf_le_get_double (p);
		return value_new_float (v);
	}
}


static gboolean
lotus_read_new (LotusState *state, record_t *r)
{
	gboolean result = TRUE;
	int sheetnameno = 0;
	gboolean bigrll = (state->version >= LOTUS_VERSION_123SS98);
	int rllsize = bigrll ? 4 : 2;
	LotusRLDB *rldb = NULL;
	int rldb_type = 0;

	state->style_pool = g_hash_table_new_full
		(g_direct_hash,
		 g_direct_equal,
		 NULL,
		 (GDestroyNotify)gnm_style_unref);

	do {
		switch (r->type) {
		case LOTUS_BOF:
			break;

		case LOTUS_EOF:
			goto done;

		case LOTUS_ERRCELL: CHECK_RECORD_SIZE (>= 4) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *v = value_new_error_VALUE (NULL);
			(void)insert_value (sheet, col, row, v);
			break;
		}

		case LOTUS_NACELL: CHECK_RECORD_SIZE (>= 4) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *v = value_new_error_NA (NULL);
			(void)insert_value (sheet, col, row, v);
			break;
		}

		case LOTUS_LABEL2: CHECK_RECORD_SIZE (>= 6) {
			/* one of '\', '''', '"', '^' */
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
/*			gchar format_prefix = *(r->data + ofs + 4);*/
			GnmValue *v = lotus_new_string (r->data + 5);
			(void)insert_value (sheet, col, row, v);
			break;
		}

		case LOTUS_NUMBER2: CHECK_RECORD_SIZE (>= 12) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *v = get_lnumber (r, 4);
			(void)insert_value (sheet, col, row, v);
			break;
		}

		case LOTUS_STYLE: CHECK_RECORD_SIZE (>= 2) {
			guint16 subtype = GSF_LE_GET_GUINT16 (r->data);
			switch (subtype) {
			case 0xfa1: CHECK_RECORD_SIZE (>= 24) {
				/* Text style.  */
				guint16 styleid = GSF_LE_GET_GUINT16 (r->data + 2);
				guint8 fontid = GSF_LE_GET_GUINT8 (r->data + 9);
				guint16 fontsize = GSF_LE_GET_GUINT16 (r->data + 10);
				/* (FontSize * 100 / 83 + 16 ) / 32. */
				guint16 fg = GSF_LE_GET_GUINT16 (r->data + 12);
				guint16 bg = GSF_LE_GET_GUINT16 (r->data + 14);
				guint16 facebits = GSF_LE_GET_GUINT16 (r->data + 16);
				guint16 facemask = GSF_LE_GET_GUINT16 (r->data + 18);
				guint8 halign = GSF_LE_GET_GUINT8 (r->data + 20);
				guint8 valign = GSF_LE_GET_GUINT8 (r->data + 21);
				guint16 angle = GSF_LE_GET_GUINT16 (r->data + 22);
				break;
			}

			case 0xfd2: CHECK_RECORD_SIZE (>= 24) {
				GnmStyle *style;
				GnmColor *color;

				/* Cell style.  */
				guint16 styleid = GSF_LE_GET_GUINT16 (r->data + 2);
				guint8 fontid = GSF_LE_GET_GUINT8 (r->data + 9);
				guint16 fontsize = GSF_LE_GET_GUINT16 (r->data + 10);
				/* (FontSize * 100 / 83 + 16 ) / 32. */
				guint16 textfg = GSF_LE_GET_GUINT16 (r->data + 12);
				guint16 textbg = GSF_LE_GET_GUINT16 (r->data + 14);
				guint16 facebits = GSF_LE_GET_GUINT16 (r->data + 16);
				guint16 facemask = GSF_LE_GET_GUINT16 (r->data + 18);
				guint8 halign = GSF_LE_GET_GUINT8 (r->data + 20);
				guint8 valign = GSF_LE_GET_GUINT8 (r->data + 21);
				guint16 angle = GSF_LE_GET_GUINT16 (r->data + 22);
				guint16 intfg = GSF_LE_GET_GUINT16 (r->data + 24);
				guint16 intbg = GSF_LE_GET_GUINT16 (r->data + 26);
				guint8 intpat = GSF_LE_GET_GUINT8 (r->data + 28);

				style = gnm_style_new ();
				gnm_style_set_pattern (style, 1);

				color = lotus_color (intbg);
				if (color)
					gnm_style_set_back_color (style, color);

				color = lotus_color (intfg);
				if (color)
					gnm_style_set_font_color (style, color);

				g_hash_table_insert (state->style_pool,
						     GUINT_TO_POINTER ((guint)styleid),
						     style);
				break;
			}

			default:
				g_print ("Unknown style record 0x%x/%04x of length %d.\n",
					 r->type, subtype,
					 r->len);

			case 0xfab: /* Edge style */
			case 0xfb4: /* Interior style */
			case 0xfc9: /* Frame style */
			case 0xfdc: /* Fontname style */
			case 0xfe6: /* Named style */
			case 0xffa: /* Style pool */
				break;
			}
			break;
		}

		case LOTUS_PACKED_NUMBER: CHECK_RECORD_SIZE (== 8) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			double v = lotus_unpack_number (GSF_LE_GET_GUINT32 (r->data + 4));
			GnmValue *val;

			if (v == gnm_floor (v) &&
			    v >= G_MININT &&
			    v <= G_MAXINT)
				val = value_new_int ((int)v);
			else
				val = value_new_float (v);

			(void)insert_value (sheet, col, row, val);
			break;
		}

		case LOTUS_SHEET_NAME: CHECK_RECORD_SIZE (>= 11) {
			Sheet *sheet = lotus_get_sheet (state->wb, sheetnameno++);
			char *name = lotus_get_cstr (r, 10);
			g_return_val_if_fail (name != NULL, FALSE);
			/* Name is followed by something indicating tab colour.  */
			g_object_set (sheet, "name", name, NULL);
			g_free (name);
			break;
		}

		case LOTUS_NAMED_SHEET:
			/*
			 * Compare LOTUS_SHEET_NAME.  It is unclear why there
			 * are two such records.
			 */
			break;

		case LOTUS_FORMULA2: CHECK_RECORD_SIZE (>= 13) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *curval = get_lnumber (r, 4);
			GnmParsePos pp;
			const GnmExpr *expr;
			GnmCell *cell;

			pp.eval.col = col;
			pp.eval.row = row;
			pp.sheet = sheet;
			pp.wb = sheet->workbook;
			expr = lotus_parse_formula (state, &pp,
						    r->data + 12, r->len - 12);
			cell = sheet_cell_fetch (sheet, col, row);
			cell_set_expr_and_value (cell, expr, curval, TRUE);

			gnm_expr_unref (expr);
			break;
		}

		case LOTUS_CA_DB:
		case LOTUS_DEFAULTS_DB:
		case LOTUS_NAMED_STYLE_DB:
			break;

		case LOTUS_RLDB_PACKINFO: CHECK_RECORD_SIZE (>= 4 + rllsize) {
			int ndims = GSF_LE_GET_GUINT16 (r->data + 2);
			int *dims, i;

			if (rldb) {
				g_warning ("Unused rldb.");
				lotus_rldb_unref (rldb);
				rldb = NULL;
			}

			CHECK_RECORD_SIZE (== 4 + rllsize * ndims);
			if (ndims < 1 || ndims > 3) {
				g_warning ("Ignoring %dd rldb.", ndims);
				break;
			}

			dims = g_new (int, ndims);
			for (i = 0; i < ndims; i++) {
				gconstpointer p = r->data + 4 + i * rllsize;
				dims[ndims - 1 - i] = bigrll
					? GSF_LE_GET_GUINT32 (p)
					: GSF_LE_GET_GUINT16 (p);
			}

			rldb = lotus_rldb_new (ndims, dims, NULL);
			g_free (dims);
			break;
		}

		case LOTUS_RLDB_NODE: CHECK_RECORD_SIZE (>= rllsize) {
			guint32 rll = bigrll
				? GSF_LE_GET_GUINT32 (r->data)
				: GSF_LE_GET_GUINT16 (r->data);

			if (!rldb) {
				g_warning ("Ignoring stray RLDB_NODE");
				break;				
			}

			lotus_rldb_repeat (rldb, rll);
			break;
		}

		case LOTUS_RLDB_DATANODE: /* No length check needed.  */ {
			lotus_rldb_data (rldb, r->data, r->len);
			break;
		}

		case LOTUS_RLDB_REGISTERID: CHECK_RECORD_SIZE (== 2) {
			guint16 id = GSF_LE_GET_GUINT16 (r->data);
			lotus_rldb_register_id (rldb, id);
			break;
		}

		case LOTUS_RLDB_USEREGISTEREDID: CHECK_RECORD_SIZE (== 2) {
			guint16 id = GSF_LE_GET_GUINT16 (r->data);
			lotus_rldb_use_id (rldb, id);
			break;
		}

		case LOTUS_RLDB_FORMATS:
		case LOTUS_RLDB_BORDERS:
		case LOTUS_RLDB_STYLES:
			if (rldb_type == 0)
				rldb_type = r->type;
			else if (rldb && rldb_type == r->type) {
				lotus_rldb_apply (rldb, rldb_type, state);
				lotus_rldb_unref (rldb);
				rldb_type = 0;
				rldb = NULL;
			} else
				g_warning ("Unordered style info.");
			break;

		case LOTUS_RLDB_DEFAULTS:
		case LOTUS_RLDB_NAMEDSTYLES:
		case LOTUS_RLDB_COLWIDTHS:
		case LOTUS_RLDB_ROWHEIGHTS:
		case LOTUS_RL2DB:
		case LOTUS_RL3DB:
			/* Style database related.  */
			break;

		case LOTUS_DOCUMENT_1:
		case LOTUS_DOCUMENT_2:
			break;

		case LOTUS_PRINT_SETTINGS:
		case LOTUS_PRINT_STRINGS:
			break;

		case LOTUS_LARGE_DATA:
			g_warning ("Unhandled \"large data\" record seen.");
			break;

		case LOTUS_CELL_COMMENT: CHECK_RECORD_SIZE (>=6) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			char *text = lotus_get_cstr (r, 5);
			GnmCellPos pos;

			pos.col = col;
			pos.row = row;
			cell_set_comment (sheet, &pos, NULL, text);
			g_free (text);			
			break;
		}

		default:
			g_print ("Unknown record 0x%x of length %d.\n", r->type, r->len);

		case LOTUS_CALCORDER:
		case LOTUS_USER_RANGE:
		case LOTUS_ZEROFORCE:
		case LOTUS_SORTKEY_DIR:
		case LOTUS_DTLABELMISC:
		case LOTUS_CPA:
		case LOTUS_PERSISTENT_ID:
		case LOTUS_WINDOW:
		case LOTUS_BEGIN_OBJECT:
		case LOTUS_END_OBJECT:
		case LOTUS_BEGIN_GROUP:
		case LOTUS_END_GROUP:
		case LOTUS_DOCUMENT_WINDOW:
		case LOTUS_OBJECT_SELECT:
		case LOTUS_OBJECT_NAME_INDEX:
		case LOTUS_STYLE_MANAGER_BEGIN:
		case LOTUS_STYLE_MANAGER_END:
		case LOTUS_WORKBOOK_VIEW:
		case LOTUS_SPLIT_MANAGEMENT:
		case LOTUS_SHEET_OBJECT_ID:
		case LOTUS_SHEET:
		case LOTUS_SHEET_VIEW:
		case LOTUS_FIRST_WORKSHEET:
		case LOTUS_SHEET_PROPS:
		case LOTUS_RESERVED_288:
		case LOTUS_SCRIPT_STREAM:
		case LOTUS_RANGE_REGION:
		case LOTUS_RANGE_MISC:
		case LOTUS_RANGE_ALIAS:
		case LOTUS_DATA_FILL:
		case LOTUS_BACKSOLVER:
		case LOTUS_SORT_HEADER:
		case LOTUS_CELL_EOF:
		case LOTUS_FILE_PREFERENCE:
		case LOTUS_END_DATA:
			break;
		}
	} while (record_next (r));

 done:
	/*
	 * Newer formats have something that looks like document
	 * properties after the EOF record.
	 */

	if (workbook_sheet_count (state->wb) < 1)
		result = FALSE;

	if (rldb) {
		g_warning ("Unfinished rldb.");
		lotus_rldb_unref (rldb);
		rldb = NULL;
	}

	g_hash_table_destroy (state->style_pool);
	state->style_pool = NULL;

	return result;
}

gboolean
lotus_read (LotusState *state)
{
	record_t r;
	r.input = state->input;

	if (record_next (&r) && r.type == LOTUS_BOF) {
		state->version = GSF_LE_GET_GUINT16 (r.data);
		g_print ("Version=%x\n", state->version);
		switch (state->version) {
		case LOTUS_VERSION_ORIG_123:
		case LOTUS_VERSION_SYMPHONY:
			return lotus_read_old (state, &r);

		default:
			g_warning ("Unexpected version %x", state->version);
			/* Fall through.  */
		case LOTUS_VERSION_123V6:
		case LOTUS_VERSION_123SS98:
			return lotus_read_new (state, &r);
		}
	}

	return FALSE;
}
