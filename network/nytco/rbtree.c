#include "rbtree.h"

#define RB_RED   0
#define RB_BLACK 1

#define __rb_parent(pc)    ((struct rb_node *)(pc & ~3))
#define __rb_color(pc)     ((pc) & 1)
#define rb_parent(r)       __rb_parent((r)->__rb_parent_color)
#define rb_color(r)        __rb_color((r)->__rb_parent_color)
#define rb_is_red(r)       (!rb_color(r))
#define rb_is_black(r)     rb_color(r)

static inline void rb_set_parent(struct rb_node *rb, struct rb_node *p)
{
    rb->__rb_parent_color = rb_color(rb) + (unsigned long)p;
}

static inline void rb_set_color(struct rb_node *rb, int color)
{
    rb->__rb_parent_color = (rb->__rb_parent_color & ~1) + color;
}

static void __rb_rotate_left(struct rb_node *node, struct rb_root *root)
{
    struct rb_node *right = node->rb_right;
    struct rb_node *parent = rb_parent(node);

    if ((node->rb_right = right->rb_left))
        rb_set_parent(right->rb_left, node);
    right->rb_left = node;

    rb_set_parent(right, parent);

    if (parent) {
        if (node == parent->rb_left)
            parent->rb_left = right;
        else
            parent->rb_right = right;
    } else
        root->rb_node = right;
    rb_set_parent(node, right);
}

static void __rb_rotate_right(struct rb_node *node, struct rb_root *root)
{
    struct rb_node *left = node->rb_left;
    struct rb_node *parent = rb_parent(node);

    if ((node->rb_left = left->rb_right))
        rb_set_parent(left->rb_right, node);
    left->rb_right = node;

    rb_set_parent(left, parent);

    if (parent) {
        if (node == parent->rb_right)
            parent->rb_right = left;
        else
            parent->rb_left = left;
    } else
        root->rb_node = left;
    rb_set_parent(node, left);
}

