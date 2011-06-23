/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * xls-write-docprops.c: MS Excel XLSX export of document properties
 *
 * Copyright (C) 2011 Andreas J. Guelzow, All rights reserved 
 * aguelzow@pyrshep.ca
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 **/

/*
 *
 * DO * NOT * COMPILE * DIRECTLY *
 * DO * NOT * COMPILE * DIRECTLY *
 * DO * NOT * COMPILE * DIRECTLY *
 *
 * included via xlsx-write.c
 **/

static void
xlsx_write_docprops_app (XLSXWriteState *state, GsfOutfile *root_part, GsfOutfile *docprops_dir)
{
	GsfOutput *part = gsf_outfile_open_pkg_add_rel 
		(docprops_dir, "app.xml",
		 "application/vnd.openxmlformats-officedocument.extended-properties+xml",
		 root_part,
		 "http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties");
	GsfXMLOut *xml = gsf_xml_out_new (part);
	
	gsf_xml_out_start_element (xml, "Properties");
	gsf_xml_out_add_cstr_unchecked (xml, "xmlns", ns_docprops_extended);
	gsf_xml_out_add_cstr_unchecked (xml, "xml:space", "preserve");
	gsf_xml_out_start_element (xml, "Application");
	gsf_xml_out_add_cstr_unchecked (xml, NULL, PACKAGE_NAME);
	gsf_xml_out_end_element (xml); /* </Application> */
	gsf_xml_out_start_element (xml, "AppVersion");
	gsf_xml_out_add_cstr_unchecked (xml, NULL, VERSION);
	gsf_xml_out_end_element (xml); /* </AppVersion> */
	gsf_xml_out_end_element (xml); /* </Properties> */
	
	g_object_unref (xml);
	gsf_output_close (part);
	g_object_unref (part);
}

static void
xlsx_write_docprops_core (XLSXWriteState *state, GsfOutfile *root_part, GsfOutfile *docprops_dir)
{

}

static void
xlsx_write_docprops (XLSXWriteState *state, GsfOutfile *root_part)
{
	GsfOutfile *docprops_dir    = (GsfOutfile *)gsf_outfile_new_child (root_part, "docProps", TRUE);

	xlsx_write_docprops_app (state, root_part, docprops_dir);
	xlsx_write_docprops_core (state, root_part, docprops_dir);
}
