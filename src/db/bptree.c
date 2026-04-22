#include "bptree.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * 부모 자식 배열에서 특정 child 위치를 찾는다.
 */
static int bptree_find_child_index(const BPTreeNode *parent,
                                   const BPTreeNode *child) {
    int i;

    if (parent == NULL || child == NULL) {
        return FAILURE;
    }

    for (i = 0; i <= parent->key_count; i++) {
        if (parent->children[i] == child) {
            return i;
        }
    }

    return FAILURE;
}

/*
 * 내부 노드에 key와 오른쪽 자식을 지정 위치에 삽입한다.
 */
static void bptree_insert_into_internal(BPTreeNode *node, int child_index,
                                        int key, BPTreeNode *right_child) {
    int i;

    for (i = node->key_count; i > child_index; i--) {
        node->keys[i] = node->keys[i - 1];
    }
    for (i = node->key_count + 1; i > child_index + 1; i--) {
        node->children[i] = node->children[i - 1];
    }

    node->keys[child_index] = key;
    node->children[child_index + 1] = right_child;
    if (right_child != NULL) {
        right_child->parent = node;
    }
    node->key_count++;
}

BPTreeNode *bptree_create_node(int is_leaf) {
    BPTreeNode *node;

    node = (BPTreeNode *)calloc(1, sizeof(BPTreeNode));
    if (node == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return NULL;
    }

    node->is_leaf = is_leaf;
    return node;
}

BPTreeNode *bptree_find_leaf(BPTreeNode *root, int key) {
    BPTreeNode *current;
    int i;

    current = root;
    while (current != NULL && !current->is_leaf) {
        for (i = 0; i < current->key_count; i++) {
            if (key < current->keys[i]) {
                break;
            }
        }
        current = current->children[i];
    }

    return current;
}

int bptree_search(BPTreeNode *root, int key, int *out_row_index) {
    BPTreeNode *leaf;
    int i;

    if (root == NULL || out_row_index == NULL) {
        return FAILURE;
    }

    leaf = bptree_find_leaf(root, key);
    if (leaf == NULL) {
        return FAILURE;
    }

    for (i = 0; i < leaf->key_count; i++) {
        if (leaf->keys[i] == key) {
            *out_row_index = leaf->row_indices[i];
            return SUCCESS;
        }
    }

    return FAILURE;
}

int bptree_insert_into_leaf(BPTreeNode *leaf, int key, int row_index) {
    int insert_index;
    int i;

    if (leaf == NULL || !leaf->is_leaf || leaf->key_count >= BPTREE_MAX_KEYS) {
        return FAILURE;
    }

    insert_index = 0;
    while (insert_index < leaf->key_count && leaf->keys[insert_index] < key) {
        insert_index++;
    }

    for (i = leaf->key_count; i > insert_index; i--) {
        leaf->keys[i] = leaf->keys[i - 1];
        leaf->row_indices[i] = leaf->row_indices[i - 1];
    }

    leaf->keys[insert_index] = key;
    leaf->row_indices[insert_index] = row_index;
    leaf->key_count++;
    return SUCCESS;
}

int bptree_insert_into_parent(BPTreeNode **root, BPTreeNode *left,
                              int key, BPTreeNode *right) {
    BPTreeNode *parent;
    BPTreeNode *new_root;
    int child_index;

    if (root == NULL || left == NULL || right == NULL) {
        return FAILURE;
    }

    parent = left->parent;
    if (parent == NULL) {
        new_root = bptree_create_node(0);
        if (new_root == NULL) {
            return FAILURE;
        }

        new_root->keys[0] = key;
        new_root->children[0] = left;
        new_root->children[1] = right;
        new_root->key_count = 1;
        left->parent = new_root;
        right->parent = new_root;
        *root = new_root;
        return SUCCESS;
    }

    child_index = bptree_find_child_index(parent, left);
    if (child_index == FAILURE) {
        return FAILURE;
    }

    if (parent->key_count < BPTREE_MAX_KEYS) {
        bptree_insert_into_internal(parent, child_index, key, right);
        return SUCCESS;
    }

    return bptree_split_internal(root, parent, child_index, key, right);
}

