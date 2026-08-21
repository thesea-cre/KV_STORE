#define TEST_SIZE 10000  // 测试数据量
/* 测试用节点：int型键值 */
struct test_node {
    int key;
    struct rb_node rb;
};

/* 节点比较函数 */
static int test_cmp(struct rb_node *a, struct rb_node *b)
{
    struct test_node *na = (struct test_node *)a;
    struct test_node *nb = (struct test_node *)b;
    return na->key - nb->key;
}

/* 键值比较函数 */
static int test_cmp_key(struct rb_node *node, const void *key)
{
    struct test_node *n = (struct test_node *)node;
    int k = *(const int *)key;
    return n->key - k;
}

/* 递归计算黑高，返回-1表示黑高不一致 */
static int check_black_height(struct rb_node *node)
{
    if (!node)
        return 1; // NULL叶子为黑色，黑高1

    int left_h = check_black_height(node->rb_left);
    int right_h = check_black_height(node->rb_right);

    if (left_h == -1 || right_h == -1 || left_h != right_h)
        return -1;

    return rb_is_black(node) ? left_h + 1 : left_h;
}

/* 递归验证红黑树5条性质 */
static int check_rb_properties(struct rb_node *node, struct rb_node *parent)
{
    if (!node)
        return 1;

    // 验证父指针正确性
    if (rb_parent(node) != parent)
        return 0;

    // 性质4：红节点的孩子必须是黑的
    if (rb_is_red(node)) {
        if (node->rb_left && rb_is_red(node->rb_left)) return 0;
        if (node->rb_right && rb_is_red(node->rb_right)) return 0;
    }

    // 递归验证左右子树
    if (!check_rb_properties(node->rb_left, node)) return 0;
    if (!check_rb_properties(node->rb_right, node)) return 0;

    return 1;
}

/* 完整校验红黑树合法性 */
static int rbtree_validate(struct rb_root *root)
{
    if (!root->rb_node)
        return 1;

    // 性质2：根节点必须是黑色
    if (!rb_is_black(root->rb_node))
        return 0;

    // 验证父指针与红节点性质
    if (!check_rb_properties(root->rb_node, NULL))
        return 0;

    // 性质5：所有路径黑高相同
    if (check_black_height(root->rb_node) == -1)
        return 0;

    return 1;
}
void rbtree_test(void)
{
    struct rb_root root = {NULL};
    struct test_node *nodes = malloc(sizeof(struct test_node) * TEST_SIZE);
    int *keys = malloc(sizeof(int) * TEST_SIZE);

    srand((unsigned)time(NULL));
    printf("=== 红黑树测试开始 ===\n");

    // 1. 生成不重复随机键
    for (int i = 0; i < TEST_SIZE; i++) {
        keys[i] = i;
    }
    // 打乱顺序
    for (int i = TEST_SIZE - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = keys[i];
        keys[i] = keys[j];
        keys[j] = tmp;
    }

    // 2. 批量插入测试
    for (int i = 0; i < TEST_SIZE; i++) {
        nodes[i].key = keys[i];
        rb_insert(&root, &nodes[i].rb, test_cmp);

        // 每100次校验一次性质
        if ((i + 1) % 1000 == 0) {
            assert(rbtree_validate(&root));
            printf("插入 %d 个节点，树性质校验通过\n", i + 1);
        }
    }
    assert(rbtree_validate(&root));
    printf("全部插入完成，树性质校验通过\n");

    // 3. 查找测试
    for (int i = 0; i < TEST_SIZE; i++) {
        int key = keys[i];
        struct rb_node *found = rb_find(&root, &key, test_cmp_key);
        assert(found != NULL);
        assert(((struct test_node *)found)->key == key);
    }
    printf("全部查找测试通过\n");

    // 4. 中序遍历验证有序性
    int last_key = -1;
    int count = 0;
    struct rb_node *cur = rb_first(&root);
    while (cur) {
        struct test_node *n = (struct test_node *)cur;
        assert(n->key > last_key);
        last_key = n->key;
        count++;
        cur = rb_next(cur);
    }
    assert(count == TEST_SIZE);
    printf("中序遍历有序性校验通过，节点数：%d\n", count);

    // 5. 随机删除测试
    for (int i = 0; i < TEST_SIZE; i++) {
        int idx = rand() % TEST_SIZE;
        if (nodes[idx].key == -1)
            continue; // 已删除跳过

        struct rb_node *to_del = &nodes[idx].rb;
        rb_erase(to_del, &root);
        nodes[idx].key = -1; // 标记已删除

        // 每1000次校验一次
        if ((i + 1) % 2000 == 0) {
            assert(rbtree_validate(&root));
            printf("删除 %d 次后，树性质校验通过\n", i + 1);
        }
    }
    assert(root.rb_node == NULL);
    printf("全部删除完成，树为空\n");

    free(nodes);
    free(keys);
    printf("=== 所有测试通过 ===\n");
}

int main()
{
    rbtree_test();
    return 0;
}