#ifndef __INCLUDE_DEFINES_H_
#define __INCLUDE_DEFINES_H_

#ifndef SAFE_FREE
#define SAFE_FREE(p)	{if(p) {free(p); (p)=NULL;}}
#endif

#ifndef SAFE_CLOSE_HANDLE
#define SAFE_CLOSE_HANDLE(p)	{if(p != INVALID_HANDLE_VALUE) {CloseHandle(p); (p)=INVALID_HANDLE_VALUE;}}
#endif

#ifndef INITSYNC_HOME_PATH
#define MDP_HOME_PATH				"C:\\mDP\\"
#define INITSYNC_HOME_PATH				MDP_HOME_PATH##"Initsync"
#endif

#define INITSYNC_CONFIG_PATH				(INITSYNC_HOME_PATH##"\\conf\\conf.ini")

#define INITSYNC_MODE_UNKNOWN				(0)
#define INITSYNC_MODE_SOURCE				(1)
#define INITSYNC_MODE_TARGET				(2)
#define INITSYNC_MODE_LOCAL_REPLICATION		(3)

//<TYPE definition>//
#define TYPE_SHORT		(1)
#define TYPE_INT		(2)
#define TYPE_UINT		(3)
#define TYPE_LONG		(4)
#define TYPE_ULONG		(5)
#define TYPE_LLONG      (6)
#define TYPE_ULLONG     (7)
#define TYPE_LOFF_T		(8)
#define TYPE_P_VOID		(9)
#define TYPE_P_CHAR		(10)

/**********************************************************
 *           LENGTH definition
 **********************************************************/
#define LEN_ARG_CHAR	(sizeof(char))
#define LEN_ARG_SHORT	(sizeof(short))
#define LEN_ARG_INT		(sizeof(int))
#define LEN_ARG_LONG	(sizeof(long))
#define LEN_ARG_LLONG	(sizeof(long long))

#define LEN_TYPE        (LEN_ARG_CHAR)
#define LEN_ARG_LEN     (LEN_ARG_INT)
#define LEN_ARG_HDR     (LEN_TYPE + LEN_ARG_LEN)

#define LEN_NL_MSG_LEN	(4)
#define LEN_NL_MSG_TYPE	(1)
#define LEN_NL_ARG_TYPE	(1)
//#define LEN_NL_MSG_HDR	(LEN_NL_MSG_LEN + LEN_NL_MSG_TYPE + LEN_NL_ARG_TYPE)

#define LEN_MDP_MSG_TYPE			(1)
#define LEN_MDP_MSG_FIX_TYPE_LEN	(1)
#define LEN_NULL_CHAR				(1)

#define NUM_PROCESS_HASH_TABLE		(16)

#define DEFAULT_QUEUE_SIZE	(1024)
#define LARGE_QUEUE_SIZE	(1024*4)

#define MAX_SCAN_BUF (1024)

// type: directory or file
#define   INITSYNC_FTYPE_FILE                   (1)
#define   INITSYNC_FTYPE_DIRECTORY              (2)

#define MAX_SID_NAME_CNT		10

#define	BLOCKING_CALL			1
#define NON_BLOCKING_CALL		0

#define MAX_CONN_TRY	20
//#define _USE_TRANSMITFILE_
//#define _WITHOUT_SD_

#define FILE_BUF_SIZE	8192
#define TCP_BUF_SIZE	(8192*5)
#define SOCK_BUF_SIZE	100*1024

#define SEGMENT_DEFAULT_FILES (256 * 1024)
#define SEGMENT_DEFAULT_SIZE  (1024*1024*1024)

#define IGNORE_FILE_LEN (0xFFFFFFFF)

#define HB_PORT_OFFSET (100)

/* ERRORS */
#define ERROR_INITSYNC_SUCCESS	0
#define ERROR_SYS_BASE			1000
#define ERROR_SYS_ALLOC			(ERROR_SYS_BASE + 1)

#define ERROR_INI_BASE			2000
#define ERROR_INI_NO_FILE		(ERROR_INI_BASE + 1)
#define ERROR_INI_WRONG_FORMAT	(ERROR_INI_BASE + 2)
#define ERROR_INI_NO_SECTION	(ERROR_INI_BASE + 3)
#define ERROR_INI_NO_LINE		(ERROR_INI_BASE + 4)
#define ERROR_INI_NO_DATA		(ERROR_INI_BASE + 5)
#define ERROR_INI_NOT_RELEVANT	(ERROR_INI_BASE + 6)

#define MAX_INITSYNC (16)

#define INITSYNC_FILE_CREATE(handle, path, err, err_handle) \
{ \
	handle = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL); \
	if( handle == INVALID_HANDLE_VALUE ) \
	{ \
		err = GetLastError(); \
		if(err == ERROR_FILE_EXISTS){ \
			do{ \
				handle = CreateFileA(path, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL); \
				if( handle == INVALID_HANDLE_VALUE ) \
				{ \
					err = GetLastError(); \
					LOG_ERROR(LOG_MOD_SESSION, "file[%s] open fail, error code : %d\n", path, err); \
					Sleep(1); \
				} \
			}while(handle == INVALID_HANDLE_VALUE); \
		}else{ \
			LOG_ERROR(LOG_MOD_SESSION, "file[%s] open fail, error code : %d\n", path, err); \
			goto err_handle; \
		} \
	} \
}

#define INITSYNC_FILE_WRITE(handle, data, len, err, err_handle) \
{ \
	ret = WriteFile(handle, data, len, &written, NULL); \
	if((!ret) || written == 0) \
	{ \
		err = GetLastError(); \
		LOG_ERROR(LOG_MOD_FILE, "file Write Fail. error code[%d]", err); \
		SAFE_CLOSE_HANDLE(handle); \
		handle = INVALID_HANDLE_VALUE; \
		goto err_handle; \
	} \
}


#endif
