#include "buf_pool.h"
#include "report.h"

buf_pool_t *init_buf_pool(int nr_entry, int data_size)
{
	buf_pool_t *p;
	int i;

	p = malloc(sizeof(buf_pool_t));
	if (!p)
		return NULL;

	memset(p, 0,sizeof(buf_pool_t));

	/* pool list init */
	init_list_node(&p->free_list);
	init_list_node(&p->alloc_list);
	
	
	/* mutex init */
	p->pool_mutex = CreateMutex(NULL, FALSE, NULL);
	if(!p->pool_mutex)
	{
		goto out_error;
	}

	/* event init */
	p->pool_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!p->pool_event)
	{
		goto out_error;
	}

	/* pool entry init */
	p->nr_total = nr_entry;
	p->fixed_data_size = data_size;
	for(i = 0; i < p->nr_total; i++)
	{
		buf_pool_entry_t *entry;
		entry = malloc(sizeof(buf_pool_entry_t));
		if (!entry) {
			/* destroy all objects */
			goto out_error;
		}
		entry->buf = malloc(data_size);
		if(!entry->buf)
			goto out_error;

		entry->state = BUF_POOL_ENTRY_STATE_FREE;
		list_add_tail(&entry->lnode, &p->free_list);
		++p->nr_free;
	}

	return p;

out_error:
	destroy_buf_pool(p);
	return NULL;
}

int destroy_buf_pool(buf_pool_t *p)
{
	list_node_t *pos, *nxt;
	buf_pool_entry_t *entry;

	if(p){
		WaitForSingleObject(p->pool_mutex, INFINITE);

		if (!list_empty(&p->alloc_list)) {
			list_for_each_safe(pos, nxt, &p->alloc_list)
			{
				entry = list_entry(pos, buf_pool_entry_t, lnode);
				list_del(&entry->lnode);
				//SAFE_FREE(entry->data);
				SAFE_FREE(entry);
				p->nr_alloc--;
				p->nr_total--;
			}
		}

		if (!list_empty(&p->free_list)) {
			list_for_each_safe(pos, nxt, &p->free_list)
			{
				entry = list_entry(pos, buf_pool_entry_t, lnode);
				list_del(&entry->lnode);
				SAFE_FREE(entry->buf);
				SAFE_FREE(entry);
				p->nr_free--;
				p->nr_total--;
			}
		}

		ReleaseMutex(p->pool_mutex);

		SAFE_CLOSE_HANDLE(p->pool_event);
		SAFE_CLOSE_HANDLE(p->pool_mutex);

		SAFE_FREE(p);
	}
	return 0;
}

char *alloc_buf(buf_pool_t *p, int *size){
	buf_pool_entry_t *entry;
	char *ret_buf;
	int ret;

	if(p == NULL)
		return NULL;

	WaitForSingleObject(p->pool_mutex, INFINITE);

	if(p->nr_free == 0 && list_empty(&p->free_list)){
		ReleaseMutex(p->pool_mutex);

		//LOG_DEBUG(LOG_MOD_UTIL, "Wait Push Event");
		ret = WaitForSingleObject(p->pool_event, INFINITE);
		if(ret == WAIT_FAILED){
#ifdef _DEBUG			
			LOG_DEBUG(LOG_MOD_UTIL, "wait failt\n");
#endif			
			return NULL;
		}else if(ret == WAIT_ABANDONED){
#ifdef _DEBUG			
			LOG_DEBUG(LOG_MOD_UTIL, "wait_abandoned\n");
#endif			
			return NULL;
		}else if(ret == WAIT_TIMEOUT){
#ifdef _DEBUG			
			LOG_DEBUG(LOG_MOD_UTIL, "time out\n");
#endif			
			return NULL;
		}
		//LOG_DEBUG(LOG_MOD_UTIL, "Wakeup Queue");
		WaitForSingleObject(p->pool_mutex, INFINITE);
	}

	if(!list_empty(&p->free_list)){
		entry = list_first_entry(&p->free_list, buf_pool_entry_t, lnode);

		entry->state = BUF_POOL_ENTRY_STATE_ALLOC;
		ret_buf = entry->buf;
		entry->buf = NULL;

		*size = p->fixed_data_size;

		list_move_tail(&entry->lnode, &p->alloc_list);
		p->nr_free--;
		p->nr_alloc++;
	}else{
		/* error */
		*size = 0;
		ret_buf = NULL;
	}

	ReleaseMutex(p->pool_mutex);

	return ret_buf;
}

void free_buf(const void *vp, const void *buf)
{
	buf_pool_t *p = (buf_pool_t *)vp;
	buf_pool_entry_t *entry;

	WaitForSingleObject(p->pool_mutex, INFINITE);

	if(p->nr_alloc == 0 && list_empty(&p->alloc_list)){
		/* error */

		ReleaseMutex(p->pool_mutex);
		return;
	}

	if(p->nr_total == 0){
		ReleaseMutex(p->pool_mutex);
		return;
	}

	entry = list_first_entry(&p->alloc_list, buf_pool_entry_t, lnode);

	entry->state = BUF_POOL_ENTRY_STATE_FREE;
	entry->buf = (char *)buf;

	memset(entry->buf, 0, p->fixed_data_size);

	list_move_tail(&entry->lnode, &p->free_list);
	p->nr_free++;
	p->nr_alloc--;

	SetEvent(p->pool_event);

	ReleaseMutex(p->pool_mutex);

	return;
}