int bptree_split_leaf(BPTreeNode **root, BPTreeNode *leaf,
                      int key, int row_index) {
    BPTreeNode *new_leaf;
    int temp_keys[BPTREE_MAX_KEYS + 1];
    int temp_row_indices[BPTREE_MAX_KEYS + 1];
    int total_keys;
    int insert_index;
    int split_index;
    int i;
    int j;

    if (root == NULL || leaf == NULL || !leaf->is_leaf) {
        return FAILURE;
    }

    new_leaf = bptree_create_node(1);
    if (new_leaf == NULL) {
        return FAILURE;
    }

    total_keys = leaf->key_count + 1;
    insert_index = 0;
    while (insert_index < leaf->key_count && leaf->keys[insert_index] < key) {
        insert_index++;
    }

    for (i = 0, j = 0; i < total_keys; i++) {
        if (i == insert_index) {
            temp_keys[i] = key;
            temp_row_indices[i] = row_index;
            continue;
        }
        temp_keys[i] = leaf->keys[j];
        temp_row_indices[i] = leaf->row_indices[j];
        j++;
    }

    split_index = total_keys / 2;
    leaf->key_count = 0;
    for (i = 0; i < split_index; i++) {
        leaf->keys[i] = temp_keys[i];
        leaf->row_indices[i] = temp_row_indices[i];
        leaf->key_count++;
    }

    for (i = split_index, j = 0; i < total_keys; i++, j++) {
        new_leaf->keys[j] = temp_keys[i];
        new_leaf->row_indices[j] = temp_row_indices[i];
        new_leaf->key_count++;
    }

    new_leaf->next = leaf->next;
    leaf->next = new_leaf;
    new_leaf->parent = leaf->parent;

    return bptree_insert_into_parent(root, leaf, new_leaf->keys[0], new_leaf);
}

int bptree_split_internal(BPTreeNode **root, BPTreeNode *node,
                          int left_child_index, int key,
                          BPTreeNode *right_child) {
    BPTreeNode *new_node;
    int temp_keys[BPTREE_MAX_KEYS + 1];
    BPTreeNode *temp_children[BPTREE_MAX_KEYS + 2];
    int total_keys;
    int promote_index;
    int promote_key;
    int i;
    int j;

    if (root == NULL || node == NULL || node->is_leaf) {
        return FAILURE;
    }

    new_node = bptree_create_node(0);
    if (new_node == NULL) {
        return FAILURE;
    }

    total_keys = node->key_count + 1;

    for (i = 0, j = 0; i < total_keys; i++) {
        if (i == left_child_index) {
            temp_keys[i] = key;
            continue;
        }
        temp_keys[i] = node->keys[j];
        j++;
    }

    for (i = 0, j = 0; i < total_keys + 1; i++) {
        if (i == left_child_index + 1) {
            temp_children[i] = right_child;
            continue;
        }
        temp_children[i] = node->children[j];
        j++;
    }

    promote_index = total_keys / 2;
    promote_key = temp_keys[promote_index];

    node->key_count = 0;
    for (i = 0; i < BPTREE_MAX_KEYS + 1; i++) {
        node->children[i] = NULL;
    }

    for (i = 0; i < promote_index; i++) {
        node->keys[i] = temp_keys[i];
        node->children[i] = temp_children[i];
        if (node->children[i] != NULL) {
            node->children[i]->parent = node;
        }
        node->key_count++;
    }
    node->children[promote_index] = temp_children[promote_index];
    if (node->children[promote_index] != NULL) {
        node->children[promote_index]->parent = node;
    }

    for (i = promote_index + 1, j = 0; i < total_keys; i++, j++) {
        new_node->keys[j] = temp_keys[i];
        new_node->key_count++;
    }
    for (i = promote_index + 1, j = 0; i < total_keys + 1; i++, j++) {
        new_node->children[j] = temp_children[i];
        if (new_node->children[j] != NULL) {
            new_node->children[j]->parent = new_node;
        }
    }

    new_node->parent = node->parent;
    return bptree_insert_into_parent(root, node, promote_key, new_node);
}

int bptree_insert(BPTreeNode **root, int key, int row_index) {
    BPTreeNode *leaf;
    int ignored_row_index;

    if (root == NULL) {
        return FAILURE;
    }

    if (*root == NULL) {
        *root = bptree_create_node(1);
        if (*root == NULL) {
            return FAILURE;
        }

        (*root)->keys[0] = key;
        (*root)->row_indices[0] = row_index;
        (*root)->key_count = 1;
        return SUCCESS;
    }

    if (bptree_search(*root, key, &ignored_row_index) == SUCCESS) {
        fprintf(stderr, "Error: Duplicate B+ tree key %d.\n", key);
        return FAILURE;
    }

    leaf = bptree_find_leaf(*root, key);
    if (leaf == NULL) {
        return FAILURE;
    }

    if (leaf->key_count < BPTREE_MAX_KEYS) {
        return bptree_insert_into_leaf(leaf, key, row_index);
    }

    return bptree_split_leaf(root, leaf, key, row_index);
}

void bptree_free(BPTreeNode *root) {
    int i;

    if (root == NULL) {
        return;
    }

    if (!root->is_leaf) {
        for (i = 0; i <= root->key_count; i++) {
            bptree_free(root->children[i]);
        }
    }

    free(root);
}
