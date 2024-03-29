
/*****************************************************************
 * SOUCE_MODULE
 *    fd_list.c - description
 *
 * CONTENTS
 *    <PUBLIC FUNCTION>
 *  	create_fd_list 		- Create a new file descriptor list
 *  	duplicate_fd_list 	- Duplicate file descriptor list
 *  	remove_fd_from 		- Remove file descriptor from list 
 *  	append_fd_to 		- Append file descriptor to list 
 *  	find_fd_in 			- Find file descriptor in list
 *    	destroy_fd_list 	- Destroy file descriptor list
 *
 *    <PRIVATE FUNCTION>
 *  	fd_cmp - Compare two file descriptors 
 *
 * DESCRIPTION
 *    This file implements a list to store file descriptors which
 *	  a process maintains 
 *
 * NOTES
 *	  None
 *****************************************************************/

/*****************************************************************
 * REVISION HISTORY
 * DATE         Authors          Description
 * ===============================================================
 *****************************************************************/

/*****************************************************************
 * INCLUDE FILES.
 *****************************************************************/

#include "privilege.h"
#include "report.h"
#include <tchar.h>
#include <aclapi.h>

/*****************************************************************
 * EXTERNAL DECLARATIONS.
 *****************************************************************/

/*****************************************************************
 * PRIVATE DEFINES AND TYPEDEFS.
 *****************************************************************/

/*****************************************************************
 * STATIC VARIABLE DECLARATIONS.
 *****************************************************************/

/*****************************************************************
 * LOCAL FUNCTION PROTOTYPES.
 *****************************************************************/
static BOOL SetPrivilege(
    HANDLE hToken,          // token handle
    LPCTSTR Privilege,      // Privilege to enable/disable
    BOOL bEnablePrivilege   // TRUE to enable.  FALSE to disable
    )
{
    TOKEN_PRIVILEGES tp;
    LUID luid;
    TOKEN_PRIVILEGES tpPrevious;
    DWORD cbPrevious=sizeof(TOKEN_PRIVILEGES);

    if (!LookupPrivilegeValue( NULL, Privilege, &luid )) return FALSE;

    // 
    // first pass.  get current privilege setting
    // 
    tp.PrivilegeCount           = 1;
    tp.Privileges[0].Luid       = luid;
    tp.Privileges[0].Attributes = 0;

    AdjustTokenPrivileges(
            hToken,
            FALSE,
            &tp,
            sizeof(TOKEN_PRIVILEGES),
            &tpPrevious,
            &cbPrevious
            );

    if (GetLastError() != ERROR_SUCCESS) 
		return FALSE;

    // 
    // second pass.  set privilege based on previous setting
    // 
    tpPrevious.PrivilegeCount       = 1;
    tpPrevious.Privileges[0].Luid   = luid;

    if (bEnablePrivilege)
	{
        tpPrevious.Privileges[0].Attributes |= (SE_PRIVILEGE_ENABLED);
    }
    else
	{
        tpPrevious.Privileges[0].Attributes ^= (SE_PRIVILEGE_ENABLED &
            tpPrevious.Privileges[0].Attributes);
    }

    AdjustTokenPrivileges(
            hToken,
            FALSE,
            &tpPrevious,
            cbPrevious,
            NULL,
            NULL
            );

    if (GetLastError() != ERROR_SUCCESS) return FALSE;

    return TRUE;
}



BOOL AddPrivilige(LPCTSTR seName)
{
	HANDLE hToken;
	TOKEN_PRIVILEGES tkp;
	BOOL	res = FALSE;

	//Adjust token privileges to open system processes
	if (OpenProcessToken(GetCurrentProcess(), 
		TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken) == FALSE)
	{
		LOG_ERROR(LOG_MOD_UTIL, "OpenProcessToken() failed");
		return FALSE;
	}

	res = LookupPrivilegeValue(NULL, seName, &tkp.Privileges[0].Luid);
	if (res == FALSE)
	{
		LOG_ERROR(LOG_MOD_UTIL, "LookupPrivilegeValue() [%s] failed", seName);
		return FALSE;
	}

	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	res = AdjustTokenPrivileges(hToken, 0, &tkp, sizeof(tkp), NULL, NULL);
	if (res == 0)
	{
		LOG_ERROR(LOG_MOD_UTIL, "AdjustTokenPrivileges() failed");
		return FALSE;
	}

	SAFE_CLOSE_HANDLE(hToken);
	return TRUE;
}


