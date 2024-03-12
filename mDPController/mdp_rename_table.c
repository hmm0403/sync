#pragma comment(lib, "Rpcrt4.lib")

#include "mdp_rename_table.h"
#include "report.h"
//#include "user_util.h"
#include "defines.h"
#include "Rpcdce.h"

typedef struct _rename_tbl_double_linked_list_tag
{
	struct _rename_tbl_double_linked_list_tag  *prev;
	struct _rename_tbl_double_linked_list_tag  *next;
} _rt_dbll_t;

typedef struct _rename_tbl_entry_tag
{
	_rt_dbll_t	list;
	_rt_dbll_t	type_list;
	_rt_dbll_t  created_list;

	unsigned int  mode;
	unsigned int  bp_depth;

	TCHAR         *begin_path;
	TCHAR         *final_path;
	TCHAR		  *tmp_path;

	int           bp_len;
	int           fp_len;
	int			  tp_len;
} _rename_tbl_entry_t;

typedef struct _rename_tbl_tag
{
	_rename_tbl_entry_t *head, *tail;
	long count;
	unsigned long size;
}_rename_tbl_t;

enum link_type {
	_RENAME_TBL_LINK = 0,
	_RENAME_TBL_TYPE_LINK = 1,
};

enum _rename_tbl_add_result_type {
	_RENAME_TBL_DONT_NEED_ADD = 0,
	_RENAME_TBL_ADD_LIST = 1,
	_RENAME_TBL_NEED_RENAME = 2
};

#define  _RT_PREV(c)                      c->list.prev
#define  _RT_NEXT(c)                      c->list.next

#define  _RT_C_NEXT(c)                    (_rename_tbl_entry_t *)_RT_NEXT(c)
#define  _RT_C_PREV(c)                    (_rename_tbl_entry_t *)_RT_PREV(c)
                                          
#define  _RT_TYPE_PREV(c)                 c->type_list.prev
#define  _RT_TYPE_NEXT(c)                 c->type_list.next

#define  _RT_C_TYPE_PREV(c)               (_rename_tbl_entry_t *)_RT_TYPE_PREV(c)
#define  _RT_C_TYPE_NEXT(c)               (_rename_tbl_entry_t *)_RT_TYPE_NEXT(c)

#define  _RT_GET_PATH(c)                  ((c->final_path==NULL)?c->begin_path:c->final_path)
#define  _RT_GET_LEN(c)                   ((c->final_path==NULL)?c->bp_len:c->fp_len)

#define  _RT_CMP_DIR(c,p,l)               (!_tcsncmp(_RT_GET_PATH(c),p,l))
#define  _RT_CMP_PATH(c,p)                (!_tcscmp( _RT_GET_PATH(c),p))

#define  _RT_ADDMODE_REMOVE(c)            (c->mode |= _RENAME_TBL_REMOVE)
#define  _RT_ADDMODE_RENAME(c)            (c->mode |= _RENAME_TBL_RENAME)

#define  _RT_CHECK_REMOVE(c)              (c->mode & _RENAME_TBL_REMOVE)
#define  _RT_CHECK_RENAME(c)              (c->mode & _RENAME_TBL_RENAME)
#define  _RT_CHECK_CREATE(c)              (c->mode & _RENAME_TBL_CREATE)

#define  _RT_LINK(i, c)                  rename_tbl_link(&g_rename_tbl[i],      c, _RENAME_TBL_LINK)
#define  _RT_DIR_LINK(i, c)              rename_tbl_link(&g_rename_tbl_dir[i],  c, _RENAME_TBL_TYPE_LINK)
#define  _RT_FILE_LINK(i, c)             rename_tbl_link(&g_rename_tbl_file[i], c, _RENAME_TBL_TYPE_LINK)

#define  _RT_DEL_LINK(i, c)                  rename_tbl_link_remove(&g_rename_tbl[i],      c, _RENAME_TBL_LINK)
#define  _RT_DEL_DIR_LINK(i, c)              rename_tbl_link_remove(&g_rename_tbl_dir[i],  c, _RENAME_TBL_TYPE_LINK)
#define  _RT_DEL_FILE_LINK(i, c)             rename_tbl_link_remove(&g_rename_tbl_file[i], c, _RENAME_TBL_TYPE_LINK)

#define  _RT_PREV_LINK(i, p,n)           rename_tbl_prev_link(&g_rename_tbl[i],      p, n, _RENAME_TBL_LINK)
#define  _RT_DIR_PREV_LINK(i, p,n)       rename_tbl_prev_link(&g_rename_tbl_dir[i],  p, n, _RENAME_TBL_TYPE_LINK)
#define  _RT_FILE_PREV_LINK(i, p,n)      rename_tbl_prev_link(&g_rename_tbl_file[i], p, n, _RENAME_TBL_TYPE_LINK)
// mode change for remove file cause by rename() : new path is exist & ( file or empty directory )
#define  _RT_MODE_CHG_1(m)            ((m & _RENAME_TBL_FILE_TYPE) | _RENAME_TBL_REMOVE)

#define  _RT_PRINT_FINAL(c)           ((c->final_path)?c->final_path:_T("NOT EXIST"))

#define MDP_MAX_HOSTS (16)

#define SM_ERRNO_ALLOC_FAIL (-1)

_rename_tbl_t g_rename_tbl[MDP_MAX_HOSTS];// = {NULL, NULL, 0, 0};
_rename_tbl_t g_rename_tbl_dir[MDP_MAX_HOSTS];// = {NULL, NULL, 0, 0};
_rename_tbl_t g_rename_tbl_file[MDP_MAX_HOSTS];// = {NULL, NULL, 0, 0};

void rename_tbl_entry_remove( _rename_tbl_entry_t *pr_rm );

void rename_tbl_print_mode(int pr_mode)
{

}

void rename_tbl_print_list(void)
{

}

