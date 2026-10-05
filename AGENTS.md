# AGENTS.md — snow_kvstore

本文件面向 AI 编码代理，假设读者对本项目一无所知。所有信息均来自对仓库实际内容的分析。

## 项目概述

snow_kvstore 是一个用 C 语言编写的键值存储（KV Store）服务器，面向 Linux 平台，属于练习/课程项目（GitLab: `gitlab.0voice.com:wangchuanqi/snow_kvstore`，GitHub 镜像: `thesea-cre/KV_STORE`）。

整体为分层架构，自上而下：

1. **网络层**：TCP 服务端，提供三种可互相切换的实现——epoll reactor、io_uring proactor、基于自研协程库 nytco 的服务。用 `include/kvstore.h` 中的宏 `NETWORK_SELECT` 切换（当前启用 `ENABLE_REACTOR`）。
2. **协议层**（`protocol/`）：自定义二进制协议（10 字节固定头 + TLV 负载），负责解包/组包、粘包拆包处理、命令分发与响应编码。对上层暴露的核心入口是 `kv_request()`（`protocol/kvstore.c:691`）。
3. **引擎层**（`engine/`）：实际存储键值对的数据结构，三种实现可切换——数组、链式哈希表、红黑树，用宏 `ENGINE` 切换（当前启用 `ENGINE_ARRAY`）。

辅助功能模块：

- **AOF 持久化**（`AOF/`）：写命令追加到 `aof_db` 文件，启动时 `aof_recover()` 重放恢复。
- **主从同步**（`SYNC/`）：主从复制（PSYNC 全量 + 增量补发 + 增量在线、16KB 环形积压缓冲区、kv 写路径传播钩子）。心跳/超时检测与从节点状态持久化未实现。注意：该目录尚未加入 git，是进行中的开发。
- **内存池**（`mempool/`）：64/128/256 字节三档定长块内存池（单例 `Mempool_Manager`）。
- **日志**（`log/log.c`）：带互斥锁的文件日志，级别 DEBUG/INFO/WARN/ERROR/FATAL。注意：没有对应的 `log.h`，宏定义在 `log.c` 内部，其他模块基本用不到它。
- **nytco 协程库**（`network/nytco/`）：独立子模块，含汇编上下文切换 `ctx_switch.S`、协程调度、红黑树、hook 过的 `co_recv`/`co_send`。

## 技术栈

- 纯 C（gcc，`-D_GNU_SOURCE`），GNU Make 构建，无第三方包管理器。
- 依赖 Linux 专有 API：epoll、io_uring、socket、`arpa/inet.h` 等。**无法在 Windows/MinGW 上编译**，必须在 Linux 环境构建。
- io_uring 依赖以源码形式内置在 `lib/liburing/`（git 子模块），其中已有预编译好的 `lib/liburing/src/liburing.a`。

## 目录结构

```
include/          所有公共头文件（kvstore.h 是核心总入口头）
Main/main.c       服务器主程序（入口 main，./main.exe <ip> <port> <role>）
protocol/         协议层：kvproto.c（协议编解码）、kvstore.c（命令分发/引擎调度）、kv_test.c
engine/           引擎层：kv_array.c、kv_hash.c、kv_rbtree.c
network/          reactor.c（epoll）、uring_tcp_server.c（io_uring）、nytco.c（协程服务端）
network/nytco/    协程库源码（含自己的 Makefile 和小测试）
AOF/              aof.c 持久化 + test.c
SYNC/             sync.c 主从复制 + test.c（未纳入 git，开发中）
mempool/          mempool.c 内存池
log/              log.c 日志
obj/              顶层 Makefile 产出的公共 .o 文件（编译产物）
lib/liburing/     io_uring 库（子模块，已预编译）
testcase.c        TCP 客户端测试/压测程序（根 Makefile 中相关构建目标已被注释掉）
test_kv_case/     空目录
test_network/     空目录
```