BOOL AddPriviliges()
{
	BOOL res = TRUE;

	if (!AddPrivilige(SE_DEBUG_NAME))
		res = FALSE;

	if (!AddPrivilige(SE_CREATE_PAGEFILE_NAME))
		res = FALSE;

	if (!AddPrivilige(SE_SECURITY_NAME))
		res = FALSE;

	if (!AddPrivilige(SE_ASSIGNPRIMARYTOKEN_NAME))
		res = FALSE;

	if (!AddPrivilige(SE_INCREASE_QUOTA_NAME))
		res = FALSE;

	if (!AddPrivilige(SE_CREATE_TOKEN_NAME))
		res = FALSE;

	if (!AddPrivilige(SE_TCB_NAME))
		res = FALSE;

	if (!AddPrivilige(SE_TAKE_OWNERSHIP_NAME))
		res = FALSE;

	if (!AddPrivilige(SE_MANAGE_VOLUME_NAME))
		res = FALSE;

	return res;
}


// 모든 프로세스가 접근할 수 있는 커널오브젝트 (뮤텍스, 공유메모리, 이벤트 등등) 만들기 위한 SA
// 참고: http://www.unixwiz.net/tools/dbmutex-1.0.1.cpp
BOOL CreatePublicSA(PSECURITY_ATTRIBUTES pSa, PSECURITY_DESCRIPTOR pSd)
{
	BOOL res;

	res = InitializeSecurityDescriptor(pSd, SECURITY_DESCRIPTOR_REVISION);
	if (res == FALSE)
	{
		LOG_ERROR(LOG_MOD_UTIL, "InitializeSecurityDescriptor() Failed"); 
		return FALSE;
	}

	res = SetSecurityDescriptorDacl(
		pSd,		// addr of SD
		TRUE,		// TRUE=DACL present
		NULL,		// ... but it's empty (wide open)
		FALSE);		// DACL explicitly set, not defaulted
	if (res == FALSE)
	{
		LOG_ERROR(LOG_MOD_UTIL, "SetSecurityDescriptorDacl() Failed"); 
		return FALSE;
	}

	ZeroMemory(pSa, sizeof(SECURITY_ATTRIBUTES));

	pSa->nLength              = sizeof(SECURITY_ATTRIBUTES);
	pSa->lpSecurityDescriptor = pSd;
	pSa->bInheritHandle       = FALSE;

	return TRUE;
}


HANDLE CreatePublicFileMapping(char *key, int size)
{
	BOOL	res;
	HANDLE	hMapping;

	SECURITY_ATTRIBUTES sa;
	SECURITY_DESCRIPTOR sd;

	res = CreatePublicSA(&sa, &sd);
	if (res == FALSE)
	{
		LOG_ERROR(LOG_MOD_UTIL, "CreatePublicSA() Failed");
		return NULL;
	}

#ifdef UNICODE
	hMapping = CreateFileMapping(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, size, (LPCWSTR)key);
#else
	hMapping = CreateFileMapping(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, size, (LPCSTR)key);
#endif
	if (hMapping == NULL)
	{
		#ifndef _MDP_DLL_
		LOG_ERROR(LOG_MOD_UTIL, "CreateFileMapping() failed"); 
		#endif
		return NULL;
	}

	return hMapping;
}



