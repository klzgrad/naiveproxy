/* Automatically generated from ./asm/pptok.dat by ./asm/pptok.pl */
/* Do not edit */

enum preproc_token {
    PP_IF                    =   0,
    PP_IF_BOGUS              =   1,
    PP_IFCTX                 =   2,
    PP_IFDEF                 =   3,
    PP_IFDEFALIAS            =   4,
    PP_IFDIFI                =   5,
    PP_IFDIRECTIVE           =   6,
    PP_IFEMPTY               =   7,
    PP_IFENV                 =   8,
    PP_IFFILE                =   9,
    PP_IFID                  =  10,
    PP_IFIDN                 =  11,
    PP_IFIDNI                =  12,
    PP_IFMACRO               =  13,
    PP_IFNUM                 =  14,
    PP_IFSTR                 =  15,
    PP_IFTOKEN               =  16,
    PP_IFUSABLE              =  17,
    PP_IFUSING               =  18,
    PP_IF_COND_19            =  19,
    PP_IF_COND_20            =  20,
    PP_IF_COND_21            =  21,
    PP_IF_COND_22            =  22,
    PP_IF_COND_23            =  23,
    PP_IF_COND_24            =  24,
    PP_IF_COND_25            =  25,
    PP_IF_COND_26            =  26,
    PP_IF_COND_27            =  27,
    PP_IF_COND_28            =  28,
    PP_IF_COND_29            =  29,
    PP_IF_COND_30            =  30,
    PP_IF_COND_31            =  31,
    PP_IFN                   =  32,
    PP_IFN_BOGUS             =  33,
    PP_IFNCTX                =  34,
    PP_IFNDEF                =  35,
    PP_IFNDEFALIAS           =  36,
    PP_IFNDIFI               =  37,
    PP_IFNDIRECTIVE          =  38,
    PP_IFNEMPTY              =  39,
    PP_IFNENV                =  40,
    PP_IFNFILE               =  41,
    PP_IFNID                 =  42,
    PP_IFNIDN                =  43,
    PP_IFNIDNI               =  44,
    PP_IFNMACRO              =  45,
    PP_IFNNUM                =  46,
    PP_IFNSTR                =  47,
    PP_IFNTOKEN              =  48,
    PP_IFNUSABLE             =  49,
    PP_IFNUSING              =  50,
    PP_IFN_COND_19           =  51,
    PP_IFN_COND_20           =  52,
    PP_IFN_COND_21           =  53,
    PP_IFN_COND_22           =  54,
    PP_IFN_COND_23           =  55,
    PP_IFN_COND_24           =  56,
    PP_IFN_COND_25           =  57,
    PP_IFN_COND_26           =  58,
    PP_IFN_COND_27           =  59,
    PP_IFN_COND_28           =  60,
    PP_IFN_COND_29           =  61,
    PP_IFN_COND_30           =  62,
    PP_IFN_COND_31           =  63,
    PP_ELIF                  =  64,
    PP_ELIF_BOGUS            =  65,
    PP_ELIFCTX               =  66,
    PP_ELIFDEF               =  67,
    PP_ELIFDEFALIAS          =  68,
    PP_ELIFDIFI              =  69,
    PP_ELIFDIRECTIVE         =  70,
    PP_ELIFEMPTY             =  71,
    PP_ELIFENV               =  72,
    PP_ELIFFILE              =  73,
    PP_ELIFID                =  74,
    PP_ELIFIDN               =  75,
    PP_ELIFIDNI              =  76,
    PP_ELIFMACRO             =  77,
    PP_ELIFNUM               =  78,
    PP_ELIFSTR               =  79,
    PP_ELIFTOKEN             =  80,
    PP_ELIFUSABLE            =  81,
    PP_ELIFUSING             =  82,
    PP_ELIF_COND_19          =  83,
    PP_ELIF_COND_20          =  84,
    PP_ELIF_COND_21          =  85,
    PP_ELIF_COND_22          =  86,
    PP_ELIF_COND_23          =  87,
    PP_ELIF_COND_24          =  88,
    PP_ELIF_COND_25          =  89,
    PP_ELIF_COND_26          =  90,
    PP_ELIF_COND_27          =  91,
    PP_ELIF_COND_28          =  92,
    PP_ELIF_COND_29          =  93,
    PP_ELIF_COND_30          =  94,
    PP_ELIF_COND_31          =  95,
    PP_ELIFN                 =  96,
    PP_ELIFN_BOGUS           =  97,
    PP_ELIFNCTX              =  98,
    PP_ELIFNDEF              =  99,
    PP_ELIFNDEFALIAS         = 100,
    PP_ELIFNDIFI             = 101,
    PP_ELIFNDIRECTIVE        = 102,
    PP_ELIFNEMPTY            = 103,
    PP_ELIFNENV              = 104,
    PP_ELIFNFILE             = 105,
    PP_ELIFNID               = 106,
    PP_ELIFNIDN              = 107,
    PP_ELIFNIDNI             = 108,
    PP_ELIFNMACRO            = 109,
    PP_ELIFNNUM              = 110,
    PP_ELIFNSTR              = 111,
    PP_ELIFNTOKEN            = 112,
    PP_ELIFNUSABLE           = 113,
    PP_ELIFNUSING            = 114,
    PP_ELIFN_COND_19         = 115,
    PP_ELIFN_COND_20         = 116,
    PP_ELIFN_COND_21         = 117,
    PP_ELIFN_COND_22         = 118,
    PP_ELIFN_COND_23         = 119,
    PP_ELIFN_COND_24         = 120,
    PP_ELIFN_COND_25         = 121,
    PP_ELIFN_COND_26         = 122,
    PP_ELIFN_COND_27         = 123,
    PP_ELIFN_COND_28         = 124,
    PP_ELIFN_COND_29         = 125,
    PP_ELIFN_COND_30         = 126,
    PP_ELIFN_COND_31         = 127,
    PP_ALIASES               = 128,
    PP_ARG                   = 129,
    PP_CLEAR                 = 130,
    PP_DEPEND                = 131,
    PP_ELSE                  = 132,
    PP_ENDIF                 = 133,
    PP_ENDM                  = 134,
    PP_ENDMACRO              = 135,
    PP_ENDREP                = 136,
    PP_ERROR                 = 137,
    PP_EXITMACRO             = 138,
    PP_EXITREP               = 139,
    PP_FATAL                 = 140,
    PP_INCLUDE               = 141,
    PP_LINE                  = 142,
    PP_LOCAL                 = 143,
    PP_NOTE                  = 144,
    PP_NULL                  = 145,
    PP_POP                   = 146,
    PP_PRAGMA                = 147,
    PP_PUSH                  = 148,
    PP_REP                   = 149,
    PP_REPL                  = 150,
    PP_REQUIRE               = 151,
    PP_ROTATE                = 152,
    PP_STACKSIZE             = 153,
    PP_UNDEF                 = 154,
    PP_UNDEFALIAS            = 155,
    PP_USE                   = 156,
    PP_WARNING               = 157,
    PP_ASSIGN                = 158,
    PP_IASSIGN               = 159,
    PP_DEFALIAS              = 160,
    PP_IDEFALIAS             = 161,
    PP_DEFINE                = 162,
    PP_IDEFINE               = 163,
    PP_DEFSTR                = 164,
    PP_IDEFSTR               = 165,
    PP_DEFTOK                = 166,
    PP_IDEFTOK               = 167,
    PP_MACRO                 = 168,
    PP_IMACRO                = 169,
    PP_PATHSEARCH            = 170,
    PP_IPATHSEARCH           = 171,
    PP_RMACRO                = 172,
    PP_IRMACRO               = 173,
    PP_STRCAT                = 174,
    PP_ISTRCAT               = 175,
    PP_STRLEN                = 176,
    PP_ISTRLEN               = 177,
    PP_SUBSTR                = 178,
    PP_ISUBSTR               = 179,
    PP_XDEFINE               = 180,
    PP_IXDEFINE              = 181,
    PP_UNMACRO               = 182,
    PP_UNIMACRO              = 183,
    PP_count                 = 184,
    PP_invalid               =  -1
};

