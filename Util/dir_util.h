#ifndef __UTIL_DIR_UTIL_H_
#define __UTIL_DIR_UTIL_H_

#include <tchar.h>
#include "crt_dbg.h"

int isDirectory( char *pr_filename);
void dirMaker( const char *pr_path, const char *pr_orig );
unsigned long directoryStructureCopy( char *pr_backup_dir, char *pr_path, int pr_skip );
unsigned long dirStcCopy( char *pr_backup_dir, char *pr_path );
TCHAR *get_parent_path(TCHAR *path);
#endif
