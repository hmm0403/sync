/*
 * list.c
 * 
 * 이 list 모듈은 circular doubley linked list를 구현한다.
 *
 * 모듈 자체에서 lock mechanism을 지원하지 않으므로, 
 * 사용자는 list에 접근시 lock을 걸어야 한다.
 *
 */

#include <stdlib.h>
#include "list.h"

void init_list_node(list_node_t *list_node)
{
	list_node->next = list_node;
	list_node->prev = list_node;
}

void __list_add(list_node_t *new_node, list_node_t *prev_node, list_node_t *next_node)
{
	next_node->prev = new_node;
	new_node->next = next_node;
	new_node->prev = prev_node;
	prev_node->next = new_node;
}

void __list_del(list_node_t *prev_node, list_node_t *next_node)
{
	next_node->prev = prev_node;
	prev_node->next = next_node;
}

/* new_node를 head_node 리스트의 head에 추가한다. */
void list_add(list_node_t *new_node, list_node_t *head_node)
{
	__list_add(new_node, head_node, head_node->next);
}

void list_add_prev(list_node_t *new_node, list_node_t *node)
{
	__list_add(new_node, node->prev, node);
}

/* new_node를 head_node 리스트의 tail에 추가한다. */
void list_add_tail(list_node_t *new_node, list_node_t *head_node)
{
	__list_add(new_node, head_node->prev, head_node);
}

/* node를 node가 속한 리스트에서 dettach 시킨다. free 하지는 않는다. */
void list_del(list_node_t *node)
{
	__list_del(node->prev, node->next);
	node->next = NULL;
	node->prev = NULL;
}

/* node를 head_node 리스트의 head로 옮긴다. */
void list_move(list_node_t *node, list_node_t *head_node)
{
	__list_del(node->prev, node->next);
	list_add(node, head_node);
}

/* node를 head_node 리스트의 tail로 옮긴다. */
void list_move_tail(list_node_t *node, list_node_t *head_node)
{
	__list_del(node->prev, node->next);
	list_add_tail(node, head_node);
}

/* head_node 리스트가 비어있는지 검사한다. empty라면 1을 리턴한다. */
int list_empty(list_node_t *head_node)
{
	return head_node->next == head_node;

}