#define PP_COND(x)     ((x) & 0x1f)
#define PP_IS_COND(x)  ((unsigned int)(x) < PP_ALIASES)
#define PP_COND_NEGATIVE(x) (!!((x) & 0x20))

#define PP_HAS_CASE(x) ((x) >= PP_ASSIGN)
#define PP_INSENSITIVE(x) ((x) & 1)
#define PP_TOKLEN_MAX 15

#define CASE_PP_IF \
	case PP_IF:\
	case PP_IF_BOGUS:\
	case PP_IFCTX:\
	case PP_IFDEF:\
	case PP_IFDEFALIAS:\
	case PP_IFDIFI:\
	case PP_IFDIRECTIVE:\
	case PP_IFEMPTY:\
	case PP_IFENV:\
	case PP_IFFILE:\
	case PP_IFID:\
	case PP_IFIDN:\
	case PP_IFIDNI:\
	case PP_IFMACRO:\
	case PP_IFNUM:\
	case PP_IFSTR:\
	case PP_IFTOKEN:\
	case PP_IFUSABLE:\
	case PP_IFUSING:\
	case PP_IF_COND_19:\
	case PP_IF_COND_20:\
	case PP_IF_COND_21:\
	case PP_IF_COND_22:\
	case PP_IF_COND_23:\
	case PP_IF_COND_24:\
	case PP_IF_COND_25:\
	case PP_IF_COND_26:\
	case PP_IF_COND_27:\
	case PP_IF_COND_28:\
	case PP_IF_COND_29:\
	case PP_IF_COND_30:\
	case PP_IF_COND_31:\
	case PP_IFN:\
	case PP_IFN_BOGUS:\
	case PP_IFNCTX:\
	case PP_IFNDEF:\
	case PP_IFNDEFALIAS:\
	case PP_IFNDIFI:\
	case PP_IFNDIRECTIVE:\
	case PP_IFNEMPTY:\
	case PP_IFNENV:\
	case PP_IFNFILE:\
	case PP_IFNID:\
	case PP_IFNIDN:\
	case PP_IFNIDNI:\
	case PP_IFNMACRO:\
	case PP_IFNNUM:\
	case PP_IFNSTR:\
	case PP_IFNTOKEN:\
	case PP_IFNUSABLE:\
	case PP_IFNUSING:\
	case PP_IFN_COND_19:\
	case PP_IFN_COND_20:\
	case PP_IFN_COND_21:\
	case PP_IFN_COND_22:\
	case PP_IFN_COND_23:\
	case PP_IFN_COND_24:\
	case PP_IFN_COND_25:\
	case PP_IFN_COND_26:\
	case PP_IFN_COND_27:\
	case PP_IFN_COND_28:\
	case PP_IFN_COND_29:\
	case PP_IFN_COND_30:\
	case PP_IFN_COND_31
