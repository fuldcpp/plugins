#pragma once

/* Everything the plugin can be configured with is reachable from /tr in any
   chat window, but a language code is not something anyone should have to know
   by heart -- so there is a dialog too, reached from the host's plugin list. */

#define VER_MAJOR           1
#define VER_MINOR           0

#define IDD_SETTINGS        101

#define IDC_LANG            1001
#define IDC_BACKEND         1002
#define IDC_APIKEY          1003
#define IDC_REGION          1004
#define IDC_EMAIL           1005
#define IDC_PM              1006
#define IDC_ECHO            1007
#define IDC_STATUS          1008

/* Labels and group boxes need ids of their own so their captions can be written
   at runtime; IDC_STATIC would make them unaddressable. */
#define IDC_GRP_LANG        1020
#define IDC_LBL_LANG        1021
#define IDC_LANGHINT        1022
#define IDC_GRP_SERVICE     1023
#define IDC_LBL_BACKEND     1024
#define IDC_LBL_APIKEY      1025
#define IDC_LBL_REGION      1026
#define IDC_LBL_EMAIL       1027
#define IDC_KEYHINT         1028
#define IDC_LBL_LIMIT       1029
#define IDC_LIMIT           1035

#define IDC_GRP_AUTO        1030
#define IDC_AUTOHUBS        1031
#define IDC_AUTOREMOVE      1032
#define IDC_AUTOHINT        1033
#define IDC_CLEARCACHE      1034
#define IDC_CMDHINT         1036
