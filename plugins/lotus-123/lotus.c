/**
 * lotus.c: Lotus 123 support for Gnumeric
 *
 * Authors:
 *    See: README
 *    Michael Meeks (mmeeks@gnu.org)
 *    Stephen Wood (saw@genhomepage.com)
 *    Morten Welinder (terra@gnome.org)
 *
 * Docs are scarce.
 * https://www.mettalogic.co.uk/tim/l123/l123r4.html
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
#include <ranges.h>
#include <mstyle.h>
#include <style-color.h>
#include <style-font.h>
#include <parse-util.h>
#include <sheet-style.h>
#include <sheet-object-cell-comment.h>
#include <sheet-view.h>
#include <selection.h>

#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-msole-utils.h>
#include <string.h>

#define LOTUS_DEBUG 0
#undef DEBUG_RLDB
#undef DEBUG_STYLE
#undef DEBUG_FORMAT
#undef DEBUG_COLROW

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

static guint16 lmbcs_group_12[128][256];
static GIConv lmbcs_12_iconv;

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
	{ 66, 255, 255 },		/* light turquoise	*/
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
	{ 0, 33, 191 },			/* violet		*/
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
	if (i < G_N_ELEMENTS (lotus_color_table))
		return gnm_color_new_rgb8 (lotus_color_table[i][0],
					     lotus_color_table[i][1],
					     lotus_color_table[i][2]);
	switch (i) {
	case 240:
		g_warning ("Unhandled \"3D face\" color.");
		return NULL;
	case 241:
		g_warning ("Unhandled \"highlight\" color.");
		return NULL;
	case 242:
		g_warning ("Unhandled \"button shadow\" color.");
		return NULL;
	case 243:
		g_warning ("Unhandled \"window background\" color.");
		return NULL;
	case 244:
		g_warning ("Unhandled \"window text\" color.");
		return NULL;
	default:
		g_warning ("Unhandled color id %d.", i);
		return NULL;
	case 0xffff:
		return NULL;
	}
}

/* ------------------------------------------------------------------------- */

static gint8
lotus_pattern_table[74] = {
	0,	/* 0	Transparent */
	-1,	/* 1	SolidForeground */
	1,	/* 2	SolidBackground */
	-1,	/* 3	DoubleRightHatch */
	-1,	/* 4	TripleRightHatch */
	-1,	/* 5	DoubleCoarseRightHatch */
	-1,	/* 6	SingleCoarseRightHatch */
	-1,	/* 7	SingleRightHatch */
	15,	/* 8	BroadRightHatch */
	-1,	/* 9	VertHatch */
	-1,	/* 10	VertCoarseHatch */
	-1,	/* 11	DoubleLeftHatch */
	-1,	/* 12	TripleLeftHatch */
	-1,	/* 13	DoubleCoarseLeftHatch */
	-1,	/* 14	SingleCoarseLeftHatch */
	-1,	/* 15	HorizHatch */
	-1,	/* 16	HorizCoarseHatch */
	-1,	/* 17	CrossHatch */
	-1,	/* 18	CoarseCrossHatch */
	-1,	/* 19	WideCrossHatch */
	-1,	/* 20	FineCrossHatch */
	-1,	/* 21	SquareHatch */
	-1,	/* 22	CoarseSquareHatch */
	-1,	/* 23	DottedSquareHatch */
	-1,	/* 24	WideSquareHatch */
	-1,	/* 25	GrayScaleDarkest */
	4,	/* 26	GrayScale3rdDarkest */
	3,	/* 27	GrayScale4thDarkest */
	-1,	/* 28	GrayScale5thDarkest */
	-1,	/* 29	GrayScale6thDarkest */
	-1,	/* 30	GrayScale5thLightest */
	-1,	/* 31	GrayScale4thLightest */
	-1,	/* 32	GrayScale3rdLightest */
	-1,	/* 33	GrayScale2ndLightest */
	-1,	/* 34	GrayScaleLightest */
	-1,	/* 35	Confetti */
	-1,	/* 36	RunningBricks */
	-1,	/* 37	DiagonalBricks */
	-1,	/* 38	RoundStones */
	-1,	/* 39	Fountains */
	-1,	/* 40	Pavers */
	-1,	/* 41	Waves */
	-1,	/* 42	Flags */
	-1,	/* 43	BowlingBalls */
	-1,	/* 44	Diamonds */
	-1,	/* 45	Checkers */
	-1,	/* 46	FineCheckers */
	-1,	/* 47	Zigzags */
	-1,	/* 48	Mountains */
	-1,	/* 49	BroadLeftHatch */
	-1,	/* 50	NarrowLeftHatch */
	7,	/* 51	DoubleVertStripes */
	-1,	/* 52	FineSquareHatch */
	-1,	/* 53	DarkCoarseCrossHatch */
	-1,	/* 54	DoubleHorizStripes */
	8,	/* 55	DarkSingleLeftHatch */
	-1,	/* 56	DarkSingleRightHatch */
	-1,	/* 57	SingleLeftHatch */
	-1,	/* 58	SingleNarrowLeftHatch */
	-1,	/* 59	GrayScale2ndDarkest */
	-1,	/* 60	LongHorizCheckers */
	-1,	/* 61	TallVertCheckers */
	-1,	/* 62	QuadrupleDarkHorizStripes */
	-1,	/* 63	QuadrupleDarkVertStripes */
	-1,	/* 64	GradientFillVertFG2BG	Generated By Code */
	-1,	/* 65	GradientFillVertBG2FG	Generated By Code */
	-1,	/* 66	GradientFillHorizFG2BG	Generated By Code */
	-1,	/* 67	GradientFillHorizBG2FG	Generated By Code */
	-1,	/* 68	GradientFillFSlashFG2BG	Generated By Code */
	-1,	/* 69	GradientFillFSlashBG2FG	Generated By Code */
	-1,	/* 70	GradientFillBSlashFG2BG	Generated By Code */
	-1,	/* 71	GradientFillBSlashBG2FG	Generated By Code */
	-1,	/* 72	QuadrupleGrayVertStripes */
	-1	/* 73	QuadrupleGrayHorizStripes */
};

static int
lotus_pattern (guint i)
{
	int p;
	if (i < G_N_ELEMENTS (lotus_pattern_table))
		p = lotus_pattern_table[i];
	else
		p = -1;

	if (p == -1 && i != 0xff)
		g_warning ("Unhandled pattern %d.", i);
	return p;
}

/* ------------------------------------------------------------------------- */