void rename_tbl_link_remove(_rename_tbl_t *pr_hdr, _rename_tbl_entry_t *pr_entry, int pr_type){
	_rename_tbl_entry_t *pr_prev, *pr_next;

	if( pr_type == _RENAME_TBL_LINK )
	{
		pr_prev = _RT_C_PREV(pr_entry);
		pr_next = _RT_C_NEXT(pr_entry);

		if(pr_prev != NULL){
			_RT_NEXT(pr_prev) = (_rt_dbll_t *)pr_next;
		}else{
			pr_hdr->head = pr_next;
		}

		if(pr_next != NULL){
			_RT_PREV(pr_next) = (_rt_dbll_t *)pr_prev;
		}else{
			pr_hdr->tail = pr_prev;
		}

		_RT_PREV(pr_entry) = NULL;
		_RT_NEXT(pr_entry) = NULL;
	}else{
		pr_prev = _RT_C_TYPE_PREV(pr_entry);
		pr_next = _RT_C_TYPE_NEXT(pr_entry);

		if(pr_prev != NULL){
			_RT_TYPE_NEXT(pr_prev) = (_rt_dbll_t *)pr_next;
		}else{
			pr_hdr->head = pr_next;
		}

		if(pr_next != NULL){
			_RT_TYPE_PREV(pr_next) = (_rt_dbll_t *)pr_prev;
		}else{
			pr_hdr->tail = pr_prev;
		}

		_RT_TYPE_PREV(pr_entry) = NULL;
		_RT_TYPE_NEXT(pr_entry) = NULL;
	}

	pr_hdr->count--;
}

void rename_tbl_link(_rename_tbl_t *pr_hdr, _rename_tbl_entry_t *pr_entry, int pr_type)
{
	if( pr_hdr->head == NULL )
		pr_hdr->head   = pr_entry;
	else
	{
		if( pr_type == _RENAME_TBL_LINK )
		{
			_RT_NEXT(pr_hdr->tail)       = (_rt_dbll_t *)pr_entry;
			_RT_PREV(pr_entry)             = (_rt_dbll_t *)pr_hdr->tail;
		}
		else if ( pr_type == _RENAME_TBL_TYPE_LINK )
		{
			_RT_TYPE_NEXT(pr_hdr->tail)  = (_rt_dbll_t *)pr_entry;
			_RT_TYPE_PREV(pr_entry)        = (_rt_dbll_t *)pr_hdr->tail;
		}
	}
	pr_hdr->tail   = pr_entry;
}

void rename_tbl_prev_link( _rename_tbl_t *pr_hdr, _rename_tbl_entry_t *pr_prev, _rename_tbl_entry_t *pr_next, int pr_type )
{
	_rename_tbl_entry_t *pr_prev_prev = _RT_C_PREV(pr_next);

	if( pr_hdr->head == pr_next )
		pr_hdr->head   = pr_prev;
	else
	{
		if( pr_type == _RENAME_TBL_LINK )
		{
			_RT_PREV(pr_prev)  = _RT_PREV(pr_next);
			_RT_NEXT(pr_prev)  = (_rt_dbll_t *)pr_next;
			if(pr_prev_prev)
				_RT_NEXT(pr_prev_prev) = (_rt_dbll_t *)pr_prev;
			else
				pr_hdr->head = pr_prev;
			_RT_PREV(pr_next) = (_rt_dbll_t *)pr_prev;
		}
		else
		{
			_RT_TYPE_PREV(pr_prev)  = _RT_TYPE_PREV(pr_next);
			_RT_TYPE_NEXT(pr_prev)  = (_rt_dbll_t *)pr_next;
			if(pr_prev_prev)
				_RT_TYPE_NEXT(pr_prev_prev) = (_rt_dbll_t *)pr_prev;
			else
				pr_hdr->head = pr_prev;
			_RT_TYPE_PREV(pr_next) = (_rt_dbll_t *)pr_prev;
		}
	}
}

_rename_tbl_entry_t  *rename_tbl_findRTInfoInBegin_parent_dir(int pr_host_index, TCHAR *pr_path, int pr_len )
{
	_rename_tbl_entry_t  *cur = NULL;
	_rename_tbl_entry_t  *rlt = NULL;


	if( g_rename_tbl_dir[pr_host_index].head == NULL )
		return NULL;

	cur  = g_rename_tbl_dir[pr_host_index].head;
	do {
		if( pr_len > cur->bp_len && !_tcsncmp(cur->begin_path, pr_path, cur->bp_len) )
		{
			if((pr_path[cur->bp_len] == _T('\\')) && (!rlt || rlt->bp_depth < cur->bp_depth) )
				rlt  = cur;
		}
		cur  = _RT_C_TYPE_NEXT(cur);
	} while( cur );

	return rlt;
}

_rename_tbl_entry_t  *rename_tbl_findRTInfoInBegin_type( _rename_tbl_entry_t  *pr_head, TCHAR *pr_path, int pr_len )
{
	_rename_tbl_entry_t  *cur = NULL;


	cur  = pr_head;
	do {
		if( pr_len == cur->bp_len && !_tcscmp(cur->begin_path, pr_path) )
			return cur;
		cur  = _RT_C_TYPE_NEXT(cur);
	} while( cur );

	return NULL;
}

_rename_tbl_entry_t  *rename_tbl_findRTInfoInBegin_for_dir(int pr_host_index, TCHAR *pr_path, int pr_len )
{
	if( g_rename_tbl_dir[pr_host_index].head == NULL )
		return NULL;

	return rename_tbl_findRTInfoInBegin_type( g_rename_tbl_dir[pr_host_index].head, pr_path, pr_len );
}

_rename_tbl_entry_t  *rename_tbl_findRTInfoInBegin_for_file(int pr_host_index, TCHAR *pr_path, int pr_len )
{
	if( g_rename_tbl_file[pr_host_index].head == NULL )
		return NULL;

	return rename_tbl_findRTInfoInBegin_type( g_rename_tbl_file[pr_host_index].head, pr_path, pr_len );
}

_rename_tbl_entry_t  *rename_tbl_findRTInfoInBegin(int pr_host_index, int pr_type, TCHAR *pr_path, int pr_len )
{
	if( pr_type & _RENAME_TBL_DIR )
		return rename_tbl_findRTInfoInBegin_for_dir(pr_host_index, pr_path, pr_len );
	else
		return rename_tbl_findRTInfoInBegin_for_file(pr_host_index, pr_path, pr_len );
}

