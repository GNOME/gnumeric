/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//* vim: set sw=8: */
#ifndef GO_DRAWING_MS_H
#define GO_DRAWING_MS_H

/**
 * god-drawing-ms.h: MS Office Graphic Object I/O support
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jody@gnome.org)
 *    Christopher James Lahey <clahey@ximian.com>
 *
 * (C) 1998-2004 Michael Meeks, Jody Goldberg, Chris Lahey
 **/

#include <drawing/god-drawing.h>
#include <drawing/god-drawing-group.h>
#include <gsf/gsf.h>

GodDrawing      *god_drawing_read_ms        (GsfInput    *input,
					    gsf_off_t    length,
					    GError     **err);
GodDrawingGroup *god_drawing_group_read_ms  (GsfInput    *input,
					    gsf_off_t    length,
					    GError     **err);
#if 0
int              god_drawing_write_ms       (GodDrawing  *drawing,
					    GsfOutput   *output);
#endif

#endif /* GO_DRAWING_MS_H */