static char const * const
lotus_special_formats[16] = {
	"",
	"General",
	"d-mmm-yy",
	"d-mmm",
	"mmm yy",
	"",
	";;;",  /* Hidden */
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
append_precision (GString *res, guint n)
{
	static char const dotzeros[17] = ".0000000000000000";
	if (n > 0)
		g_string_append_len (res, dotzeros, n + 1);
}

static char *
lotus_format_string (guint fmt)
{
	guint fmt_type  = (fmt >> 4) & 7;
	guint precision = fmt & 0xf;
	GString *res = g_string_new (NULL);

	switch (fmt_type) {
	case 0:
		/* Float */
		g_string_append (res, "0");
		append_precision (res, precision);
		break;
	case 1:
		/* Scientific */
		g_string_append (res, "0");
		append_precision (res, precision);
		g_string_append (res, "E+00");
		break;
	case 2:
		/* Currency */
		g_string_append (res, "$#,##0"); /* Should not force $ */
		append_precision (res, precision);
		g_string_append (res, "_);[Red]($#,##0");
		append_precision (res, precision);
		g_string_append (res, ")");
		break;
	case 3:
		/* Percent */
		g_string_append (res, "0");
		append_precision (res, precision);
		g_string_append (res, "%");
		break;
	case 4:
		/* Comma */
		g_string_append (res, "#,##0"); /* Should not force $ */
		append_precision (res, precision);
		break;

	case 6:
		/* Country, whatever that is.  */
		g_warning ("Country format used.");
		break;

	case 7: {
		/* Lotus special format */
		char const *f = lotus_special_formats[precision];
		if (f[0] == 0)
			f = "General";
		g_string_append (res, f);
		break;
	}

	default:
		g_warning ("Unknown format type %d used.", fmt_type);
		break;

	}

	return g_string_free (res, FALSE);
}

static void
range_set_format_from_lotus_format (Sheet *sheet,
				    int scol, int srow,
				    int ecol, int erow,
				    guint fmt)
{
	char *fmt_string = lotus_format_string (fmt);
	if (fmt_string[0]) {
		GnmRange r;
		GnmStyle *mstyle = gnm_style_new ();
		gnm_style_set_format_text (mstyle, fmt_string);
		range_init (&r, scol, srow, ecol, erow);
		sheet_style_apply_range (sheet, &r, mstyle);
	}
#ifdef DEBUG_FORMAT
	g_printerr ("Format: %s\n", fmt_string);
#endif
	g_free (fmt_string);
}

static void
cell_set_format_from_lotus_format (GnmCell *cell, guint frmt)
{
	range_set_format_from_lotus_format
		(cell->base.sheet,
		 cell->pos.col, cell->pos.row,
		 cell->pos.col, cell->pos.row,
		 frmt);
}

/* ------------------------------------------------------------------------- */

static double
lotus_fontsize_to_pts (int fontsize)
{
	double size = (fontsize * 100 / 83.0 + 16 ) / 32;

	/* Round to nearest half point.  */
	size = floor (size * 2 + 0.5) / 2;

	return size;
}

static double
lotus_twips_to_points (guint twips)
{
	return ((twips * 100.0) + (44 * 20)) / (87 * 20);
}

static double
lotus_qmps_to_points (guint qmps)
{
	return ((qmps * 100.0) + (44 * 256)) / (87 * 256);
}

/* ------------------------------------------------------------------------- */

typedef struct {
	GsfInput *input;
	guint16   type;
	guint16   len;
	guint8 const *data;
} record_t;

static gboolean lotus_read_works (LotusState *state, record_t *r);

static GnmValue *lotus_get_strval (const record_t *r, int ofs, int def_group);

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

	if (r->len) {
		r->data = gsf_input_read (r->input, r->len, NULL);
		if (!r->data) {
			g_printerr ("Truncated record.  File is probably corrupted.\n");
			r->len = 0;
		}
	} else
		r->data = NULL;

#if LOTUS_DEBUG > 0
	g_printerr ("Record 0x%x length 0x%x\n", r->type, r->len);
	if (r->data)
		gsf_mem_dump (r->data, r->len);
#endif

	return TRUE;
}

/* ------------------------------------------------------------------------- */

static GnmValue *
lotus_lnumber (const record_t *r, int ofs)
{
	const guint8 *p;
	g_return_val_if_fail (ofs + 8 <= r->len, NULL);

	p = r->data + ofs;

	/* FIXME: Some special values indicate ERR, NA, BLANK, and string.  */

	return value_new_float (gsf_le_get_double (p));
}

GnmValue *
lotus_unpack_number (guint32 u)
{
	gnm_float v = (u >> 6);

	if (u & 0x20) v = 0 - v;
	if (u & 0x10)
		return value_new_float (v / gnm_pow10 (u & 15));
	else
		return value_new_float (v * gnm_pow10 (u & 15));
}

GnmValue *
lotus_smallnum (signed int d)
{
	if (d & 1) {
		static int factors[8] = {
			5000, 500, -20, -200, -2000, -20000, -16, -64
		};
		int f = factors[(d >> 1) & 7];
		int mant = (d >> 4);
		return (f > 0)
			? value_new_int (f * mant)
			: value_new_float ((gnm_float)mant / -f);
	} else
		return value_new_int (d >> 1);
}

static GnmValue *
lotus_extfloat (guint64 mant, guint16 signexp)
{
	int exp = (signexp & 0x7fff) - 16383;
	int sign = (signexp & 0x8000) ? -1 : 1;
	/*
	 * NOTE: if gnm_float is "double", then passing the first argument
	 * to gnm_ldexp will perform rounding from 64-bit integer to
	 * 53-bit mantissa in a double.
	 *
	 * NOTE: the gnm_ldexp may under- or overflow.  That ought to do
	 * the right thing.
	 */
	return value_new_float (sign * gnm_ldexp (mant, exp - 63));
}

GnmValue *
lotus_load_treal (gconstpointer p)
{
	const guint8 *pc = p;

	if (pc[9] == 0xff && pc[8] == 0xff) {
		switch (pc[7]) {
		case 0x00: return value_new_empty ();
		case 0xc0: return value_new_error_VALUE (NULL);
		case 0xd0: return value_new_error_NA (NULL);
		case 0xe0: return value_new_string (""); // Supplied in FORMULASTRING
		}
	}

	return lotus_extfloat (GSF_LE_GET_GUINT64 (p),
			       GSF_LE_GET_GUINT16 (pc + 8));
}

static GnmValue *
lotus_treal (const record_t *r, int ofs)
{
	g_return_val_if_fail (ofs + 10 <= r->len, NULL);

	return lotus_load_treal (r->data + ofs);
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
#ifdef DEBUG_RLDB
		int i;
		g_printerr ("New %dd (", ndims);
		for (i = 0; i < ndims; i++)
			g_printerr ("%s%d", (i ? " x " : ""), dims[i]);
		g_printerr (") rldb.\n");
#endif
		top = res;
		res->dims = go_memdup_n (dims, ndims, sizeof (*dims));
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
		g_printerr ("%*sRepeat %d.\n", rldb->top->ndims - rldb->ndims, "", rll);
#endif
		if (rll > rldb->rest) {
			g_warning ("Got rll of %d when only %d left.",
				   rll, rldb->rest);
			rll = rldb->rest;
		}
		child = lotus_rldb_new (rldb->ndims - 1,
					NULL,
					rldb->top);
		child->rll = rll;
		g_ptr_array_add (rldb->lower, child);
		if (rldb->top->pending_id) {
#ifdef DEBUG_RLDB
			g_printerr ("%*sMapped id %d to child with rll %d.\n",
				 rldb->top->ndims - rldb->ndims, "",
				 rldb->top->pending_id, rll);
#endif
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
		g_printerr ("%*sNow %d left.\n", 3 - rldb->ndims, "",
			 rldb->rest);
#endif
	}
}

static void
lotus_rldb_data (LotusRLDB *rldb, gconstpointer p, size_t l)
{
	g_return_if_fail (rldb->pending_id == 0);
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
	g_printerr ("Defining id %d.\n", id);
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
		g_printerr ("%*sUsing id %d.\n", rldb->top->ndims - rldb->ndims, "",
			 id);
#endif
		lotus_rldb_ref (child);
		g_ptr_array_add (rldb->lower, child);
	}

	if (lotus_rldb_full (child)) {
		rldb->rest -= child->rll;
#ifdef DEBUG_RLDB
		g_printerr ("%*sNow %d left.\n", rldb->top->ndims - rldb->ndims, "",
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
		     r.range.start.col < gnm_sheet_get_max_cols (r.sheet);
		     r.range.start.col = r.range.end.col + 1) {
			if (ci >= rldb2->lower->len)
				break;
			rldb1 = g_ptr_array_index (rldb2->lower, ci);
			ci++;
			r.range.end.col =
				MIN (gnm_sheet_get_last_col (r.sheet),
				     r.range.start.col + (rldb1->rll - 1));

			ri = 0;
			for (r.range.start.row = 0;
			     r.range.start.row < gnm_sheet_get_max_rows (r.sheet);
			     r.range.start.row = r.range.end.row + 1) {
				if (ri >= rldb1->lower->len)
					break;
				rldb0 = g_ptr_array_index (rldb1->lower, ri);
				ri++;
				r.range.end.row =
					MIN (gnm_sheet_get_last_row (r.sheet),
					     r.range.start.row + (rldb0->rll - 1));

				data = rldb0->datanode;
				handler (state, &r,
					 data ? data->str : NULL,
					 data ? data->len : 0);
			}
		}
	}
}

