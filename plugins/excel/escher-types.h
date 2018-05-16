#ifndef GNM_EXCEL_ESCHER_TYPES_H
#define GNM_EXCEL_ESCHER_TYPES_H

/**
 * escher-types.h: A long and dull list of types used
 *                 in the MS drawing layer.
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 **/

#define DggContainer		0xf000 /* Drawing Group Container */
#define Dgg			0xf006
#define CLSID			0xf016
#define OPT			0xf00b
#define BStoreContainer		0xf001
#define BSE			0xf007
#define Blip_START		0xf018 /* Blip types are between */
#define Blip_END		0xf117 /* these two values */
#define DgContainer		0xf002 /* Drawing Container */
#define Dg			0xf008
#define RegroupItems		0xf118
#define ColorScheme		0xf120 /* bug in docs */
#define SpgrContainer		0xf003
#define SpContainer		0xf004
#define Spgr			0xf009
#define Sp			0xf00a
#define Textbox			0xf00c
#define ClientTextbox		0xf00d
#define Anchor			0xf00e
#define ChildAnchor		0xf00f
#define ClientAnchor		0xf010
#define ClientData		0xf011
#define SolverContainer		0xf005
#define ConnectorRule		0xf012 /* bug in docs */
#define AlignRule		0xf013
#define ArcRule			0xf014
#define ClientRule		0xf015
#define CalloutRule		0xf017
#define Selection		0xf119
#define ColorMRU		0xf11a
#define DeletedPspl		0xf11d /* bug in docs */
#define SplitMenuColors		0xf11e
#define OleObject		0xf11f
#define UserDefined		0xf122

#endif /* GNM_EXCEL_ESCHER_TYPES_H */
