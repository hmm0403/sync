#include "dir_util.h"

#ifdef WIN32
#include "crt_dbg.h"
#ifdef _MT
#include <windows.h>
#endif
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include "defines.h"
#include "report.h"
#include <string.h>

int isDirectory( char *pr_filename )
{
        struct stat statbuf;

        if( stat(pr_filename, &statbuf) == -1 )
                return -1;

		if( _S_IFDIR &statbuf.st_mode)return 1;
		else return -1;

        return 0;
}


void dirMaker( const char *pr_path, const char *pr_orig )
{
	int  rtv = 0;
	struct stat   statbuf;


	if( stat(pr_orig, &statbuf) == -1 )
	{
#if _DEBUG
		LOG_DEBUG(LOG_MOD_UTIL, "dir make fail: %s -> %s", pr_orig, pr_path );
#endif
		return ;
	}

	//rtv  = mkdir( pr_path, (statbuf.st_mode & 0777) );
	rtv=CreateDirectoryA(pr_path, NULL);
	if( rtv == FALSE )
	{
#if _DEBUG
		LOG_DEBUG(LOG_MOD_UTIL,  "mkdir fail: %s  -- (%u)", pr_path, GetLastError());
#endif
	}
}

unsigned long directoryStructureCopy( char *pr_backup_dir, char *pr_path,
	       int pr_skip )
{
	int   i                    = 0;
//	int   count                = 0;
	int   flag                 = 0;
	char  fullname[512]        = { '\0' };
	HANDLE hPath			   = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATAA file_info = {0, };

	unsigned long  ret_count   = 0;

	sprintf_s( fullname, 512, "%s%s", pr_backup_dir, (pr_path+pr_skip) );
	dirMaker( fullname, pr_path );
	memset( fullname, 0, 512 );

	//chdir(pr_path);
	//count  = scandir(pr_path, &files, NULL, alphasort);
	SetCurrentDirectoryA(pr_path);
	hPath = FindFirstFileA(pr_path, &file_info);
	
	//if( count < 2 )  // '.', '..'
	//{
	//	return 0;
	//}

	// DIRECTORY
	//for( i = 0; i < count; i++ )
	do
	{
		if( isDirectory(file_info.cFileName) != 1 )
			continue;

		if( (flag != 2) && (!strcmp(file_info.cFileName, ".") ||
			!strcmp(file_info.cFileName, "..")) )
		{
			flag++;
			continue;
		}

		sprintf_s( fullname, 512, "%s/%s", pr_path, file_info.cFileName );

		ret_count++;
		ret_count  += directoryStructureCopy( pr_backup_dir, fullname,
			pr_skip );

		//chdir("..");
		SetCurrentDirectoryA("..");

		memset( fullname, '\0', 512 );
	}while( FindNextFileA(hPath, &file_info)!=0 );

	return ret_count;
}

unsigned long dirStcCopy( char *pr_backup_dir, char *pr_path )
{
	int   len  = 0;
	int   i    = 0;

	if( pr_backup_dir == NULL || pr_path == NULL )
		return -1;

	len  = strlen( pr_path );
	if( len == 1 )
		return -2;

	for( i = len-1; i > 0; i-- )
	{
		if( pr_path[i] == '/' )
			break;
	}

	len  = strlen( pr_backup_dir );
	if( pr_backup_dir[len-1] == '/' )
		i++;

	return  directoryStructureCopy( pr_backup_dir, pr_path, i );
}

TCHAR *get_parent_path(TCHAR *path)
{
	TCHAR *parent;
	int len, i;

	if(!path)
		return NULL;

	len = _tcslen(path);

	if(path[len-1] == _T('\\') || path[len-1] == _T('/'))
		len--;

	for(i = len-1; i > 0; i--){
		if(path[i] == _T('\\') || path[i] == _T('/'))
			break;
	}

	len = i;

	parent = (TCHAR *)malloc(sizeof(TCHAR) * (len + 1));
	if(!parent){
		return NULL;
	}
	memset(parent, 0, sizeof(TCHAR) * (len + 1));

	memcpy(parent, path, sizeof(TCHAR) * len); 

	return parent;
}