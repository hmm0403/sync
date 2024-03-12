

/*****************************************************************
 * HEADER FILE 
 *    t_fd_mapper.h
 *
 * DESCRIPTION
 *		This file implements the file mapper node which maintains
 *		a target file descriptor which corresponds with the source file descriptor.
 *		File mapper list consists of these file mapper nodes.
 *
 * NOTES
 *		None
 *****************************************************************/

/*****************************************************************
 * REVISION HISTORY
 * DATE         Authors          Description
 * ===============================================================
 *****************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************
 * NESTED INCLUDE FILES.
 *****************************************************************/

#include <wchar.h>
#include <windows.h>
#include "sizeof.h"
#include <tchar.h>
#include "crt_dbg.h"

#define _RENAME_TBL_DIR        (POW_2_0)
#define _RENAME_TBL_FILE       (POW_2_1)
#define _RENAME_TBL_CREATE	   (POW_2_2)
#define _RENAME_TBL_REMOVE     (POW_2_3)
#define _RENAME_TBL_RENAME     (POW_2_4)
#define _RENAME_TBL_NEXIST     (POW_2_5)  // rename( old, new ), new file exist

#define _RENAME_TBL_FILE_TYPE  (_RENAME_TBL_DIR | _RENAME_TBL_FILE)
#define _RENAME_TBL_FUNC_TYPE  (_RENAME_TBL_REMOVE | _RENAME_TBL_RENAME )

#define RENAME_TBL_EXIST	 (2)
#define RENAME_TBL_DELETED	 (1)
#define RENAME_TBL_NOT_EXIST (0)

#define MAX_FLT_CM_MSG_LEN	2048
#define RENAME_PATH_PREFIX_OFFSET 4
#define RENAME_PATH_PREFIX_OFFSET_BYTE 8

int _rename_tbl_mergedInfoAdd(
	    int		pr_host_index,
		int    pr_type,
		TCHAR  *pr_begin_path,
		TCHAR  *pr_final_path,
		int    pr_bp_len,
		int    pr_fp_len );

int  _rename_tbl_mergedInfoCheck(
	    int		pr_host_index,
		int		type,
		TCHAR   *pr_path,
		int     pr_len,
		TCHAR  **pr_r_path);


void _rename_tbl_Init( void );
void _rename_tbl_Destory( int pr_host_index );


#ifdef __cplusplus
}
#endif
