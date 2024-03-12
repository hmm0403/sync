﻿#include "file_manager.h"
#include "file_list.h"
#include "report.h"
#include "defines.h"
#include <Aclapi.h>
#include <stdio.h>
#include <tchar.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "wchar_conv.h"
#include "dir_util.h"
#include "privilege.h"
#include "session.h"
#include "session_manager.h"
#include "privilege.h"
#include "mdp_controller.h"

extern int g_stopFlag;
//extern int g_pauseFlag;
extern int g_target_delete;

#define CONDITION_WRITE_MFL(handle, dst, dst_pos, dst_size, src, src_size, w)	\
	{																			\
		if(dst_pos + src_size >= dst_size){										\
			WriteFile(handle, dst, dst_pos, w, NULL);							\
			dst_pos = 0;														\
		}																		\
		memcpy(dst + dst_pos, (char *)src, src_size);							\
		dst_pos += src_size;													\
	}	

#define WRITE_MFL(handle, dst, dst_pos, w)										\
	{																			\
		if(dst_pos > 0){														\
			WriteFile(handle, dst, dst_pos, w, NULL);							\
			dst_pos = 0;														\
		}																		\
}	

int segment_mdp_set(file_list_t* fl, void* mdp_controller)
{
	list_node_t* pos, * nxt;
	PSEG_T seg;
	int ret;
	mdp_controller_t* mc = mdp_controller;

	if (!mc)
		return 0;
	list_for_each_safe(pos, nxt, &fl->segment_list)
	{
		seg = list_entry(pos, SEG_T, lnode);
		ret = mdp_ctrl_set_segment(mc, fl->src_index, seg->first_key.key, seg->last_key.key, seg->hdr.segment_id, seg->hdr.exception);
		if (ret < 0) {
			return -1;
		}
	}
	return 0;
}

int segment_mdp_set_cur(file_list_t* fl, void* mdp_controller)
{
	PSEG_T seg;
	int ret;
	mdp_controller_t* mc = mdp_controller;

	seg = fl->cur_seg;
	if (seg == NULL)
		return 0;

	LOG_INFO(LOG_MOD_FILE, "Node[%d] Set cur segment id : %d", fl->src_index, seg->hdr.segment_id);
	ret = mdp_ctrl_set_cur_segment(mc, fl->src_index, seg->hdr.segment_id);
	if (ret < 0)
		return -1;

	return 0;
}

int segment_mdp_pause(file_list_t* fl, void* mdp_controller)
{
	PSEG_T seg;
	int ret;
	mdp_controller_t* mc = mdp_controller;

	if (!mc)
		return 0;

	seg = fl->cur_seg;
	if (seg == NULL) {
		ret = mdp_ctrl_pause(mc, fl->src_index, -1, fl->last_path);
	}
	else {
		ret = mdp_ctrl_pause(mc, fl->src_index, seg->hdr.segment_id, fl->last_path);
	}

	if (ret < 0)
		return -1;

	return 0;
}

int segment_mdp_stop(file_list_t* fl, void* mdp_controller, int stop_flag)
{
	PSEG_T seg;
	int ret;
	mdp_controller_t* mc = mdp_controller;

	if (!mc)
		return 0;

	seg = fl->cur_seg;
	if (seg == NULL) {
		ret = mdp_ctrl_stop(mc, fl->src_index, -1, fl->last_path, stop_flag);
	}
	else {
		ret = mdp_ctrl_stop(mc, fl->src_index, seg->hdr.segment_id, fl->last_path, stop_flag);
	}

	if (ret < 0)
		return -1;

	return 0;
}

int segment_mdp_get_cur_seg(file_list_t* fl, void* mdp_controller)
{
	PSEG_T seg;
	int seg_id;
	mdp_controller_t* mc = mdp_controller;
	list_node_t* pos, * nxt;

	if (!mc)
		return 0;

	seg_id = mdp_ctrl_get_cur_segment(mc, fl->src_index);

	LOG_DEBUG(LOG_MOD_FILE, "get Cur Seg ID : %d from mDP", seg_id);

	/* search seg_id */
	list_for_each_safe(pos, nxt, &fl->segment_list)
	{
		seg = list_entry(pos, SEG_T, lnode);
		if (seg->hdr.segment_id == seg_id) {
			fl->cur_seg = seg;
			break;
		}
	}
	return 0;
}

int segment_mdp_resume(file_list_t* fl, void* mdp_controller)
{
	mdp_controller_t* mc = mdp_controller;
	if (!mc)
		return 0;
	LOG_DEBUG(LOG_MOD_FILE, "Send RESUME to mDP");
	return mdp_ctrl_resume(mc, fl->src_index);
}

int segment_new(file_list_t* fl, int* err)
{
	PSEG_T seg;
	seg = (PSEG_T)malloc(sizeof(SEG_T));
	if (!seg) {
		*err = -1;
	}
	memset(seg, 0, sizeof(SEG_T));

	seg->hdr.segment_id = fl->nr_segment;

	list_add_tail(&seg->lnode, &fl->segment_list);

	fl->nr_segment++;

	fl->cur_seg = seg;

	return 0;
}

int segment_condition_set(file_list_t* fl, TCHAR* filename, uint64 size, int last, int* err)
{
	if (fl->cur_seg == NULL)
		segment_new(fl, *err);
	fl->cur_seg->hdr.nr_files++;
	fl->cur_seg->hdr.size += size;

	_tcscpy(fl->last_path, filename);
	fl->last_path_len = _tcslen(filename);

	if (fl->cur_seg->first_key.key_len == 0) {
		fl->cur_seg->first_key.key_len = _tcslen(filename);
		fl->cur_seg->first_key.key = malloc((fl->cur_seg->first_key.key_len + 1) * sizeof(TCHAR));
		_tcscpy(fl->cur_seg->first_key.key, filename);
	}

	if (fl->cur_seg->hdr.nr_files > SEGMENT_DEFAULT_FILES || fl->cur_seg->hdr.size > SEGMENT_DEFAULT_SIZE || last) {
		if (last && fl->cur_seg->last_key.key) {
			if (!_tcsicmp(fl->cur_seg->last_key.key, filename)) {
				fl->cur_seg->hdr.nr_files--;
				fl->cur_seg->hdr.size -= size;
				return 0;
			}
		}

		if (fl->cur_seg->last_key.key) {
			SAFE_FREE(fl->cur_seg->last_key.key);
		}

		fl->cur_seg->last_key.key_len = _tcslen(filename);
		fl->cur_seg->last_key.key = malloc((fl->cur_seg->last_key.key_len + 1) * sizeof(TCHAR));
		_tcscpy(fl->cur_seg->last_key.key, filename);

		memset(fl->last_path, 0, 1024);
		fl->last_path_len = 0;

		if (!last) {
			segment_new(fl, *err);
		}
	}
	return 0;
}

int segment_last_check(file_list_t* fl)
{
	if (fl->cur_seg->last_key.key_len == 0) {
		if (fl->cur_seg->hdr.nr_files > 0) {
			fl->cur_seg->last_key.key_len = _tcslen(fl->last_path);
			fl->cur_seg->last_key.key = malloc((fl->cur_seg->last_key.key_len + 1) * sizeof(TCHAR));
			_tcscpy(fl->cur_seg->last_key.key, fl->last_path);
			if (fl->cur_seg->hdr.exception && fl->cur_seg->hdr.nr_files == fl->cur_seg->hdr.except_seq)
				fl->cur_seg->hdr.exception = 0;
		}
	}
	return 0;
}

int segment_set_exception(file_list_t* fl)
{
	if (fl->cur_seg)
	{
		if (fl->cur_seg->first_key.key_len > 0) {
			fl->cur_seg->hdr.exception = 1;
			fl->cur_seg->hdr.except_seq = fl->cur_seg->hdr.nr_files;
		}
	}
	return 0;
}

#if 0
int segment_set_first(file_list_t *fl, TCHAR *filename, uint64 size, int *err) 
{ 
	fl->cur_seg->hdr.nr_files++; 
	fl->cur_seg->hdr.size += size; 

	fl->cur_seg->first_key.key_len = _tcslen(filename); 
	fl->cur_seg->last_key.key = malloc((fl->cur_seg->first_key.key_len + 1) * sizeof(TCHAR)); 
	_tcscpy(fl->cur_seg->first_key.key, filename); 

	return 0;
}

int segment_set_last(file_list_t *fl, TCHAR *filename, uint64 size, int *err) 
{ 
	fl->cur_seg->hdr.nr_files++; 
	fl->cur_seg->hdr.size += size; 

	if(fl->cur_seg->last_key.key_len > 0)
		LOG_ERROR(LOG_MOD_FILE, "Segment Info Set Last Error");

	fl->cur_seg->last_key.key_len = _tcslen(filename); 
	fl->cur_seg->last_key.key = malloc((fl->cur_seg->last_key.key_len + 1) * sizeof(TCHAR)); 
	_tcscpy(fl->cur_seg->last_key.key, filename); 

	return 0;
}
#endif