_rename_tbl_entry_t  *rename_tbl_findRTInfoInFinal_type( _rename_tbl_entry_t  *pr_head, TCHAR *pr_path, int pr_len )
{
	_rename_tbl_entry_t  *cur = NULL;


	cur  = pr_head;
	do {
		if( cur->final_path && pr_len == cur->fp_len && !_tcscmp(cur->final_path, pr_path) )
			return cur;
		cur  = _RT_C_TYPE_NEXT(cur);
	} while( cur );

	return NULL;
}

_rename_tbl_entry_t  *rename_tbl_findRTInfoInFinal_for_dir(int pr_host_index, TCHAR *pr_path, int pr_len )
{
	if( g_rename_tbl_dir[pr_host_index].head == NULL )
		return NULL;

	return rename_tbl_findRTInfoInFinal_type( g_rename_tbl_dir[pr_host_index].head, pr_path, pr_len );
}

_rename_tbl_entry_t  *rename_tbl_findRTInfoInFinal_for_file(int pr_host_index, TCHAR *pr_path, int pr_len )
{
	if( g_rename_tbl_file[pr_host_index].head == NULL )
		return NULL;

	return rename_tbl_findRTInfoInFinal_type( g_rename_tbl_file[pr_host_index].head, pr_path, pr_len );
}

_rename_tbl_entry_t *rename_tbl_findRTInfoInFinal_parent_dir(int pr_host_index, TCHAR *pr_path, int pr_len )
{
	_rename_tbl_entry_t  *cur = NULL;
	_rename_tbl_entry_t  *rlt = NULL;


	if( g_rename_tbl_dir[pr_host_index].head == NULL )
		return NULL;

	cur  = g_rename_tbl_dir[pr_host_index].head;
	do {
		if( pr_len > cur->fp_len && !_tcsncmp(cur->final_path, pr_path, cur->fp_len))
		{
			if((pr_path[cur->fp_len] == _T('\\')) && ( !rlt || rlt->bp_depth < cur->bp_depth ) )
				rlt  = cur;
		}
		cur  = _RT_C_TYPE_NEXT(cur);
	} while( cur );

	return rlt;
}

_rename_tbl_entry_t  *rename_tbl_findRTInfoInFinal(int pr_host_index, int pr_type, TCHAR *pr_path, int pr_len )
{
	if( pr_type & _RENAME_TBL_DIR )
		return rename_tbl_findRTInfoInFinal_for_dir(pr_host_index, pr_path, pr_len );
	else
		return rename_tbl_findRTInfoInFinal_for_file(pr_host_index, pr_path, pr_len );
}

// we have file ( A, B )
// rename( B, A ) <== A is remove. Below function is check this remove
int rename_tbl_renameRemoveCheck(
	    int		pr_host_index,
		int    pr_type,
		TCHAR  *pr_path,
		int    pr_len  )
{
	int              rtv = _RENAME_TBL_ADD_LIST;
	_rename_tbl_entry_t  *cur = NULL;


	if( pr_type & _RENAME_TBL_DIR )
	{
		if( g_rename_tbl_dir[pr_host_index].head )
			cur  = g_rename_tbl_dir[pr_host_index].head;
	}
	else
	{
		if( g_rename_tbl_file[pr_host_index].head )
			cur  = g_rename_tbl_file[pr_host_index].head;
	}

	if( cur )
	{
		do {
			if( !(_RT_CHECK_REMOVE(cur)) && _RT_GET_LEN(cur) == pr_len && _RT_CMP_PATH(cur, pr_path) )
			{
				rtv  = _RENAME_TBL_DONT_NEED_ADD;
				LOG_DEBUG_W(LOG_MOD_MDP_CNTRL , _T("ADD MODE: REMOVE: %s -- %s"), cur->begin_path, _RT_PRINT_FINAL(cur) );
				_RT_ADDMODE_REMOVE(cur);
				break;
			}
			cur  = _RT_C_TYPE_NEXT(cur);
		} while( cur );
	}

	return rtv;
}

int rename_tbl_upperDirRename(
		TCHAR   *pr_path,
		int     pr_len,
		TCHAR   *pr_new_dir,
		int     pr_old_len,
		int     pr_new_len,
		TCHAR  **pr_r_path
		)
{
	TCHAR  *path = NULL;
	int    len  = 0;


	len   = pr_len - pr_old_len + pr_new_len;
	path  = (TCHAR *)malloc( (len + 1)*sizeof(TCHAR) );
	if( !path )
	{
		LOG_ERROR( LOG_MOD_MDP_CNTRL , "Rename resync path for resync prepare job" );
		return SM_ERRNO_ALLOC_FAIL;
	}

	_stprintf( path, _T("%s%s"), pr_new_dir, (pr_path+pr_old_len) );
	LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("RR: %s -> %s"), pr_path, path );
	*pr_r_path  = path;

	return len;
}

int rename_tbl_renameUpperDir( 
		_rename_tbl_entry_t  *pr_rr,
		TCHAR            *pr_new_dir,
		int              pr_old_len,
		int              pr_new_len
		)
{
	TCHAR  *path = NULL;
	int    len  = 0;


	len  = rename_tbl_upperDirRename( _RT_GET_PATH(pr_rr), _RT_GET_LEN(pr_rr), pr_new_dir, pr_old_len, pr_new_len, &path );
	if( !path )
	{
		LOG_ERROR( LOG_MOD_MDP_CNTRL, "Rename resync path for resync prepare job" );
		return SM_ERRNO_ALLOC_FAIL;
	}
	if( pr_rr->final_path )
		free( _RT_GET_PATH(pr_rr) );
	pr_rr->final_path  = path;
	pr_rr->fp_len      = len;
	LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("  : ADD MODE: RENAME: %s -- %s"), pr_rr->begin_path, pr_rr->final_path );
	_RT_ADDMODE_RENAME(pr_rr);

	return len;
}