typedef void (*LotusRLDB_2D_Handler) (LotusState *state,
				      Sheet *sheet, int start, int end,
				      const guint8 *data,
				      size_t len);

static void
lotus_rldb_walk_2d (LotusRLDB *rldb2,
		    LotusState *state,
		    gboolean iscol,
		    LotusRLDB_2D_Handler handler)
{
	int sheetcount = workbook_sheet_count (state->wb);
	int sno, srll;
	guint si, cri;
	LotusRLDB *rldb1, *rldb0;
	const GString *data;
	Sheet *ref_sheet = workbook_sheet_by_index (state->wb, 0);
	int max = colrow_max (iscol, ref_sheet);
	int start, end;
	Sheet *sheet;

	g_return_if_fail (rldb2->ndims == 2);

	rldb1 = NULL;
	si = 0;
	srll = 0;
	for (sno = 0; sno < sheetcount; sno++) {
		if (srll == 0) {
			if (si >= rldb2->lower->len)
				break;
			rldb1 = g_ptr_array_index (rldb2->lower, si);
			si++;
			srll = rldb1->rll;
		}
		sheet = lotus_get_sheet (state->wb, sno);
		srll--;

		cri = 0;
		for (start = 0; start < max; start = end + 1) {
			if (cri >= rldb1->lower->len)
				break;
			rldb0 = g_ptr_array_index (rldb1->lower, cri);
			cri++;
			end = MIN (max - 1, start + (rldb0->rll - 1));

			data = rldb0->datanode;
			handler (state, sheet, start, end,
				 data ? data->str : NULL,
				 data ? data->len : 0);
		}
	}
}


static void
lotus_set_style_cb (LotusState *state, const GnmSheetRange *r,
		    const guint8 *data, size_t len)
{
	guint sid;
	GnmStyle *style;

	g_return_if_fail (len == 0 || len == 2);
	if (len == 0)
		return;

	sid = GSF_LE_GET_GUINT16 (data);
#ifdef DEBUG_STYLE
	g_printerr ("Got style %d for %s!%s",
		 sid,
		 r->sheet->name_unquoted,
		 cellpos_as_string (&r->range.start));
	g_printerr (":%s\n",
		 cellpos_as_string (&r->range.end));
#endif

	style = g_hash_table_lookup (state->style_pool,
				     GUINT_TO_POINTER (sid));
	g_return_if_fail (style != NULL);

#ifdef DEBUG_STYLE
	gnm_style_dump (style);
#endif

	gnm_style_ref (style);
	sheet_apply_style (r->sheet, &r->range, style);
}

static void
lotus_set_formats_cb (LotusState *state, const GnmSheetRange *r,
		      const guint8 *data, size_t len)
{
	guint32 fmt;
	GnmStyle *style;
	char *fmt_string;

	g_return_if_fail (len == 0 || len >= 4);
	if (len == 0)
		return;

#ifdef DEBUG_FORMAT
	g_printerr ("Got format for %s!%s",
		 r->sheet->name_unquoted,
		 cellpos_as_string (&r->range.start));
	g_printerr (":%s\n",
		 cellpos_as_string (&r->range.end));
#endif

	fmt = GSF_LE_GET_GUINT32 (data);


	if (fmt & 0x800) {
		guint sid;

		g_return_if_fail (len >= 6);
		sid = GSF_LE_GET_GUINT16 (data + 4);
		style = g_hash_table_lookup (state->style_pool,
					     GUINT_TO_POINTER (sid));
		g_return_if_fail (style != NULL);
#ifdef DEBUG_FORMAT
		gnm_style_dump (style);
#endif
		style = gnm_style_dup (style);
	} else {
		style = gnm_style_new ();
	}

	fmt_string = lotus_format_string (fmt);
#ifdef DEBUG_FORMAT
	g_printerr ("Format 0x%x: %s\n", fmt, fmt_string);
#endif
	gnm_style_set_format_text (style, fmt_string);
	g_free (fmt_string);
	sheet_apply_style (r->sheet, &r->range, style);

}

static void
lotus_set_borders_cb (LotusState *state, const GnmSheetRange *r,
		      const guint8 *data, size_t len)
{
}

static void
lotus_set_colwidth_cb (LotusState *state,
		       Sheet *sheet, int start, int end,
		       const guint8 *data, size_t len)
{
	guint16 flags, outlinelevel;
	double size;
	gboolean value_set;

	g_return_if_fail (len == 0 || len >= 8);
	if (len == 0)
		return;

#ifdef DEBUG_COLROW
	g_printerr ("Got width for %s!%s",
		 sheet->name_unquoted,
		 col_name (start));
	g_printerr ("-%s\n", col_name (end));
#endif

	outlinelevel = GSF_LE_GET_GUINT16 (data);
	flags = GSF_LE_GET_GUINT16 (data + 2);
	size = (state->version >= LOTUS_VERSION_123SS98)
		? lotus_twips_to_points (GSF_LE_GET_GUINT32 (data + 4))
		: lotus_qmps_to_points (GSF_LE_GET_GUINT32 (data + 4));

	value_set = (flags & 1) != 0;
	if (end - start >= gnm_sheet_get_max_cols (sheet))
		sheet_col_set_default_size_pixels (sheet, size);
	else {
		int i;
		for (i = start; i <= end; i++)
			sheet_col_set_size_pts (sheet, i, size, value_set);
	}

	if (flags & 2) {
		/* Hidden */
		colrow_set_visibility (sheet, TRUE, FALSE, start, end);
	}

	/* (flags & 4): Collapsed */
	/* (flags & 8): Level set */
	/* (flags & 0x10): Invisible */
	/* (flags & 0x20): Page break */
}

static void
lotus_set_rowheight_cb (LotusState *state,
			Sheet *sheet, int start, int end,
			const guint8 *data, size_t len)
{
	guint16 flags, outlinelevel;
	double size;
	gboolean value_set;

	g_return_if_fail (len == 0 || len >= 8);
	if (len == 0)
		return;

#ifdef DEBUG_COLROW
	g_printerr ("Got height for %s!%d-%d\n",
		 sheet->name_unquoted,
		 start, end);
#endif

	outlinelevel = GSF_LE_GET_GUINT16 (data);
	flags = GSF_LE_GET_GUINT16 (data + 2);
	size = (state->version >= LOTUS_VERSION_123SS98)
		? lotus_twips_to_points (GSF_LE_GET_GUINT32 (data + 4))
		: lotus_qmps_to_points (GSF_LE_GET_GUINT32 (data + 4));

	value_set = (flags & 1) != 0;
	if (end - start >= gnm_sheet_get_max_rows (sheet))
		sheet_row_set_default_size_pixels (sheet, size);
	else {
		int i;
		for (i = start; i <= end; i++)
			sheet_row_set_size_pts (sheet, i, size, value_set);
	}

	if (flags & 2) {
		/* Hidden */
		colrow_set_visibility (sheet, FALSE, FALSE, start, end);
	}

	/* (flags & 4): Collapsed */
	/* (flags & 8): Level set */
	/* (flags & 0x10): Invisible */
	/* (flags & 0x20): Page break */
}

static void
lotus_rldb_apply (LotusRLDB *rldb, int type, LotusState *state)
{
	g_return_if_fail (lotus_rldb_full (rldb));

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

	case LOTUS_RLDB_COLWIDTHS:
		lotus_rldb_walk_2d (rldb, state, TRUE, lotus_set_colwidth_cb);
		break;

	case LOTUS_RLDB_ROWHEIGHTS:
		lotus_rldb_walk_2d (rldb, state, FALSE, lotus_set_rowheight_cb);
		break;

	default:
		g_assert_not_reached ();
	}
}

/* ------------------------------------------------------------------------- */

