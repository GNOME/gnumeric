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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 *  MA  02111-1307  USA
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <datetime.h>
#include <math.h>
#include <value.h>
#include "sc-fin.h"


static gint
GetDiffDate360 (gint nDay1, gint nMonth1, gint nYear1, gint bLeapYear1,
		gint nDay2, gint nMonth2, gint nYear2, gboolean bUSAMethod )
{
        if ( nDay1 == 31 )
                nDay1--;
        else if ( bUSAMethod && ( nMonth1 == 2 &&
				  ( nDay1 == 29 ||
				    ( nDay1 == 28 && !bLeapYear1 ) ) ) )
		nDay1 = 30;

        if ( nDay2 == 31 ) {
                if ( bUSAMethod && nDay1 != 30 ) {
                        /* aDate2 += 1;          -> 1.xx.yyyy */
                        nDay2 = 1;
                        if ( nMonth2 == 12 ) {
                                nYear2++;
                                nMonth2 = 1;
                        } else
                                nMonth2++;
                } else
                        nDay2 = 30;
        }

        return nDay2 + nMonth2 * 30 + nYear2 * 360 - nDay1 - nMonth1 *
		30 - nYear1 * 360;
}


static void
GetDiffParam (GDate *nStartDate, GDate *nEndDate, gint nMode, gint *rYears,
	      gint *rDayDiffPart, gint *rDaysInYear)
{
	gint  nDay1,   nDay2;
	gint  nMonth1, nMonth2;
	gint  nYear1,  nYear2;
	gint  nYears = 0;
	gint  nDayDiff = 0, nDaysInYear = 0;

        if ( g_date_compare (nStartDate, nEndDate) == 1 ) {
                GDate *tmp = nEndDate;
                nEndDate   = nStartDate;
                nStartDate = tmp;
        }

	nDay1   = g_date_get_day   (nStartDate);
	nDay2   = g_date_get_day   (nEndDate);
	nMonth1 = g_date_get_month (nStartDate);
	nMonth2 = g_date_get_month (nEndDate);
	nYear1  = g_date_get_year  (nStartDate);
	nYear2  = g_date_get_year  (nEndDate);

        switch ( nMode ) {
	case 0:                 /* 0=USA (NASD) 30/360 */
	case 4:                 /* 4=Europe 30/360 */
		nDaysInYear = 360;
		nYears      = nYear2 - nYear1;
		nDayDiff    = GetDiffDate360 ( nDay1, nMonth1, nYear1,
					       g_date_is_leap_year ( nYear1 ),
					       nDay2, nMonth2, nYear2,
					       nMode == 0 )
			- nYears * nDaysInYear;
		break;
	case 1:                 /* 1=exact/exact */
		nYears      = nYear2 - nYear1;
		
		nDaysInYear = g_date_is_leap_year ( nYear1 )? 366 : 365;

		if ( nYears && ( nMonth1 > nMonth2 ||
				 ( nMonth1 == nMonth2 && nDay1 > nDay2 ) ) )
			nYears--;

		if ( nYears ) {
			GDate *tmp = g_date_new_dmy (nDay1, nMonth1, nYear2);

			nDayDiff = g_date_days_between (tmp, nEndDate);
			g_free (tmp);
		} else
			nDayDiff = g_date_days_between (nStartDate, nEndDate);
		
		if ( nDayDiff < 0 )
			nDayDiff += nDaysInYear;
		
		break;
	case 2:                 /* 2=exact/360 */
		nDaysInYear = 360;
		nYears      = g_date_days_between ( nStartDate, nEndDate ) /
			nDaysInYear; /* ?? */
		nDayDiff    = g_date_days_between ( nStartDate, nEndDate );
		nDayDiff   %= nDaysInYear;
		break;
	case 3:                 /* 3=exact/365 */
		nDaysInYear = 365;
		nYears      = g_date_days_between ( nStartDate, nEndDate ) /
			nDaysInYear; /* ?? */
		nDayDiff    = g_date_days_between ( nStartDate, nEndDate );
		nDayDiff   %= nDaysInYear;
		break;
	default:
		break;
        }

        *rYears       = nYears;
        *rDayDiffPart = nDayDiff;
        *rDaysInYear  = nDaysInYear;
}