## 构建

构建分两级：先在顶层把公共源码编译成 `obj/*.o`，再进入各模块目录链接出自己的可执行文件。

```bash
make                # 顶层：编译所有公共源文件到 obj/*.o
make -C Main        # 链接生成 Main/main.exe（服务器程序）
make -C AOF         # 链接生成 AOF/test_aof
make -C SYNC        # 链接生成 SYNC/test_sync
```

各模块 Makefile 都提供 `make run`（编译并运行）和 `make clean`（只清理本模块产物，不动 `obj/`）。

**已知的构建问题（修改构建脚本时注意）：**

- 顶层 `Makefile` 的 `CFLAGS`/`LDFLAGS` 硬编码了旧机器的绝对路径 `/home/snow/share/KV_STORE/...`；`protocol/Makefile` 同样硬编码了该路径（属于旧版独立构建方式，未使用 `obj/` 机制）。
- `Main/Makefile`、`AOF/Makefile`、`SYNC/Makefile` 中的 `-L../liburing/src` 与实际库位置不符（实际在 `lib/liburing/src`，且 `liburing.a` 已预编译存在）。
- 当前 checkout 位于 Windows（`V:\KV_STORE`），仓库里的 `Main/main.exe` 实际是 Linux ELF 可执行文件（虽叫 .exe）。本机只有 MinGW gcc 且没有 make，无法在此构建或运行，构建需到 Linux 环境进行。

## 运行

```bash
./main.exe 192.168.72.129 2000 master    # 主节点（再监听 SLAVE_PORT=8000 等从节点接入）
./main.exe 192.168.72.128 8000 slave     # 从节点（主动连主节点的 SLAVE_PORT）
./main.exe 127.0.0.1 8888 standalone     # 单机（role 缺省解析为 standalone）
# 参数缺一或为 4 个以上会直接 return -1
```

启动流程（`Main/main.c`）：内存池单例 → `aof_init("aof_db")` → 解析命令行（ip/port/role，写入全局 `s_or_c` 和 `ip`）→ `init_kvengine()` → `aof_recover()` 重放 → 按角色初始化复制模块（master 角色 `master_init(&mgr)`，slave 角色 `slave_init(&slave)`，master_fd 待 reactor connect 后回填）→ `reactor_entry(ip, port)`。

## 主流程梳理

### 1. 启动（`reactor_entry`，network/reactor.c）

1. 监听 port~port+19 共 `MAX_PORT`(20) 个客户端端口（压测设计，有意保留）；
2. 按 `s_or_c` 分流：**0=主** 再监听 `SLAVE_PORT`(8000, `LISTEN_SLAVE`)；**1=从** `connect_server()` 主动连主节点 8000，`slave_init` + 回填 `slave.master_fd`；**2=单机** 什么都不加。

### 2. 客户端命令（收 → 处理 → 回）

`accept_cb` 注册 `CLIENTFD` → `recv_cb` 收包并拼包（栈缓冲区 + `connection[fd].rbuffer` 持久缓冲，带溢出检查）→ `kv_request()` 循环解包，按 `s_or_c` 分派：

- **主** → `kv_master_protocol`：写命令(SET/MOD/DEL) `aof_append` 落AOF → `kv_cmd_execute` 写引擎 → `master_append_cmd` 进积压区推进 `master_offset` → `master_notify_slaves` 唤醒复制；读命令(GET/EXIST)直接执行回包；
- **从** → `kv_slave_protocol`：对客户端只读（写命令仅回响应不执行）；
- **单机** → `kv_protocol`：读写都执行，写落AOF。

响应由 `kv_response` 编码进 `wbuffer` → set EPOLLOUT → `send_cb` 回包（rbuffer 有残留则继续 `kv_request`）。

### 3. AOF（AOF/aof.c）

