/**
 * escher-types.h: A long and dull list of types used
 *                 in the MS drawing layer.
 *
 * Author:
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998, 1999, 2000 Michael Meeks
 **/
/**
 * See S59FDC.HTM for the spec.
 * MS use similar names with 'msofbt' prefix.
 **/

#define DggContainer           0xf000 /* Drawing Group Container */
#define Dgg                    0xf006
#define CLSID                  0xf016
#define OPT                    0xf00b
#define ColorMRU               0xf11a
#define SplitMenuColors        0xf11e
#define BStoreContainer        0xf001
#define BSE                    0xf007
#define Blip_START             0xf018 /* Blip types are between */
#define Blip_END               0xf117 /* these two values */
#define DgContainer            0xf002 /* Drawing Container */
#define Dg                     0xf008
#define RegroupItems           0xf118
#define ColorScheme            0xf120 /* bug in docs */
#define SpgrContainer          0xf003
#define SpContainer            0xf004
#define Spgr                   0xf009
#define Sp                     0xf00a
#define Textbox                0xf00c
#define ClientTextbox          0xf00d
#define Anchor                 0xf00e
#define ChildAnchor            0xf00f
#define ClientAnchor           0xf010
#define ClientData             0xf011
#define OleObject              0xf11f
#define DeletedPspl            0xf11d /* bug in docs */
#define SolverContainer        0xf005
#define ConnectorRule          0xf012 /* bug in docs */
#define AlignRule              0xf013
#define ArcRule                0xf014
#define ClientRule             0xf015
#define CalloutRule            0xf017
#define Selection              0xf119
