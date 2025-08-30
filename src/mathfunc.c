/*
 * mathfunc.c:  Mathematical functions.
 *
 * Authors:
 *   Ross Ihaka.  (See note 1.)
 *   The R Development Core Team.  (See note 1.)
 *   Morten Welinder <terra@gnome.org>
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Ian Smith (iandjmsmith@aol.com).  (See note 2.)
 */

/*
 * NOTE 1: most of this file comes from the "R" package, notably version 2
 * or newer (we re-sync from time to time).
 * "R" is distributed under GPL licence, see file COPYING.
 * The relevant parts are copyright (C) 1998 Ross Ihaka and
 * 2000-2004 The R Development Core Team.
 *
 * Thank you!
 */

/*
 * NOTE 2: the pbeta (and support) code comes from Ian Smith.  (Translated
 * into C, adapted to Gnumeric naming conventions, and R's API conventions
 * by Morten Welinder.  Blame me for problems.)
 *
 * Copyright © Ian Smith 2002-2003
 * Version 1.0.24
 * Thanks to Jerry W. Lewis for help with testing of and improvements to the code.
 *
 * Thank you!
 */


#include <gnumeric-config.h>
#include <gnumeric.h>
#include <mathfunc.h>
#include <sf-dpq.h>
#include <sf-gamma.h>
#include <glib/gi18n-lib.h>

#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <unistd.h>
#include <string.h>
#include <goffice/goffice.h>
#include <glib/gstdio.h>
#include <value.h>
#include <gutils.h>

/* R code wants this, so provide it.  */
#ifndef IEEE_754
#define IEEE_754
#endif

#define M_SQRT_32       GNM_const(5.656854249492380195206754896838)  /* sqrt(32) */
#define M_1_SQRT_2PI    GNM_const(0.398942280401432677939946059934)  /* 1/sqrt(2pi) */
#define M_2PIgnum       (2 * M_PIgnum)
#define M_1_PIgnum      GNM_const(0.318309886183790671537767526745)  /* 1/pi */

#define ML_ERROR(cause) do { } while(0)
#define MATHLIB_WARNING g_warning
#define REprintf g_warning
#define ML_WARNING(typ,what) g_printerr("mathfunc: trouble in %s\n", (what))

static inline gnm_float fmin2 (gnm_float x, gnm_float y) { return MIN (x, y); }
static inline gnm_float fmax2 (gnm_float x, gnm_float y) { return MAX (x, y); }

#define MATHLIB_STANDALONE
#define ML_ERR_return_NAN { return gnm_nan; }
#define ML_WARN_return_NAN { return gnm_nan; }
static void pnorm_both (gnm_float x, gnm_float *cum, gnm_float *ccum, int i_tail, gboolean log_p);

# define R_nonint(x) 	  (gnm_abs((x) - gnm_round(x)) > GNM_const(1e-7)*fmax2(1, gnm_abs(x)))

/* MW ---------------------------------------------------------------------- */

void
mathfunc_init (void)
{
	/* Nothing, for the time being.  */
}

/*
 * A table of logs for scaling purposes.  Each value has four parts with
 * 23 bits in each.  That means each part can be multiplied by a double
 * with at most 30 bits set and not have any rounding error.  Note, that
 * the first entry is log(2).
 *
 * Entry i is associated with the value r = 0.5 + i / 256.0.  The
 * argument to log is p/q where q=1024 and p=floor(q / r + 0.5).
 * Thus r*p/q is close to 1.
 */
static const float bd0_scale[128 + 1][4] = {
	{ +0x1.62e430p-1, -0x1.05c610p-29, -0x1.950d88p-54, +0x1.d9cc02p-79 }, /* 128: log(2048/1024.) */
	{ +0x1.5ee02cp-1, -0x1.6dbe98p-25, -0x1.51e540p-50, +0x1.2bfa48p-74 }, /* 129: log(2032/1024.) */
	{ +0x1.5ad404p-1, +0x1.86b3e4p-26, +0x1.9f6534p-50, +0x1.54be04p-74 }, /* 130: log(2016/1024.) */
	{ +0x1.570124p-1, -0x1.9ed750p-25, -0x1.f37dd0p-51, +0x1.10b770p-77 }, /* 131: log(2001/1024.) */
	{ +0x1.5326e4p-1, -0x1.9b9874p-25, -0x1.378194p-49, +0x1.56feb2p-74 }, /* 132: log(1986/1024.) */
	{ +0x1.4f4528p-1, +0x1.aca70cp-28, +0x1.103e74p-53, +0x1.9c410ap-81 }, /* 133: log(1971/1024.) */
	{ +0x1.4b5bd8p-1, -0x1.6a91d8p-25, -0x1.8e43d0p-50, -0x1.afba9ep-77 }, /* 134: log(1956/1024.) */
	{ +0x1.47ae54p-1, -0x1.abb51cp-25, +0x1.19b798p-51, +0x1.45e09cp-76 }, /* 135: log(1942/1024.) */
	{ +0x1.43fa00p-1, -0x1.d06318p-25, -0x1.8858d8p-49, -0x1.1927c4p-75 }, /* 136: log(1928/1024.) */
	{ +0x1.3ffa40p-1, +0x1.1a427cp-25, +0x1.151640p-53, -0x1.4f5606p-77 }, /* 137: log(1913/1024.) */
	{ +0x1.3c7c80p-1, -0x1.19bf48p-34, +0x1.05fc94p-58, -0x1.c096fcp-82 }, /* 138: log(1900/1024.) */
	{ +0x1.38b320p-1, +0x1.6b5778p-25, +0x1.be38d0p-50, -0x1.075e96p-74 }, /* 139: log(1886/1024.) */
	{ +0x1.34e288p-1, +0x1.d9ce1cp-25, +0x1.316eb8p-49, +0x1.2d885cp-73 }, /* 140: log(1872/1024.) */
	{ +0x1.315124p-1, +0x1.c2fc60p-29, -0x1.4396fcp-53, +0x1.acf376p-78 }, /* 141: log(1859/1024.) */
	{ +0x1.2db954p-1, +0x1.720de4p-25, -0x1.d39b04p-49, -0x1.f11176p-76 }, /* 142: log(1846/1024.) */
	{ +0x1.2a1b08p-1, -0x1.562494p-25, +0x1.a7863cp-49, +0x1.85dd64p-73 }, /* 143: log(1833/1024.) */
	{ +0x1.267620p-1, +0x1.3430e0p-29, -0x1.96a958p-56, +0x1.f8e636p-82 }, /* 144: log(1820/1024.) */
	{ +0x1.23130cp-1, +0x1.7bebf4p-25, +0x1.416f1cp-52, -0x1.78dd36p-77 }, /* 145: log(1808/1024.) */
	{ +0x1.1faa34p-1, +0x1.70e128p-26, +0x1.81817cp-50, -0x1.c2179cp-76 }, /* 146: log(1796/1024.) */
	{ +0x1.1bf204p-1, +0x1.3a9620p-28, +0x1.2f94c0p-52, +0x1.9096c0p-76 }, /* 147: log(1783/1024.) */
	{ +0x1.187ce4p-1, -0x1.077870p-27, +0x1.655a80p-51, +0x1.eaafd6p-78 }, /* 148: log(1771/1024.) */
	{ +0x1.1501c0p-1, -0x1.406cacp-25, -0x1.e72290p-49, +0x1.5dd800p-73 }, /* 149: log(1759/1024.) */
	{ +0x1.11cb80p-1, +0x1.787cd0p-25, -0x1.efdc78p-51, -0x1.5380cep-77 }, /* 150: log(1748/1024.) */
	{ +0x1.0e4498p-1, +0x1.747324p-27, -0x1.024548p-51, +0x1.77a5a6p-75 }, /* 151: log(1736/1024.) */
	{ +0x1.0b036cp-1, +0x1.690c74p-25, +0x1.5d0cc4p-50, -0x1.c0e23cp-76 }, /* 152: log(1725/1024.) */
	{ +0x1.077070p-1, -0x1.a769bcp-27, +0x1.452234p-52, +0x1.6ba668p-76 }, /* 153: log(1713/1024.) */
	{ +0x1.04240cp-1, -0x1.a686acp-27, -0x1.ef46b0p-52, -0x1.5ce10cp-76 }, /* 154: log(1702/1024.) */
	{ +0x1.00d22cp-1, +0x1.fc0e10p-25, +0x1.6ee034p-50, -0x1.19a2ccp-74 }, /* 155: log(1691/1024.) */
	{ +0x1.faf588p-2, +0x1.ef1e64p-27, -0x1.26504cp-54, -0x1.b15792p-82 }, /* 156: log(1680/1024.) */
	{ +0x1.f4d87cp-2, +0x1.d7b980p-26, -0x1.a114d8p-50, +0x1.9758c6p-75 }, /* 157: log(1670/1024.) */
	{ +0x1.ee1414p-2, +0x1.2ec060p-26, +0x1.dc00fcp-52, +0x1.f8833cp-76 }, /* 158: log(1659/1024.) */
	{ +0x1.e7e32cp-2, -0x1.ac796cp-27, -0x1.a68818p-54, +0x1.235d02p-78 }, /* 159: log(1649/1024.) */
	{ +0x1.e108a0p-2, -0x1.768ba4p-28, -0x1.f050a8p-52, +0x1.00d632p-82 }, /* 160: log(1638/1024.) */
	{ +0x1.dac354p-2, -0x1.d3a6acp-30, +0x1.18734cp-57, -0x1.f97902p-83 }, /* 161: log(1628/1024.) */
	{ +0x1.d47424p-2, +0x1.7dbbacp-31, -0x1.d5ada4p-56, +0x1.56fcaap-81 }, /* 162: log(1618/1024.) */
	{ +0x1.ce1af0p-2, +0x1.70be7cp-27, +0x1.6f6fa4p-51, +0x1.7955a2p-75 }, /* 163: log(1608/1024.) */
	{ +0x1.c7b798p-2, +0x1.ec36ecp-26, -0x1.07e294p-50, -0x1.ca183cp-75 }, /* 164: log(1598/1024.) */
	{ +0x1.c1ef04p-2, +0x1.c1dfd4p-26, +0x1.888eecp-50, -0x1.fd6b86p-75 }, /* 165: log(1589/1024.) */
	{ +0x1.bb7810p-2, +0x1.478bfcp-26, +0x1.245b8cp-50, +0x1.ea9d52p-74 }, /* 166: log(1579/1024.) */
	{ +0x1.b59da0p-2, -0x1.882b08p-27, +0x1.31573cp-53, -0x1.8c249ap-77 }, /* 167: log(1570/1024.) */
	{ +0x1.af1294p-2, -0x1.b710f4p-27, +0x1.622670p-51, +0x1.128578p-76 }, /* 168: log(1560/1024.) */
	{ +0x1.a925d4p-2, -0x1.0ae750p-27, +0x1.574ed4p-51, +0x1.084996p-75 }, /* 169: log(1551/1024.) */
	{ +0x1.a33040p-2, +0x1.027d30p-29, +0x1.b9a550p-53, -0x1.b2e38ap-78 }, /* 170: log(1542/1024.) */
	{ +0x1.9d31c0p-2, -0x1.5ec12cp-26, -0x1.5245e0p-52, +0x1.2522d0p-79 }, /* 171: log(1533/1024.) */
	{ +0x1.972a34p-2, +0x1.135158p-30, +0x1.a5c09cp-56, +0x1.24b70ep-80 }, /* 172: log(1524/1024.) */
	{ +0x1.911984p-2, +0x1.0995d4p-26, +0x1.3bfb5cp-50, +0x1.2c9dd6p-75 }, /* 173: log(1515/1024.) */
	{ +0x1.8bad98p-2, -0x1.1d6144p-29, +0x1.5b9208p-53, +0x1.1ec158p-77 }, /* 174: log(1507/1024.) */
	{ +0x1.858b58p-2, -0x1.1b4678p-27, +0x1.56cab4p-53, -0x1.2fdc0cp-78 }, /* 175: log(1498/1024.) */
	{ +0x1.7f5fa0p-2, +0x1.3aaf48p-27, +0x1.461964p-51, +0x1.4ae476p-75 }, /* 176: log(1489/1024.) */
	{ +0x1.79db68p-2, -0x1.7e5054p-26, +0x1.673750p-51, -0x1.a11f7ap-76 }, /* 177: log(1481/1024.) */
	{ +0x1.744f88p-2, -0x1.cc0e18p-26, -0x1.1e9d18p-50, -0x1.6c06bcp-78 }, /* 178: log(1473/1024.) */
	{ +0x1.6e08ecp-2, -0x1.5d45e0p-26, -0x1.c73ec8p-50, +0x1.318d72p-74 }, /* 179: log(1464/1024.) */
	{ +0x1.686c80p-2, +0x1.e9b14cp-26, -0x1.13bbd4p-50, -0x1.efeb1cp-78 }, /* 180: log(1456/1024.) */
	{ +0x1.62c830p-2, -0x1.a8c70cp-27, -0x1.5a1214p-51, -0x1.bab3fcp-79 }, /* 181: log(1448/1024.) */
	{ +0x1.5d1bdcp-2, -0x1.4fec6cp-31, +0x1.423638p-56, +0x1.ee3feep-83 }, /* 182: log(1440/1024.) */
	{ +0x1.576770p-2, +0x1.7455a8p-26, -0x1.3ab654p-50, -0x1.26be4cp-75 }, /* 183: log(1432/1024.) */
	{ +0x1.5262e0p-2, -0x1.146778p-26, -0x1.b9f708p-52, -0x1.294018p-77 }, /* 184: log(1425/1024.) */
	{ +0x1.4c9f08p-2, +0x1.e152c4p-26, -0x1.dde710p-53, +0x1.fd2208p-77 }, /* 185: log(1417/1024.) */
	{ +0x1.46d2d8p-2, +0x1.c28058p-26, -0x1.936284p-50, +0x1.9fdd68p-74 }, /* 186: log(1409/1024.) */
	{ +0x1.41b940p-2, +0x1.cce0c0p-26, -0x1.1a4050p-50, +0x1.bc0376p-76 }, /* 187: log(1402/1024.) */
	{ +0x1.3bdd24p-2, +0x1.d6296cp-27, +0x1.425b48p-51, -0x1.cddb2cp-77 }, /* 188: log(1394/1024.) */
	{ +0x1.36b578p-2, -0x1.287ddcp-27, -0x1.2d0f4cp-51, +0x1.38447ep-75 }, /* 189: log(1387/1024.) */
	{ +0x1.31871cp-2, +0x1.2a8830p-27, +0x1.3eae54p-52, -0x1.898136p-77 }, /* 190: log(1380/1024.) */
	{ +0x1.2b9304p-2, -0x1.51d8b8p-28, +0x1.27694cp-52, -0x1.fd852ap-76 }, /* 191: log(1372/1024.) */
	{ +0x1.265620p-2, -0x1.d98f3cp-27, +0x1.a44338p-51, -0x1.56e85ep-78 }, /* 192: log(1365/1024.) */
	{ +0x1.211254p-2, +0x1.986160p-26, +0x1.73c5d0p-51, +0x1.4a861ep-75 }, /* 193: log(1358/1024.) */
	{ +0x1.1bc794p-2, +0x1.fa3918p-27, +0x1.879c5cp-51, +0x1.16107cp-78 }, /* 194: log(1351/1024.) */
	{ +0x1.1675ccp-2, -0x1.4545a0p-26, +0x1.c07398p-51, +0x1.f55c42p-76 }, /* 195: log(1344/1024.) */
	{ +0x1.111ce4p-2, +0x1.f72670p-37, -0x1.b84b5cp-61, +0x1.a4a4dcp-85 }, /* 196: log(1337/1024.) */
	{ +0x1.0c81d4p-2, +0x1.0c150cp-27, +0x1.218600p-51, -0x1.d17312p-76 }, /* 197: log(1331/1024.) */
	{ +0x1.071b84p-2, +0x1.fcd590p-26, +0x1.a3a2e0p-51, +0x1.fe5ef8p-76 }, /* 198: log(1324/1024.) */
	{ +0x1.01ade4p-2, -0x1.bb1844p-28, +0x1.db3cccp-52, +0x1.1f56fcp-77 }, /* 199: log(1317/1024.) */
	{ +0x1.fa01c4p-3, -0x1.12a0d0p-29, -0x1.f71fb0p-54, +0x1.e287a4p-78 }, /* 200: log(1311/1024.) */
	{ +0x1.ef0adcp-3, +0x1.7b8b28p-28, -0x1.35bce4p-52, -0x1.abc8f8p-79 }, /* 201: log(1304/1024.) */
	{ +0x1.e598ecp-3, +0x1.5a87e4p-27, -0x1.134bd0p-51, +0x1.c2cebep-76 }, /* 202: log(1298/1024.) */
	{ +0x1.da85d8p-3, -0x1.df31b0p-27, +0x1.94c16cp-57, +0x1.8fd7eap-82 }, /* 203: log(1291/1024.) */
	{ +0x1.d0fb80p-3, -0x1.bb5434p-28, -0x1.ea5640p-52, -0x1.8ceca4p-77 }, /* 204: log(1285/1024.) */
	{ +0x1.c765b8p-3, +0x1.e4d68cp-27, +0x1.5b59b4p-51, +0x1.76f6c4p-76 }, /* 205: log(1279/1024.) */
	{ +0x1.bdc46cp-3, -0x1.1cbb50p-27, +0x1.2da010p-51, +0x1.eb282cp-75 }, /* 206: log(1273/1024.) */
	{ +0x1.b27980p-3, -0x1.1b9ce0p-27, +0x1.7756f8p-52, +0x1.2ff572p-76 }, /* 207: log(1266/1024.) */
	{ +0x1.a8bed0p-3, -0x1.bbe874p-30, +0x1.85cf20p-56, +0x1.b9cf18p-80 }, /* 208: log(1260/1024.) */
	{ +0x1.9ef83cp-3, +0x1.2769a4p-27, -0x1.85bda0p-52, +0x1.8c8018p-79 }, /* 209: log(1254/1024.) */
	{ +0x1.9525a8p-3, +0x1.cf456cp-27, -0x1.7137d8p-52, -0x1.f158e8p-76 }, /* 210: log(1248/1024.) */
	{ +0x1.8b46f8p-3, +0x1.11b12cp-30, +0x1.9f2104p-54, -0x1.22836ep-78 }, /* 211: log(1242/1024.) */
	{ +0x1.83040cp-3, +0x1.2379e4p-28, +0x1.b71c70p-52, -0x1.990cdep-76 }, /* 212: log(1237/1024.) */
	{ +0x1.790ed4p-3, +0x1.dc4c68p-28, -0x1.910ac8p-52, +0x1.dd1bd6p-76 }, /* 213: log(1231/1024.) */
	{ +0x1.6f0d28p-3, +0x1.5cad68p-28, +0x1.737c94p-52, -0x1.9184bap-77 }, /* 214: log(1225/1024.) */
	{ +0x1.64fee8p-3, +0x1.04bf88p-28, +0x1.6fca28p-52, +0x1.8884a8p-76 }, /* 215: log(1219/1024.) */
	{ +0x1.5c9400p-3, +0x1.d65cb0p-29, -0x1.b2919cp-53, +0x1.b99bcep-77 }, /* 216: log(1214/1024.) */
	{ +0x1.526e60p-3, -0x1.c5e4bcp-27, -0x1.0ba380p-52, +0x1.d6e3ccp-79 }, /* 217: log(1208/1024.) */
	{ +0x1.483bccp-3, +0x1.9cdc7cp-28, -0x1.5ad8dcp-54, -0x1.392d3cp-83 }, /* 218: log(1202/1024.) */
	{ +0x1.3fb25cp-3, -0x1.a6ad74p-27, +0x1.5be6b4p-52, -0x1.4e0114p-77 }, /* 219: log(1197/1024.) */
	{ +0x1.371fc4p-3, -0x1.fe1708p-27, -0x1.78864cp-52, -0x1.27543ap-76 }, /* 220: log(1192/1024.) */
	{ +0x1.2cca10p-3, -0x1.4141b4p-28, -0x1.ef191cp-52, +0x1.00ee08p-76 }, /* 221: log(1186/1024.) */
	{ +0x1.242310p-3, +0x1.3ba510p-27, -0x1.d003c8p-51, +0x1.162640p-76 }, /* 222: log(1181/1024.) */
	{ +0x1.1b72acp-3, +0x1.52f67cp-27, -0x1.fd6fa0p-51, +0x1.1a3966p-77 }, /* 223: log(1176/1024.) */
	{ +0x1.10f8e4p-3, +0x1.129cd8p-30, +0x1.31ef30p-55, +0x1.a73e38p-79 }, /* 224: log(1170/1024.) */
	{ +0x1.08338cp-3, -0x1.005d7cp-27, -0x1.661a9cp-51, +0x1.1f138ap-79 }, /* 225: log(1165/1024.) */
	{ +0x1.fec914p-4, -0x1.c482a8p-29, -0x1.55746cp-54, +0x1.99f932p-80 }, /* 226: log(1160/1024.) */
	{ +0x1.ed1794p-4, +0x1.d06f00p-29, +0x1.75e45cp-53, -0x1.d0483ep-78 }, /* 227: log(1155/1024.) */
	{ +0x1.db5270p-4, +0x1.87d928p-32, -0x1.0f52a4p-57, +0x1.81f4a6p-84 }, /* 228: log(1150/1024.) */
	{ +0x1.c97978p-4, +0x1.af1d24p-29, -0x1.0977d0p-60, -0x1.8839d0p-84 }, /* 229: log(1145/1024.) */
	{ +0x1.b78c84p-4, -0x1.44f124p-28, -0x1.ef7bc4p-52, +0x1.9e0650p-78 }, /* 230: log(1140/1024.) */
	{ +0x1.a58b60p-4, +0x1.856464p-29, +0x1.c651d0p-55, +0x1.b06b0cp-79 }, /* 231: log(1135/1024.) */
	{ +0x1.9375e4p-4, +0x1.5595ecp-28, +0x1.dc3738p-52, +0x1.86c89ap-81 }, /* 232: log(1130/1024.) */
	{ +0x1.814be4p-4, -0x1.c073fcp-28, -0x1.371f88p-53, -0x1.5f4080p-77 }, /* 233: log(1125/1024.) */
	{ +0x1.6f0d28p-4, +0x1.5cad68p-29, +0x1.737c94p-53, -0x1.9184bap-78 }, /* 234: log(1120/1024.) */
	{ +0x1.60658cp-4, -0x1.6c8af4p-28, +0x1.d8ef74p-55, +0x1.c4f792p-80 }, /* 235: log(1116/1024.) */
	{ +0x1.4e0110p-4, +0x1.146b5cp-29, +0x1.73f7ccp-54, -0x1.d28db8p-79 }, /* 236: log(1111/1024.) */
	{ +0x1.3b8758p-4, +0x1.8b1b70p-28, -0x1.20aca4p-52, -0x1.651894p-76 }, /* 237: log(1106/1024.) */
	{ +0x1.28f834p-4, +0x1.43b6a4p-30, -0x1.452af8p-55, +0x1.976892p-80 }, /* 238: log(1101/1024.) */
	{ +0x1.1a0fbcp-4, -0x1.e4075cp-28, +0x1.1fe618p-52, +0x1.9d6dc2p-77 }, /* 239: log(1097/1024.) */
	{ +0x1.075984p-4, -0x1.4ce370p-29, -0x1.d9fc98p-53, +0x1.4ccf12p-77 }, /* 240: log(1092/1024.) */
	{ +0x1.f0a30cp-5, +0x1.162a68p-37, -0x1.e83368p-61, -0x1.d222a6p-86 }, /* 241: log(1088/1024.) */
	{ +0x1.cae730p-5, -0x1.1a8f7cp-31, -0x1.5f9014p-55, +0x1.2720c0p-79 }, /* 242: log(1083/1024.) */
	{ +0x1.ac9724p-5, -0x1.e8ee08p-29, +0x1.a7de04p-54, -0x1.9bba74p-78 }, /* 243: log(1079/1024.) */
	{ +0x1.868a84p-5, -0x1.ef8128p-30, +0x1.dc5eccp-54, -0x1.58d250p-79 }, /* 244: log(1074/1024.) */
	{ +0x1.67f950p-5, -0x1.ed684cp-30, -0x1.f060c0p-55, -0x1.b1294cp-80 }, /* 245: log(1070/1024.) */
	{ +0x1.494accp-5, +0x1.a6c890p-32, -0x1.c3ad48p-56, -0x1.6dc66cp-84 }, /* 246: log(1066/1024.) */
	{ +0x1.22c71cp-5, -0x1.8abe2cp-32, -0x1.7e7078p-56, -0x1.ddc3dcp-86 }, /* 247: log(1061/1024.) */
	{ +0x1.03d5d8p-5, +0x1.79cfbcp-31, -0x1.da7c4cp-58, +0x1.4e7582p-83 }, /* 248: log(1057/1024.) */
	{ +0x1.c98d18p-6, +0x1.a01904p-31, -0x1.854164p-55, +0x1.883c36p-79 }, /* 249: log(1053/1024.) */
	{ +0x1.8b31fcp-6, -0x1.356500p-30, +0x1.c3ab48p-55, +0x1.b69bdap-80 }, /* 250: log(1049/1024.) */
	{ +0x1.3cea44p-6, +0x1.a352bcp-33, -0x1.8865acp-57, -0x1.48159cp-81 }, /* 251: log(1044/1024.) */
	{ +0x1.fc0a8cp-7, -0x1.e07f84p-32, +0x1.e7cf6cp-58, +0x1.3a69c0p-82 }, /* 252: log(1040/1024.) */
	{ +0x1.7dc474p-7, +0x1.f810a8p-31, -0x1.245b5cp-56, -0x1.a1f4f8p-80 }, /* 253: log(1036/1024.) */
	{ +0x1.fe02a8p-8, -0x1.4ef988p-32, +0x1.1f86ecp-57, +0x1.20723cp-81 }, /* 254: log(1032/1024.) */
	{ +0x1.ff00acp-9, -0x1.d4ef44p-33, +0x1.2821acp-63, +0x1.5a6d32p-87 }, /* 255: log(1028/1024.) */
	{ 0, 0, 0, 0 }
};

#define PAIR_ADD(d, H, L) do {				\
  gnm_float d_ = (d);					\
  gnm_float dh_ = gnm_round (d_ * 65536) / 65536;	\
  gnm_float dl_ = d_ - dh_;				\
  if (0) g_printerr ("Adding %.50" GNM_FORMAT_g "\n", d_);	\
  L += dl_;						\
  H += dh_;						\
} while (0)

#define ADD1(d) PAIR_ADD(d,*yh,*yl)

/*
 * Compute x * gnm_log (x / M) + (M - x)
 * aka -x * log1pmx ((M - x) / x)
 *
 * Deliver the result back in two parts, *yh and *yl.
 */

