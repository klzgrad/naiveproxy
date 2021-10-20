/* Automatically generated from ./asm/pptok.dat by ./asm/pptok.pl */
/* Do not edit */

enum preproc_token {
    PP_IF                    =   0,
    PP_IFCTX                 =   1,
    PP_IFDEF                 =   2,
    PP_IFDEFALIAS            =   3,
    PP_IFEMPTY               =   4,
    PP_IFENV                 =   5,
    PP_IFID                  =   6,
    PP_IFIDN                 =   7,
    PP_IFIDNI                =   8,
    PP_IFMACRO               =   9,
    PP_IFNUM                 =  10,
    PP_IFSTR                 =  11,
    PP_IFTOKEN               =  12,
    PP_IFUSABLE              =  13,
    PP_IFUSING               =  14,
    PP_IF_COND_15            =  15,
    PP_IFN                   =  16,
    PP_IFNCTX                =  17,
    PP_IFNDEF                =  18,
    PP_IFNDEFALIAS           =  19,
    PP_IFNEMPTY              =  20,
    PP_IFNENV                =  21,
    PP_IFNID                 =  22,
    PP_IFNIDN                =  23,
    PP_IFNIDNI               =  24,
    PP_IFNMACRO              =  25,
    PP_IFNNUM                =  26,
    PP_IFNSTR                =  27,
    PP_IFNTOKEN              =  28,
    PP_IFNUSABLE             =  29,
    PP_IFNUSING              =  30,
    PP_IFN_COND_15           =  31,
    PP_ELIF                  =  32,
    PP_ELIFCTX               =  33,
    PP_ELIFDEF               =  34,
    PP_ELIFDEFALIAS          =  35,
    PP_ELIFEMPTY             =  36,
    PP_ELIFENV               =  37,
    PP_ELIFID                =  38,
    PP_ELIFIDN               =  39,
    PP_ELIFIDNI              =  40,
    PP_ELIFMACRO             =  41,
    PP_ELIFNUM               =  42,
    PP_ELIFSTR               =  43,
    PP_ELIFTOKEN             =  44,
    PP_ELIFUSABLE            =  45,
    PP_ELIFUSING             =  46,
    PP_ELIF_COND_15          =  47,
    PP_ELIFN                 =  48,
    PP_ELIFNCTX              =  49,
    PP_ELIFNDEF              =  50,
    PP_ELIFNDEFALIAS         =  51,
    PP_ELIFNEMPTY            =  52,
    PP_ELIFNENV              =  53,
    PP_ELIFNID               =  54,
    PP_ELIFNIDN              =  55,
    PP_ELIFNIDNI             =  56,
    PP_ELIFNMACRO            =  57,
    PP_ELIFNNUM              =  58,
    PP_ELIFNSTR              =  59,
    PP_ELIFNTOKEN            =  60,
    PP_ELIFNUSABLE           =  61,
    PP_ELIFNUSING            =  62,
    PP_ELIFN_COND_15         =  63,
    PP_ALIASES               =  64,
    PP_ARG                   =  65,
    PP_CLEAR                 =  66,
    PP_DEPEND                =  67,
    PP_ELSE                  =  68,
    PP_ENDIF                 =  69,
    PP_ENDM                  =  70,
    PP_ENDMACRO              =  71,
    PP_ENDREP                =  72,
    PP_ERROR                 =  73,
    PP_EXITMACRO             =  74,
    PP_EXITREP               =  75,
    PP_FATAL                 =  76,
    PP_INCLUDE               =  77,
    PP_LINE                  =  78,
    PP_LOCAL                 =  79,
    PP_NULL                  =  80,
    PP_POP                   =  81,
    PP_PRAGMA                =  82,
    PP_PUSH                  =  83,
    PP_REP                   =  84,
    PP_REPL                  =  85,
    PP_ROTATE                =  86,
    PP_STACKSIZE             =  87,
    PP_UNDEF                 =  88,
    PP_UNDEFALIAS            =  89,
    PP_USE                   =  90,
    PP_WARNING               =  91,
    PP_ASSIGN                =  92,
    PP_IASSIGN               =  93,
    PP_DEFALIAS              =  94,
    PP_IDEFALIAS             =  95,
    PP_DEFINE                =  96,
    PP_IDEFINE               =  97,
    PP_DEFSTR                =  98,
    PP_IDEFSTR               =  99,
    PP_DEFTOK                = 100,
    PP_IDEFTOK               = 101,
    PP_MACRO                 = 102,
    PP_IMACRO                = 103,
    PP_PATHSEARCH            = 104,
    PP_IPATHSEARCH           = 105,
    PP_RMACRO                = 106,
    PP_IRMACRO               = 107,
    PP_STRCAT                = 108,
    PP_ISTRCAT               = 109,
    PP_STRLEN                = 110,
    PP_ISTRLEN               = 111,
    PP_SUBSTR                = 112,
    PP_ISUBSTR               = 113,
    PP_XDEFINE               = 114,
    PP_IXDEFINE              = 115,
    PP_UNMACRO               = 116,
    PP_UNIMACRO              = 117,
    PP_INVALID               =  -1
};

