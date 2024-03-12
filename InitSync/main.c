#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <tchar.h>

#include "node.h"
#include "report.h"
#include "initsync_time.h"
#include "NetworkEventHandler.h"
#include "sd_notifier.h"
#include "scandir.h"
#include "wchar_conv.h"

int g_stopFlag = 0;
int g_nodeChannelInfo = 0;
//int g_pauseFlag = 0;
int g_logLevel = LOG_LEVEL_INFO; // default
int g_logMod = LOG_MOD_ALL;
int g_target_delete = 0; 
int g_alone = 0;
int g_resume = 0;
int g_group_id = 0;
int g_initsync_id = 0;
char *g_conf_path = NULL;

int g_file_buf_size = FILE_BUF_SIZE;
int g_send_buf_size = TCP_BUF_SIZE;
int g_recv_buf_size = FILE_BUF_SIZE;
int g_sock_buf_size = SOCK_BUF_SIZE;

node_t *g_node = NULL;
int g_exit = 0;

int parse_arg(char *argv)
{
	int i, arglen, conflen;
	char *conf, *p1, *p2;

	p1 = p2 = argv;

	arglen = strlen(argv);

	p2 = strstr(p1, "-alone");
	if(p2){
		g_alone = 1;
	}

	p2 = strstr(p1, "-delete");
	if(p2){
		g_target_delete = 1;
	}

	p2 = strstr(p1, "-r");
	if(p2){
		g_resume = 1;
	}

	p2 = strstr(p1, "-c");
	if(p2){
		p1 = p2+2;
		while(*p1 == ' ')
			p1++;

		if(p1 - argv >= arglen){
			return -1;
		}

		if(*p1 == '"'){
			p1 = p1+1;
			p2 = strstr(p1, "\"");
			if(!p2)
				return -1;
			*p2 = '\0';

			g_conf_path = malloc(strlen(p1) + 1);
			if(!g_conf_path){
				return -1;
			}
			strcpy(g_conf_path, p1);
		}else{
			p2 = strstr(p1, " ");
			if(!p2){
				p2 = argv+arglen;
			}
			*p2 = '\0';

			if(p2-p1 > 0){
				g_conf_path = malloc(strlen(p1) + 1);
				if(!g_conf_path){
					return -1;
				}
				strcpy(g_conf_path, p1);
			}else{
				return -1;
			}
		}
	}

	p1 = argv;
	p2 = strstr(p1, "-v");
	if(p2){
		p1 = p2+2;
		while(*p1 == ' ')
			p1++;

		if(p1 - argv >= arglen){
			return -1;
		}

		p2 = strstr(p1, " ");
		if(!p2){
			p2 = argv+arglen;
		}
		*p2 = '\0';

		if(p2-p1 > 0){
			g_logLevel = atoi(p1);
		}else{
			return -1;
		}
	}

	p1 = argv;
	p2 = strstr(p1, "-i");
	if(p2){
		p1 = p2+2;
		while(*p1 == ' ')
			p1++;

		if(p1 - argv >= arglen){
			return -1;
		}

		p2 = strstr(p1, " ");
		if(!p2){
			p2 = argv+arglen;
		}
		*p2 = '\0';

		if(p2-p1 > 0){
			g_initsync_id = atoi(p1);
		}else{
			return -1;
		}
	}

	p1 = argv;
	p2 = strstr(p1, "-group");
	if(p2){
		p1 = p2+2;
		while(*p1 == ' ')
			p1++;

		if(p1 - argv >= arglen){
			return -1;
		}

		p2 = strstr(p1, " ");
		if(!p2){
			p2 = argv+arglen;
		}
		*p2 = '\0';

		if(p2-p1 > 0){
			g_group_id = atoi(p1);
		}else{
			return -1;
		}
	}

	return 0;
}

int check_prev_control_file(TCHAR *file)
{
	TCHAR prev[64];

	_stprintf(prev, _T("AFL_%d_%d"), g_node->group_index, g_node->initsync_id);
	if(!_tcscmp(prev, file))
		return 1;

	_stprintf(prev, _T("MFL_%d_%d"), g_node->group_index, g_node->initsync_id);
	if(!_tcscmp(prev, file))
		return 1;

	_stprintf(prev, _T("history_%d_%d"), g_node->group_index, g_node->initsync_id);
	if(!_tcscmp(prev, file))
		return 1;

	_stprintf(prev, _T("segment_%d_%d"), g_node->group_index, g_node->initsync_id);
	if(!_tcscmp(prev, file))
		return 1;

	_stprintf(prev, _T("sync_fail_%d_%d"), g_node->group_index, g_node->initsync_id);
	if(!_tcscmp(prev, file))
		return 1;

	return 0;
}

void cleanup_prev_initsync()
{
	TCHAR *home, subpath[260];
	scan_file_t *files = NULL;
	struct _stat64 sbuf;
	int count, i;

	home = CharToWChar(INITSYNC_HOME_PATH);
	count = scandir(home, &files);
	if(count > 0){
		for(i = 0; i < count; i++)
		{
			if(check_prev_control_file(files[i].name)){
				_stprintf(subpath, _T("%s\\%s"), home, files[i].name);

				DeleteFile(subpath);
			}
		}
	}
	SAFE_FREE(files);

	return 0;
}

//int main(int argc, char *argv[])
int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	int ret;
	char *argv;
	//node_t *node;
	char time[256];

	argv = GetCommandLineA();

	/* parse arg */
	ret = parse_arg(argv);
	if(ret == -1){
		return -1;
	}

	/* component-initializing */
	ret = init_component();
	if (ret < 0) {
		return -1;
	}

#ifdef _DEBUG
	g_logLevel = LOG_LEVEL_DEBUG;
#endif

	ret = get_local_time(time, 256);
	if(ret < 0)
		LOG_INFO(LOG_MOD_MAIN, "Start");
	else
		LOG_INFO(LOG_MOD_MAIN, "Initsync[group id : %d][initsync id : %d] Start at [%s]", g_group_id, g_initsync_id, time);

	/* set up node profile */
	g_node = init_node();
	if(!g_node){
		LOG_ERROR(LOG_MOD_MAIN, "Init_node fail, Finish Initsync");
		return -1;
	}

	/* Clean up prev Initsync */
	cleanup_prev_initsync();

	ret = init_service(g_node);
	if (ret < 0) {
		LOG_ERROR(LOG_MOD_MAIN, "Init_node service, Finish Initsync");
		return -1;
	}

	/* notify to SD */
	NOTIFY_WAIT(g_node->sd_notifier);

	/* start listener */
	start_dispatcher(g_node->dis);

	/* destroy node */
	destroy_node(g_node);

	LOG_INFO(LOG_MOD_MAIN, "+-+-+-+-+-+-+-+-+-+-+-+");
	LOG_INFO(LOG_MOD_MAIN, "+- INITSYNC Finalize -+");
	LOG_INFO(LOG_MOD_MAIN, "+-+-+-+-+-+-+-+-+-+-+-+");

	return 0;
}