static void
ebd0(gnm_float x, gnm_float M, gnm_float *yh, gnm_float *yl)
{
	gnm_float r, f, fg, M1;
	int e;
	int i, j;
	const int Sb = 10;
	const gnm_float S = 1u << Sb;
	const int N = G_N_ELEMENTS(bd0_scale) - 1;
	const gnm_float e4 = GNM_EPSILON * GNM_EPSILON * GNM_EPSILON * GNM_EPSILON;

	if (gnm_isnan (x) || gnm_isnan (M)) {
		*yl = *yh = x;
		return;
	}

	*yl = *yh = 0;

	if (x == M) return;
	if (x < M * e4) { ADD1(M); return; }
	if (M == 0) { *yh = gnm_pinf; return; }

	if (M < x * e4) {
		ADD1(x * (gnm_log(x) - 1));
		ADD1(-x * gnm_log(M));
		return;
	}

#ifdef DEBUG_EBD0
	g_printerr ("x=%.20g  M=%.20g\n", x, M);
#endif

	r = gnm_frexp (M / x, &e);
	i = gnm_round ((r - GNM_const(0.5)) * (2 * N));
	g_assert (i >= 0 && i <= N);
	f = gnm_round (S / (GNM_const(0.5) + i / (GNM_const(2.0) * N)));
	fg = gnm_ldexp (f, -(e + Sb));

	/* We now have (M * fg / x) close to 1.  */

	/*
	 * We need to compute this:
	 * (x/M)^x * exp(M-x) =
	 * (M/x)^-x * exp(M-x) =
	 * (M*fg/x)^-x * (fg)^x * exp(M-x) =
	 * (M*fg/x)^-x * (fg)^x * exp(M*fg-x) * exp(M-M*fg)
	 *
	 * In log terms:
	 * log((x/M)^x * exp(M-x)) =
	 * log((M*fg/x)^-x * (fg)^x * exp(M*fg-x) * exp(M-M*fg)) =
	 * log((M*fg/x)^-x * exp(M*fg-x)) + x*log(fg) + (M-M*fg) =
	 * -x*log1pmx((M*fg-x)/x) + x*log(fg) + M - M*fg =
	 *
	 * Note, that fg has at most 10 bits.  If M and x are suitably
	 * "nice" -- such as being integers or half-integers -- then
	 * we can compute M*fg as well as x * bd0_scale[.][.] without
	 * rounding errors.
	 */

	for (j = G_N_ELEMENTS(bd0_scale[i]) - 1; j >= 0; j--) {
		ADD1(x * (gnm_float)(bd0_scale[i][j]));      /* Handles x*log(fg*2^e) */
		ADD1(-x * e * (gnm_float)(bd0_scale[0][j])); /* Handles x*log(1/2^e) */
	}

	ADD1(M);
	M1 = gnm_round (M);
	ADD1(-M1 * fg);
	ADD1(-(M-M1) * fg);

	ADD1(-x * log1pmx ((M * fg - x) / x));

#ifdef DEBUG_EBD0
	g_printerr ("res=%.20g + %.20g\n", *yh, *yl);
#endif
}

#undef ADD1

/* Legacy function.  */
gnm_float
bd0(gnm_float x, gnm_float M)
{
	gnm_float yh, yl;
	ebd0 (x, M, &yh, &yl);
	return yh + yl;
}

/* ------------------------------------------------------------------------- */
/* --- BEGIN MAGIC R SOURCE MARKER --- */

// The following source code was imported from the R project.
// It was automatically transformed by tools/import-R.

/* Imported src/nmath/dpq.h from R.  */
/*
 *  R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 2000--2015 The  R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 */
	/* Utilities for `dpq' handling (density/probability/quantile) */

/* give_log in "d";  log_p in "p" & "q" : */
#define give_log log_p
							/* "DEFAULT" */
							/* --------- */
#define R_D__0	(log_p ? gnm_ninf : GNM_const(0.))		/* 0 */
#define R_D__1	(log_p ? GNM_const(0.) : GNM_const(1.))			/* 1 */
#define R_DT_0	(lower_tail ? R_D__0 : R_D__1)		/* 0 */
#define R_DT_1	(lower_tail ? R_D__1 : R_D__0)		/* 1 */
#define R_D_half (log_p ? -M_LN2gnum : GNM_const(0.5))		// 1/2 (lower- or upper tail)


/* Use 0.5 - p + 0.5 to perhaps gain 1 bit of accuracy */
#define R_D_Lval(p)	(lower_tail ? (p) : (GNM_const(0.5) - (p) + GNM_const(0.5)))	/*  p  */
#define R_D_Cval(p)	(lower_tail ? (GNM_const(0.5) - (p) + GNM_const(0.5)) : (p))	/*  1 - p */

#define R_D_val(x)	(log_p	? gnm_log(x) : (x))		/*  x  in pF(x,..) */
#define R_D_qIv(p)	(log_p	? gnm_exp(p) : (p))		/*  p  in qF(p,..) */
#define R_D_exp(x)	(log_p	?  (x)	 : gnm_exp(x))	/* exp(x) */
#define R_D_log(p)	(log_p	?  (p)	 : gnm_log(p))	/* log(p) */
#define R_D_Clog(p)	(log_p	? gnm_log1p(-(p)) : (GNM_const(0.5) - (p) + GNM_const(0.5))) /* [log](1-p) */

// gnm_log(1 - gnm_exp(x))  in more stable form than gnm_log1p(- R_D_qIv(x)) :
// #define swap_log_tail(x)   ((x) > -M_LN2gnum ? gnm_log(-gnm_expm1(x)) : gnm_log1p(-gnm_exp(x)))

/* log(1-exp(x)):  R_D_LExp(x) == (log1p(- R_D_qIv(x))) but even more stable:*/
#define R_D_LExp(x)     (log_p ? swap_log_tail(x) : gnm_log1p(-x))

#define R_DT_val(x)	(lower_tail ? R_D_val(x)  : R_D_Clog(x))
#define R_DT_Cval(x)	(lower_tail ? R_D_Clog(x) : R_D_val(x))

/*#define R_DT_qIv(p)	R_D_Lval(R_D_qIv(p))		 *  p  in qF ! */
#define R_DT_qIv(p)	(log_p ? (lower_tail ? gnm_exp(p) : - gnm_expm1(p)) \
			       : R_D_Lval(p))

/*#define R_DT_CIv(p)	R_D_Cval(R_D_qIv(p))		 *  1 - p in qF */
#define R_DT_CIv(p)	(log_p ? (lower_tail ? -gnm_expm1(p) : gnm_exp(p)) \
			       : R_D_Cval(p))

#define R_DT_exp(x)	R_D_exp(R_D_Lval(x))		/* exp(x) */
#define R_DT_Cexp(x)	R_D_exp(R_D_Cval(x))		/* exp(1 - x) */

#define R_DT_log(p)	(lower_tail? R_D_log(p) : R_D_LExp(p))/* log(p) in qF */
#define R_DT_Clog(p)	(lower_tail? R_D_LExp(p): R_D_log(p))/* log(1-p) in qF*/
#define R_DT_Log(p)	(lower_tail? (p) : swap_log_tail(p))
// ==   R_DT_log when we already "know" log_p == TRUE


#define R_Q_P01_check(p)			\
    if ((log_p	&& p > 0) ||			\
	(!log_p && (p < 0 || p > 1)) )		\
	ML_WARN_return_NAN

/* Do the boundaries exactly for q*() functions :
 * Often  _LEFT_ = ML_NEGINF , and very often _RIGHT_ = ML_POSINF;
 *
 * R_Q_P01_boundaries(p, _LEFT_, _RIGHT_)  :<==>
 *
 *     R_Q_P01_check(p);
 *     if (p == R_DT_0) return _LEFT_ ;
 *     if (p == R_DT_1) return _RIGHT_;
 *
 * the following implementation should be more efficient (less tests):
 */
#define R_Q_P01_boundaries(p, _LEFT_, _RIGHT_)		\
    if (log_p) {					\
	if(p > 0)					\
	    ML_WARN_return_NAN;				\
	if(p == 0) /* upper bound*/			\
	    return lower_tail ? _RIGHT_ : _LEFT_;	\
	if(p == gnm_ninf)				\
	    return lower_tail ? _LEFT_ : _RIGHT_;	\
    }							\
    else { /* !log_p */					\
	if(p < 0 || p > 1)				\
	    ML_WARN_return_NAN;				\
	if(p == 0)					\
	    return lower_tail ? _LEFT_ : _RIGHT_;	\
	if(p == 1)					\
	    return lower_tail ? _RIGHT_ : _LEFT_;	\
    }

#define R_P_bounds_01(x, x_min, x_max)	\
    if(x <= x_min) return R_DT_0;		\
    if(x >= x_max) return R_DT_1
/* is typically not quite optimal for (-Inf,Inf) where
 * you'd rather have */
#define R_P_bounds_Inf_01(x)			\
    if(!gnm_finite(x)) {				\
	if (x > 0) return R_DT_1;		\
	/* x < 0 */return R_DT_0;		\
    }



/* additions for density functions (C.Loader) */
#define R_D_fexp(f,x)     (give_log ? GNM_const(-0.5)*gnm_log(f)+(x) : gnm_exp(x)/gnm_sqrt(f))

/* [neg]ative or [non int]eger : */
#define R_D_negInonint(x) (x < 0 || R_nonint(x))