static GnmCell *
lotus_cell_fetch (LotusState *state, Sheet *sheet, guint32 col, guint32 row)
{
	if (col >= (unsigned)gnm_sheet_get_max_cols (sheet) ||
	    row >= (unsigned)gnm_sheet_get_max_rows (sheet)) {
		if (!state->sheet_area_error) {
			state->sheet_area_error = TRUE;
			g_warning ("File is most likely corrupted.\n"
				   "(It claims to contain a cell outside the range Gnumeric can handle.)");
		}

		return NULL;
	}

	return sheet_cell_fetch (sheet, col, row);
}



static GnmCell *
insert_value (LotusState *state, Sheet *sheet, guint32 col, guint32 row, GnmValue *val)
{
	GnmCell *cell;

	g_return_val_if_fail (val != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);

	cell = lotus_cell_fetch (state, sheet, col, row);

	if (cell) {
		gnm_cell_set_value (cell, val);
#if LOTUS_DEBUG > 0
		g_printerr ("Inserting value at %s:\n", cell_name (cell));
		value_dump (val);
#endif
	} else
		value_release (val);

	return cell;
}

static Sheet *
attach_sheet (Workbook *wb, int idx)
{
	/*
	 * Yes, I do mean col_name.  Use that as an easy proxy for
	 * naming the sheets similarly to lotus.
	 */
	Sheet *sheet = sheet_new (wb, col_name (idx), GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);

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

	state->lmbcs_group = 1;

	do {
		switch (r->type) {
		case LOTUS_BOF:
			state->sheet = attach_sheet (state->wb, sheetidx++);
			break;

		case LOTUS_EOF:
			state->sheet = NULL;
			break;

		case LOTUS_INTEGER: CHECK_RECORD_SIZE (>= 7) {
			GnmValue *v = value_new_int (GSF_LE_GET_GINT16 (r->data + 5));
			guint8 fmt = GSF_LE_GET_GUINT8 (r->data);
			int col = GSF_LE_GET_GUINT16 (r->data + 1);
			int row = GSF_LE_GET_GUINT16 (r->data + 3);

			cell = insert_value (state, state->sheet, col, row, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
		case LOTUS_NUMBER: CHECK_RECORD_SIZE (>= 13) {
			GnmValue *v = value_new_float (gsf_le_get_double (r->data + 5));
			guint8 fmt = GSF_LE_GET_GUINT8 (r->data);
			int col = GSF_LE_GET_GUINT16 (r->data + 1);
			int row = GSF_LE_GET_GUINT16 (r->data + 3);

			cell = insert_value (state, state->sheet, col, row, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
		case LOTUS_LABEL: CHECK_RECORD_SIZE (>= 7) {
			/* one of '\', '''', '"', '^' */
			/* gchar format_prefix = *(r->data + 1 + 4);*/
			GnmValue *v = lotus_get_strval (r, 6, state->lmbcs_group);
			guint8 fmt = GSF_LE_GET_GUINT8 (r->data);
			int col = GSF_LE_GET_GUINT16 (r->data + 1);
			int row = GSF_LE_GET_GUINT16 (r->data + 3);
			cell = insert_value (state, state->sheet, col, row, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
		case LOTUS_FORMULA: CHECK_RECORD_SIZE (>= 15) {
			/* 5-12 = value */
			/* 13-14 = formula r->length */
			guint8 fmt = GSF_LE_GET_GUINT8 (r->data);
			int col = GSF_LE_GET_GUINT16 (r->data + 1);
			int row = GSF_LE_GET_GUINT16 (r->data + 3);
			int len = GSF_LE_GET_GINT16 (r->data + 13);
			GnmExprTop const *texpr;
			GnmParsePos pp;
			GnmValue *v = NULL;

			if (state->sheet == NULL) {
				g_printerr ("Assertion state->sheet != NULL failed\n");
				break;
			}
			if (r->len < (15 + len))
				break;

			pp.eval.col = col;
			pp.eval.row = row;
			pp.sheet = state->sheet;
			pp.wb = pp.sheet->workbook;
			texpr = lotus_parse_formula (state, &pp,
						     r->data + 15, len);

			if (0x7ff0 == (GSF_LE_GET_GUINT16 (r->data + 11) & 0x7ff8)) {
				/* I cannot find normative definition
				 * for when this is an error, an when
				 * a string, so we cheat, and peek
				 * at the next record.
				 */
				if (LOTUS_STRING == record_peek_next (r)) {
					record_next (r);
					v = lotus_get_strval (r, 5, state->lmbcs_group);
				} else
					v = value_new_error_VALUE (NULL);
			} else
				v = value_new_float (gsf_le_get_double (r->data + 5));
			cell = lotus_cell_fetch (state, state->sheet, col, row);
			if (cell) {
				gnm_cell_set_expr_and_value (cell, texpr, v, TRUE);
				cell_set_format_from_lotus_format (cell, fmt);
			} else
				value_release (v);
			gnm_expr_top_unref (texpr);
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
		workbook_sheet_add (wb, -1, GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);

	return workbook_sheet_by_index (wb, i);
}

static gunichar
lmbcs_12 (const guint8 *p)
{
	guint8 c0, c1;
	gunichar uc;

	if ((c0 = p[0]) == 0 || (c1 = p[1]) == 0)
		return 0;

	/* Might be wrong for 0x80 */
	if (c0 <= 0x80 || c0 == 0xff)
		return 0;

	uc = lmbcs_group_12[c0 - 0x80][c1];
	if (uc == 0) {
		char *s;
		gsize bytes_read;

		if (lmbcs_12_iconv == (GIConv)0)
			lmbcs_12_iconv = gsf_msole_iconv_open_for_import (950);
		if (lmbcs_12_iconv == (GIConv)-1)
			return 0;

		s = g_convert_with_iconv ((const gchar *)p, 2,
					  lmbcs_12_iconv,
					  &bytes_read, NULL,
					  NULL);
		if (s && bytes_read == 2)
			uc = g_utf8_get_char (s);
		else
			uc = 0xffff;
		g_free (s);

		lmbcs_group_12[c0 - 0x80][c1] = uc;
	}

	if (uc == 0xffff)
		return 0;
	return uc;
}


char *
lotus_get_lmbcs (char const *data, int maxlen, int def_group)
{
	GString *res = g_string_sized_new (maxlen + 2);
	guint8 const *p;
	guint8 const *theend;

	p = (guint8 const *)data;
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

		case 0x10: case 0x11: case 0x13:
		case 0x15: case 0x16: case 0x17: {
			unsigned code = (p[0] << 16) | (p[1] << 8) | p[2];
			g_warning ("Unhandled character 0x%06x", code);
			p += 3;
			/* See http://www.batutis.com/i18n/papers/lmbcs/ */
			break;
		}

		case 0x12: {
			gunichar uc = lmbcs_12 (p + 1);
			p += 3;
			if (uc)
				g_string_append_unichar (res, uc);
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
				guint8 c = p[0];
				gunichar uc = 0;

				switch (def_group) {
				case 0x01: uc = lmbcs_group_1[c]; p++; break;
				case 0x02: uc = lmbcs_group_2[c]; p++; break;
				case 0x03: if (c >= 0x80) uc = lmbcs_group_3[c - 0x80]; p++; break;
				case 0x04: if (c >= 0x80) uc = lmbcs_group_4[c - 0x80]; p++; break;
				case 0x05: if (c >= 0x80) uc = lmbcs_group_5[c - 0x80]; p++; break;
				case 0x06: uc = lmbcs_group_6[c]; p++; break;
				case 0x08: if (c >= 0x80) uc = lmbcs_group_8[c - 0x80]; p++; break;
				case 0x0b: if (c >= 0x80) uc = lmbcs_group_b[c - 0x80]; p++; break;
				case 0x0f: uc = lmbcs_group_f[c]; p++; break;
				case 0x12: uc = lmbcs_12 (p); p += 2; break;
				default:
					g_warning ("Unhandled character set 0x%x", def_group);
					p++;
					break;
				}

				if (uc)
					g_string_append_unichar (res, uc);
			}
			break;
		}
	}

	return g_string_free (res, FALSE);
}


static char *
lotus_get_cstr (const record_t *r, int ofs, int def_group)
{
	if (ofs < 0 || ofs >= r->len)
		return NULL;
	else
		return lotus_get_lmbcs (r->data + ofs, r->len - ofs, def_group);
}

GnmValue *
lotus_get_strval (const record_t *r, int ofs, int def_group)
{
	char *s = lotus_get_cstr (r, ofs, def_group);
	return s ? value_new_string_nocopy (s) : value_new_empty ();
}

GnmValue *
lotus_new_string (gchar const *data, int def_group)
{
	return value_new_string_nocopy
		(lotus_get_lmbcs (data, strlen (data), def_group));
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
		case LOTUS_BOF: CHECK_RECORD_SIZE (>= 18) {
			state->lmbcs_group = GSF_LE_GET_GUINT8 (r->data + 16);
			break;
		}

		case LOTUS_EOF:
			goto done;

		case LOTUS_SHEETCELLPTR: CHECK_RECORD_SIZE (== 16) {
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[0]);
			int row = GSF_LE_GET_GUINT16 (r->data + 4);
			int col = r->data[6];
			int left = r->data[7];
			int top = GSF_LE_GET_GUINT16 (r->data + 8);
			SheetView *sv = sheet_get_view (sheet, state->wbv);
			GnmCellPos edit;
			edit.col = col;
			edit.row = row;
			sv_selection_set (sv, &edit, col, row, col, row);
			gnm_sheet_view_set_initial_top_left (sv, left, top);
			break;
		}

		case LOTUS_SHEETLAYOUT: CHECK_RECORD_SIZE (== 5) {
			// Despite the name, this stores default col width
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[0]);
			guint8 chars = r->data[4];
			// Very approximate
			double size = lotus_twips_to_points (chars * (20 * 72 / 11));
			sheet_col_set_default_size_pts (sheet, size);
			break;
		}

		case LOTUS_COLW4: CHECK_RECORD_SIZE (>= 4) {
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[0]);
			int i, n = (r->len - 4) / 2;
			for (i = 0; i < n; i++) {
				guint8 col = r->data[4 + 2 * i];
				guint8 chars = r->data[5 + 2 * i];
				gboolean value_set = TRUE;
				// Very approximate
				double size = lotus_twips_to_points (chars * (20 * 72 / 11));
				sheet_col_set_size_pts (sheet, col, size, value_set);
			}
			break;
		}

		case LOTUS_FORMAT: CHECK_RECORD_SIZE (>= 2) {
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[0]);
			guint8 subtype = GSF_LE_GET_GUINT8 (r->data + 1);
			switch (subtype) {
			case 0: CHECK_RECORD_SIZE (>= 4) { // FORMAT
				int row = GSF_LE_GET_GUINT16 (r->data + 2);
				int col = 0, o;
				for (o = 4; o + 4 <= r->len; o += 4) {
					guint32 frmt = GSF_LE_GET_GUINT32 (r->data + o);
					gboolean rep = (frmt & 0x80000000u) && (o + 4 < r->len);
					int n = rep ? 1 + r->data[o++ + 4] : 1;
					range_set_format_from_lotus_format (sheet,
									    col, row,
									    col + (n - 1), row,
									    frmt);
					col += n;
				}
				break;
			}
			case 2: CHECK_RECORD_SIZE (>= 8) { // DUPFMT
				int row = GSF_LE_GET_GUINT16 (r->data + 2);
				Sheet *src_sheet = lotus_get_sheet (state->wb, GSF_LE_GET_GUINT16 (r->data + 4));
				int src_row = GSF_LE_GET_GUINT16 (r->data + 6);
				GnmRange r;
				GnmStyleList *styles;
				GnmCellPos cp;

				range_init_rows (&r, src_sheet, src_row, src_row);
				styles = sheet_style_get_range (src_sheet, &r);

				cp.col = 0;
				cp.row = row;
				sheet_style_set_list  (sheet, &cp, styles, NULL, NULL);
				style_list_free (styles);

#if 0
				g_printerr ("%s's row %d copies style from %s's row %d\n",
					    sheet->name_unquoted, row + 1,
					    src_sheet->name_unquoted, src_row + 1);
#endif

				break;
			}

			case 1: // GBLFMT
			default:
				g_printerr ("Unknown format record 0x%x/%02x of length %d.\n",
					 r->type, subtype, r->len);
			}
			break;
		}

		case LOTUS_ERRCELL: CHECK_RECORD_SIZE (>= 4) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *v = value_new_error_VALUE (NULL);
			(void)insert_value (state, sheet, col, row, v);
			break;
		}

		case LOTUS_NACELL: CHECK_RECORD_SIZE (>= 4) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *v = value_new_error_NA (NULL);
			(void)insert_value (state, sheet, col, row, v);
			break;
		}

		case LOTUS_LABEL2: CHECK_RECORD_SIZE (>= 6) {
			/* one of '\', '''', '"', '^' */
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
/*			gchar format_prefix = *(r->data + ofs + 4);*/
			GnmValue *v = lotus_get_strval (r, 5, state->lmbcs_group);
			(void)insert_value (state, sheet, col, row, v);
			break;
		}

		case LOTUS_NUMBER2: CHECK_RECORD_SIZE (>= 12) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *v = lotus_lnumber (r, 4);
			(void)insert_value (state, sheet, col, row, v);
			break;
		}

		case LOTUS_SMALLNUM: CHECK_RECORD_SIZE (>= 6) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *v = lotus_smallnum (GSF_LE_GET_GINT16 (r->data + 4));
			(void)insert_value (state, sheet, col, row, v);
			break;
		}

		case LOTUS_EXTENDED_FLOAT: CHECK_RECORD_SIZE (>= 14) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *v = lotus_treal (r, 4);
			(void)insert_value (state, sheet, col, row, v);
			break;
		}

		case LOTUS_STYLE: CHECK_RECORD_SIZE (>= 2) {
			guint16 subtype = GSF_LE_GET_GUINT16 (r->data);
			switch (subtype) {
			case 0xfa1: CHECK_RECORD_SIZE (>= 24) {
				/* Text style.  */
#if 0
				guint16 styleid = GSF_LE_GET_GUINT16 (r->data + 2);
				guint8 fontid = GSF_LE_GET_GUINT8 (r->data + 9);
				guint16 fontsize = GSF_LE_GET_GUINT16 (r->data + 10);
				guint16 fg = GSF_LE_GET_GUINT16 (r->data + 12);
				guint16 bg = GSF_LE_GET_GUINT16 (r->data + 14);
				guint16 facebits = GSF_LE_GET_GUINT16 (r->data + 16);
				guint16 facemask = GSF_LE_GET_GUINT16 (r->data + 18);
				guint8 halign = GSF_LE_GET_GUINT8 (r->data + 20);
				guint8 valign = GSF_LE_GET_GUINT8 (r->data + 21);
				guint16 angle = GSF_LE_GET_GUINT16 (r->data + 22);
#endif

#if 0
				g_printerr ("Saw text style %d with fontid %d.\n", styleid, fontid);
#endif
				break;
			}

			case 0xfd2: CHECK_RECORD_SIZE (>= 24) {
				GnmStyle *style;
				GnmColor *color;
				int pat;

				/* Cell style.  */
				guint16 styleid = GSF_LE_GET_GUINT16 (r->data + 2);
#if 0
				guint8 fontid = GSF_LE_GET_GUINT8 (r->data + 9);
#endif
				guint16 fontsize = GSF_LE_GET_GUINT16 (r->data + 10);
				guint16 textfg = GSF_LE_GET_GUINT16 (r->data + 12);
#if 0
				guint16 textbg = GSF_LE_GET_GUINT16 (r->data + 14);
#endif
				guint16 facebits = GSF_LE_GET_GUINT16 (r->data + 16);
				guint16 facemask = GSF_LE_GET_GUINT16 (r->data + 18);

#if 0
				guint8 halign = GSF_LE_GET_GUINT8 (r->data + 20);
				guint8 valign = GSF_LE_GET_GUINT8 (r->data + 21);
				guint16 angle = GSF_LE_GET_GUINT16 (r->data + 22);
#endif
				guint16 intfg = GSF_LE_GET_GUINT16 (r->data + 24);
				guint16 intbg = GSF_LE_GET_GUINT16 (r->data + 26);
				guint8 intpat = GSF_LE_GET_GUINT8 (r->data + 28);

				style = gnm_style_new ();

				color = lotus_color (intbg);
				if (color) {
					gnm_style_set_back_color (style, color);
					if (intpat == 0xff) intpat = 2;
				}

				color = lotus_color (intfg);
				if (color)
					gnm_style_set_pattern_color (style, color);

				color = lotus_color (textfg);
				if (color)
					gnm_style_set_font_color (style, color);

				pat = lotus_pattern (intpat);
				if (pat >= 0)
					gnm_style_set_pattern (style, pat);

				if (fontsize != 0xffff)
					gnm_style_set_font_size (style,
								 lotus_fontsize_to_pts (fontsize));

#define FACEBIT(bitval,attr)							\
	if (facemask & bitval) {						\
		gnm_style_set_font_ ## attr (style, (facebits & bitval) != 0);	\
	}
				FACEBIT (0x0001,bold);
				FACEBIT (0x0002,italic);
				FACEBIT (0x0004,uline);
				FACEBIT (0x0080,strike);
#undef FACEBIT

#ifdef DEBUG_STYLE
				g_printerr ("Defining style 0x%04x:\n", styleid);
				gnm_style_dump (style);
#endif
				g_hash_table_insert (state->style_pool,
						     GUINT_TO_POINTER ((guint)styleid),
						     style);
				break;
			}

			case 0xfdc: CHECK_RECORD_SIZE (>= 11) {
				/* Fontname style */
				GnmStyle *style;

				guint16 styleid = GSF_LE_GET_GUINT16 (r->data + 2);
				/* guint8 fontclass = GSF_LE_GET_GUINT8 (r->data + 8); */
				/* guint8 fontfamily = GSF_LE_GET_GUINT8 (r->data + 9); */
				char *fontname = lotus_get_cstr (r, 10, state->lmbcs_group);

				if (!fontname) {
					g_warning ("Invalid fontname record.");
					break;
				}

				style = gnm_style_new ();
				gnm_style_set_font_name (style, fontname);

#ifdef DEBUG_STYLE
				g_printerr ("Defining style 0x%04x:\n", styleid);
				gnm_style_dump (style);
#endif
				g_hash_table_insert (state->style_pool,
						     GUINT_TO_POINTER ((guint)styleid),
						     style);

				g_free (fontname);
				break;
			}

			case 0x36b0: CHECK_RECORD_SIZE (> 5) {
				// Sheet name
				guint8 sheetno = GSF_LE_GET_GUINT8 (r->data + 2);
				char *name = g_strndup (r->data + 4, r->len - 5);
				Sheet *sheet = lotus_get_sheet (state->wb, sheetno);
				g_object_set (sheet, "name", name, NULL);
				g_free (name);
				break;
			}

			default:
				g_printerr ("Unknown style record 0x%x/%04x of length %d.\n",
					 r->type, subtype,
					 r->len);

			case 0x07d7: // Row height
			case 0x0fab: // Edge style
			case 0x0fb4: // Interior style
			case 0x0fc9: // Frame style
			case 0x0fe6: // Background style
			case 0x0ff0: // Text style
			case 0x0ffa: // Style pool
			case 0x32e7: // Named style
				break;
			}
			break;
		}

		case LOTUS_PACKED_NUMBER: CHECK_RECORD_SIZE (== 8) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *val = lotus_unpack_number (GSF_LE_GET_GUINT32 (r->data + 4));
			(void)insert_value (state, sheet, col, row, val);
			break;
		}

		case LOTUS_SHEET_NAME: CHECK_RECORD_SIZE (>= 11) {
			Sheet *sheet = lotus_get_sheet (state->wb, sheetnameno++);
			char *name = lotus_get_cstr (r, 10, state->lmbcs_group);
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
			GnmValue *curval = lotus_lnumber (r, 4);
			GnmParsePos pp;
			const GnmExprTop *texpr;
			GnmCell *cell;

			pp.eval.col = col;
			pp.eval.row = row;
			pp.sheet = sheet;
			pp.wb = sheet->workbook;
			texpr = lotus_parse_formula (state, &pp,
						     r->data + 12,
						     r->len - 12);
			cell = lotus_cell_fetch (state, sheet, col, row);
			if (cell)
				gnm_cell_set_expr_and_value (cell, texpr, curval, TRUE);
			else
				value_release (curval);

			gnm_expr_top_unref (texpr);
			break;
		}

		case LOTUS_FORMULA3: CHECK_RECORD_SIZE (>= 15) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *curval = lotus_treal (r, 4);
			GnmParsePos pp;
			const GnmExprTop *texpr;
			GnmCell *cell;

			pp.eval.col = col;
			pp.eval.row = row;
			pp.sheet = sheet;
			pp.wb = sheet->workbook;
			texpr = lotus_parse_formula (state, &pp,
						     r->data + 14,
						     r->len - 14);
			cell = lotus_cell_fetch (state, sheet, col, row);
			if (cell)
				gnm_cell_set_expr_and_value (cell, texpr, curval, TRUE);
			else
				value_release (curval);

			gnm_expr_top_unref (texpr);
			break;
		}

		case LOTUS_FORMULASTRING: CHECK_RECORD_SIZE (>= 5) {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			char *s = g_strndup (r->data + 4, r->len - 5);
			GnmCell *cell = lotus_cell_fetch (state, sheet, col, row);
			gnm_cell_assign_value (cell, value_new_string_nocopy (s));
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
				if (lotus_rldb_full (rldb))
					g_warning ("Unused rldb.");
				else
					g_warning ("Unfinished rldb.");
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
			if (!rldb) {
				g_warning ("Ignoring stray RLDB_DATANODE");
				break;
			}

			lotus_rldb_data (rldb, r->data, r->len);
			break;
		}

		case LOTUS_RLDB_REGISTERID: CHECK_RECORD_SIZE (== 2) {
			guint16 id = GSF_LE_GET_GUINT16 (r->data);

			if (!rldb) {
				g_warning ("Ignoring stray RLDB_REGISTERID");
				break;
			}

			lotus_rldb_register_id (rldb, id);
			break;
		}

		case LOTUS_RLDB_USEREGISTEREDID: CHECK_RECORD_SIZE (== 2) {
			guint16 id = GSF_LE_GET_GUINT16 (r->data);

			if (!rldb) {
				g_warning ("Ignoring stray RLDB_USEID");
				break;
			}

			lotus_rldb_use_id (rldb, id);
			break;
		}

		case LOTUS_RLDB_FORMATS:
		case LOTUS_RLDB_BORDERS:
		case LOTUS_RLDB_STYLES:
		case LOTUS_RLDB_COLWIDTHS:
		case LOTUS_RLDB_ROWHEIGHTS:
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
			char *text = lotus_get_cstr (r, 5, state->lmbcs_group);
			GnmCellPos pos;

			pos.col = col;
			pos.row = row;
			cell_set_comment (sheet, &pos, NULL, text, NULL);
			g_free (text);
			break;
		}

		default:
			g_warning ("Unknown record 0x%x of length %d.", r->type, r->len);

		case LOTUS_CALCORDER:
		case LOTUS_USER_RANGE:
		case LOTUS_SYSTEMRANGE:
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
		if (lotus_rldb_full (rldb))
			g_warning ("Unused rldb.");
		else
			g_warning ("Unfinished rldb.");
		lotus_rldb_unref (rldb);
		rldb = NULL;
	}

	return result;
}

