#include <stdio.h>

#include "gnumeric.h"
#include "sheet.h"
#include "workbook.h"
#include "mstyle.h"
#include "test.h"

#define SCREEN_WIDTH  40
#define SCREEN_HEIGHT 30
#define SCREEN_SLIDES 500
#define SCREEN_INCR   (SCREEN_HEIGHT / 2)

extern int style_cache_hits;
extern int style_cache_misses;
extern int style_cache_flushes;
extern int style_cache_range_hits;

static void
thrash_sheet (Sheet *sheet)
{
	int i;

	g_return_if_fail (sheet != NULL);
	fprintf (stderr, "Style lookups on '%s'\n", sheet->name_unquoted);

	sheet_styles_dump (sheet);

	for (i = 0; i < SCREEN_SLIDES * SCREEN_INCR; i += SCREEN_INCR) {
		int j;

		for (j = 0; j < SCREEN_HEIGHT; j++) {
			int k;
			for (k = 0; k < SCREEN_WIDTH; k++) {
				MStyle *mstyle;

				mstyle = sheet_style_compute (sheet, k, i + j);
				mstyle_unref (mstyle);
			}
		}
	}
	printf ("Hits %d, Range Hits %d, Misses %d Flushes %d\n", style_cache_hits,
		style_cache_range_hits, style_cache_misses, style_cache_flushes);

	style_cache_hits = 0;
	style_cache_misses = 0;
	style_cache_flushes = 0;
	style_cache_range_hits = 0;
}

void
workbook_style_test (Workbook *wb)
{
	GList *sheets;

	g_return_if_fail (wb != NULL);

	sheets = workbook_sheets (wb);

	while (sheets) {
		thrash_sheet (sheets->data);
		sheets = g_list_remove (sheets, sheets->data);
	}
}