HANDLE CreatePublicMutex(char *key)
{
	BOOL	res;
	HANDLE	hMutex;

	SECURITY_ATTRIBUTES sa;
	SECURITY_DESCRIPTOR sd;

	res = CreatePublicSA(&sa, &sd);
	if (res == FALSE)
	{
		LOG_ERROR(LOG_MOD_UTIL, "CreatePublicSA() Failed");
		return NULL;
	}

#ifdef UNICODE
	hMutex = CreateMutex(&sa, FALSE, (LPCWSTR)key);
#else
	hMutex = CreateMutex(&sa, FALSE, (LPCSTR)key);
#endif
	if (hMutex == NULL)
	{
		LOG_ERROR(LOG_MOD_UTIL, "CreateMutex() Failed"); 
		return NULL;
	}
	
	return hMutex;
}


HANDLE CreatePublicEvent(char *key)
{
	BOOL	res;
	HANDLE	hEvent;

	SECURITY_ATTRIBUTES sa;
	SECURITY_DESCRIPTOR sd;

	res = CreatePublicSA(&sa, &sd);
	if (res == FALSE)
	{
		LOG_ERROR(LOG_MOD_UTIL, "CreatePublicSA() Failed");
		return NULL;
	}

#ifdef UNICODE
	hEvent = CreateEvent(&sa, FALSE, FALSE, (LPCWSTR)key);
#else
	hEvent = CreateEvent(&sa, FALSE, FALSE, (LPCSTR)key);
#endif
	if (hEvent == NULL)
	{
		LOG_ERROR(LOG_MOD_UTIL, "CreateEvent() Failed"); 
		return NULL;
	}
	
	return hEvent;
}

int SetSecurity(TCHAR* path, void* pmsg, int len, SECURITY_INFORMATION secu_info)
{
	char*						curPos=(char*)pmsg;
	DWORD						Err_rtv=0;
	TCHAR*						Filepath=path;
	unsigned long				pathlen;

	//TCHAR NameIdx[MAX_SID_NAME_CNT][MAX_NAME_LEN];
	char NameIdx[MAX_SID_NAME_CNT][MAX_NAME_LEN];
	char SidIdx[MAX_SID_NAME_CNT][255]={0,};
	char* StrSidIdx[MAX_SID_NAME_CNT]={NULL, };
	EXPLICIT_ACCESS pDACL[MAX_SID_NAME_CNT]={0, };
	EXPLICIT_ACCESS pSACL[MAX_SID_NAME_CNT]={0, };
	int i=0, name_cnt=0;

	PSID pOwner=NULL, pGroup=NULL;
	PACL pDacl=NULL, pSacl=NULL;
	ULONG	Dacl_cnt, Sacl_cnt;

	DWORD cbSid, cbDomain;
	char Domain[255]={0,};
	SID_NAME_USE peUse;

	memcpy(&name_cnt, curPos, sizeof(int));
	curPos+=sizeof(int);

	//name, Sid, StrSid index를 만듬
	for(i=0;i<name_cnt;i++)
	{
		//length정보 얻음
		memcpy(&len, curPos, sizeof(int));
		curPos+=sizeof(int);
		//Name정보
		//_tcscpy_s(NameIdx[i], len, (char*)curPos);
		strcpy_s(NameIdx[i], len, (char*)curPos);
		curPos+=len;

		cbSid=255;
		cbDomain=255;

		//Sid index에 등록
		if(!LookupAccountNameA(NULL, NameIdx[i], (PSID)SidIdx[i], &cbSid, Domain, &cbDomain, &peUse))
		{
			LOG_ERROR(LOG_MOD_UTIL, "Set SID failed(%d)", GetLastError());
		}
	}

	//Owner의 Sid를 구함
	if(secu_info&OWNER_SECURITY_INFORMATION)
	{
		pOwner=SidIdx[0];
	}

	if(secu_info&GROUP_SECURITY_INFORMATION)
	{
		if(secu_info==OWNER_SECURITY_INFORMATION)
		{
			pGroup=SidIdx[1];
		}
		else
		{
			pGroup=SidIdx[0];
		}
	}

	if(secu_info&DACL_SECURITY_INFORMATION)
	{
		int index=-1;
		//length정보 얻음
		memcpy(&Dacl_cnt, curPos, sizeof(ULONG));
		curPos+=sizeof(ULONG);
		//EXPLICIT_ACCESS
		for(i=0;(ULONG)i<Dacl_cnt;i++)
		{
			memcpy(&pDACL[i], curPos, sizeof(EXPLICIT_ACCESS));
			curPos+=sizeof(EXPLICIT_ACCESS);
			index=(int)pDACL[i].Trustee.ptstrName;
			pDACL[i].Trustee.ptstrName=SidIdx[index];
		}
		
		Err_rtv=SetEntriesInAcl(Dacl_cnt, pDACL, NULL, &pDacl);
		if(Err_rtv!=ERROR_SUCCESS)
		{
			LOG_ERROR(LOG_MOD_UTIL, "[init_sync]Set Dacl Entries failed(%d)", Err_rtv);
			return -1;
		}
	}

	if(secu_info&SACL_SECURITY_INFORMATION)
	{
		//length정보 얻음
		memcpy(&Sacl_cnt, curPos, sizeof(ULONG));
		curPos+=sizeof(ULONG);
		//EXPLICIT_ACCESS
		for(i=0;(ULONG)i<Sacl_cnt;i++)
		{
			memcpy(&pSACL[i], (void*)curPos, sizeof(EXPLICIT_ACCESS));
			curPos+=sizeof(EXPLICIT_ACCESS);
			pSACL[i].Trustee.ptstrName=SidIdx[(int)pSACL[i].Trustee.ptstrName];
		}

		Err_rtv=SetEntriesInAcl(Sacl_cnt, pSACL, NULL, &pSacl);
		if(Err_rtv!=ERROR_SUCCESS)
		{
			LOG_ERROR(LOG_MOD_UTIL, "[init_sync]Set Sacl Entries failed(%d)", Err_rtv);
			return -1;
		}
	}

		/*
	memcpy(&pathlen, curPos, sizeof(unsigned long));
	curPos+=sizeof(unsigned long);

	Filepath = (char*)malloc(pathlen);
	memcpy(Filepath, (void*)curPos, pathlen);
	curPos+=pathlen;
*/
	//Err_rtv=SetSecurityInfo(hTarget, SE_FILE_OBJECT,
	//	pSS_info->secu_info,
	//	pOwner,
	//	pGroup,
	//	pDacl,
	//	pSacl);

	//Err_rtv=SetNamedSecurityInfoA(Filepath, SE_FILE_OBJECT,
	Err_rtv=SetNamedSecurityInfo(path, SE_FILE_OBJECT,
		secu_info,
		pOwner,
		pGroup,
		pDacl,
		pSacl);

	if(Err_rtv!=ERROR_SUCCESS)
	{
		LOG_ERROR(LOG_MOD_UTIL, "[init_sync]Set Security failed(%x)", Err_rtv);
		return -1;
	}
	
	return 0;
}