gboolean
lotus_read (LotusState *state)
{
	record_t r;
	r.input = state->input;

	if (!record_next (&r)) return FALSE;

	if (r.len < 2)
		return FALSE;

	state->version = GSF_LE_GET_GUINT16 (r.data);

	if (r.type == LOTUS_BOF) {
		state->is_works = FALSE;
#if LOTUS_DEBUG > 0
		g_printerr ("Version=%x, Lotus\n", state->version);
#endif
		switch (state->version) {
		case LOTUS_VERSION_ORIG_123:
		case LOTUS_VERSION_SYMPHONY:
		case LOTUS_VERSION_SYMPHONY2:
			return lotus_read_old (state, &r);

		default:
			g_warning ("Unexpected version %x", state->version);
			/* Fall through.  */
		case LOTUS_VERSION_123V4:
		case LOTUS_VERSION_123V6:
		case LOTUS_VERSION_123V7:
		case LOTUS_VERSION_123SS98:
			return lotus_read_new (state, &r);
		}
	} else if (r.type == WORKS_BOF) {
		state->is_works = TRUE;
#if LOTUS_DEBUG > 0
		g_printerr ("Version=%x, MS Works\n", state->version);
#endif
		if (state->version == WORKS_VERSION_3)
			return lotus_read_works (state, &r);
	}

	return FALSE;
}

