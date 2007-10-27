/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *//*
 * This implementation has been taken from the OpenOffice 1.0, see
 * functions in scaddins/source/analysis/analysishelper.cxx. Since
 * then there has been made some Gnumeric type system, glib and the C
 * language specific changes.
 *
 *  The Initial Developer of the Original Code is: Sun Microsystems, Inc.
 *
 *  Sun has made the contents of this file available subject to the
 *  terms of GNU Lesser General Public License Version 2.1 as
 *  specified in scaddins/source/analysis/analysishelper.cxx revision
 *  1.35 available in the OpenOffice package.
 *
 *
 *  Copyright: 2000 by Sun Microsystems, Inc.
 *
 *  GNU Lesser General Public License Version 2.1
 *  =============================================
 *  Copyright 2000 by Sun Microsystems, Inc.
 *  901 San Antonio Road, Palo Alto, CA 94303, USA
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License version 2.1, as published by the Free Software Foundation.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 *  02110-1301  USA.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <gnm-datetime.h>
#include <math.h>
#include <value.h>
#include "sc-fin.h"


static gnm_float
GetRmz ( gnm_float fZins, gnm_float fZzr, gnm_float fBw, gnm_float fZw,
	 gint nF )
{
        gnm_float fRmz;

        if ( fZins == 0.0 )
                fRmz = ( fBw + fZw ) / fZzr;
        else {
                gnm_float fTerm = gnm_pow ( 1.0 + fZins, fZzr );
                if ( nF > 0 )
                        fRmz = ( fZw * fZins / ( fTerm - 1.0 ) + fBw * fZins /
				 ( 1.0 - 1.0 / fTerm ) ) / ( 1.0 + fZins );
                else
                        fRmz = fZw * fZins / ( fTerm - 1.0 ) + fBw * fZins /
				( 1.0 - 1.0 / fTerm );
        }

        return -fRmz;
}

static gnm_float
GetZw ( gnm_float fZins, gnm_float fZzr, gnm_float fRmz, gnm_float fBw,
	gint nF )
{
        gnm_float fZw;

        if ( fZins == 0.0 )
                fZw = fBw + fRmz * fZzr;
        else {
                gnm_float fTerm = gnm_pow ( 1.0 + fZins, fZzr );
                if ( nF > 0 )
                        fZw = fBw * fTerm + fRmz * ( 1.0 + fZins ) *
				( fTerm - 1.0 ) / fZins;
                else
                        fZw = fBw * fTerm + fRmz * ( fTerm - 1.0 ) / fZins;
        }

        return -fZw;
}

static gnm_float
Duration (GDate *nSettle, GDate *nMat, gnm_float fCoup, gnm_float fYield,
	  gint nFreq, gint nBase, gnm_float fNumOfCoups)
{
        /* gnm_float  fYearfrac   = yearfrac ( nSettle, nMat, nBase ); */
        gnm_float  fDur        = 0.0;
	gnm_float  t, p        = 0.0;

        const gnm_float f100   = 100.0;

        fCoup  *= f100 / (gnm_float) nFreq; /* fCoup is used as cash flow */
        fYield /= nFreq;
        fYield += 1.0;

        for ( t = 1.0 ; t < fNumOfCoups ; t++ )
                fDur += t * ( fCoup ) / gnm_pow ( fYield, t );

        fDur += fNumOfCoups * ( fCoup + f100 ) / gnm_pow ( fYield, fNumOfCoups );

        for ( t = 1.0 ; t < fNumOfCoups ; t++ )
                p += fCoup / gnm_pow ( fYield, t );

        p += ( fCoup + f100 ) / gnm_pow ( fYield, fNumOfCoups );

        fDur /= p;
        fDur /= (gnm_float) nFreq;

        return ( fDur );
}

/***************************************************************************/

GnmValue *
get_amordegrc (gnm_float fCost, GDate *nDate, GDate *nFirstPer,
	       gnm_float fRestVal, gint nPer, gnm_float fRate,
	       gint nBase)
{
        gint       n;
	gnm_float fAmorCoeff, fNRate, fRest, fUsePer;

#define Round(x,y) (gnm_floor ((x) + 0.5))

        fUsePer = 1.0 / fRate;

        if (fUsePer < 3.0)
                fAmorCoeff = 1.0;
        else if (fUsePer < 5.0)
                fAmorCoeff = 1.5;
        else if (fUsePer <= 6.0)
                fAmorCoeff = 2.0;
        else
                fAmorCoeff = 2.5;

        fRate *= fAmorCoeff;
        fNRate = Round ( yearfrac( nDate, nFirstPer, nBase ) * fRate *
			 fCost, 0 );
        fCost -= fNRate;
        fRest = fCost - fRestVal;

        for ( n = 0 ; n < nPer ; n++ ) {
                fNRate = Round ( fRate * fCost, 0 );
                fRest -= fNRate;

                if ( fRest < 0.0 ) {
                        switch ( nPer - n ) {
			case 0:
			case 1:
				return value_new_float (Round ( fCost * 0.5,
								0 ) );
			default:
				return value_new_float (0.0);
                        }
                }

                fCost -= fNRate;
        }
	return value_new_float (fNRate);
#undef Round
}

