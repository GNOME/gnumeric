/*
 * ps.h
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

#ifndef GNUMERIC_PLUGIN_PS_H
#define GNUMERIC_PLUGIN_PS_H

typedef struct {
	unsigned char r;
	unsigned char g;
	unsigned char b;
} RGB_t;

enum {
	HELVETICA,
	HELVETICA_BOLD,
	HELVETICA_OBLIQUE,
	HELVETICA_BOLD_OBLIQUE,
	TIMES,
	TIMES_BOLD,
	TIMES_ITALIC,
	TIMES_BOLD_ITALIC,
	COURIER,
	COURIER_BOLD,
	COURIER_ITALIC,
	COURIER_BOLD_ITALIC,
	FONT_END,
};

void ps_init_eps (FILE *fp, int bx, int by, int bw, int bh);
void ps_text_left (FILE *fp, const char *s, double x, double y);
void ps_text_right (FILE *fp, const char *s, double x, double y);
void ps_text_center (FILE *fp, const char *s, double x1, double x2, double y);
void ps_write_raw (FILE *fp, const char *s);
void ps_box_bordered(FILE *fp,double llx,double lly,double w,double h,float lw);
void ps_box_filled (FILE *fp, double llx, double lly, double w, double h);
void ps_set_color (FILE *fp, RGB_t *rgb);
void ps_set_font (FILE *fp, unsigned int font, int size);
void ps_draw_line (FILE *fp,double x1,double y1,double x2,double y2, double w);
void ps_draw_circle (FILE *fp,double x,double y,double r, double w);
void ps_draw_ellipse (FILE *fp,double llx,double lly,double w,double h,float lw);

#endif

