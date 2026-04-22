#ifndef BPTREE_H
#define BPTREE_H

#include "utils.h"

#define BPTREE_MAX_KEYS 31

typedef struct BPTreeNode
{
    int is_leaf;
    int key_count;
    int keys[BPTREE_MAX_KEYS];
    struct BPTreeNode *parent;
    struct BPTreeNode *children[BPTREE_MAX_KEYS + 1];
    int row_indices[BPTREE_MAX_KEYS];
    struct BPTreeNode *next;
} BPTreeNode;

/*
 * 비어 있는 B+ 트리 노드 하나를 생성한다.
 */
BPTreeNode *bptree_create_node(int is_leaf);

/*
 * key가 있어야 할 리프 노드를 찾아 반환한다.
 */
BPTreeNode *bptree_find_leaf(BPTreeNode *root, int key);

/*
 * key를 검색해 row_index를 out_row_index에 저장한다.
 */
int bptree_search(BPTreeNode *root, int key, int *out_row_index);

/*
 * key -> row_index 쌍을 트리에 삽입한다.
 */
int bptree_insert(BPTreeNode **root, int key, int row_index);

/*
 * leaf 노드에 정렬 상태를 유지하며 key를 삽입한다.
 */
int bptree_insert_into_leaf(BPTreeNode *leaf, int key, int row_index);

/*
 * 가득 찬 leaf를 분할하며 key를 삽입한다.
 */
int bptree_split_leaf(BPTreeNode **root, BPTreeNode *leaf,
                      int key, int row_index);

/*
 * 분할된 자식의 분리 키를 부모에 삽입한다.
 */
int bptree_insert_into_parent(BPTreeNode **root, BPTreeNode *left,
                              int key, BPTreeNode *right);

/*
 * 가득 찬 내부 노드를 분할하며 새 키를 삽입한다.
 */
int bptree_split_internal(BPTreeNode **root, BPTreeNode *node,
                          int left_child_index, int key,
                          BPTreeNode *right_child);

/*
 * 트리가 소유한 모든 노드를 해제한다.
 */
void bptree_free(BPTreeNode *root);

#endif