int rename_tbl_renameFullPath( 
		_rename_tbl_entry_t  *pr_rr,
		TCHAR            *pr_dir,
		int             pr_len
		)
{
	TCHAR  *path = NULL;


	path  = (TCHAR *)malloc( (pr_len + 1)*sizeof(TCHAR) );
	if( !path )
	{
		LOG_ERROR( LOG_MOD_MDP_CNTRL, "Rename resync path for resync prepare job" );
		return SM_ERRNO_ALLOC_FAIL;
	}

	_stprintf( path, _T("%s"), pr_dir );
	LOG_DEBUG( LOG_MOD_MDP_CNTRL, "RR: %s -> %s", _RT_GET_PATH(pr_rr), path );
	if( pr_rr->final_path )
		free( _RT_GET_PATH(pr_rr) );
	pr_rr->final_path  = path;
	pr_rr->fp_len      = pr_len;
	LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("  : ADD MODE: RENAME: %s -- %s"), pr_rr->begin_path, pr_rr->final_path );
	_RT_ADDMODE_RENAME(pr_rr);

	return pr_len;
}

int rename_tbl_removeDir_all( 
		int pr_host_index, 
		TCHAR *pr_begin_path,
		int pr_bp_len,
		TCHAR *pr_final_path, 
		int pr_fp_len )
{
	int              rtv = _RENAME_TBL_ADD_LIST;
	_rename_tbl_entry_t  *cur = NULL;
	_rename_tbl_entry_t  *del;

	if( g_rename_tbl_file[pr_host_index].head )
	{
		cur  = g_rename_tbl_file[pr_host_index].head;
		do {
			if( _RT_GET_LEN(cur) > pr_fp_len && _RT_CMP_DIR(cur, pr_final_path, pr_fp_len) )
			{
				if(_RT_GET_PATH(cur)[pr_fp_len] == _T('\\')){
					if( !_RT_CHECK_RENAME(cur) ){
						/* remove entry */
						del = cur;
						cur  = _RT_C_TYPE_NEXT(cur);

						_RT_DEL_LINK(pr_host_index, del);
						_RT_DEL_FILE_LINK(pr_host_index, del);

						rename_tbl_entry_remove(del);

						continue;
					}else{
						_RT_ADDMODE_REMOVE(cur);

						if( cur->bp_len > pr_bp_len && (!_tcsncmp(cur->begin_path, pr_begin_path, pr_bp_len))){
							if(cur->begin_path[pr_bp_len] == _T('\\')){
								/* remove entry */
								del = cur;
								cur  = _RT_C_TYPE_NEXT(cur);

								_RT_DEL_LINK(pr_host_index, del);
								_RT_DEL_FILE_LINK(pr_host_index, del);

								rename_tbl_entry_remove(del);

								continue;
							}
						}
					}
				}
			}
			cur  = _RT_C_TYPE_NEXT(cur);
		} while( cur );
	}

	if( g_rename_tbl_dir[pr_host_index].head )
	{
		cur  = g_rename_tbl_dir[pr_host_index].head;
		do {
			if( _RT_GET_LEN(cur) > pr_fp_len && _RT_CMP_DIR(cur, pr_final_path, pr_fp_len ) )
			{
				if(_RT_GET_PATH(cur)[pr_fp_len] == _T('\\')){
					if( !_RT_CHECK_RENAME(cur) ){
						/* remove entry */
						del = cur;
						cur  = _RT_C_TYPE_NEXT(cur);

						_RT_DEL_LINK(pr_host_index, del);
						_RT_DEL_DIR_LINK(pr_host_index, del);

						rename_tbl_entry_remove(del);

						continue;
					}else{
						_RT_ADDMODE_REMOVE(cur);

						if( cur->bp_len > pr_bp_len && (!_tcsncmp(cur->begin_path, pr_begin_path, pr_bp_len))){
							if(cur->begin_path[pr_bp_len] == _T('\\')){
								/* remove entry */
								del = cur;
								cur  = _RT_C_TYPE_NEXT(cur);

								_RT_DEL_LINK(pr_host_index, del);
								_RT_DEL_FILE_LINK(pr_host_index, del);

								rename_tbl_entry_remove(del);

								continue;
							}
						}
					}
				}
			}
			cur  = _RT_C_TYPE_NEXT(cur);
		} while( cur );
	}

	return rtv;
}

int rename_tbl_renameDir_all(
	    int		pr_host_index,
		TCHAR  *pr_begin_path,
		TCHAR  *pr_final_path,
		int    pr_bp_len,
		int    pr_fp_len )
{
	int              rtv = _RENAME_TBL_ADD_LIST;
	_rename_tbl_entry_t  *cur = NULL;


	if( g_rename_tbl_file[pr_host_index].head )
	{
		cur  = g_rename_tbl_file[pr_host_index].head;
		do {
			if( !(_RT_CHECK_REMOVE(cur)) && _RT_GET_LEN(cur) > pr_bp_len && _RT_CMP_DIR(cur, pr_begin_path, pr_bp_len) )
			{
				if(_RT_GET_PATH(cur)[pr_bp_len] == _T('\\'))
					rename_tbl_renameUpperDir( cur, pr_final_path, pr_bp_len, pr_fp_len );
			}
			cur  = _RT_C_TYPE_NEXT(cur);
		} while( cur );
	}

	if( g_rename_tbl_dir[pr_host_index].head )
	{
		cur  = g_rename_tbl_dir[pr_host_index].head;
		do {
			if( !(_RT_CHECK_REMOVE(cur)) && _RT_GET_LEN(cur) > pr_bp_len && _RT_CMP_DIR(cur, pr_begin_path, pr_bp_len ) )
			{
				if(_RT_GET_PATH(cur)[pr_bp_len] == _T('\\'))
					rename_tbl_renameUpperDir( cur, pr_final_path, pr_bp_len, pr_fp_len );
			}
			else if( _RT_GET_LEN(cur) == pr_bp_len && _RT_CMP_PATH(cur, pr_begin_path) )
			{
				rtv  = _RENAME_TBL_DONT_NEED_ADD;
				if( !(_RT_CHECK_REMOVE(cur)) )
					rename_tbl_renameFullPath( cur, pr_final_path, pr_fp_len );
				// see comment-1
			}
			cur  = _RT_C_TYPE_NEXT(cur);
		} while( cur );
	}

	return rtv;
}