void
lmbcs_init (void)
{
}

void
lmbcs_shutdown (void)
{
	gsf_iconv_close (lmbcs_12_iconv);
}

typedef struct {
	char *typeface;
	int variant;
	int size;
	GIConv converter;
} WksFontEntry;

static WksFontEntry *
wks_font_new (void)
{
	WksFontEntry *res = g_new0 (WksFontEntry, 1);
	res->converter = (GIConv)-1;
	return res;
}

static void
wks_font_dtor (WksFontEntry *s)
{
	g_free (s->typeface);
	if (s->converter != (GIConv)-1)
		gsf_iconv_close (s->converter);
	g_free (s);
}

static void
cell_set_fmt (LotusState *state, GnmCell *cell, int fmt)
{
	GnmRange range;
	GnmStyle *style;

	range.start = cell->pos;
	range.end = range.start;

	style = g_hash_table_lookup (state->style_pool,
				     GINT_TO_POINTER (fmt));

	if (style) {
		gnm_style_ref (style);
		sheet_apply_style (state->sheet, &range, style);
	}
}

static const guint8 works_color_table[16][3]={
	{0,0,0}, /* dummy */
	{0,0,0},
	{0,0,255},
	{0,255,255},
	{0,255,0},
	{255,0,255},
	{255,0,0},
	{255,255,0},
	{128,128,128},
	{255,255,255},
	{0,0,128},
	{0,128,128},
	{0,128,0},
	{128,0,128},
	{128,0,0},
	{192,192,192}
};