static void rb_insert_color(struct rb_node *node, struct rb_root *root)
{
    struct rb_node *parent, *gparent;

    while ((parent = rb_parent(node)) && rb_is_red(parent)) {
        gparent = rb_parent(parent);

        if (parent == gparent->rb_left) {
            struct rb_node *uncle = gparent->rb_right;
            if (uncle && rb_is_red(uncle)) {
                rb_set_color(uncle, RB_BLACK);
                rb_set_color(parent, RB_BLACK);
                rb_set_color(gparent, RB_RED);
                node = gparent;
                continue;
            }

            if (parent->rb_right == node) {
                __rb_rotate_left(parent, root);
                struct rb_node *tmp = parent;
                parent = node;
                node = tmp;
            }

            rb_set_color(parent, RB_BLACK);
            rb_set_color(gparent, RB_RED);
            __rb_rotate_right(gparent, root);
        } else {
            struct rb_node *uncle = gparent->rb_left;
            if (uncle && rb_is_red(uncle)) {
                rb_set_color(uncle, RB_BLACK);
                rb_set_color(parent, RB_BLACK);
                rb_set_color(gparent, RB_RED);
                node = gparent;
                continue;
            }

            if (parent->rb_left == node) {
                __rb_rotate_right(parent, root);
                struct rb_node *tmp = parent;
                parent = node;
                node = tmp;
            }

            rb_set_color(parent, RB_BLACK);
            rb_set_color(gparent, RB_RED);
            __rb_rotate_left(gparent, root);
        }
    }
    rb_set_color(root->rb_node, RB_BLACK);
}
static void __rb_erase_color(struct rb_node *node, struct rb_node *parent, struct rb_root *root)
{
    struct rb_node *other;

    while ((!node || rb_is_black(node)) && node != root->rb_node) {
        if (parent->rb_left == node) {
            other = parent->rb_right;

            // 情况1：兄弟节点是红色
            if (rb_is_red(other)) {
                rb_set_color(other, RB_BLACK);
                rb_set_color(parent, RB_RED);
                __rb_rotate_left(parent, root);
                other = parent->rb_right;
            }

            // 情况2：兄弟的两个孩子都是黑色（NULL视为黑色）
            if ((!other->rb_left || rb_is_black(other->rb_left)) &&
                (!other->rb_right || rb_is_black(other->rb_right))) {
                rb_set_color(other, RB_RED);
                node = parent;
                parent = rb_parent(node);
                continue;
            }

            // 情况3：兄弟左孩子红、右孩子黑
            if (!other->rb_right || rb_is_black(other->rb_right)) {
                if (other->rb_left)
                    rb_set_color(other->rb_left, RB_BLACK);
                rb_set_color(other, RB_RED);
                __rb_rotate_right(other, root);
                other = parent->rb_right;
            }

            // 情况4：兄弟右孩子红
            rb_set_color(other, rb_color(parent));
            rb_set_color(parent, RB_BLACK);
            if (other->rb_right)
                rb_set_color(other->rb_right, RB_BLACK);
            __rb_rotate_left(parent, root);
            node = root->rb_node;
            break;
        } else {
            other = parent->rb_left;

            // 对称情况1：兄弟节点是红色
            if (rb_is_red(other)) {
                rb_set_color(other, RB_BLACK);
                rb_set_color(parent, RB_RED);
                __rb_rotate_right(parent, root);
                other = parent->rb_left;
            }

            // 对称情况2：兄弟的两个孩子都是黑色
            if ((!other->rb_left || rb_is_black(other->rb_left)) &&
                (!other->rb_right || rb_is_black(other->rb_right))) {
                rb_set_color(other, RB_RED);
                node = parent;
                parent = rb_parent(node);
                continue;
            }

            // 对称情况3：兄弟右孩子红、左孩子黑
            if (!other->rb_left || rb_is_black(other->rb_left)) {
                if (other->rb_right)
                    rb_set_color(other->rb_right, RB_BLACK);
                rb_set_color(other, RB_RED);
                __rb_rotate_left(other, root);
                other = parent->rb_left;
            }

            // 对称情况4：兄弟左孩子红
            rb_set_color(other, rb_color(parent));
            rb_set_color(parent, RB_BLACK);
            if (other->rb_left)
                rb_set_color(other->rb_left, RB_BLACK);
            __rb_rotate_right(parent, root);
            node = root->rb_node;
            break;
        }
    }
    if (node)
        rb_set_color(node, RB_BLACK);
}
// void rb_erase(struct rb_node *node, struct rb_root *root)
// {
//     struct rb_node *child, *parent;
//     int color;

//     if (!node->rb_left)
//         child = node->rb_right;
//     else if (!node->rb_right)
//         child = node->rb_left;
//     else {
//         struct rb_node *old = node, *left;
//         node = node->rb_right;
//         while ((left = node->rb_left) != NULL)
//             node = left;
//         child = node->rb_right;
//         parent = rb_parent(node);
//         color = rb_color(node);

//         if (child)
//             rb_set_parent(child, parent);
//         if (parent == old)
//             parent->rb_right = child;
//         else
//             parent->rb_left = child;

//         node->__rb_parent_color = old->__rb_parent_color;
//         node->rb_right = old->rb_right;
//         node->rb_left = old->rb_left;

//         if (rb_parent(old)) {
//             if (rb_parent(old)->rb_left == old)
//                 rb_parent(old)->rb_left = node;
//             else
//                 rb_parent(old)->rb_right = node;
//         } else
//             root->rb_node = node;

//         rb_set_parent(old->rb_left, node);
//         if (old->rb_right)
//             rb_set_parent(old->rb_right, node);
//         goto color;
//     }

//     parent = rb_parent(node);
//     color = rb_color(node);

//     if (child)
//         rb_set_parent(child, parent);
//     if (parent) {
//         if (parent->rb_left == node)
//             parent->rb_left = child;
//         else
//             parent->rb_right = child;
//     } else
//         root->rb_node = child;