int rename_tbl_renameFile(
	    int		pr_host_index,
		TCHAR  *pr_begin_path,
		TCHAR  *pr_final_path,
		int    pr_bp_len,
		int    pr_fp_len )
{
	int              rtv = _RENAME_TBL_ADD_LIST;
	_rename_tbl_entry_t  *cur = NULL;


	if( g_rename_tbl_file[pr_host_index].head )
	{
		cur  = g_rename_tbl_file[pr_host_index].head;
		do {
			if( (_RT_GET_LEN(cur) == pr_bp_len) && _RT_CMP_PATH(cur, pr_begin_path) )
			{
				rtv  = _RENAME_TBL_DONT_NEED_ADD;
				if( !(_RT_CHECK_REMOVE(cur)) )
				{
					rename_tbl_renameFullPath( cur, pr_final_path, pr_fp_len );
					rtv  = _RENAME_TBL_DONT_NEED_ADD;
					break;
				}
				// comment-1
				// why we continue when removed file??  ==> becasue..
				// example) A, B
				//     (1) remove A
				//     (2) rename B -> A
				//     (3) remove A
				// ==> then we find (remove A). first we face (1)  <== it's wrong
				//                              second we find (2) <== it's correct             
			}
			cur  = _RT_C_TYPE_NEXT(cur);
		} while( cur );
	}

	return rtv;
}

int rename_tbl_renameParentDirCheck(
		int		pr_host_index,
		TCHAR   *pr_path,
		int     pr_len,
		TCHAR  **pr_r_path
		)
{
	int              len  = 0;
	int              rtv  = _RENAME_TBL_ADD_LIST;
	TCHAR            *path = NULL;
	_rename_tbl_entry_t  *cur  = NULL;
	_rename_tbl_entry_t  *rlt  = NULL;


	if( g_rename_tbl_dir[pr_host_index].head )
		cur  = g_rename_tbl_dir[pr_host_index].head;

	if( cur )
	{
		do {
			if( !(_RT_CHECK_REMOVE(cur)) && _RT_GET_LEN(cur) < pr_len && _RT_CMP_DIR(cur, pr_path, _RT_GET_LEN(cur)) )
			{
				if( (pr_path[_RT_GET_LEN(cur)] == _T('\\')) &&(!rlt || rlt->bp_depth < cur->bp_depth) )
					rlt  = cur;
			}
			cur  = _RT_C_TYPE_NEXT(cur);
		} while( cur );
	}

	if( rlt )
	{
		len   = pr_len + rlt->bp_len - rlt->fp_len;
		path  = (TCHAR *)malloc( (len + 1)*sizeof(TCHAR) );
		if( !path )
		{
			LOG_ERROR( LOG_MOD_MDP_CNTRL, "rename parent dir change fail" );
			return SM_ERRNO_ALLOC_FAIL;
		}

		_stprintf( path, _T("%s%s"), rlt->begin_path, (pr_path+rlt->fp_len) );
		*pr_r_path  = path;
	}

	return len;
}


int rename_tbl_checker_tester(
		_rename_tbl_entry_t  *pr_final,
		_rename_tbl_entry_t  *pr_begin,
		TCHAR            *pr_path,
		int              pr_len  )
{
	int  rtv  = _RENAME_TBL_DONT_NEED_ADD;


	if( !pr_final )
	{
		if( !pr_begin )
		{
			LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("CHECK: %s is new, ADD"), pr_path );
			rtv  = _RENAME_TBL_ADD_LIST;
		}
		else
		{
			LOG_DEBUG_W( SM_ERRNO_ALLOC_FAIL, _T("CHECK: begin only: %s(F: %s)"), pr_begin->begin_path, _RT_PRINT_FINAL(pr_begin) );
			if( pr_begin->final_path )
				LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("                 : %s, NEW: remove it we don't need this"), pr_path );
			else
				LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("                 : %s, ERROR"), pr_path );
		}
	}
	else
	{
		if( pr_begin )
		{
			LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("CHECK: final: %s(B: %s) after come begin: %s(F: %s)"), pr_final->final_path, pr_final->begin_path, pr_begin->begin_path, _RT_PRINT_FINAL(pr_begin) );
			LOG_DEBUG( LOG_MOD_MDP_CNTRL, "            : ERROR" );
		}
		else
		{
			LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("CHECK: final only: %s(B: %s)"), pr_final->final_path, pr_final->begin_path );
			LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("                 : %s is child. ADD LIST"), pr_path);
			rtv  = _RENAME_TBL_NEED_RENAME;
		}
	}

	return rtv;
}

