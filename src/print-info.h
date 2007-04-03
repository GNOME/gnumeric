#ifndef GNM_PRINT_INFO_H
#define GNM_PRINT_INFO_H

#include "gnumeric.h"
#include <gtk/gtk.h>

typedef struct {
	double    points;
#if 0
	GnomePrintUnit const *desired_display;
#endif
} PrintUnit;

/*
 * Margins.  In Points
 */
typedef struct {
	PrintUnit top, bottom;     /* see print.c for the definition (these are header/footer) */
} PrintMargins;

/* Header/Footer definition */
typedef struct {
	char *left_format;
	char *middle_format;
	char *right_format;
} PrintHF;

typedef struct {
	gboolean use;
	GnmRange range;
} PrintRepeatRange;

struct _PrintInformation {
	struct {
		enum {
			PRINT_SCALE_PERCENTAGE,
			PRINT_SCALE_FIT_PAGES
		} type;

		/* We store separate x and y scales internally, for the
		* 'fit-to' printing feature. (They are calculated at print-time)
		* When the user is doing the simple scaling, both these values
		* will be equal.
		*/
		struct {
			double x;
			double y;
		} percentage;

		struct { /* zero == use as many as required */
			int cols;
			int rows;
		} dim;
	} scaling;
	PrintMargins     margin;
	PrintRepeatRange repeat_top, repeat_left;
	unsigned int	 print_across_then_down;
	unsigned int     center_vertically:1;
	unsigned int     center_horizontally:1;
	unsigned int     print_grid_lines:1;
	unsigned int     print_titles:1;	/* col/row headers */
	unsigned int     print_black_and_white:1;
	unsigned int     print_as_draft:1;
	unsigned int     portrait_orientation:1;

	/* Gnumeric specific */
	unsigned int     invert_orientation:1;	/* gnome print offers R0 & R180 or R90 & R270 */
	unsigned int     print_even_if_only_styles:1;

	enum {
		PRINT_COMMENTS_NONE,
		PRINT_COMMENTS_IN_PLACE,
		PRINT_COMMENTS_AT_END
	} comment_placement;
	enum {
		PRINT_ERRORS_AS_DISPLAYED,
		PRINT_ERRORS_AS_BLANK,
		PRINT_ERRORS_AS_DASHES,
		PRINT_ERRORS_AS_NA
	} error_display;

	PrintHF          *header;
	PrintHF          *footer;

	int		  start_page; /* < 0 implies auto */
        int              n_copies;

  /* page_setup doubles as a flag whether the defaults are loaded */
        GtkPageSetup     *page_setup;
};

typedef enum {
	HF_RENDER_PRINT
} HFRenderType;

typedef struct {
	Sheet const *sheet;
	int       page;
	int       pages;
	GnmValue *date_time;
} HFRenderInfo;

PrintInformation *print_info_new         (gboolean load_defaults);
PrintInformation *print_info_load_defaults (PrintInformation *pi);
PrintInformation *print_info_dup	 (PrintInformation *pi);
void              print_info_free        (PrintInformation *pi);
void              print_info_save        (PrintInformation *pi);

GtkPageSetup     *print_info_get_page_setup (PrintInformation const *pi);
void              print_info_set_page_setup (PrintInformation *pi, GtkPageSetup *page_setup);

PrintHF          *print_hf_new           (char const *left,
					  char const *middle,
				          char const *right);
void              print_hf_free          (PrintHF *print_hf);
PrintHF          *print_hf_copy          (PrintHF const *source);
PrintHF          *print_hf_register      (PrintHF *hf);
gboolean          print_hf_same          (PrintHF const *a, PrintHF const *b);

char             *hf_format_render       (char const *format,
					  HFRenderInfo *info,
					  HFRenderType render_type);
HFRenderInfo     *hf_render_info_new     (void);
void              hf_render_info_destroy (HFRenderInfo *hfi);

#if 0
GnomePrintUnit const *unit_name_to_unit    (char const *name);
double      unit_convert             (double value,
				      GnomePrintUnit const *source,
				      GnomePrintUnit const *target);
#endif

void        print_init               (void);
void        print_shutdown           (void);

void	    print_info_set_paper     	   (PrintInformation *pi, char const *paper);
void        print_info_set_paper_width_height (PrintInformation *pi,
					       double paper_width,
					       double paper_height,
					       GtkUnit unit);
char const *print_info_get_paper     	   (PrintInformation *pi);
double      print_info_get_paper_width     (PrintInformation *pi, GtkUnit unit);
double      print_info_get_paper_height    (PrintInformation *pi, GtkUnit unit);
void        print_info_get_margins   (PrintInformation *pi,
				      double *header, double *footer, double *left, double *right);
void        print_info_set_margins   (PrintInformation *pi,
				      double header, double footer, double left, double right);
void        print_info_set_margin_header (PrintInformation *pi, double header);
void        print_info_set_margin_footer (PrintInformation *pi, double footer);
void        print_info_set_margin_left   (PrintInformation *pi, double left);
void        print_info_set_margin_right  (PrintInformation *pi, double right);

/* Formats known */
extern GList *hf_formats;

#endif /* GNM_PRINT_INFO_H */
