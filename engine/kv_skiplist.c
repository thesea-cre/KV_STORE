#include "kv_skiplist.h"
#include "tool.h" // 假设 kv_malloc 和 kv_free 在这里

// 随机生成层数：每次有 1/4 的概率向上晋升，最高不超过 KV_MAX_LEVEL
static int random_level() {
    int level = 1;
    while ((rand() & 0xFFFF) < (0xFFFF >> 2) && level < KV_MAX_LEVEL) {
        level++;
    }
    return level;
}

// 创建跳表
int kv_skiplist_create(kv_skiplist_t *init) {
    if (init == NULL) return -1;
    
    // 创建头节点
    init->header = (kv_skiplist_node_t *)kv_malloc(sizeof(kv_skiplist_node_t));
    if (init->header == NULL) return -2;
    
    // 头节点的 key 和 value 为空
    init->header->key = NULL;
    init->header->value = NULL;
    init->header->value_len = 0;
    
    // 给头节点的 forward 数组分配内存
    init->header->forward = (kv_skiplist_node_t **)kv_malloc(KV_MAX_LEVEL * sizeof(kv_skiplist_node_t *));
    if (init->header->forward == NULL) {
        kv_free(init->header);
        return -2;
    }
    memset(init->header->forward, 0, KV_MAX_LEVEL * sizeof(kv_skiplist_node_t *));
    
    init->level = 1;
    init->idx = 0;
    return 0;
}

// 销毁跳表 (释放所有节点，防止内存泄漏)
int kv_skiplist_destory(kv_skiplist_t *init) {
    if (init == NULL || init->header == NULL) return -1;
    
    kv_skiplist_node_t *node = init->header->forward[0]; // 从最底层开始遍历
    while (node != NULL) {
        kv_skiplist_node_t *next = node->forward[0];
        if (node->key) kv_free(node->key);
        if (node->value) kv_free(node->value);
        if (node->forward) kv_free(node->forward);
        kv_free(node);
        node = next;
    }
    
    kv_free(init->header->forward);
    kv_free(init->header);
    init->header = NULL;
    init->level = 0;
    init->idx = 0;
    return 0;
}

// 插入数据 (对应 array 的 set)
int kv_skiplist_set(kv_skiplist_t *init, char *key, uint8_t *value, uint32_t value_size) {
    if (init == NULL || key == NULL || value == NULL || value_size == 0) return -1;

    kv_skiplist_node_t *update[KV_MAX_LEVEL]; // 记录每一层需要插入位置的前驱节点
    kv_skiplist_node_t *current = init->header;

    // 1. 从最高层往下查找插入位置
    for (int i = init->level - 1; i >= 0; i--) {
        while (current->forward[i] != NULL && strcmp(current->forward[i]->key, key) < 0) {
            current = current->forward[i];
        }
        update[i] = current;
    }

    // 检查是否已存在
    current = current->forward[0];
    if (current != NULL && strcmp(current->key, key) == 0) {
        return 1; // 已存在，拒绝
    }

    // 2. 生成新节点层数
    int new_level = random_level();
    if (new_level > init->level) {
        for (int i = init->level; i < new_level; i++) {
            update[i] = init->header; // 超出当前层数的高层，前驱直接是 header
        }
        init->level = new_level;
    }

    // 3. 分配并初始化新节点
    kv_skiplist_node_t *new_node = (kv_skiplist_node_t *)kv_malloc(sizeof(kv_skiplist_node_t));
    if (!new_node) return -2;
    
    new_node->forward = (kv_skiplist_node_t **)kv_malloc(new_level * sizeof(kv_skiplist_node_t *));
    if (!new_node->forward) {
        kv_free(new_node);
        return -2;
    }
    memset(new_node->forward, 0, new_level * sizeof(kv_skiplist_node_t *));

    // 【重要：深度拷贝 Key 和 Value，与数组引擎保持一致】
    int kcopy_len = strlen(key) + 1;
    new_node->key = (char *)kv_malloc(kcopy_len);
    if (!new_node->key) { /* 省略部分判空代码，原理同上 */ return -2; }
    memcpy(new_node->key, key, kcopy_len);

    new_node->value = (uint8_t *)kv_malloc(value_size);
    if (!new_node->value) { return -2; }
    memcpy(new_node->value, value, value_size);
    new_node->value_len = value_size;

    // 4. 将新节点插入到各层链表中
    for (int i = 0; i < new_level; i++) {
        new_node->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = new_node;
    }

    init->idx++;
    return 0;
}

