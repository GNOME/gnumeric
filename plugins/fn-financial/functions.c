/*
 * fn-financial.c:  Built in financial functions and functions registration
 *
 * Author:
 *  Vladimir Vuksan (vuksan@veus.hr)
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

/* Some forward declarations */

static char *help_effect = {
	N_("@FUNCTION=EFFECT\n"
	   "@SYNTAX=EFFECT(b1,b2)\n"

	   "@DESCRIPTION=Calculates the effective interest rate from "
	   "given nominal rate."
	   "\n"
	   "@SEEALSO=")
};
	

static Value *
gnumeric_effect (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	double rate;
	int nper;

	rate = value_get_as_double (argv [0]);
	nper = value_get_as_int (argv [1]);

	/* 
		Effective interest rate is
		
		( 1 + r/nper ) ^ nper - 1

	where

		r = nominal interest rate (yearly stated)
		nper = number of periods used for compounding

	*/


	/* Rate or number of periods cannot be negative */
	if ( (rate < 0) || (nper < 0) ){
		*error_string = _("effect - domain error");
		return NULL;
	}

        return value_float  ( pow( (1 + rate/nper) , nper) - 1 );

}

static char *help_sln = {
	N_("@FUNCTION=SLN\n"
	   "@SYNTAX=SLN(cost,salvage value,life)\n"

	   "@DESCRIPTION=Calculates the straight line depriciation for an"
	   "asset based on its cost, salvage value and anticipated life."
	   "\n"
	   "Formula for it is:"
	   "\n"
	   "Depriciation expense = ( cost - salvage value ) / life"
	   "@SEEALSO=")
};


static Value *
gnumeric_sln (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	double cost,salvage_value,life;

	cost = value_get_as_double (argv [0]);
	salvage_value = value_get_as_int (argv [1]);
	life = value_get_as_double (argv [2]);

	/* 
		Straight line depriciation is
	       
                                         cost - salvage_value
		depriciation expense = ----------------------------
		                            life (in years)

	where

		cost = cost of an asset when acquired (market value)
		salvage_value = amount you get when asset sold at the end of life
		life = anticipated life of an asset

	*/

	/* Life of an asset cannot be negative */

	if ( life < 0 ){
		*error_string = _("sln - domain error");
		return NULL;
	}


        return value_float  ( (cost - salvage_value) / life ) ;

}

static char *help_syd = {
	N_("@FUNCTION=SYD\n"
	   "@SYNTAX=SYD(cost,salvage value,life,period)\n"

	   "@DESCRIPTION=Calculates the sum-of-years digits depriciation for an"
	   "asset based on its cost, salvage value, anticipated life and a"
	   "particular period."
	   "\n"
	   "Formula for it is:"
	   "\n"
	   "Depriciation expense = ( cost - salvage value ) * (life-period+1) * 2 / life * (life + 1)"
	   "@SEEALSO=")
};

static Value *
gnumeric_syd (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	double cost,salvage_value,life,period;

	cost = value_get_as_double (argv [0]);
	salvage_value = value_get_as_int (argv [1]);
	life = value_get_as_double (argv [2]);
	period = value_get_as_double (argv [3]);

        return value_float  ( ( (cost - salvage_value) * (life-period+1) * 2 ) / ( life * (life + 1) )) ;

}



FunctionDefinition finance_functions [] = {
	{ "effect", "ff",    "number1,number2",    &help_effect,   NULL, gnumeric_effect },
	{ "sln", "fff", "cost,salvagevalue,life", &help_sln, NULL, gnumeric_sln },
	{ "syd", "ffff", "cost,salvagevalue,life,period", &help_syd, NULL, gnumeric_syd },
	{ NULL, NULL },
};
