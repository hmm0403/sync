#include "queue.h"
#include "report.h"

queue_t *init_queue(int nr_entry)
{
	queue_t *q;
	int i;

	q = malloc(sizeof(queue_t));
	if (!q)
		return NULL;

	memset(q, 0,sizeof(queue_t));

	/* queue list init */
	init_list_node(&q->free_list);
	init_list_node(&q->queue_list);
	
	
	/* mutex init */
	q->queue_mutex = CreateMutex(NULL, FALSE, NULL);
	if(!q->queue_mutex)
	{
		goto out_error;
	}

	/* event init */
	q->push_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!q->push_event)
	{
		goto out_error;
	}

	q->pop_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!q->pop_event)
	{
		goto out_error;
	}

	/* queue entry init */
	q->nr_total = nr_entry;
	for(i = 0; i < q->nr_total; i++)
	{
		queue_entry_t *qe;
		qe = malloc(sizeof(queue_entry_t));
		if (!qe) {
			/* destroy all objects */
			goto out_error;
		}
		qe->len = 0;
		qe->data = NULL;

		qe->state = QUEUE_ENTRY_STATE_FREE;
		list_add_tail(&qe->lnode, &q->free_list);
		++q->nr_free;
	}

	return q;

out_error:
	destroy_queue(q, NULL, NULL);
	return NULL;
}

int flush_queue(queue_t *q, void *pool, FQUEUEFREE ffree)
{
	list_node_t *pos, *nxt;
	queue_entry_t *entry;

	if(q){
		WaitForSingleObject(q->queue_mutex, INFINITE);

		if (!list_empty(&q->queue_list)) {
			list_for_each_safe(pos, nxt, &q->queue_list)
			{
				entry = list_entry(pos, queue_entry_t, lnode);
				list_del(&entry->lnode);
				if(pool)
					ffree(pool, entry->data);
				SAFE_FREE(entry);
				q->nr_queue--;
				q->nr_total--;
			}
		}

		if (!list_empty(&q->free_list)) {
			list_for_each_safe(pos, nxt, &q->free_list)
			{
				entry = list_entry(pos, queue_entry_t, lnode);
				list_del(&entry->lnode);
				SAFE_FREE(entry);
				q->nr_free--;
				q->nr_total--;
			}
		}

		SetEvent(q->pop_event);
		SetEvent(q->push_event);

		ReleaseMutex(q->queue_mutex);
	}
	return 0;
}

int clear_queue(queue_t *q, void *pool, FQUEUEFREE ffree)
{
	list_node_t *pos, *nxt;
	queue_entry_t *entry;

	if(q){
		WaitForSingleObject(q->queue_mutex, INFINITE);

		if (!list_empty(&q->queue_list)) {
			list_for_each_safe(pos, nxt, &q->queue_list)
			{
				entry = list_entry(pos, queue_entry_t, lnode);
				entry->len = 0;
				if(pool)
					ffree(pool, entry->data);
				entry->data = NULL;
				entry->state = QUEUE_ENTRY_STATE_FREE;

				list_move_tail(&entry->lnode, &q->free_list);
				q->nr_queue--;
				q->nr_free++;
			}
		}

		ReleaseMutex(q->queue_mutex);
	}
	return 0;
}

int clear_N_push_queue(queue_t *q, void *data, int len, void *pool, FQUEUEFREE ffree)
{
	list_node_t *pos, *nxt;
	queue_entry_t *entry;

	if(q){
		WaitForSingleObject(q->queue_mutex, INFINITE);

		if (!list_empty(&q->queue_list)) {
			list_for_each_safe(pos, nxt, &q->queue_list)
			{
				entry = list_entry(pos, queue_entry_t, lnode);
				entry->len = 0;
				if(pool)
					ffree(pool, entry->data);
				entry->data = NULL;
				entry->state = QUEUE_ENTRY_STATE_FREE;

				list_move_tail(&entry->lnode, &q->free_list);
				q->nr_queue--;
				q->nr_free++;
			}
		}
		
		if(q->nr_total == 0){
			ReleaseMutex(q->queue_mutex);
			return -1;
		}

		entry = list_first_entry(&q->free_list, queue_entry_t, lnode);

		entry->data = data;
		entry->len = len;

		list_move_tail(&entry->lnode, &q->queue_list);
		q->nr_free--;
		q->nr_queue++;

		SetEvent(q->push_event);

		ReleaseMutex(q->queue_mutex);
	}
	return 0;
}

