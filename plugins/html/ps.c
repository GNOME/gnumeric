/*
 * ps.c
 *
 * Copyright (C) 1999 Rasca, Berlin
 * EMail: thron@gmx.de
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

#include <gnome.h>
#include "config.h"
#include "ps.h"


static char *ps_font[] = {
	"Helvetica",
	"Helvetica-Bold",
	"Helvetica-Oblique",
	"Helvetica-BoldOblique",
	"Times",
	"Times-Bold",
	"Times-Italic",
	"Times-BoldItalic",
	"Courier",
	"Courier-Bold",
	"Courier-Italic",
	"Courier-BoldItalic",
};

/*
 */
void
ps_init_eps (FILE *fp, int bx, int by, int bw, int bh)
{
	fprintf (fp, "%%!PS-Adobe-3.0 EPSF-3.0\n");
	fprintf (fp, "%%%%Pages: 1\n");
	fprintf (fp, "%%%%BoundingBox: %d %d %d %d\n", bx, by, bw, bh);
	fprintf (fp, "%%%%EndComments\n");
	fprintf (fp, "%%%%BeginProlog\n");
	fprintf (fp, "/setColor {\n"
					"\tsetrgbcolor\n"
					" } bind def\n");
	fprintf (fp, "/textLeft {\n"
					"\tmoveto show\n"
					" } bind def\n");
	fprintf (fp, "/rtextLeft {\n"
					"\trmoveto show\n"
					" } bind def\n");
	fprintf (fp, "/textRight {%% (text) lrx, lry\n"
					"\texch 2 index stringwidth pop\n"
					"\tsub exch moveto show\n"
					" } bind def\n");
	fprintf (fp, "/setFont {\n"
					"\texch findfont exch scalefont setfont\n"
					" } bind def\n");
	fprintf (fp, "/rectBorder {\n"
					"\tsetlinewidth 4 2 roll moveto\n"
					"\t1 index 0 rlineto 0 exch\n"
					"\trlineto neg 0 rlineto closepath stroke\n"
					" } bind def\n");
	fprintf (fp, "/rectFill {%% llx, lly, width, height, linewidth\n"
					"\tsetlinewidth 4 2 roll moveto 1 index 0 rlineto\n"
					"\t0 exch rlineto neg 0 rlineto closepath fill\n"
					" } bind def\n");
	fprintf (fp, "/line {%% x1 y1 x2 y2 linewidth\n"
					"\tsetlinewidth\n"
					"\tnewpath\n"
					"\t4 2 roll\n"
					"\tmoveto\n"
					"\tlineto\n"
					"\tclosepath stroke\n"
					" } bind def\n");
	fprintf (fp, "/circle {%% x y r linewidth\n"
					"\tsetlinewidth\n"
					"\t0 360 arc\n"
					"\tstroke\n"
					" } bind def\n");
	fprintf (fp, "/ellipse {%% llx lly width height linewidth\n"
					"\tsetlinewidth\n"
					"\t1 index 2 div\n"
					"\t4 index add 3 index\n"
					"\tmoveto\n"						/* p0 */
					"\t3 index 3 index\n"				/* p1 */
					"\t1 index 1 index 4 index add\n"	/* p2 */
					"\t1 index 1 index exch 7 index 2 div add exch\n" 	/* p3 */
					"\tcurveto\n"
					"\t3 index 2 index add 3 index 2 index add\n"	/* p1 */
					"\t1 index 5 index\n"							/* p2 */
					"\t7 index 6 index 2 div add 7 index\n"			/* p3 */
					"\tcurveto stroke clear\n"
					" } bind def\n");
	fprintf (fp, "%%%%EndProlog\n");
	fprintf (fp, "%%%%Page: 1 1\n");
	fprintf (fp, "%d %d translate\n\n", bx, by);
}
/*
 * escape special characters .. needs work
 */
static void
ps_write_string (FILE *fp, const char *s)
{
	int len, i;
	const char *p;

	if (!s) {
		fprintf (fp, "((null))");
		return;
	}
	len = strlen (s);

	if (!len) {
		fprintf (fp, "()");
		return;
	}
	fprintf (fp, "(");
	p = s;
	for (i = 0; i < len; i++) {
		switch (*p) {
			case '(':
			case ')':
			case '%':
				fprintf (fp, "\\");
				fprintf (fp, "%c", *p);
				break;
			default:
				fprintf (fp, "%c", *p);
				break;
		}
		p++;
	}
	fprintf (fp, ")");
	return;
}

/*
 */
void
ps_text_left (FILE *fp, const char *str, double x, double y)
{
	ps_write_string (fp, str);
	fprintf (fp, " %.2f %.2f textLeft\n", x, y);
}

/*
 */
void
ps_text_right (FILE *fp, const char *str, double x, double y)
{
	ps_write_string (fp, str);
	fprintf (fp, " %.2f %.2f textRight\n", x, y);
}

/*
 */
void
ps_text_center (FILE *fp, const char *str, double x1, double x2, double y)
{
	ps_write_string (fp, str);
	fprintf (fp, " dup stringwidth pop\n");/* string + width on the stack */
	/* calc new x position */
	fprintf (fp, "%.3f exch sub 2 div %.3f add\n", (x2-x1+1), x1);
	fprintf (fp, "%.3f textLeft\n", y);			/* draw string */
}

/*
 */
void
ps_write_raw (FILE *fp, const char *s)
{
	fprintf (fp, "%s", s);
}


/*
 */
void
ps_box_bordered (FILE *fp, double llx, double lly, double w, double h, float lw)
{
	if (!fp)
		return;
	fprintf (fp, "%.2f %.2f %.2f %.2f %.2f rectBorder\n", llx,lly,w,h, lw);
}

/*
 */
void
ps_box_filled (FILE *fp, double llx, double lly, double w, double h)
{
	if (!fp)
		return;
	fprintf (fp, "%.2f %.2f %.2f %.2f 0 rectFill\n", llx, lly, w, h);
}

/*
 */
void
ps_set_color (FILE *fp, RGB_t *rgb)
{
	if (!fp)
		return;
	if (!rgb)
		return;
	fprintf (fp, "%.2f %.2f %.2f setColor\n",
		(float)rgb->r/255, (float)rgb->g/255, (float)rgb->b/255);
}

/*
 */
void
ps_set_font (FILE *fp, unsigned int font, int size)
{
	if (font >= FONT_END)
		return;
	fprintf (fp, "/%s %d setFont\n", ps_font[font], size);
}

/*
 */
void
ps_draw_line (FILE *fp, double x1, double y1, double x2, double y2, double w)
{
	if (!fp)
		return;
	fprintf (fp, "%.2f %.2f %.2f %.2f %.2f line\n", x1, y1, x2, y2, w);
}

/*
 */
void
ps_draw_circle (FILE *fp, double x, double y, double r, double w)
{
	if (!fp)
		return;
	fprintf (fp, "%.2f %.2f %.2f %.2f circle\n", x, y, r, w);
}

/*
 */
void
ps_draw_ellipse (FILE *fp, double llx, double lly, double w, double h, float lw)
{
	if (!fp)
		return;
	fprintf (fp, "%.2f %.2f %.2f %.2f %.2f ellipse\n", llx, lly, w, h, lw);
}
