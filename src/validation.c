/*
 * validation.c: Implementation of validation.
 *
 * Copyright (C) Almer S. Tigelaar <almer@gnome.org>
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

#include <config.h>
#include "validation.h"

/**
 * validation_new :
 * @vs :
 * @title :
 * @msg :
 * @sc :
 *
 * Absorb the StyleCondition reference.
 */
Validation *
validation_new (ValidationStyle vs, char const *title,
		char const *msg, StyleCondition *sc)
{
	Validation *v;

	g_return_val_if_fail (sc != NULL, NULL);

	v = g_new0 (Validation, 1);
	v->ref_count = 1;
	
	v->vs    = vs;
	v->title = title ? string_get (title) : NULL;
	v->msg   = msg ? string_get (msg) : NULL;
	v->sc    = sc;

	return v;
}

void
validation_ref (Validation *v)
{
	g_return_if_fail (v != NULL);
	v->ref_count++;
}

void
validation_unref (Validation *v)
{
	g_return_if_fail (v != NULL);

	v->ref_count--;
	
	if (v->ref_count < 1) {
		if (v->title)
			string_unref (v->title);
		if (v->msg)
			string_unref (v->msg);
		style_condition_unref (v->sc);
		g_free (v);
	}
}

void
validation_link (Validation *v, Sheet *sheet)
{
	g_return_if_fail (v != NULL);
	style_condition_link (v->sc, sheet);
}

void
validation_unlink (Validation *v)
{
	g_return_if_fail (v != NULL);
	style_condition_unlink (v->sc);
}
