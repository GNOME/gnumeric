#include <gnumeric-config.h>
#include <gnumeric.h>
#include "sheet.h"
#include "sheet-style.h"
#include "workbook.h"
#include "mstyle.h"
#include "test.h"
#include "value.h"
#include "cell.h"

#include <stdio.h>

#define SCREEN_WIDTH  40
#define SCREEN_HEIGHT 30
#define SCREEN_SLIDES 500
#define SCREEN_INCR   (SCREEN_HEIGHT / 2)

#define INSERT_HEIGHT  64
#define INSERT_WIDTH   256

extern int style_cache_hits;
extern int style_cache_misses;
extern int style_cache_flushes;
extern int style_cache_range_hits;

static void
zero_stats (void)
{
	style_cache_hits = 0;
	style_cache_misses = 0;
	style_cache_flushes = 0;
	style_cache_range_hits = 0;
}

static void
dump_stats (const char *type)
{
	printf ("'%s': Hits %d, Range Hits %d, Misses %d Flushes %d\n", type,
		style_cache_hits, style_cache_range_hits,
		style_cache_misses, style_cache_flushes);
}

#ifdef RUN_THRASH_SCROLL
static void
thrash_scroll (Sheet *sheet)
{
	int i;

	for (i = 0; i < SCREEN_SLIDES * SCREEN_INCR; i += SCREEN_INCR) {
		int j;

		for (j = 0; j < SCREEN_HEIGHT; j++) {
			int k;
			for (k = 0; k < SCREEN_WIDTH; k++) {
				MStyle *mstyle;

				mstyle = sheet_style_get (sheet, k, i + j);
			}
		}
	}
}
#endif

static void
thrash_insert (Sheet *sheet)
{
	int     j;
	MStyle *style1 = mstyle_new ();
	MStyle *style2 = mstyle_new ();

	mstyle_set_font_bold   (style1, TRUE);
	mstyle_set_font_italic (style1, TRUE);
	mstyle_set_font_size   (style2, 20.0);

	for (j = 0; j < INSERT_HEIGHT; j++) {
		Range r;
		int i;

		for (i = 0; i < INSERT_WIDTH; i++) {
			Cell    *cell;
			MStyle *setstyle;

			r.start.col = i;
			r.start.row = j;
			r.end       = r.start;

			if (((i / 31) % 2) == 0)
				setstyle = style1;
			else
				setstyle = style2;

			mstyle_ref (setstyle);
			sheet_style_attach (sheet, &r, setstyle);

			cell = sheet_cell_fetch (sheet, i, j);

			cell_set_value (cell, value_new_int (i), NULL);
		}

		r.start.col = 0;
		r.start.row = MAX (0, j - 1);
		r.end.col   = SHEET_MAX_COLS;
		r.end.row   = MIN (SHEET_MAX_ROWS, j + 1);

		sheet_style_optimize (sheet, r);
	}

	mstyle_unref (style1);
	mstyle_unref (style2);
}

void
workbook_style_test (Workbook *wb)
{
	GList *sheets;

	g_return_if_fail (wb != NULL);

	sheets = workbook_sheets (wb);

	while (sheets) {
		Sheet *sheet = sheets->data;

		fprintf (stderr, "Style lookups on '%s'\n", sheet->name_unquoted);
		sheet_styles_dump (sheet);

#ifdef RUN_THRASH_SCROLL
		zero_stats ();
		thrash_scroll (sheet);
		dump_stats ("Scroll");
#endif

		zero_stats ();
		thrash_insert (sheet);
		dump_stats ("Insert");

		sheets = g_list_remove (sheets, sheet);
		sheet_flag_recompute_spans (sheet);
	}
	workbook_recalc (wb);
	workbook_calc_spans (wb, SPANCALC_RENDER);
}
