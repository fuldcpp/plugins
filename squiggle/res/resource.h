#pragma once

#define IDD_SETTINGS        101

#define IDC_LANGLIST        1001
#define IDC_PREVIEW         1002
#define IDC_COLOUR          1003
#define IDC_THICKNESS       1004
#define IDC_PERSONAL        1005
#define IDC_AUTOCORRECT     1006
#define IDC_CORRECTIONS     1007
#define IDC_STATUS          1008
#define IDC_LANGHINT        1009
#define IDC_AUTOHINT        1010
#define IDC_OPENFOLDER      1011
#define IDC_UILANG          1012

/* Group boxes and labels need ids of their own so their captions can be
   written at runtime; IDC_STATIC would make them unaddressable. */
#define IDC_GRP_LANG        1020
#define IDC_GRP_UNDERLINE   1021
#define IDC_LBL_COLOUR      1022
#define IDC_LBL_THICKNESS   1023
#define IDC_GRP_PERSONAL    1024
#define IDC_GRP_AUTO        1025
#define IDC_LBL_UILANG      1026
