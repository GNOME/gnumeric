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

#ifdef SUPPORT_OLD_EPSF
/* This is disabled and will probably be removed.  Printing support can produce
 * postscript and handles things like spanning cells much better.   There is no
 * reason to fix the breakage here.
 */
#include "epsf.h"
#endif

/*
 * We can unload
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
	file_format_unregister_save (html_write_wb_roff_dvi);
	file_format_unregister_save (html_write_wb_roff_pdf);
	file_format_unregister_save (html_write_wb_roff);
#ifdef SUPPORT_OLD_EPSF
	file_format_unregister_save (epsf_write_wb);
#endif
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
	file_format_register_save (".html", desc, FILE_FL_AUTO,
				   html_write_wb_html32);

	desc = _("HTML 4.0 file format (*.html)");
	file_format_register_save (".html", desc, FILE_FL_AUTO,
				   html_write_wb_html40);

	desc = _("HTML file made by gnumeric");
		/* Register file format with priority 100 */
	file_format_register_open (100, desc, NULL, html_read);

	desc = _("LaTeX file format (*.tex)");
	file_format_register_save (".tex", desc, FILE_FL_WRITE_ONLY,
				   html_write_wb_latex);

	desc = _("LaTeX2e file format (*.tex)");
	file_format_register_save (".tex", desc, FILE_FL_WRITE_ONLY,
				   html_write_wb_latex2e);

	desc = _("DVI TeX file format (via groff)");
	file_format_register_save (".dvi", desc, FILE_FL_WRITE_ONLY,
				   html_write_wb_roff_dvi);

	desc = _("TROFF file format (*.me)");
	file_format_register_save (".me", desc, FILE_FL_WRITE_ONLY,
				   html_write_wb_roff);

	desc = _("PDF file format (via groff/gs)");
	file_format_register_save (".pdf", desc, FILE_FL_WRITE_ONLY,
				   html_write_wb_roff_pdf);

#ifdef SUPPORT_OLD_EPSF
	desc = _("EPS file format (*.eps)");
	file_format_register_save (".eps", desc, FILE_FL_WRITE_ONLY,
				   epsf_write_wb);
#endif
}

/*
 * called by gnumeric to load the plugin
 */
PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	html_init ();

	if (plugin_data_init (pd, &html_can_unload, &html_cleanup_plugin,
			      _("HTML"),
			      _("Import/Export of HTML, TeX, DVI, roff and pdf")))
	        return PLUGIN_OK;
	else
	        return PLUGIN_ERROR;

}