int segment_set(file_list_t *fl, PSEG_T seg)
{
	fl->cur_seg->hdr = seg->hdr;
	
	fl->cur_seg->first_key.key_len = seg->first_key.key_len;
	fl->cur_seg->first_key.key = malloc((fl->cur_seg->first_key.key_len + 1) * sizeof(TCHAR)); 
	_tcscpy(fl->cur_seg->first_key.key, seg->first_key.key);
	
	fl->cur_seg->last_key.key_len = seg->last_key.key_len;
	fl->cur_seg->last_key.key = malloc((fl->cur_seg->last_key.key_len + 1) * sizeof(TCHAR)); 
	_tcscpy(fl->cur_seg->last_key.key, seg->last_key.key);

	return 0;
}

int segment_write(file_list_t *fl)
{
	list_node_t *pos, *nxt;
	HANDLE hFile;
	PSEG_T seg;
	DWORD written, ret;
	int files = 0;

	hFile = CreateFileA(fl->segment_files, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if( hFile == INVALID_HANDLE_VALUE )
	{
		int err = GetLastError();
		if(err == ERROR_FILE_EXISTS){
			hFile = CreateFileA(fl->segment_files, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
			if( hFile == INVALID_HANDLE_VALUE )
			{
				err = GetLastError();
				LOG_ERROR(LOG_MOD_FILE, "Open File [%s] Open Error : %d", fl->segment_files, err);
				return -1;
			}
		}else{
			LOG_ERROR(LOG_MOD_FILE, "Open File [%s] Open Error : %d", fl->segment_files, err);
			return -1;
		}
	}

	if(fl->nr_files > 0 || fl->nr_so_dirs > 0){
		list_for_each_safe(pos, nxt, &fl->segment_list){
			seg = list_entry(pos, SEG_T, lnode);

			if(seg->hdr.nr_files > 0){
				files += seg->hdr.nr_files;

				ret = WriteFile(hFile, &seg->hdr, sizeof(SEG_HDR_T), &written, NULL);
				if((!ret) || written == 0)
				{
					LOG_ERROR(LOG_MOD_FILE, "Segment File Write Fail. error code[%d]", GetLastError());

					return -1;
				}

				ret = WriteFile(hFile, &seg->first_key.key_len, sizeof(int), &written, NULL);
				if((!ret) || written == 0)
				{
					LOG_ERROR(LOG_MOD_FILE, "Segment File Write Fail. error code[%d]", GetLastError());

					return -1;
				}

				ret = WriteFile(hFile, seg->first_key.key, (seg->first_key.key_len+1)*sizeof(TCHAR), &written, NULL);
				if((!ret) || written == 0)
				{
					LOG_ERROR(LOG_MOD_FILE, "Segment File Write Fail. error code[%d]", GetLastError());

					return -1;
				}

				ret = WriteFile(hFile, &seg->last_key.key_len, sizeof(int), &written, NULL);
				if((!ret) || written == 0)
				{
					LOG_ERROR(LOG_MOD_FILE, "Segment File Write Fail. error code[%d]", GetLastError());

					return -1;
				}

				ret = WriteFile(hFile, seg->last_key.key, (seg->last_key.key_len+1)*sizeof(TCHAR), &written, NULL);
				if((!ret) || written == 0)
				{
					LOG_ERROR(LOG_MOD_FILE, "Segment File Write Fail. error code[%d]", GetLastError());

					return -1;
				}
			}
		}
	}
	SAFE_CLOSE_HANDLE(hFile);
	return 0;
}

int segment_read(file_list_t* fl) {
	list_node_t* pos, * nxt;
	HANDLE hFile;
	SEG_T seg;
	DWORD readn, ret, err;

	hFile = CreateFileA(fl->segment_files, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		err = GetLastError();
		LOG_ERROR(LOG_MOD_FILE, "Open File [%s] Open Error : %d", fl->segment_files, err);
		goto error_out;
	}

	while (1) {
		ret = ReadFile(hFile, &seg.hdr, sizeof(SEG_HDR_T), &readn, NULL);
		if ((!ret) || readn == 0)
		{
			err = GetLastError();
			if (err == ERROR_HANDLE_EOF || err == ERROR_SUCCESS) {
				break;
			}

			LOG_ERROR(LOG_MOD_FILE, "Segment File Read Fail. error code[%d]", err);

			goto error_out;
		}
		ret = ReadFile(hFile, &seg.first_key.key_len, sizeof(int), &readn, NULL);
		if ((!ret) || readn == 0)
		{
			err = GetLastError();

			LOG_ERROR(LOG_MOD_FILE, "Segment File Read Fail. error code[%d]", err);

			goto error_out;
		}

		seg.first_key.key = (TCHAR*)malloc((seg.first_key.key_len + 1) * sizeof(TCHAR));
		ret = ReadFile(hFile, (char*)seg.first_key.key, (seg.first_key.key_len + 1) * sizeof(TCHAR), &readn, NULL);
		if ((!ret) || readn == 0)
		{
			err = GetLastError();

			LOG_ERROR(LOG_MOD_FILE, "Segment File Read Fail. error code[%d]", err);

			goto error_out;
		}

		ret = ReadFile(hFile, &seg.last_key.key_len, sizeof(int), &readn, NULL);
		if ((!ret) || readn == 0)
		{
			err = GetLastError();

			LOG_ERROR(LOG_MOD_FILE, "Segment File Read Fail. error code[%d]", err);

			goto error_out;
		}

		seg.last_key.key = (TCHAR*)malloc((seg.last_key.key_len + 1) * sizeof(TCHAR));
		ret = ReadFile(hFile, (char*)seg.last_key.key, (seg.last_key.key_len + 1) * sizeof(TCHAR), &readn, NULL);
		if ((!ret) || readn == 0)
		{
			err = GetLastError();

			LOG_ERROR(LOG_MOD_FILE, "Segment File Read Fail. error code[%d]", err);

			goto error_out;
		}
		segment_new(fl, &err);
		segment_set(fl, &seg);
	}

	if (!list_empty(&fl->segment_files))
		fl->cur_seg = list_first_entry(&fl->segment_list, SEG_T, lnode);

	SAFE_CLOSE_HANDLE(hFile);
	return 0;

error_out:
	SAFE_CLOSE_HANDLE(hFile);
	return -1;
}

int segment_check(file_list_t* fl, TCHAR* path) {
	int rtv;

	_tcscpy(fl->last_path, path);

	if (fl->cur_seg == NULL)
		return -1;

	if (fl->cur_seg->last_key.key_len == 0)
		return -1;

	rtv = str_dep_cmp(fl->cur_seg->last_key.key, path);
	if (rtv <= 0) {
		if (list_is_last_entry(fl->cur_seg, SEG_T, lnode, &fl->segment_list)) {
			fl->cur_seg = NULL;
			return 0;
		}
		else
			fl->cur_seg = (PSEG_T)list_next_entry(fl->cur_seg, SEG_T, lnode);

		return 1;
	}
	return 0;
}

void segment_print(file_list_t* fl)
{
	list_node_t* pos, * nxt;
	SEG_T* seg;

	LOG_INFO(LOG_MOD_FILE, "");
	LOG_INFO(LOG_MOD_FILE, "SEGMENT LIST PRINT----------------------------------");

	list_for_each_safe(pos, nxt, &fl->segment_list) {
		seg = list_entry(pos, SEG_T, lnode);

		LOG_INFO(LOG_MOD_FILE, "  ID : %d, NR_FILES : %d, SIZE : %llu",
			seg->hdr.segment_id, seg->hdr.nr_files, seg->hdr.size);

		LOG_INFO_W(LOG_MOD_FILE, _T("    FK : %s"), seg->first_key.key);
		LOG_INFO_W(LOG_MOD_FILE, _T("    LK : %s"), seg->last_key.key);
	}

	LOG_INFO(LOG_MOD_FILE, "----------------------------------------------------");
}

TCHAR* convert_path_to_local_rep_path(file_list_t* fl, TCHAR* path)
{
	int len;
	TCHAR* local_dir = fl->local_rep_dir;
	TCHAR* retpath;

	len = _tcslen(local_dir) + _tcslen(path) + 1;

	retpath = (TCHAR*)malloc(sizeof(TCHAR) * len);
	if (!retpath) {
		LOG_ERROR(LOG_MOD_FILE, "out of memory %s:%d", __FILE__, __LINE__);
		return NULL;
	}

	_stprintf(retpath, _T("%s\\%c\\%s"), local_dir, *path, path + 3);

	return retpath;
}

TCHAR* convert_local_rep_path_to_origin(TCHAR* local_dir, TCHAR* path)
{
	int len;
	TCHAR* retpath;

	len = _tcslen(path) - _tcslen(local_dir) + 1;

	retpath = (TCHAR*)malloc(sizeof(TCHAR) * len);
	if (!retpath) {
		LOG_ERROR(LOG_MOD_FILE, "out of memory %s:%d", __FILE__, __LINE__);
		return NULL;
	}

	_stprintf(retpath, _T("%c:\\%s"), *(path + _tcslen(local_dir) + 1), path + _tcslen(local_dir) + 3);

	return retpath;
}

int scandir_to_file(file_list_t* fl, TCHAR* path, HANDLE hFile, int depth)
{
	int count, i, total;
	scan_file_t* files = NULL;
	TCHAR subpath[MAX_PATH] = { 0, }, * origin_subpath = NULL;
	int ret, len, written, mark;
	char buf[1024];
	struct _stat64 sbuf;
	AFL_FILE_HDR_T hdr;
	file_manager_t* fm = (file_manager_t*)fl->fm;

	total = 0;

	//if(g_stopFlag || g_pauseFlag)
	if (g_stopFlag)
		return -1;

	if (fl->cancel)
		return -1;

	if (fl->local_rep_dir) {
		path = convert_path_to_local_rep_path(fl, path);
	}

	count = scandir(path, &files);
	if (count > 0) {
		for (i = 0; i < count; i++)
		{
			if (fl->cancel) {
				SAFE_FREE(path);
				return -1;
			}

			if (g_stopFlag) {
				SAFE_FREE(path);
				return -1;
			}

			_stprintf(subpath, _T("%s\\%s"), path, files[i].name);
			if (fl->local_rep_dir)
				origin_subpath = convert_local_rep_path_to_origin(fl->local_rep_dir, subpath);
			else
				origin_subpath = subpath;

			_tstati64(subpath, &sbuf);

			if (sbuf.st_mode & _S_IFDIR) /* dir */
			{
				hdr.type = INITSYNC_FTYPE_DIRECTORY;

				hdr.depth = depth;
				hdr.path_size = _tcslen(origin_subpath) * sizeof(TCHAR);
				hdr.filesize = sbuf.st_size;
				hdr.ctime = sbuf.st_ctime;
				hdr.mtime = sbuf.st_mtime;

				WriteFile(hFile, &hdr, sizeof(AFL_FILE_HDR_T), &written, NULL);
				WriteFile(hFile, origin_subpath, hdr.path_size, &written, NULL);

				total += scandir_to_file(fl, origin_subpath, hFile, depth + 1);
			}
			else {
				mark = 0;

				hdr.type = INITSYNC_FTYPE_FILE;

				hdr.depth = depth;
				hdr.path_size = _tcslen(origin_subpath) * sizeof(TCHAR);
				hdr.filesize = sbuf.st_size;
				hdr.ctime = sbuf.st_ctime;
				hdr.mtime = sbuf.st_mtime;

				WriteFile(hFile, &hdr, sizeof(AFL_FILE_HDR_T), &written, NULL);
				WriteFile(hFile, origin_subpath, hdr.path_size, &written, NULL);
			}
			total++;

			if (fl->local_rep_dir)
				SAFE_FREE(origin_subpath);
		}
	}

	SAFE_FREE(files);
	if (fl->local_rep_dir)
		SAFE_FREE(path);

	return total;
}

int generate_all_file_list(file_list_t *fl)
{
	char buf[5];
	int ret = 0;
	list_node_t *pos, *nxt;
	dir_t *dir;
	DWORD attr;

	fl->hAFL = CreateFileA(fl->all_files, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,NULL);
	if(fl->hAFL == INVALID_HANDLE_VALUE )
	{
		int err = GetLastError();
		do {
			fl->hAFL = CreateFileA(fl->all_files, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,NULL);
			Sleep(1);
		} while( fl->hAFL == INVALID_HANDLE_VALUE );
	}

	list_for_each_safe(pos, nxt, &fl->dir_list)
	{
		dir = list_entry(pos, dir_t, lnode);

		check_N_generate_dir(dir->name);

		ret = scandir_to_file(fl, dir->name, fl->hAFL, 0);	
		if(ret < 0)
			break;
	}

	SetEndOfFile(fl->hAFL);
	SAFE_CLOSE_HANDLE(fl->hAFL);
	return ret;
}

int check_N_generate_dir(TCHAR *path)
{
	WIN32_FIND_DATAA file_info = {0, };
	HANDLE hFind;
	TCHAR *parent_path;
	int ret;

	hFind = FindFirstFile(path, &file_info);
	if(hFind == INVALID_HANDLE_VALUE){
		/* Get parend path */
		parent_path = get_parent_path(path);

		check_N_generate_dir(parent_path);

		/* Create dir */
		CreateDirectory(path, NULL);

		SAFE_FREE(parent_path);
	}
	FindClose(hFind);
	return 0;
}

void check_N_create_target_dir(file_list_t *fl)
{
	list_node_t *pos, *nxt;
	dir_t *dir;
	TCHAR *path;

	list_for_each_safe(pos, nxt, &fl->dir_list)
	{
		dir = list_entry(pos, dir_t, lnode);
		path = convert_path_to_local_rep_path(fl, dir->name);
		check_N_generate_dir(path);
		SAFE_FREE(path);
	}
}

int AFLFileToStruct(HANDLE hFile, AFL_FILE_T **sf, int nr_scan)
{
	AFL_FILE_HDR_T hdr;
	int i, readn, ret;
	TCHAR *path;

	*sf = malloc(sizeof(AFL_FILE_T) * nr_scan);
	if(!*sf){
		LOG_ERROR(LOG_MOD_FILE, "out of memory %s:%d", __FILE__, __LINE__);
		return -1;
	}

	for(i = 0; i < nr_scan; i++){
		ret = ReadFile(hFile, &hdr, sizeof(AFL_FILE_HDR_T), &readn, NULL);
		if(ret == 0){
			if(GetLastError() == ERROR_HANDLE_EOF)
			{
				return i;
			}else{
				LOG_ERROR(LOG_MOD_FILE, "Read AFL file fail, error code : %d", GetLastError());
				return -1;
			}
		}

		if(readn == 0)
		{
			return i;
		}

		if(hdr.path_size > 0){
			path = (TCHAR *)malloc(hdr.path_size + sizeof(TCHAR));
			if(!path){
				LOG_ERROR(LOG_MOD_FILE, "out of memory %s:%d", __FILE__, __LINE__);
				return -1;
			}
			ret = ReadFile(hFile, path, hdr.path_size, &readn, NULL);
			if(ret == 0 || readn == 0){
				LOG_ERROR(LOG_MOD_FILE, "Read AFL file fail, error code : %d", GetLastError());
				return -1;
			}

			path[hdr.path_size/sizeof(TCHAR)] = 0;//'\0';

			(*sf)[i].hdr = hdr;
			(*sf)[i].path = path;
		}else{
			LOG_ERROR(LOG_MOD_FILE, "We need File path size");
			return -1;
		}
	}
	return i;
}

int read_afl_to_struct(file_list_t *fl)
{
	int ret;

	SAFE_FREE(fl->remote);

	ret = AFLFileToStruct(fl->hAFL, &fl->remote, MAX_SCAN_BUF);
	if(ret == 0 || ret == -1){
		fl->afl_idx = 0;
		fl->nr_afl = 0;
	}else{
		fl->afl_idx = 0;
		fl->nr_afl = ret;
	}

	return ret;
}

PAFL_FILE_T get_next_afl(file_list_t *fl)
{
	int ret; 

	if(!fl->nr_afl || (fl->afl_idx+1 == fl->nr_afl)){
		ret = read_afl_to_struct(fl);
		if(ret == -1){
			return NULL;
		}else if(ret == 0){
			fl->afl_complete = 1;
			return NULL;
		}else{
			return fl->remote;
		}
	}else{
		fl->afl_idx++;
		return (PAFL_FILE_T)fl->remote + fl->afl_idx;
	}
}

PAFL_FILE_T get_curr_afl(file_list_t *fl)
{
	if(!fl->afl_idx && !fl->nr_afl){
		return get_next_afl(fl);
	}else{
		return (PAFL_FILE_T)fl->remote + fl->afl_idx;
	}
}

int MFLFileToStruct(HANDLE hFile, MFL_FILE_T **sf, int nr_scan)
{
	int i, readn, ret;
	TCHAR *path;
	char *secu_data;
	MFL_FILE_HDR_T hdr;

	*sf = malloc(sizeof(MFL_FILE_T) * nr_scan);
	if(!*sf){
		LOG_ERROR(LOG_MOD_FILE, "out of memory %s:%d", __FILE__, __LINE__);
		return -1;
	}

	for(i = 0; i < nr_scan; i++){
		ret = ReadFile(hFile, &hdr, sizeof(MFL_FILE_HDR_T), &readn, NULL);
		if(ret == 0){
			if(GetLastError() == ERROR_HANDLE_EOF)
			{
				return i;
			}else{
				LOG_ERROR(LOG_MOD_FILE, "Read MFL file fail, error code : %d", GetLastError());
				return -1;
			}
		}

		if(readn == 0)
		{
			return i;
		}

		if(hdr.secu_len > 0){
			secu_data = malloc(hdr.secu_len);
			if(!secu_data){
				LOG_ERROR(LOG_MOD_FILE, "out of memory %s:%d", __FILE__, __LINE__);
				return -1;
			}
			ret = ReadFile(hFile, secu_data, hdr.secu_len, &readn, NULL);
			if(ret == 0 || readn == 0){
				LOG_ERROR(LOG_MOD_FILE, "Read MFL file fail, error code : %d", GetLastError());
				return -1;
			}
		}else{
			secu_data = NULL;
		}

		if(hdr.path_size > 0){
			path = (TCHAR *)malloc(hdr.path_size + sizeof(TCHAR));
			if(!path){
				LOG_ERROR(LOG_MOD_FILE, "out of memory %s:%d", __FILE__, __LINE__);
				return -1;
			}
			ret = ReadFile(hFile, path, hdr.path_size, &readn, NULL);
			if(ret == 0 || readn == 0){
				LOG_ERROR(LOG_MOD_FILE, "Read MFL file fail, error code : %d", GetLastError());
				return -1;
			}

			path[hdr.path_size/sizeof(TCHAR)] = 0;//'\0';
		}else{
			LOG_ERROR(LOG_MOD_FILE, "We need File path size");
			return -1;
		}

		(*sf)[i].hdr = hdr;
		(*sf)[i].secu_data = secu_data;
		(*sf)[i].path = path;
	}
	return i;
}

int read_file_list_to_struct(file_list_t *fl, HANDLE hFile)
{
	int ret;
	PMFL_FILE_T tmp;
	int i;

	for(i = 0; i < fl->nr_file_entry; i++)
	{
		tmp = &((PMFL_FILE_T)fl->file_entry)[i];
		SAFE_FREE(tmp->path);
		SAFE_FREE(tmp->secu_data);
	}

	SAFE_FREE(fl->file_entry);

	ret = MFLFileToStruct(hFile, &fl->file_entry, MAX_SCAN_BUF);
	if(ret == 0 || ret == -1){
		fl->idx_file_entry = 0;
		fl->nr_file_entry = 0;
	}else{
		fl->idx_file_entry = 0;
		fl->nr_file_entry = ret;
	}

	if(ret != -1){
		LARGE_INTEGER li, result;
		li.QuadPart = 0;
		SetFilePointerEx(hFile, li, &result, FILE_CURRENT);
		fl->file_entry_offset = result.QuadPart;
	}

	return ret;
}

PMFL_FILE_T get_next_file_entry(file_list_t *fl, HANDLE hFile)
{
	int ret; 

	if(fl->nr_file_entry == 0 || (fl->idx_file_entry+1 == fl->nr_file_entry)){
		ret = read_file_list_to_struct(fl, hFile);
		if(ret == -1){
			return NULL;
		}else if(ret == 0){
			fl->file_entry_complete = 1;
			return NULL;
		}else{
			return fl->file_entry;
		}
	}else{
		fl->idx_file_entry++;
		return (PMFL_FILE_T)fl->file_entry + fl->idx_file_entry;
	}
}

PMFL_FILE_T get_curr_file_entry(file_list_t *fl, HANDLE hFile)
{
	if(!fl->idx_file_entry && !fl->nr_file_entry){
		return get_next_file_entry(fl, hFile);
	}else{
		return (PMFL_FILE_T)fl->file_entry + fl->idx_file_entry;
	}
}


int divided_compare_file_list(file_list_t *fl, TCHAR *path, int depth, int src_only, int last)
{
	int count, i, total;
	PAFL_FILE_T remote_cur;
	scan_file_t *files = NULL;
	TCHAR subpath[MAX_PATH]={0,};
	int ret, nr_remote, written, type;
	char buf[8192] = {0,};
	int write_pos;
	struct _stat64 sbuf;
	MFL_FILE_HDR_T mhdr;
	SEG_T segment;
	char *secu_data;
	int err;

	total = 0;
	write_pos = 0;

	if(g_stopFlag)
		return -1;

	if(fl->cancel)
		return -1;

	count = scandir(path, &files);
	if(count > 0){
		//if(depth > 0)
			//segment_condition_set(fl, path, 0, 0, &err);

		for(i = 0; i < count;)
		{
			if(fl->cancel)
				return -1;

			if(g_stopFlag)
				return -1;

			remote_cur = get_curr_afl(fl);
			if(!remote_cur){
				if(!fl->afl_complete){
					/* error */
					LOG_ERROR(LOG_MOD_FILE, "AFL READ FAIL");
					return -1;
				}
			}

			_stprintf(subpath, _T("%s\\%s"), path, files[i].name);
			_tstati64(subpath, &sbuf);

			if(!fl->afl_complete){ // afl_complete : 1 -> No More remote AFL Entry
				if(src_only){
					ret = 1;
				}else{
					ret = depth - remote_cur->hdr.depth;
					if(ret == 0)
						ret = str_dep_cmp(remote_cur->path, subpath);
				}

				if(ret == 0){ // Same Path, Compare Size
					/* compare size */
					if(remote_cur->hdr.filesize != sbuf.st_size){
						mhdr.modified_type = MODIFIED_TYPE_MF;
						mhdr.path_size = _tcslen(subpath) * sizeof(TCHAR);
						mhdr.filesize = sbuf.st_size;

						/* Get File Attribute */
						mhdr.File_attribute = GetFileAttributes(subpath);
#ifdef _PRE_MAKE_SECU_INFO_
						secu_data = MakeSetSecuInfo(subpath, &mhdr.secu_len, &mhdr.secu_info, &err);
						if(!secu_data){
							/* error */
							LOG_ERROR_W(LOG_MOD_FILE, _T("Make Security Information Fail, file name : %s, error code : %d"), subpath, err);
							return -1;
						}
#else
						secu_data = NULL;
						mhdr.secu_info = 0;
						mhdr.secu_len = 0;
#endif

						/* insert modified file list */
						CONDITION_WRITE_MFL(fl->hMFL, buf, write_pos, sizeof(buf), &mhdr, sizeof(MFL_FILE_HDR_T), &written);
						if(secu_data){
							CONDITION_WRITE_MFL(fl->hMFL, buf, write_pos, sizeof(buf), secu_data, mhdr.secu_len, &written);
						}
						CONDITION_WRITE_MFL(fl->hMFL, buf, write_pos, sizeof(buf), subpath, mhdr.path_size, &written);

						fl->total_size += sbuf.st_size;

						if(sbuf.st_mode & _S_IFDIR){
							fl->nr_so_dirs++;
							 (fl, subpath, sbuf.st_size, 0, &err);
						}else{
							fl->nr_files++;
							segment_condition_set(fl, subpath, sbuf.st_size, ((i == count - 1) && last)?1:0, &err);
						}
					}else{ // size same
							segment_set_exception(fl);
					}

					/* move next both */
					remote_cur = get_next_afl(fl);
					if(sbuf.st_mode & _S_IFDIR){
						WRITE_MFL(fl->hMFL, buf, write_pos,&written);
						
						divided_compare_file_list(fl, subpath, depth+1, 0, ((i == count - 1) && last)?1:0);
					}

					i++;
				}else if(ret > 0){
					/* source only */
					mhdr.modified_type = MODIFIED_TYPE_SO;
					mhdr.path_size = _tcslen(subpath) * sizeof(TCHAR);
					mhdr.filesize = sbuf.st_size;

					/* Get File Attribute */
					mhdr.File_attribute = GetFileAttributes(subpath);

#ifdef _PRE_MAKE_SECU_INFO_
					secu_data = MakeSetSecuInfo(subpath, &mhdr.secu_len, &mhdr.secu_info, &err);
					if(!secu_data){
						/* error */
						LOG_ERROR_W(LOG_MOD_FILE, _T("Make Security Information Fail, file name : %s, error code : %d"), subpath, err);
						return -1;
					}
#else
					secu_data = NULL;
					mhdr.secu_info = 0;
					mhdr.secu_len = 0;
#endif
					/* insert modified file list */
					CONDITION_WRITE_MFL(fl->hMFL, buf, write_pos, sizeof(buf), &mhdr, sizeof(MFL_FILE_HDR_T), &written);
					if(secu_data){
						CONDITION_WRITE_MFL(fl->hMFL, buf, write_pos, sizeof(buf), secu_data, mhdr.secu_len, &written);
					}
					CONDITION_WRITE_MFL(fl->hMFL, buf, write_pos, sizeof(buf), subpath, mhdr.path_size, &written);

					fl->total_size += sbuf.st_size;

					/* move next local */
					if(sbuf.st_mode & _S_IFDIR){
						WRITE_MFL(fl->hMFL, buf, write_pos, &written);
						fl->nr_so_dirs++;

						segment_condition_set(fl, subpath, sbuf.st_size, 0, &err);
						divided_compare_file_list(fl, subpath, depth+1, 1, ((i == count - 1) && last)?1:0);
					}else{
						fl->nr_files++;

						segment_condition_set(fl, subpath, sbuf.st_size, ((i == count - 1) && last)?1:0, &err);
					}

					i++;
				}else{
					/* tgt only */
					mhdr.modified_type = MODIFIED_TYPE_TO;
					mhdr.path_size = _tcslen(remote_cur->path) * sizeof(TCHAR);

					/* insert remote to tgt only*/
					CONDITION_WRITE_MFL(fl->hMFL, buf, write_pos, sizeof(buf), &mhdr, sizeof(MFL_FILE_HDR_T), &written);

					CONDITION_WRITE_MFL(fl->hMFL, buf, write_pos, sizeof(buf), remote_cur->path, remote_cur->hdr.path_size, &written);
					
					/* move next remote */
					remote_cur = get_next_afl(fl);
				}
			}else{
				/* source only */
				/* insert modified file list */
				/* Set MFL hdr */
				mhdr.modified_type = MODIFIED_TYPE_SO;
				mhdr.path_size = _tcslen(subpath) * sizeof(TCHAR);
				mhdr.filesize = sbuf.st_size;

				/* Get File Attribute */
				mhdr.File_attribute = GetFileAttributes(subpath);

#ifdef _PRE_MAKE_SECU_INFO_
					secu_data = MakeSetSecuInfo(subpath, &mhdr.secu_len, &mhdr.secu_info, &err);
					if(!secu_data){
						/* error */
						LOG_ERROR_W(LOG_MOD_FILE, _T("Make Security Information Fail, file name : %s, error code : %d"), subpath, err);
						return -1;
					}
#else
				secu_data = NULL;
				mhdr.secu_info = 0;
				mhdr.secu_len = 0;
#endif

				/* insert modified file list */
				CONDITION_WRITE_MFL(fl->hMFL, buf, write_pos, sizeof(buf), &mhdr, sizeof(MFL_FILE_HDR_T), &written);
				if(secu_data){
					CONDITION_WRITE_MFL(fl->hMFL, buf, write_pos, sizeof(buf), secu_data, mhdr.secu_len, &written);
				}
				CONDITION_WRITE_MFL(fl->hMFL, buf, write_pos, sizeof(buf), subpath, mhdr.path_size, &written);

				fl->total_size += sbuf.st_size;

				/* move next local */
				if(sbuf.st_mode & _S_IFDIR){
					WRITE_MFL(fl->hMFL, buf, write_pos,&written);

					fl->nr_so_dirs++;
					segment_condition_set(fl, subpath, sbuf.st_size, 0, &err);
					divided_compare_file_list(fl, subpath, depth+1, 1, ((i == count - 1) && last)?1:0);
				}else{
					fl->nr_files++;

					segment_condition_set(fl, subpath, sbuf.st_size, ((i == count - 1) && last)?1:0, &err);
				}

				i++;
			}
		}
	}else{
		/* empty dir */
		if(last){
			segment_last_check(fl);
			//segment_condition_set(fl, path, 0, last, &err);
		}
	}

	if(depth == 0 && last){
		remote_cur = get_curr_afl(fl);
		while(remote_cur){
			/* tgt only */
			mhdr.modified_type = MODIFIED_TYPE_TO;
			mhdr.path_size = _tcslen(remote_cur->path) * sizeof(TCHAR);

			/* insert remote to tgt only*/
			CONDITION_WRITE_MFL(fl->hMFL, buf, write_pos, sizeof(buf), &mhdr, sizeof(MFL_FILE_HDR_T), &written);

			CONDITION_WRITE_MFL(fl->hMFL, buf, write_pos, sizeof(buf), remote_cur->path, remote_cur->hdr.path_size, &written);
			
			/* move next remote */
			remote_cur = get_next_afl(fl);
		}
	}

	WRITE_MFL(fl->hMFL, buf, write_pos, &written);

	SAFE_FREE(files);

	return 0;
}

#if 0
int divided_compare_and_send_file_list(session_t *s, file_list_t *fl, TCHAR *path, int depth)
{
	int count, i, total;
	PAFL_FILE_T remote_cur;
	scan_file_t *files = NULL;
	char subpath[MAX_PATH]={0,};
	int ret, written;
	struct _stat64 sbuf;
	MFL_FILE_HDR_T mhdr;
	
	char *secu_data;

	total = 0;

	if(g_stopFlag)
		return -1;

	count = scandir(path, &files);
	if(count > 0){
		for(i = 0; i < count;)
		{
			if(g_stopFlag)
				return -1;

			remote_cur = get_curr_afl(fl);
			if(!remote_cur){
				if(!fl->afl_complete){
					/* error */
					LOG_ERROR("AFL READ FAIL");
					return -1;
				}
			}

			sprintf(subpath, "%s\\%s", path, files[i].name);
			_stati64(subpath, &sbuf);
			if(sbuf.st_mode & _S_IFDIR) /* dir */
			{
				if(!fl->afl_complete){
					ret = strcmp(remote_cur->path, subpath);
					if(ret >= 0){
						/* Set MFL hdr */
						mhdr.modified_type = MODIFIED_TYPE_MF;
						mhdr.filesize = sbuf.st_size;
						mhdr.path_size = strlen(subpath);

						/* Get File Attribute */
						mhdr.File_attribute = GetFileAttributesA(subpath);

						
						/* Get File Security */
						secu_data = MakeSetSecuInfo(subpath, &mhdr.secu_len, &mhdr.secu_info);
						if(!secu_data){
							/* error */
							LOG_ERROR("Make Security Information Fail, file name : %s", subpath);
							return -1;
						}

					}else{
						/* Set TOL hdr */
						if(g_target_delete){
							mhdr.modified_type = MODIFIED_TYPE_TO;
							mhdr.filesize = 0;
							mhdr.File_attribute = FILE_ATTRIBUTE_DIRECTORY;
							mhdr.path_size = strlen(remote_cur->path);	
						}
					}

					if(ret == 0){
						ret = session_send(s, &mhdr, sizeof(MFL_FILE_HDR_T), QUEUE_ENTRY_DATA_TYPE_BINARY);
						if(ret < 0){
							LOG_ERROR("Send MFL Fail");
							return -1;
						}
						ret = session_send(s, secu_data, mhdr.secu_len, QUEUE_ENTRY_DATA_TYPE_BINARY);
						if(ret < 0){
							LOG_ERROR("Send MFL Fail");
							return -1;
						}
						ret = session_send(s, subpath, mhdr.path_size, QUEUE_ENTRY_DATA_TYPE_BINARY);
						if(ret < 0){
							LOG_ERROR("Send MFL Fail");
							return -1;
						}
					
						/* move next both */
						remote_cur = get_next_afl(fl);
						divided_compare_and_send_file_list(s, fl, subpath, depth+1);
						i++;
					}else if(ret > 0){
						/* source only */
						/* insert modified file list */
						ret = session_send(s, &mhdr, sizeof(MFL_FILE_HDR_T), QUEUE_ENTRY_DATA_TYPE_BINARY);
						if(ret < 0){
							LOG_ERROR("Send MFL Fail");
							return -1;
						}
						ret = session_send(s, secu_data, mhdr.secu_len, QUEUE_ENTRY_DATA_TYPE_BINARY);
						if(ret < 0){
							LOG_ERROR("Send MFL Fail");
							return -1;
						}
						ret = session_send(s, subpath, mhdr.path_size, QUEUE_ENTRY_DATA_TYPE_BINARY);
						if(ret < 0){
							LOG_ERROR("Send MFL Fail");
							return -1;
						}

						/* move next local */
						divided_compare_and_send_file_list(s, fl, subpath, depth+1);
						i++;
					}else{
						/* send TOL */
						if(g_target_delete){
							ret = session_send(s, &mhdr, sizeof(MFL_FILE_HDR_T), QUEUE_ENTRY_DATA_TYPE_BINARY);
							if(ret < 0){
								LOG_ERROR("Send MFL Fail");
								return -1;
							}

							ret = session_send(s, remote_cur->path, mhdr.path_size, QUEUE_ENTRY_DATA_TYPE_BINARY);
							if(ret < 0){
								LOG_ERROR("Send MFL Fail");
								return -1;
							}
						}

						/* move next remote */
						remote_cur = get_next_afl(fl);
					}
				}else{
					/* source only */
					/* insert modified file list */
					/* Set MFL hdr */
					mhdr.modified_type = MODIFIED_TYPE_SO;
					mhdr.filesize = sbuf.st_size;
					mhdr.path_size = strlen(subpath);

					/* Get File Attribute */
					mhdr.File_attribute = GetFileAttributesA(subpath);


					/* Get File Security */
					secu_data = MakeSetSecuInfo(subpath, &mhdr.secu_len, &mhdr.secu_info);
					if(!secu_data){
						/* error */
						LOG_ERROR("Make Security Information Fail, file name : %s", subpath);
						return -1;
					}
					if(mhdr.path_size > MAX_PATH || mhdr.secu_len > MAX_PATH){
						LOG_ERROR("Wrong Path length, file name : %s", subpath);
						return -1;
					}

					ret = session_send(s, &mhdr, sizeof(MFL_FILE_HDR_T), QUEUE_ENTRY_DATA_TYPE_BINARY);
					if(ret < 0){
						LOG_ERROR("Send MFL Fail");
						return -1;
					}
					ret = session_send(s, secu_data, mhdr.secu_len, QUEUE_ENTRY_DATA_TYPE_BINARY);
					if(ret < 0){
						LOG_ERROR("Send MFL Fail");
						return -1;
					}
					ret = session_send(s, subpath, mhdr.path_size, QUEUE_ENTRY_DATA_TYPE_BINARY);
					if(ret < 0){
						LOG_ERROR("Send MFL Fail");
						return -1;
					}

					/* move next local */
					divided_compare_and_send_file_list(s, fl, subpath, depth+1);
					i++;
				}
			}else{
				if(!fl->afl_complete){
					ret = strcmp(remote_cur->path, subpath);
					if(ret >= 0){
						/* Set MFL hdr */
						mhdr.modified_type = MODIFIED_TYPE_MF;
						mhdr.filesize = sbuf.st_size;
						mhdr.path_size = strlen(subpath);

						/* Get File Attribute */
						mhdr.File_attribute = GetFileAttributesA(subpath);

						
						/* Get File Security */
						secu_data = MakeSetSecuInfo(subpath, &mhdr.secu_len, &mhdr.secu_info);
						if(!secu_data){
							/* error */
							LOG_ERROR("Make Security Information Fail, file name : %s", subpath);
							return -1;
						}

					}else{
						/* Set TOL hdr */
						if(g_target_delete){
							mhdr.modified_type = MODIFIED_TYPE_TO;
							mhdr.filesize = 0;
							mhdr.File_attribute = FILE_ATTRIBUTE_DIRECTORY;
							mhdr.path_size = strlen(remote_cur->path);	
						}
					}

					if(ret == 0){
						if(fl->check_option == FILE_LIST_CHECK_ALL){
							/* size, ctime, mtime check*/
							if((remote_cur->hdr.filesize != sbuf.st_size)
								|| (remote_cur->hdr.ctime != sbuf.st_ctime)
								|| (remote_cur->hdr.mtime != sbuf.st_mtime)) {
									/* insert modified file list */
									ret = session_send(s, &mhdr, sizeof(MFL_FILE_HDR_T), QUEUE_ENTRY_DATA_TYPE_BINARY);
									if(ret < 0){
										LOG_ERROR("Send MFL Fail");
										return -1;
									}
									ret = session_send(s, secu_data, mhdr.secu_len, QUEUE_ENTRY_DATA_TYPE_BINARY);
									if(ret < 0){
										LOG_ERROR("Send MFL Fail");
										return -1;
									}
									ret = session_send(s, subpath, mhdr.path_size, QUEUE_ENTRY_DATA_TYPE_BINARY);
									if(ret < 0){
										LOG_ERROR("Send MFL Fail");
										return -1;
									}

									WriteFile(fl->hMFL, &mhdr.File_attribute, sizeof(DWORD), &written, NULL);
									WriteFile(fl->hMFL, &mhdr.path_size, sizeof(int), &written, NULL);
									WriteFile(fl->hMFL, subpath, mhdr.path_size, &written, NULL);
							}
						}else{
							/* size */
							if(remote_cur->hdr.filesize != sbuf.st_size){
								/* insert modified file list */
								ret = session_send(s, &mhdr, sizeof(MFL_FILE_HDR_T), QUEUE_ENTRY_DATA_TYPE_BINARY);
								if(ret < 0){
									LOG_ERROR("Send MFL Fail");
									return -1;
								}
								ret = session_send(s, secu_data, mhdr.secu_len, QUEUE_ENTRY_DATA_TYPE_BINARY);
								if(ret < 0){
									LOG_ERROR("Send MFL Fail");
									return -1;
								}
								ret = session_send(s, subpath, mhdr.path_size, QUEUE_ENTRY_DATA_TYPE_BINARY);
								if(ret < 0){
									LOG_ERROR("Send MFL Fail");
									return -1;
								}

								WriteFile(fl->hMFL, &mhdr.File_attribute, sizeof(DWORD), &written, NULL);
								WriteFile(fl->hMFL, &mhdr.path_size, sizeof(int), &written, NULL);
								WriteFile(fl->hMFL, subpath, mhdr.path_size, &written, NULL);
							}
						}
						/* move next both */
						remote_cur = get_next_afl(fl);
						i++;
					}else if(ret > 0){
						/* source only */
						/* insert modified file list */
						ret = session_send(s, &mhdr, sizeof(MFL_FILE_HDR_T), QUEUE_ENTRY_DATA_TYPE_BINARY);
						if(ret < 0){
							LOG_ERROR("Send MFL Fail");
							return -1;
						}
						ret = session_send(s, secu_data, mhdr.secu_len, QUEUE_ENTRY_DATA_TYPE_BINARY);
						if(ret < 0){
							LOG_ERROR("Send MFL Fail");
							return -1;
						}
						ret = session_send(s, subpath, mhdr.path_size, QUEUE_ENTRY_DATA_TYPE_BINARY);
						if(ret < 0){
							LOG_ERROR("Send MFL Fail");
							return -1;
						}

						WriteFile(fl->hMFL, &mhdr.File_attribute, sizeof(DWORD), &written, NULL);
						WriteFile(fl->hMFL, &mhdr.path_size, sizeof(int), &written, NULL);
						WriteFile(fl->hMFL, subpath, mhdr.path_size, &written, NULL);

						/* move next local */
						i++;
					}else{
						/* Send TOL */
						if(g_target_delete){
							ret = session_send(s, &mhdr, sizeof(MFL_FILE_HDR_T), QUEUE_ENTRY_DATA_TYPE_BINARY);
							if(ret < 0){
								LOG_ERROR("Send MFL Fail");
								return -1;
							}

							ret = session_send(s, remote_cur->path, mhdr.path_size, QUEUE_ENTRY_DATA_TYPE_BINARY);
							if(ret < 0){
								LOG_ERROR("Send MFL Fail");
								return -1;
							}
						}

						/* move next remote */
						remote_cur = get_next_afl(fl);
					}
				}else{
					/* source only */
					/* insert modified file list */
					/* Set MFL hdr */
					mhdr.modified_type = MODIFIED_TYPE_SO;
					mhdr.filesize = sbuf.st_size;
					mhdr.path_size = strlen(subpath);

					/* Get File Attribute */
					mhdr.File_attribute = GetFileAttributesA(subpath);


					/* Get File Security */
					secu_data = MakeSetSecuInfo(subpath, &mhdr.secu_len, &mhdr.secu_info);
					if(!secu_data){
						/* error */
						LOG_ERROR("Make Security Information Fail, file name : %s", subpath);
						return -1;
					}
					if(mhdr.path_size > MAX_PATH || mhdr.secu_len > MAX_PATH){
						LOG_ERROR("Wrong Path length, file name : %s", subpath);
						return -1;
					}

					ret = session_send(s, &mhdr, sizeof(MFL_FILE_HDR_T), QUEUE_ENTRY_DATA_TYPE_BINARY);
					if(ret < 0){
						LOG_ERROR("Send MFL Fail");
						return -1;
					}
					ret = session_send(s, secu_data, mhdr.secu_len, QUEUE_ENTRY_DATA_TYPE_BINARY);
					if(ret < 0){
						LOG_ERROR("Send MFL Fail");
						return -1;
					}
					ret = session_send(s, subpath, mhdr.path_size, QUEUE_ENTRY_DATA_TYPE_BINARY);
					if(ret < 0){
						LOG_ERROR("Send MFL Fail");
						return -1;
					}

					WriteFile(fl->hMFL, &mhdr.File_attribute, sizeof(DWORD), &written, NULL);
					WriteFile(fl->hMFL, &mhdr.path_size, sizeof(int), &written, NULL);
					WriteFile(fl->hMFL, subpath, mhdr.path_size, &written, NULL);

					/* move next local */
					i++;
				}
			}
		}
		if(depth == 0){
			remote_cur = get_curr_afl(fl);
			while(remote_cur){
				/* Set TOL hdr */
				if(g_target_delete){
					mhdr.modified_type = MODIFIED_TYPE_TO;
					mhdr.File_attribute = FILE_ATTRIBUTE_DIRECTORY;
					mhdr.path_size = strlen(remote_cur->path);	

					/* Send TOL */
					ret = session_send(s, &mhdr, sizeof(MFL_FILE_HDR_T), QUEUE_ENTRY_DATA_TYPE_BINARY);
					if(ret < 0){
						LOG_ERROR("Send MFL Fail");
						return -1;
					}

					ret = session_send(s, remote_cur->path, mhdr.path_size, QUEUE_ENTRY_DATA_TYPE_BINARY);
					if(ret < 0){
						LOG_ERROR("Send MFL Fail");
						return -1;
					}
				}

				/* move next remote */
				remote_cur = get_next_afl(fl);
			}
		}

		SAFE_FREE(files);
	}

	return 0;
}

#endif

int first_history(file_list_t *fl)
{
	DWORD written;
	uint64 tmp;

	fl->hHistory = CreateFileA(fl->history, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(fl->hHistory ==  INVALID_HANDLE_VALUE){
		fl->hHistory = CreateFileA(fl->history, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if(fl->hHistory ==  INVALID_HANDLE_VALUE){
			LOG_ERROR(LOG_MOD_FILE, "history file[%s] open fail, error code : %d\n", fl->history, GetLastError());
			return -1;				
		}	
	}

	tmp = 0;
	WriteFile(fl->hHistory, &tmp, sizeof(uint64), &written, NULL);
	WriteFile(fl->hHistory, &tmp, sizeof(uint64), &written, NULL);
	WriteFile(fl->hHistory, &tmp, sizeof(uint64), &written, NULL);
	WriteFile(fl->hHistory, &(fl->total_size), sizeof(uint64), &written, NULL);

	SAFE_CLOSE_HANDLE(fl->hHistory);

	return 0;
}

int compare_file_list(file_list_t *fl)
{
	list_node_t *pos, *nxt;
	dir_t *dir;
	int ret, err;

	fl->hAFL = CreateFileA(fl->all_files, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if( fl->hAFL == INVALID_HANDLE_VALUE )
	{
		int err = GetLastError();
		LOG_ERROR(LOG_MOD_FILE, "File(%s) Open Error : %d", fl->all_files, err);
		return -1;
	}	

	fl->hMFL = CreateFileA(fl->modified_files, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if( fl->hMFL == INVALID_HANDLE_VALUE )
	{
		int err = GetLastError();
		LOG_ERROR(LOG_MOD_FILE, "Open File [%s] Open Error : %d", fl->modified_files, err);
		return -1;
	}

	segment_new(fl, &err);

	list_for_each_safe(pos, nxt, &fl->dir_list)
	{
		dir = list_entry(pos, dir_t, lnode);
		
		//segment_condition_set(fl, dir->name, 0, 0, &err);
		ret = divided_compare_file_list(fl,dir->name, 0, 0, list_is_last_entry(dir, dir_t, lnode, &fl->dir_list));	
	}

	/* Write Segment List */
	if(!ret){
		segment_last_check(fl);
		segment_print(fl);
		segment_write(fl);

		first_history(fl);
	}

	SetEndOfFile(fl->hAFL);
	SAFE_CLOSE_HANDLE(fl->hAFL);

	SetEndOfFile(fl->hMFL);
	SAFE_CLOSE_HANDLE(fl->hMFL);

	return ret;
}

#if 0
/* MFL file 을 업데이트하면서 remote 에 Send Remote 에서도 파일 생성을 병렬도 처리한다. 
* MFL 파일에 Security 정보 및 파일 정보를 적지 않아도 되므로 MFL 파일의 크기가 줄어든다. 
* MFL 정보 전송을 위한 protocol 이 달라진다. 미리 Remote 에 알릴 수 있어야 한다. 
* TOL 은 무시한다. */
int compare_and_send_file_list(void *s, file_list_t *fl)
{
	list_node_t *pos, *nxt;
	dir_t *dir;
	session_t *session = s;
	MFL_FILE_HDR_T mhdr;
	int ret;

	fl->hAFL = CreateFileA(fl->all_files, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,NULL);
	if( fl->hAFL == INVALID_HANDLE_VALUE )
	{
		int err = GetLastError();
		LOG_ERROR("File(%s) Open Error : %d", fl->all_files, err);
		return -1;
	}	

	fl->hMFL = CreateFileA(fl->modified_files, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,NULL);
	if( fl->hAFL == INVALID_HANDLE_VALUE )
	{
		int err = GetLastError();
		LOG_ERROR("Open File [%s] Open Error : %d", fl->modified_files, err);
		return -1;
	}

	list_for_each_safe(pos, nxt, &fl->dir_list)
	{
		dir = list_entry(pos, dir_t, lnode);
		ret = divided_compare_and_send_file_list(session, fl, dir->name, 0);	
		if(ret < 0)
		{
			LOG_ERROR("Compare and Send file list fail");
			return -1;
		}
	}

	/* Send NULL mfl hdr for END */
	mhdr.path_size = -1;
	ret = session_send(s, &mhdr, sizeof(MFL_FILE_HDR_T), QUEUE_ENTRY_DATA_TYPE_BINARY);
	if(ret < 0){
		LOG_ERROR("Send NULL MFL fail");
		return -1;
	}

	SetEndOfFile(fl->hAFL);
	CloseHandle(fl->hAFL);
	fl->hAFL = INVALID_HANDLE_VALUE;

	SetEndOfFile(fl->hMFL);
	CloseHandle(fl->hMFL);
	fl->hMFL = INVALID_HANDLE_VALUE;

	return 0;
}


#endif

int dummy_file_generate(file_list_t *fl)
{
#ifdef _SOL_
	int ret, readn;
	file_msg_t hdr;
	char path[1024];
	char from[1024];// = {0,};
	HANDLE hFile;
	SHFILEOPSTRUCTA shfo;
	DWORD dwFileAttr;
	LARGE_INTEGER offset;

	fl->hSOL = CreateFileA(fl->src_only_files, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,NULL);
	if( fl->hAFL == INVALID_HANDLE_VALUE )
	{
		int err = GetLastError();
		LOG_ERROR("File(%s) Open Error : %d", fl->tgt_only_files, err);
		return -1;
	}

	do{
		/* hdr */
		ret = ReadFile(fl->hSOL, &hdr, sizeof(file_msg_t), &readn, NULL);
		if(ret == 0){
			if(GetLastError() == ERROR_HANDLE_EOF)
			{
				break;
			}else{
				LOG_ERROR("ReadFile Fail");
				break;
			}
		}

		if(readn == 0)
		{
			break;
		}

		/* Get Path */
		memset(path, 0, 1024);

		ret = ReadFile(fl->hTOL, &path, hdr.path_size, &readn, NULL);
		if(ret == 0){
			if(GetLastError() == ERROR_HANDLE_EOF)
			{
				break;
			}else{
				LOG_ERROR("ReadFile Fail");
				break;
			}
		}

		if(readn == 0)
		{
			break;
		}

		/* dummy file create */
		if(hdr.type == INITSYNC_FTYPE_DIRECTORY){
			ret = CreateDirectoryA(path, NULL);
			if( ret == FALSE )
			{
#if _DEBUG
				LOG_DEBUG( "mkdir fail: %s  -- (%u)", path, GetLastError());
#endif
			}			
		}else{
			hFile = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,NULL);
			if( hFile == INVALID_HANDLE_VALUE )
			{
				int err = GetLastError();
				LOG_ERROR("File(%s) Open Error : %d", path, err);
				return -1;
			}	

			/* todo */
			/* mode 정보 소유권 정보는 어떻게 맞춰주지... */
			/* Truncate */
			//SetFilePointer(hFile, hdr.sbuf.st_size, NULL, FILE_BEGIN);
			offset.QuadPart = hdr.sbuf.st_size;
			SetFilePointerEx(hFile, offset, NULL, FILE_BEGIN);
			SetEndOfFile(hFile);
			CloseHandle(hFile);
		}

	}while(readn);

	CloseHandle(fl->hTOL);
#endif
	return 0;
}

#if 0
int truncate_modified_file(file_list_t *fl)
{
	int ret, readn;
	TCHAR path[1024];
	char secu_data[1024];
	TCHAR from[1024];// = {0,};
	HANDLE hFile;
	LARGE_INTEGER offset;
	MFL_FILE_HDR_T hdr;

	fl->hMFL = CreateFileA(fl->modified_files, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,NULL);
	if( fl->hMFL == INVALID_HANDLE_VALUE )
	{
		int err = GetLastError();
		LOG_ERROR("Open File [%s] fail, error code : %d", fl->modified_files, err);
		return -1;
	}

	do{
		if(g_stopFlag )
			break;

		/* hdr */
		ret = ReadFile(fl->hMFL, &hdr, sizeof(MFL_FILE_HDR_T), &readn, NULL);
		if(ret == 0){
			if(GetLastError() == ERROR_HANDLE_EOF)
			{
				break;
			}else{
				LOG_ERROR("Read MFL file Fail, error code : %d", GetLastError());
				break;
			}
		}

		if(readn == 0)
		{
			break;
		}

		/* Get Secu */
		memset(secu_data, 0, 1024);

		ret = ReadFile(fl->hMFL, secu_data, hdr.secu_len, &readn, NULL);
		if(ret == 0){
			if(GetLastError() == ERROR_HANDLE_EOF)
			{
				break;
			}else{
				LOG_ERROR("Read MFL file Fail, error code : %d", GetLastError());
				break;
			}
		}

		if(readn == 0)
		{
			break;
		}

		/* Get Path */
		memset(path, 0, 1024*sizeof(TCHAR));

		ret = ReadFile(fl->hMFL, path, hdr.path_size, &readn, NULL);
		if(ret == 0){
			if(GetLastError() == ERROR_HANDLE_EOF)
			{
				break;
			}else{
				LOG_ERROR("Read MFL file Fail, error code : %d", GetLastError());
				break;
			}
		}

		if(readn == 0)
		{
			break;
		}

		/* Modified file truncate */
		if(hdr.File_attribute & FILE_ATTRIBUTE_DIRECTORY){
			/* Create Directory */
			ret = CreateDirectory(path, NULL);
			if( ret == FALSE )
			{
				LOG_ERROR("Make Dir [%s] Fail, error code : %d", path, GetLastError());
			}	
			/* change Attribute */
			SetFileAttributes(path, hdr.File_attribute);
		}else{
			hFile = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,NULL);
			if( hFile == INVALID_HANDLE_VALUE )
			{
				int err = GetLastError();
				LOG_ERROR("Create File [%s] Fail, error code : %d", path, err);
				return -1;
			}	

			/* change Attribute */
			SetFileAttributes(path, hdr.File_attribute);

			/* Truncate */
			offset.QuadPart = hdr.filesize;
			SetFilePointerEx(hFile, offset, NULL, FILE_BEGIN);
			SetEndOfFile(hFile);
			CloseHandle(hFile);

			/* Security information sync */
			ret = SetSecurity(path, secu_data, hdr.secu_len, hdr.secu_info);
			if(ret < 0){
				LOG_ERROR("Set Security [%s] Fail", path);
			}
		}

	}while(readn);

	return 0;
}

#endif

/* Create Files */
HANDLE generate_file(void *hdr, TCHAR *path, char *secu_data, DWORD *err)
{
	int ret;
	TCHAR from[1024];// = {0,};
	HANDLE hFile;
	SHFILEOPSTRUCT shfo;
	MFL_FILE_HDR_T *mhdr = hdr;
	DWORD dwFileAttr;

	*err = ERROR_SUCCESS;

	if(mhdr->modified_type == MODIFIED_TYPE_TO){
		/* change Attribute */
		// See if file is read-only : if so unset read-only
		dwFileAttr = GetFileAttributes(path);
		if (dwFileAttr & FILE_ATTRIBUTE_READONLY)
		{
			dwFileAttr &= ~FILE_ATTRIBUTE_READONLY;
			ret = SetFileAttributes(path, dwFileAttr);
			if(!ret){
				*err = GetLastError();
				return INVALID_HANDLE_VALUE;
			}
		}

		/* delete file */
		if(g_target_delete){
			if(mhdr->File_attribute & FILE_ATTRIBUTE_DIRECTORY){
				memset(&shfo, 0, sizeof(SHFILEOPSTRUCT));

				//_snprintf_s(from, _countof(from), _TRUNCATE, "%s%c", path, '\0');
				_stprintf(from, _T("%s%c"), path, '\0'); 

				shfo.hwnd = NULL;
				shfo.wFunc = FO_DELETE;
				shfo.lpszProgressTitle = NULL;
				shfo.fFlags = FOF_SILENT | FOF_NOCONFIRMATION;
				shfo.pTo = NULL;
				shfo.pFrom = from;

				shfo.fAnyOperationsAborted = FALSE;
				shfo.hNameMappings = NULL;

				ret = SHFileOperation(&shfo);
				if(ret)
				{
					LOG_ERROR_W(LOG_MOD_FILE, _T("Delete Dir [%s] Fail, error code : %d"), path, ret);
					*err = GetLastError();
					return INVALID_HANDLE_VALUE;
				}

			}else{
				ret = DeleteFile(path);
				if(!ret){
					*err = GetLastError();
					if(*err != ERROR_FILE_NOT_FOUND){
						LOG_ERROR_W(LOG_MOD_FILE, _T("Delete File [%s] Fail, error code : %d"), path, *err);
						return INVALID_HANDLE_VALUE;
					}
					*err = ERROR_SUCCESS;
				}
			}
		}

		return INVALID_HANDLE_VALUE;
	}else{
		/* Modified file */
		if(mhdr->File_attribute & FILE_ATTRIBUTE_DIRECTORY){
			/* Create Directory */
			ret = CreateDirectory(path, NULL);
			if( ret == FALSE )
			{
				*err = GetLastError();
				if(*err != ERROR_ALREADY_EXISTS){
					LOG_ERROR_W(LOG_MOD_FILE, _T("Make Dir [%s] Fail, error code : %d"), path, *err);
					return INVALID_HANDLE_VALUE;
				}
			}	
			/* change Attribute */
			SetFileAttributes(path, mhdr->File_attribute);

			/* Security information sync */
			ret = SetSecurity(path, secu_data, mhdr->secu_len, mhdr->secu_info);
			if(ret < 0){
				//*err = GetLastError();
				LOG_ERROR_W(LOG_MOD_FILE, _T("Set Security [%s] Fail"), path);
				//return INVALID_HANDLE_VALUE;
			}

			*err = ERROR_SUCCESS;
			return INVALID_HANDLE_VALUE;
		}else{
			dwFileAttr = GetFileAttributes(path);
			if(dwFileAttr != INVALID_FILE_ATTRIBUTES){
				if (dwFileAttr & FILE_ATTRIBUTE_READONLY)
				{
					dwFileAttr &= ~FILE_ATTRIBUTE_READONLY;
					ret = SetFileAttributes(path, dwFileAttr);
					if(!ret){
						*err = GetLastError();
						return INVALID_HANDLE_VALUE;
					}
				}
			}else{
				dwFileAttr = mhdr->File_attribute;
				if (dwFileAttr & FILE_ATTRIBUTE_READONLY)
				{
					dwFileAttr &= ~FILE_ATTRIBUTE_READONLY;
				}
			}

			hFile = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, dwFileAttr, NULL);
			if( hFile == INVALID_HANDLE_VALUE )
			{
				*err = GetLastError();
				if(*err != ERROR_ALREADY_EXISTS){
					LOG_ERROR_W(LOG_MOD_FILE, _T("Create File [%s] Fail, error code : %d"), path, *err);
					return INVALID_HANDLE_VALUE;
				}
			}	

			return hFile;
		}
	}

	return INVALID_HANDLE_VALUE;
}

HANDLE generate_local_rep_file(file_list_t *fl, void *hdr, TCHAR *path, char *secu_data, DWORD *err)
{
	file_manager_t *fm = fl->fm;
	HANDLE ret;

	path = convert_path_to_local_rep_path(fl, path);

	ret = generate_file(hdr, path, secu_data, err);

	SAFE_FREE(path);

	return ret;
}

void DeleteLocalReplicationFile(file_list_t *fl, TCHAR *path)
{
	file_manager_t *fm = fl->fm;
	HANDLE ret;

	path = convert_path_to_local_rep_path(fl, path);

	DeleteFile(path);

	SAFE_FREE(path);
}


int create_hitory_mmap(file_list_t *fl)
{
	/* file open, mmap open */
	if(fl->hHistory != INVALID_HANDLE_VALUE)
		goto error_out;

	fl->hHistory = CreateFileA(fl->history, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(fl->hHistory ==  INVALID_HANDLE_VALUE){
		fl->hHistory = CreateFileA(fl->history, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if(fl->hHistory ==  INVALID_HANDLE_VALUE){
			LOG_ERROR(LOG_MOD_FILE, "history file[%s] open fail, error code : %d\n", fl->history, GetLastError());
			goto error_out;				
		}	
	}

	fl->hMapFile = CreateFileMapping(fl->hHistory, NULL, PAGE_READWRITE, 0, sizeof(uint64) * 4, NULL);
	if(fl->hMapFile == NULL){
		LOG_ERROR(LOG_MOD_FILE, "Create file Map fail, error code : %d\n", GetLastError());
		goto error_out;
	}

	fl->pWrite = (uint64 *)MapViewOfFile(fl->hMapFile, FILE_MAP_WRITE, 0, 0, 0);
	if(fl->hMapFile == NULL){
		LOG_ERROR(LOG_MOD_FILE, "Map View fail, error code : %d\n", GetLastError());
		goto error_out;
	}

	return 0;

error_out:
	return -1;
}

int close_history_mmap(file_list_t *fl)
{
	if(fl->pWrite){
		/* Close history file, mmap */
		UnmapViewOfFile(fl->pWrite);
		fl->pWrite = NULL;
	}

	SAFE_CLOSE_HANDLE(fl->hMapFile);
	SAFE_CLOSE_HANDLE(fl->hHistory);

	return 0;
}

int write_history(file_list_t *fl, uint64 offset, uint64 idx, uint64 filesize)
{
	fl->sent_size += filesize;

	fl->pWrite[0] = offset;
	fl->pWrite[1] = idx;
	fl->pWrite[2] = fl->sent_size;
	fl->pWrite[3] = fl->total_size;

	return 0;
}

int flist_sync_fail(file_list_t *fl, TCHAR *origin_path, TCHAR *new_path, int recved_err)
{
	char buf[4096];
	int buf_len;
	int ret;
	DWORD written;
	LPVOID *err_msg;
	wchar_t bom = 0xFEFF;
	int err;

	if(fl->hSyncFail == INVALID_HANDLE_VALUE){
		INITSYNC_FILE_CREATE(fl->hSyncFail, fl->sync_fail, err, error_out);

		INITSYNC_FILE_WRITE(fl->hSyncFail, &bom, 2, err, error_out);
	}
 
	ret = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 
		NULL, recved_err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&err_msg, 0, NULL);

	buf_len = 0;
	if(new_path){
		buf_len += swprintf(buf, 4096, _T("[SYNC_FAIL] %s [is renamed %s]\n\t\tERROR : %s"), origin_path, new_path, (TCHAR *)err_msg);
	}else{
		buf_len += swprintf(buf, 4096, _T("[SYNC_FAIL] %s \n\t\tERROR : %s"), origin_path, (TCHAR *)err_msg);
	}

	buf_len = buf_len * sizeof(TCHAR);

	INITSYNC_FILE_WRITE(fl->hSyncFail, buf, buf_len, err, error_out);

	if(err_msg)
		LocalFree(err_msg);
error_out:
	return -1;
}