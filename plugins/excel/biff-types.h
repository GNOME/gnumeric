#ifndef GNM_EXCEL_BIFF_TYPES_H
#define GNM_EXCEL_BIFF_TYPES_H

/*
 * biff-types.h: A long and dull list of BIFF types.
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 */

const char *biff_opcode_name (unsigned int opcode);

#define BIFF_DIMENSIONS_v0		0x000
#define BIFF_DIMENSIONS_v2			0x200
#define BIFF_BLANK_v0			0x001
#define BIFF_BLANK_v2				0x201
#define BIFF_INTEGER			0x002
#define BIFF_NUMBER_v0			0x003
#define BIFF_NUMBER_v2				0x203
#define BIFF_LABEL_v0			0x004
#define BIFF_LABEL_v2				0x204
#define BIFF_BOOLERR_v0			0x005
#define BIFF_BOOLERR_v2				0x205
#define BIFF_FORMULA_v0			0x006
#define BIFF_FORMULA_v2				0x206
#define BIFF_FORMULA_v4					0x406
#define BIFF_STRING_v0			0x007
#define BIFF_STRING_v2				0x207
#define BIFF_ROW_v0			0x008
#define BIFF_ROW_v2				0x208
#define BIFF_BOF_v0			0x009
#define BIFF_BOF_v2				0x209
#define BIFF_BOF_v4					0x409
#define BIFF_BOF_v8						0x809
#define BIFF_EOF			0x00a
#define BIFF_INDEX_v0			0x00b
#define BIFF_INDEX_v2				0x20b
#define BIFF_CALCCOUNT			0x00c
#define BIFF_CALCMODE			0x00d
#define BIFF_PRECISION			0x00e
#define BIFF_REFMODE			0x00f
#define BIFF_DELTA			0x010
#define BIFF_ITERATION			0x011
#define BIFF_PROTECT			0x012
#define BIFF_PASSWORD			0x013
#define BIFF_HEADER			0x014
#define BIFF_FOOTER			0x015
#define BIFF_EXTERNCOUNT		0x016
#define BIFF_EXTERNSHEET		0x017
#define BIFF_NAME_v0			0x018
#define BIFF_NAME_v2				0x218
#define BIFF_WINDOWPROTECT		0x019
#define BIFF_VERTICALPAGEBREAKS		0x01a
#define BIFF_HORIZONTALPAGEBREAKS	0x01b
#define BIFF_NOTE			0x01c
#define BIFF_SELECTION			0x01d
#define BIFF_FORMAT_v0			0x01e
#define BIFF_FORMAT_v4					0x41e
#define BIFF_FORMATCOUNT		0x01f	/* Undocumented */
#define BIFF_COLUMNDEFAULT		0x020	/* Undocumented */
#define BIFF_ARRAY_v0			0x021
#define BIFF_ARRAY_v2				0x221
#define BIFF_1904			0x022
#define BIFF_EXTERNNAME_v0		0x023
#define BIFF_EXTERNNAME_v2			0x223
#define BIFF_COLWIDTH			0x024	/* Undocumented */
#define BIFF_DEFAULTROWHEIGHT_v0	0x025
#define BIFF_DEFAULTROWHEIGHT_v2		0x225
#define BIFF_LEFT_MARGIN		0x026
#define BIFF_RIGHT_MARGIN		0x027
#define BIFF_TOP_MARGIN			0x028
#define BIFF_BOTTOM_MARGIN		0x029
#define BIFF_PRINTHEADERS		0x02a
#define BIFF_PRINTGRIDLINES		0x02b
#define BIFF_FILEPASS			0x02f
#define BIFF_FONT_v0			0x031
#define BIFF_FONT_v2				0x231
#define BIFF_FONTCOUNT			0x032	/* Undocumented */
#define BIFF_PRINTSIZE			0x033	/* Undocumented */
#define BIFF_TABLE_v0			0x036
#define BIFF_TABLE_v2				0x236
#define BIFF_TABLE2			0x037	/* OOo has docs */
#define BIFF_WNDESK			0x038	/* Undocumented */
#define BIFF_ZOOM			0x039	/* Undocumented */
#define BIFF_BEGINPREF			0x03a	/* Undocumented */
#define BIFF_ENDPREF			0x03b	/* Undocumented */
#define BIFF_CONTINUE			0x03c
#define BIFF_WINDOW1			0x03d
#define BIFF_WINDOW2_v0			0x03e
#define BIFF_WINDOW2_v2				0x23e
#define BIFF_PANE_V2			0x03f	/* Undocumented */
#define BIFF_BACKUP			0x040
#define BIFF_PANE			0x041
#define BIFF_CODEPAGE			0x042
#define BIFF_XF_OLD_v0			0x043
#define BIFF_XF_OLD_v2				0x243
#define BIFF_XF_OLD_v4					0x443
#define BIFF_XF_INDEX			0x044
#define BIFF_FONT_COLOR			0x045
#define BIFF_PLS			0x04d
#define BIFF_DCON			0x050
#define BIFF_DCONREF			0x051
#define BIFF_DCONNAME			0x052
#define BIFF_DEFCOLWIDTH		0x055
#define BIFF_XCT			0x059
#define BIFF_CRN			0x05a
#define BIFF_FILESHARING		0x05b
#define BIFF_WRITEACCESS		0x05c
#define BIFF_OBJ			0x05d
#define BIFF_UNCALCED			0x05e
#define BIFF_SAVERECALC			0x05f
#define BIFF_TEMPLATE			0x060
#define BIFF_INTL			0x061	/* Undocumented */
#define BIFF_TAB_COLOR						0x862	/* Undocumented, OO calls it SHEETLAYOUT */
#define BIFF_OBJPROTECT			0x063
#define BIFF_COLINFO			0x07d
#define BIFF_RK					0x27e /* Odd that there is no 0x7e */
#define BIFF_IMDATA			0x07f
#define BIFF_GUTS			0x080
#define BIFF_WSBOOL			0x081
#define BIFF_GRIDSET			0x082
#define BIFF_HCENTER			0x083
#define BIFF_VCENTER			0x084
#define BIFF_BOUNDSHEET			0x085
#define BIFF_WRITEPROT			0x086
#define BIFF_ADDIN			0x087
#define BIFF_EDG			0x088
#define BIFF_PUB			0x089
/* NOTEOFF is in here somewhere according to biffview, but no firm number */
#define BIFF_COUNTRY			0x08c
#define BIFF_HIDEOBJ			0x08d
#define BIFF_BUNDLESOFFSET		0x08e	/* Undocumented */
#define BIFF_BUNDLEHEADER		0x08f	/* Undocumented */
#define BIFF_SORT			0x090
#define BIFF_SUB			0x091
#define BIFF_PALETTE			0x092
#define BIFF_STYLE				0x293 /* Odd that there is no 0x93 */
#define BIFF_LHRECORD			0x094
#define BIFF_LHNGRAPH			0x095
#define BIFF_SOUND			0x096
#define BIFF_SYNC			0x097	/* Undocumented */
#define BIFF_LPR			0x098
#define BIFF_STANDARDWIDTH		0x099
#define BIFF_FNGROUPNAME		0x09a
#define BIFF_FILTERMODE			0x09b
#define BIFF_FNGROUPCOUNT		0x09c
#define BIFF_AUTOFILTERINFO		0x09d
#define BIFF_AUTOFILTER			0x09e
#define BIFF_SCL			0x0a0
#define BIFF_SETUP			0x0a1
#define BIFF_TOOLBARVER			0x0a4	/* Undocumented */
#define BIFF_COORDLIST			0x0a9
#define BIFF_GCW			0x0ab
#define BIFF_SCENMAN			0x0ae
#define BIFF_SCENARIO			0x0af
#define BIFF_SXVIEW			0x0b0
#define BIFF_SXVD			0x0b1
#define BIFF_SXVI			0x0b2
#define BIFF_SXSI			0x0b3	/* Undocumented */
#define BIFF_SXIVD			0x0b4
#define BIFF_SXLI			0x0b5
#define BIFF_SXPI			0x0b6
#define BIFF_FACENUM			0x0b7	/* Undocumented*/
#define BIFF_DOCROUTE			0x0b8
#define BIFF_RECIPNAME			0x0b9
#define BIFF_SSLIST			0x0ba	/* Undocumented */
#define BIFF_MASKIMDATA			0x0bb	/* Undocumented */
#define BIFF_SHRFMLA					0x4bc
#define BIFF_MULRK			0x0bd
#define BIFF_MULBLANK			0x0be
#define BIFF_TOOLBARHDR			0x0bf	/* Undocumented */
#define BIFF_TOOLBAREND			0x0c0	/* Undocumented */
#define BIFF_MMS			0x0c1
#define BIFF_ADDMENU			0x0c2
#define BIFF_DELMENU			0x0c3
#define BIFF_TIPHISTORY			0x0c4	/* Undocumented */
#define BIFF_SXDI			0x0c5
#define BIFF_SXDB			0x0c6
#define BIFF_SXFDB			0x0c7	/* guessed */
#define BIFF_SXDDB			0x0c8	/* guessed */
#define BIFF_SXNUM			0x0c9	/* guessed */
#define BIFF_SXBOOL			0x0ca	/* guessed */
#define BIFF_SXERR			0x0cb	/* guessed */
#define BIFF_SXINT			0x0cc	/* guessed */
#define BIFF_SXSTRING			0x0cd
#define BIFF_SXDTR			0x0ce	/* guessed */
#define BIFF_SXNIL			0x0cf	/* guessed */
#define BIFF_SXTBL			0x0d0
#define BIFF_SXTBRGIITM			0x0d1
#define BIFF_SXTBPG			0x0d2
#define BIFF_OBPROJ			0x0d3
#define BIFF_SXStreamID			0x0d5
#define BIFF_RSTRING			0x0d6
#define BIFF_DBCELL			0x0d7
#define BIFF_SXNUMGROUP			0x0d8	/* from OO : numerical grouping in pivot cache field */
#define BIFF_BOOKBOOL			0x0da
#define BIFF_PARAMQRY			0x0dc	/* DUPLICATE dc */
#define BIFF_SXEXT			0x0dc	/* DUPLICATE dc */
#define BIFF_SCENPROTECT		0x0dd
#define BIFF_OLESIZE			0x0de
#define BIFF_UDDESC			0x0df
#define BIFF_XF				0x0e0
#define BIFF_INTERFACEHDR		0x0e1
#define BIFF_INTERFACEEND		0x0e2
#define BIFF_SXVS			0x0e3
#define BIFF_MERGECELLS			0x0e5	/* guessed */
#define BIFF_BG_PIC			0x0e9	/* Undocumented */
#define BIFF_TABIDCONF			0x0ea
#define BIFF_MS_O_DRAWING_GROUP		0x0eb
#define BIFF_MS_O_DRAWING		0x0ec
#define BIFF_MS_O_DRAWING_SELECTION	0x0ed
#define BIFF_PHONETIC			0x0ef	/* semi-Undocumented */
#define BIFF_SXRULE			0x0f0
#define BIFF_SXEX			0x0f1
#define BIFF_SXFILT			0x0f2
#define BIFF_SXNAME			0x0f6
#define BIFF_SXSELECT			0x0f7
#define BIFF_SXPAIR			0x0f8
#define BIFF_SXFMLA			0x0f9
#define BIFF_SXFORMAT			0x0fb
#define BIFF_SST			0x0fc
#define BIFF_LABELSST			0x0fd
#define BIFF_EXTSST			0x0ff
#define BIFF_SXVDEX			0x100
#define BIFF_SXFORMULA			0x103
#define BIFF_SXDBEX			0x122
#define BIFF_CHTRINSERT			0x137
#define BIFF_CHTRINFO			0x138
#define BIFF_CHTRCELLCONTENT		0x13B
#define BIFF_TABID			0x13d
#define BIFF_CHTRMOVERANGE		0x140
#define BIFF_CHTRINSERTTAB		0x14D
#define BIFF_LABELRANGES		0x15F
#define BIFF_USESELFS			0x160
#define BIFF_DSF			0x161
#define BIFF_XL5MODIFY			0x162
#define BIFF_CHTRHEADER			0x196
#define BIFF_FILESHARING2		0x1a5
#define BIFF_USERDBVIEW			0x1a9
#define BIFF_USERSVIEWBEGIN		0x1aa
#define BIFF_USERSVIEWEND		0x1ab
#define BIFF_QSI			0x1ad
#define BIFF_SUPBOOK			0x1ae
#define BIFF_PROT4REV			0x1af
#define BIFF_CONDFMT			0x1b0
#define BIFF_CF				0x1b1
#define BIFF_DVAL			0x1b2
#define BIFF_DCONBIN			0x1b5
#define BIFF_TXO			0x1b6
#define BIFF_REFRESHALL			0x1b7
#define BIFF_HLINK			0x1b8
#define BIFF_CODENAME			0x1ba	/* TYPO in MS Docs */
#define BIFF_SXFDBTYPE			0x1bb
#define BIFF_PROT4REVPASS		0x1bc
#define BIFF_DV				0x1be
#define BIFF_XL9FILE			0x1c0
#define BIFF_RECALCID			0x1c1