static gnum_float
GetYearFrac (GDate *nStartDate, GDate *nEndDate, gint nMode)
{
	gint nYears, nDayDiff, nDaysInYear;

        GetDiffParam ( nStartDate, nEndDate, nMode, &nYears, &nDayDiff,
		       &nDaysInYear );

        return nYears + (gnum_float) nDayDiff / nDaysInYear;
}

static gnum_float
GetRmz ( gnum_float fZins, gnum_float fZzr, gnum_float fBw, gnum_float fZw,
	 gint nF )
{
        gnum_float fRmz;

        if ( fZins == 0.0 )
                fRmz = ( fBw + fZw ) / fZzr;
        else {
                gnum_float fTerm = pow ( 1.0 + fZins, fZzr );
                if ( nF > 0 )
                        fRmz = ( fZw * fZins / ( fTerm - 1.0 ) + fBw * fZins /
				 ( 1.0 - 1.0 / fTerm ) ) / ( 1.0 + fZins );
                else
                        fRmz = fZw * fZins / ( fTerm - 1.0 ) + fBw * fZins /
				( 1.0 - 1.0 / fTerm );
        }

        return -fRmz;
}

static gnum_float
GetZw ( gnum_float fZins, gnum_float fZzr, gnum_float fRmz, gnum_float fBw,
	gint nF )
{
        gnum_float fZw;

        if ( fZins == 0.0 )
                fZw = fBw + fRmz * fZzr;
        else {
                gnum_float fTerm = pow ( 1.0 + fZins, fZzr );
                if ( nF > 0 )
                        fZw = fBw * fTerm + fRmz * ( 1.0 + fZins ) *
				( fTerm - 1.0 ) / fZins;
                else
                        fZw = fBw * fTerm + fRmz * ( fTerm - 1.0 ) / fZins;
        }

        return -fZw;
}

static gnum_float
Duration (GDate *nSettle, GDate *nMat, gnum_float fCoup, gnum_float fYield,
	  gint nFreq, gint nBase, gnum_float fNumOfCoups)
{
        /* gnum_float  fYearfrac   = GetYearFrac ( nSettle, nMat, nBase ); */
        gnum_float  fDur        = 0.0;
	gnum_float  t, p        = 0.0;

        const gnum_float f100   = 100.0;

        fCoup  *= f100 / (gnum_float) nFreq; /* fCoup is used as cash flow */
        fYield /= nFreq;
        fYield += 1.0;

        for ( t = 1.0 ; t < fNumOfCoups ; t++ )
                fDur += t * ( fCoup ) / pow ( fYield, t );

        fDur += fNumOfCoups * ( fCoup + f100 ) / pow ( fYield, fNumOfCoups );

        for ( t = 1.0 ; t < fNumOfCoups ; t++ )
                p += fCoup / pow ( fYield, t );

        p += ( fCoup + f100 ) / pow ( fYield, fNumOfCoups );

        fDur /= p;
        fDur /= (gnum_float) nFreq;

        return ( fDur );
}

/***************************************************************************/

