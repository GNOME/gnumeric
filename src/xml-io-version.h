/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GNUMERIC_XML_IO_VERSION_H
#define GNUMERIC_XML_IO_VERSION_H

typedef enum
{
	GNM_XML_UNKNOWN = -1,
	GNM_XML_V1,
	GNM_XML_V2,
	GNM_XML_V3,	/* >= 0.52 */
	GNM_XML_V4,	/* >= 0.57 */
	GNM_XML_V5,	/* >= 0.58 */
	GNM_XML_V6,	/* >= 0.62 */
	GNM_XML_V7,	/* >= 0.66 */
	GNM_XML_V8,	/* >= 0.71 */
	GNM_XML_V9,	/* >= 0.73 add print scaling */
	GNM_XML_V10,	/* >= 1.03 remove useless Content node in cells */

	/* NOTE : Keep this up to date (and in sync with the schema) */
	GNM_XML_LATEST = GNM_XML_V10
} GnumericXMLVersion;

#endif /* GNUMERIC_XML_IO_VERSION_H */