// color:
//     if (color == RB_BLACK)
//         __rb_erase_color(child, parent, root);
// }
void rb_erase(struct rb_node *node, struct rb_root *root)
{
    struct rb_node *child, *parent;
    int color;

    if (!node->rb_left)
        child = node->rb_right;
    else if (!node->rb_right)
        child = node->rb_left;
    else {
        struct rb_node *old = node, *left;
        node = node->rb_right;
        while ((left = node->rb_left) != NULL)
            node = left;
        child = node->rb_right;
        parent = rb_parent(node);
        color = rb_color(node);

        if (child)
            rb_set_parent(child, parent);
        if (parent == old)
            parent->rb_right = child;
        else
            parent->rb_left = child;

        node->__rb_parent_color = old->__rb_parent_color;
        node->rb_right = old->rb_right;
        node->rb_left = old->rb_left;

        if (rb_parent(old)) {
            if (rb_parent(old)->rb_left == old)
                rb_parent(old)->rb_left = node;
            else
                rb_parent(old)->rb_right = node;
        } else
            root->rb_node = node;

        rb_set_parent(old->rb_left, node);
        if (old->rb_right)
            rb_set_parent(old->rb_right, node);

        // ========== 修复：后继是被删节点右孩子时，修正parent ==========
        if (parent == old)
            parent = node;

        goto color;
    }

    parent = rb_parent(node);
    color = rb_color(node);

    if (child)
        rb_set_parent(child, parent);
    if (parent) {
        if (parent->rb_left == node)
            parent->rb_left = child;
        else
            parent->rb_right = child;
    } else
        root->rb_node = child;

color:
    if (color == RB_BLACK)
        __rb_erase_color(child, parent, root);
}

struct rb_node *rb_first(const struct rb_root *root)
{
    struct rb_node *n = root->rb_node;
    if (!n) return NULL;
    while (n->rb_left)
        n = n->rb_left;
    return n;
}

void rb_insert(struct rb_root *root, struct rb_node *node, rb_cmp_fn cmp)
{
    struct rb_node **p = &root->rb_node;
    struct rb_node *parent = NULL;

    while (*p) {
        parent = *p;
        if (cmp(node, parent) < 0)
            p = &parent->rb_left;
        else
            p = &parent->rb_right;
    }

    node->rb_left = node->rb_right = NULL;
    rb_set_parent_color(node, parent, RB_RED);
    *p = node;

    rb_insert_color(node, root);
}

struct rb_node *rb_find(struct rb_root *root, const void *key, rb_cmp_key_fn cmp_key)
{
    struct rb_node *node = root->rb_node;

    while (node) {
        int ret = cmp_key(node, key);
        if (ret == 0)
            return node;
        else if (ret > 0)
            node = node->rb_left;
        else
            node = node->rb_right;
    }
    return NULL;
}
static int check_black_height(struct rb_node *node)
{
    if (!node)
        return 1;

    int left_h = check_black_height(node->rb_left);
    int right_h = check_black_height(node->rb_right);

    if (left_h == -1 || right_h == -1 || left_h != right_h)
        return -1;

    return rb_is_black(node) ? left_h + 1 : left_h;
}

static int check_rb_properties(struct rb_node *node, struct rb_node *parent)
{
    if (!node)
        return 1;

    // 校验父指针正确性
    if (rb_parent(node) != parent)
        return 0;

    // 红节点的孩子必须是黑的
    if (rb_is_red(node)) {
        if (node->rb_left && rb_is_red(node->rb_left)) return 0;
        if (node->rb_right && rb_is_red(node->rb_right)) return 0;
    }

    // 递归校验
    if (!check_rb_properties(node->rb_left, node)) return 0;
    if (!check_rb_properties(node->rb_right, node)) return 0;

    return 1;
}

int rbtree_validate(struct rb_root *root)
{
    if (!root->rb_node)
        return 1;

    // 根节点必须为黑
    if (!rb_is_black(root->rb_node))
        return 0;

    // 校验父指针与红节点性质
    if (!check_rb_properties(root->rb_node, NULL))
        return 0;

    // 所有路径黑高一致
    if (check_black_height(root->rb_node) == -1)
        return 0;

    return 1;
}