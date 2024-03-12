#ifndef __FILE_FILE_LIST_H_
#define __FILE_FILE_LIST_H_

#ifdef WIN32
#include "crt_dbg.h"
#endif

#include "extern_c.h"
#include "file_manager.h"

EXTERN_C_START
int compare_file_list(file_list_t *fl);
int compare_and_send_file_list(void *session, file_list_t *fl);
int dummy_file_generate(file_list_t *fl);

#if 0
int truncate_modified_file(file_list_t *fl);
#endif

int truncate_modified_file_each(void *s, file_list_t *fl);
int generate_all_file_list(file_list_t *fl);
void delete_tgt_only_file(file_list_t *fl);
PMFL_FILE_T get_curr_file_entry(file_list_t *fl, HANDLE hFile);
PMFL_FILE_T get_next_file_entry(file_list_t *fl, HANDLE hFil);
HANDLE generate_file(void *hdr, TCHAR *path, char *secu_data, DWORD *err);
HANDLE generate_local_rep_file(file_list_t *fl, void *hdr, TCHAR *path, char *secu_data, DWORD *err);
void DeleteLocalReplicationFile(file_list_t *fl, TCHAR *path);
void check_N_create_target_dir(file_list_t *fl);

int segment_read(file_list_t *fl);
int segment_check(file_list_t *fl, TCHAR *path);
int segment_mdp_set(file_list_t *fl, void *mc);
int segment_mdp_set_cur(file_list_t *fl, void *mc);
int segment_mdp_get_cur_seg(file_list_t *fl, void *mdp_controller);
int segment_mdp_stop(file_list_t *fl, void *mdp_controller, int stop_flag);
int segment_mdp_pause(file_list_t *fl, void *mdp_controller);
int segment_mdp_resume(file_list_t *fl, void *mdp_controller);
void segment_print(file_list_t *fl);

int create_hitory_mmap(file_list_t *fl);
int close_history_mmap(file_list_t *fl);
int write_history(file_list_t *fl, uint64 offset, uint64 idx, uint64 filesize);

int flist_sync_fail(file_list_t *fl, TCHAR *origin_path, TCHAR *new_path, int err);

TCHAR *convert_path_to_local_rep_path(file_list_t *fl, TCHAR *path);
EXTERN_C_END

#endif