void wakeup_queue(queue_t *q){
		SetEvent(q->pop_event);
		SetEvent(q->push_event);
}

int destroy_queue(queue_t *q, void *pool, FQUEUEFREE ffree)
{
	list_node_t *pos, *nxt;
	queue_entry_t *entry;

	if(q){
		WaitForSingleObject(q->queue_mutex, INFINITE);

		if (!list_empty(&q->queue_list)) {
			list_for_each_safe(pos, nxt, &q->queue_list)
			{
				entry = list_entry(pos, queue_entry_t, lnode);
				list_del(&entry->lnode);
				if(pool)
					ffree(pool, entry->data);
				SAFE_FREE(entry);
				q->nr_queue--;
				q->nr_total--;
			}
		}

		if (!list_empty(&q->free_list)) {
			list_for_each_safe(pos, nxt, &q->free_list)
			{
				entry = list_entry(pos, queue_entry_t, lnode);
				list_del(&entry->lnode);
				SAFE_FREE(entry);
				q->nr_free--;
				q->nr_total--;
			}
		}

		ReleaseMutex(q->queue_mutex);

		SAFE_CLOSE_HANDLE(q->pop_event);
		SAFE_CLOSE_HANDLE(q->push_event);
		SAFE_CLOSE_HANDLE(q->queue_mutex);

		SAFE_FREE(q);
	}
	return 0;
}

#if 0
queue_entry_t *get_first_entry(queue_t *q, int blocking_flag)
{
	int ret;
	queue_entry_t *entry;

	entry = NULL;

	WaitForSingleObject(q->queue_mutex, INFINITE);

	if(q->nr_queue > 0 && !list_empty(&q->queue_list))
	{
		entry = list_first_entry(&q->queue_list, queue_entry_t, lnode);
	}else{
		if(blocking_flag != 0){
			ReleaseMutex(q->queue_mutex);

			ret = WaitForSingleObject(q->push_event, INFINITE);
			if(ret == WAIT_FAILED){
#ifdef _DEBUG			
				LOG_DEBUG("wait failt\n");
#endif			
				return NULL;
			}else if(ret == WAIT_ABANDONED){
#ifdef _DEBUG			
				LOG_DEBUG("wait_abandoned\n");
#endif			
				return NULL;
			}else if(ret == WAIT_TIMEOUT){
#ifdef _DEBUG			
				LOG_DEBUG("time out\n");
#endif			
				return NULL;
			}

			WaitForSingleObject(q->queue_mutex, INFINITE);
			entry = list_first_entry(&q->queue_list, queue_entry_t, lnode);
		}
	}
	
	ReleaseMutex(q->queue_mutex);

	return entry;
}
#endif

char *popQ(queue_t *q, int *len, int blocking)
{
	queue_entry_t *entry;
	int ret;
	char *ret_data;

	*len = 0;

	if(q == NULL)
		return NULL;

	WaitForSingleObject(q->queue_mutex, INFINITE);

	if(q->nr_queue == 0 && list_empty(&q->queue_list)){
		if(blocking){
			ReleaseMutex(q->queue_mutex);

			//LOG_DEBUG(LOG_MOD_UTIL, "Wait Push Event 0x%x", q);
			ret = WaitForSingleObject(q->push_event, INFINITE);
			if(ret == WAIT_FAILED){
#ifdef _DEBUG			
				LOG_DEBUG(LOG_MOD_UTIL, "wait failt\n");
#endif			
				*len = -1;
				return NULL;
			}else if(ret == WAIT_ABANDONED){
#ifdef _DEBUG			
				LOG_DEBUG(LOG_MOD_UTIL, "wait_abandoned\n");
#endif			
				*len = -1;
				return NULL;
			}else if(ret == WAIT_TIMEOUT){
#ifdef _DEBUG			
				LOG_DEBUG(LOG_MOD_UTIL, "time out\n");
#endif			
				*len = -1;
				return NULL;
			}

			//LOG_DEBUG(LOG_MOD_UTIL, "Wakeup Queue 0x%x", q);
			WaitForSingleObject(q->queue_mutex, INFINITE);
		}else{ // non blocking call
			ReleaseMutex(q->queue_mutex);
			return NULL;
		}
	}

	if(q->nr_total == 0){
		*len = -1;
		ReleaseMutex(q->queue_mutex);
		return NULL;
	}

	ret_data = NULL;

	if(!list_empty(&q->queue_list)){
		entry = list_first_entry(&q->queue_list, queue_entry_t, lnode);

		*len = entry->len;

		ret_data = entry->data;
		entry->data = NULL;
		entry->len = 0;
		entry->state = QUEUE_ENTRY_STATE_FREE;

		list_move_tail(&entry->lnode, &q->free_list);
		q->nr_queue--;
		q->nr_free++;

		SetEvent(q->pop_event);
	}

	ResetEvent(q->push_event);
	ReleaseMutex(q->queue_mutex);

	return ret_data;
}

