#ifndef GNUMERIC_PLUGIN_GUILE_SMOB_VALUE_H
#define GNUMERIC_PLUGIN_GUILE_SMOB_VALUE_H

/* -*- mode: c; c-basic-offset: 8 -*- */

/*
 *
 *     Author: Ariel Rios <ariel@arcavia.com>
 *	   Copyright Ariel Rios 2000
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307 USA
 */


#include <libguile.h>
#include "value.h"

void init_value_type (void);

SCM make_new_smob (Value *);
Value *get_value_from_smob (SCM);

#endif
