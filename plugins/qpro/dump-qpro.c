#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <glib.h>
#include "qpro.h"


#define arraysize(x)     (sizeof(x)/sizeof(*(x)))


struct qpro_header {
	guint16 type;
	guint16 len;
};


static const struct {
	qpro_record_t type;
	const char *name;
	int (*handler) (struct qpro_header *);
} qpro_records[] = {
	{ UNKNOWN, "Unknown", NULL },
	{ BOF_BEGINNING_OF_FILE, "BOF Beginning of File", NULL },
	{ VERSION, "Version", NULL },
	{ PASSWORD_LEVEL, "Password Level", NULL },
	{ DIMENSION, "Dimension", NULL },
	{ MACRO_LIBRARY, "Macro Library", NULL },
	{ RECALCULATION_MODE, "Recalculation Mode", NULL },
	{ RECALCULATION_ORDER, "Recalculation Order", NULL },
	{ RECALCULATION_ITERATION_COUNT, "Recalculation Iteration Count", NULL },
	{ COMPILE_FORMULAS, "Compile Formulas", NULL },
	{ AUDIT_FORMULAS, "Audit Formulas", NULL },
	{ FONT, "Font", NULL },
	{ COLOR_TABLE, "Color Table", NULL },
	{ CELL_ATTRIBUTE, "Cell Attribute", NULL },
	{ PAGE_GROUP_ON, "Page Group On", NULL },
	{ FILL, "Fill", NULL },
	{ TABLE, "Table", NULL },
	{ QUERY, "Query", NULL },
	{ COMPATIBLE_SLIDE_SHOW, "Compatible Slide Show", NULL },
	{ OPTIMIZER_CONSTRAINT, "Optimizer Constraint", NULL },
	{ SOLVE_FOR, "Solve For", NULL },
	{ SORT_BLOCK, "Sort Block", NULL },
	{ SORT_FIRST_KEY, "Sort First Key", NULL },
	{ SORT_SECOND_KEY, "Sort Second Key", NULL },
	{ SORT_THIRD_KEY, "Sort Third Key", NULL },
	{ SORT_FOURTH_KEY, "Sort Fourth Key", NULL },
	{ SORT_FIFTH_KEY, "Sort Fifth Key", NULL },
	{ PARSE, "Parse", NULL },
	{ REGRESSION, "Regression", NULL },
	{ MATRIX, "Matrix", NULL },
	{ NOTEBOOK_OBJECT_SHOW, "Notebook Object Show", NULL },
	{ EOF_END_OF_FILE, "EOF End of File", NULL },
	{ PRINT_BEGIN_RECORDS, "Print Begin/Records", NULL },
	{ PRINT_BEGIN_GRAPHS, "Print Begin/Graphs", NULL },
	{ PRINT_AREA, "Print Area", NULL },
	{ PRINT_BLOCK, "Print Block", NULL },
	{ PRINT_MARGINS, "Print Margins", NULL },
	{ PRINT_PAGE_BREAK, "Print Page Break", NULL },
	{ PRINT_FORMULAS, "Print Formulas", NULL },
	{ PRINT_HEADINGS, "Print Headings", NULL },
	{ PRINT_GUIDELINES, "Print Guidelines", NULL },
	{ PRINT_BLOCK_DELIMITER, "Print Block Delimiter", NULL },
	{ PRINT_PAGE_DELIMITER, "Print Page Delimiter", NULL },
	{ PRINT_COPIES, "Print Copies", NULL },
	{ PRINT_PAGES, "Print Pages", NULL },
	{ PRINT_DENSITY, "Print Density", NULL },
	{ PRINT_TO_FIT, "Print To Fit", NULL },
	{ PRINT_SCALING, "Print Scaling", NULL },
	{ PRINT_PAPER_TYPE, "Print Paper Type", NULL },
	{ PRINT_ORIENTATION, "Print Orientation", NULL },
	{ PRINT_LEFT_BORDER, "Print Left Border", NULL },
	{ PRINT_TOP_BORDER, "Print Top Border", NULL },
	{ PRINT_CENTER_BLOCKS, "Print Center Blocks", NULL },
	{ PRINT_FOOTER_FONT, "Print Footer Font", NULL },
	{ PRINT_HEADER_FONT, "Print Header Font", NULL },
	{ PRINT_END, "Print End", NULL },
	{ BOP_BEGINNING_OF_PAGE, "BOP Beginning of Page", NULL },
	{ PROTECTION, "Protection", NULL },
	{ PAGE_ATTRIBUTE, "Page Attribute", NULL },
	{ PAGE_OBJECT_PROTECTION, "Page Object Protection", NULL },
	{ PAGE_TAB_COLOR, "Page Tab Color", NULL },
	{ PAGE_ZOOM_FACTOR, "Page Zoom Factor", NULL },
	{ EOP_END_OF_PAGE, "EOP End of Page", NULL },
	{ DEFAULT_COLUMN_ATTRIBUTE, "Default Column Attribute", NULL },
	{ BLANK_CELL, "Blank Cell", NULL },
	{ INTEGER_CELL, "Integer Cell", NULL },
	{ FLOATING_POINT_CELL, "Floating-Point Cell", NULL },
	{ BEGIN_VIEW, "Begin View", NULL },
	{ WINDOW_SIZE, "Window Size", NULL },
	{ WINDOW_LOCATION, "Window Location", NULL },
	{ DISPLAY_SETTINGS, "Display Settings", NULL },
	{ ZOOM_FACTOR, "Zoom Factor", NULL },
	{ SPLIT, "Split", NULL },
	{ SYNCHRONIZE, "Synchronize", NULL },
	{ CURRENT_PANE_VIEW, "Current Pane View", NULL },
	{ END_VIEW, "End View", NULL },
	{ GRAPH_ENGINE_VERSION, "Graph Engine Version", NULL },
	{ BEGIN_OBJECT, "Begin Object", NULL },
	{ BEGIN_GRAPH_RECORD, "Begin Graph Record", NULL },
	{ BEGIN_CHART_ENGINE_RECORD, "Begin Chart Engine Record", NULL },
	{ BEGIN_CHART_SAVE, "Begin Chart Save", NULL },
	{ END_CHART_SAVE, "End Chart Save", NULL },
	{ END_CHART_ENGINE_RECORD, "End Chart Engine Record", NULL },
	{ BEGIN_CHART_SERIES, "Begin Chart Series", NULL },
	{ X_AXIS_LABEL_SERIES, "X Axis Label Series", NULL },
	{ Z_AXIS_LABEL_SERIES, "Z Axis Label Series", NULL },
	{ LEGEND_SERIES, "Legend Series", NULL },
	{ DISPLAY_ORDER, "Display Order", NULL },
	{ NUMBER_OF_SERIES, "Number of Series", NULL },
	{ BEGIN_DATA_SERIES, "Begin Data Series", NULL },
	{ SERIES_DATA, "Series Data", NULL },
	{ SERIES_LABEL, "Series Label", NULL },
	{ END_DATA_SERIES, "End Data Series", NULL },
	{ END_CHART_SERIES, "End Chart Series", NULL },
	{ END_GRAPH_RECORD, "End Graph Record", NULL },
	{ END_OBJECT, "End Object", NULL },
	{ GRAPH_ICON_COORDINATES, "Graph Icon Coordinates", NULL },
	{ END_GRAPH, "End Graph", NULL },
	{ SLIDE_SHOW_ICON_COORDINATES, "Slide Show icon Coordinates", NULL },
	{ SLIDE_TYPE, "Slide Type", NULL },
	{ END_SLIDE_SHOW, "End Slide Show", NULL },
	{ SLIDE_TIME, "Slide Time", NULL },
	{ SLIDE_SPEED, "Slide Speed", NULL },
	{ SLIDE_LEVEL, "Slide Level", NULL },
	{ NEW_SLIDE_SPECIAL_EFFECTS, "New Slide Special Effects", NULL },
	{ SLIDE_SPECIAL_EFFECTS, "Slide Special Effects", NULL },
	{ STYLE, "Style", NULL },
	{ EXTERNAL_LINK, "External Link", NULL },
	{ BEGIN_GRAPH, "Begin Graph", NULL },
	{ PRINT_FOOTER, "Print Footer", NULL },
	{ PRINT_HEADER, "Print Header", NULL },
	{ PRINT_SETUP, "Print Setup", NULL },
	{ PRINT_DRAFT_MODE_MARGINS, "Print Draft Mode Margins", NULL },
	{ OPTIMIZER, "Optimizer", NULL },
	{ PANE1_DEFAULT_STYLE, "P1 Def Style", NULL },
	{ PANE1_COLUMN_WIDTH, "P1 Col Width", NULL },
	{ PANE1_MAX_FONT, "P1 Max Font", NULL },
	{ LABEL_CELL, "Label Cell", NULL },
	{ PANE1_INFORMATION, "P1 Info", NULL },
	{ PANE1_VISIBLE_PAGE, "P1 Visible Page", NULL },
};



