/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * load-ppt.h - 
 * Copyright (C) 2003, Christopher James Lahey
 *
 * Authors:
 *   Christopher James Lahey <clahey@ximian.com>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU Library General Public
 * License as published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this file; if not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 **/

#ifndef GOFFICE_PRESENT_LOAD_PPT_H
#define GOFFICE_PRESENT_LOAD_PPT_H

#include <libpresent/present-presentation.h>

PresentPresentation *
load_ppt (char *input_file);

#endif
