/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dao-gui-utils.h:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2001, 2002 by Andreas J. Guelzow  <aguelzow@taliesin.ca>
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

#ifndef GNUMERIC_DAO_GUI_UTILS_H
#define GNUMERIC_DAO_GUI_UTILS_H

#include "gnumeric.h"
#include "numbers.h"
#include <widgets/gnumeric-expr-entry.h>
#include <glade/glade.h>
#include "tool-dialogs.h"
#include "tools/dao.h"


extern char const * const output_group[];


void dialog_tool_init_outputs (GenericToolState *state, GCallback sensitivity_cb);
data_analysis_output_t *parse_output (GenericToolState *state, data_analysis_output_t *dao);
void dialog_tool_preset_to_range (GenericToolState *state);


#endif