/***************************************************************************/

GnmValue *
get_amorlinc (gnm_float fCost, GDate *nDate, GDate *nFirstPer,
	      gnm_float fRestVal, gint nPer, gnm_float fRate, gint nBase)
{
        gnm_float fOneRate          = fCost * fRate;
        gnm_float fCostDelta        = fCost - fRestVal;
        gnm_float f0Rate            = yearfrac ( nDate, nFirstPer, nBase )
	        * fRate * fCost;
        gint       nNumOfFullPeriods = (fCost - fRestVal - f0Rate) / fOneRate;
	gnm_float result;

        if ( nPer == 0 )
                result = f0Rate;
        else if( nPer <= nNumOfFullPeriods )
                result = fOneRate;
        else if( nPer == nNumOfFullPeriods + 1 )
                result = fCostDelta - fOneRate * nNumOfFullPeriods - f0Rate;
        else
                result = 0.0;

	return value_new_float ( result );
}

/***************************************************************************/

GnmValue *	   get_yieldmat  (GDate *nSettle, GDate *nMat, GDate *nIssue,
			  gnm_float fRate, gnm_float fPrice, gint nBase)
{
        gnm_float   fIssMat = yearfrac ( nIssue, nMat, nBase );
        gnm_float   fIssSet = yearfrac ( nIssue, nSettle, nBase );
        gnm_float   fSetMat = yearfrac ( nSettle, nMat, nBase );
        gnm_float   y       = 1.0 + fIssMat * fRate;

        y /= fPrice / 100.0 + fIssSet * fRate;
        y--;
        y /= fSetMat;

        return value_new_float ( y );
}

/***************************************************************************/

GnmValue *
get_duration  (GDate *nSettle, GDate *nMat, gnm_float fCoup,
	       gnm_float fYield, gint nFreq, gint nBase,
	       gnm_float fNumOfCoups)
{
        return value_new_float ( Duration (nSettle, nMat, fCoup, fYield, nFreq,
					   nBase, fNumOfCoups) );
}

/***************************************************************************/

GnmValue *
get_mduration (GDate *nSettle, GDate *nMat, gnm_float fCoup,
	       gnm_float fYield, gint nFreq, gint nBase,
	       gnm_float fNumOfCoups)
{
	gnm_float fRet = Duration (nSettle, nMat, fCoup, fYield, nFreq, nBase,
				    fNumOfCoups);

	fRet /= 1.0 + ( fYield / (gnm_float) nFreq );

        return value_new_float ( fRet );
}

/***************************************************************************/

GnmValue *
get_cumprinc  (gnm_float fRate, gint nNumPeriods, gnm_float fVal,
	       gint nStart, gint nEnd, gint nPayType)
{
        gnm_float fRmz, fKapZ;
	gint       i;

        fRmz = GetRmz ( fRate, nNumPeriods, fVal, 0.0, nPayType );

        fKapZ = 0.0;

	if ( nStart == 1 ) {
                if ( nPayType <= 0 )
                        fKapZ = fRmz + fVal * fRate;
                else
                        fKapZ = fRmz;

		nStart++;
        }

	for ( i = nStart ; i <= nEnd ; i++ ) {
                if ( nPayType > 0 )
                        fKapZ += fRmz - ( GetZw ( fRate, ( i - 2 ), fRmz,
						  fVal, 1 ) - fRmz ) * fRate;
                else
                        fKapZ += fRmz - GetZw( fRate, ( i - 1 ), fRmz, fVal,
					       0 ) * fRate;
        }

	return value_new_float ( fKapZ );
}

/***************************************************************************/