Value *
get_amordegrc (gnum_float fCost, GDate *nDate, GDate *nFirstPer, 
	       gnum_float fRestVal, gint nPer, gnum_float fRate,
	       gint nBase)
{
        gint       n;
	gnum_float fAmorCoeff, fNRate, fRest, fUsePer;

#define Round(x,y) (floorgnum ((x) + 0.5))

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
        fNRate = Round ( GetYearFrac( nDate, nFirstPer, nBase ) * fRate *
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

Value *
get_amorlinc (gnum_float fCost, GDate *nDate, GDate *nFirstPer, 
	      gnum_float fRestVal, gint nPer, gnum_float fRate, gint nBase)
{
        gnum_float fOneRate          = fCost * fRate;
        gnum_float fCostDelta        = fCost - fRestVal;
        gnum_float f0Rate            = GetYearFrac ( nDate, nFirstPer, nBase )
	        * fRate * fCost;
        gint       nNumOfFullPeriods = (fCost - fRestVal - f0Rate) / fOneRate;
	gnum_float result;

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

Value *    get_yielddisc (GDate *nSettle, GDate *nMat, gnum_float fPrice,
			  gnum_float fRedemp, gint nBase)
{
        gnum_float fRet = 1.0 - fPrice / fRedemp;
	/* FIXME: I think this is bogus stuff. */

	fRet /= GetYearFrac ( nSettle, nMat, nBase );
	fRet /= 0.99795;  /* don't know what this constant means in original */

	return value_new_float ( fRet );
}

/***************************************************************************/

Value *	   get_yieldmat  (GDate *nSettle, GDate *nMat, GDate *nIssue,
			  gnum_float fRate, gnum_float fPrice, gint nBase)
{
        gnum_float   fIssMat = GetYearFrac ( nIssue, nMat, nBase );
        gnum_float   fIssSet = GetYearFrac ( nIssue, nSettle, nBase );
        gnum_float   fSetMat = GetYearFrac ( nSettle, nMat, nBase );
        gnum_float   y       = 1.0 + fIssMat * fRate;

        y /= fPrice / 100.0 + fIssSet * fRate;
        y--;
        y /= fSetMat;

        return value_new_float ( y );
}

/***************************************************************************/

Value *    get_oddlprice (GDate *nSettle, GDate *nMat, GDate *nLastCoup,
			  gnum_float fRate, gnum_float fYield,
			  gnum_float fRedemp, gint nFreq, gint nBase)
{
        gnum_float fDCi  = GetYearFrac ( nLastCoup, nMat, nBase ) * nFreq;
        gnum_float fDSCi = GetYearFrac ( nSettle, nMat, nBase ) *  nFreq;
        gnum_float fAi   = GetYearFrac ( nLastCoup, nSettle, nBase ) * nFreq;

        gnum_float p     = fRedemp + fDCi * 100.0 * fRate / nFreq;
        p /= fDSCi * fYield / nFreq + 1.0;
        p -= fAi * 100.0 * fRate / nFreq;

        return value_new_float ( p );
}

/***************************************************************************/

Value *    get_oddlyield (GDate *nSettle, GDate *nMat, GDate *nLastCoup,
			  gnum_float fRate, gnum_float fPrice,
			  gnum_float fRedemp, gint nFreq, gint nBase)
{
        gnum_float      fDCi  = GetYearFrac ( nLastCoup, nMat, nBase ) * nFreq;
        gnum_float      fDSCi = GetYearFrac ( nSettle, nMat, nBase ) * nFreq;
        gnum_float      fAi   = GetYearFrac ( nLastCoup, nSettle, nBase ) *
	        nFreq;

        gnum_float      y     = fRedemp + fDCi * 100.0 * fRate / nFreq;
        y /= fPrice + fAi * 100.0 * fRate / nFreq;
        y--;
        y *= nFreq / fDSCi;

        return value_new_float ( y );
}

/***************************************************************************/

Value *    get_duration  (GDate *nSettle, GDate *nMat, gnum_float fCoup,
			  gnum_float fYield, gint nFreq, gint nBase,
			  gnum_float fNumOfCoups)
{
        return value_new_float ( Duration (nSettle, nMat, fCoup, fYield, nFreq,
					   nBase, fNumOfCoups) );
}

/***************************************************************************/

Value *    get_mduration (GDate *nSettle, GDate *nMat, gnum_float fCoup,
			  gnum_float fYield, gint nFreq, gint nBase,
			  gnum_float fNumOfCoups)
{
	gnum_float fRet = Duration (nSettle, nMat, fCoup, fYield, nFreq, nBase,
				    fNumOfCoups);

	fRet /= 1.0 + ( fYield / (gnum_float) nFreq );

        return value_new_float ( fRet );
}

/***************************************************************************/

Value *    get_cumprinc  (gnum_float fRate, gint nNumPeriods, gnum_float fVal,
			  gint nStart, gint nEnd, gint nPayType)
{
        gnum_float fRmz, fKapZ;
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

Value *    get_cumipmt   (gnum_float fRate, gint nNumPeriods, gnum_float fVal,
			  gint nStart, gint nEnd, gint nPayType)
{
        gnum_float fRmz, fZinsZ;
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
