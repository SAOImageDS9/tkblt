%{
%}
#include def.tin

#include yesno.tin
#include wcssys.tin
#include skyframe.tin
#include wcsformat.tin
#include numeric.tin
#include sexstr.tin
#include string.tin

%start command

%token CANCEL_
%token CLEAR_
%token CLOSE_
%token COORDINATE_
%token CROSSHAIR_
%token DECR_
%token EXPORT_
%token FILTER_
%token HIDE_
%token INCR_
%token LOAD_
%token NAME_
%token PANTO_
%token PRINT_
%token RADIUS_
%token REGIONS_
%token RETRIEVE_
%token SAVE_
%token SHOW_
%token SIZE_
%token SKY_
%token SKYFORMAT_
%token SORT_
%token SYSTEM_
%token UPDATE_

%token CXC_
%token HLA_

%token XML_
%token VOT_
%token SB_
%token STARBASE_
%token CSV_
%token TSV_

%%

#include yesno.trl
#include wcssys.trl
#include skyframe.trl
#include wcsformat.trl
#include numeric.trl

command : fp 
 | fp {global ds9; if {!$ds9(init)} {YYERROR} else {yyclearin; YYACCEPT}} STRING_
 ;

fp : {if {![FPCmdCheck]} {fp::YYABORT}} fpCmd
 | site {FPCmdRef $1}
 | site {FPCmdRef $1} fpCmd
 ;

fpCmd : CANCEL_ {ProcessCmdCVAR0 ARCancel}
 | CLEAR_ {ProcessCmdCVAR0 FPOff}
 | CLOSE_ {ProcessCmdCVAR0 FPDestroy}
 | COORDINATE_ coordinate
 | CROSSHAIR_ {ProcessCmdCVAR0 TBLCrosshair}
 | EXPORT_ writer STRING_ {TBLCmdSave $3 $2}
 | FILTER_ filter
 | HIDE_ {ProcessCmdCVAR show 0 FPGenerate}
 | NAME_ STRING_ {ProcessCmdCVAR name $2}
 | PANTO_ yesno {ProcessCmdCVAR panto $2}
 | PRINT_ {ProcessCmdCVAR0 TBLCmdPrint}
 | RADIUS_ numeric rformat {TBLCmdSize $2 $3}
 | REGIONS_ {ProcessCmdCVAR0 FPGenerateRegions}
 | RETRIEVE_ {global cvarname; FPApply $cvarname 1}
 | SAVE_ STRING_ {TBLCmdSave $2 VOTWrite}
 | SHOW_ yesno {ProcessCmdCVAR show $2 FPGenerate}
# backward compatibily
 | SIZE_ numeric numeric rformat {TBLCmdSize [expr ($2+$3)/2.] $4}
 | SKY_ skyframe {TBLCmdSkyframe $2}
 | SKYFORMAT_ skyformat {ProcessCmdCVAR skyformat $2}
 | SORT_ sort
 | SYSTEM_ wcssys {TBLCmdSystem $2}
 | UPDATE_ {ProcessCVAR0 TBLUpdate}
 ;

coordinate : numeric numeric {TBLCmdCoord $1 $2 fk5}
 | numeric numeric skyframe {TBLCmdCoord $1 $2 $3}
 | SEXSTR_ SEXSTR_ {TBLCmdCoord $1 $2 fk5}
 | SEXSTR_ SEXSTR_ skyframe {TBLCmdCoord $1 $2 $3}
 ;

filter : LOAD_ STRING_ {TBLCmdFilterLoad $2}
 | STRING_ {ProcessCmdCVAR filter $1 FPTable}
 ;

site : CXC_ {set _ cxc}
 | HLA_ {set _ hla}
 ;

sort : STRING_ {ProcessCmdCVAR sort $1; ProcessCmdCVAR sort,dir "-increasing" FPTable}
 | STRING_ sortDir {ProcessCmdCVAR sort $1; ProcessCmdCVAR sort,dir $2 FPTable}
 ;

sortDir : INCR_ {set _ "-increasing"}
 | DECR_ {set _ "-decreasing"}
 ;

writer : XML_ {set _ VOTWrite}
 | VOT_ {set _ VOTWrite}
 | SB_ {set _ starbase_write}
 | STARBASE_ {set _ starbase_write}
 | CSV_ {set _ TSVWrite}
 | TSV_ {set _ TSVWrite}
 ;

%%

proc fp::yyerror {msg} {
     variable yycnt
     variable yy_current_buffer
     variable index_

     ParserError $msg $yycnt $yy_current_buffer $index_
}