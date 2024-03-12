#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ini_reader.h"
#include "defines.h"

#define LINE_SIZE 1024
#define syntax_error(no, reason) fprintf(stderr, "ini file syntax error:line %d:\t%s\n", no, reason)

typedef enum {
	NO_READ,
	SECTION_READ,
	LINE_READ
} ini_state_t;

static void ini_del_line(ini_line_t *ini_line)
{
	ini_line_t *pil, *tmp;
	
	pil = ini_line;
	while(pil) {
		tmp = pil;
		pil = pil->next;
		if (tmp->name) {
			SAFE_FREE(tmp->name);
			tmp->name = NULL;
		}
		if (tmp->data) {
			SAFE_FREE(tmp->data);
			tmp->data = NULL;
		}
		SAFE_FREE(tmp);
		tmp = NULL;
	}
}

void ini_destroy(ini_t *ini)
{
	ini_t *pi, *tmp;

	pi = ini;
	while (pi != NULL) {
		tmp = pi;
		pi = pi->next;	

		if (tmp->section) {
			SAFE_FREE(tmp->section);
			tmp->section = NULL;
		}
		if (tmp->ini_line) {
			ini_del_line(tmp->ini_line);
			tmp->ini_line = NULL;
		}
		SAFE_FREE(tmp);
		tmp = NULL;
	}
	ini = NULL;
}

static int ini_line_read(char *buf, ini_t *ini, ini_line_t *ini_line, int no, int *err)
{
	char *p1, *p2;
	int i;
	
	*err = ERROR_INITSYNC_SUCCESS;

	p1 = buf;
	/* skip blank */
	while (*p1 == ' ' || *p1 == '\t')
		++p1;		
	p2 = strrchr(p1, '=');
	if (!p2) {
		/* Bad syntax: no '=' */
		*err = ERROR_INI_WRONG_FORMAT;
		syntax_error(no, "'name=data' line needs '='");
		return -1;
	}
	ini_line->name = malloc(p2 - p1 + 1);
	if (!ini_line->name) {
		/* system error */
		*err = ERROR_SYS_ALLOC;
		return -1;
	}
	strncpy(ini_line->name, p1, p2 - p1);
	
	i = 0;
	while(ini_line->name[p2 - p1 - i - 1] == ' ')
		i++;
	ini_line->name[p2 - p1 - i] = '\0';

	/* Syntax check - redefined name problem */
	if (ini_get_data(ini, ini_line->name)) {
		syntax_error(no, "redefined name error");
		*err = ERROR_INI_WRONG_FORMAT;
		SAFE_FREE(ini_line->name);
		return -1;
	}

	p1 = ++p2;	
	while (*p1 == ' '){
		p1++;
		p2++;
	}

	while (*p2 != '\r' && *p2 != '\n')
		++p2;
	if (p1 == p2) {
		/* Bad syntax: no right-hand value */
		syntax_error(no, "no data error");
		*err = ERROR_INI_NO_DATA;
		SAFE_FREE(ini_line->name);
		return -1;
	}
	ini_line->data = malloc(p2 - p1 + 1);
	if (!ini_line->data) {
		/* system error */
		*err = ERROR_INI_WRONG_FORMAT;
		SAFE_FREE(ini_line->name);
		return -1;
	}
	strncpy(ini_line->data, p1, p2 - p1);
	i = 0;
	while(ini_line->data[p2 - p1 - i - 1] == ' ')
		i++;
	ini_line->data[p2 - p1 - i] = '\0';
	
	return 0;	
}

