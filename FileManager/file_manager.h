#ifndef __FILE_FILE_MANAGER_H_
#define __FILE_FILE_MANAGER_H_

#ifdef WIN32
#include "crt_dbg.h"
#ifdef _MT
#include <windows.h>
#include <process.h>
#endif
#endif
#include "extern_c.h"
#include "list.h"
#include "defines.h"
#include "type_defines.h"
#include <sys/stat.h>
#include "scandir.h"
#include "sizeof.h"


typedef enum {
	FILE_LIST_STATE_INIT,
	FILE_LIST_STATE_RUN,
	FILE_LIST_STATE_STOP
} file_list_state_t;

typedef enum {
	FILE_LIST_CHECK_SIZEONLY,
	FILE_LIST_CHECK_ALL
} check_option_t;

typedef struct file_manager_t{
	int nr_total;
	int nr_init;
	int nr_run;

	list_node_t init_list;
	list_node_t run_list;

	TCHAR *local_rep_dir;

#ifdef WIN32
	HANDLE fm_mutex;
#else
	pthread_mutex_t fm_mutex;
#endif

	/* node */
	void *node;
}file_manager_t;

typedef struct dir_t{
	list_node_t lnode;
	int depth;
	int length;
	TCHAR *name;
}dir_t;

typedef struct SEG_HDR_T
{
	int segment_id;
	int nr_files;
	uint64 size;
	int exception;
	int except_seq;
}SEG_HDR_T, *PSEG_HDR_T;

typedef struct SEG_KEY_T
{
	int key_len;
	TCHAR *key;
}SEG_KEY_T, *PSEG_KEY_T;

typedef struct SEG_T
{
	list_node_t lnode;
	SEG_HDR_T hdr;
	SEG_KEY_T first_key;
	SEG_KEY_T last_key;
}SEG_T, *PSEG_T;

typedef struct file_list_t{
	list_node_t lnode;

	char *src_name;
	int src_index;

	char *correspond_host;

	/* file list 비교시, check 옵션, 현재는 size 만 비교 */
	int check_option;

	/* file list 생성시 취소 flag */
	int cancel;

	int nr_dir;
	list_node_t dir_list;

	/* file list 는 file로 관리한다. 이는 file의 개수가 많을 경우 memory에 부하가 생기기 때문이다. */
	char *all_files;
	HANDLE hAFL;

	char *modified_files;
	HANDLE hMFL;

	char *segment_files;
	HANDLE hSegmentList;

	char *history;
	HANDLE hHistory;
	HANDLE hMapFile;
	uint64 *pWrite;

	char *sync_fail;
	HANDLE hSyncFail;

	file_list_state_t state;

	/* compare file info buffer */
	void *remote;
	int nr_afl;
	int afl_idx;
	int afl_complete;

	void *file_entry;
	int nr_file_entry;
	uint64 idx_file_entry;
	uint64 file_entry_offset;
	int file_entry_complete;

	uint64 total_size;
	uint64 sent_size;

	int nr_so_dirs;
	int nr_files;
	int nr_segment;

	list_node_t segment_list;
	PSEG_T cur_seg;

	TCHAR last_path[1024];
	int last_path_len;

	TCHAR *local_rep_dir;

	file_manager_t *fm;
}file_list_t;

typedef struct AFL_FILE_HDR_T
{
	int type;
	int depth;
	int path_size;
	int64 filesize;
    __time64_t mtime;
    __time64_t ctime;
}AFL_FILE_HDR_T, *PAFL_FILE_HDR_T;

typedef struct AFL_FILE_T
{
	AFL_FILE_HDR_T hdr;
	TCHAR *path;
}AFL_FILE_T, *PAFL_FILE_T;

typedef struct TOL_FILE_HDR_T
{
	int type;
	int path_size;
}TOL_FILE_HDR_T, *PTOL_FILE_HDR_T;

typedef struct MFL_FILE_HDR_T
{
	DWORD	File_attribute;
	DWORD	modified_type;
	int		path_size;
	int		secu_len;
	uint64	filesize;
	SECURITY_INFORMATION secu_info;
}MFL_FILE_HDR_T, *PMFL_FILE_HDR_T;

typedef struct MFL_FILE_T
{
	MFL_FILE_HDR_T hdr;
	char *secu_data;
	TCHAR *path;
}MFL_FILE_T, *PMFL_FILE_T;



#define MODIFIED_TYPE_MF	(POW_2_1)
#define MODIFIED_TYPE_TO	(POW_2_2)
#define MODIFIED_TYPE_SO	(POW_2_3)

EXTERN_C_START
file_manager_t *init_file_manager(void *node);
int destroy_file_manager(file_manager_t *fm);
int add_file_list(file_manager_t *fm, TCHAR *name, char *src, char *dst);
int delete_file_list(file_manager_t *fm, file_list_t *fl);
file_list_t *flist_alloc(file_manager_t *fm, char *src, char *dst);
int flist_duplicate(file_manager_t *fm, file_list_t *fl);
int flist_dealloc(file_manager_t *fm, file_list_t *fl, int delete_flag);
int flist_regist_local_dir(file_manager_t *fm, char *src, TCHAR *local_dir);
EXTERN_C_END

#endif