// 查找数据 (对应 array 的 get)
int kv_skiplist_get(kv_skiplist_t *init, char *key, uint8_t **value, uint32_t *value_size) {
    if (init == NULL || key == NULL) return -1;

    kv_skiplist_node_t *current = init->header;

    // 从顶层向下搜索
    for (int i = init->level - 1; i >= 0; i--) {
        while (current->forward[i] != NULL && strcmp(current->forward[i]->key, key) < 0) {
            current = current->forward[i];
        }
    }

    current = current->forward[0]; // 到达底层
    if (current != NULL && strcmp(current->key, key) == 0) {
        (*value) = current->value;
        (*value_size) = current->value_len;
        return 0; // 找到了
    }
    return 1; // 未找到
}

// 删除数据
int kv_skiplist_del(kv_skiplist_t *init, char *key) {
    if (init == NULL || key == NULL) return -1;

    kv_skiplist_node_t *update[KV_MAX_LEVEL];
    kv_skiplist_node_t *current = init->header;

    // 1. 找前驱
    for (int i = init->level - 1; i >= 0; i--) {
        while (current->forward[i] != NULL && strcmp(current->forward[i]->key, key) < 0) {
            current = current->forward[i];
        }
        update[i] = current;
    }

    current = current->forward[0];

    // 2. 如果找到了，断开链接
    if (current != NULL && strcmp(current->key, key) == 0) {
        for (int i = 0; i < init->level; i++) {
            if (update[i]->forward[i] != current) break; // 这个节点没有延伸到更高层
            update[i]->forward[i] = current->forward[i];
        }

        // 3. 释放节点内存
        kv_free(current->key);
        kv_free(current->value);
        kv_free(current->forward);
        kv_free(current);

        // 4. 如果最高层空了，降低跳表层数
        while (init->level > 1 && init->header->forward[init->level - 1] == NULL) {
            init->level--;
        }

        init->idx--;
        return 0;
    }
    return 1; // 未找到
}

// 修改数据
int kv_skiplist_mod(kv_skiplist_t *init, char *key, uint8_t *value, uint32_t value_size) {
    if (init == NULL || key == NULL || value == NULL || value_size == 0) return -1;

    kv_skiplist_node_t *current = init->header;
    for (int i = init->level - 1; i >= 0; i--) {
        while (current->forward[i] != NULL && strcmp(current->forward[i]->key, key) < 0) {
            current = current->forward[i];
        }
    }
    current = current->forward[0];

    if (current != NULL && strcmp(current->key, key) == 0) {
        // 释放旧值
        kv_free(current->value);
        
        // 分配新值并拷贝
        current->value = (uint8_t *)kv_malloc(value_size);
        if (!current->value) return -2;
        memcpy(current->value, value, value_size);
        current->value_len = value_size;
        return 0;
    }
    return 1; // 未找到
}

// 判断是否存在
int kv_skiplist_exist(kv_skiplist_t *init, char *key) {
    if (init == NULL || key == NULL) return -1;
    uint8_t *value = NULL;
    uint32_t value_size = 0;
    int ret = kv_skiplist_get(init, key, &value, &value_size);
    if (ret == 0) {
        return 1;
    }
    return 0;
}

// 遍历所有数据 (按顺序)
// 跳表的天然优势：最底层（forward[0]）包含了所有有序元素
void kv_skiplist_tra(kv_skiplist_t *init, char **key, uint8_t **value, uint32_t *value_size) {
    if (init == NULL || init->header == NULL) return;
    
    int num = 0;
    kv_skiplist_node_t *current = init->header->forward[0];
    while (current != NULL) {
        key[num] = current->key;
        value[num] = current->value;
        value_size[num] = current->value_len;
        num++;
        current = current->forward[0];
    }
}

// 获取总数
int kv_skiplist_count(kv_skiplist_t *init) {
    if (init == NULL) return 0;
    return init->idx;
}