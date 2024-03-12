#include "hashtable.h"
#include "report.h"

hashtable_t *init_hashtable()
{
	hashtable_t *tbl;
	int i;

	tbl = malloc(sizeof(hashtable_t));
	if(!tbl){
		LOG_ERROR(LOG_MOD_UTIL, "out of memory %s:%d", __FILE__, __LINE__);
		return 0;
	}

	tbl->nr_entry = 0;

	for(i = 0; i < INITSYNC_HASH_TABLE_KEY_SIZE; i++){
		init_list_node(&tbl->queue[i]);
	}

	/* mutex init */
	InitializeCriticalSection(&tbl->cs);

	return tbl;
}

void destroy_hash(hashtable_t *tbl)
{
}

hash_entry_t *find_hashtable(hashtable_t *tbl, uint32 key)
{
	hash_entry_t *entry;
	list_node_t *pos, *nxt;
	uint32 idx;

	EnterCriticalSection(&tbl->cs);

	idx = key % INITSYNC_HASH_TABLE_KEY_SIZE;

	if(list_empty(&tbl->queue[idx])){
		LeaveCriticalSection(&tbl->cs);
		return NULL;
	}

	list_for_each_safe(pos, nxt, &tbl->queue[idx])
	{
		entry = list_entry(pos, hash_entry_t, lnode);
		if(entry->key == key){
			LeaveCriticalSection(&tbl->cs);
			return entry;
		}
	}

	LeaveCriticalSection(&tbl->cs);

	return NULL;
}

void insert_hashtable(hashtable_t *tbl, hash_entry_t *entry)
{
	uint32 idx;

	EnterCriticalSection(&tbl->cs);

	idx = entry->key % INITSYNC_HASH_TABLE_KEY_SIZE;

	list_add_tail(&entry->lnode, &tbl->queue[idx]);

	tbl->nr_entry++;
	
	LeaveCriticalSection(&tbl->cs);
}

void remove_hashtable(hashtable_t *tbl, hash_entry_t *entry)
{
}