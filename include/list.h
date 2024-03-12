#ifndef __INCLUDE_LIST_H_
#define __INCLUDE_LIST_H_

#ifdef WIN32
#include "crt_dbg.h"
#endif
#include "extern_c.h"
#include <stddef.h>

typedef struct list_node_t {
	struct list_node_t *next, *prev;
} list_node_t;

/* list_node_t *node 가 속한 구조체의 포인터값을 반환한다. */
#define list_entry(ptr, type, member)	\
	((type *)((char *)(ptr)-(intptr_t)(&((type *)0)->member)))

#define list_next_entry(entry, type, member)	\
	list_entry(((list_node_t *)(&((type *)entry)->member)->next), type, member)

#define list_prev_entry(entry, type, member)	\
	list_entry(((list_node_t *)(&((type *)entry)->member)->prev), type, member)

/* list를 조회한다. */
#define list_for_each(pos, head)	\
	for (pos = (head)->next; pos != (head); pos = pos->next)

/* list를 조회한다. */
#define list_for_each_safe(pos, n, head)	\
	for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n= pos->next)

/* list의 첫 노드를 반환한다. */
#define list_first_entry(ptr, type, member)	\
	list_entry((ptr)->next, type, member)

/* list의 마지막 노드를 반환한다. */
#define list_last_entry(ptr, type, member)	\
	list_entry((ptr)->prev, type, member)

#define list_is_last_entry(entry, type, member, head) \
	(((list_node_t *)(&((type *)entry)->member)->next) == (head)) ? 1 : 0


EXTERN_C_START
void init_list_node(list_node_t *list_node);
void list_add(list_node_t *new_node, list_node_t *head_node);
void list_add_prev(list_node_t *new_node, list_node_t *head_node);
void list_add_tail(list_node_t *new_node, list_node_t *node);
void list_del(list_node_t *node);
void list_move(list_node_t *node, list_node_t *head_node);
void list_move_tail(list_node_t *node, list_node_t *head_node);
int list_empty(list_node_t *head_node);
EXTERN_C_END

#endif
