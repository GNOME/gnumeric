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

static void
thrash_sheet (Sheet *sheet)
{
	int i;

	g_return_if_fail (sheet != NULL);
	fprintf (stderr, "Style lookups on '%s'\n", sheet->name);

	sheet_styles_dump (sheet);

	for (i = 0; i < SCREEN_SLIDES*SCREEN_INCR; i+=SCREEN_INCR) {
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
