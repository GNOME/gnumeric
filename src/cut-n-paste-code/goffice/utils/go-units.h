/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * go-units.h : 
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#ifndef GO_UNITS_H
#define GO_UNITS_H

G_BEGIN_DECLS

/* Conversion factors */
#define PT_PER_IN 72.0
#define CM_PER_IN 2.54
#define PT_PER_CM (PT_PER_IN/CM_PER_IN)
#define IN_PER_CM (1.0/CM_PER_IN)
#define IN_PER_PT (1.0/PT_PER_IN)
#define CM_PER_PT (1.0/PT_PER_CM)

#define GO_IN_TO_PT(inch)	((inch)*PT_PER_IN)
#define GO_IN_TO_CM(inch)	((inch)*CM_PER_IN)
#define GO_CM_TO_PT(inch)	((inch)*PT_PER_CM)
#define GO_CM_TO_IN(inch)	((inch)*IN_PER_CM)
#define GO_PT_TO_CM(inch)	((inch)*CM_PER_PT)
#define GO_PT_TO_IN(inch)	((inch)*IN_PER_PT)

G_END_DECLS

#endif /* GO_UNITS_H */