int NameCompare(char** NameIdx, int idx_cnt, char* Name)
{
	int i;
	char *tmp;

	for(i=0;i<idx_cnt;i++)
	{
		tmp = NameIdx + (i * MAX_NAME_LEN);
		//if(!_tcscmp(tmp, Name))
		if(!strcmp(tmp, Name))
			return i;
	}

	return -1;
}

//////////////////////////////////////////////////////////////////////////////
//권한 설정을 위해 보낼 데이터를 만든다.
//return: 메시지 길이
//paramter: path, data ptr
//////////////////////////////////////////////////////////////////////////////
void* MakeSetSecuInfo(TCHAR* path, int* len, SECURITY_INFORMATION* secu_info, int *err)
{
	TCHAR* Filepath=path;

	//char *Name, *Domain;
	char Domain[MAX_NAME_LEN], Name[MAX_NAME_LEN];
	//TCHAR Domain[MAX_NAME_LEN], Name[MAX_NAME_LEN];
	char MapSidtoName[MAX_SID_NAME_CNT][MAX_NAME_LEN];
	//TCHAR MapSidtoName[MAX_SID_NAME_CNT][MAX_NAME_LEN];
	PEXPLICIT_ACCESS pDACL[MAX_SID_NAME_CNT]={NULL, };
	PEXPLICIT_ACCESS pSACL[MAX_SID_NAME_CNT]={NULL, };
	int i,	name_cnt=0;
	ULONG Dacl_cnt=0, Sacl_cnt=0;

	PSID pOwner=NULL, pGroup=NULL;
	PACL pDacl=NULL, pSacl=NULL;
	PSECURITY_DESCRIPTOR pSD=NULL;

	DWORD cbName, cbDomain;
	SID_NAME_USE peUse;
	PEXPLICIT_ACCESS pEntry;
	int NameCmpRtv = -1;

	DWORD result=0;
	int info_len = 0;
	int ret;

	TCHAR* ptemp_info=NULL;

	SECURITY_INFORMATION temp_secu_info = 
		OWNER_SECURITY_INFORMATION
		| GROUP_SECURITY_INFORMATION
		| DACL_SECURITY_INFORMATION
		| SACL_SECURITY_INFORMATION;

	*err = ERROR_SUCCESS;

	result=GetNamedSecurityInfo(
		Filepath, SE_FILE_OBJECT,
		temp_secu_info,
		&pOwner,
		&pGroup,
		&pDacl,
		&pSacl, &pSD);

	if(ERROR_SUCCESS!=result)
	{
		LOG_ERROR(LOG_MOD_UTIL, "Getting File's Security information failed:(%lu)", result);
		*err = result;

		if(pSD!=NULL)LocalFree(pSD);
		return (void*)NULL;
	}

	temp_secu_info = 0;
	if(pOwner != NULL) temp_secu_info |= OWNER_SECURITY_INFORMATION;
	if(pGroup != NULL) temp_secu_info |= GROUP_SECURITY_INFORMATION;
	if(pDacl  != NULL) temp_secu_info |= DACL_SECURITY_INFORMATION;
	if(pSacl  != NULL) temp_secu_info |= SACL_SECURITY_INFORMATION;

	if(temp_secu_info!=0)info_len += sizeof(int);

	name_cnt = 0;

	if(temp_secu_info & OWNER_SECURITY_INFORMATION)
	{
		//Owner(소유자) 이름 얻기
		cbName=MAX_NAME_LEN;
		cbDomain=MAX_NAME_LEN;
		
		LookupAccountSidA(NULL, pOwner, MapSidtoName[name_cnt], &cbName, Domain, &cbDomain, &peUse);
		name_cnt++;

		//메시지 길이 추가
		//info_len+=(sizeof(int) + (cbName+1)*sizeof(TCHAR));
		info_len+=(sizeof(int) + (cbName+1));
	}

	if(temp_secu_info & GROUP_SECURITY_INFORMATION)
	{
		//Group(그룹) 이름 얻기
		cbName=MAX_NAME_LEN;
		cbDomain=MAX_NAME_LEN;
	
		LookupAccountSidA(NULL, pGroup, MapSidtoName[name_cnt], &cbName, Domain, &cbDomain, &peUse);
		name_cnt++;

		//메시지 길이 추가
		//info_len+=(sizeof(int) + (cbName+1)*sizeof(TCHAR));
		info_len+=(sizeof(int) + (cbName+1));
	}

	if(temp_secu_info & DACL_SECURITY_INFORMATION)
	{
		//Dacl에 대한 정보 처리
		Dacl_cnt=0;
		if(ERROR_SUCCESS!=GetExplicitEntriesFromAcl(pDacl, &Dacl_cnt, &pEntry))
		{
			LOG_ERROR(LOG_MOD_UTIL, "SET_SECURITY processing failed:Dacl(%x)", GetLastError());
		}

		if(Dacl_cnt != 0)
		{
			//메시지 길이 추가(ACE갯수(int)+Total ACE(sizeof(EXPLICIT_ACCESS)*Dacl_cnt)
			info_len+=(sizeof(ULONG)+(sizeof(EXPLICIT_ACCESS)*Dacl_cnt));

			for (i=0;i<(int)Dacl_cnt;i++)
			{
				pDACL[i]=(PEXPLICIT_ACCESS)malloc(sizeof(EXPLICIT_ACCESS));
				memcpy(pDACL[i], &pEntry[i], sizeof(EXPLICIT_ACCESS));

				cbName=MAX_NAME_LEN;
				cbDomain=MAX_NAME_LEN;
				
				LookupAccountSidA(NULL, pEntry[i].Trustee.ptstrName, Name, &cbName, 
					Domain, &cbDomain, &peUse);

				NameCmpRtv=NameCompare(MapSidtoName, name_cnt, Name);
				if(-1==NameCmpRtv) //name index에 없는 경우 index에 추가
				{
					memcpy(MapSidtoName[name_cnt], Name, (cbName+1)/**sizeof(TCHAR)*/);
					pDACL[i]->Trustee.ptstrName=(char*)name_cnt;
					name_cnt++;
					//메시지 길이 추가
					info_len+=(sizeof(int) + (cbName+1)/**sizeof(TCHAR)*/);
				}
				else //name index에 있는 경우
				{
					pDACL[i]->Trustee.ptstrName=(char*)NameCmpRtv;
				}
			}

			LocalFree(pEntry);
		}
		else temp_secu_info ^= DACL_SECURITY_INFORMATION;
	}

	if(temp_secu_info & SACL_SECURITY_INFORMATION)
	{
		//Sacl에 대한 정보 처리
		Sacl_cnt = 0;
		if(ERROR_SUCCESS!=GetExplicitEntriesFromAcl(pSacl, &Sacl_cnt, &pEntry))
		{
			LOG_ERROR(LOG_MOD_UTIL, "SET_SECURITY processing failed:Sacl(%x)", GetLastError());
		}

		if(Sacl_cnt != 0)
		{
			//메시지 길이 추가(ACE갯수(int)+Total ACE(sizeof(EXPLICIT_ACCESS)*Sacl_cnt)
			info_len+=(sizeof(ULONG)+sizeof(EXPLICIT_ACCESS)*Sacl_cnt);

			for (i=0;i<(int)Sacl_cnt;i++)
			{
				pSACL[i]=(PEXPLICIT_ACCESS)malloc(sizeof(EXPLICIT_ACCESS));
				memcpy(pSACL[i], &pEntry[i], sizeof(EXPLICIT_ACCESS));

				cbName=MAX_NAME_LEN;
				cbDomain=MAX_NAME_LEN;
				
				LookupAccountSidA(NULL, pEntry[i].Trustee.ptstrName, Name, &cbName, 
					Domain, &cbDomain, &peUse);

				NameCmpRtv=NameCompare(MapSidtoName, name_cnt, Name);
				if(-1==NameCmpRtv) //name index에 없는 경우 index에 추가
				{
					memcpy(MapSidtoName[name_cnt], Name, (cbName+1)/**sizeof(TCHAR)*/);
					pSACL[i]->Trustee.ptstrName=(char*)name_cnt;
					name_cnt++;
					//메시지 길이 추가
					info_len+=(sizeof(int)+cbName+1);
				}
				else //name index에 있는 경우
				{
					pSACL[i]->Trustee.ptstrName=(char*)NameCmpRtv;
				}
			}
			LocalFree(pEntry);
		}
		else temp_secu_info ^= SACL_SECURITY_INFORMATION;
	}

	//새로운 info_msg를 만듬 //쓸 위치는 cbDomain으로 나타냄
	cbDomain=0;
	//info header 할당 및 복사
	ptemp_info=(char*)malloc(sizeof(char)*info_len);

	//name cnt 복사
	memcpy_s(ptemp_info, info_len - cbDomain, &name_cnt, sizeof(int));
	cbDomain += sizeof(int);

	//name index 복사
	for(i=0;i<name_cnt;i++)
	{
		//int len = (_tcslen(MapSidtoName[i])+1) * sizeof(TCHAR);
		int len = (strlen(MapSidtoName[i])+1)/* * sizeof(TCHAR)*/;

		memcpy_s((char*)ptemp_info+cbDomain, info_len - cbDomain, &len, sizeof(int));
		cbDomain+=sizeof(int);
		//ret = _tcsncpy_s((char*)ptemp_info+cbDomain, len,  MapSidtoName[i], _TRUNCATE);
		ret = strncpy_s((char*)ptemp_info+cbDomain, len,  MapSidtoName[i], _TRUNCATE);
		if(ret != 0){
			LOG_ERROR(LOG_MOD_UTIL, "strcpy fail %s:%d", __FILE__, __LINE__);
			return NULL;
		}
		//SAFE_FREE(MapSidtoName[i]);
		cbDomain+=len;
	}

	//DACL복사
	if( (temp_secu_info & DACL_SECURITY_INFORMATION) )
	{
		memcpy_s((char*)ptemp_info+cbDomain, info_len - cbDomain, &Dacl_cnt, sizeof(ULONG));
		cbDomain+=sizeof(ULONG);

		for(i=0;(ULONG)i<Dacl_cnt;i++)
		{
			memcpy_s((char*)ptemp_info+cbDomain, info_len - cbDomain, pDACL[i], sizeof(EXPLICIT_ACCESS));
			cbDomain+=sizeof(EXPLICIT_ACCESS);
		}
	}

	//SACL복사
	if( (temp_secu_info & SACL_SECURITY_INFORMATION) )
	{
		memcpy_s((char*)ptemp_info+cbDomain, info_len - cbDomain, &Sacl_cnt, sizeof(ULONG));
		cbDomain+=sizeof(ULONG);

		for(i=0;(ULONG)i<Sacl_cnt;i++)
		{
			memcpy_s((char*)ptemp_info+cbDomain, info_len - cbDomain, pSACL[i], sizeof(EXPLICIT_ACCESS));
			cbDomain+=sizeof(EXPLICIT_ACCESS);
		}
	}

	//info_data의 길이
	*len = info_len;
	//SECURITY_INFORMATION
	*secu_info = temp_secu_info;

	//CleanUp
	for(i=0;(ULONG)i<Dacl_cnt;i++)free(pDACL[i]);
	for(i=0;(ULONG)i<Sacl_cnt;i++)free(pSACL[i]);

	if(pSD!=NULL)LocalFree(pSD);

	return ptemp_info;
}