// for discrete d<distr>(x, ...) :
#define R_D_nonint_check(x)				\
   if(R_nonint(x)) {					\
       MATHLIB_WARNING(("non-integer x = %" GNM_FORMAT_f ""), x);	\
	return R_D__0;					\
   }

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pnorm.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998	    Ross Ihaka
 *  Copyright (C) 2000-2013 The R Core Team
 *  Copyright (C) 2003	    The R Foundation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  SYNOPSIS
 *
 *   #include <Rmath.h>
 *
 *   double pnorm5(double x, double mu, double sigma, int lower_tail,int log_p);
 *	   {pnorm (..) is synonymous and preferred inside R}
 *
 *   void   pnorm_both(double x, double *cum, double *ccum,
 *		       int i_tail, int log_p);
 *
 *  DESCRIPTION
 *
 *	The main computation evaluates near-minimax approximations derived
 *	from those in "Rational Chebyshev approximations for the error
 *	function" by W. J. Cody, Math. Comp., 1969, 631-637.  This
 *	transportable program uses rational functions that theoretically
 *	approximate the normal distribution function to at least 18
 *	significant decimal digits.  The accuracy achieved depends on the
 *	arithmetic system, the compiler, the intrinsic functions, and
 *	proper selection of the machine-dependent constants.
 *
 *  REFERENCE
 *
 *	Cody, W. D. (1993).
 *	ALGORITHM 715: SPECFUN - A Portable FORTRAN Package of
 *	Special Function Routines and Test Drivers".
 *	ACM Transactions on Mathematical Software. 19, 22-32.
 *
 *  EXTENSIONS
 *
 *  The "_both" , lower, upper, and log_p  variants were added by
 *  Martin Maechler, Jan.2000;
 *  as well as log1p() and similar improvements later on.
 *
 *  James M. Rath contributed bug report PR#699 and patches correcting SIXTEN
 *  and if() clauses {with a bug: "|| instead of &&" -> PR #2883) more in line
 *  with the original Cody code.
 */

gnm_float pnorm(gnm_float x, gnm_float mu, gnm_float sigma, gboolean lower_tail, gboolean log_p)
{
    gnm_float p, cp = gnm_nan;

    /* Note: The structure of these checks has been carefully thought through.
     * For example, if x == mu and sigma == 0, we get the correct answer 1.
     */
#ifdef IEEE_754
    if(gnm_isnan(x) || gnm_isnan(mu) || gnm_isnan(sigma))
	return x + mu + sigma;
#endif
    if(!gnm_finite(x) && mu == x) return gnm_nan;/* x-mu is NaN */
    if (sigma <= 0) {
	if(sigma < 0) ML_WARN_return_NAN;
	/* sigma = 0 : */
	return (x < mu) ? R_DT_0 : R_DT_1;
    }
    p = (x - mu) / sigma;
    if(!gnm_finite(p))
	return (x < mu) ? R_DT_0 : R_DT_1;
    x = p;

    pnorm_both(x, &p, &cp, (lower_tail ? 0 : 1), log_p);

    return(lower_tail ? p : cp);
}

#define SIXTEN	16 /* Cutoff allowing exact "*" and "/" */

void pnorm_both(gnm_float x, gnm_float *cum, gnm_float *ccum, int i_tail, gboolean log_p)
{
/* i_tail in {0,1,2} means: "lower", "upper", or "both" :
   if(lower) return  *cum := P[X <= x]
   if(upper) return *ccum := P[X >  x] = 1 - P[X <= x]
*/
    static const gnm_float a[5] = {
	GNM_const(2.2352520354606839287),
	GNM_const(161.02823106855587881),
	GNM_const(1067.6894854603709582),
	GNM_const(18154.981253343561249),
	GNM_const(0.065682337918207449113)
    };
    static const gnm_float b[4] = {
	GNM_const(47.20258190468824187),
	GNM_const(976.09855173777669322),
	GNM_const(10260.932208618978205),
	GNM_const(45507.789335026729956)
    };
    static const gnm_float c[9] = {
	GNM_const(0.39894151208813466764),
	GNM_const(8.8831497943883759412),
	GNM_const(93.506656132177855979),
	GNM_const(597.27027639480026226),
	GNM_const(2494.5375852903726711),
	GNM_const(6848.1904505362823326),
	GNM_const(11602.651437647350124),
	GNM_const(9842.7148383839780218),
	GNM_const(1.0765576773720192317e-8)
    };
    static const gnm_float d[8] = {
	GNM_const(22.266688044328115691),
	GNM_const(235.38790178262499861),
	GNM_const(1519.377599407554805),
	GNM_const(6485.558298266760755),
	GNM_const(18615.571640885098091),
	GNM_const(34900.952721145977266),
	GNM_const(38912.003286093271411),
	GNM_const(19685.429676859990727)
    };
    static const gnm_float p[6] = {
	GNM_const(0.21589853405795699),
	GNM_const(0.1274011611602473639),
	GNM_const(0.022235277870649807),
	GNM_const(0.001421619193227893466),
	GNM_const(2.9112874951168792e-5),
	GNM_const(0.02307344176494017303)
    };
    static const gnm_float q[5] = {
	GNM_const(1.28426009614491121),
	GNM_const(0.468238212480865118),
	GNM_const(0.0659881378689285515),
	GNM_const(0.00378239633202758244),
	GNM_const(7.29751555083966205e-5)
    };

    gnm_float xden, xnum, temp, del, eps, xsq, y;
#ifdef NO_DENORMS
    gnm_float min = GNM_MIN;
#endif
    int i, lower, upper;

#ifdef IEEE_754
    if(gnm_isnan(x)) { *cum = *ccum = x; return; }
#endif

    /* Consider changing these : */
    eps = GNM_EPSILON * GNM_const(0.5);

    /* i_tail in {0,1,2} =^= {lower, upper, both} */
    lower = i_tail != 1;
    upper = i_tail != 0;

    y = gnm_abs(x);
    if (y <= GNM_const(0.67448975)) { /* qnorm(3/4) = .6744.... -- earlier had 0.66291 */
	if (y > eps) {
	    xsq = x * x;
	    xnum = a[4] * xsq;
	    xden = xsq;
	    for (i = 0; i < 3; ++i) {
		xnum = (xnum + a[i]) * xsq;
		xden = (xden + b[i]) * xsq;
	    }
	} else xnum = xden = 0;

	temp = x * (xnum + a[3]) / (xden + b[3]);
	if(lower)  *cum = GNM_const(0.5) + temp;
	if(upper) *ccum = GNM_const(0.5) - temp;
	if(log_p) {
	    if(lower)  *cum = gnm_log(*cum);
	    if(upper) *ccum = gnm_log(*ccum);
	}
    }
    else if (y <= M_SQRT_32) {

	/* Evaluate pnorm for 0.674.. = qnorm(3/4) < |x| <= sqrt(32) ~= 5.657 */

	xnum = c[8] * y;
	xden = y;
	for (i = 0; i < 7; ++i) {
	    xnum = (xnum + c[i]) * y;
	    xden = (xden + d[i]) * y;
	}
	temp = (xnum + c[7]) / (xden + d[7]);

#define do_del(X)							\
	xsq = gnm_trunc(X * SIXTEN) / SIXTEN;				\
	del = (X - xsq) * (X + xsq);					\
	if(log_p) {							\
	    *cum = (-xsq * xsq * GNM_const(0.5)) + (-del * GNM_const(0.5)) + gnm_log(temp);	\
	    if((lower && x > 0) || (upper && x <= 0))			\
		  *ccum = gnm_log1p(-gnm_exp(-xsq * xsq * GNM_const(0.5)) *		\
				gnm_exp(-del * GNM_const(0.5)) * temp);		\
	}								\
	else {								\
	    *cum = gnm_exp(-xsq * xsq * GNM_const(0.5)) * gnm_exp(-del * GNM_const(0.5)) * temp;	\
	    *ccum = GNM_const(1.0) - *cum;						\
	}

#define swap_tail						\
	if (x > 0) {/* swap  ccum <--> cum */			\
	    temp = *cum; if(lower) *cum = *ccum; *ccum = temp;	\
	}

	do_del(y);
	swap_tail;
    }

/* else	  |x| > sqrt(32) = 5.657 :
 * the next two case differentiations were really for lower=T, log=F
 * Particularly	 *not*	for  log_p !

 * Cody had (-37.5193 < x  &&  x < 8.2924) ; R originally had y < 50
 *
 * Note that we do want symmetry(0), lower/upper -> hence use y
 */
    else if((log_p && y < GNM_const(1e170)) /* avoid underflow below */
	/*  ^^^^^ MM FIXME: can speedup for log_p and much larger |x| !
	 * Then, make use of  Abramowitz & Stegun, 26.2.13, something like

	 xsq = x*x;

	 if(xsq * DBL_EPSILON < 1.)
	    del = (1. - (1. - 5./(xsq+6.)) / (xsq+4.)) / (xsq+2.);
	 else
	    del = 0.;
	 *cum = -.5*xsq - M_LN_SQRT_2PI - log(x) + log1p(-del);
	 *ccum = log1p(-exp(*cum)); /.* ~ log(1) = 0 *./

	 swap_tail;

	 [Yes, but xsq might be infinite.]

	*/
	    || (lower && GNM_const(-37.5193) < x  &&  x < GNM_const(8.2924))
	    || (upper && GNM_const(-8.2924)  < x  &&  x < GNM_const(37.5193))
	) {

	/* Evaluate pnorm for x in (-37.5, -5.657) union (5.657, 37.5) */
	xsq = GNM_const(1.0) / (x * x); /* (1./x)*(1./x) might be better */
	xnum = p[5] * xsq;
	xden = xsq;
	for (i = 0; i < 4; ++i) {
	    xnum = (xnum + p[i]) * xsq;
	    xden = (xden + q[i]) * xsq;
	}
	temp = xsq * (xnum + p[4]) / (xden + q[4]);
	temp = (M_1_SQRT_2PI - temp) / y;

	do_del(x);
	swap_tail;
    } else { /* large x such that probs are 0 or 1 */
	if(x > 0) {	*cum = R_D__1; *ccum = R_D__0;	}
	else {	        *cum = R_D__0; *ccum = R_D__1;	}
    }


#ifdef NO_DENORMS
    /* do not return "denormalized" -- we do in R */
    if(log_p) {
	if(*cum > -min)	 *cum = GNM_const(-0.);
	if(*ccum > -min)*ccum = GNM_const(-0.);
    }
    else {
	if(*cum < min)	 *cum = 0;
	if(*ccum < min)	*ccum = 0;
    }
#endif
    return;
}
/* Cleaning up done by tools/import-R:  */
#undef SIXTEN
#undef do_del
#undef swap_tail

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qnorm.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998       Ross Ihaka
 *  Copyright (C) 2000--2005 The R Core Team
 *  based on AS 111 (C) 1977 Royal Statistical Society
 *  and   on AS 241 (C) 1988 Royal Statistical Society
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  SYNOPSIS
 *
 *	double qnorm5(double p, double mu, double sigma,
 *		      int lower_tail, int log_p)
 *            {qnorm (..) is synonymous and preferred inside R}
 *
 *  DESCRIPTION
 *
 *	Compute the quantile function for the normal distribution.
 *
 *	For small to moderate probabilities, algorithm referenced
 *	below is used to obtain an initial approximation which is
 *	polished with a final Newton step.
 *
 *	For very large arguments, an algorithm of Wichura is used.
 *
 *  REFERENCE
 *
 *	Beasley, J. D. and S. G. Springer (1977).
 *	Algorithm AS 111: The percentage points of the normal distribution,
 *	Applied Statistics, 26, 118-121.
 *
 *      Wichura, M.J. (1988).
 *      Algorithm AS 241: The Percentage Points of the Normal Distribution.
 *      Applied Statistics, 37, 477-484.
 */


gnm_float qnorm(gnm_float p, gnm_float mu, gnm_float sigma, gboolean lower_tail, gboolean log_p)
{
    gnm_float p_, q, r, val;

#ifdef IEEE_754
    if (gnm_isnan(p) || gnm_isnan(mu) || gnm_isnan(sigma))
	return p + mu + sigma;
#endif
    R_Q_P01_boundaries(p, gnm_ninf, gnm_pinf);

    if(sigma  < 0)	ML_WARN_return_NAN;
    if(sigma == 0)	return mu;

    p_ = R_DT_qIv(p);/* real lower_tail prob. p */
    q = p_ - GNM_const(0.5);

#ifdef DEBUG_qnorm
    REprintf("qnorm(p=%1" GNM_FORMAT_G "NM_const(0.7)g, m=%" GNM_FORMAT_g ", s=%" GNM_FORMAT_g ", l.t.= %d, log= %d): q = %" GNM_FORMAT_g "\n",
	     p,mu,sigma, lower_tail, log_p, q);
#endif


/*-- use AS 241 --- */
/* double ppnd16_(double *p, long *ifault)*/
/*      ALGORITHM AS241  APPL. STATIST. (1988) VOL. 37, NO. 3

        Produces the normal deviate Z corresponding to a given lower
        tail area of P; Z is accurate to about 1 part in 10**16.

        (original fortran code used PARAMETER(..) for the coefficients
         and provided hash codes for checking them...)
*/
    if (gnm_abs(q) <= GNM_const(.425)) {/* 0.075 <= p <= 0.925 */
        r = GNM_const(.180625) - q * q;
	val =
            q * (((((((r * GNM_const(2509.0809287301226727) +
                       GNM_const(33430.575583588128105)) * r + GNM_const(67265.770927008700853)) * r +
                     GNM_const(45921.953931549871457)) * r + GNM_const(13731.693765509461125)) * r +
                   GNM_const(1971.5909503065514427)) * r + GNM_const(133.14166789178437745)) * r +
                 GNM_const(3.387132872796366608))
            / (((((((r * GNM_const(5226.495278852854561) +
                     GNM_const(28729.085735721942674)) * r + GNM_const(39307.89580009271061)) * r +
                   GNM_const(21213.794301586595867)) * r + GNM_const(5394.1960214247511077)) * r +
                 GNM_const(687.1870074920579083)) * r + GNM_const(42.313330701600911252)) * r + GNM_const(1.));
    }
    else { /* closer than 0.075 from {0,1} boundary */

	/* r = min(p, 1-p) < 0.075 */
	if (q > 0)
	    r = R_DT_CIv(p);/* 1-p */
	else
	    r = p_;/* = R_DT_Iv(p) ^=  p */

	r = gnm_sqrt(- ((log_p &&
		     ((lower_tail && q <= 0) || (!lower_tail && q > 0))) ?
		    p : /* else */ gnm_log(r)));
        /* r = sqrt(-log(r))  <==>  min(p, 1-p) = exp( - r^2 ) */
#ifdef DEBUG_qnorm
	REprintf("\t close to 0 or 1: r = %7" GNM_FORMAT_g "\n", r);
#endif

        if (r <= 5) { /* <==> min(p,1-p) >= exp(-25) ~= 1.3888e-11 */
            r += GNM_const(-1.6);
            val = (((((((r * GNM_const(7.7454501427834140764e-4) +
                       GNM_const(.0227238449892691845833)) * r + GNM_const(.24178072517745061177)) *
                     r + GNM_const(1.27045825245236838258)) * r +
                    GNM_const(3.64784832476320460504)) * r + GNM_const(5.7694972214606914055)) *
                  r + GNM_const(4.6303378461565452959)) * r +
                 GNM_const(1.42343711074968357734))
                / (((((((r *
                         GNM_const(1.05075007164441684324e-9) + GNM_const(5.475938084995344946e-4)) *
                        r + GNM_const(.0151986665636164571966)) * r +
                       GNM_const(.14810397642748007459)) * r + GNM_const(.68976733498510000455)) *
                     r + GNM_const(1.6763848301838038494)) * r +
                    GNM_const(2.05319162663775882187)) * r + GNM_const(1.));
        }
        else { /* very close to  0 or 1 */
            r += GNM_const(-5.);
            val = (((((((r * GNM_const(2.01033439929228813265e-7) +
                       GNM_const(2.71155556874348757815e-5)) * r +
                      GNM_const(.0012426609473880784386)) * r + GNM_const(.026532189526576123093)) *
                    r + GNM_const(.29656057182850489123)) * r +
                   GNM_const(1.7848265399172913358)) * r + GNM_const(5.4637849111641143699)) *
                 r + GNM_const(6.6579046435011037772))
                / (((((((r *
                         GNM_const(2.04426310338993978564e-15) + GNM_const(1.4215117583164458887e-7))*
                        r + GNM_const(1.8463183175100546818e-5)) * r +
                       GNM_const(7.868691311456132591e-4)) * r + GNM_const(.0148753612908506148525))
                     * r + GNM_const(.13692988092273580531)) * r +
                    GNM_const(.59983220655588793769)) * r + GNM_const(1.));
        }

	if(q < 0)
	    val = -val;
        /* return (q >= 0.)? r : -r ;*/
    }
    return mu + sigma * val;
}




/* ------------------------------------------------------------------------ */
/* Imported src/nmath/ppois.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *    The distribution function of the Poisson distribution.
 */


gnm_float ppois(gnm_float x, gnm_float lambda, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(lambda))
	return x + lambda;
#endif
    if(lambda < 0) ML_WARN_return_NAN;
    if (x < 0)		return R_DT_0;
    if (lambda == 0)	return R_DT_1;
    if (!gnm_finite(x))	return R_DT_1;
    x = gnm_fake_floor(x);

    return pgamma(lambda, x + 1, GNM_const(1.), !lower_tail, log_p);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dpois.c from R.  */
/*
 *  AUTHOR
 *    Catherine Loader, catherine@research.bell-labs.com.
 *    October 23, 2000.
 *
 *  Merge in to R:
 *	Copyright (C) 2000-2016 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *
 * DESCRIPTION
 *
 *    dpois() checks argument validity and calls dpois_raw().
 *
 *    dpois_raw() computes the Poisson probability  lb^x exp(-lb) / x!.
 *      This does not check that x is an integer, since dgamma() may
 *      call this with a fractional x argument. Any necessary argument
 *      checks should be done in the calling function.
 *
 */


// called also from dgamma.c, pgamma.c, dnbeta.c, dnbinom.c, dnchisq.c :
/* Definition of function dpois_raw removed.  */

gnm_float dpois(gnm_float x, gnm_float lambda, gboolean give_log)
{
#ifdef IEEE_754
    if(gnm_isnan(x) || gnm_isnan(lambda))
        return x + lambda;
#endif

    if (lambda < 0) ML_WARN_return_NAN;
    R_D_nonint_check(x);
    if (x < 0 || !gnm_finite(x))
	return R_D__0;

    x = gnm_round(x);

    return( dpois_raw(x,lambda,give_log) );
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dgamma.c from R.  */
/*
 *  AUTHOR
 *    Catherine Loader, catherine@research.bell-labs.com.
 *    October 23, 2000.
 *
 *  Merge in to R:
 *	Copyright (C) 2000-2019 The R Core Team
 *	Copyright (C) 2004-2019 The R Foundation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *
 * DESCRIPTION
 *
 *   Computes the density of the gamma distribution,
 *
 *                   1/s (x/s)^{a-1} exp(-x/s)
 *        p(x;a,s) = -----------------------
 *                            (a-1)!
 *
 *   where 's' is the scale (= 1/lambda in other parametrizations)
 *     and 'a' is the shape parameter ( = alpha in other contexts).
 *
 * The old (R 1.1.1) version of the code is available via '#define D_non_pois'
 */


gnm_float dgamma(gnm_float x, gnm_float shape, gnm_float scale, gboolean give_log)
{
    gnm_float pr;
#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(shape) || gnm_isnan(scale))
        return x + shape + scale;
#endif
    if (shape < 0 || scale <= 0) ML_WARN_return_NAN;
    if (x < 0)
	return R_D__0;
    if (shape == 0) /* point mass at 0 */
	return (x == 0)? gnm_pinf : R_D__0;
    if (x == 0) {
	if (shape < 1) return gnm_pinf;
	if (shape > 1) return R_D__0;
	/* else */
	return give_log ? -gnm_log(scale) : 1 / scale;
    }

    if (shape < 1) {
	pr = dpois_raw(shape, x/scale, give_log);
	return (
	    give_log/* NB: currently *always*  shape/x > 0  if shape < 1:
		     * -- overflow to Inf happens, but underflow to 0 does NOT : */
	    ? pr + (gnm_finite(shape/x)
		    ? gnm_log(shape/x)
		    : /* shape/x overflows to +Inf */ gnm_log(shape) - gnm_log(x))
	    : pr*shape / x);
    }
    /* else  shape >= 1 */
    pr = dpois_raw(shape-1, x/scale, give_log);
    return give_log ? pr - gnm_log(scale) : pr/scale;
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pgamma.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 2005-6 Morten Welinder <terra@gnome.org>
 *  Copyright (C) 2005-10 The R Foundation
 *  Copyright (C) 2006-2015 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  SYNOPSIS
 *
 *	#include <Rmath.h>
 *
 *	double pgamma (double x, double alph, double scale,
 *		       int lower_tail, int log_p)
 *
 *	double log1pmx	(double x)
 *	double lgamma1p (double a)
 *
 *	double logspace_add (double logx, double logy)
 *	double logspace_sub (double logx, double logy)
 *	double logspace_sum (double* logx, int n)
 *
 *
 *  DESCRIPTION
 *
 *	This function computes the distribution function for the
 *	gamma distribution with shape parameter alph and scale parameter
 *	scale.	This is also known as the incomplete gamma function.
 *	See Abramowitz and Stegun (6.5.1) for example.
 *
 *  NOTES
 *
 *	Complete redesign by Morten Welinder, originally for Gnumeric.
 *	Improvements (e.g. "while NEEDED_SCALE") by Martin Maechler
 *
 *  REFERENCES
 *
 */

/*----------- DEBUGGING -------------
 * make CFLAGS='-DDEBUG_p -g'
 * (cd `R-devel RHOME`/src/nmath; gcc -I. -I../../src/include -I../../../R/src/include  -DHAVE_CONFIG_H -fopenmp -DDEBUG_p -g -c ../../../R/src/nmath/pgamma.c -o pgamma.o)
 */

/* Scalefactor:= (2^32)^8 = 2^256 = 1.157921e+77 */
#define SQR(x) ((x)*(x))
static const gnm_float scalefactor = SQR(SQR(SQR(GNM_const(4294967296.0))));
#undef SQR

/* If |x| > |k| * M_cutoff,  then  log[ exp(-x) * k^x ]	 =~=  -x */
static const gnm_float M_cutoff = M_LN2gnum * GNM_MAX_EXP / GNM_EPSILON;/*=3.196577e18*/

/* Continued fraction for calculation of
 *    1/i + x/(i+d) + x^2/(i+2*d) + x^3/(i+3*d) + ... = sum_{k=0}^Inf x^k/(i+k*d)
 *
 * auxilary in log1pmx() and lgamma1p()
 */
gnm_float
gnm_logcf (gnm_float x, gnm_float i, gnm_float d,
       gnm_float eps /* ~ relative tolerance */)
{
    gnm_float c1 = 2 * d;
    gnm_float c2 = i + d;
    gnm_float c4 = c2 + d;
    gnm_float a1 = c2;
    gnm_float b1 = i * (c2 - i * x);
    gnm_float b2 = d * d * x;
    gnm_float a2 = c4 * c2 - b2;

#if 0
    assert (i > 0);
    assert (d >= 0);
#endif

    b2 = c4 * b1 - i * b2;

    while (gnm_abs(a2 * b1 - a1 * b2) > gnm_abs(eps * b1 * b2)) {
	gnm_float c3 = c2*c2*x;
	c2 += d;
	c4 += d;
	a1 = c4 * a2 - c3 * a1;
	b1 = c4 * b2 - c3 * b1;

	c3 = c1 * c1 * x;
	c1 += d;
	c4 += d;
	a2 = c4 * a1 - c3 * a2;
	b2 = c4 * b1 - c3 * b2;

	if (gnm_abs (b2) > scalefactor) {
	    a1 /= scalefactor;
	    b1 /= scalefactor;
	    a2 /= scalefactor;
	    b2 /= scalefactor;
	} else if (gnm_abs (b2) < 1 / scalefactor) {
	    a1 *= scalefactor;
	    b1 *= scalefactor;
	    a2 *= scalefactor;
	    b2 *= scalefactor;
	}
    }

    return a2 / b2;
}

/* Accurate calculation of log(1+x)-x, particularly for small x.  */
gnm_float log1pmx (gnm_float x)
{
    static const gnm_float minLog1Value = GNM_const(-0.79149064);

    if (x > 1 || x < minLog1Value)
	return gnm_log1p(x) - x;
    else { /* GNM_const(-.791) <=  x <= 1  -- expand in  [x/(2+x)]^2 =: y :
	    * gnm_log1p (x) - x =  x/(2+x) * [ 2 * y * S(y) - x],  with
	    * ---------------------------------------------
	    * S(y) = 1/3 + y/5 + y^2/7 + ... = \sum_{k=0}^\infty  y^k / (2k + 3)
	   */
	gnm_float r = x / (2 + x), y = r * r;
	if (gnm_abs(x) < GNM_const(1e-2)) {
	    static const gnm_float two = 2;
	    return r * ((((two / 9 * y + two / 7) * y + two / 5) * y +
			    two / 3) * y - x);
	} else {
	    static const gnm_float tol_logcf = GNM_const(1e-14);
	    return r * (2 * y * gnm_logcf (y, 3, 2, tol_logcf) - x);
	}
    }
}


/* Compute  log(gamma(a+1))  accurately also for small a (0 < a < 0.5). */
gnm_float lgamma1p (gnm_float a)
{
    const gnm_float eulers_const =	 GNM_const(0.5772156649015328606065120900824024);

    /* coeffs[i] holds (zeta(i+2)-1)/(i+2) , i = 0:(N-1), N = 40 : */
    const int N = 40;
    static const gnm_float coeffs[40] = {
	GNM_const(0.3224670334241132182362075833230126e-0),/* = (zeta(2)-1)/2 */
	GNM_const(0.6735230105319809513324605383715000e-1),/* = (zeta(3)-1)/3 */
	GNM_const(0.2058080842778454787900092413529198e-1),
	GNM_const(0.7385551028673985266273097291406834e-2),
	GNM_const(0.2890510330741523285752988298486755e-2),
	GNM_const(0.1192753911703260977113935692828109e-2),
	GNM_const(0.5096695247430424223356548135815582e-3),
	GNM_const(0.2231547584535793797614188036013401e-3),
	GNM_const(0.9945751278180853371459589003190170e-4),
	GNM_const(0.4492623673813314170020750240635786e-4),
	GNM_const(0.2050721277567069155316650397830591e-4),
	GNM_const(0.9439488275268395903987425104415055e-5),
	GNM_const(0.4374866789907487804181793223952411e-5),
	GNM_const(0.2039215753801366236781900709670839e-5),
	GNM_const(0.9551412130407419832857179772951265e-6),
	GNM_const(0.4492469198764566043294290331193655e-6),
	GNM_const(0.2120718480555466586923135901077628e-6),
	GNM_const(0.1004322482396809960872083050053344e-6),
	GNM_const(0.4769810169363980565760193417246730e-7),
	GNM_const(0.2271109460894316491031998116062124e-7),
	GNM_const(0.1083865921489695409107491757968159e-7),
	GNM_const(0.5183475041970046655121248647057669e-8),
	GNM_const(0.2483674543802478317185008663991718e-8),
	GNM_const(0.1192140140586091207442548202774640e-8),
	GNM_const(0.5731367241678862013330194857961011e-9),
	GNM_const(0.2759522885124233145178149692816341e-9),
	GNM_const(0.1330476437424448948149715720858008e-9),
	GNM_const(0.6422964563838100022082448087644648e-10),
	GNM_const(0.3104424774732227276239215783404066e-10),
	GNM_const(0.1502138408075414217093301048780668e-10),
	GNM_const(0.7275974480239079662504549924814047e-11),
	GNM_const(0.3527742476575915083615072228655483e-11),
	GNM_const(0.1711991790559617908601084114443031e-11),
	GNM_const(0.8315385841420284819798357793954418e-12),
	GNM_const(0.4042200525289440065536008957032895e-12),
	GNM_const(0.1966475631096616490411045679010286e-12),
	GNM_const(0.9573630387838555763782200936508615e-13),
	GNM_const(0.4664076026428374224576492565974577e-13),
	GNM_const(0.2273736960065972320633279596737272e-13),
	GNM_const(0.1109139947083452201658320007192334e-13)/* = (zeta(40+1)-1)/(40+1) */
    };

    const gnm_float c = GNM_const(0.2273736845824652515226821577978691e-12);/* zeta(N+2)-1 */
    const gnm_float tol_logcf = GNM_const(1e-14);
    gnm_float lgam;
    int i;

    if (gnm_abs (a) >= GNM_const(0.5))
	return gnm_lgamma (a + 1);

    /* Abramowitz & Stegun 6.1.33 : for |x| < 2,
     * <==> log(gamma(1+x)) = -(log(1+x) - x) - gamma*x + x^2 * \sum_{n=0}^\infty c_n (-x)^n
     * where c_n := (Zeta(n+2) - 1)/(n+2)  = coeffs[n]
     *
     * Here, another convergence acceleration trick is used to compute
     * lgam(x) :=  sum_{n=0..Inf} c_n (-x)^n
     */
    lgam = c * gnm_logcf(-a / 2, N + 2, 1, tol_logcf);
    for (i = N - 1; i >= 0; i--)
	lgam = coeffs[i] - a * lgam;

    return (a * lgam - eulers_const) * a - log1pmx (a);
} /* lgamma1p */



/*
 * Compute the log of a sum from logs of terms, i.e.,
 *
 *     log (exp (logx) + exp (logy))
 *
 * without causing overflows and without throwing away large handfuls
 * of accuracy.
 */
gnm_float logspace_add (gnm_float logx, gnm_float logy)
{
    return fmax2 (logx, logy) + gnm_log1p (gnm_exp (-gnm_abs (logx - logy)));
}


/*
 * Compute the log of a difference from logs of terms, i.e.,
 *
 *     log (exp (logx) - exp (logy))
 *
 * without causing overflows and without throwing away large handfuls
 * of accuracy.
 */
gnm_float logspace_sub (gnm_float logx, gnm_float logy)
{
    return logx + swap_log_tail(logy - logx);
}

/*
 * Compute the log of a sum from logs of terms, i.e.,
 *
 *     log (sum_i  exp (logx[i]) ) =
 *     log (e^M * sum_i  e^(logx[i] - M) ) =
 *     M + log( sum_i  e^(logx[i] - M)
 *
 * without causing overflows or throwing much accuracy.
 */
#ifdef HAVE_LONG_DOUBLE
# define EXP gnm_exp
# define LOG gnm_log
#else
# define EXP gnm_exp
# define LOG gnm_log
#endif
/* Definition of function logspace_sum removed.  */



/* dpois_wrap (x__1, lambda) := dpois(x__1 - 1, lambda);  where
 * dpois(k, L) := exp(-L) L^k / gamma(k+1)  {the usual Poisson probabilities}
 *
 * and  dpois*(.., give_log = TRUE) :=  log( dpois*(..) )
*/
static gnm_float
dpois_wrap (gnm_float x_plus_1, gnm_float lambda, gboolean give_log)
{
#ifdef DEBUG_p
    REprintf (" dpois_wrap(x+1=%.14" GNM_FORMAT_g ", lambda=%.14" GNM_FORMAT_g ", log=%d)\n",
	      x_plus_1, lambda, give_log);
#endif
    if (!gnm_finite(lambda))
	return R_D__0;
    if (x_plus_1 > 1)
	return dpois_raw (x_plus_1 - 1, lambda, give_log);
    if (lambda > gnm_abs(x_plus_1 - 1) * M_cutoff)
	return R_D_exp(-lambda - gnm_lgamma(x_plus_1));
    else {
	gnm_float d = dpois_raw (x_plus_1, lambda, give_log);
#ifdef DEBUG_p
	REprintf ("  -> d=dpois_raw(..)=%.14" GNM_FORMAT_g "\n", d);
#endif
	return give_log
	    ? d + gnm_log (x_plus_1 / lambda)
	    : d * (x_plus_1 / lambda);
    }
}

/*
 * Abramowitz and Stegun 6.5.29 [right]
 */
static gnm_float
pgamma_smallx (gnm_float x, gnm_float alph, gboolean lower_tail, gboolean log_p)
{
    gnm_float sum = 0, c = alph, n = 0, term;

#ifdef DEBUG_p
    REprintf (" pg_smallx(x=%.12" GNM_FORMAT_g ", alph=%.12" GNM_FORMAT_g "): ", x, alph);
#endif

    /*
     * Relative to 6.5.29 all terms have been multiplied by alph
     * and the first, thus being 1, is omitted.
     */

    do {
	n++;
	c *= -x / n;
	term = c / (alph + n);
	sum += term;
    } while (gnm_abs (term) > GNM_EPSILON * gnm_abs (sum));

#ifdef DEBUG_p
    REprintf ("%5" GNM_FORMAT_G "NM_const(.0)f terms --> conv.sum=%" GNM_FORMAT_g ";", n, sum);
#endif
    if (lower_tail) {
	gnm_float f1 = log_p ? gnm_log1p (sum) : 1 + sum;
	gnm_float f2;
	if (alph > 1) {
	    f2 = dpois_raw (alph, x, log_p);
	    f2 = log_p ? f2 + x : f2 * gnm_exp (x);
	} else if (log_p)
	    f2 = alph * gnm_log (x) - lgamma1p (alph);
	else
	    f2 = gnm_pow (x, alph) / gnm_exp (lgamma1p (alph));
#ifdef DEBUG_p
    REprintf (" (f1,f2)= (%" GNM_FORMAT_g ",%" GNM_FORMAT_g ")\n", f1,f2);
#endif
	return log_p ? f1 + f2 : f1 * f2;
    } else {
	gnm_float lf2 = alph * gnm_log (x) - lgamma1p (alph);
#ifdef DEBUG_p
	REprintf (" 1:%.14" GNM_FORMAT_g "  2:%.14" GNM_FORMAT_g "\n", alph * gnm_log (x), lgamma1p (alph));
	REprintf (" sum=%.14" GNM_FORMAT_g "  gnm_log1p (sum)=%.14" GNM_FORMAT_g "	 lf2=%.14" GNM_FORMAT_g "\n",
		  sum, gnm_log1p (sum), lf2);
#endif
	if (log_p)
	    return swap_log_tail (gnm_log1p (sum) + lf2);
	else {
	    gnm_float f1m1 = sum;
	    gnm_float f2m1 = gnm_expm1 (lf2);
	    return -(f1m1 + f2m1 + f1m1 * f2m1);
	}
    }
} /* pgamma_smallx() */

static gnm_float
pd_upper_series (gnm_float x, gnm_float y, gboolean log_p)
{
    gnm_float term = x / y;
    gnm_float sum = term;

    do {
	y++;
	term *= x / y;
	sum += term;
    } while (term > sum * GNM_EPSILON);

    /* sum =  \sum_{n=1}^ oo  x^n     / (y*(y+1)*...*(y+n-1))
     *	   =  \sum_{n=0}^ oo  x^(n+1) / (y*(y+1)*...*(y+n))
     *	   =  x/y * (1 + \sum_{n=1}^oo	x^n / ((y+1)*...*(y+n)))
     *	   ~  x/y +  o(x/y)   {which happens when alph -> Inf}
     */
    return log_p ? gnm_log (sum) : sum;
}

/* Continued fraction for calculation of
 *    scaled upper-tail F_{gamma}
 *  ~=  (y / d) * [1 +  (1-y)/d +  O( ((1-y)/d)^2 ) ]
 */
static gnm_float
pd_lower_cf (gnm_float y, gnm_float d)
{
    gnm_float f= GNM_const(0.0) /* -Wall */, of, f0;
    gnm_float i, c2, c3, c4,  a1, b1,  a2, b2;

#define	NEEDED_SCALE				\
	  (b2 > scalefactor) {			\
	    a1 /= scalefactor;			\
	    b1 /= scalefactor;			\
	    a2 /= scalefactor;			\
	    b2 /= scalefactor;			\
	}

#define max_it 200000

#ifdef DEBUG_p
    REprintf("pd_lower_cf(y=%.14" GNM_FORMAT_g ", d=%.14" GNM_FORMAT_g ")", y, d);
#endif
    if (y == 0) return 0;

    f0 = y/d;
    /* Needed, e.g. for  pgamma(10^c(100,295), shape= 1.1, log=TRUE): */
    if(gnm_abs(y - 1) < gnm_abs(d) * GNM_EPSILON) { /* includes y < d = Inf */
#ifdef DEBUG_p
	REprintf(" very small 'y' -> returning (y/d)\n");
#endif
	return (f0);
    }

    if(f0 > 1) f0 = 1;
    c2 = y;
    c4 = d; /* original (y,d), *not* potentially scaled ones!*/

    a1 = 0; b1 = 1;
    a2 = y; b2 = d;

    while NEEDED_SCALE

    i = 0; of = GNM_const(-1.); /* far away */
    while (i < max_it) {

	i++;	c2--;	c3 = i * c2;	c4 += 2;
	/* c2 = y - i,  c3 = i(y - i),  c4 = d + 2i,  for i odd */
	a1 = c4 * a2 + c3 * a1;
	b1 = c4 * b2 + c3 * b1;

	i++;	c2--;	c3 = i * c2;	c4 += 2;
	/* c2 = y - i,  c3 = i(y - i),  c4 = d + 2i,  for i even */
	a2 = c4 * a1 + c3 * a2;
	b2 = c4 * b1 + c3 * b2;

	if NEEDED_SCALE

	if (b2 != 0) {
	    f = a2 / b2;
	    /* convergence check: relative; "absolute" for very small f : */
	    if (gnm_abs (f - of) <= GNM_EPSILON * fmax2(f0, gnm_abs(f))) {
#ifdef DEBUG_p
		REprintf(" %" GNM_FORMAT_g " iter.\n", i);
#endif
		return f;
	    }
	    of = f;
	}
    }

    MATHLIB_WARNING(" ** NON-convergence in pgamma()'s pd_lower_cf() f= %" GNM_FORMAT_g ".\n",
		    f);
    return f;/* should not happen ... */
} /* pd_lower_cf() */
#undef NEEDED_SCALE


static gnm_float
pd_lower_series (gnm_float lambda, gnm_float y)
{
    gnm_float term = 1, sum = 0;

#ifdef DEBUG_p
    REprintf("pd_lower_series(lam=%.14" GNM_FORMAT_g ", y=%.14" GNM_FORMAT_g ") ...", lambda, y);
#endif
    while (y >= 1 && term > sum * GNM_EPSILON) {
	term *= y / lambda;
	sum += term;
	y--;
    }
    /* sum =  \sum_{n=0}^ oo  y*(y-1)*...*(y - n) / lambda^(n+1)
     *	   =  y/lambda * (1 + \sum_{n=1}^Inf  (y-1)*...*(y-n) / lambda^n)
     *	   ~  y/lambda + o(y/lambda)
     */
#ifdef DEBUG_p
    REprintf(" done: term=%" GNM_FORMAT_g ", sum=%" GNM_FORMAT_g ", y= %" GNM_FORMAT_g "\n", term, sum, y);
#endif

    if (y != gnm_floor (y)) {
	/*
	 * The series does not converge as the terms start getting
	 * bigger (besides flipping sign) for y < -lambda.
	 */
	gnm_float f;
#ifdef DEBUG_p
	REprintf(" y not int: add another term ");
#endif
	/* FIXME: in quite few cases, adding  term*f  has no effect (f too small)
	 *	  and is unnecessary e.g. for pgamma(4e12, 121.1) */
	f = pd_lower_cf (y, lambda + 1 - y);
#ifdef DEBUG_p
	REprintf("  (= %.14" GNM_FORMAT_g ") * term = %.14" GNM_FORMAT_g " to sum %" GNM_FORMAT_g "\n", f, term * f, sum);
#endif
	sum += term * f;
    }

    return sum;
} /* pd_lower_series() */

/*
 * Compute the following ratio with higher accuracy that would be had
 * from doing it directly.
 *
 *		 dnorm (x, 0, 1, FALSE)
 *	   ----------------------------------
 *	   pnorm (x, 0, 1, lower_tail, FALSE)
 *
 * Abramowitz & Stegun 26.2.12
 */
static gnm_float
dpnorm (gnm_float x, gboolean lower_tail, gnm_float lp)
{
    /*
     * So as not to repeat a pnorm call, we expect
     *
     *	 lp == pnorm (x, 0, 1, lower_tail, TRUE)
     *
     * but use it only in the non-critical case where either x is small
     * or p==exp(lp) is close to 1.
     */

    if (x < 0) {
	x = -x;
	lower_tail = !lower_tail;
    }

    if (x > 10 && !lower_tail) {
	gnm_float term = 1 / x;
	gnm_float sum = term;
	gnm_float x2 = x * x;
	gnm_float i = 1;

	do {
	    term *= -i / x2;
	    sum += term;
	    i += 2;
	} while (gnm_abs (term) > GNM_EPSILON * sum);

	return 1 / sum;
    } else {
	gnm_float d = dnorm (x, GNM_const(0.), GNM_const(1.), FALSE);
	return d / gnm_exp (lp);
    }
}

/*
 * Asymptotic expansion to calculate the probability that Poisson variate
 * has value <= x.
 * Various assertions about this are made (without proof) at
 * http://members.aol.com/iandjmsmith/PoissonApprox.htm
 */
static gnm_float
ppois_asymp (gnm_float x, gnm_float lambda, gboolean lower_tail, gboolean log_p)
{
    static const gnm_float coefs_a[8] = {
	GNM_const(-1e99), /* placeholder used for 1-indexing */
	2/GNM_const(3.),
	-4/GNM_const(135.),
	8/GNM_const(2835.),
	16/GNM_const(8505.),
	-8992/GNM_const(12629925.),
	-334144/GNM_const(492567075.),
	698752/GNM_const(1477701225.)
    };

    static const gnm_float coefs_b[8] = {
	GNM_const(-1e99), /* placeholder */
	1/GNM_const(12.),
	1/GNM_const(288.),
	-139/GNM_const(51840.),
	-571/GNM_const(2488320.),
	163879/GNM_const(209018880.),
	5246819/GNM_const(75246796800.),
	-534703531/GNM_const(902961561600.)
    };

    gnm_float elfb, elfb_term;
    gnm_float res12, res1_term, res1_ig, res2_term, res2_ig;
    gnm_float dfm, pt_, s2pt, f, np;
    int i;

    dfm = lambda - x;
    /* If lambda is large, the distribution is highly concentrated
       about lambda.  So representation error in x or lambda can lead
       to arbitrarily large values of pt_ and hence divergence of the
       coefficients of this approximation.
    */
    pt_ = - log1pmx (dfm / x);
    s2pt = gnm_sqrt (2 * x * pt_);
    if (dfm < 0) s2pt = -s2pt;

    res12 = 0;
    res1_ig = res1_term = gnm_sqrt (x);
    res2_ig = res2_term = s2pt;
    for (i = 1; i < 8; i++) {
	res12 += res1_ig * coefs_a[i];
	res12 += res2_ig * coefs_b[i];
	res1_term *= pt_ / i ;
	res2_term *= 2 * pt_ / (2 * i + 1);
	res1_ig = res1_ig / x + res1_term;
	res2_ig = res2_ig / x + res2_term;
    }

    elfb = x;
    elfb_term = 1;
    for (i = 1; i < 8; i++) {
	elfb += elfb_term * coefs_b[i];
	elfb_term /= x;
    }
    if (!lower_tail) elfb = -elfb;
#ifdef DEBUG_p
    REprintf ("res12 = %.14" GNM_FORMAT_g "   elfb=%.14" GNM_FORMAT_g "\n", elfb, res12);
#endif

    f = res12 / elfb;

    np = pnorm (s2pt, GNM_const(0.0), GNM_const(1.0), !lower_tail, log_p);

    if (log_p) {
	gnm_float n_d_over_p = dpnorm (s2pt, !lower_tail, np);
#ifdef DEBUG_p
	REprintf ("pp*_asymp(): f=%.14" GNM_FORMAT_g "	 np=e^%.14" GNM_FORMAT_g "  nd/np=%.14" GNM_FORMAT_g "  f*nd/np=%.14" GNM_FORMAT_g "\n",
		  f, np, n_d_over_p, f * n_d_over_p);
#endif
	return np + gnm_log1p (f * n_d_over_p);
    } else {
	gnm_float nd = dnorm (s2pt, GNM_const(0.), GNM_const(1.), log_p);

#ifdef DEBUG_p
	REprintf ("pp*_asymp(): f=%.14" GNM_FORMAT_g "	 np=%.14" GNM_FORMAT_g "  nd=%.14" GNM_FORMAT_g "  f*nd=%.14" GNM_FORMAT_g "\n",
		  f, np, nd, f * nd);
#endif
	return np + f * nd;
    }
} /* ppois_asymp() */


static gnm_float pgamma_raw (gnm_float x, gnm_float alph, gboolean lower_tail, gboolean log_p)
{
/* Here, assume that  (x,alph) are not NA  &  alph > 0 . */

    gnm_float res;

#ifdef DEBUG_p
    REprintf("pgamma_raw(x=%.14" GNM_FORMAT_g ", alph=%.14" GNM_FORMAT_g ", low=%d, log=%d)\n",
	     x, alph, lower_tail, log_p);
#endif
    R_P_bounds_01(x, GNM_const(0.), gnm_pinf);

    if (x < 1) {
	res = pgamma_smallx (x, alph, lower_tail, log_p);
    } else if (x <= alph - 1 && x < GNM_const(0.8) * (alph + 50)) {
	/* incl. large alph compared to x */
	gnm_float sum = pd_upper_series (x, alph, log_p);/* = x/alph + o(x/alph) */
	gnm_float d = dpois_wrap (alph, x, log_p);
#ifdef DEBUG_p
	REprintf(" alph 'large': sum=pd_upper*()= %.12" GNM_FORMAT_g ", d=dpois_w(*)= %.12" GNM_FORMAT_g "\n",
		 sum, d);
#endif
	if (!lower_tail)
	    res = log_p
		? swap_log_tail (d + sum)
		: 1 - d * sum;
	else
	    res = log_p ? sum + d : sum * d;
    } else if (alph - 1 < x && alph < GNM_const(0.8) * (x + 50)) {
	/* incl. large x compared to alph */
	gnm_float sum;
	gnm_float d = dpois_wrap (alph, x, log_p);
#ifdef DEBUG_p
	REprintf(" x 'large': d=dpois_w(*)= %.14" GNM_FORMAT_g " ", d);
#endif
	if (alph < 1) {
	    if (x * GNM_EPSILON > 1 - alph)
		sum = R_D__1;
	    else {
		gnm_float f = pd_lower_cf (alph, x - (alph - 1)) * x / alph;
		/* = [alph/(x - alph+1) + o(alph/(x-alph+1))] * x/alph = 1 + o(1) */
		sum = log_p ? gnm_log (f) : f;
	    }
	} else {
	    sum = pd_lower_series (x, alph - 1);/* = (alph-1)/x + o((alph-1)/x) */
	    sum = log_p ? gnm_log1p (sum) : 1 + sum;
	}
#ifdef DEBUG_p
	REprintf(", sum= %.14" GNM_FORMAT_g "\n", sum);
#endif
	if (!lower_tail)
	    res = log_p ? sum + d : sum * d;
	else
	    res = log_p
		? swap_log_tail (d + sum)
		: 1 - d * sum;
    } else { /* x >= 1 and x fairly near alph. */
#ifdef DEBUG_p
	REprintf(" using ppois_asymp()\n");
#endif
	res = ppois_asymp (alph - 1, x, !lower_tail, log_p);
    }

    /*
     * We lose a fair amount of accuracy to underflow in the cases
     * where the final result is very close to DBL_MIN.	 In those
     * cases, simply redo via log space.
     */
    if (!log_p && res < GNM_MIN / GNM_EPSILON) {
	/* with(.Machine, double.xmin / double.eps) #|-> 1.002084e-292 */
#ifdef DEBUG_p
	REprintf(" very small res=%.14" GNM_FORMAT_g "; -> recompute via log\n", res);
#endif
	return gnm_exp (pgamma_raw (x, alph, lower_tail, 1));
    } else
	return res;
}


gnm_float pgamma(gnm_float x, gnm_float alph, gnm_float scale, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(alph) || gnm_isnan(scale))
	return x + alph + scale;
#endif
    if(alph < 0 || scale <= 0)
	ML_WARN_return_NAN;
    x /= scale;
#ifdef IEEE_754
    if (gnm_isnan(x)) /* eg. original x = scale = +Inf */
	return x;
#endif
    if(alph == 0) /* limit case; useful e.g. in pnchisq() */
	return (x <= 0) ? R_DT_0: R_DT_1; /* <= assert  pgamma(0,0) ==> 0 */
    return pgamma_raw (x, alph, lower_tail, log_p);
}
/* From: terra@gnome.org (Morten Welinder)
 * To: R-bugs@biostat.ku.dk
 * Cc: maechler@stat.math.ethz.ch
 * Subject: Re: [Rd] pgamma discontinuity (PR#7307)
 * Date: Tue, 11 Jan 2005 13:57:26 -0500 (EST)

 * this version of pgamma appears to be quite good and certainly a vast
 * improvement over current R code.  (I last looked at 2.0.1)  Apart from
 * type naming, this is what I have been using for Gnumeric 1.4.1.

 * This could be included into R as-is, but you might want to benefit from
 * making logcf, log1pmx, lgamma1p, and possibly logspace_add/logspace_sub
 * available to other parts of R.

 * MM: I've not (yet?) taken  logcf(), but the other four
 */
/* Cleaning up done by tools/import-R:  */
#undef EXP
#undef LOG
#undef max_it

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dt.c from R.  */
/*
 *  AUTHOR
 *    Catherine Loader, catherine@research.bell-labs.com.
 *    October 23, 2000.
 *
 *  Merge in to R:
 *	Copyright (C) 2000-2015 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *
 * DESCRIPTION
 *
 *    The t density is evaluated as
 *         sqrt(n/2) / ((n+1)/2) * Gamma((n+3)/2) / Gamma((n+2)/2).
 *             * (1+x^2/n)^(-n/2)
 *             / sqrt( 2 pi (1+x^2/n) )
 *
 *    This form leads to a stable computation for all
 *    values of n, including n -> 0 and n -> infinity.
 */


gnm_float dt(gnm_float x, gnm_float n, gboolean give_log)
{
#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(n))
	return x + n;
#endif
    if (n <= 0) ML_WARN_return_NAN;
    if(!gnm_finite(x))
	return R_D__0;
    if(!gnm_finite(n))
	return dnorm(x, GNM_const(0.), GNM_const(1.), give_log);

    gnm_float u, t = -bd0(n/GNM_const(2.),(n+1)/GNM_const(2.)) + stirlerr((n+1)/GNM_const(2.)) - stirlerr(n/GNM_const(2.)),
	x2n = x*x/n, // in  [0, Inf]
	ax = GNM_const(0.), // <- -Wpedantic
	l_x2n; // := gnm_log(gnm_sqrt(1 + x2n)) = gnm_log1p (x2n)/2
    gboolean lrg_x2n =  (x2n > GNM_const(1.)/GNM_EPSILON);
    if (lrg_x2n) { // large x^2/n :
	ax = gnm_abs(x);
	l_x2n = gnm_log(ax) - gnm_log(n)/GNM_const(2.); // = gnm_log(x2n)/2 = 1/2 * gnm_log(x^2 / n)
	u = //  gnm_log1p (x2n) * n/2 =  n * gnm_log1p (x2n)/2 =
	    n * l_x2n;
    }
    else if (x2n > GNM_const(0.2)) {
	l_x2n = gnm_log1p (x2n)/GNM_const(2.);
	u = n * l_x2n;
    } else {
	l_x2n = gnm_log1p(x2n)/GNM_const(2.);
	u = -bd0(n/GNM_const(2.),(n+x*x)/GNM_const(2.)) + x*x/GNM_const(2.);
    }

    //old: return  R_D_fexp(M_2PIgnum*(1+x2n), t-u);

    // R_D_fexp(f,x) :=  (give_log ? GNM_const(-0.5)*gnm_log(f)+(x) : gnm_exp(x)/gnm_sqrt(f))
    // f = 2pi*(1+x2n)
    //  ==> GNM_const(0.5)*gnm_log(f) = gnm_log(2pi)/2 + gnm_log1p (x2n)/2 = gnm_log(2pi)/2 + l_x2n
    //	     1/gnm_sqrt(f) = 1/gnm_sqrt(2pi * (1+ x^2 / n))
    //		       = 1/gnm_sqrt(2pi)/(|x|/gnm_sqrt(n)*gnm_sqrt(1+1/x2n))
    //		       = M_1_SQRT_2PI * gnm_sqrt(n)/ (|x|*gnm_sqrt(1+1/x2n))
    if(give_log)
	return t-u - (M_LN_SQRT_2PI + l_x2n);

    // else :  if(lrg_x2n) : gnm_sqrt(1 + 1/x2n) ='= gnm_sqrt(1) = 1
    gnm_float I_sqrt_ = (lrg_x2n ? gnm_sqrt(n)/ax : gnm_exp(-l_x2n));
    return gnm_exp(t-u) * M_1_SQRT_2PI * I_sqrt_;
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pt.c from R.  */
/*
 *  R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 2000-2007   The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 */


gnm_float pt(gnm_float x, gnm_float n, gboolean lower_tail, gboolean log_p)
{
/* return  P[ T <= x ]	where
 * T ~ t_{n}  (t distrib. with n degrees of freedom).

 *	--> ./pnt.c for NON-central
 */
    gnm_float val, nx;
#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(n))
	return x + n;
#endif
    if (n <= 0) ML_WARN_return_NAN;

    if(!gnm_finite(x))
	return (x < 0) ? R_DT_0 : R_DT_1;
    if(!gnm_finite(n))
	return pnorm(x, GNM_const(0.0), GNM_const(1.0), lower_tail, log_p);

#ifdef R_version_le_260
    if (n > GNM_const(4e5)) { /*-- Fixme(?): test should depend on `n' AND `x' ! */
	/* Approx. from	 Abramowitz & Stegun 26.7.8 (p.949) */
	val = GNM_const(1.)/(GNM_const(4.)*n);
	return pnorm(x*(GNM_const(1.) - val)/gnm_sqrt(GNM_const(1.) + x*x*GNM_const(2.)*val), GNM_const(0.0), GNM_const(1.0),
		     lower_tail, log_p);
    }
#endif

    nx = 1 + (x/n)*x;
    /* FIXME: This test is probably losing rather than gaining precision,
     * now that pbeta(*, log_p = TRUE) is much better.
     * Note however that a version of this test *is* needed for x*x > D_MAX */
    if(nx > GNM_const(1e100)) { /* <==>  x*x > 1e100 * n  */
	/* Danger of underflow. So use Abramowitz & Stegun 26.5.4
	   pbeta(z, a, b) ~ z^a(1-z)^b / aB(a,b) ~ z^a / aB(a,b),
	   with z = 1/nx,  a = n/2,  b= 1/2 :
	*/
	gnm_float lval;
	lval = GNM_const(-0.5)*n*(2*gnm_log(gnm_abs(x)) - gnm_log(n))
		- gnm_lbeta(GNM_const(0.5)*n, GNM_const(0.5)) - gnm_log(GNM_const(0.5)*n);
	val = log_p ? lval : gnm_exp(lval);
    } else {
	val = (n > x * x)
	    ? pbeta (x * x / (n + x * x), GNM_const(0.5), n / GNM_const(2.), /*lower_tail*/0, log_p)
	    : pbeta (GNM_const(1.) / nx,             n / GNM_const(2.), GNM_const(0.5), /*lower_tail*/1, log_p);
    }

    /* Use "1 - v"  if	lower_tail  and	 x > 0 (but not both):*/
    if(x <= 0)
	lower_tail = !lower_tail;

    if(log_p) {
	if(lower_tail) return gnm_log1p(GNM_const(-0.5)*gnm_exp(val));
	else return val - M_LN2gnum; /* = log(.5* pbeta(....)) */
    }
    else {
	val /= 2;
	return R_D_Cval(val);
    }
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qt.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000-2013 The R Core Team
 *  Copyright (C) 2003-2013 The R Foundation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *	The "Student" t distribution quantile function.
 *
 *  NOTES
 *
 *	This is a C translation of the Fortran routine given in:
 *	Hill, G.W (1970) "Algorithm 396: Student's t-quantiles"
 *	CACM 13(10), 619-620.
 *
 *	Supplemented by inversion for 0 < ndf < 1.
 *
 *  ADDITIONS:
 *	- lower_tail, log_p
 *	- using	 expm1() : takes care of  Lozy (1979) "Remark on Algo.", TOMS
 *	- Apply 2-term Taylor expansion as in
 *	  Hill, G.W (1981) "Remark on Algo.396", ACM TOMS 7, 250-1
 *	- Improve the formula decision for 1 < df < 2
 */


gnm_float qt(gnm_float p, gnm_float ndf, gboolean lower_tail, gboolean log_p)
{
    static const gnm_float eps = GNM_const(1.e-12);

    gnm_float P, q;

#ifdef IEEE_754
    if (gnm_isnan(p) || gnm_isnan(ndf))
	return p + ndf;
#endif

    R_Q_P01_boundaries(p, gnm_ninf, gnm_pinf);

    if (ndf <= 0) ML_WARN_return_NAN;

    if (ndf < 1) { /* based on qnt */
	static const gnm_float accu = GNM_const(1e-13);
	static const gnm_float Eps = GNM_const(1e-11); /* must be > accu */

	gnm_float ux, lx, nx, pp;

	int iter = 0;

	p = R_DT_qIv(p);

	/* Invert pt(.) :
	 * 1. finding an upper and lower bound */
	if(p > 1 - GNM_EPSILON) return gnm_pinf;
	pp = fmin2(1 - GNM_EPSILON, p * (1 + Eps));
	for(ux = 1; ux < GNM_MAX && pt(ux, ndf, TRUE, FALSE) < pp; ux *= 2);
	pp = p * (1 - Eps);
	for(lx =GNM_const(-1.); lx > -GNM_MAX && pt(lx, ndf, TRUE, FALSE) > pp; lx *= 2);

	/* 2. interval (lx,ux)  halving
	   regula falsi failed on qt(0.1, 0.1)
	 */
	do {
	    nx = GNM_const(0.5) * (lx + ux);
	    if (pt(nx, ndf, TRUE, FALSE) > p) ux = nx; else lx = nx;
	} while ((ux - lx) / gnm_abs(nx) > accu && ++iter < 1000);

	if(iter >= 1000) ML_WARNING(ME_PRECISION, "qt");

	return GNM_const(0.5) * (lx + ux);
    }

    /* Old comment:
     * FIXME: "This test should depend on  ndf  AND p  !!
     * -----  and in fact should be replaced by
     * something like Abramowitz & Stegun 26.7.5 (p.949)"
     *
     * That would say that if the qnorm value is x then
     * the result is about x + (x^3+x)/4df + (5x^5+16x^3+3x)/96df^2
     * The differences are tiny even if x ~ 1e5, and qnorm is not
     * that accurate in the extreme tails.
     */
    if (ndf > GNM_const(1e20)) return qnorm(p, GNM_const(0.), GNM_const(1.), lower_tail, log_p);

    P = R_D_qIv(p); /* if exp(p) underflows, we fix below */

    gboolean neg = (!lower_tail || P < GNM_const(0.5)) && (lower_tail || P > GNM_const(0.5)),
	is_neg_lower = (lower_tail == neg); /* both TRUE or FALSE == !xor */
    if(neg)
	P = 2 * (log_p ? (lower_tail ? P : -gnm_expm1(p)) : R_D_Lval(p));
    else
	P = 2 * (log_p ? (lower_tail ? -gnm_expm1(p) : P) : R_D_Cval(p));
    /* 0 <= P <= 1 ; P = 2*min(P', 1 - P')  in all cases */

     if (gnm_abs(ndf - 2) < eps) {	/* df ~= 2 */
	if(P > GNM_MIN) {
	    if(3* P < GNM_EPSILON) /* P ~= 0 */
		q = 1 / gnm_sqrt(P);
	    else if (P > GNM_const(0.9))	   /* P ~= 1 */
		q = (1 - P) * gnm_sqrt(2 /(P * (2 - P)));
	    else /* eps/3 <= P <= 0.9 */
		q = gnm_sqrt(2 / (P * (2 - P)) - 2);
	}
	else { /* P << 1, q = 1/sqrt(P) = ... */
	    if(log_p)
		q = is_neg_lower ? gnm_exp(- p/2) / M_SQRT2gnum : 1/gnm_sqrt(-gnm_expm1(p));
	    else
		q = gnm_pinf;
	}
    }
    else if (ndf < 1 + eps) { /* df ~= 1  (df < 1 excluded above): Cauchy */
	if(P == 1) q = 0; // some versions of gnm_tanpi give Inf, some NaN
	else if(P > 0)
	    q = 1/gnm_tanpi(P/GNM_const(2.));/* == - tan((P+1) * M_PI_2) -- suffers for P ~= 0 */

	else { /* P = 0, but maybe = 2*exp(p) ! */
	    if(log_p) /* 1/tan(e) ~ 1/e */
		q = is_neg_lower ? M_1_PIgnum * gnm_exp(-p) : GNM_const(-1.)/(M_PIgnum * gnm_expm1(p));
	    else
		q = gnm_pinf;
	}
    }
    else {		/*-- usual case;  including, e.g.,  df = 1.1 */
	gnm_float x = GNM_const(0.), y, log_P2 = GNM_const(0.)/* -Wall */,
	    a = 1 / (ndf - GNM_const(0.5)),
	    b = 48 / (a * a),
	    c = ((20700 * a / b - 98) * a - 16) * a + GNM_const(96.36),
	    d = ((GNM_const(94.5) / (b + c) - 3) / b + 1) * gnm_sqrt(a * M_PI_2gnum) * ndf;

	gboolean P_ok1 = P > GNM_MIN || !log_p,  P_ok = P_ok1;
	if(P_ok1) {
	    y = gnm_pow(d * P, GNM_const(2.0) / ndf);
	    P_ok = (y >= GNM_EPSILON);
	}
	if(!P_ok) {// log.p && P very.small  ||  (d*P)^(2/df) =: y < eps_c
	    log_P2 = is_neg_lower ? R_D_log(p) : R_D_LExp(p); /* == log(P / 2) */
	    x = (gnm_log(d) + M_LN2gnum + log_P2) / ndf;
	    y = gnm_exp(2 * x);
	}

	if ((ndf < GNM_const(2.1) && P > GNM_const(0.5)) || y > GNM_const(0.05) + a) { /* P > P0(df) */
	    /* Asymptotic inverse expansion about normal */
	    if(P_ok)
		x = qnorm(GNM_const(0.5) * P, GNM_const(0.), GNM_const(1.), /*lower_tail*/TRUE,  /*log_p*/FALSE);
	    else /* log_p && P underflowed */
		x = qnorm(log_P2,  GNM_const(0.), GNM_const(1.), lower_tail,	        /*log_p*/ TRUE);

	    y = x * x;
	    if (ndf < 5)
		c += GNM_const(0.3) * (ndf - GNM_const(4.5)) * (x + GNM_const(0.6));
	    c = (((GNM_const(0.05) * d * x - 5) * x - 7) * x - 2) * x + b + c;
	    y = (((((GNM_const(0.4) * y + GNM_const(6.3)) * y + 36) * y + GNM_const(94.5)) / c
		  - y - 3) / b + 1) * x;
	    y = gnm_expm1(a * y * y);
	    q = gnm_sqrt(ndf * y);
	} else if(!P_ok && x < - M_LN2gnum * DBL_MANT_DIG) {/* 0.5* log(DBL_EPSILON) */
	    /* y above might have underflown */
	    q = gnm_sqrt(ndf) * gnm_exp(-x);
	}
	else { /* re-use 'y' from above */
	    y = ((1 / (((ndf + 6) / (ndf * y) - GNM_const(0.089) * d - GNM_const(0.822))
		       * (ndf + 2) * 3) + GNM_const(0.5) / (ndf + 4))
		 * y - 1) * (ndf + 1) / (ndf + 2) + 1 / y;
	    q = gnm_sqrt(ndf * y);
	}


	/* Now apply 2-term Taylor expansion improvement (1-term = Newton):
	 * as by Hill (1981) [ref.above] */

	/* FIXME: This can be far from optimal when log_p = TRUE
	 *      but is still needed, e.g. for qt(-2, df=1.01, log=TRUE).
	 *	Probably also improvable when  lower_tail = FALSE */

	if(P_ok1) {
	    int it=0;
	    while(it++ < 10 && (y = dt(q, ndf, FALSE)) > 0 &&
		  gnm_finite(x = (pt(q, ndf, FALSE, FALSE) - P/2) / y) &&
		  gnm_abs(x) > GNM_const(1e-14)*gnm_abs(q))
		/* Newton (=Taylor 1 term):
		 *  q += x;
		 * Taylor 2-term : */
		q += x * (GNM_const(1.) + x * q * (ndf + 1) / (2 * (q * q + ndf)));
	}
    }
    if(neg) q = -q;
    return q;
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pchisq.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998	Ross Ihaka
 *  Copyright (C) 2000	The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *     The distribution function of the chi-squared distribution.
 */


gnm_float pchisq(gnm_float x, gnm_float df, gboolean lower_tail, gboolean log_p)
{
    return pgamma(x, df/GNM_const(2.), GNM_const(2.), lower_tail, log_p);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qchisq.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *	The quantile function of the chi-squared distribution.
 */


gnm_float qchisq(gnm_float p, gnm_float df, gboolean lower_tail, gboolean log_p)
{
    return qgamma(p, GNM_const(0.5) * df, GNM_const(2.0), lower_tail, log_p);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dweibull.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000-6 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *    The density function of the Weibull distribution.
 */


gnm_float dweibull(gnm_float x, gnm_float shape, gnm_float scale, gboolean give_log)
{
    gnm_float tmp1, tmp2;
#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(shape) || gnm_isnan(scale))
	return x + shape + scale;
#endif
    if (shape <= 0 || scale <= 0) ML_WARN_return_NAN;

    if (x < 0) return R_D__0;
    if (!gnm_finite(x)) return R_D__0;
    /* need to handle x == 0 separately */
    if(x == 0 && shape < 1) return gnm_pinf;
    tmp1 = gnm_pow(x / scale, shape - 1);
    tmp2 = tmp1 * (x / scale);
    /* These are incorrect if tmp1 == 0 */
    return  give_log ?
	-tmp2 + gnm_log(shape * tmp1 / scale) :
	shape * tmp1 * gnm_exp(-tmp2) / scale;
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pweibull.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000-2015 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *    The distribution function of the Weibull distribution.
 */


gnm_float pweibull(gnm_float x, gnm_float shape, gnm_float scale, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(shape) || gnm_isnan(scale))
	return x + shape + scale;
#endif
    if(shape <= 0 || scale <= 0) ML_WARN_return_NAN;

    if (x <= 0)
	return R_DT_0;
    x = -gnm_pow(x / scale, shape);
    return lower_tail
	? (log_p ? swap_log_tail(x) : -gnm_expm1(x))
	: R_D_exp(x);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pbinom.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000-2015  The R Core Team
 *  Copyright (C) 2004-2015  The R Foundation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *    The distribution function of the binomial distribution.
 */

gnm_float pbinom(gnm_float x, gnm_float n, gnm_float p, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(n) || gnm_isnan(p))
	return x + n + p;
    if (!gnm_finite(n) || !gnm_finite(p)) ML_WARN_return_NAN;

#endif
    if(R_nonint(n)) {
	MATHLIB_WARNING(("non-integer n = %" GNM_FORMAT_f ""), n);
	ML_WARN_return_NAN;
    }
    n = gnm_round(n);
    /* PR#8560: n=0 is a valid value */
    if(n < 0 || p < 0 || p > 1) ML_WARN_return_NAN;

    if (x < 0) return R_DT_0;
    x = gnm_fake_floor(x);
    if (n <= x) return R_DT_1;
    return pbeta(p, x + 1, n - x, !lower_tail, log_p);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dbinom.c from R.  */
/*
 * AUTHOR
 *   Catherine Loader, catherine@research.bell-labs.com.
 *   October 23, 2000.
 *
 *  Merge in to R and further tweaks :
 *	Copyright (C) 2000-2015 The R Core Team
 *	Copyright (C) 2008 The R Foundation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *
 * DESCRIPTION
 *
 *   To compute the binomial probability, call dbinom(x,n,p).
 *   This checks for argument validity, and calls dbinom_raw().
 *
 *   dbinom_raw() does the actual computation; note this is called by
 *   other functions in addition to dbinom().
 *     (1) dbinom_raw() has both p and q arguments, when one may be represented
 *         more accurately than the other (in particular, in df()).
 *     (2) dbinom_raw() does NOT check that inputs x and n are integers. This
 *         should be done in the calling function, where necessary.
 *         -- but is not the case at all when called e.g., from df() or dbeta() !
 *     (3) Also does not check for 0 <= p <= 1 and 0 <= q <= 1 or NaN's.
 *         Do this in the calling function.
 */


/* Definition of function dbinom_raw removed.  */

gnm_float dbinom(gnm_float x, gnm_float n, gnm_float p, gboolean give_log)
{
#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (gnm_isnan(x) || gnm_isnan(n) || gnm_isnan(p)) return x + n + p;
#endif

    if (p < 0 || p > 1 || R_D_negInonint(n))
	ML_WARN_return_NAN;
    R_D_nonint_check(x);
    if (x < 0 || !gnm_finite(x)) return R_D__0;

    n = gnm_round(n);
    x = gnm_round(x);

    return dbinom_raw(x, n, p, 1-p, give_log);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qbinom.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000-2009 The R Core Team
 *  Copyright (C) 2003-2009 The R Foundation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *	The quantile function of the binomial distribution.
 *
 *  METHOD
 *
 *	Uses the Cornish-Fisher Expansion to include a skewness
 *	correction to a normal approximation.  This gives an
 *	initial value which never seems to be off by more than
 *	1 or 2.	 A search is then conducted of values close to
 *	this initial start point.
 */

static gnm_float
do_search(gnm_float y, gnm_float *z, gnm_float p, gnm_float n, gnm_float pr, gnm_float incr)
{
    if(*z >= p) {
			/* search to the left */
#ifdef DEBUG_qbinom
	REprintf("\tnew z=%7" GNM_FORMAT_g " >= p = %7" GNM_FORMAT_g "  --> search to left (y--) ..\n", z,p);
#endif
	for(;;) {
	    gnm_float newz;
	    if(y == 0 ||
	       (newz = pbinom(y - incr, n, pr, /*l._t.*/TRUE, /*log_p*/FALSE)) < p)
		return y;
	    y = fmax2(0, y - incr);
	    *z = newz;
	}
    }
    else {		/* search to the right */
#ifdef DEBUG_qbinom
	REprintf("\tnew z=%7" GNM_FORMAT_g " < p = %7" GNM_FORMAT_g "  --> search to right (y++) ..\n", z,p);
#endif
	for(;;) {
	    y = fmin2(y + incr, n);
	    if(y == n ||
	       (*z = pbinom(y, n, pr, /*l._t.*/TRUE, /*log_p*/FALSE)) >= p)
		return y;
	}
    }
}


gnm_float qbinom(gnm_float p, gnm_float n, gnm_float pr, gboolean lower_tail, gboolean log_p)
{
    gnm_float q, mu, sigma, gamma, z, y;

#ifdef IEEE_754
    if (gnm_isnan(p) || gnm_isnan(n) || gnm_isnan(pr))
	return p + n + pr;
#endif
    if(!gnm_finite(n) || !gnm_finite(pr))
	ML_WARN_return_NAN;
    /* if log_p is true, p = -Inf is a legitimate value */
    if(!gnm_finite(p) && !log_p)
	ML_WARN_return_NAN;

    if(n != gnm_floor(n + GNM_const(0.5))) ML_WARN_return_NAN;
    if (pr < 0 || pr > 1 || n < 0)
	ML_WARN_return_NAN;

    R_Q_P01_boundaries(p, 0, n);

    if (pr == 0 || n == 0) return GNM_const(0.);

    q = 1 - pr;
    if(q == 0) return n; /* covers the full range of the distribution */
    mu = n * pr;
    sigma = gnm_sqrt(n * pr * q);
    gamma = (q - pr) / sigma;

#ifdef DEBUG_qbinom
    REprintf("qbinom(p=%7" GNM_FORMAT_g ", n=%" GNM_FORMAT_g ", pr=%7" GNM_FORMAT_g ", l.t.=%d, log=%d): sigm=%" GNM_FORMAT_g ", gam=%" GNM_FORMAT_g "\n",
	     p,n,pr, lower_tail, log_p, sigma, gamma);
#endif
    /* Note : "same" code in qpois.c, qbinom.c, qnbinom.c --
     * FIXME: This is far from optimal [cancellation for p ~= 1, etc]: */
    if(!lower_tail || log_p) {
	p = R_DT_qIv(p); /* need check again (cancellation!): */
	if (p == 0) return GNM_const(0.);
	if (p == 1) return n;
    }
    /* temporary hack --- FIXME --- */
    if (p + GNM_const(1.01)*GNM_EPSILON >= 1) return n;

    /* y := approx.value (Cornish-Fisher expansion) :  */
    z = qnorm(p, GNM_const(0.), GNM_const(1.), /*lower_tail*/TRUE, /*log_p*/FALSE);
    y = gnm_floor(mu + sigma * (z + gamma * (z*z - 1) / 6) + GNM_const(0.5));

    if(y > n) /* way off */ y = n;

#ifdef DEBUG_qbinom
    REprintf("  new (p,1-p)=(%7" GNM_FORMAT_g ",%7" GNM_FORMAT_g "), z=qnorm(..)=%7" GNM_FORMAT_g ", y=%5" GNM_FORMAT_g "\n", p, 1-p, z, y);
#endif
    z = pbinom(y, n, pr, /*lower_tail*/TRUE, /*log_p*/FALSE);

    /* fuzz to ensure left continuity: */
    p *= 1 - 64*GNM_EPSILON;

    if(n < GNM_const(1e5)) return do_search(y, &z, p, n, pr, 1);
    /* Otherwise be a bit cleverer in the search */
    {
	gnm_float incr = gnm_floor(n * GNM_const(0.001)), oldincr;
	do {
	    oldincr = incr;
	    y = do_search(y, &z, p, n, pr, incr);
	    incr = fmax2(1, gnm_floor(incr/100));
	} while(oldincr > 1 && incr > n*GNM_const(1e-15));
	return y;
    }
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dnbinom.c from R.  */
/*
 *  AUTHOR
 *    Catherine Loader, catherine@research.bell-labs.com.
 *    October 23, 2000 and Feb, 2001.
 *
 *    dnbinom_mu(): Martin Maechler, June 2008
 *
 *  Merge in to R:
 *	Copyright (C) 2000--2016, The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *
 * DESCRIPTION
 *
 *   Computes the negative binomial distribution. For integer n,
 *   this is probability of x failures before the nth success in a
 *   sequence of Bernoulli trials. We do not enforce integer n, since
 *   the distribution is well defined for non-integers,
 *   and this can be useful for e.g. overdispersed discrete survival times.
 */


gnm_float dnbinom(gnm_float x, gnm_float size, gnm_float prob, gboolean give_log)
{
    gnm_float ans, p;

#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(size) || gnm_isnan(prob))
        return x + size + prob;
#endif

    if (prob <= 0 || prob > 1 || size < 0) ML_WARN_return_NAN;
    R_D_nonint_check(x);
    if (x < 0 || !gnm_finite(x)) return R_D__0;
    /* limiting case as size approaches zero is point mass at zero */
    if (x == 0 && size==0) return R_D__1;
    x = gnm_round(x);
    if(!gnm_finite(size)) size = GNM_MAX;

    ans = dbinom_raw(size, x+size, prob, 1-prob, give_log);
    p = ((gnm_float)size)/(size+x);
    return((give_log) ? gnm_log(p) + ans : p * ans);
}

/* Definition of function dnbinom_mu removed.  */

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pnbinom.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000-2016 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *	The distribution function of the negative binomial distribution.
 *
 *  NOTES
 *
 *	x = the number of failures before the n-th success
 */


gnm_float pnbinom(gnm_float x, gnm_float size, gnm_float prob, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(size) || gnm_isnan(prob))
	return x + size + prob;
    if(!gnm_finite(size) || !gnm_finite(prob))	ML_WARN_return_NAN;
#endif
    if (size < 0 || prob <= 0 || prob > 1)	ML_WARN_return_NAN;

    /* limiting case: point mass at zero */
    if (size == 0)
        return (x >= 0) ? R_DT_1 : R_DT_0;

    if (x < 0) return R_DT_0;
    if (!gnm_finite(x)) return R_DT_1;
    x = gnm_fake_floor(x);
    return pbeta(prob, size, x + 1, lower_tail, log_p);
}

/* Definition of function pnbinom_mu removed.  */

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qnbinom.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000-2016 The R Core Team
 *  Copyright (C) 2005-2016 The R Foundation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  SYNOPSIS
 *
 *	#include <Rmath.h>
 *	double qnbinom(double p, double size, double prob,
 *                     int lower_tail, int log_p)
 *
 *  DESCRIPTION
 *
 *	The quantile function of the negative binomial distribution.
 *
 *  NOTES
 *
 *	x = the number of failures before the n-th success
 *
 *  METHOD
 *
 *	Uses the Cornish-Fisher Expansion to include a skewness
 *	correction to a normal approximation.  This gives an
 *	initial value which never seems to be off by more than
 *	1 or 2.	 A search is then conducted of values close to
 *	this initial start point.
 */


static gnm_float
qbinom_do_search(gnm_float y, gnm_float *z, gnm_float p, gnm_float n, gnm_float pr, gnm_float incr)
{
    if(*z >= p) {	/* search to the left */
	for(;;) {
	    if(y == 0 ||
	       (*z = pnbinom(y - incr, n, pr, /*l._t.*/TRUE, /*log_p*/FALSE)) < p)
		return y;
	    y = fmax2(0, y - incr);
	}
    }
    else {		/* search to the right */
	for(;;) {
	    y = y + incr;
	    if((*z = pnbinom(y, n, pr, /*l._t.*/TRUE, /*log_p*/FALSE)) >= p)
		return y;
	}
    }
}


gnm_float qnbinom(gnm_float p, gnm_float size, gnm_float prob, gboolean lower_tail, gboolean log_p)
{
    gnm_float P, Q, mu, sigma, gamma, z, y;

#ifdef IEEE_754
    if (gnm_isnan(p) || gnm_isnan(size) || gnm_isnan(prob))
	return p + size + prob;
#endif

    /* this happens if specified via mu, size, since
       prob == size/(size+mu)
    */
    if (prob == 0 && size == 0) return 0;

    if (prob <= 0 || prob > 1 || size < 0) ML_WARN_return_NAN;

    if (prob == 1 || size == 0) return 0;

    R_Q_P01_boundaries(p, 0, gnm_pinf);

    Q = GNM_const(1.0) / prob;
    P = (GNM_const(1.0) - prob) * Q;
    mu = size * P;
    sigma = gnm_sqrt(size * P * Q);
    gamma = (Q + P)/sigma;

    /* Note : "same" code in qpois.c, qbinom.c, qnbinom.c --
     * FIXME: This is far from optimal [cancellation for p ~= 1, etc]: */
    if(!lower_tail || log_p) {
	p = R_DT_qIv(p); /* need check again (cancellation!): */
	if (p == R_DT_0) return 0;
	if (p == R_DT_1) return gnm_pinf;
    }
    /* temporary hack --- FIXME --- */
    if (p + GNM_const(1.01)*GNM_EPSILON >= 1) return gnm_pinf;

    /* y := approx.value (Cornish-Fisher expansion) :  */
    z = qnorm(p, GNM_const(0.), GNM_const(1.), /*lower_tail*/TRUE, /*log_p*/FALSE);
    y = gnm_round(mu + sigma * (z + gamma * (z*z - 1) / 6));

    z = pnbinom(y, size, prob, /*lower_tail*/TRUE, /*log_p*/FALSE);

    /* fuzz to ensure left continuity: */
    p *= 1 - 64*GNM_EPSILON;

    /* If the C-F value is not too large a simple search is OK */
    if(y < GNM_const(1e5)) return qbinom_do_search(y, &z, p, size, prob, 1);
    /* Otherwise be a bit cleverer in the search */
    {
	gnm_float incr = gnm_floor(y * GNM_const(0.001)), oldincr;
	do {
	    oldincr = incr;
	    y = qbinom_do_search(y, &z, p, size, prob, incr);
	    incr = fmax2(1, gnm_floor(incr/100));
	} while(oldincr > 1 && incr > y*GNM_const(1e-15));
	return y;
    }
}

/* Definition of function qnbinom_mu removed.  */

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dbeta.c from R.  */
/*
 *  AUTHOR
 *    Catherine Loader, catherine@research.bell-labs.com.
 *    October 23, 2000.
 *
 *  Merge in to R:
 *	Copyright (C) 2000, The R Core Team
 *  Changes to case a, b < 2, use logs to avoid underflow
 *	Copyright (C) 2006-2014 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *
 *  DESCRIPTION
 *    Beta density,
 *                   (a+b-1)!     a-1       b-1
 *      p(x;a,b) = ------------ x     (1-x)
 *                 (a-1)!(b-1)!
 *
 *               = (a+b-1) dbinom(a-1; a+b-2,x)
 *
 *    The basic formula for the log density is thus
 *    (a-1) log x + (b-1) log (1-x) - lbeta(a, b)
 *    If either a or b <= 2 then 0 < lbeta(a, b) < 710 and so no
 *    term is large.  We use Loader's code only if both a and b > 2.
 */


gnm_float dbeta(gnm_float x, gnm_float a, gnm_float b, gboolean give_log)
{
#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (gnm_isnan(x) || gnm_isnan(a) || gnm_isnan(b)) return x + a + b;
#endif

    if (a < 0 || b < 0) ML_WARN_return_NAN;
    if (x < 0 || x > 1) return(R_D__0);

    // limit cases for (a,b), leading to point masses
    if(a == 0 || b == 0 || !gnm_finite(a) || !gnm_finite(b)) {
	if(a == 0 && b == 0) { // point mass 1/2 at each of {0,1} :
	    if (x == 0 || x == 1) return(gnm_pinf); else return(R_D__0);
	}
	if (a == 0 || a/b == 0) { // point mass 1 at 0
	    if (x == 0) return(gnm_pinf); else return(R_D__0);
	}
	if (b == 0 || b/a == 0) { // point mass 1 at 1
	    if (x == 1) return(gnm_pinf); else return(R_D__0);
	}
	// else, remaining case:  a = b = Inf : point mass 1 at 1/2
	if (x == GNM_const(0.5)) return(gnm_pinf); else return(R_D__0);
    }

    if (x == 0) {
	if(a > 1) return(R_D__0);
	if(a < 1) return(gnm_pinf);
	/* a == 1 : */ return(R_D_val(b));
    }
    if (x == 1) {
	if(b > 1) return(R_D__0);
	if(b < 1) return(gnm_pinf);
	/* b == 1 : */ return(R_D_val(a));
    }

    gnm_float lval;
    if (a <= 2 || b <= 2)
	lval = (a-1)*gnm_log(x) + (b-1)*gnm_log1p(-x) - gnm_lbeta(a, b);
    else
	lval = gnm_log(a+b-1) + dbinom_raw(a-1, a+b-2, x, 1-x, TRUE);

    return R_D_exp(lval);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dhyper.c from R.  */
/*
 *  AUTHOR
 *    Catherine Loader, catherine@research.bell-labs.com.
 *    October 23, 2000.
 *
 *  Merge in to R:
 *	Copyright (C) 2000-2014 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *
 * DESCRIPTION
 *
 *    Given a sequence of r successes and b failures, we sample n (\le b+r)
 *    items without replacement. The hypergeometric probability is the
 *    probability of x successes:
 *
 *		       choose(r, x) * choose(b, n-x)
 *	p(x; r,b,n) =  -----------------------------  =
 *			       choose(r+b, n)
 *
 *		      dbinom(x,r,p) * dbinom(n-x,b,p)
 *		    = --------------------------------
 *			       dbinom(n,r+b,p)
 *
 *    for any p. For numerical stability, we take p=n/(r+b); with this choice,
 *    the denominator is not exponentially small.
 */


gnm_float dhyper(gnm_float x, gnm_float r, gnm_float b, gnm_float n, gboolean give_log)
{
    gnm_float p, q, p1, p2, p3;

#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(r) || gnm_isnan(b) || gnm_isnan(n))
	return x + r + b + n;
#endif

    if (R_D_negInonint(r) || R_D_negInonint(b) || R_D_negInonint(n) || n > r+b)
	ML_WARN_return_NAN;
    if(x < 0) return(R_D__0);
    R_D_nonint_check(x);// incl warning

    x = gnm_round(x);
    r = gnm_round(r);
    b = gnm_round(b);
    n = gnm_round(n);

    if (n < x || r < x || n - x > b) return(R_D__0);
    if (n == 0) return((x == 0) ? R_D__1 : R_D__0);

    p = ((gnm_float)n)/((gnm_float)(r+b));
    q = ((gnm_float)(r+b-n))/((gnm_float)(r+b));

    p1 = dbinom_raw(x,	r, p,q,give_log);
    p2 = dbinom_raw(n-x,b, p,q,give_log);
    p3 = dbinom_raw(n,r+b, p,q,give_log);

    return( (give_log) ? p1 + p2 - p3 : p1*p2/p3 );
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/phyper.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 1999-2014  The R Core Team
 *  Copyright (C) 2004	     Morten Welinder
 *  Copyright (C) 2004	     The R Foundation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *	The distribution function of the hypergeometric distribution.
 *
 * Current implementation based on posting
 * From: Morten Welinder <terra@gnome.org>
 * Cc: R-bugs@biostat.ku.dk
 * Subject: [Rd] phyper accuracy and efficiency (PR#6772)
 * Date: Thu, 15 Apr 2004 18:06:37 +0200 (CEST)
 ......

 The current version has very serious cancellation issues.  For example,
 if you ask for a small right-tail you are likely to get total cancellation.
 For example,  phyper(59, 150, 150, 60, FALSE, FALSE) gives 6.372680161e-14.
 The right answer is dhyper(0, 150, 150, 60, FALSE) which is 5.111204798e-22.

 phyper is also really slow for large arguments.

 Therefore, I suggest using the code below. This is a sniplet from Gnumeric ...
 The code isn't perfect.  In fact, if  x*(NR+NB)  is close to	n*NR,
 then this code can take a while. Not longer than the old code, though.

 -- Thanks to Ian Smith for ideas.
*/


static gnm_float pdhyper (gnm_float x, gnm_float NR, gnm_float NB, gnm_float n, gboolean log_p)
{
/*
 * Calculate
 *
 *	    phyper (x, NR, NB, n, TRUE, FALSE)
 *   [log]  ----------------------------------
 *	       dhyper (x, NR, NB, n, FALSE)
 *
 * without actually calling phyper.  This assumes that
 *
 *     x * (NR + NB) <= n * NR
 *
 */
    gnm_float sum = 0;
    gnm_float term = 1;

    while (x > 0 && term >= GNM_EPSILON * sum) {
	term *= x * (NB - n + x) / (n + 1 - x) / (NR + 1 - x);
	sum += term;
	x--;
    }

    gnm_float ss = (gnm_float) sum;
    return log_p ? gnm_log1p(ss) : 1 + ss;
}


/* FIXME: The old phyper() code was basically used in ./qhyper.c as well
 * -----  We need to sync this again!
*/
gnm_float phyper (gnm_float x, gnm_float NR, gnm_float NB, gnm_float n,
	       gboolean lower_tail, gboolean log_p)
{
/* Sample of  n balls from  NR red  and	 NB black ones;	 x are red */

    gnm_float d, pd;

#ifdef IEEE_754
    if(gnm_isnan(x) || gnm_isnan(NR) || gnm_isnan(NB) || gnm_isnan(n))
	return x + NR + NB + n;
#endif

    x = gnm_fake_floor(x);
    NR = gnm_round(NR);
    NB = gnm_round(NB);
    n  = gnm_round(n);

    if (NR < 0 || NB < 0 || !gnm_finite(NR + NB) || n < 0 || n > NR + NB)
	ML_WARN_return_NAN;

    if (x * (NR + NB) > n * NR) {
	/* Swap tails.	*/
	gnm_float oldNB = NB;
	NB = NR;
	NR = oldNB;
	x = n - x - 1;
	lower_tail = !lower_tail;
    }

    if (x < 0)
	return R_DT_0;
    if (x >= NR || x >= n)
	return R_DT_1;

    d  = dhyper (x, NR, NB, n, log_p);
    pd = pdhyper(x, NR, NB, n, log_p);

    return log_p ? R_DT_Log(d + pd) : R_D_Lval(d * pd);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dexp.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *	The density of the exponential distribution.
 */


gnm_float dexp(gnm_float x, gnm_float scale, gboolean give_log)
{
#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (gnm_isnan(x) || gnm_isnan(scale)) return x + scale;
#endif
    if (scale <= 0) ML_WARN_return_NAN;

    if (x < 0)
	return R_D__0;
    return (give_log ?
	    (-x / scale) - gnm_log(scale) :
	    gnm_exp(-x / scale) / scale);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pexp.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000-2015 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *	The distribution function of the exponential distribution.
 */

gnm_float pexp(gnm_float x, gnm_float scale, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(scale))
	return x + scale;
    if (scale < 0) ML_WARN_return_NAN;
#else
    if (scale <= 0) ML_WARN_return_NAN;
#endif

    if (x <= 0)
	return R_DT_0;
    /* same as weibull( shape = 1): */
    x = -(x / scale);
    return lower_tail
	? (log_p ? swap_log_tail(x) : -gnm_expm1(x))
	: R_D_exp(x);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dgeom.c from R.  */
/*
 *  AUTHOR
 *    Catherine Loader, catherine@research.bell-labs.com.
 *    October 23, 2000.
 *
 *  Merge in to R:
 *	Copyright (C) 2000-2014 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *
 *  DESCRIPTION
 *
 *    Computes the geometric probabilities, Pr(X=x) = p(1-p)^x.
 */


gnm_float dgeom(gnm_float x, gnm_float p, gboolean give_log)
{
    gnm_float prob;

#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(p)) return x + p;
#endif

    if (p <= 0 || p > 1) ML_WARN_return_NAN;

    R_D_nonint_check(x);
    if (x < 0 || !gnm_finite(x) || p == 0) return R_D__0;
    x = gnm_round(x);

    /* prob = (1-p)^x, stable for small p */
    prob = dbinom_raw(GNM_const(0.),x, p,1-p, give_log);

    return((give_log) ? gnm_log(p) + prob : p*prob);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pgeom.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000-2006 The R Core Team
 *  Copyright (C) 2004	    The R Foundation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *    The distribution function of the geometric distribution.
 */


gnm_float pgeom(gnm_float x, gnm_float p, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(p))
	return x + p;
#endif
    if(p <= 0 || p > 1) ML_WARN_return_NAN;

    if (x < 0) return R_DT_0;
    if (!gnm_finite(x)) return R_DT_1;
    x = gnm_fake_floor(x);

    if(p == 1) { /* we cannot assume IEEE */
	x = lower_tail ? 1: 0;
	return log_p ? gnm_log(x) : x;
    }
    x = gnm_log1p(-p) * (x + 1);
    if (log_p)
	return R_DT_Clog(x);
    else
	return lower_tail ? -gnm_expm1(x) : gnm_exp(x);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dcauchy.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *    The density of the Cauchy distribution.
 */


gnm_float dcauchy(gnm_float x, gnm_float location, gnm_float scale, gboolean give_log)
{
    gnm_float y;
#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (gnm_isnan(x) || gnm_isnan(location) || gnm_isnan(scale))
	return x + location + scale;
#endif
    if (scale <= 0) ML_WARN_return_NAN;

    y = (x - location) / scale;
    return give_log ?
	- gnm_log(M_PIgnum * scale * (GNM_const(1.) + y * y)) :
	GNM_const(1.) / (M_PIgnum * scale * (GNM_const(1.) + y * y));
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pcauchy.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000-2014 The R Core Team
 *  Copyright (C) 2004 The R Foundation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *	The distribution function of the Cauchy distribution.
 */


#if 1 // HAVE_ATANPI
gnm_float gnm_atanpi(gnm_float);
#endif


gnm_float pcauchy(gnm_float x, gnm_float location, gnm_float scale,
	       gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(location) || gnm_isnan(scale))
	return x + location + scale;
#endif
    if (scale <= 0) ML_WARN_return_NAN;

    x = (x - location) / scale;
    if (gnm_isnan(x)) ML_WARN_return_NAN;
#ifdef IEEE_754
    if(!gnm_finite(x)) {
	if(x < 0) return R_DT_0;
	else return R_DT_1;
    }
#endif
    if (!lower_tail)
	x = -x;
    /* for large x, the standard formula suffers from cancellation.
     * This is from Morten Welinder thanks to  Ian Smith's  atan(1/x) : */
#if 1 // HAVE_ATANPI
    if (gnm_abs(x) > 1) {
	gnm_float y = gnm_atanpi(1/x);
	return (x > 0) ? R_D_Clog(y) : R_D_val(-y);
    } else
	return R_D_val(GNM_const(0.5) + gnm_atanpi(x));
#else
    if (gnm_abs(x) > 1) {
	gnm_float y = atan(1/x) / M_PIgnum;
	return (x > 0) ? R_D_Clog(y) : R_D_val(-y);
    } else
	return R_D_val(GNM_const(0.5) + atan(x) / M_PIgnum);
#endif
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/df.c from R.  */
/*
 *  AUTHOR
 *    Catherine Loader, catherine@research.bell-labs.com.
 *    October 23, 2000.
 *
 *  Merge in to R:
 *	Copyright (C) 2000, 2005 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *
 *  DESCRIPTION
 *
 *    The density function of the F distribution.
 *    To evaluate it, write it as a Binomial probability with p = x*m/(n+x*m).
 *    For m >= 2, we use the simplest conversion.
 *    For m < 2, (m-2)/2 < 0 so the conversion will not work, and we must use
 *               a second conversion.
 *    Note the division by p; this seems unavoidable
 *    for m < 2, since the F density has a singularity as x (or p) -> 0.
 */


gnm_float df(gnm_float x, gnm_float m, gnm_float n, gboolean give_log)
{
    gnm_float p, q, f, dens;

#ifdef IEEE_754
    if (gnm_isnan(x) || gnm_isnan(m) || gnm_isnan(n))
	return x + m + n;
#endif
    if (m <= 0 || n <= 0) ML_WARN_return_NAN;
    if (x < 0)  return(R_D__0);
    if (x == 0) return(m > 2 ? R_D__0 : (m == 2 ? R_D__1 : gnm_pinf));
    if (!gnm_finite(m) && !gnm_finite(n)) { /* both +Inf */
	if(x == 1) return gnm_pinf; else return R_D__0;
    }
    if (!gnm_finite(n)) /* must be +Inf by now */
	return(dgamma(x, m/2, GNM_const(2.)/m, give_log));
    if (m > GNM_const(1e14)) {/* includes +Inf: code below is inaccurate there */
	dens = dgamma(GNM_const(1.)/x, n/2, GNM_const(2.)/n, give_log);
	return give_log ? dens - 2*gnm_log(x): dens/(x*x);
    }

    f = GNM_const(1.)/(n+x*m);
    q = n*f;
    p = x*m*f;

    if (m >= 2) {
	f = m*q/2;
	dens = dbinom_raw((m-2)/2, (m+n-2)/2, p, q, give_log);
    }
    else {
	f = m*m*q / (2*p*(m+n));
	dens = dbinom_raw(m/2, (m+n)/2, p, q, give_log);
    }
    return(give_log ? gnm_log(f)+dens : f*dens);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dchisq.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *    The density of the chi-squared distribution.
 */


gnm_float dchisq(gnm_float x, gnm_float df, gboolean give_log)
{
    return dgamma(x, df / GNM_const(2.), GNM_const(2.), give_log);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qweibull.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Core Team
 *  Copyright (C) 2005 The R Foundation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *    The quantile function of the Weibull distribution.
 */


gnm_float qweibull(gnm_float p, gnm_float shape, gnm_float scale, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (gnm_isnan(p) || gnm_isnan(shape) || gnm_isnan(scale))
	return p + shape + scale;
#endif
    if (shape <= 0 || scale <= 0) ML_WARN_return_NAN;

    R_Q_P01_boundaries(p, 0, gnm_pinf);

    return scale * gnm_pow(- R_DT_Clog(p), GNM_const(1.)/shape) ;
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qexp.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *    The quantile function of the exponential distribution.
 *
 */


gnm_float qexp(gnm_float p, gnm_float scale, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (gnm_isnan(p) || gnm_isnan(scale))
	return p + scale;
#endif
    if (scale < 0) ML_WARN_return_NAN;

    R_Q_P01_check(p);
    if (p == R_DT_0)
	return 0;

    return - scale * R_DT_Clog(p);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qgeom.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998	     Ross Ihaka
 *  Copyright (C) 2000--2016 The R Core Team
 *  Copyright (C) 2004--2016 The R Foundation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  DESCRIPTION
 *
 *    The quantile function of the geometric distribution.
 */


gnm_float qgeom(gnm_float p, gnm_float prob, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (gnm_isnan(p) || gnm_isnan(prob))
	return p + prob;
#endif
    if (prob <= 0 || prob > 1) ML_WARN_return_NAN;

    R_Q_P01_check(p);
    if (prob == 1) return(0);
    R_Q_P01_boundaries(p, 0, gnm_pinf);

/* add a fuzz to ensure left continuity, but value must be >= 0 */
    return fmax2(0, gnm_ceil(R_DT_Clog(p) / gnm_log1p(- prob) - 1 - GNM_const(1e-12)));
}

/* ------------------------------------------------------------------------ */
/* --- END MAGIC R SOURCE MARKER --- */

/* ------------------------------------------------------------------------ */
/*
 * Based on code imported from R by hand.  Heavily modified to enhance
 * accuracy.  See bug 700132.
 */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998       Ross Ihaka
 *  Copyright (C) 2000--2007 The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  http://www.r-project.org/Licenses/
 *
 *  SYNOPSIS
 *
 *    #include <Rmath.h>
 *    double ptukey(q, rr, cc, df, lower_tail, log_p);
 *
 *  DESCRIPTION
 *
 *    Computes the probability that the maximum of rr studentized
 *    ranges, each based on cc means and with df degrees of freedom
 *    for the standard error, is less than q.
 *
 *    The algorithm is based on that of the reference.
 *
 *  REFERENCE
 *
 *    Copenhaver, Margaret Diponzio & Holland, Burt S.
 *    Multiple comparisons of simple effects in
 *    the two-way analysis of variance with fixed effects.
 *    Journal of Statistical Computation and Simulation,
 *    Vol.30, pp.1-15, 1988.
 */

static gnm_float ptukey_wprob(gnm_float w, gnm_float rr, gnm_float cc)
{
/*  wprob() :

	This function calculates probability integral of Hartley's
	form of the range.

	w     = value of range
	rr    = no. of rows or groups
	cc    = no. of columns or treatments
	pr_w = returned probability integral

	program will not terminate if ir is raised.

	bb = upper limit of legendre integration
	nleg = order of legendre quadrature
	wlar = value of range above which wincr1 intervals are used to
	       calculate second part of integral,
	       else wincr2 intervals are used.
	or modifying a calculation.

	M_1_SQRT_2PI = 1 / sqrt(2 * pi);  from abramowitz & stegun, p. 3.
	M_SQRT2gnum = sqrt(2)
	xleg = legendre 12-point nodes
	aleg = legendre 12-point coefficients
 */

    static const gboolean debug = FALSE;
    static const gnm_float xleg[] = {
	GNM_const(0.981560634246719250690549090149),
	GNM_const(0.904117256370474856678465866119),
	GNM_const(0.769902674194304687036893833213),
	GNM_const(0.587317954286617447296702418941),
	GNM_const(0.367831498998180193752691536644),
	GNM_const(0.125233408511468915472441369464)
    };
    static const gnm_float aleg[G_N_ELEMENTS (xleg)] = {
	GNM_const(0.047175336386511827194615961485),
	GNM_const(0.106939325995318430960254718194),
	GNM_const(0.160078328543346226334652529543),
	GNM_const(0.203167426723065921749064455810),
	GNM_const(0.233492536538354808760849898925),
	GNM_const(0.249147045813402785000562436043)
    };
    const int nleg = G_N_ELEMENTS (xleg) * 2;
    gnm_float pr_w, binc, qsqz, blb;
    int i, jj;

    qsqz = w * GNM_const(0.5);

    /* find (2F(w/2) - 1) ^ cc */
    /* (first term in integral of hartley's form). */

    /*
     * Use different formulas for different size of qsqz to avoid
     * cancellation for pnorm or squeezing erf's result against 1.
     */
    pr_w = qsqz > 1
	    ? pow1p (-2 * pnorm (qsqz, 0, 1, FALSE, FALSE), cc)
	    : gnm_pow (gnm_erf (qsqz / M_SQRT2gnum), cc);
    if (pr_w >= 1)
	return 1.0;

    /* find the integral of second term of hartley's form */
    /* Limits of integration are (w/2, Inf).  Large cc means that */
    /* that we need smaller step size.  The formula for binc is */
    /* a heuristic.  */

    /* blb and bub are lower and upper limits of integration. */

    blb = qsqz;
    binc = 3 / gnm_log1p (cc);

    /* integrate over each interval */

    for (i = 1; TRUE; i++) {
	gnm_float C = blb + binc * GNM_const(0.5);
	gnm_float elsum = 0;

	/* legendre quadrature with order = nleg */

	for (jj = 0; jj < nleg; jj++) {
	    gnm_float xx, aa, v, rinsum;

	    if (nleg / 2 <= jj) {
		xx = xleg[nleg - 1 - jj];
		aa = aleg[nleg - 1 - jj];
	    } else {
		xx = -xleg[jj];
		aa = aleg[jj];
	    }
	    v = C + binc * GNM_const(0.5) * xx;

	    rinsum = pnorm2 (v - w, v);
	    elsum += gnm_pow (rinsum, cc - 1) * aa * expmx2h(v);
	}
	elsum *= (binc * cc * M_1_SQRT_2PI);
	pr_w += elsum;

	if (pr_w >= 1) {
		/* Nothing more will contribute.  */
		pr_w = 1;
		break;
	}

	if (elsum <= pr_w * (GNM_EPSILON / 2)) {
		/* This interval contributed nothing.  We're done.  */
		if (debug) {
			g_printerr ("End at i=%d  for w=%g  cc=%g  dP=%g  P=%g\n",
				    i, w, cc, elsum, pr_w);
		}
		break;
	}

	blb += binc;
    }

    if (0) g_printerr ("w=%g pr_w=%.20g\n", w, pr_w);

    return gnm_pow (pr_w, rr);
}

static gnm_float
ptukey_otsum (gnm_float u0, gnm_float u1, gnm_float f2, gnm_float f2lf,
	      gnm_float q, gnm_float rr, gnm_float cc)
{
	gboolean debug = FALSE;
	static const gnm_float xlegq[] = {
		GNM_const(0.989400934991649932596154173450),
		GNM_const(0.944575023073232576077988415535),
		GNM_const(0.865631202387831743880467897712),
		GNM_const(0.755404408355003033895101194847),
		GNM_const(0.617876244402643748446671764049),
		GNM_const(0.458016777657227386342419442984),
		GNM_const(0.281603550779258913230460501460),
		GNM_const(0.950125098376374401853193354250e-1)
	};
	static const gnm_float alegq[G_N_ELEMENTS (xlegq)] = {
		GNM_const(0.271524594117540948517805724560e-1),
		GNM_const(0.622535239386478928628438369944e-1),
		GNM_const(0.951585116824927848099251076022e-1),
		GNM_const(0.124628971255533872052476282192),
		GNM_const(0.149595988816576732081501730547),
		GNM_const(0.169156519395002538189312079030),
		GNM_const(0.182603415044923588866763667969),
		GNM_const(0.189450610455068496285396723208)
	};
	const int nlegq = G_N_ELEMENTS (xlegq) * 2;
	int jj;
	gnm_float C = GNM_const(0.5) * (u0 + u1);
	gnm_float L = u1 - u0;
	gnm_float otsum = 0;

	for (jj = 0; jj < nlegq; jj++) {
	    gnm_float xx, aa, u, t1, wprb;

	    if (nlegq / 2 <= jj) {
		xx = xlegq[nlegq - 1 - jj];
		aa = alegq[nlegq - 1 - jj];
	    } else {
		xx = -xlegq[jj];
		aa = alegq[jj];
	    }

	    u = xx * (GNM_const(0.5) * L) + C;

	    t1 = f2lf + (f2 - 1) * gnm_log(u) - u * f2;

	    wprb = ptukey_wprob(q * gnm_sqrt(u), rr, cc);
	    otsum += aa * (wprb * (GNM_const(0.5) * L) * gnm_exp(t1));
	}

	if (debug)
		g_printerr ("Integral over [%g;%g] was %g\n",
			    u0, u1, otsum);


	return otsum;
}


static gnm_float
R_ptukey(gnm_float q, gnm_float rr, gnm_float cc, gnm_float df,
	 gboolean lower_tail, gboolean log_p)
{
/*  function ptukey() [was qprob() ]:

	q = value of studentized range
	rr = no. of rows or groups
	cc = no. of columns or treatments
	df = degrees of freedom of error term
	ir[0] = error flag = 1 if wprob probability > 1
	ir[1] = error flag = 1 if qprob probability > 1

	qprob = returned probability integral over [0, q]

	The program will not terminate if ir[0] or ir[1] are raised.

	All references in wprob to Abramowitz and Stegun
	are from the following reference:

	Abramowitz, Milton and Stegun, Irene A.
	Handbook of Mathematical Functions.
	New York:  Dover publications, Inc. (1970).

	All constants taken from this text are
	given to 25 significant digits.

	nlegq = order of legendre quadrature
	eps = max. allowable value of integral
	eps1 & eps2 = values below which there is
		      no contribution to integral.

	d.f. <= dhaf:	integral is divided into ulen1 length intervals.  else
	d.f. <= dquar:	integral is divided into ulen2 length intervals.  else
	d.f. <= deigh:	integral is divided into ulen3 length intervals.  else
	d.f. <= dlarg:	integral is divided into ulen4 length intervals.

	d.f. > dlarg:	the range is used to calculate integral.

	xlegq = legendre 16-point nodes
	alegq = legendre 16-point coefficients

	The coefficients and nodes for the legendre quadrature used in
	qprob and wprob were calculated using the algorithms found in:

	Stroud, A. H. and Secrest, D.
	Gaussian Quadrature Formulas.
	Englewood Cliffs,
	New Jersey:  Prentice-Hall, Inc, 1966.

	All values matched the tables (provided in same reference)
	to 30 significant digits.

	F(x) = .5 + erf(x / sqrt(2)) / 2      for x > 0

	F(x) = erfc( -x / sqrt(2)) / 2	      for x < 0

	where F(x) is standard normal c. d. f.

	if degrees of freedom large, approximate integral
	with range distribution.
 */

    static const gnm_float dhaf  = 100.0;
    static const gnm_float dquar = 800.0;
    static const gnm_float deigh = 5000.0;
    static const gnm_float dlarg = 25000.0;
    static const gnm_float ulen1 = 1.0;
    static const gnm_float ulen2 = 0.5;
    static const gnm_float ulen3 = 0.25;
    static const gnm_float ulen4 = 0.125;
    gnm_float ans, f2, f2lf, ulen, u0, u1;
    int i;
    gboolean fail = FALSE;
    gboolean debug = TRUE;

#ifdef IEEE_754
    if (gnm_isnan(q) || gnm_isnan(rr) || gnm_isnan(cc) || gnm_isnan(df))
	ML_ERR_return_NAN;
#endif

    if (q <= 0)
	return R_DT_0;

    /* FIXME: Special case for cc==2&&rr=1: we have explicit formula  */


    /* df must be > 1 */
    /* there must be at least two values */

    if (df < 2 || rr < 1 || cc < 2) ML_ERR_return_NAN;

    if(!gnm_finite(q))
	return R_DT_1;

    if (df > dlarg)
	return R_DT_val(ptukey_wprob(q, rr, cc));

    /* calculate leading constant */

    f2 = df * GNM_const(0.5);
    /* gnm_lgamma(u) = log(gamma(u)) */
    f2lf = f2 * gnm_log(f2) - gnm_lgamma(f2);

    /* integral is divided into unit, half-unit, quarter-unit, or */
    /* eighth-unit length intervals depending on the value of the */
    /* degrees of freedom. */

    if	    (df <= dhaf)	ulen = ulen1;
    else if (df <= dquar)	ulen = ulen2;
    else if (df <= deigh)	ulen = ulen3;
    else			ulen = ulen4;

    /* integrate over each subinterval */
    ans = 0.0;

    /*
     * Integration for [0;ulen/2].
     *
     * We use a sequence of intervals each covering an ever increasing
     * fraction of what is left.  Breaking the interval takes care of
     * some very un-polynomial behaviour of the integrand:
     * (1) Infinite derivative at 0 for low df.
     * (2) Infinite roots (due to underflow) in the left part of an
     *     interval with meaningful contributions from its right part
     */
    u1 = ulen / 2;
    for (i = 1; TRUE; i++) {
	    gnm_float otsum;

	    u0 = u1 / (i + 1);
	    otsum = ptukey_otsum (u0, u1, f2, f2lf, q, rr, cc);

	    ans += otsum;
	    if (otsum <= ans * (GNM_EPSILON / 2)) {
		    /* This interval contributed nothing.  We're done.  */
		    break;
	    }

	    if (i == 20) {
		    if (debug)
			    g_printerr ("PTUKEY FAIL LEFT: %d q=%g cc=%g df=%g otsum=%g ans=%g\n",
					i, q, cc, df, otsum, ans);
		    fail = TRUE;
		    break;
	    }

	    u1 = u0;
    }

    /*
     * Integration for [ulen/2;Infinity].
     *
     * We use a sequence of intervals starting with length ulen, but
     * when we think we're in the tail we ramp up the length.
     */
    u0 = ulen / 2;
    for (i = 1; TRUE; i++) {
	    gnm_float otsum;

	    u1 = u0 + ulen;
	    otsum = ptukey_otsum (u0, u1, f2, f2lf, q, rr, cc);

	    ans += otsum;
	    if (otsum < ans * GNM_EPSILON) {
		    /*
		     * This interval contributed nothing.  This can either be
		     * because the integrand is so flat that we haven't seen
		     * anyting yet or because we're done.  Or both.
		     *
		     * The maximum of the integrand falls not far from
		     *     u=(f-2)/f,
		     * but let's go a little further.
		     */
		    if (ans > 0 || u0 > 2) {
			    break;
		    }
	    }

	    if (i == 150) {
		    if (debug)
			    g_printerr ("PTUKEY FAIL RIGHT: %i %g %g\n",
					i, otsum, ans);
		    fail = TRUE;
		    break;
	    }

	    /*
	     * When we see a contribution that added less than 0.1%
	     * to the result, start ramping up the interval size.
	     * Note that this will not trigger unless ans>0.
	     */
	    if (otsum < ans / 1000)
		    ulen *= 2;

	    u0 = u1;
    }

    if (fail)
	    ML_ERROR(ME_PRECISION);
    ans = MIN (ans, 1);
    return R_DT_val(ans);
}

/* Silly order-of-arguments wrapper.  */
gnm_float
ptukey(gnm_float x, gnm_float nmeans, gnm_float df, gnm_float nranges, gboolean lower_tail, gboolean log_p)
{
	return R_ptukey (x, nranges, nmeans, df, lower_tail, log_p);
}

static gnm_float
ptukey1 (gnm_float x, const gnm_float shape[],
	 gboolean lower_tail, gboolean log_p)
{
	return ptukey (x, shape[0], shape[1], shape[2], lower_tail, log_p);
}

gnm_float
qtukey (gnm_float p, gnm_float nmeans, gnm_float df, gnm_float nranges, gboolean lower_tail, gboolean log_p)
{
	gnm_float x0, shape[3];

	if (!log_p && p > GNM_const(0.9)) {
		/* We're far into the tail.  Flip.  */
		p = 1 - p;
		lower_tail = !lower_tail;
	}

	/* This is accurate for nmeans==2 and nranges==1.   */
	x0 = M_SQRT2gnum * qt ((1 + p) / 2, df, lower_tail, log_p);

	shape[0] = nmeans;
	shape[1] = df;
	shape[2] = nranges;

	return pfuncinverter (p, shape, lower_tail, log_p, 0, gnm_pinf, x0, ptukey1, NULL);
}

static gnm_float
logspace_signed_add (gnm_float logx, gnm_float logabsy, gboolean ypos)
{
	return ypos
		? logspace_add (logx, logabsy)
		: logspace_sub (logx, logabsy);
}

/* Accurate  gnm_log (1 - gnm_exp (p))  for  p <= 0.  */
gnm_float
swap_log_tail (gnm_float lp)
{
	if (lp > -1 / gnm_log (2))
		return gnm_log (-gnm_expm1 (lp));  /* Good formula for lp near zero.  */
	else
		return gnm_log1p (-gnm_exp (lp));  /* Good formula for small lp.  */
}


/* --- BEGIN IANDJMSMITH SOURCE MARKER --- */

/* Calculation of logfbit(x)-logfbit(1+x). y2 must be < 1.  */
static gnm_float
logfbitdif (gnm_float x)
{
	gnm_float y = 1 / (2 * x + 3);
	gnm_float y2 = y * y;
	return y2 * gnm_logcf (y2, 3, 2, GNM_const(1e-14));
}

/*
 * lfbc{1-7} from this Mathematica program:
 *
 * Table[Numerator[BernoulliB[2n]/(2n(2n - 1))], {n, 1, 22}]
 * Table[Denominator[BernoulliB[2n]/(2n(2n - 1))], {n, 1, 22}]
 */
static const gnm_float lfbc1 = GNM_const (1.0) / 12;
static const gnm_float lfbc2 = GNM_const (1.0) / 30;
static const gnm_float lfbc3 = GNM_const (1.0) / 105;
static const gnm_float lfbc4 = GNM_const (1.0) / 140;
static const gnm_float lfbc5 = GNM_const (1.0) / 99;
static const gnm_float lfbc6 = GNM_const (691.0) / 30030;
static const gnm_float lfbc7 = GNM_const (1.0) / 13;
/* lfbc{8,9} to make logfbit(6) and logfbit(7) exact.  */
static const gnm_float lfbc8 = GNM_const (3.5068606896459316479e-01);
static const gnm_float lfbc9 = GNM_const (1.6769998201671114808);

/* This is also stirlerr(x+1).  */
static gnm_float
logfbit (gnm_float x)
{
	/*
	 * Error part of Stirling's formula where
	 *   log(x!) = log(sqrt(twopi))+(x+0.5)*log(x+1)-(x+1)+logfbit(x).
	 *
	 * Are we ever concerned about the relative error involved in this
	 * function? I don't think so.
	 */
	if (x >= GNM_const(1e10)) return 1 / (12 * (x + 1));
	else if (x >= 6) {
		gnm_float x1 = x + 1;
		gnm_float x2 = 1 / (x1 * x1);
		gnm_float x3 =
			x2 * (lfbc2 - x2 *
			      (lfbc3 - x2 *
			       (lfbc4 - x2 *
				(lfbc5 - x2 *
				 (lfbc6 - x2 *
				  (lfbc7 - x2 *
				   (lfbc8 - x2 *
				    lfbc9)))))));
		return lfbc1 * (1 - x3) / x1;
	}
	else if (x == 5) return GNM_const (0.13876128823070747998745727023762908562e-1);
	else if (x == 4) return GNM_const (0.16644691189821192163194865373593391145e-1);
	else if (x == 3) return GNM_const (0.20790672103765093111522771767848656333e-1);
	else if (x == 2) return GNM_const (0.27677925684998339148789292746244666596e-1);
	else if (x == 1) return GNM_const (0.41340695955409294093822081407117508025e-1);
	else if (x == 0) return GNM_const (0.81061466795327258219670263594382360138e-1);
	else if (x > -1) {
		gnm_float x1 = x;
		gnm_float x2 = 0;
		while (x1 < 6) {
			x2 += logfbitdif (x1);
			x1++;
		}
		return x2 + logfbit (x1);
	}
	else return gnm_pinf;
}

/* Calculation of logfbit1(x)-logfbit1(1+x).  */
static gnm_float
logfbit1dif (gnm_float x)
{
	return (logfbitdif (x) - 1 / (4 * (x + 1) * (x + 2))) / (x + GNM_const(1.5));
}

/* Derivative logfbit.  */
static gnm_float
logfbit1 (gnm_float x)
{
	if (x >= GNM_const(1e10)) return -lfbc1 * gnm_pow (x + 1, -2);
	else if (x >= 6) {
		gnm_float x1 = x + 1;
		gnm_float x2 = 1 / (x1 * x1);
		gnm_float x3 =
			x2 * (3 * lfbc2 - x2*
			      (5 * lfbc3 - x2 *
			       (7 * lfbc4 - x2 *
				(9 * lfbc5 - x2 *
				 (11 * lfbc6 - x2 *
				  (13 * lfbc7 - x2 *
				   (15 * lfbc8 - x2 *
				    17 * lfbc9)))))));
		return -lfbc1 * (1 - x3) * x2;
	}
	else if (x > -1) {
		gnm_float x1 = x;
		gnm_float x2 = 0;
		while (x1 < 6) {
			x2 += logfbit1dif (x1);
			x1++;
		}
		return x2 + logfbit1 (x1);
	}
	else return gnm_ninf;
}


/* Calculation of logfbit3(x)-logfbit3(1+x). */
static gnm_float
logfbit3dif (gnm_float x)
{
	return -(2 * x + 3) * gnm_pow ((x + 1) * (x + 2), -3);
}


/* Third derivative logfbit.  */
static gnm_float
logfbit3 (gnm_float x)
{
	if (x >= GNM_const(1e10)) return GNM_const(-0.5) * gnm_pow (x + 1, -4);
	else if (x >= 6) {
		gnm_float x1 = x + 1;
		gnm_float x2 = 1 / (x1 * x1);
		gnm_float x3 =
			x2 * (60 * lfbc2 - x2 *
			      (210 * lfbc3 - x2 *
			       (504 * lfbc4 - x2 *
				(990 * lfbc5 - x2 *
				 (1716 * lfbc6 - x2 *
				  (2730 * lfbc7 - x2 *
				   (4080 * lfbc8 - x2 *
				    5814 * lfbc9)))))));
		return -lfbc1 * (6 - x3) * x2 * x2;
	}
	else if (x > -1) {
		gnm_float x1 = x;
		gnm_float x2 = 0;
		while (x1 < 6) {
			x2 += logfbit3dif (x1);
			x1++;
		}
		return x2 + logfbit3 (x1);
	}
	else return gnm_ninf;
}

/* Calculation of logfbit5(x)-logfbit5(1+x).  */
static gnm_float
logfbit5dif (gnm_float x)
{
	return -6 * (2 * x + 3) * ((5 * x + 15) * x + 12) *
		gnm_pow ((x + 1) * (x + 2), -5);
}

/* Fifth derivative logfbit.  */
static gnm_float
logfbit5 (gnm_float x)
{
	if (x >= GNM_const(1e10)) return -10 * gnm_pow (x + 1, -6);
	else if (x >= 6) {
		gnm_float x1 = x + 1;
		gnm_float x2 = 1 / (x1 * x1);
		gnm_float x3 =
			x2 * (2520 * lfbc2 - x2 *
			      (15120 * lfbc3 - x2 *
			       (55440 * lfbc4 - x2 *
				(154440 * lfbc5 - x2 *
				 (360360 * lfbc6 - x2 *
				  (742560 * lfbc7 - x2 *
				   (1395360 * lfbc8 - x2 *
				    2441880 * lfbc9)))))));
		return -lfbc1 * (120 - x3) * x2 * x2 * x2;
	} else if (x > -1) {
		gnm_float x1 = x;
		gnm_float x2 = 0;
		while (x1 < 6) {
			x2 += logfbit5dif (x1);
			x1++;
		}
		return x2 + logfbit5 (x1);
	}
	else return gnm_ninf;
}

/* Calculation of logfbit7(x)-logfbit7(1+x).  */
static gnm_float
logfbit7dif (gnm_float x)
{
	return -120 *
		(2 * x + 3) *
		((((14 * x + 84) * x + 196) * x + 210) * x + 87) *
		gnm_pow ((x + 1) * (x + 2), -7);
}

/* Seventh derivative logfbit.  */
static gnm_float
logfbit7 (gnm_float x)
{
	if (x >= GNM_const(1e10)) return -420 * gnm_pow (x + 1, -8);
	else if (x >= 6) {
		gnm_float x1 = x + 1;
		gnm_float x2 = 1 / (x1 * x1);
		gnm_float x3 =
			x2 * (181440 * lfbc2 - x2 *
			      (1663200 * lfbc3 - x2 *
			       (8648640 * lfbc4 - x2 *
				(32432400 * lfbc5 - x2 *
				 (98017920 * lfbc6 - x2 *
				  (253955520 * lfbc7 - x2 *
				   (586051200 * lfbc8 - x2 *
				    1235591280 * lfbc9)))))));
		return -lfbc1 * (5040 - x3) * x2 * x2 * x2 * x2;
	} else if (x > -1) {
		gnm_float x1 = x;
		gnm_float x2 = 0;
		while (x1 < 6) {
			x2 += logfbit7dif (x1);
			x1++;
		}
		return x2 + logfbit7 (x1);
	}
	else return gnm_ninf;
}


static gnm_float
lfbaccdif (gnm_float a, gnm_float b)
{
	if (a > GNM_const(0.03) * (a + b))
		return logfbit (a + b) - logfbit (b);
	else {
		gnm_float a2 = a * a;
		gnm_float ab2 = a / 2 + b;
		return a * (logfbit1 (ab2) + a2 / 24 *
			    (logfbit3 (ab2) + a2 / 80 *
			     (logfbit5 (ab2) + a2 / 168 *
			      logfbit7 (ab2))));
	}
}

static gnm_float
compbfunc (gnm_float x, gnm_float a, gnm_float b)
{
	const gnm_float sumAcc = GNM_const(5E-16);
	gnm_float term = x;
	gnm_float d = 2;
	gnm_float sum = term / (a + 1);
	while (gnm_abs (term) > gnm_abs (sum * sumAcc)) {
		term *= (d - b) * x / d;
		sum += term / (a + d);
		d++;
	}
	return a * (b - 1) * sum;
}

static gnm_float
pbeta_smalla (gnm_float x, gnm_float a, gnm_float b, gboolean lower_tail, gboolean log_p)
{
	gnm_float r;

#if 0
	assert (a >= 0 && b >= 0);
	assert (a < 1);
	assert (b < 1 || (1 + b) * x <= 1);
#endif

	if (x > GNM_const(0.5)) {
		gnm_float olda = a;
		a = b;
		b = olda;
		x = 1 - x;
		lower_tail = !lower_tail;
	}

	r = (a + b + GNM_const(0.5)) * log1pmx (a / (1 + b)) +
		a * (a - GNM_const(0.5)) / (1 + b) +
		lfbaccdif (a, b);
	r += a * gnm_log ((1 + b) * x) - lgamma1p (a);
	if (lower_tail) {
		if (log_p)
			return r + gnm_log1p (-compbfunc (x, a, b)) + gnm_log (b / (a + b));
		else
			return gnm_exp (r) * (1 - compbfunc (x, a, b)) * (b / (a + b));
	} else {
		/* x=0.500000001 a=0.5  b=0.000001 ends up here [swapped]
		 * with r=-7.94418987455065e-08 and cbf=-3.16694087508444e-07.
		 *
		 * x=0.0000001 a=0.999999 b=0.02 end up here with
		 * r=-16.098276918385 and cbf=-4.89999787339858e-08.
		 */
		if (log_p) {
			return swap_log_tail (r + gnm_log1p (-compbfunc (x, a, b)) + gnm_log (b / (a + b)));
		} else {
			r = -gnm_expm1 (r);
			r += compbfunc (x, a, b) * (1 - r);
			r += (a / (a + b)) * (1 - r);
			return r;
		}
	}
}

/* Cumulative Students t-distribution, with odd parameterisation for
 * use with binApprox.
 * p is x*x/(k+x*x)
 * q is 1-p
 * logqk2 is LN(q)*k/2
 * approxtdistDens returns with approx density function, for use in
 * binApprox
 */
static gnm_float
tdistexp (gnm_float p, gnm_float q, gnm_float logqk2, gnm_float k,
	  gboolean log_p, gnm_float *approxtdistDens)
{
	const gnm_float sumAcc = GNM_const(5E-16);
	const gnm_float cfVSmall = GNM_const(1.0e-14);
	const gnm_float lstpi = gnm_log (2 * M_PIgnum) / 2;

	if (gnm_floor (k / 2) * 2 == k) {
		gnm_float ldens = logqk2 + logfbit (k - 1) - 2 * logfbit (k * GNM_const(0.5) - 1) - lstpi;
		*approxtdistDens = R_D_exp (ldens);
	} else {
		gnm_float ldens = logqk2 + k * log1pmx (1 / k) + 2 * logfbit ((k - 1) * GNM_const(0.5)) - logfbit (k - 1) - lstpi;
		*approxtdistDens = R_D_exp (ldens);
	}

	if (k * p < 4 * q) {
		gnm_float sum = 0;
		gnm_float aki = k + 1;
		gnm_float ai = 3;
		gnm_float term = aki * p / ai;

		while (term > sumAcc * sum) {
			sum += term;
			ai += 2;
			aki += 2;
			term *= aki * p / ai;
		}
		sum += term;

		return log_p
			? logspace_sub (-M_LN2gnum, *approxtdistDens + gnm_log1p (sum) + gnm_log (k * p) / 2)
			: GNM_const(0.5) - *approxtdistDens * (sum + 1) * gnm_sqrt (k * p);
	} else {
		gnm_float q1 = 2 * (1 + q);
		gnm_float q8 = 8 * q;
		gnm_float a1 = 0;
		gnm_float b1 = 1;
		gnm_float c1 = -6 * q;
		gnm_float a2 = 1;
		gnm_float b2 = (k - 1) * p + 3;
		gnm_float cadd = -14 * q;
		gnm_float c2 = b2 + q1;

		while (gnm_abs (a2 * b1 - a1 * b2) > gnm_abs (cfVSmall * b1 * b2)) {
			a1 = c2 * a2 + c1 * a1;
			b1 = c2 * b2 + c1 * b1;
			c1 += cadd;
			cadd -= q8;
			c2 += q1;
			a2 = c2 * a1 + c1 * a2;
			b2 = c2 * b1 + c1 * b2;
			c1 += cadd;
			cadd -= q8;
			c2 += q1;

			if (gnm_abs (b2) > scalefactor) {
				a1 *= 1 / scalefactor;
				b1 *= 1 / scalefactor;
				a2 *= 1 / scalefactor;
				b2 *= 1 / scalefactor;
			} else if (gnm_abs (b2) < 1 / scalefactor) {
				a1 *= scalefactor;
				b1 *= scalefactor;
				a2 *= scalefactor;
				b2 *= scalefactor;
			}
		}

		return log_p
			? *approxtdistDens + gnm_log1p (-q * a2 / b2) - gnm_log (k * p) / 2
			: *approxtdistDens * (1 - q * a2 / b2) / gnm_sqrt (k * p);
	}
}


/* Asymptotic expansion to calculate the probability that binomial variate
 * has value <= a.
 * (diffFromMean = (a+b)*p-a).
 */
static gnm_float
binApprox (gnm_float a, gnm_float b, gnm_float diffFromMean,
           gboolean lower_tail, gboolean log_p)
{
	gnm_float pq1, res, t;
	gnm_float ib05, ib15, ib25, ib35, ib3;
	gnm_float elfb, coef15, coef25, coef35;
	gnm_float approxtdistDens;

	gnm_float n = a + b;
	gnm_float n1 = n + 1;
	gnm_float lvv = b - n * diffFromMean;
	gnm_float lval = (a * log1pmx (lvv / (a * n1)) +
			  b * log1pmx (-lvv / (b * n1))) / n;
	gnm_float tp = -gnm_expm1 (lval);
	gnm_float mfac = n1 * tp;
	gnm_float ib2 = 1 + mfac;
	gnm_float t1 = (n + 2) * tp;

	mfac = 2 * mfac;

	ib3 = ib2 + mfac*t1;
	ib05 = tdistexp (tp, 1 - tp, n1 * lval, 2 * n1, log_p, &approxtdistDens);

	ib15 = gnm_sqrt (mfac);
	mfac = t1 * (GNM_const (2.0) / 3);
	ib25 = 1 + mfac;
	ib35 = ib25 + mfac * (n + 3) * tp * (GNM_const (2.0) / 5);

	pq1 = (n * n) / (a * b);

	res = (ib2 * (1 + 2 * pq1) / 135 - 2 * ib3 * ((2 * pq1 - 43) * pq1 - 22) / (2835 * (n + 3))) / (n + 2);
	res = (GNM_const (1.0) / 3 - res) * 2 * gnm_sqrt (pq1 / n1) * (a - b) / n;
	if (lvv > 0) {
		res = -res;
		lower_tail = !lower_tail;
	}

	n1 = (n + GNM_const(1.5)) * (n + GNM_const(2.5));
	coef15 = (-17 + 2 * pq1) / (24 * (n + GNM_const(1.5)));
	coef25 = (-503 + 4 * pq1 * (19 + pq1)) / (1152 * n1);
	coef35 = (-315733 + pq1 * (53310 + pq1 * (8196 - 1112 * pq1))) /
		(414720 * n1 * (n + GNM_const(3.5)));
	elfb = (coef35 + coef25) + coef15;
	res += ib15 * ((coef35 * ib35 + coef25 * ib25) + coef15);

	t = log_p
		? logspace_signed_add (ib05,
				       gnm_log (gnm_abs (res)) + approxtdistDens - gnm_log1p (elfb),
				       res >= 0)
		: ib05 + res * approxtdistDens / (1 + elfb);

	return lower_tail
		? t
		: log_p ? swap_log_tail (t) : (1 - t);
}

/* Probability that binomial variate with sample size i+j and
 * event prob p (=1-q) has value i (diffFromMean = (i+j)*p-i)
 */
static gnm_float
binomialTerm (gnm_float i, gnm_float j, gnm_float p, gnm_float q,
	      gnm_float diffFromMean, gboolean log_p)
{
	const gnm_float minLog1Value = GNM_const(-0.79149064);
	gnm_float c1,c2,c3;
	gnm_float c4,c5,c6,ps,logbinomialTerm,dfm;
	gnm_float t;

	if (i == 0 && j <= 0)
		return R_D__1;

	if (i <= -1 || j < 0)
		return R_D__0;

	c1 = (i + 1) + j;
	if (p < q) {
		c2 = i;
		c3 = j;
		ps = p;
		dfm = diffFromMean;
	} else {
		c3 = i;
		c2 = j;
		ps = q;
		dfm = -diffFromMean;
	}

	c5 = (dfm - (1 - ps)) / (c2 + 1);
	c6 = -(dfm + ps) / (c3 + 1);

	if (c5 < minLog1Value) {
		if (c2 == 0) {
			logbinomialTerm = c3 * gnm_log1p (-ps);
			return log_p ? logbinomialTerm : gnm_exp (logbinomialTerm);
		} else if (ps == 0 && c2 > 0) {
			return R_D__0;
		} else {
			t = gnm_log ((ps * c1) / (c2 + 1)) - c5;
		}
	} else {
		t = log1pmx (c5);
	}

	c4 = logfbit (i + j) - logfbit (i) - logfbit (j);
	logbinomialTerm = c4 + c2 * t - c5 + (c3 * log1pmx (c6) - c6);

	return log_p
		? logbinomialTerm + GNM_const(0.5) * gnm_log (c1 / ((c2 + 1) * (c3 + 1) * 2 * M_PIgnum))
		: gnm_exp (logbinomialTerm) * gnm_sqrt (c1 / ((c2 + 1) * (c3 + 1) * 2 * M_PIgnum));
}


/*
 * Probability that binomial variate with sample size ii+jj
 * and event prob pp (=1-qq) has value <=i.
 * (diffFromMean = (ii+jj)*pp-ii).
 */
static gnm_float
binomialcf (gnm_float ii, gnm_float jj, gnm_float pp, gnm_float qq,
	    gnm_float diffFromMean, gboolean lower_tail, gboolean log_p)
{
	const gnm_float sumAlways = 0;
	const gnm_float sumFactor = 6;
	const gnm_float cfSmall = GNM_const(1.0e-15);

	gnm_float prob,p,q,a1,a2,b1,b2,c1,c2,c3,c4,n1,q1,dfm;
	gnm_float i,j,ni,nj,numb,ip1;
	gboolean swapped;

	ip1 = ii + 1;
	if (ii > -1 && (jj <= 0 || pp == 0)) {
		return R_DT_1;
	} else if (ii > -1 && ii < 0) {
		gnm_float f;
		ii = -ii;
		ip1 = ii;
		f = ii / ((ii + jj) * pp);
		prob = binomialTerm (ii, jj, pp, qq, (ii + jj) * pp - ii, log_p);
		prob = log_p ? prob + gnm_log (f) : prob * f;
		ii--;
		diffFromMean = (ii + jj) * pp - ii;
	} else
		prob = binomialTerm (ii, jj, pp, qq, diffFromMean, log_p);

	n1 = (ii + 3) + jj;
	if (ii < 0)
		swapped = FALSE;
	else if (pp > qq)
		swapped = (n1 * qq >= jj + 1);
	else
		swapped = (n1 * pp <= ii + 2);

	if (prob == R_D__0) {
		if (swapped == !lower_tail)
			return R_D__0;
		else
			return R_D__1;
	}

	if (swapped) {
		j = ip1;
		ip1 = jj;
		i = jj - 1;
		p = qq;
		q = pp;
		dfm = 1 - diffFromMean;
	} else {
		i = ii;
		j = jj;
		p = pp;
		q = qq;
		dfm = diffFromMean;
	}

	if (i > sumAlways) {
		numb = gnm_floor (sumFactor * gnm_sqrt (p + GNM_const(0.5)) * gnm_exp (gnm_log (n1 * p * q) / 3));
		numb = gnm_floor (numb - dfm);
		if (numb > i) numb = gnm_floor (i);
	} else
		numb = gnm_floor (i);
	if (numb < 0) numb = 0;

	a1 = 0;
	b1 = 1;
	q1 = q + 1;
	a2 = (i - numb) * q;
	b2 = dfm + numb + 1;
	c1 = 0;

	c2 = a2;
	c4 = b2;

	while (gnm_abs (a2 * b1 - a1 * b2) > gnm_abs (cfSmall * b1 * b2)) {
		c1++;
		c2 -= q;
		c3 = c1 * c2;
		c4 += q1;
		a1 = c4 * a2 + c3 * a1;
		b1 = c4 * b2 + c3 * b1;
		c1++;
		c2 -= q;
		c3 = c1 * c2;
		c4 += q1;
		a2 = c4 * a1 + c3 * a2;
		b2 = c4 * b1 + c3 * b2;

		if (gnm_abs (b2) > scalefactor) {
			a1 *= 1 / scalefactor;
			b1 *= 1 / scalefactor;
			a2 *= 1 / scalefactor;
			b2 *= 1 / scalefactor;
		} else if (gnm_abs (b2) < 1 / scalefactor) {
			a1 *= scalefactor;
			b1 *= scalefactor;
			a2 *= scalefactor;
			b2 *= scalefactor;
		}
	}

	a1 = a2 / b2;

	ni = (i - numb + 1) * q;
	nj = (j + numb) * p;
	while (numb > 0) {
		a1 = (1 + a1) * (ni / nj);
		ni = ni + q;
		nj = nj - p;
		numb--;
	}

	prob = log_p ? prob + gnm_log1p (a1) : prob * (1 + a1);

	if (swapped) {
		if (log_p)
			prob += gnm_log (ip1 * q / nj);
		else
			prob *= ip1 * q / nj;
	}

	if (swapped == !lower_tail)
		return prob;
	else
		return log_p ? swap_log_tail (prob) : 1 - prob;
}

static gnm_float
binomial (gnm_float ii, gnm_float jj, gnm_float pp, gnm_float qq,
          gnm_float diffFromMean, gboolean lower_tail, gboolean log_p)
{
	gnm_float mij = fmin2 (ii, jj);

	if (mij > 500 && gnm_abs (diffFromMean) < GNM_const(0.01) * mij)
		return binApprox (jj - 1, ii, diffFromMean, lower_tail, log_p);

	return binomialcf (ii, jj, pp, qq, diffFromMean, lower_tail, log_p);
}

gnm_float
pbeta (gnm_float x, gnm_float a, gnm_float b, gboolean lower_tail, gboolean log_p)
{
	gnm_float am1;

	if (gnm_isnan (x) || gnm_isnan (a) || gnm_isnan (b))
		return x + a + b;

	if (x <= 0) return R_DT_0;
	if (x >= 1) return R_DT_1;

	if (a < 1 && (b < 1 || (1 + b) * x <= 1))
		return pbeta_smalla (x, a, b, lower_tail, log_p);

	if (b < 1 && (1 + a) * (1 - x) <= 1)
		return pbeta_smalla (1 - x, b, a, !lower_tail, log_p);

	if (a < 1)
		return binomial (-a, b, x, 1 - x, 0, !lower_tail, log_p);

	if (b < 1)
		return binomial (-b, a, 1 - x, x, 0, lower_tail, log_p);

	am1 = a - 1;
	return binomial (am1, b, x, 1 - x, (am1 + b) * x - am1,
			 !lower_tail, log_p);
}

/* --- END IANDJMSMITH SOURCE MARKER --- */
/* ------------------------------------------------------------------------ */

gnm_float
pf (gnm_float x, gnm_float n1, gnm_float n2, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (gnm_isnan (x) || gnm_isnan (n1) || gnm_isnan (n2))
	return x + n2 + n1;
#endif
    if (n1 <= 0 || n2 <= 0) ML_ERR_return_NAN;

    if (x <= 0)
	return R_DT_0;

    /* Avoid squeezing pbeta's first parameter against 1.  */
    if (n1 * x > n2)
	    return pbeta (n2 / (n2 + n1 * x), n2 / 2, n1 / 2,
			  !lower_tail, log_p);
    else
	    return pbeta (n1 * x / (n2 + n1 * x), n1 / 2, n2 / 2,
			  lower_tail, log_p);
}

/* ------------------------------------------------------------------------ */

static gnm_float
ppois1 (gnm_float x, const gnm_float *plambda,
	gboolean lower_tail, gboolean log_p)
{
	return ppois (x, *plambda, lower_tail, log_p);
}

gnm_float
qpois (gnm_float p, gnm_float lambda, gboolean lower_tail, gboolean log_p)
{
	gnm_float mu, sigma, gamma, y, z;

	if (!(lambda >= 0))
		return gnm_nan;

	mu = lambda;
	sigma = gnm_sqrt (lambda);
	gamma = 1 / sigma;

	/* Cornish-Fisher expansion:  */
	z = qnorm (p, 0., 1., lower_tail, log_p);
	y = mu + sigma * (z + gamma * (z * z - 1) / 6);

	return discpfuncinverter (p, &lambda, lower_tail, log_p,
				  0, gnm_pinf, y,
				  ppois1);
}

/* ------------------------------------------------------------------------ */

static gnm_float
dgamma1 (gnm_float x, const gnm_float *palpha, gboolean log_p)
{
	return dgamma (x, *palpha, 1, log_p);
}

static gnm_float
pgamma1 (gnm_float x, const gnm_float *palpha,
	 gboolean lower_tail, gboolean log_p)
{
	return pgamma (x, *palpha, 1, lower_tail, log_p);
}


gnm_float
qgamma (gnm_float p, gnm_float alpha, gnm_float scale,
	gboolean lower_tail, gboolean log_p)
{
	gnm_float res1, x0, v;

#ifdef IEEE_754
	if (gnm_isnan(p) || gnm_isnan(alpha) || gnm_isnan(scale))
		return p + alpha + scale;
#endif
	R_Q_P01_check(p);
	if (alpha <= 0) ML_ERR_return_NAN;

	if (!log_p && p > GNM_const(0.9)) {
		/* We're far into the tail.  Flip.  */
		p = 1 - p;
		lower_tail = !lower_tail;
	}

	/* Make an initial guess, x0, assuming scale==1.  */
	v = 2 * alpha;
	if (v < GNM_const(-1.24) * R_DT_log (p))
		x0 = gnm_pow (R_DT_qIv (p) * alpha * gnm_exp (gnm_lgamma (alpha) + alpha * M_LN2gnum),
			      1 / alpha) / 2;
	else {
		gnm_float x1 = qnorm (p, 0, 1, lower_tail, log_p);
		gnm_float p1 = GNM_const(0.222222) / v;
		x0 = v * gnm_pow (x1 * gnm_sqrt (p1) + 1 - p1, 3) / 2;
	}

	res1 = pfuncinverter (p, &alpha, lower_tail, log_p, 0, gnm_pinf, x0,
			      pgamma1, dgamma1);

	return res1 * scale;
}

/* ------------------------------------------------------------------------ */

static gnm_float
dbeta1 (gnm_float x, const gnm_float shape[], gboolean log_p)
{
	return dbeta (x, shape[0], shape[1], log_p);
}

static gnm_float
pbeta1 (gnm_float x, const gnm_float shape[],
	gboolean lower_tail, gboolean log_p)
{
	return pbeta (x, shape[0], shape[1], lower_tail, log_p);
}

static gnm_float
abramowitz_stegun_26_5_22 (gnm_float p, gnm_float a, gnm_float b,
			   gboolean lower_tail, gboolean log_p)
{
	gnm_float yp = qnorm (p, 0, 1, !lower_tail, log_p);
	gnm_float ta = 1 / (2 * a - 1);
	gnm_float tb = 1 / (2 * b - 1);
	gnm_float h = 2 / (ta + tb);
	gnm_float l = (yp * yp - 3) / 6;
	gnm_float w = yp * gnm_sqrt (h + l) / h -
		(tb - ta) * (l + (5 - 4 / h) / 6);
	return a / (a + b * gnm_exp (2 * w));
}


gnm_float
qbeta (gnm_float p, gnm_float pin, gnm_float qin, gboolean lower_tail, gboolean log_p)
{
	gnm_float x0, q, shape[2];
	gnm_float S = pin + qin;

#ifdef IEEE_754
	if (gnm_isnan (S) || gnm_isnan (p))
		return S + p;
#endif
	R_Q_P01_check (p);

	if (pin < 0 || qin < 0) ML_ERR_return_NAN;

	if (!log_p && p > GNM_const(0.9)) {
		/* We're far into the tail.  Flip.  */
		p = 1 - p;
		lower_tail = !lower_tail;
	}

	if (pin >= 1 && qin >= 1)
		x0 = abramowitz_stegun_26_5_22 (p, pin, qin, lower_tail, log_p);
	else {
		/*
		 * The density function is U-shaped.
		 */
		gnm_float phalf = pbeta (0.5, pin, qin, lower_tail, log_p);
		gnm_float lb = gnm_lbeta (pin, qin);

		if (!lower_tail == (p > phalf)) {
			/*
			 * The following approximation follows from simply ignoring
			 * the (1-t)^(qin-1) factor from the density.  That works
			 * fine as long as we stay far away from the right tail.
			 */
			x0 = gnm_exp ((gnm_log (pin) + R_DT_log (p) + lb) / pin);
		} else {
			/* Mirror.  */
			x0 = -gnm_expm1 ((gnm_log (qin) + R_DT_Clog (p) + lb) / qin);
		}
	}

	shape[0] = pin;
	shape[1] = qin;

	q = pfuncinverter (p, shape, lower_tail, log_p, 0, 1, x0,
			   pbeta1, dbeta1);
	return q;
}

gnm_float
qf (gnm_float p, gnm_float n1, gnm_float n2, gboolean lower_tail, gboolean log_p)
{
	gnm_float q, qc;
#ifdef IEEE_754
	if (gnm_isnan(p) || gnm_isnan(n1) || gnm_isnan(n2))
		return p + n1 + n2;
#endif
	if (n1 <= 0 || n2 <= 0) ML_ERR_return_NAN;

	R_Q_P01_check(p);
	if (p == R_DT_0)
		return 0;

	q = qbeta (p, n2 / 2, n1 / 2, !lower_tail, log_p);
	if (q < GNM_const(0.9))
		qc = 1 - q;
	else
		qc = qbeta (p, n1 / 2, n2 / 2, lower_tail, log_p);

	return (qc / q) * (n2 / n1);
}


gnm_float
pbinom2 (gnm_float x0, gnm_float x1, gnm_float n, gnm_float p)
{
	gnm_float Pl;

	if (x0 > n || x1 < 0 || x0 > x1)
		return 0;

	if (x0 == x1)
		return dbinom (x0, n, p, FALSE);

	if (x0 <= 0)
		return pbinom (x1, n, p, TRUE, FALSE);

	if (x1 >= n)
		return pbinom (x0 - 1, n, p, FALSE, FALSE);

	Pl = pbinom (x0 - 1, n, p, TRUE, FALSE);
	if (Pl > GNM_const(0.75)) {
		gnm_float PlC = pbinom (x0 - 1, n, p, FALSE, FALSE);
		gnm_float PrC = pbinom (x1, n, p, FALSE, FALSE);
		return PlC - PrC;
	} else {
		gnm_float Pr = pbinom (x1, n, p, TRUE, FALSE);
		return Pr - Pl;
	}
}

/* ------------------------------------------------------------------------- */
/**
 * expmx2h:
 * @x: a number
 *
 * Returns: The result of exp(-0.5*@x*@x) with higher accuracy than the
 * naive formula.
 */
gnm_float
expmx2h (gnm_float x)
{
	x = gnm_abs (x);

	if (x < 5 || gnm_isnan (x))
		return gnm_exp (GNM_const(-0.5) * x * x);
	else if (x >= GNM_MAX_EXP * M_LN2gnum + 10)
		return 0;
	else {
		/*
		 * Split x into two parts, x=x1+x2, such that |x2|<=2^-16.
		 * Assuming that we are using IEEE doubles, that means that
		 * x1*x1 is error free for x<1024 (above which we will underflow
		 * anyway).  If we are not using IEEE doubles then this is
		 * still an improvement over the naive formula.
		 */
		gnm_float x1 = gnm_round (x * 65536) / 65536;
		gnm_float x2 = x - x1;
		return (gnm_exp (GNM_const(-0.5) * x1 * x1) *
			gnm_exp ((GNM_const(-0.5) * x2 - x1) * x2));
	}
}

/* ------------------------------------------------------------------------- */

/**
 * gnm_agm:
 * @a: a number
 * @b: a number
 *
 * Returns: The arithmetic-geometric mean of @a and @b.
 */
gnm_float
gnm_agm (gnm_float a, gnm_float b)
{
	gnm_float ab = a * b;
	gnm_float scale = 1;
	int i;

	if (a < 0 || b < 0 || gnm_isnan (ab))
		return gnm_nan;

	if (a == b)
		return a;

	if (ab == 0 || ab == gnm_pinf) {
		int ea, eb;

		if (a == 0 || b == 0)
			return 0;

		// Underflow or overflow
		(void)gnm_unscalbn (a, &ea);
		(void)gnm_unscalbn (b, &eb);
		scale = gnm_scalbn (1, -(ea + eb) / 2);
		a *= scale;
		b *= scale;
	}

	for (i = 1; i < 20; i++) {
		gnm_float am = (a + b) / 2;
		gnm_float gm = gnm_sqrt (a * b);
		a = am;
		b = gm;
		if (gnm_abs (a - b) < a * GNM_EPSILON)
			break;
	}
	if (i == 20)
		g_warning ("AGM failed to converge.");

	return a / scale;
}

/**
 * gnm_lambert_w:
 * @x: a number
 * @k: branch, either 0 or -1
 *
 * Returns: The Lambert W function at @x.  @k is a branch number: 0
 * for the primary branch and -1 for the alternate.
 */
gnm_float
gnm_lambert_w (gnm_float x, int k)
{
	gnm_float w;
	static const gnm_float one_over_e = 1 / M_Egnum;
	const gnm_float sqrt_one_over_e = gnm_sqrt (1 / M_Egnum);
	static const gboolean debug = FALSE;
	gnm_float wmin, wmax;
	int i, imax = 20;

	if (gnm_isnan (x) || x < -one_over_e)
		return gnm_nan;
	else if (x == -one_over_e) {
		// This is technically wrong.  The mathematically right
		// value of 1/e is 0.367879441171442321595524...
		// which rounds to 0.367879441171442334024277... as a double.
		// Note, that the rounding went up.  In other words, when we
		// observe "x == -one_over_e" then x is already less that the
		// mathematically right value of -1/e and we ought to return
		// NaN.  That is, however, a somewhat useless behaviour so
		// we return -1 instead.
		//
		// NOTE 1: The analysis might be different for long double.
		// NOTE 2: The analysis assumes that 1/e when computed as double
		//         division (based on rounded e) produces the right
		//         value.  It does, see goffice's test/constants
		return -1;
	}

	if (k == 0) {
		if (x == gnm_pinf)
			return gnm_pinf;
		if (x < 0)
			w = GNM_const(1.5) * (gnm_sqrt (x + one_over_e) - sqrt_one_over_e);
		else if (x < 10)
			w = gnm_sqrt (x) / GNM_const(1.7);
		else {
			gnm_float l1 = gnm_log (x);
			gnm_float l2 = gnm_log (l1);
			w = l1 - l2;
		}
		wmin = -1;
		wmax = gnm_pinf;
	} else if (k == -1) {
		if (x >= 0)
			return (x == 0) ? gnm_ninf : gnm_nan;
		if (x < -GNM_const(0.1))
			w = -1 - 3 * gnm_sqrt (x + one_over_e);
		else {
			gnm_float l1 = gnm_log (-x);
			gnm_float l2 = gnm_log (-l1);
			w = l1 - l2;
		}

		wmin = gnm_ninf;
		wmax = -1;
	} else
		return gnm_nan;

	if (debug) g_printerr ("x = %.20g    w=%.20g\n", x, w);
	for (i = 0; i < imax; i++) {
		gnm_float ew = gnm_exp (w);
		gnm_float wew = w * ew;
		gnm_float d1 = ew * (w + 1);
		gnm_float d2 = ew * (w + 2);
		gnm_float dw;
		gnm_float wold = w;

		dw = (-2 * ((wew - x) * d1) / (2 * d1 * d1 - (wew - x) * d2));
		w += dw;

		if (w <= wmin || w >= wmax) {
			// We overshot
			gnm_float l = (w < wmin ? wmin : wmax);
			g_printerr (" (%2d w = %.20g)\n", i, w);
			dw = (l - wold) * 15 / 16;
			w = wold + dw;
		}

		if (debug) {
			g_printerr ("  %2d w = %.20g\n", i, w);
			if (i == imax - 1) {
				g_printerr ("  wew = %.20g\n", wew);
				g_printerr ("  d1  = %.20g\n", d1);
				g_printerr ("  d2  = %.20g\n", d2);
			}
		}

		if (gnm_abs (dw) <= 2 * GNM_EPSILON * gnm_abs (w))
			break;
	}

	return w;
}


/**
 * pow1p:
 * @x: a number
 * @y: a number
 *
 * Returns: The result of (1+@x)^@y with less rounding error than the
 * naive formula.
 */
gnm_float
pow1p (gnm_float x, gnm_float y)
{
	/*
	 * We defer to the naive algorithm in two cases: (1) when 1+x
	 * is exact (let us hope the compiler does not mess this up),
	 * and (2) when |x|>1/2 and we have no better algorithm.
	 */

	if ((x + 1) - 1 == x || gnm_abs (x) > GNM_const(0.5) ||
	    gnm_isnan (x) || gnm_isnan (y))
		return gnm_pow (1 + x, y);
	else if (y < 0)
		return 1 / pow1p (x, -y);
	else {
		gnm_float x1 = gnm_round (x * 65536) / 65536;
		gnm_float x2 = x - x1;
		gnm_float h, l;
		ebd0 (y, y*(x+1), &h, &l);
		PAIR_ADD (-y*x1, h, l);
		PAIR_ADD (-y*x2, h, l);

#if 0
		g_printerr ("pow1p (%.20g, %.20g)\n", x, y);
#endif

		return gnm_exp (-l) * gnm_exp (-h);
	}
}

/**
 * pow1pm1:
 * @x: a number
 * @y: a number
 *
 * Returns: The result of (1+@x)^@y-1 with less rounding error than the
 * naive formula.
 */
gnm_float
pow1pm1 (gnm_float x, gnm_float y)
{
	if (x <= -1)
		return gnm_pow (1 + x, y) - 1;
	else
		return gnm_expm1 (y * gnm_log1p (x));
}


/**
 * gnm_taylor_log1p:
 * @x: a number
 * @k: starting term.
 *
 * Returns: The taylor series for log(1+@x), except that terms before the @k'th are discarded.
 */
gnm_float
gnm_taylor_log1p (gnm_float x, int k)
{
	gnm_float xn[100];
	gnm_float lim = 0, term, sum = 0;
	int i;

	// The actual requirement is |x| < 1 going to the edge would be
	// painfully slow.
	g_return_val_if_fail (gnm_abs (x) <= GNM_const(0.58), gnm_nan);

	k = CLAMP (k, 1, (int)G_N_ELEMENTS(xn));
	if (k == 1)
		return gnm_log1p (x);

	xn[1] = x;
	for (i = 2; i < k; i++)
		xn[i] = xn[i / 2] * xn[(i + 1) / 2];

	for (i = k; i < (int)G_N_ELEMENTS(xn); i++) {
		xn[i] = xn[i / 2] * xn[(i + 1) / 2];
		term = xn[i] / ((i & 1) ? i : -i);
		sum += term;
		if (i == k)
			lim = xn[i] * (GNM_EPSILON / 100);
		else if (gnm_abs (term) <= lim)
			break;
	}

	return sum;
}

/*
 ---------------------------------------------------------------------
  Matrix functions
 ---------------------------------------------------------------------
 */

GType
gnm_matrix_get_type (void)
{
	static GType t = 0;

	if (t == 0)
		t = g_boxed_type_register_static ("GnmMatrix",
			 (GBoxedCopyFunc)gnm_matrix_ref,
			 (GBoxedFreeFunc)gnm_matrix_unref);
	return t;
}

/**
 * gnm_matrix_new:
 * @rows: Number of rows.
 * @cols: Number of columns.
 *
 * Returns: (transfer full): A new #GnmMatrix.
 */
/* Note the order: y then x. */
GnmMatrix *
gnm_matrix_new (int rows, int cols)
{
	GnmMatrix *m = g_new (GnmMatrix, 1);
	int r;

	m->ref_count = 1;
	m->rows = rows;
	m->cols = cols;
	m->data = g_new (gnm_float *, rows);
	for (r = 0; r < rows; r++)
		m->data[r] = g_new (gnm_float, cols);

	return m;
}

/**
 * gnm_matrix_ref:
 * @m: (transfer none) (nullable): #GnmMatrix
 *
 * Returns: (transfer full) (nullable): a new reference to @m.
 */
GnmMatrix *
gnm_matrix_ref (GnmMatrix *m)
{
	if (m)
		m->ref_count++;
	return m;
}

/**
 * gnm_matrix_unref:
 * @m: (transfer full) (nullable): #GnmMatrix
 */
void
gnm_matrix_unref (GnmMatrix *m)
{
	int r;

	if (!m || m->ref_count-- > 1)
		return;

	for (r = 0; r < m->rows; r++)
		g_free (m->data[r]);
	g_free (m->data);
	g_free (m);
}

/**
 * gnm_matrix_is_empty:
 * @m: (nullable): A #GnmMatrix
 *
 * Returns: %TRUE if @m is empty.
 */
gboolean
gnm_matrix_is_empty (GnmMatrix const *m)
{
	return m == NULL || m->rows <= 0 || m->cols <= 0;
}

/**
 * gnm_matrix_from_value:
 * @v: #GnmValue
 * @perr: (out) (transfer full): #GnmValue with error value
 * @ep: Evaluation location
 *
 * Returns: (transfer full) (nullable): A new #GnmMatrix, %NULL on error.
 */
GnmMatrix *
gnm_matrix_from_value (GnmValue const *v, GnmValue **perr, GnmEvalPos const *ep)
{
	int cols, rows;
	int c, r;
	GnmMatrix *m = NULL;

	*perr = NULL;
	cols = value_area_get_width (v, ep);
	rows = value_area_get_height (v, ep);
	m = gnm_matrix_new (rows, cols);
	for (r = 0; r < rows; r++) {
		for (c = 0; c < cols; c++) {
			GnmValue const *v1 = value_area_fetch_x_y (v, c, r, ep);
			if (VALUE_IS_ERROR (v1)) {
				*perr = value_dup (v1);
				gnm_matrix_unref (m);
				return NULL;
			}

			m->data[r][c] = value_get_as_float (v1);
		}
	}
	return m;
}

/**
 * gnm_matrix_to_value:
 * @m: #GnmMatrix
 *
 * Returns: (transfer full): A #GnmValue array
 */
GnmValue *
gnm_matrix_to_value (GnmMatrix const *m)
{
	GnmValue *res = value_new_array_non_init (m->cols, m->rows);
	int c, r;

	for (c = 0; c < m->cols; c++) {
	        res->v_array.vals[c] = g_new (GnmValue *, m->rows);
	        for (r = 0; r < m->rows; r++)
		        res->v_array.vals[c][r] = value_new_float (m->data[r][c]);
	}
	return res;
}

/**
 * gnm_matrix_multiply:
 * @C: Output #GnmMatrix
 * @A: #GnmMatrix
 * @B: #GnmMatrix
 *
 * Computes @A * @B and stores the result in @C.  The matrices must have
 * suitable sizes.
 */
void
gnm_matrix_multiply (GnmMatrix *C, const GnmMatrix *A, const GnmMatrix *B)
{
	void *state;
	GnmAccumulator *acc;
	int c, r, i;

	g_return_if_fail (C != NULL);
	g_return_if_fail (A != NULL);
	g_return_if_fail (B != NULL);
	g_return_if_fail (C->rows == A->rows);
	g_return_if_fail (C->cols == B->cols);
	g_return_if_fail (A->cols == B->rows);

	state = gnm_accumulator_start ();
	acc = gnm_accumulator_new ();

	for (r = 0; r < C->rows; r++) {
		for (c = 0; c < C->cols; c++) {
			gnm_accumulator_clear (acc);
			for (i = 0; i < A->cols; ++i) {
				GnmQuad p;
				gnm_quad_mul12 (&p,
						A->data[r][i],
						B->data[i][c]);
				gnm_accumulator_add_quad (acc, &p);
			}
			C->data[r][c] = gnm_accumulator_value (acc);
		}
	}

	gnm_accumulator_free (acc);
	gnm_accumulator_end (state);
}

/***************************************************************************/

static int
gnm_matrix_eigen_max_index (gnm_float *row, guint row_n, guint size)
{
	guint i, res = row_n + 1;
	gnm_float max;

	if (res >= size)
		return (size - 1);

	max = gnm_abs (row[res]);

	for (i = res + 1; i < size; i++)
		if (gnm_abs (row[i]) > max) {
			res = i;
			max = gnm_abs (row[i]);
		}
	return res;
}

static void
gnm_matrix_eigen_rotate (gnm_float **matrix, guint k, guint l, guint i, guint j, gnm_float c, gnm_float s)
{
	gnm_float x = c * matrix[k][l] - s * matrix[i][j];
	gnm_float y = s * matrix[k][l] + c * matrix[i][j];

	matrix[k][l] = x;
	matrix[i][j] = y;
}

static void
gnm_matrix_eigen_update (guint k, gnm_float t, gnm_float *eigenvalues, gboolean *changed, guint *state)
{
	gnm_float y = eigenvalues[k];
	gboolean unchanged;
	eigenvalues[k] += t;
	unchanged = (y == eigenvalues[k]);
	if (changed[k] && unchanged) {
		changed[k] = FALSE;
		(*state)--;
	} else if (!changed[k] && !unchanged) {
		changed[k] = TRUE;
		(*state)++;
	}
}

/**
 * gnm_matrix_eigen:
 * @m: Input #GnmMatrix
 * @EIG: Output #GnmMatrix
 * @eigenvalues: (out): Output location for eigen values.
 *
 * Calculates the eigenvalues and eigenvectors of a real symmetric matrix.
 *
 * This is the Jacobi iterative process in which we use a sequence of
 * Jacobi rotations (two-sided Givens rotations) in order to reduce the
 * magnitude of off-diagonal elements while preserving eigenvalues.
 */
gboolean
gnm_matrix_eigen (GnmMatrix const *m, GnmMatrix *EIG, gnm_float *eigenvalues)
{
	guint i, state, usize, *ind;
	gboolean *changed;
	guint counter = 0;
	gnm_float **matrix;
	gnm_float **eigenvectors;

	g_return_val_if_fail (m != NULL, FALSE);
	g_return_val_if_fail (m->rows == m->cols, FALSE);
	g_return_val_if_fail (EIG != NULL, FALSE);
	g_return_val_if_fail (EIG->rows == EIG->cols, FALSE);
	g_return_val_if_fail (EIG->rows == m->rows, FALSE);

	matrix = m->data;
	eigenvectors = EIG->data;
	usize = m->rows;

	state = usize;

	ind = g_new (guint, usize);
	changed =  g_new (gboolean, usize);

	for (i = 0; i < usize; i++) {
		guint j;
		for (j = 0; j < usize; j++)
			eigenvectors[j][i] = 0.;
		eigenvectors[i][i] = 1.;
		eigenvalues[i] = matrix[i][i];
		ind[i] = gnm_matrix_eigen_max_index (matrix[i], i, usize);
		changed[i] = TRUE;
	}

	while (usize > 1 && state != 0) {
		guint k, l, m = 0;
		gnm_float c, s, y, pivot, t;

		counter++;
		if (counter > 400000) {
			g_free (ind);
			g_free (changed);
			g_print ("gnm_matrix_eigen exceeded iterations\n");
			return FALSE;
		}
		for (k = 1; k < usize - 1; k++)
			if (gnm_abs (matrix[k][ind[k]]) > gnm_abs (matrix[m][ind[m]]))
				m = k;
		l = ind[m];
		pivot = matrix[m][l];
		/* pivot is (m,l) */
		if (pivot == 0) {
			/* All remaining off-diagonal elements are zero.  We're done.  */
			break;
		}

		y = (eigenvalues[l] - eigenvalues[m]) / 2;
		t = gnm_abs (y) + gnm_hypot (pivot, y);
		s = gnm_hypot (pivot, t);
		c = t / s;
		s = pivot / s;
		t = pivot * pivot / t;
		if (y < 0) {
			s = -s;
			t = -t;
		}
		matrix[m][l] = 0.;
		gnm_matrix_eigen_update (m, -t, eigenvalues, changed, &state);
		gnm_matrix_eigen_update (l, t, eigenvalues, changed, &state);
		for (i = 0; i < m; i++)
			gnm_matrix_eigen_rotate (matrix, i, m, i, l, c, s);
		for (i = m + 1; i < l; i++)
			gnm_matrix_eigen_rotate (matrix, m, i, i, l, c, s);
		for (i = l + 1; i < usize; i++)
			gnm_matrix_eigen_rotate (matrix, m, i, l, i, c, s);
		for (i = 0; i < usize; i++) {
			gnm_float x = c * eigenvectors[i][m] - s * eigenvectors[i][l];
			gnm_float y = s * eigenvectors[i][m] + c * eigenvectors[i][l];

			eigenvectors[i][m] = x;
			eigenvectors[i][l] = y;
		}
		ind[m] = gnm_matrix_eigen_max_index (matrix[m], m, usize);
		ind[l] = gnm_matrix_eigen_max_index (matrix[l], l, usize);
	}

	g_free (ind);
	g_free (changed);

	return TRUE;
}

/* ------------------------------------------------------------------------- */

static void
swap_row_and_col (GnmMatrix *M, int a, int b)
{
	gnm_float *r;
	int i;

	// Swap rows
	r = M->data[a];
	M->data[a] = M->data[b];
	M->data[b] = r;

	// Swap cols
	for (i = 0; i < M->rows; i++) {
		gnm_float d = M->data[i][a];
		M->data[i][a] = M->data[i][b];
		M->data[i][b] = d;
	}
}


gboolean
gnm_matrix_modified_cholesky (GnmMatrix const *A,
			      GnmMatrix *L,
			      gnm_float *D,
			      gnm_float *E,
			      int *P)
{
	int n = A->cols;
	gnm_float nu, xsi, gam, bsqr, delta;
	int i, j;
	GnmMatrix *G, *C;

	g_return_val_if_fail (A->rows == A->cols, FALSE);
	g_return_val_if_fail (A->rows == L->rows, FALSE);
	g_return_val_if_fail (A->cols == L->cols, FALSE);

	// Copy A into L; Use G and C as aliases for L.
	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++)
			L->data[i][j] = A->data[i][j];
	G = L;
	C = G;

	// Init permutation as identity.
	for (i = 0; i < n; i++)
		P[i] = i;

	nu = n == 1 ? 1 : gnm_sqrt (n * n - 1);
	gam = xsi = 0;
	for (i = 0; i < n; i++) {
		gnm_float aii = gnm_abs (G->data[i][i]);
		gam = MAX (gam, aii);
		for (j = i + 1; j < n; j++) {
			gnm_float aij = gnm_abs (G->data[i][j]);
			xsi = MAX (xsi, aij);
		}
	}
	bsqr = MAX (MAX (gam, xsi / nu), GNM_EPSILON);
	delta = MAX (gam + xsi, 1) * GNM_EPSILON;

	for (j = 0; j < n; j++) {
		int q, s;
		gnm_float theta_j = 0, dj;

		q = j;
		for (i = j + 1; i < n; i++) {
			if (gnm_abs (C->data[i][i]) > gnm_abs (C->data[q][q]))
				q = i;
		}

		if (j != q) {
			int a;
			gnm_float b;

			swap_row_and_col (L, j, q);
			a = P[j]; P[j] = P[q]; P[q] = a;
			b = D[j]; D[j] = D[q]; D[q] = b;
			if (E) { b = E[j]; E[j] = E[q]; E[q] = b; }
		}

		for (s = 0; s < j; s++)
			L->data[j][s] = C->data[j][s] / D[s];

		for (i = j + 1; i < n; i++) {
			int s;
			gnm_float d = G->data[i][j];

			for (s = 0; s < j; s++)
				d -= L->data[j][s] * C->data[i][s];
			C->data[i][j] = d;

			theta_j = MAX (theta_j, gnm_abs (d));
		}

		dj = MAX (theta_j * theta_j / bsqr, delta);
		dj = MAX (dj, gnm_abs (C->data[j][j]));
		D[j] = dj;
		if (E) E[j] = dj - C->data[j][j];

		for (i = j + 1; i < n; i++) {
			gnm_float cij = C->data[i][j];
			C->data[i][i] -= cij * cij / D[j];
		}
	}

	for (i = 0; i < n; i++) {
		for (j = i + 1; j < n; j++)
			L->data[i][j] = 0;
		L->data[i][i] = 1;
	}

	return TRUE;
}

GORegressionResult
gnm_linear_solve_posdef (GnmMatrix const *A, const gnm_float *b,
			 gnm_float *x)
{
	int i, j, n;
	GnmMatrix *L;
	gnm_float *D, *E;
	int *P;
	GORegressionResult res;
	gboolean ok;

	g_return_val_if_fail (A != NULL, GO_REG_invalid_dimensions);
	g_return_val_if_fail (A->rows == A->cols, GO_REG_invalid_dimensions);
	g_return_val_if_fail (b != NULL, GO_REG_invalid_dimensions);
	g_return_val_if_fail (x != NULL, GO_REG_invalid_dimensions);

	n = A->cols;
	L = gnm_matrix_new (n, n);
	D = g_new (gnm_float, n);
	E = g_new (gnm_float, n);
	P = g_new (int, n);

	ok = gnm_matrix_modified_cholesky (A, L, D, E, P);
	if (!ok) {
		res = GO_REG_invalid_data;
		goto done;
	}

	if (gnm_debug_flag ("posdef")) {
		for (i = 0; i < n; i++)
			g_printerr ("Posdef E[i] = %g\n", E[P[i]]);
	}

	// The only information from the above we use is E and P.
	// However, we reuse the memory for L for A+E
	for (i = 0; i < n; i++) {
		for (j = 0; j < n; j++)
			L->data[i][j] = A->data[i][j];
		L->data[i][i] += E[P[i]];
	}

	res = gnm_linear_solve (L, b, x);

done:
	g_free (P);
	g_free (E);
	g_free (D);
	gnm_matrix_unref (L);

	return res;
}

/* ------------------------------------------------------------------------- */

GORegressionResult
gnm_linear_solve (GnmMatrix const *A, const gnm_float *b,
		  gnm_float *x)
{
	g_return_val_if_fail (A != NULL, GO_REG_invalid_dimensions);
	g_return_val_if_fail (A->rows == A->cols, GO_REG_invalid_dimensions);
	g_return_val_if_fail (b != NULL, GO_REG_invalid_dimensions);
	g_return_val_if_fail (x != NULL, GO_REG_invalid_dimensions);

	return GNM_SUFFIX(go_linear_solve) (A->data, b, A->rows, x);
}

GORegressionResult
gnm_linear_solve_multiple (GnmMatrix const *A, GnmMatrix *B)
{
	g_return_val_if_fail (A != NULL, GO_REG_invalid_dimensions);
	g_return_val_if_fail (B != NULL, GO_REG_invalid_dimensions);
	g_return_val_if_fail (A->rows == A->cols, GO_REG_invalid_dimensions);
	g_return_val_if_fail (A->rows == B->rows, GO_REG_invalid_dimensions);

	return GNM_SUFFIX(go_linear_solve_multiple) (A->data, B->data, A->rows, B->cols);
}

/* ------------------------------------------------------------------------- */

#ifdef GNM_SUPPLIES_ERFL
long double
erfl (long double x)
{
	if (fabsl (x) < GNM_const(0.125)) {
		/* For small x the pnorm formula loses precision.  */
		long double sum = 0;
		long double term = x * 2 / sqrtl (M_PIgnum);
		long double n;
		long double x2 = x * x;

		for (n = 0; fabsl (term) >= fabsl (sum) * LDBL_EPSILON ; n++) {
			sum += term / (2 * n + 1);
			term *= -x2 / (n + 1);
		}

		return sum;
	}
	return pnorm (x * M_SQRT2gnum, 0, 1, TRUE, FALSE) * 2 - 1;
}
#endif

/* ------------------------------------------------------------------------- */

#ifdef GNM_SUPPLIES_ERFCL
long double
erfcl (long double x)
{
	return 2 * pnorm (x * M_SQRT2gnum, 0, 1, FALSE, FALSE);
}
#endif

/* ------------------------------------------------------------------------- */

static gnm_float
gnm_owent_T1 (gnm_float h, gnm_float a, int order)
{
	const gnm_float hs = GNM_const(-0.5) * (h * h);
	const gnm_float dhs = gnm_exp (hs);
	const gnm_float as = a * a;
	gnm_float aj = a / (M_PIgnum * 2);
	gnm_float dj = gnm_expm1 (hs);
	gnm_float gj = hs * dhs;
	gnm_float res = gnm_atanpi (a) / 2;
	int j;

	for (j = 1; j <= order; j++) {
		res += dj * aj / (j + j - 1);

		aj *= as;
		dj = gj - dj;
		gj *= hs / (j + 1);
	}

	return res;
}

static gnm_float
gnm_owent_T2 (gnm_float h, gnm_float a, int order)
{
	const gnm_float ah = a * h;
	const gnm_float as = -a * a;
	const gnm_float y = 1 / (h * h);
	gnm_float val = 0;
	gnm_float vi = a * dnorm (ah, 0, 1, FALSE);
	gnm_float z = gnm_erf (ah / M_SQRT2gnum) / (2 * h);
	int i;

	for (i = 1; i <= 2 * order + 1; i += 2) {
		val += z;
		z = y * (vi - i * z);
		vi *= as;
	}
	return val * dnorm (h, 0, 1, FALSE);
}

static gnm_float
gnm_owent_T3 (gnm_float h, gnm_float a, int order)
{
	static const gnm_float c2[] = {
		GNM_const(+0.99999999999999987510),
		GNM_const(-0.99999999999988796462),
		GNM_const(+0.99999999998290743652),
		GNM_const(-0.99999999896282500134),
		GNM_const(+0.99999996660459362918),
		GNM_const(-0.99999933986272476760),
		GNM_const(+0.99999125611136965852),
		GNM_const(-0.99991777624463387686),
		GNM_const(+0.99942835555870132569),
		GNM_const(-0.99697311720723000295),
		GNM_const(+0.98751448037275303682),
		GNM_const(-0.95915857980572882813),
		GNM_const(+0.89246305511006708555),
		GNM_const(-0.76893425990463999675),
		GNM_const(+0.58893528468484693250),
		GNM_const(-0.38380345160440256652),
		GNM_const(+0.20317601701045299653),
		GNM_const(-0.82813631607004984866E-01),
		GNM_const(+0.24167984735759576523E-01),
		GNM_const(-0.44676566663971825242E-02),
		GNM_const(+0.39141169402373836468E-03)
	};

	const gnm_float ah = a * h;
	const gnm_float as = a * a;
	const gnm_float y = 1 / (h * h);
	gnm_float vi = a * dnorm (ah, 0, 1, FALSE);
	gnm_float zi = gnm_erf (ah / M_SQRT2gnum) / (2 * h);
	gnm_float val = 0;
	int i;

	g_return_val_if_fail (order < (int)G_N_ELEMENTS(c2), gnm_nan);

	for (i = 0; i <= order; i++) {
		val += zi * c2[i];
		zi = y * ((i + i + 1) * zi - vi);
		vi *= as;
	}
	return val * dnorm (h, 0, 1, FALSE);
}

static gnm_float
gnm_owent_T4 (gnm_float h, gnm_float a, int order)
{
	const gnm_float hs = h * h;
	const gnm_float as = -a * a;
	gnm_float ai = a * gnm_exp (GNM_const(-0.5) * hs * (1 - as)) / (2 * M_PIgnum);
	gnm_float yi = 1;
	gnm_float val = 0;
	int i;

	for (i = 1; i <= 2 * order + 1; i += 2) {
		val += ai * yi;
		yi = (1 - hs * yi) / (i + 2);
		ai *= as;
	}
	return val;
}

static gnm_float
gnm_owent_T5 (gnm_float h, gnm_float a, int order)
{
	static const gnm_float pts[] = {
		GNM_const(0.35082039676451715489E-02),
		GNM_const(0.31279042338030753740E-01),
		GNM_const(0.85266826283219451090E-01),
		GNM_const(0.16245071730812277011),
		GNM_const(0.25851196049125434828),
		GNM_const(0.36807553840697533536),
		GNM_const(0.48501092905604697475),
		GNM_const(0.60277514152618576821),
		GNM_const(0.71477884217753226516),
		GNM_const(0.81475510988760098605),
		GNM_const(0.89711029755948965867),
		GNM_const(0.95723808085944261843),
		GNM_const(0.99178832974629703586)
	};
	static const gnm_float wts[] = {
		0.18831438115323502887E-01,
		0.18567086243977649478E-01,
		0.18042093461223385584E-01,
		0.17263829606398753364E-01,
		0.16243219975989856730E-01,
		0.14994592034116704829E-01,
		0.13535474469662088392E-01,
		0.11886351605820165233E-01,
		0.10070377242777431897E-01,
		0.81130545742299586629E-02,
		0.60419009528470238773E-02,
		0.38862217010742057883E-02,
		0.16793031084546090448E-02
	};
	const gnm_float as = a * a;
	const gnm_float hs = GNM_const(-0.5) * h * h;
	gnm_float val = 0;
	int i;

	g_return_val_if_fail (order <= (int)G_N_ELEMENTS(pts), gnm_nan);
	g_return_val_if_fail (order <= (int)G_N_ELEMENTS(wts), gnm_nan);

	for (i = 0; i < order; i++) {
		gnm_float r = 1 + as * pts[i];
		val += wts[i] * gnm_exp (hs * r) / r;
	}

	return val * a;
}

static gnm_float
gnm_owent_T6 (gnm_float h, gnm_float a, int order)
{
	const gnm_float normh = pnorm (h, 0, 1, FALSE, FALSE);
	const gnm_float normhC = 1 - normh;
	const gnm_float y = 1 - a;
	const gnm_float r = gnm_atan2 (y, 1 + a);
	gnm_float val = GNM_const(0.5) * normh * normhC;

	if (r != 0)
		val -= r * gnm_exp (GNM_const(-0.5) * y * h * h / r) / (2 * M_PIgnum);

	return val;
}

static gnm_float
gnm_owent_helper (gnm_float h, gnm_float a)
{
	static const gnm_float hrange[] = {
		0.02, 0.06, 0.09, 0.125, 0.26, 0.4,  0.6,
		1.6,  1.7,  2.33,  2.4,  3.36, 3.4,  4.8
	};
	static const gnm_float arange[] = {
		0.025, 0.09, 0.15, 0.36, 0.5, 0.9, 0.99999
	};
	static const guint8 method[] = {
		1, 1, 2,13,13,13,13,13,13,13,13,16,16,16, 9,
		1, 2, 2, 3, 3, 5, 5,14,14,15,15,16,16,16, 9,
		2, 2, 3, 3, 3, 5, 5,15,15,15,15,16,16,16,10,
		2, 2, 3, 5, 5, 5, 5, 7, 7,16,16,16,16,16,10,
		2, 3, 3, 5, 5, 6, 6, 8, 8,17,17,17,12,12,11,
		2, 3, 5, 5, 5, 6, 6, 8, 8,17,17,17,12,12,12,
		2, 3, 4, 4, 6, 6, 8, 8,17,17,17,17,17,12,12,
		2, 3, 4, 4, 6, 6,18,18,18,18,17,17,17,12,12
	};
	int ai, hi;

	g_return_val_if_fail (h >= 0, gnm_nan);
	g_return_val_if_fail (a >= 0 && a <= 1, gnm_nan);

	for (ai = 0; ai < (int)G_N_ELEMENTS(arange); ai++)
		if (a <= arange[ai])
			break;
	for (hi = 0; hi < (int)G_N_ELEMENTS(hrange); hi++)
		if (h <= hrange[hi])
			break;

	switch (method[ai * (1 + G_N_ELEMENTS(hrange)) + hi]) {
	case  1: return gnm_owent_T1 (h, a, 2);
	case  2: return gnm_owent_T1 (h, a, 3);
	case  3: return gnm_owent_T1 (h, a, 4);
	case  4: return gnm_owent_T1 (h, a, 5);
	case  5: return gnm_owent_T1 (h, a, 7);
	case  6: return gnm_owent_T1 (h, a, 10);
	case  7: return gnm_owent_T1 (h, a, 12);
	case  8: return gnm_owent_T1 (h, a, 18);
	case  9: return gnm_owent_T2 (h, a, 10);
	case 10: return gnm_owent_T2 (h, a, 20);
	case 11: return gnm_owent_T2 (h, a, 30);
	case 12: return gnm_owent_T3 (h, a, 20);
	case 13: return gnm_owent_T4 (h, a, 4);
	case 14: return gnm_owent_T4 (h, a, 7);
	case 15: return gnm_owent_T4 (h, a, 8);
	case 16: return gnm_owent_T4 (h, a, 20);
	case 17: return gnm_owent_T5 (h, a, 13);
	case 18: return gnm_owent_T6 (h, a, 0);
	default:
		g_assert_not_reached ();
	}
}

/*
 * See "Fast and Accurate Calculation of Owen's T-Function" by
 * Mike Patefield and David Tandy.  Journal of Statistical Software,
 * Volume 5, Issue 5, July 2000.
 *
 * Original code licensed under GPLv2+.
 */
gnm_float
gnm_owent (gnm_float h, gnm_float a)
{
	gnm_float res;
	gboolean neg;

	/* Even in the "h" argument.  */
	h = gnm_abs (h);

	/* Odd in the "a" argument.  */
	neg = (a < 0);
	a = gnm_abs (a);

	if (a == 0)
		res = 0;
	else if (h == 0)
		res = gnm_atanpi (a) / 2;
	else if (a == 1)
		res = GNM_const(0.5) * pnorm (h, 0, 1, TRUE, FALSE) *
			pnorm (h, 0, 1, FALSE, FALSE);
	else if (a <= 1)
		res = gnm_owent_helper (h, a);
	else {
		gnm_float ah = h * a;
		/*
		 * Use formula (2):
		 *
		 * T(h,a) = .5N(h) + .5N(ha) - N(h)N(ha) - T(ha,1/a)
		 *
		 * with care to avoid cancellation.
		 */
		if (h <= GNM_const(0.67)) {
			gnm_float nh = GNM_const(0.5) * gnm_erf (h / M_SQRT2gnum);
			gnm_float nah = GNM_const(0.5) * gnm_erf (ah / M_SQRT2gnum);
			res = GNM_const(0.25) - nh * nah -
				gnm_owent_helper (ah, 1 / a);
		} else {
			gnm_float nh = pnorm (h, 0, 1, FALSE, FALSE);
			gnm_float nah = pnorm (ah, 0, 1, FALSE, FALSE);
			res = GNM_const(0.5) * (nh + nah) - nh * nah -
				gnm_owent_helper (ah, 1 / a);
		}
	}

	/* Odd in the "a" argument.  */
	if (neg)
		res = 0 - res;

	return res;
}

/* ------------------------------------------------------------------------- */

gnm_float
gnm_ilog (gnm_float x, gnm_float b)
{
	int be;

	if (gnm_isnan (x) || x < 0 ||
	    gnm_isnan (b) || b == 1 || b <= 0 || b == gnm_pinf)
		return gnm_nan;

	if (x == 0)
		return b < 1 ? gnm_pinf : gnm_ninf;

	if (x == gnm_pinf)
		return b < 1 ? gnm_ninf : gnm_pinf;

	if (b == GNM_RADIX) {
		int eb;
		(void)gnm_unscalbn (x, &eb);
		return eb - 1;
	}

	// If the base is 2^i for i>0 then matters are simple
	if (gnm_frexp (b, &be) == GNM_const(0.5) && be >= 2) {
		int e;
		gnm_float m = gnm_frexp (x, &e);
		(void)m;
		return (e - 1) / (be - 1);
	}

	if (GNM_RADIX != 10 && b == 10 && x >= 1 && x <= GNM_const(1e22)) {
		// This code relies on 10^i being exact
		gnm_float l10 = gnm_log10 (x);
		int il10 = (int)l10;
		if (x < gnm_pow10 (il10))
			il10--;
		return il10;
	}

	if (b == gnm_floor (b)) {
		void *state = gnm_quad_start ();
		GnmQuad qx, qb, qlogb, qfudge;

		gnm_quad_init (&qb, b);
		gnm_quad_log (&qlogb, &qb);

		gnm_quad_init (&qx, x);
		gnm_quad_log (&qx, &qx);
		gnm_quad_div (&qx, &qx, &qlogb);

		// We have computed log_b(x) and we need to take the
		// floor of that.  But we have rounding errors, so we
		// need to answer the question of how close can b^i
		// get to a floating point number while still being
		// more than said number?
		//
		// For double with 2<=b<=100000, the answer seems to
		// be about 1 part in 10^23.  (For 5561^13 if you must
		// know.)
		//
		// The GnmQuad precision is better than 1 in 10^30, so
		// we have quite some room to work with.  But deep down
		// we're hoping for the best.

		gnm_quad_init (&qfudge, qx.h * (256 * GNM_EPSILON * GNM_EPSILON));
		gnm_quad_add (&qx, &qx, &qfudge);
		gnm_quad_floor (&qx, &qx);

		gnm_quad_end (state);

		return gnm_quad_value (&qx);
	}

	// Not implemented.
	return gnm_nan;
}

/* ------------------------------------------------------------------------- */

gnm_float
gnm_logbase (gnm_float x, gnm_float b)
{
	gnm_float l, lr;

	if (gnm_isnan (x) || !gnm_finite (b) || x < 0 || b <= 0 || b == 1)
		return gnm_nan;

	// A few special cases (only reached when b is sane)
	if (x == 0)
		return b < 1 ? gnm_pinf : gnm_ninf;
	if (x == gnm_pinf)
		return b < 1 ? gnm_ninf : gnm_pinf;

	if (b == 2)
		return gnm_log2 (x);
#if GNM_RADIX % 2 == 0
	if (b == GNM_const(0.5))
		return -gnm_log2 (x);  // Since 0.5 has exact representation
#endif

#if GNM_RADIX == 10
	if (b == 10)
		return gnm_log10 (x);  // Don't trust this unless GNM_RADIX == 10
	if (b == GNM_const(0.1))
		return 0 - gnm_log10 (x);  // Since 0.1 has exact representation
#endif

	if (b == 10)
		l = gnm_log10 (x);
	else
		l = gnm_log (x) / gnm_log (b);

	// If base is not an integer, don't try anything fancy
	// Ditto for numbers so large we have loss of precision
	if (b != gnm_floor (b) || x > GNM_const(1.0) / GNM_EPSILON)
		return l;

	// If result is not near an integer, bail
	lr = gnm_round (l);
	if (gnm_abs (l - lr) > GNM_const(1e-8))
		return l;

	// If the b^lr is a match, use lr as result instead.
	if (gnm_pow (b, lr) == x)
		return lr;

	return l;
}