#define PP_COND(x)     ((x) & 0xf)
#define PP_IS_COND(x)  ((unsigned int)(x) < PP_ALIASES)
#define PP_COND_NEGATIVE(x) (!!((x) & 0x10))

#define PP_HAS_CASE(x) ((x) >= PP_ASSIGN)
#define PP_INSENSITIVE(x) ((x) & 1)

#define CASE_PP_IF \
	case PP_IF:\
	case PP_IFCTX:\
	case PP_IFDEF:\
	case PP_IFDEFALIAS:\
	case PP_IFEMPTY:\
	case PP_IFENV:\
	case PP_IFID:\
	case PP_IFIDN:\
	case PP_IFIDNI:\
	case PP_IFMACRO:\
	case PP_IFNUM:\
	case PP_IFSTR:\
	case PP_IFTOKEN:\
	case PP_IFUSABLE:\
	case PP_IFUSING:\
	case PP_IF_COND_15:\
	case PP_IFN:\
	case PP_IFNCTX:\
	case PP_IFNDEF:\
	case PP_IFNDEFALIAS:\
	case PP_IFNEMPTY:\
	case PP_IFNENV:\
	case PP_IFNID:\
	case PP_IFNIDN:\
	case PP_IFNIDNI:\
	case PP_IFNMACRO:\
	case PP_IFNNUM:\
	case PP_IFNSTR:\
	case PP_IFNTOKEN:\
	case PP_IFNUSABLE:\
	case PP_IFNUSING:\
	case PP_IFN_COND_15
#define CASE_PP_ELIF \
	case PP_ELIF:\
	case PP_ELIFCTX:\
	case PP_ELIFDEF:\
	case PP_ELIFDEFALIAS:\
	case PP_ELIFEMPTY:\
	case PP_ELIFENV:\
	case PP_ELIFID:\
	case PP_ELIFIDN:\
	case PP_ELIFIDNI:\
	case PP_ELIFMACRO:\
	case PP_ELIFNUM:\
	case PP_ELIFSTR:\
	case PP_ELIFTOKEN:\
	case PP_ELIFUSABLE:\
	case PP_ELIFUSING:\
	case PP_ELIF_COND_15:\
	case PP_ELIFN:\
	case PP_ELIFNCTX:\
	case PP_ELIFNDEF:\
	case PP_ELIFNDEFALIAS:\
	case PP_ELIFNEMPTY:\
	case PP_ELIFNENV:\
	case PP_ELIFNID:\
	case PP_ELIFNIDN:\
	case PP_ELIFNIDNI:\
	case PP_ELIFNMACRO:\
	case PP_ELIFNNUM:\
	case PP_ELIFNSTR:\
	case PP_ELIFNTOKEN:\
	case PP_ELIFNUSABLE:\
	case PP_ELIFNUSING:\
	case PP_ELIFN_COND_15
