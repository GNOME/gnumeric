#ifndef GNUMERIC_RENDERED_VALUE_H
# define GNUMERIC_RENDERED_VALUE_H

#include "gnumeric.h"

/**
 * RenderedValue:
 *
 * A place holder for what will eventually support
 * multiple fonts, colours, lines, and unicode.
 */
struct _RenderedValue {
	/* Text rendered and displayed */
	String      *rendered_text;

	/* Colour supplied by the formater eg [Red]0.00 */
	StyleColor  *render_color;

	/*
	 * TODO :  DO NOT USE THESE.
	 * Storing these values as pixels is the last hurdle to supporting
	 * multiple views with different zoom.  Use the accessors.
	 *
	 * Computed sizes of rendered text.
	 * In pixels EXCLUSIVE of margins and grid lines
	 */
	gboolean	dynamic_width;
	int		width_pixel;
	short		height_pixel, offset_pixel;
};

RenderedValue * rendered_value_new           (Cell *cell, MStyle const *mstyle,
					      gboolean dynamic_width);
void            rendered_value_destroy       (RenderedValue *rv);
void            rendered_value_calc_size     (Cell const *cell);
void            rendered_value_calc_size_ext (Cell const *cell, MStyle *mstyle);

/* Return the value as a single string without format infomation.
 * Caller is responsible for freeing the result
 */
char * rendered_value_get_text (RenderedValue const * rv);

void rendered_value_init (void);
void rendered_value_shutdown (void);

#endif /* GNUMERIC_RENDERED_VALUE_H */