GnmValue *
get_cumipmt (gnm_float fRate, gint nNumPeriods, gnm_float fVal,
	     gint nStart, gint nEnd, gint nPayType)
{
        gnm_float fRmz, fZinsZ;
	gint       i;

        fRmz = GetRmz ( fRate, nNumPeriods, fVal, 0.0, nPayType );

        fZinsZ = 0.0;

	if ( nStart == 1 ) {
                if ( nPayType <= 0 )
                        fZinsZ = -fVal;

		nStart++;
        }

	for ( i = nStart ; i <= nEnd ; i++ ) {
                if ( nPayType > 0 )
                        fZinsZ += GetZw ( fRate, ( i - 2 ), fRmz, fVal, 1 )
				- fRmz;
                else
                        fZinsZ += GetZw ( fRate, ( i - 1 ), fRmz, fVal, 0 );
        }

        fZinsZ *= fRate;

	return value_new_float ( fZinsZ );
}

/***************************************************************************/

/*
 *
 *  Original source of the following functions (ScGetGDA, ScInterVDB, and
 *  get_vdb) is the OpenOffice version 1.0, `sc/source/core/tool/interpr2.cxx'.
 *
 *  RCSfile: interpr2.cxx,v
 *
 *  Revision: 1.11
 *
 *  last change: Author: er  Date: 2001/03/15 21:31:13
 *
 */

static gnm_float
ScGetGDA (gnm_float fWert, gnm_float fRest, gnm_float fDauer,
	  gnm_float fPeriode, gnm_float fFaktor)
{
        gnm_float fGda, fZins, fAlterWert, fNeuerWert;  /* FIXME: translate? */

        fZins = fFaktor / fDauer;
        if (fZins >= 1.0) {
                fZins = 1.0;
                if (fPeriode == 1.0)
                        fAlterWert = fWert;
                else
                        fAlterWert = 0.0;
        } else
                fAlterWert = fWert * gnm_pow (1.0 - fZins, fPeriode - 1.0);
        fNeuerWert = fWert * gnm_pow (1.0 - fZins, fPeriode);

        if (fNeuerWert < fRest)
                fGda = fAlterWert - fRest;
        else
                fGda = fAlterWert - fNeuerWert;
        if (fGda < 0.0)
                fGda = 0.0;
        return fGda;
}

static gnm_float
ScInterVDB (gnm_float cost, gnm_float salvage, gnm_float life,
	    gnm_float life1, gnm_float period, gnm_float factor)
{
        gnm_float fVdb       = 0;
        gnm_float fIntEnd    = gnm_ceil (period);
        int        nLoopEnd   = fIntEnd;

        gnm_float fTerm, fLia;
        gnm_float fRestwert  = cost - salvage;
        gboolean   bNowLia    = FALSE;

        gnm_float fGda;
        int        i;

        fLia = 0;
        for ( i = 1; i <= nLoopEnd; i++ ) {
                if (!bNowLia) {
                        fGda = ScGetGDA (cost, salvage, life, i, factor);
                        fLia = fRestwert / (life1 - (gnm_float) (i - 1));

                        if (fLia > fGda) {
                                fTerm   = fLia;
                                bNowLia = TRUE;
                        } else {
                                fTerm      = fGda;
                                fRestwert -= fGda;
                        }
                } else
                        fTerm = fLia;

                if ( i == nLoopEnd)
                        fTerm *= ( period + 1.0 - fIntEnd );

                fVdb += fTerm;
        }
        return fVdb;
}

GnmValue *
get_vdb (gnm_float cost, gnm_float salvage, gnm_float life,
	 gnm_float start_period, gnm_float end_period, gnm_float factor,
	 gboolean flag)
{
	gnm_float fVdb;
	gnm_float fIntStart = gnm_floor (start_period);
	gnm_float fIntEnd   = gnm_ceil (end_period);
	int        i;
	int        nLoopStart = (int) fIntStart;
	int        nLoopEnd   = (int) fIntEnd;

	fVdb      = 0.0;

	if ( flag ) {
		for (i = nLoopStart + 1; i <= nLoopEnd; i++) {
			gnm_float fTerm;

			fTerm = ScGetGDA (cost, salvage, life, i, factor);
			if ( i == nLoopStart+1 )
				fTerm *= ( MIN( end_period, fIntStart + 1.0 )
					   - start_period );
			else if ( i == nLoopEnd )
				fTerm *= ( end_period + 1.0 - fIntEnd );
			fVdb += fTerm;
		}
	} else {
		gnm_float life1 = life;
		gnm_float fPart;

		if ( start_period != gnm_floor (start_period) )
			if (factor > 1) {
				if (start_period >= life / 2) {
					fPart        = start_period - life / 2;
					start_period = life / 2;
					end_period  -= fPart;
					life1       += 1;
				}
			}

		cost -= ScInterVDB (cost, salvage, life, life1, start_period,
				    factor);
		fVdb = ScInterVDB (cost, salvage, life, life - start_period,
				   end_period - start_period, factor);
	}
	return value_new_float (fVdb);
}
