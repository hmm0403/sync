#ifndef __UTIL_HASHTABLE_H__
#define __UTIL_HASHTABLE_H__

#include <windows.h>
#include "list.h"
#include "type_defines.h"
#include "crt_dbg.h"

#define INITSYNC_HASH_TABLE_KEY_SIZE	(17)
#define INITSYNC_HASH_FIRST_INDEX		(0)

typedef struct hash_entry_t{
	list_node_t lnode;

	int type;
	uint32 key;
	void *data;
	int datalen;

	struct hash_entry_t *opposite;
}hash_entry_t;

typedef struct hashtable_t{
	int nr_entry;

	list_node_t queue[INITSYNC_HASH_TABLE_KEY_SIZE];

	CRITICAL_SECTION cs;
}hashtable_t;

hashtable_t *init_hashtable();
void destroy_hash(hashtable_t *tbl);
hash_entry_t *find_hashtable(hashtable_t *tbl, uint32 key);
void insert_hashtable(hashtable_t *tbl, hash_entry_t *entry);
void remove_hashtable(hashtable_t *tbl, hash_entry_t *entry);

#endif