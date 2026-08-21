#ifndef RBTREE_H
#define RBTREE_H
#define RB_RED   0
#define RB_BLACK 1

#define __rb_parent(pc)    ((struct rb_node *)(pc & ~3))
#define __rb_color(pc)     ((pc) & 1)
#define rb_parent(r)       __rb_parent((r)->__rb_parent_color)
#define rb_color(r)        __rb_color((r)->__rb_parent_color)
#define rb_is_red(r)       (!rb_color(r))
#define rb_is_black(r)     rb_color(r)

#include <stddef.h>

struct rb_node {
    unsigned long  __rb_parent_color;
    struct rb_node *rb_right;
    struct rb_node *rb_left;
} __attribute__((aligned(sizeof(long))));

struct rb_root {
    struct rb_node *rb_node;
};

#define RB_ROOT_INIT { NULL }
#define RB_ROOT(name) struct rb_root name = RB_ROOT_INIT
#define rb_set_parent_color(rb, p, color) \
    (rb)->__rb_parent_color = (unsigned long)(p) | (color)

#define rb_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

typedef int (*rb_cmp_fn)(const struct rb_node *a, const struct rb_node *b);
typedef int (*rb_cmp_key_fn)(const struct rb_node *node, const void *key);

void rb_insert(struct rb_root *root, struct rb_node *node, rb_cmp_fn cmp);
void rb_erase(struct rb_node *node, struct rb_root *root);
struct rb_node *rb_find(struct rb_root *root, const void *key, rb_cmp_key_fn cmp_key);
struct rb_node *rb_first(const struct rb_root *root);

static inline int rb_empty(const struct rb_root *root)
{
    return root->rb_node == NULL;
}
int rbtree_validate(struct rb_root *root);

#endif