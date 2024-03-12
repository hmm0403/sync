/*
 * list.c
 * 
 * �� list ����� circular doubley linked list�� �����Ѵ�.
 *
 * ��� ��ü���� lock mechanism�� �������� �����Ƿ�, 
 * ����ڴ� list�� ���ٽ� lock�� �ɾ�� �Ѵ�.
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

/* new_node�� head_node ����Ʈ�� head�� �߰��Ѵ�. */
void list_add(list_node_t *new_node, list_node_t *head_node)
{
	__list_add(new_node, head_node, head_node->next);
}

void list_add_prev(list_node_t *new_node, list_node_t *node)
{
	__list_add(new_node, node->prev, node);
}

/* new_node�� head_node ����Ʈ�� tail�� �߰��Ѵ�. */
void list_add_tail(list_node_t *new_node, list_node_t *head_node)
{
	__list_add(new_node, head_node->prev, head_node);
}

/* node�� node�� ���� ����Ʈ���� dettach ��Ų��. free ������ �ʴ´�. */
void list_del(list_node_t *node)
{
	__list_del(node->prev, node->next);
	node->next = NULL;
	node->prev = NULL;
}

/* node�� head_node ����Ʈ�� head�� �ű��. */
void list_move(list_node_t *node, list_node_t *head_node)
{
	__list_del(node->prev, node->next);
	list_add(node, head_node);
}

/* node�� head_node ����Ʈ�� tail�� �ű��. */
void list_move_tail(list_node_t *node, list_node_t *head_node)
{
	__list_del(node->prev, node->next);
	list_add_tail(node, head_node);
}

/* head_node ����Ʈ�� ����ִ��� �˻��Ѵ�. empty��� 1�� �����Ѵ�. */
int list_empty(list_node_t *head_node)
{
	return head_node->next == head_node;

}