int rename_tbl_checker(
	    int		pr_host_index,
		int     pr_type,
		TCHAR   *pr_path,
		int     pr_len,
		TCHAR  **pr_r_path,
		int    *pr_r_len
		)
{
	int              rtv = _RENAME_TBL_ADD_LIST;
	int              len = 0;
	_rename_tbl_entry_t  *cur = NULL;
	_rename_tbl_entry_t  *_type_list = NULL;
	_rename_tbl_entry_t  *_dir_final = NULL;
	_rename_tbl_entry_t  *_dir_begin = NULL;


	if( pr_type & _RENAME_TBL_DIR )
		cur  = g_rename_tbl_dir[pr_host_index].head;
	else
		cur  = g_rename_tbl_file[pr_host_index].head;

	for( ;cur; cur = _RT_C_TYPE_NEXT(cur) )
	{
		if( !(_RT_CHECK_REMOVE(cur)) && _RT_GET_LEN(cur) == pr_len && _RT_CMP_PATH(cur, pr_path) )
		{
			if( pr_type & _RENAME_TBL_REMOVE ){
				LOG_DEBUG_W(LOG_MOD_MDP_CNTRL , _T("ADD MODE: REMOVE: %s -- %s"), cur->begin_path, _RT_PRINT_FINAL(cur) );
				_RT_ADDMODE_REMOVE(cur);
				*pr_r_path = cur->begin_path;
				*pr_r_len = cur->bp_len;
			}else{
				// Created Dir/File은 여기에 항상 걸린다. 밑에 조건으로 가지 않는다.
				LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("CHECK: same path: %s: B: %s -> F: %s"), pr_path, cur->begin_path, _RT_PRINT_FINAL(cur) );
			}
			return _RENAME_TBL_DONT_NEED_ADD;
		}
	}

	// 기존 파일들만 비교
	for( cur  = g_rename_tbl_dir[pr_host_index].head; cur; cur = _RT_C_TYPE_NEXT(cur) )
	{
		if( cur->final_path && !(_RT_CHECK_REMOVE(cur)) && cur->fp_len < pr_len && !_tcsncmp(cur->final_path, pr_path, cur->fp_len ) )
		{
			if(pr_path[cur->fp_len] == _T('\\')){
				if(_dir_final){
					if(_dir_final->fp_len < cur->fp_len)
						_dir_final  = cur;
				}else{
					_dir_final  = cur;
				}
				//break;
			}
		}
	}

	if( !_dir_final )
		cur  = g_rename_tbl_dir[pr_host_index].head;
	else
		cur = _dir_final;

	for( ;cur; cur = _RT_C_TYPE_NEXT(cur) )
	{
		if( !(_RT_CHECK_REMOVE(cur)) && cur->bp_len < pr_len && !_tcsncmp(cur->begin_path, pr_path, cur->bp_len ))
		{
			if( pr_path[cur->bp_len] == _T('\\')){
				_dir_begin  = cur;
				break;
			}
		}
	}
	rtv  = rename_tbl_checker_tester( _dir_final, _dir_begin, pr_path, pr_len );

	if( rtv == _RENAME_TBL_NEED_RENAME )
	{
		*pr_r_len = rename_tbl_upperDirRename(
				pr_path,
				pr_len,
				_dir_final->begin_path,
				_dir_final->fp_len,
				_dir_final->bp_len,
				pr_r_path );
	}

	return rtv;
}


void rename_tbl_entry_init( _rename_tbl_entry_t *pr_entry)
{
	pr_entry->list.prev = NULL;
	pr_entry->list.next = NULL;
	pr_entry->type_list.prev = NULL;
	pr_entry->type_list.next = NULL;
	pr_entry->mode = 0;
	pr_entry->begin_path = NULL;
	pr_entry->final_path = NULL;
	pr_entry->bp_depth = 0;
	pr_entry->bp_len = 0;
	pr_entry->fp_len = 0;
}

// data remove only.
void rename_tbl_entry_remove( _rename_tbl_entry_t *pr_rm )
{
	if( pr_rm->begin_path )
		SAFE_FREE( pr_rm->begin_path );
	if( pr_rm->final_path )
		SAFE_FREE( pr_rm->final_path );
	if( pr_rm->tmp_path )
		SAFE_FREE( pr_rm->tmp_path );

	SAFE_FREE( pr_rm );
}

int rename_tbl_pathDepthCheck( TCHAR *pr_path, int pr_len )
{
	int  i     = 0;
	int  depth = 0;


	for( i = 0; i < pr_len; i++ )
	{
		if( pr_path[i] == _T('\\') )
			depth++;
	}

	return depth;
}

void rename_tbl_makeLink( INT pr_host_index, int pr_type, _rename_tbl_entry_t *pr_entry )
{
	_RT_LINK( pr_host_index, pr_entry );
	if( pr_type & _RENAME_TBL_DIR )
		_RT_DIR_LINK( pr_host_index, pr_entry );
	else
		_RT_FILE_LINK(pr_host_index, pr_entry );

}

void rename_tbl_makeLinkWithNext(int pr_host_index, int pr_type, _rename_tbl_entry_t *pr_entry, _rename_tbl_entry_t *pr_next )
{
	_RT_PREV_LINK( pr_host_index, pr_entry, pr_next );
	if( pr_type & _RENAME_TBL_DIR )
		_RT_DIR_PREV_LINK(pr_host_index, pr_entry, pr_next );
	else
		_RT_FILE_PREV_LINK(pr_host_index, pr_entry, pr_next );
}


_rename_tbl_entry_t  *rename_tbl_createRTInfo(
		int              pr_type,
		TCHAR            *pr_begin_path,
		TCHAR            *pr_final_path,
		int              pr_bp_len,
		int              pr_fp_len
		)
{
	_rename_tbl_entry_t  *new_rgi = NULL;


	new_rgi  = (_rename_tbl_entry_t *)malloc( sizeof(_rename_tbl_entry_t) );
	if( !new_rgi )
	{
		LOG_ERROR( LOG_MOD_MDP_CNTRL, "Resync gap information create fail" );
		return NULL;
	}

	rename_tbl_entry_init( new_rgi );
	new_rgi->begin_path  = (TCHAR *)malloc( (pr_bp_len + 1) * sizeof(TCHAR) );
	if( !(new_rgi->begin_path) )
	{
		LOG_ERROR_W( LOG_MOD_MDP_CNTRL, _T("Resync gap information create fail: begin path malloc fail, %s"), pr_begin_path );
		SAFE_FREE( new_rgi );
		return NULL;
	}
	_tcscpy(new_rgi->begin_path, pr_begin_path);

	new_rgi->bp_depth  = rename_tbl_pathDepthCheck( pr_begin_path, pr_bp_len );
	new_rgi->bp_len    = pr_bp_len;

	if( pr_type & _RENAME_TBL_CREATE){
		new_rgi->final_path  = (TCHAR *)malloc( (pr_fp_len + 1) * sizeof(TCHAR) );
		if( !(new_rgi->final_path) )
		{
			SAFE_FREE( new_rgi );
			return NULL;
		}
		_tcscpy(new_rgi->final_path, pr_begin_path);

		new_rgi->fp_len    = pr_bp_len;
		
		if(pr_final_path){
			new_rgi->tmp_path  = (TCHAR *)malloc( (pr_fp_len + 1) * sizeof(TCHAR) );
			if( !(new_rgi->tmp_path) )
			{
				SAFE_FREE( new_rgi );
				return NULL;
			}
			_tcscpy(new_rgi->tmp_path, pr_final_path);

			new_rgi->tp_len    = pr_fp_len;
		}else{
			/* Error */
			return NULL;
		}
	}else{
		if( pr_final_path )
		{
			new_rgi->final_path  = (TCHAR *)malloc( (pr_fp_len + 1) * sizeof(TCHAR) );
			if( !(new_rgi->final_path) )
			{
				LOG_ERROR_W( LOG_MOD_MDP_CNTRL, _T("Resync gap information create fail: final path malloc fail, %s  begin: %s"), pr_final_path, pr_begin_path );
				SAFE_FREE( new_rgi->begin_path );
				SAFE_FREE( new_rgi );
				return NULL;
			}
			_tcscpy(new_rgi->final_path, pr_final_path);
			new_rgi->fp_len  = pr_fp_len;
		}

		new_rgi->tmp_path = NULL;
		new_rgi->tp_len = 0;
	}
	new_rgi->mode  = pr_type;

	return new_rgi;
}