- 写：`aof_append`（O_APPEND + 互斥锁），触发点 = 各角色执行写命令处（kv_master_protocol / kv_protocol）；
- 恢复：`aof_recover()` 逐包 `kv_header_decode` → `kv_payload_decode` → `kv_cmd_execute` 重放进引擎。触发点：节点启动时、从节点全量同步完成后（rename 临时文件后）；
- 从节点执行复制流（`kv_incremental` → `kv_protocol`）也会写自己的 `aof_db`，即复制结果在从节点同样持久化。

### 4. 主从同步（SYNC/sync.c + network/reactor.c）

**握手与决策**：从节点连上后 EPOLLOUT 就绪 → `send_server_cb` → `slave_send_sync` 组 **PSYNC 20B**（cmd + slave_id + slave_offset + 总长度）→ 转 EPOLLIN。主节点 `accept_slave_cb` 注册 `SERVER_SLAVE` → `recv_from_slave_cb` 拼包 → `master_handle_sync` 按 slave_id + offset 决策：新 id → 建 Slaveconn 置 **1 全量**；id 认识且 offset∈积压区 [start,end] → **2 增量补发**（重连会刷新 fd）；offset 被淘汰 → **1 降级全量**。随后按状态 arm EPOLLOUT。

**全量**：`send_to_slave_cb` → `master_full_sync`：`full_send` 状态机 0→**FULLRESYNC 24B**（cmd+runid+full_end(AOF文件大小)+master_offset，slave_offset 对齐 master_offset）→ 1→`pread` 分块发 AOF 文件 → 2→**SYNC_END 4B** 置 status=3。从节点 `recv_server_cb` → `slave_process`：收 FULLRESYNC 开 `aof_db_tmp` → 按 full_end 写临时文件 → 收 SYNC_END 校验大小 → `rename` 原子覆盖 `aof_db` → `aof_recover()` 加载 → `slave_offset=master_offset` → status=3。

**增量补发**（重连且偏移未被淘汰）：`master_incremental_sync`：0→**CONTINUE 12B**（cmd + 补发截止偏移 master_offset）→ 1→从积压区 `[slave_offset, master_offset)` 读块发 → 2→**SYNC_END** 置 status=3。从节点 status=2 执行 `kv_incremental` 并累计偏移，追平 master_offset 转 3；迟到的收尾 SYNC_END 在 status=3 被识别后直接吃掉。

**增量在线**（日常复制）：触发源 = `kv_master_protocol` 写分支的钩子（`master_append_cmd` + `master_notify_slaves` 给所有 status 2/3 的 slaveconn arm EPOLLOUT）→ `send_to_slave_cb` → `master_online_sync` 从积压区 `[slave_offset, master_offset)` 取数据发送；追平返回 0 → EPOLLIN 睡眠等下次唤醒。从节点 `recv_server_cb` → `slave_process` status=3 → `kv_incremental` 执行命令。

**状态编号**：`Slaveconn.status` 0-空闲 1-全量同步中 2-增量补发 3-增量在线 4-离线；`Slave.status` 0-空闲/过渡 1-全量接收中 2-增量补发接收中 3-增量在线接收中。

**已知缺口**：
- 心跳/超时未实现：积压区只有 16KB，slave 偏移被淘汰后 `master_online_sync` 返 0 睡觉，只能靠重连触发重同步；`master_handle_sync` 的 default 分支收到心跳类包不消费数据，实现心跳时必须处理；
- 从节点未持久化 slave_id/slave_offset（`slave_init` 每次 rand 新 id、偏移归 0），进程重启必走全量，增量补发分支在真实部署中触发不了；
- `connect_server` 的目标 IP 仍硬编码 `192.168.72.129`（network/reactor.c），监听口已用全局 `ip`、connect 未跟进；
- 调试输出全开：`INFO` 在 kvstore.h 中是 `printf`，各回调还有 hex_dump，压测前需收敛。

## 测试

没有统一的测试框架（无 CI），采用每个模块自带测试程序 + assert/printf 断言的风格：