static GnmColor *
works_color (guint i)
{
	if (i == 0) return style_color_auto_font();
	if (i < G_N_ELEMENTS (works_color_table))
		return gnm_color_new_rgb8 (works_color_table[i][0],
					     works_color_table[i][1],
					     works_color_table[i][2]);
	return NULL;
}

static const gchar* works_data_fmts[]=
{
	"dd.mm.yyyy",
	"d mmmm yyyy",
	"dd.yyyy",
	"mmmm yyyy",
	"dd.mm",
	"d mmmm",
	"dd-mm-yy", /* looks unused */
	"mmmm"
};

static const gchar* works_time_fmts[]=
{
	"h:mm AM/PM",
	"h:mm:ss AM/PM",
	"h:mm",
	"h:mm:ss"
};

static const gchar* works_frac_fmts[]=
{
	"# ?" "?/?" "?" /* silly trick to avoid using a trigraph */,
	"# ?/4",
	"#", /* unused ? */
	"# ?/8",
	"# ?/10",
	"# ?/16",
	"# ?/32",
	"# ?/100"
};

static char *
works_format_string (guint8 arg)
{
	int type, prec;
	GString *str;

	type = arg & 0xf;
	prec = (arg >> 5) & 7;

	str = g_string_new(NULL);

	switch (type) {
		case 0: /* fixed */
		case 2: /* currency */
			g_string_append(str,"0");
			append_precision(str,prec);
			break;
		case 1: /* exp */
			g_string_append(str,"0");
			append_precision(str,prec);
			g_string_append(str,"E+00");
			break;
		case 3: /* percent */
			g_string_append(str,"0");
			append_precision(str,prec);
			g_string_append(str,"%");
			break;
		case 4: /* thousands */
			g_string_append(str,"# ##0");
			append_precision(str,prec);
			break;
		case 5:

			switch (prec) {
				case 0: /* general */
				case 1: /* boolean */
					break;
				case 2:
				case 3:
				case 4:
				case 5:
					g_string_append(str,works_time_fmts[prec-2]);
					break;
			};
			break;
		case 6:
			g_string_append(str,works_data_fmts[prec]);
			break;
		case 10: /* fixed */
			g_string_append_len(str,"000000000",prec+1);
			break;
		case 11: /* fraction (no ...) */
		case 12: /* fraction */
			if (type == 11) {
				if (prec == 0) {
					g_string_append(str,"# ?/2");
					break;
				} else if (prec == 1) {
					g_string_append(str,"# ?/3");
					break;
				}
			}
			g_string_append(str,works_frac_fmts[prec]);
			break;
		case 13: /* currency + red */
		case 14: /* thousands + red */
			g_string_append(str,"# ##0");
			append_precision(str,prec);
			g_string_append(str,";[Red]-# ##0"); /* l10n??? */
			append_precision(str,prec);
			break;
	}
	return g_string_free(str, FALSE);
}

static GnmValue *
works_get_strval (const record_t *r, guint ofs, int fmt, LotusState *state)
{
	GString *str = g_string_new (NULL);
	char *strutf8;
	GIConv converter;
	WksFontEntry *font;

	while (ofs < r->len && r->data[ofs] != 0) {
		g_string_append_c (str, r->data[ofs]);
		ofs++;
	}

	font = g_hash_table_lookup (state->works_style_font,
				    GINT_TO_POINTER(fmt));
	converter = font ? font->converter : (GIConv)-1;
	if (converter == (GIConv)-1)
		converter = state->works_conv;

	strutf8 = g_convert_with_iconv (str->str, str->len,
					converter,
					NULL, NULL,
					NULL);
	g_string_free (str, TRUE);

	if (strutf8)
		return value_new_string_nocopy (strutf8);
	else
		return value_new_empty ();
}