int rename_tbl_createRTInfoNAdd(
		int				pr_host_index,
		int              pr_type,
		TCHAR            *pr_begin_path,
		TCHAR            *pr_final_path,
		int              pr_bp_len,
		int              pr_fp_len
		)
{
	_rename_tbl_entry_t  *new_rgi = NULL;


	new_rgi  = rename_tbl_createRTInfo( pr_type, pr_begin_path, pr_final_path, pr_bp_len, pr_fp_len );
	if( !new_rgi )
	{
		LOG_ERROR( LOG_MOD_MDP_CNTRL, "Resync gap information create fail" );
		return -1;
	}
	rename_tbl_makeLink( pr_host_index, pr_type, new_rgi );
	LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("ADD Rename TBL %s -> %s"), pr_begin_path, pr_final_path );

	return 0;
}

int rename_tbl_createRTInfoNAddWithNext(
		int				pr_host_index,
		int              pr_type,
		TCHAR            *pr_begin_path,
		TCHAR            *pr_final_path,
		int              pr_bp_len,
		int              pr_fp_len,
		_rename_tbl_entry_t  *pr_next
		)
{
	_rename_tbl_entry_t  *new_rgi = NULL;


	new_rgi  = rename_tbl_createRTInfo( pr_type, pr_begin_path, pr_final_path, pr_bp_len, pr_fp_len );
	if( !new_rgi )
	{
		LOG_ERROR( LOG_MOD_MDP_CNTRL, "Resync gap information create fail" );
		return -1;
	}
	rename_tbl_makeLinkWithNext( pr_host_index, pr_type, new_rgi, pr_next );
	LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("ADD Rename TBL %s -> %s"), pr_begin_path, pr_final_path );

	return 0;
}

int rename_tbl_addRTInfo(
		int		pr_host_index,
		int    pr_type,
		TCHAR  *pr_begin_path,
		TCHAR  *pr_final_path,
		int    pr_bp_len,
		int    pr_fp_len )
{
	int    ret        = 0;
	int    ck_ret     = 0;
	int    mode       = pr_type;
	int    bp_len     = pr_bp_len;
	int    fp_len     = pr_fp_len;
	TCHAR  *begin_path = pr_begin_path;
	TCHAR  *final_path = pr_final_path;


#if   defined( _MDP_DEBUG_ )
	sm_rgi_print_list();
#endif
	//LOG_INFO( LOG_MOD_MDP_CNTRL, "ADD: %08x : %-30s -> %s",  pr_type, pr_begin_path, (pr_final_path)?pr_final_path:_T("NOT EXIST") );
	//LOG_INFO( LOG_MOD_MDP_CNTRL, "" );
	ck_ret  = rename_tbl_checker( pr_host_index, pr_type, pr_begin_path, pr_bp_len, &begin_path, &bp_len );

	if( mode & _RENAME_TBL_RENAME )
	{
		if( mode & _RENAME_TBL_NEXIST )
		{
			ret  = rename_tbl_renameRemoveCheck( pr_host_index, mode, pr_final_path, pr_fp_len );
			if( ret == _RENAME_TBL_ADD_LIST )
			{
				int              sub_mode = _RT_MODE_CHG_1(mode);
				int              s3len    = 0;
				TCHAR            *s3path   = NULL;
				_rename_tbl_entry_t  *rn_mbr   = NULL;


				rn_mbr  = rename_tbl_findRTInfoInFinal(pr_host_index, mode, pr_begin_path, pr_bp_len );
				s3len   = rename_tbl_renameParentDirCheck( pr_host_index, pr_final_path, pr_fp_len, &s3path );
				
				LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("ADD: REMOVE: (by RENAME, new file exist): %s -> %s"), (s3path)?s3path:_T("FIRST"), pr_final_path );
				if( s3path )
				{
					sub_mode  |= _RENAME_TBL_RENAME;
					if( rn_mbr )
						ret  = rename_tbl_createRTInfoNAddWithNext( pr_host_index, sub_mode, s3path, pr_final_path, s3len, fp_len, rn_mbr );
					else
						ret  = rename_tbl_createRTInfoNAdd( pr_host_index, sub_mode, s3path, pr_final_path, s3len, fp_len );
				}
				else
				{
					if( rn_mbr )
						ret  = rename_tbl_createRTInfoNAddWithNext(pr_host_index,  sub_mode, pr_final_path, NULL, fp_len, 0, rn_mbr );
					else
						ret  = rename_tbl_createRTInfoNAdd(pr_host_index, sub_mode, pr_final_path, NULL, fp_len, 0 );
				}
				if( ret )
					LOG_ERROR( LOG_MOD_MDP_CNTRL, "        : REMOVE ADD FAIL" );
			}
		}

		if( mode & _RENAME_TBL_DIR )
			ret  = rename_tbl_renameDir_all( pr_host_index, pr_begin_path, pr_final_path, pr_bp_len, pr_fp_len );
		else
			ret  = rename_tbl_renameFile(pr_host_index, pr_begin_path, pr_final_path, pr_bp_len, pr_fp_len );
		if( ck_ret == _RENAME_TBL_NEED_RENAME )
			ck_ret = _RENAME_TBL_ADD_LIST;
	}
	else // REMOVE
	{
		if( mode & _RENAME_TBL_DIR )
			ret  = rename_tbl_removeDir_all( pr_host_index, begin_path, bp_len, pr_begin_path, pr_bp_len );

		if( ck_ret == _RENAME_TBL_NEED_RENAME )
//		ret  = sm_rgi_renameParentDirCheck( pr_begin_path, pr_bp_len, &begin_path );
//		if( ret )
		{
//			bp_len      = ret;
			mode       |= _RENAME_TBL_RENAME;
			final_path  = pr_begin_path;
			fp_len      = pr_bp_len;
			LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("ADD: REMOVE: + RENAME(Parent directory is renamed): %s -> %s"), begin_path, final_path );
			ck_ret = _RENAME_TBL_ADD_LIST;
		}
		else if( ck_ret == _RENAME_TBL_ADD_LIST )
			LOG_DEBUG_W( LOG_MOD_MDP_CNTRL, _T("ADD: REMOVE: %s"), pr_begin_path );
	}

	if( ck_ret == _RENAME_TBL_ADD_LIST )
	{
		ret  = rename_tbl_createRTInfoNAdd(pr_host_index, mode, begin_path, final_path, bp_len, fp_len );
		if( begin_path != pr_begin_path )
			SAFE_FREE( begin_path );
		if( ret )
			ret  = -1;
	}

	return ret;
}