ini_t *ini_read(char *path, int *err)
{
	int ret;
	int no = 0;
	FILE *fp;
	char line[LINE_SIZE];
	char *p1, *p2;
	ini_t *ini, *ini_head, *ini_tail;
	ini_line_t *ini_line, *line_tail;
	ini_state_t state;

	*err = ERROR_INITSYNC_SUCCESS;

	fp = fopen(path, "r");
	if (!fp) {
		perror("fopen");
		*err = ERROR_INI_NO_FILE;
		return NULL;
	}

	fgets(line, LINE_SIZE, fp);
	if (!line) {
		perror("fgets");
		*err = ERROR_INI_WRONG_FORMAT;
		goto release_out;
	}

	ini = NULL;
	ini_head = NULL;
	ini_tail = NULL;
	line_tail = NULL;
	state = NO_READ;

	do {
		++no;
		p1 = line;
		if (p1[0] == ';' || p1[0] == '\r' || p1[0] == '\n') {
			/* Commentary or Empty line skip */
			continue;
		} else if (p1[0] == '[') {
			/* SECTION Start */
			/* ini struct initialization */
			ini = malloc(sizeof(ini_t));
			if (!ini) {
				*err = ERROR_SYS_ALLOC;
				goto release_out;
			}
			ini->section = NULL;
			ini->ini_line = NULL;
			ini->next = NULL;

			/* parse section */
			p2 = strrchr(line, ']');
			if (!p2) {
				/* Bad syntax */
				*err = ERROR_INI_WRONG_FORMAT;
				syntax_error(no, "bad section name");
				goto release_out;
			}
			p1++;
			ini->section = malloc(p2 - p1 + 1);
			if (!ini->section) {
				/* system error */
				*err = ERROR_SYS_ALLOC;
				goto release_out;
			}
			strncpy(ini->section, p1, p2 - p1);
			ini->section[p2 - p1] = '\0';

			/* syntax check - section redefinition problem */
			if(ini_get_section(ini_head, ini->section)) {
				/* Bad syntax */
				*err = ERROR_INI_WRONG_FORMAT;
				syntax_error(no, "redefined section error");
				goto release_out;
			}
			
	
			/* line_tail is the end of ini_line list */
			line_tail = ini->ini_line;

			/* link ini */
			if (state == NO_READ) {
				ini_head = ini_tail = ini;
			} else {
				ini_tail->next = ini;
				ini_tail = ini_tail->next;		
			}

			state = SECTION_READ;
		} else {
			/* Line Start */
			if (state == NO_READ) {
				/* Bad syntax */
				syntax_error(no, "no section error");
				*err = ERROR_INI_WRONG_FORMAT;
				goto release_out;	
			}
			
			/* ini_line struct initialization */
			ini_line = malloc(sizeof(ini_line_t));
			if (!ini_line) {
				/* system error */
				*err = ERROR_SYS_ALLOC;
				goto release_out;
			}
			ini_line->name = NULL;
			ini_line->data = NULL;
			ini_line->next = NULL;
			
			/* parse line */
			ret = ini_line_read(line, ini, ini_line, no, err);
			if (ret < 0) {
				/* syntax or system error */
				goto release_out;
			}
			
			/* link line */
			if (!line_tail) {
				/* ini has no ini_line */
				line_tail = ini->ini_line = ini_line;
			} else {
				line_tail->next = ini_line;
				line_tail = line_tail->next;	
			}
		}
	} while(fgets(line, LINE_SIZE, fp));
	
	fclose(fp);
	return ini_head;

release_out:
	fclose(fp);
	ini_destroy(ini_head);
	return NULL;
}

ini_t *ini_get_section(ini_t *ini, const char *s)
{
	ini_t *pi;
	
	pi = ini;
	while (pi) {
		if (pi->section && !_stricmp(pi->section, s)) {
			return pi;
		}
		pi = pi->next;
	}
	return NULL;
}

char *ini_get_data(ini_t *ini, const char *name)
{
	ini_line_t *pil;

	if (!ini)
		return NULL;

	pil = ini->ini_line;
	while (pil) {
		if (!_stricmp(pil->name, name))
			return pil->data;
		pil = pil->next;
	}
	return NULL;
}

char *ini_get_data_ex(ini_t *ini, const char *section, const char *name)
{
	ini_t *pi;
	
	pi = ini_get_section(ini, section);
	return ini_get_data(pi, name);
}
