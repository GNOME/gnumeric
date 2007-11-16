/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_XML_SAX_H_
# define _GNM_XML_SAX_H_

#include <gnumeric.h>
#include <gsf/gsf.h>
#include <gsf/gsf-output-memory.h>

G_BEGIN_DECLS

void	gnm_xml_file_open	(GOFileOpener const *fo, IOContext *io_context,
				 gpointer wb_view, GsfInput *input);

void	gnm_xml_file_save	(GOFileSaver const *fs, IOContext *io_context,
				 gconstpointer wb_view, GsfOutput *output);
GsfOutputMemory *
	gnm_cellregion_to_xml	(GnmCellRegion const *cr);

G_END_DECLS

#endif /* _GNM_XML_SAX_H_ */