int rename_tbl_init(void)
{
	int i;

	for(i = 0; i < MDP_MAX_HOSTS; i++){
		g_rename_tbl[i].head = NULL;
		g_rename_tbl[i].tail = NULL;
		g_rename_tbl[i].count = 0;
		g_rename_tbl[i].size = 0;

		g_rename_tbl_dir[i].head = NULL;
		g_rename_tbl_dir[i].tail = NULL;
		g_rename_tbl_dir[i].count = 0;
		g_rename_tbl_dir[i].size = 0;

		g_rename_tbl_file[i].head = NULL;
		g_rename_tbl_file[i].tail = NULL;
		g_rename_tbl_file[i].count = 0;
		g_rename_tbl_file[i].size = 0;

	}

	return 0;
}

void rename_tbl_destroy(int pr_host_index)
{
	_rename_tbl_entry_t  *cur = NULL;
	_rename_tbl_entry_t  *rm  = NULL;


	if( g_rename_tbl[pr_host_index].head == NULL )
		return;

	cur  = g_rename_tbl[pr_host_index].head;
	do {
		rm   = cur;
		cur  = _RT_C_NEXT( cur );
		rename_tbl_entry_remove( rm );
	} while( cur );

	//rename_tbl_init();
}

int  rename_tbl_mergedInfomationCheck(int pr_host_index, int type, TCHAR *pr_path, int pr_len, TCHAR **pr_r_path)
{
	int              err  = 0;
	int              len  = 0;
	TCHAR            *path = NULL;
	_rename_tbl_entry_t  *cur  = NULL;
	_rename_tbl_entry_t  *dir  = NULL;


	cur  = rename_tbl_findRTInfoInBegin(pr_host_index, type, pr_path, pr_len );
	
	if( !cur )
	{
		cur  = rename_tbl_findRTInfoInBegin_parent_dir(pr_host_index, pr_path, pr_len );
		if( !cur )
			return RENAME_TBL_NOT_EXIST;  // that file is exist or not yet read buffer file associate with pr_path

		if( _RT_CHECK_REMOVE(cur) )
			return RENAME_TBL_DELETED;

		len   = pr_len - cur->bp_len + cur->fp_len;
		path  = (TCHAR *)malloc( (len + 1) * sizeof(TCHAR) );
		if( !path )
		{
			LOG_ERROR( LOG_MOD_MDP_CNTRL, "get gap info fail: rename parent dir" );
			return SM_ERRNO_ALLOC_FAIL;
		}

		_stprintf( path, _T("%s%s"), cur->final_path, (pr_path+cur->bp_len) );
		//LOG_DEBUG( LOG_MOD_MDP_CNTRL, "GAP CHECK: %s -> %s", pr_path, path );
		*pr_r_path  = path;
	}
	else
	{
		if( _RT_CHECK_REMOVE(cur) )
			return RENAME_TBL_DELETED;  // removed file. 

		len   = cur->fp_len;
		path  = (TCHAR *)malloc( (cur->fp_len + 1) * sizeof(TCHAR) );
		if( !path )
		{
			LOG_ERROR_W( LOG_MOD_MDP_CNTRL, _T("get gap info fail: copy final path: %s -> %s"), cur->begin_path, cur->final_path );
			return SM_ERRNO_ALLOC_FAIL;
		}
		_tcscpy(path, cur->final_path);
		*pr_r_path  = path;
	}

	return RENAME_TBL_EXIST;
}


//
// GLOVAL FUNCTIONS
//
int _rename_tbl_mergedInfoAdd(
	    int		pr_host_index,
		int    pr_type,
		TCHAR  *pr_begin_path,
		TCHAR  *pr_final_path,
		int    pr_bp_len,
		int    pr_fp_len )
{
	return rename_tbl_addRTInfo(pr_host_index, pr_type, pr_begin_path, pr_final_path, pr_bp_len, pr_fp_len );
}

int  _rename_tbl_mergedInfoCheck(
	    int		pr_host_index,
		int		type,
		TCHAR   *pr_path,
		int     pr_len,
		TCHAR  **pr_r_path)
{
	return rename_tbl_mergedInfomationCheck( pr_host_index, type, pr_path, pr_len, pr_r_path );
}

void _rename_tbl_Init( void )
{
	rename_tbl_init();
}

void _rename_tbl_Destroy( int pr_host_index )
{
	rename_tbl_destroy(pr_host_index);
}
