/*
 * boot.c
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
#include "boot.h"
#include "html.h"
#include "latex.h"
#include "roff.h"
#include "file.h"
#include "epsf.h"

/*
 * Q: what's that for?
 */
static int
html_can_unload (PluginData *pd)
{
	return TRUE;
}

/*
 * called when unloading the plugin
 */
static void
html_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_save (html_write_wb_html32);
	file_format_unregister_save (html_write_wb_html40);
	file_format_unregister_save (html_write_wb_latex);
	file_format_unregister_save (html_write_wb_latex2e);
	file_format_unregister_save (html_write_wb_roff_ps);
	file_format_unregister_save (html_write_wb_roff_dvi);
	file_format_unregister_save (html_write_wb_roff_pdf);
	file_format_unregister_save (html_write_wb_roff);
	file_format_unregister_save (epsf_write_wb);
	file_format_unregister_open (NULL,html_read);
}

/*
 * register all file formats
 */
static void
html_init (void)
{
	char *desc;
	desc = _("HTML 3.2 file format (*.html)");
	file_format_register_save (".html", desc, html_write_wb_html32);
	desc = _("HTML 4.0 file format (*.html)");
	file_format_register_save (".html", desc, html_write_wb_html40);
	desc = _("HTML file made by gnumeric");
		/* Q: what does the '100' mean? */
	file_format_register_open (100, desc, NULL, html_read);

	desc = _("LaTeX file format (*.tex)");
	file_format_register_save (".tex", desc, html_write_wb_latex);
	desc = _("LaTeX2e file format (*.tex)");
	file_format_register_save (".tex", desc, html_write_wb_latex2e);

	desc = _("PS file format (via groff)");
	file_format_register_save (".ps", desc, html_write_wb_roff_ps);
	desc = _("DVI TeX file format (via groff)");
	file_format_register_save (".dvi", desc, html_write_wb_roff_dvi);
	desc = _("TROFF file format (*.me)");
	file_format_register_save (".me", desc, html_write_wb_roff);
	desc = _("PDF file format (via groff/gs)");
	file_format_register_save (".pdf", desc, html_write_wb_roff_pdf);

	desc = _("EPS file format (*.eps)");
	file_format_register_save (".eps", desc, epsf_write_wb);
}

/*
 * called by gnumeric to load the plugin
 */
int
init_plugin (PluginData *pd)
{
	html_init ();
	pd->can_unload = html_can_unload;
	pd->cleanup_plugin = html_cleanup_plugin;
	pd->title = g_strdup (_("HTML (simple html export/import plugin)"));
	return 0;
}