int qpro_record_type_idx (qpro_record_t type)
{
	int i;

	for (i = 0; i < arraysize (qpro_records); i++)
		if (qpro_records[i].type == type)
			return i;

	if (type != UNKNOWN)
		return qpro_record_type_idx (UNKNOWN);

	return -1;
}


static int qpro_dump_record (char *data, size_t datalen, char **cur_o)
{
	struct qpro_header *hdrp, hdr;
	char *cur = *cur_o;
	int idx, rc = 1;

	if (((cur - data) + sizeof (*hdrp)) >= datalen)
		return 0;

	hdrp = (struct qpro_header *) cur;
	hdr.type = GUINT16_FROM_LE (hdrp->type);
	hdr.len = GUINT16_FROM_LE (hdrp->len);

	idx = qpro_record_type_idx (hdr.type);
	if (idx < 0)
		return 0;

	g_message ("HDR \"%s\" (%d): %d bytes\n",
		   qpro_records[idx].name,
		   hdr.type,
		   hdr.len);

	if (((cur - data) + sizeof (*hdrp) + hdr.len) >= datalen)
		return 0;

	if (qpro_records[idx].handler)
		rc = qpro_records[idx].handler (&hdr);

	if (rc)
		*cur_o += (hdr.len + sizeof (*hdrp));

	return rc;
}

int main (int argc, char *argv[])
{
	void *data;
	int fd, rc;
	char *cur;
	struct stat st;

	if (argc != 2) {
		fprintf(stderr,"usage: dump-qpro FILE\n");
		abort();
	}

	fd = open (argv[1], O_RDONLY);
	if (fd < 0) {
		perror("open");
		abort();
	}

	rc = fstat (fd, &st);
	if ((rc < 0) || (st.st_size < sizeof(struct qpro_header))) {
		perror("stat");
		close(fd);
		abort();
	}

	data = mmap (0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (data == (void*)MAP_FAILED) {
		perror("mmap");
		close(fd);
		abort();
	}

	cur = (char*) data;
	while (qpro_dump_record (data, st.st_size, &cur))
		/* chirp */ ;

	munmap (data, st.st_size);
	close(fd);
	return 0;
}