/* new in 2000 */
#define BIFF_LINK_TIP			0x800	/* follows an hlink */
#define BIFF_WEBPUB			0x801
#define BIFF_QSISXTAG			0x802
#define BIFF_DBQUERYEXT			0x803
#define BIFF_EXTSTRING			0x804
#define BIFF_TXTQUERY			0x805 /* see #153260 for sample */
#define BIFF_QSIR			0x806
#define BIFF_QSIF			0x807
#define BIFF_OLEDBCONN			0x80A
#define BIFF_WOPT			0x80B
#define BIFF_SXVIEWEX			0x80C
#define BIFF_SXTH			0x80D
#define BIFF_SXPIEX			0x80E
#define BIFF_SXVDTEX			0x80F
#define BIFF_SXVIEWEX9			0x810
#define BIFF_CONTINUEFRT		0x812
#define BIFF_REALTIMEDATA		0x813
#define BIFF_SHEETEXT			0x862
#define BIFF_BOOKEXT			0x863
#define BIFF_SXADDL			0x864
#define BIFF_CRASHRECERR		0x865
#define BIFF_HFPICTURE			0x866
#define BIFF_SHEETPROTECTION		0x867	/* XL calls it FEATHEADR, but the OOo name is clearer */
#define BIFF_RANGEPROTECTION		0x868	/* XL calls it FEAT */

