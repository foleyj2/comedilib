%option noyywrap
%option nounput
%option yylineno
%option reentrant

%{

/*
    lib/calib_lex.l
    code for parsing calibration file, generated by flex

    Copyright (C) 2003 Frank Mori Hess <fmhess@users.sourceforge.net>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation, version 2.1
    of the License.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
    USA.
*/

#include <string.h>
#include "libinternal.h"
#include "calib_yacc.h"

#define YY_NO_INPUT

%}

%x COMMENT
%x STRING

%%

<STRING,INITIAL>\n

"#" { BEGIN(COMMENT); }
<COMMENT>\n {BEGIN(INITIAL); }
<COMMENT>.

\" { BEGIN(STRING); }
<STRING>[^\"]*\" {
	if(strlen(calib_yyget_text(yyscanner)) > 0)
		calib_yyget_text(yyscanner)[strlen(calib_yyget_text(yyscanner)) - 1] = 0;
	calib_lvalp->sval = calib_yyget_text(yyscanner);
	BEGIN(INITIAL);
	return ( T_STRING );
}

driver_name	{ return ( T_DRIVER_NAME ); }
board_name	{ return ( T_BOARD_NAME ); }
calibrations	{ return ( T_CALIBRATIONS ); }
subdevice	{ return ( T_SUBDEVICE); }
channels	{ return (T_CHANNELS); }
ranges	{ return ( T_RANGES ); }
arefs	{ return ( T_AREFS ); }
caldacs	{ return ( T_CALDACS ); }
channel	{ return ( T_CHANNEL ); }
value	{ return ( T_VALUE ); }
coefficients	{return (T_COEFFICIENTS);}
expansion_origin	{return (T_EXPANSION_ORIGIN);}
softcal_to_phys	{return T_SOFTCAL_TO_PHYS;}
softcal_from_phys	{return T_SOFTCAL_FROM_PHYS;}
=>	{ return ( T_ASSIGN ); };

[+-]?(0[xX])?[0-9a-fA-F]+ {calib_lvalp->ival = strtol(calib_yyget_text(yyscanner), NULL, 0);
	return( T_NUMBER ); }

[+-]?(([0-9]+\.?[0-9]*)|([0-9]*\.?[0-9]+))([eE][+-]?[0-9]+)? { calib_lvalp->dval = strtod(calib_yyget_text(yyscanner), 0);
	return( T_FLOAT ); }

[ \t]

.	{return(calib_yyget_text(yyscanner)[0]);}


%%

