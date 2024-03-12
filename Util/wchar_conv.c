#include "wchar_conv.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

wchar_t* CharToWChar(const char* pstrSrc)
{
 //   ASSERT(pstrSrc);
    int nLen = strlen(pstrSrc)+1;

    //wchar_t* pwstr      = (LPWSTR) malloc ( sizeof( wchar_t )* nLen);
	wchar_t* pwstr      =  malloc ( sizeof( wchar_t )* nLen);
    mbstowcs(pwstr, pstrSrc, nLen);

    return pwstr;
}

wchar_t* CharToWChar2(const char* pstrSrc)
{
 //   ASSERT(pstrSrc);
    int nLen = strlen(pstrSrc)+2;

    //wchar_t* pwstr      = (LPWSTR) malloc ( sizeof( wchar_t )* nLen);
	wchar_t* pwstr      =  malloc ( sizeof( wchar_t )* nLen);
	memset(pwstr, 0, nLen);
    mbstowcs(pwstr, pstrSrc, nLen-1);
	pwstr[nLen-1] = NULL;

    return pwstr;
}

char* WCharToChar(const wchar_t* pwstrSrc)
{
    //ASSERT(pwstrSrc);

//#if defined _DEBUG
    int len = (wcslen(pwstrSrc) + 1)*2;
    char* pstr      = (char*) malloc ( sizeof( char) * len);

    WideCharToMultiByte( 949, 0, pwstrSrc, -1, pstr, len, NULL, NULL);
//#else
/*
    int nLen = wcslen(pwstrSrc);

    char* pstr      = (char*) malloc ( sizeof( char) * nLen + 1);
    wcstombs(pstr, pwstrSrc, nLen+1);
*/
//#endif

    return pstr;
}