/* Chart Specific */
/* These must be here for the ole program to work, and the suffixes must be
 * lower case for the macros in ms-chart.c to work
 */
#define BIFF_CHART_units		0x1001
#define BIFF_CHART_chart		0x1002
#define BIFF_CHART_series		0x1003
#define BIFF_CHART_dataformat		0x1006
#define BIFF_CHART_lineformat		0x1007
#define BIFF_CHART_markerformat		0x1009
#define BIFF_CHART_areaformat		0x100a
#define BIFF_CHART_pieformat		0x100b
#define BIFF_CHART_attachedlabel	0x100c
#define BIFF_CHART_seriestext		0x100d
#define BIFF_CHART_chartformat		0x1014
#define BIFF_CHART_legend		0x1015
#define BIFF_CHART_serieslist		0x1016
#define BIFF_CHART_bar			0x1017
#define BIFF_CHART_line			0x1018
#define BIFF_CHART_pie			0x1019
#define BIFF_CHART_area			0x101a
#define BIFF_CHART_scatter		0x101b
#define BIFF_CHART_chartline		0x101c
#define BIFF_CHART_axis			0x101d
#define BIFF_CHART_tick			0x101e
#define BIFF_CHART_valuerange		0x101f
#define BIFF_CHART_catserrange		0x1020
#define BIFF_CHART_axislineformat	0x1021
#define BIFF_CHART_chartformatlink	0x1022
#define BIFF_CHART_defaulttext		0x1024
#define BIFF_CHART_text			0x1025
#define BIFF_CHART_fontx		0x1026
#define BIFF_CHART_objectlink		0x1027
#define BIFF_CHART_frame		0x1032
#define BIFF_CHART_begin		0x1033
#define BIFF_CHART_end			0x1034
#define BIFF_CHART_plotarea		0x1035
#define BIFF_CHART_3d			0x103a
#define BIFF_CHART_picf			0x103c
#define BIFF_CHART_dropbar		0x103d
#define BIFF_CHART_radar		0x103e
#define BIFF_CHART_surf			0x103f
#define BIFF_CHART_radararea		0x1040
#define BIFF_CHART_axisparent		0x1041
#define BIFF_CHART_legendxn		0x1043
#define BIFF_CHART_shtprops		0x1044
#define BIFF_CHART_sertocrt		0x1045
#define BIFF_CHART_axesused		0x1046
#define BIFF_CHART_sbaseref		0x1048
#define BIFF_CHART_serparent		0x104a
#define BIFF_CHART_serauxtrend		0x104b
#define BIFF_CHART_ifmt			0x104e
#define BIFF_CHART_pos			0x104f
#define BIFF_CHART_alruns		0x1050
#define BIFF_CHART_ai			0x1051
#define BIFF_CHART_serauxerrbar		0x105b
#define BIFF_CHART_clrtclient		0x105c	/* Undocumented */
#define BIFF_CHART_serfmt		0x105d
#define BIFF_CHART_3dbarshape		0x105f	/* Undocumented */
#define BIFF_CHART_fbi			0x1060
#define BIFF_CHART_boppop		0x1061
#define BIFF_CHART_axcext		0x1062
#define BIFF_CHART_dat			0x1063
#define BIFF_CHART_plotgrowth		0x1064
#define BIFF_CHART_siindex		0x1065
#define BIFF_CHART_gelframe		0x1066
#define BIFF_CHART_boppopcustom		0x1067

/* BIFF types specific to gnumeric */
#define BIFF_CHART_trendlimits	0x10C0

#endif /* GNM_EXCEL_BIFF_TYPES_H */
