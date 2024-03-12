#ifndef __INIREADER_INI_READER_H_
#define __INIREADER_INI_READER_H_

#include "crt_dbg.h"
#include "extern_c.h"

typedef struct ini_line_t {
	char *name;
	char *data;
	struct ini_line_t *next;
} ini_line_t;

typedef struct ini_t {
	char *section;
	ini_line_t *ini_line;
	struct ini_t *next;
} ini_t;

EXTERN_C_START
ini_t *ini_read(char *path, int *err);
ini_t *ini_get_section(ini_t *ini, const char *s);
char *ini_get_data(ini_t *ini, const char *name);
char *ini_get_data_ex(ini_t *ini, const char *section, const char *name);
void ini_destroy(ini_t *ini);
EXTERN_C_END

#endif