#define CASE_PP_ELIF \
	case PP_ELIF:\
	case PP_ELIF_BOGUS:\
	case PP_ELIFCTX:\
	case PP_ELIFDEF:\
	case PP_ELIFDEFALIAS:\
	case PP_ELIFDIFI:\
	case PP_ELIFDIRECTIVE:\
	case PP_ELIFEMPTY:\
	case PP_ELIFENV:\
	case PP_ELIFFILE:\
	case PP_ELIFID:\
	case PP_ELIFIDN:\
	case PP_ELIFIDNI:\
	case PP_ELIFMACRO:\
	case PP_ELIFNUM:\
	case PP_ELIFSTR:\
	case PP_ELIFTOKEN:\
	case PP_ELIFUSABLE:\
	case PP_ELIFUSING:\
	case PP_ELIF_COND_19:\
	case PP_ELIF_COND_20:\
	case PP_ELIF_COND_21:\
	case PP_ELIF_COND_22:\
	case PP_ELIF_COND_23:\
	case PP_ELIF_COND_24:\
	case PP_ELIF_COND_25:\
	case PP_ELIF_COND_26:\
	case PP_ELIF_COND_27:\
	case PP_ELIF_COND_28:\
	case PP_ELIF_COND_29:\
	case PP_ELIF_COND_30:\
	case PP_ELIF_COND_31:\
	case PP_ELIFN:\
	case PP_ELIFN_BOGUS:\
	case PP_ELIFNCTX:\
	case PP_ELIFNDEF:\
	case PP_ELIFNDEFALIAS:\
	case PP_ELIFNDIFI:\
	case PP_ELIFNDIRECTIVE:\
	case PP_ELIFNEMPTY:\
	case PP_ELIFNENV:\
	case PP_ELIFNFILE:\
	case PP_ELIFNID:\
	case PP_ELIFNIDN:\
	case PP_ELIFNIDNI:\
	case PP_ELIFNMACRO:\
	case PP_ELIFNNUM:\
	case PP_ELIFNSTR:\
	case PP_ELIFNTOKEN:\
	case PP_ELIFNUSABLE:\
	case PP_ELIFNUSING:\
	case PP_ELIFN_COND_19:\
	case PP_ELIFN_COND_20:\
	case PP_ELIFN_COND_21:\
	case PP_ELIFN_COND_22:\
	case PP_ELIFN_COND_23:\
	case PP_ELIFN_COND_24:\
	case PP_ELIFN_COND_25:\
	case PP_ELIFN_COND_26:\
	case PP_ELIFN_COND_27:\
	case PP_ELIFN_COND_28:\
	case PP_ELIFN_COND_29:\
	case PP_ELIFN_COND_30:\
	case PP_ELIFN_COND_31
