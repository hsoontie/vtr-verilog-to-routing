%{
/*
Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
*/


#include <stdio.h>
#include <string.h>
#include "odin_types.h"
#include "odin_globals.h"
#include "verilog_bison.h"
#include "vtr_util.h"
#include <string>
#include <algorithm>
#include "string.h"

/* the define below helps with watching the parser go token by token */
#define PRINT_TOKEN {printf("%d %s\n", yylineno, yytext);}
#define UNSUPPORTED_TOKEN {printf("Unsuported token::"); PRINT_TOKEN }
#define MP {if (to_view_parse) PRINT_TOKEN}

%}

%s COMMENT

%option noyywrap
%option nounput
%option noinput
%option never-interactive
%option nounistd
%option nodefault
%option yylineno

vBIN [Bb][_]*[ZzXx0-1][ZzXx0-1_]*
vOCT [Oo][_]*[ZzXx0-7][ZzXx0-7_]*
vDEC [Dd][_]*[[:digit:]][[:digit:]_]*
vHEX [Hh][_]*[ZzXx[:xdigit:]][ZzXx[:xdigit:]_]*
vINT [[:digit:]][[:digit:]_]*
vWORD [[:alpha:]_][[:alnum:]_$]*
vSTRING ["][^"]*["]  

	/**
	* add \" to vPUNCT when support is added for strings
	* update the bison file also when changing this 
	*/
vPUNCT 	[\?\:\|\^\&\<\>\-\*\/\%\(\)\{\}\[\]\~\!\;\#\,\.\@\=\+]

%%

<INITIAL>`timescale.*$              { MP; continue;}

	/*	Keywords	*/
<INITIAL>"always"			{ MP; return vALWAYS;}
<INITIAL>"and"				{ MP; return vAND;}
<INITIAL>"assign"			{ MP; return vASSIGN;}
<INITIAL>"begin"			{ MP; return vBEGIN;}
<INITIAL>"case"				{ MP; return vCASE;}
<INITIAL>"default"			{ MP; return vDEFAULT;}
<INITIAL>"defparam"			{ MP; return vDEFPARAM;}
<INITIAL>"else"				{ MP; return vELSE;}
<INITIAL>"end"				{ MP; return vEND;}
<INITIAL>"endcase"			{ MP; return vENDCASE;}
<INITIAL>"endfunction"		{ MP; return vENDFUNCTION;}
<INITIAL>"endgenerate"		{ MP; return vENDGENERATE;}
<INITIAL>"endmodule"		{ MP; return vENDMODULE;}
<INITIAL>"endspecify"		{ MP; return vENDSPECIFY;}
<INITIAL>"for"				{ MP; return vFOR;}
<INITIAL>"if"				{ MP; return vIF;}
<INITIAL>"initial"			{ MP; return vINITIAL;}
<INITIAL>"inout"			{ MP; return vINOUT;}
<INITIAL>"input"			{ MP; return vINPUT;}
<INITIAL>"integer"			{ MP; return vINTEGER;}
<INITIAL>"generate"			{ MP; return vGENERATE;}
<INITIAL>"genvar"			{ MP; return vGENVAR;}
<INITIAL>"module"			{ MP; return vMODULE;}
<INITIAL>"function"			{ MP; return vFUNCTION;}
<INITIAL>"nand"				{ MP; return vNAND;}
<INITIAL>"negedge"			{ MP; return vNEGEDGE;}
<INITIAL>"nor"				{ MP; return vNOR;}
<INITIAL>"not"				{ MP; return vNOT;}
<INITIAL>"or"				{ MP; return vOR;}
<INITIAL>"output"			{ MP; return vOUTPUT;}
<INITIAL>"parameter"		{ MP; return vPARAMETER;}
<INITIAL>"localparam"		{ MP; return vLOCALPARAM;}
<INITIAL>"posedge"			{ MP; return vPOSEDGE;}
<INITIAL>"reg"				{ MP; return vREG;}
<INITIAL>"signed"			{ MP; return vSIGNED;}
<INITIAL>"specify"			{ MP; return vSPECIFY;}
<INITIAL>"while"			{ MP; return vWHILE;}
<INITIAL>"wire"				{ MP; return vWIRE;}
<INITIAL>"tri"				{ MP; return vWIRE;}
<INITIAL>"tri0"				{ MP; return vWIRE;}
<INITIAL>"tri1"				{ MP; return vWIRE;}
<INITIAL>"xnor"				{ MP; return vXNOR;}
<INITIAL>"xor"				{ MP; return vXOR;}
<INITIAL>"macromodule"		{ MP; return vMODULE;}
<INITIAL>"specparam"		{ MP; return vSPECPARAM;}


	/*	unsupported Keywords	*/
<INITIAL>"automatic"		{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"buf"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"casex"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"casez"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"disable"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"edge"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"endtask"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"scalared"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"bufif0"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"bufif1"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"cmos"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"deassign"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"endprimitive"		{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"endtable"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"event"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"force"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"forever"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"fork"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"highz0"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"highz1"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"join"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"large"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"medium"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"nmos"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"notif0"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"notif1"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"pmos"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"primitive"		{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"pull0"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"pull1"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"pulldown"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"pullup"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"rcmos"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"release"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"repeat"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"rnmos"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"rpmos"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"rtran"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"rtranif0"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"rtranif1"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"small"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"strong0"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"strong1"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"supply0"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"supply1"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"table"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"task"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"time"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"tran"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"tranif0"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"tranif1"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"triand"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"trior"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"vectored"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"wait"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"wand"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"weak0"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"weak1"			{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}
<INITIAL>"wor"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}

	/* Operators */
<INITIAL>"**"				{ MP; return voPOWER;}
<INITIAL>"&&"				{ MP; return voANDAND;}
<INITIAL>"||"				{ MP; return voOROR;}
<INITIAL>"<="				{ MP; return voLTE;}
<INITIAL>"=>"				{ MP; return voPAL;}
<INITIAL>">="				{ MP; return voGTE;}
<INITIAL>"<<"				{ MP; return voSLEFT;}
<INITIAL>"<<<"				{ MP; return voSLEFT;}
<INITIAL>">>"				{ MP; return voSRIGHT;}
<INITIAL>">>>"				{ MP; return voASRIGHT;}
<INITIAL>"=="				{ MP; return voEQUAL;}
<INITIAL>"!="				{ MP; return voNOTEQUAL;}
<INITIAL>"==="				{ MP; return voCASEEQUAL;}
<INITIAL>"!=="				{ MP; return voCASENOTEQUAL;}
<INITIAL>"^~"				{ MP; return voXNOR;}
<INITIAL>"~^"				{ MP; return voXNOR;}
<INITIAL>"~&"				{ MP; return voNAND;}
<INITIAL>"~|"				{ MP; return voNOR;}
<INITIAL>"+:"				{ MP; return vPLUS_COLON;}
<INITIAL>"-:"				{ MP; return vMINUS_COLON;}

	/*	unsupported Operators	*/
<INITIAL>"&&&"				{ UNSUPPORTED_TOKEN; return vNOT_SUPPORT;}

	/*	C functions	*/
<INITIAL>"$clog2"			{MP; return vCLOG2;}
<INITIAL>"$unsigned"		{MP; return voUNSIGNED;}
<INITIAL>"$signed"			{MP; return voSIGNED;}

<INITIAL>[\$]{vWORD}		{MP; return vCFUNC;}

	/* Delay */
<INITIAL>#{vINT}					{ MP; yylval.num_value = vtr::strdup(yytext+1); return vDELAY_ID; }

	/* Integers */
<INITIAL>{vINT}						{ MP; yylval.num_value = vtr::strdup(yytext); return vNUMBER; }

	/* Strings */
<INITIAL>{vSTRING}				{ MP; yylval.num_value = vtr::strdup(yytext); return vNUMBER; }

	/* Numbers */
<INITIAL>[[:digit:]]*'[sS]?{vBIN}	{ MP; yylval.num_value = vtr::strdup(yytext); return vNUMBER; }
<INITIAL>[[:digit:]]*'[sS]?{vHEX}	{ MP; yylval.num_value = vtr::strdup(yytext); return vNUMBER; }
<INITIAL>[[:digit:]]*'[sS]?{vOCT}	{ MP; yylval.num_value = vtr::strdup(yytext); return vNUMBER; }
<INITIAL>[[:digit:]]*'[sS]?{vDEC}	{ MP; yylval.num_value = vtr::strdup(yytext); return vNUMBER; }

	/*	operands	*/
<INITIAL>{vWORD}					{ MP; yylval.id_name = vtr::strdup(yytext); return vSYMBOL_ID; }

	/* return operators */
<INITIAL>{vPUNCT}					{ MP; return yytext[0]; }

	/* general stuff */
[[:space:]]+						/* ignore spaces */

	/* catch all */
.									{ UNSUPPORTED_TOKEN; }

%%