int pushQ(queue_t *q, void *data, int len)//, entry_data_type_t type)
{
	queue_entry_t *entry;
	int ret;

	WaitForSingleObject(q->queue_mutex, INFINITE);

	if(q->nr_free == 0 && list_empty(&q->free_list)){
		ReleaseMutex(q->queue_mutex);

		ret = WaitForSingleObject(q->pop_event, INFINITE);
		if(ret == WAIT_FAILED){
#ifdef _DEBUG			
			LOG_DEBUG(LOG_MOD_UTIL, "wait fail\n");
#endif			
			ResetEvent(q->pop_event);
			return -1;
		}else if(ret == WAIT_ABANDONED){
#ifdef _DEBUG			
			LOG_DEBUG(LOG_MOD_UTIL, "wait_abandoned\n");
#endif			
			ResetEvent(q->pop_event);
			return -1;
		}else if(ret == WAIT_TIMEOUT){
#ifdef _DEBUG			
			LOG_DEBUG(LOG_MOD_UTIL, "time out\n");
#endif		
			ResetEvent(q->pop_event);
			return -1;
		}

		//ResetEvent(q->pop_event);
		WaitForSingleObject(q->queue_mutex, INFINITE);
	}

	if(q->nr_total == 0){
		ReleaseMutex(q->queue_mutex);
		return -1;
	}

	entry = list_first_entry(&q->free_list, queue_entry_t, lnode);

	entry->data = data;
	entry->len = len;
	
	list_move_tail(&entry->lnode, &q->queue_list);
	q->nr_free--;
	q->nr_queue++;

	ResetEvent(q->pop_event);
	SetEvent(q->push_event);

	ReleaseMutex(q->queue_mutex);

	return q->nr_free;
}

/*
entry_data_type_t get_first_entry_type(queue_t *q, int blocking)
{
	queue_entry_t *entry;
	int ret;

	WaitForSingleObject(q->queue_mutex, INFINITE);

	if(q->nr_queue == 0 && list_empty(&q->queue_list)){
		if(blocking){
			ReleaseMutex(q->queue_mutex);

			ret = WaitForSingleObject(q->push_event, INFINITE);
			if(ret == WAIT_FAILED){
#ifdef _DEBUG			
				LOG_DEBUG(LOG_MOD_UTIL, "wait failt\n");
#endif			
				return -1;
			}else if(ret == WAIT_ABANDONED){
#ifdef _DEBUG			
				LOG_DEBUG(LOG_MOD_UTIL, "wait_abandoned\n");
#endif			
				return -1;
			}else if(ret == WAIT_TIMEOUT){
#ifdef _DEBUG			
				LOG_DEBUG(LOG_MOD_UTIL, "time out\n");
#endif			
				return -1;
			}
			WaitForSingleObject(q->queue_mutex, INFINITE);
		}else{ // non blocking call
			ReleaseMutex(q->queue_mutex);
			return -1;
		}
	}

	if(q->nr_total == 0){
		ReleaseMutex(q->queue_mutex);
		return -1;
	}

	entry = list_first_entry(&q->queue_list, queue_entry_t, lnode);

	ReleaseMutex(q->queue_mutex);

	return entry->type;
}

*/
void set_queue_nagle(queue_t *q, int onoff)
{
	q->nagle = onoff;
}

void queue_pop_wakeup(queue_t *q)
{
	SetEvent(q->push_event);
}

void queue_push_wakeup(queue_t *q)
{
	SetEvent(q->pop_event);
}