| 测试程序 | 源码 | 说明 |
|---|---|---|
| `protocol/kv_test` | `protocol/kv_test.c` | 协议头/TLV 编解码与命令处理的单元测试（assert 风格） |
| `AOF/test_aof` | `AOF/test.c` | AOF 追加与恢复测试，`make -C AOF run` |
| `SYNC/test_sync` | `SYNC/test.c` | 主从复制全链路单测：socketpair 模拟连接，覆盖全量（含 aof_recover 加载校验）→ 增量补发 → 增量在线，命令真实进引擎（kv_get 断言），`make -C SYNC run` |
| `mempool/test_mempool` | 源码已不在仓库 | 仅存在二进制 |
| `network/nytco/test_ctx`、`test_rbtree` | `test_ctx.c`、`test_rbtree.c` | 协程上下文切换、红黑树测试，`make -C network/nytco` |
| `log/log_test` | 源码已不在仓库 | 仅存在二进制 |
| 根目录 `testcase` | `testcase.c` | TCP 客户端测试用例/简易压测（连上服务端发命令并比对响应） |

协议命令：二进制协议头（magic `0x4B56`、version、msg_type、opcode、length，共 10 字节）+ TLV 负载（tag 区分 KEY/VALUE/RES）。支持 SET/GET/DEL/MOD/EXIST，单包最多 10 个命令（`MAX_CMD`），网络层通过循环解包处理粘包/拆包。

## 代码约定

- 注释主要使用中文，git 提交信息也是中文，请保持这一习惯。
- 头文件位于 `include/`，源文件按模块分目录；`include/kvstore.h` 是贯穿全项目的核心头文件（引擎选择宏、网络选择宏、全局变量 extern 声明、命令枚举都集中在这里）。
- 大量使用全局变量传状态：`global_array` / `global_hash` / `global_rbtree`（引擎实例，定义在 protocol/kvstore.c）、`mempool`（内存池单例）、`s_or_c`（节点角色，定义在 Main/main.c 和 SYNC/test.c 各一份）、`ip`（从节点连接目标）、`mgr`（MasterMgr）与 `slave`（Slave，均在 SYNC/sync.c 定义，网络层直接使用）。修改时注意链接时的定义唯一性。
- `s_or_c`：0 = 主节点（master），1 = 从节点（slave），其他值 = 单机模式。`kv_request()` 据此分派到 `kv_master_protocol` / `kv_slave_protocol` / `kv_protocol`。
- 复制唤醒职责划分：状态管理与同步函数在 SYNC/sync.c，按状态唤醒 slaveconn 的 `master_notify_slaves` 在 network/reactor.c（它拥有 `set_event`），由 kv 写路径（kv_master_protocol）调用。
- 内存分配统一走 `kv_malloc()` / `kv_free()` 包装（`protocol/kvstore.c` 开头），目前直接转发到 malloc/free，预留了切换到内存池的开关。
- `INFO(...)` 宏：kvstore.h 中定义为 `printf`（当前调试输出全开），server.h 中为空实现；谁先被包含谁生效，sync.c 利用这一点在包含 kvstore.h 前置为 printf。
- 服务器单线程事件循环模型；内存池和 AOF、日志内部用 `pthread_mutex` 保护。

## 安全注意事项

- `AOF/aof_db`、`Main/aof_db` 是运行时生成的持久化数据文件（且已被 git 跟踪），`log/logfile` 是运行日志；处理真实数据时注意其中可能包含敏感内容。
- 网络输入不可信：协议解析基于固定 `BUFFER_SIZE`（1024）的线性缓冲区，做协议改动时注意边界检查，避免缓冲区溢出。
- 仓库 `.gitignore` 已排除 `*.o`/`*.a`/`*.so` 及常见可执行产物。

## 部署

无 CI/CD、无容器化、无自动部署流程。手动在 Linux 上 make 构建、手动运行即可。
