#include "scandir.h"
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "defines.h"
#include <Aclapi.h>
#include "wchar_conv.h"
#include "report.h"

#define UpChar(ch) ((ch >= _T('a')) && (ch <= _T('z'))?(ch - 32):ch)
#define ConvertChar(ch) ((ch == _T('\\'))?1:UpChar(ch))

static int cmpr(const void *a, const void *b) { 
	//return stricmp(((scan_file_t *)a)->name, ((scan_file_t *)b)->name);
	//return _tcsicmp(((scan_file_t *)a)->name, ((scan_file_t *)b)->name);
	return str_dep_cmp(((scan_file_t *)a)->name, ((scan_file_t *)b)->name);
}

int str_dep_cmp(TCHAR *a, TCHAR *b)
{
#ifdef UNICODE
	unsigned short ca, cb;
#else
	unsigned char ca, cb;
#endif

	ca = *a;
	cb = *b;

	while(!(ca==0 || cb==0) && ((ca==cb) || ConvertChar(ca)==ConvertChar(cb)))
	{
		a++;
		b++;

		ca = *a;
		cb = *b;
	}

	return ConvertChar(ca) - ConvertChar(cb);
}

int scandir(TCHAR *dir, scan_file_t **files)
{
	//파일과 디렉토리 검색을 위한 변수
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	TCHAR CurrentPath[MAX_PATH]={0,};
	TCHAR fullname[MAX_PATH]={0,};
	scan_file_t *tmp;
	int count, idx, i;
	int ret;

	_tcscpy(CurrentPath, dir);
	_tcscat(CurrentPath, _T("\\*"));

	hFind=FindFirstFile(CurrentPath, &FindFileData);
	
	if(hFind==INVALID_HANDLE_VALUE)
	{
		return -1;
	}

	count = 0;

	for(i=0; 0!=FindNextFile(hFind, &FindFileData); i++)
	{
		if(_tcscmp(FindFileData.cFileName, _T(".")) && _tcscmp(FindFileData.cFileName, _T("..")))
		{
			count++;
		}
	}

	if(count == 0)
	{
		*files = NULL;
		FindClose(hFind);
		return 0;
	}

	tmp = (scan_file_t *)malloc(sizeof(scan_file_t) * count);
	if(!tmp)
	{
		FindClose(hFind);
		return -1;
	}

	FindClose(hFind);

	hFind=FindFirstFile(CurrentPath, &FindFileData);

	idx = 0;
	for(i=0; 0!=FindNextFile(hFind, &FindFileData); i++)
	{
		if(!_tcscmp(FindFileData.cFileName, _T(".")) || !_tcscmp(FindFileData.cFileName, _T("..")))
		{
			continue;
		}
		else{
			//tmp[idx].name = WCharToChar(FindFileData.cFileName);
			tmp[idx].name = malloc((_tcslen(FindFileData.cFileName) + 1) * sizeof(TCHAR));
			if(!tmp[idx].name)
			{
				LOG_ERROR(LOG_MOD_UTIL, "out of memory %s:%d", __FILE__, __LINE__);
				FindClose(hFind);
				return -1;
			}
			ret = _tcsncpy_s(tmp[idx].name, _tcslen(FindFileData.cFileName) + 1, FindFileData.cFileName, _TRUNCATE);
			if(ret != 0){
				LOG_ERROR(LOG_MOD_UTIL, "strcpy fail %s:%d", __FILE__, __LINE__);
				FindClose(hFind);
				return -1;
			}

			/* number of link */

			/* if nr_link > 1, Get file index */

			idx++;
		}
	}
	
	FindClose(hFind);

	qsort(tmp, count, sizeof(scan_file_t), cmpr);

	*files = tmp;

	return count;
}