static gboolean
lotus_read_works (LotusState *state, record_t *r)
{
	gboolean result = TRUE;
	int sheetidx = 0;
	int styleidx = 0;
	int fontidx  = 0;
	GnmCell    *cell;

	state->style_pool = g_hash_table_new_full
	(g_direct_hash,
	 g_direct_equal,
	 NULL,
	 (GDestroyNotify)gnm_style_unref);

	state->fonts = g_hash_table_new_full
	(g_direct_hash,
	 g_direct_equal,
	 NULL,
	 (GDestroyNotify)wks_font_dtor);

	state->works_style_font = g_hash_table_new (g_direct_hash,
						    g_direct_equal);

	state->lmbcs_group = 1;

	state->works_conv = gsf_msole_iconv_open_for_import (1252);

	do {
		switch (r->type) {
		case WORKS_BOF:
			state->sheet = attach_sheet (state->wb, sheetidx++);
			break;

		case LOTUS_EOF:
			state->sheet = NULL;
			break;

		case WORKS_SMALL_FLOAT: CHECK_RECORD_SIZE (>= 6) {
			int row = GSF_LE_GET_GUINT16 (r->data + 2);
			int col = r->data[0];
#if 0
			int fmt = GSF_LE_GET_GUINT16 (r->data + 4);
#endif
			guint32 raw = GSF_LE_GET_GUINT32 (r->data + 6);
			char flag = raw & 1;
			gnm_float x;
			GnmValue *v;

			raw = (raw&0xfc000000)|((raw&0x3fffffe)<<3);
			x = gsf_le_get_float (&raw);
			v = value_new_float (flag ? x / 100 : x);
			(void)insert_value (state, state->sheet, col, row, v);
			break;
		}
#if 0
		case LOTUS_INTEGER: CHECK_RECORD_SIZE (>= 7) {
			GnmValue *v = value_new_int (GSF_LE_GET_GINT16 (r->data + 5));
			guint8 fmt = GSF_LE_GET_GUINT8 (r->data);
			int i = GSF_LE_GET_GUINT16 (r->data + 1);
			int j = GSF_LE_GET_GUINT16 (r->data + 3);

			cell = insert_value (state, state->sheet, col, row, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
#endif

		case LOTUS_NUMBER: CHECK_RECORD_SIZE (>= 14) {
			GnmValue *v = value_new_float (gsf_le_get_double (r->data + 6));
			int col = GSF_LE_GET_GUINT16 (r->data + 0);
			int row = GSF_LE_GET_GUINT16 (r->data + 2);
			int fmt = GSF_LE_GET_GUINT16 (r->data + 4);

			cell = insert_value (state, state->sheet, col, row, v);
			if (cell)
				cell_set_fmt (state, cell, fmt);
			break;
		}
		case LOTUS_LABEL: CHECK_RECORD_SIZE (>= 8) {
			int col = GSF_LE_GET_GUINT16 (r->data + 0);
			int row = GSF_LE_GET_GUINT16 (r->data + 2);
			int fmt = GSF_LE_GET_GUINT16 (r->data + 4);
			GnmValue *v = works_get_strval (r, 6, fmt, state);
			cell = insert_value (state, state->sheet, col, row, v);
			if (cell)
				cell_set_fmt (state, cell, fmt);
#if 0
			g_printerr ("String at %s:\n",
				    cellpos_as_string (&cell->pos));
			gsf_mem_dump (r->data, r->len);
#endif
			break;
		}
		case LOTUS_BLANK: CHECK_RECORD_SIZE (>= 6) {
			GnmValue *v = value_new_empty();
			int col = GSF_LE_GET_GUINT16 (r->data + 0);
			int row = GSF_LE_GET_GUINT16 (r->data + 2);
			int fmt = GSF_LE_GET_GUINT16 (r->data + 4);
			cell = insert_value (state, state->sheet, col, row, v);
			if (cell)
				cell_set_fmt (state, cell, fmt);
			break;
		}
		case LOTUS_FORMULA: CHECK_RECORD_SIZE (>= 16) {
			int col = GSF_LE_GET_GUINT16 (r->data + 0);
			int row = GSF_LE_GET_GUINT16 (r->data + 2);
			int fmt = GSF_LE_GET_GUINT16 (r->data + 4);
			int len = GSF_LE_GET_GINT16 (r->data + 14);
			GnmExprTop const *texpr;
			GnmParsePos pp;
			GnmValue *v = NULL;

			if (r->len < (15 + len))
				break;

			pp.eval.col = col;
			pp.eval.row = row;
			pp.sheet = state->sheet;
			pp.wb = pp.sheet->workbook;
			texpr = lotus_parse_formula (state, &pp,
						     r->data + 16, len);

			if (0x7ff0 == (GSF_LE_GET_GUINT16 (r->data + 12) & 0x7ff8)) {
				if (LOTUS_STRING == record_peek_next (r)) {
					record_next (r);
					v = lotus_get_strval (r, 6, state->lmbcs_group);
				} else
					v = value_new_error_VALUE (NULL);
			} else
				v = value_new_float (gsf_le_get_double (r->data + 6));
			cell = lotus_cell_fetch (state, state->sheet, col, row);
			if (cell) {
				gnm_cell_set_expr_and_value (cell, texpr, v, TRUE);
				cell_set_fmt (state, cell, fmt);
			} else
				value_release (v);
			gnm_expr_top_unref (texpr);
			break;
		}

		case WORKS_FONT: CHECK_RECORD_SIZE (>= 38) {
			WksFontEntry *font = wks_font_new();
			int codepage;
			int fid = fontidx++;
			int l;
			font->variant = GSF_LE_GET_GUINT16(r->data + 0);
			l = strlen (r->data + 2);
			if (l > 34) l = 34;
			font->typeface = g_malloc(l + 1);
			/* verify UTF-8? */
			memcpy(font->typeface, r->data + 2, l);
			font->typeface[l] = 0;
			font->size = r->data[36];

			codepage = gnm_font_override_codepage (font->typeface);
			if (codepage != -1)
				font->converter = gsf_msole_iconv_open_for_import (codepage);

			g_hash_table_insert (state->fonts,
			     GUINT_TO_POINTER ((guint)fid),
			     font);

			break;
		}

		case WORKS_STYLE: CHECK_RECORD_SIZE (>= 10) {
			GnmStyle *style;
			GnmColor *color;
			WksFontEntry *font;
			char *fmt;
			int tmp;

			guint16 styleid = styleidx++;
			guint8 fontid = GSF_LE_GET_GUINT8 (r->data + 4);
			/*guint16 fontsize = GSF_LE_GET_GUINT16 (r->data + 10);
			guint16 textbg = GSF_LE_GET_GUINT16 (r->data + 14);*/
			guint16 facebits;
			guint8 align = GSF_LE_GET_GUINT8 (r->data + 1);
			/*guint16 angle = GSF_LE_GET_GUINT16 (r->data + 22);
			guint16 intfg = GSF_LE_GET_GUINT16 (r->data + 24);
			guint16 intbg = GSF_LE_GET_GUINT16 (r->data + 26);
			guint8 intpat = GSF_LE_GET_GUINT8 (r->data + 28);*/

			style = gnm_style_new ();

			font = g_hash_table_lookup(state->fonts,GUINT_TO_POINTER ((guint)fontid));

			if (font) {
				facebits = font->variant;
#define FACEBIT(bitval,attr)							\
		gnm_style_set_font_ ## attr (style, (facebits & bitval) != 0);
				FACEBIT (0x0001,bold);
				FACEBIT (0x0002,italic);
				FACEBIT (0x0004,uline);
				FACEBIT (0x0008,strike);
#undef FACEBIT
				if (font->size != 0)
					gnm_style_set_font_size (style, font->size / 2.0);

				if (font->variant & 0xF0) {
					color = works_color((font->variant >> 4) & 0xF);
					if (color)
						gnm_style_set_font_color (style, color);
				}

				if (font->typeface)
					gnm_style_set_font_name(style, font->typeface);

				g_hash_table_insert (state->works_style_font,
						     GUINT_TO_POINTER((guint)styleid),
						     font);
			}

			tmp = (align >> 2) & 7;
			switch (tmp) {
				case 1:
					tmp = GNM_HALIGN_LEFT;
					break;
				case 2:
					tmp = GNM_HALIGN_CENTER;
					break;
				case 3:
					tmp = GNM_HALIGN_RIGHT;
					break;
				case 4:
					tmp = GNM_HALIGN_FILL;
					break;
				default:
					tmp = GNM_HALIGN_GENERAL;
			}
			gnm_style_set_align_h(style, tmp);

			tmp = (align >> 6) & 3;
			switch (tmp) {
				case 0:
					tmp = GNM_VALIGN_BOTTOM;
					break;
				case 1:
					tmp = GNM_VALIGN_CENTER;
					break;
				case 2:
					tmp = GNM_VALIGN_TOP;
					break;
			}
			gnm_style_set_align_v(style, tmp);

			if (align & 0x20) {
				gnm_style_set_wrap_text(style, TRUE);
			}

			fmt = works_format_string(r->data[0]);
			if (fmt) {
				if (*fmt) gnm_style_set_format_text(style, fmt);
				g_free(fmt);
			}

#ifdef DEBUG_STYLE
			g_printerr ("Defining style 0x%04x:\n", styleid);
			gnm_style_dump (style);
#endif
			g_hash_table_insert (state->style_pool,
					     GUINT_TO_POINTER ((guint)styleid),
					     style);
		}

		default:
			g_warning ("Unknown record 0x%x of length %d.", r->type, r->len);
			break;
		}
	} while (record_next (r));

	g_hash_table_destroy (state->works_style_font);

	return result;
}