DWORD ReleaseSecurity(TCHAR *path)
{
	BOOL bRetval = FALSE;

    PSID pSIDAdmin = NULL;
    PACL pACL = NULL;
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld =
            SECURITY_WORLD_SID_AUTHORITY;
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = SECURITY_NT_AUTHORITY;
    EXPLICIT_ACCESS ea;
    DWORD dwRes;

	// Specify the DACL to use.

    // Create a SID for the BUILTIN\Administrators group.
    if (!AllocateAndInitializeSid(&SIDAuthNT, 2,
                     SECURITY_BUILTIN_DOMAIN_RID,
                     DOMAIN_ALIAS_RID_ADMINS,
                     0, 0, 0, 0, 0, 0,
                     &pSIDAdmin)) 
    {
        printf("AllocateAndInitializeSid (Admin) error %u\n",
                GetLastError());
        goto Cleanup;
    }

    ZeroMemory(&ea, sizeof(EXPLICIT_ACCESS));

    // Set full control for Administrators.
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode = SET_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    ea.Trustee.ptstrName = (LPTSTR) pSIDAdmin;

    if (ERROR_SUCCESS != SetEntriesInAcl(1,
                                         &ea,
                                         NULL,
                                         &pACL))
    {
        printf("Failed SetEntriesInAcl\n");
        goto Cleanup;
    }

    // Try to modify the object's DACL.
    dwRes = SetNamedSecurityInfo(
        path,                 // name of the object
        SE_FILE_OBJECT,              // type of object
        DACL_SECURITY_INFORMATION,   // change only the object's DACL
        NULL, NULL,                  // do not change owner or group
        pACL,                        // DACL specified
        NULL);                       // do not change SACL

    if (ERROR_SUCCESS == dwRes) 
    {
        printf("Successfully changed DACL\n");
        bRetval = TRUE;
        // No more processing needed.
        goto Cleanup;
    }

Cleanup:
	if (pSIDAdmin)
		FreeSid(pSIDAdmin); 

	return dwRes;
}