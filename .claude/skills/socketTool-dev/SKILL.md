---
name: socketTool-dev
description: socketTool 项目专属开发规范。当在本仓库内新增/修改 applet、UI、i18n、net、tests 或准备提交时自动使用。包含日志/测试/架构/输出/提交的全部约束。
---

# socketTool 项目开发 Skill

本 skill 是 `socketTool` 仓库专属的开发契约。**任何新增或修改代码、提交、测试时必须遵守。**

---

## 1. 架构约束（目录分层）

源码严格按照 5 层组织，层之间只允许下方依赖上方：

```
src/
├── core/       # 入口、applet 注册/分发。依赖 ui/i18n/net/applets
├── applets/    # 各 applet 实现 (tcp/udp/ws/bping/btest)。依赖 ui/i18n/net
├── ui/         # 颜色/图标/表格/进度。仅依赖 libc
├── i18n/       # 中英文字符串表。仅依赖 libc
└── net/        # socket、ICMP、range 展开、mDNS/NBNS/ARP。依赖 libc + pthread
```

### 新增代码时必须遵守
- **新协议/新命令** → 在 `src/applets/<name>.c`，并在 `src/core/applets.c` 注册
- **新 UI 能力**（颜色、表格、图标）→ 放 `src/ui/ui.c`，禁止在 applet 内写死 ANSI 转义
- **新网络工具函数** → 放 `src/net/`，不要塞进 applet
- **用户可见字符串** → **必须**通过 `T(T_XXX)`；禁止直接写英文/中文字面量（`help` 文本可保留英文模板但描述词 `required`/`default`/`options` 必须走 T()）
- **头文件路径** 统一形如 `#include "net/net.h"`（已启用 `-Isrc`）

### 禁止
- ❌ `applets/` 引用其他 applet 的符号
- ❌ `ui/`、`i18n/`、`net/` 反向依赖 `applets/` 或 `core/`
- ❌ 在 Makefile 外另造构建脚本；所有源文件必须被 `$(wildcard src/*/*.c)` 自然收入

---

## 2. i18n 约束

### 添加新字符串
1. 在 `src/i18n/i18n.h` 的枚举中在 `T_MAX` **之前**追加新键（禁止插入中间位置，破坏 ABI）
2. 在 `src/i18n/i18n.c` 的 **en 表和 zh 表都要填充**；缺一视为 bug
3. 使用时：`ui_ok(T(T_YOUR_KEY), arg1, arg2)` — 格式串与参数必须两种语言一致

### 命名规范
| 前缀         | 用途                                    |
| ------------ | --------------------------------------- |
| `T_`         | 通用标签 (如 `T_TARGET`, `T_TIMEOUT_MS`)|
| `T_E_`       | 错误信息                                |
| `T_S_`       | applet summary（主帮助列表用）          |

### 禁止
- ❌ 将字符串硬编码在 applet/ui 内
- ❌ 两个表数量不一致（会触发静态分析失败）

---

## 3. 输出约束（UI）

### 结构化输出
每个 applet 开头都必须用 `ui_section(icon, T(T_SECTION_TITLE))` 起一个 section，紧跟 `ui_kv()` 展示关键参数：

```c
ui_section(ui_icon_globe(), "TCP client");
ui_kv(T(T_TARGET),     "%s%s:%s%s", UI_BCYAN, host, port, UI_RESET);
ui_kv(T(T_TIMEOUT_MS), "%d ms", timeout);
```

### 状态行规范
| 函数        | 图标 | 颜色       | 用途                   |
| ----------- | ---- | ---------- | ---------------------- |
| `ui_ok()`   | ✔    | UI_BGREEN  | 成功结果               |
| `ui_warn()` | ⚠    | UI_BYELLOW | 可恢复问题（忽略一项） |
| `ui_err()`  | ✘    | UI_BRED    | 致命错误（走 stderr）  |
| `ui_info()` | ℹ    | UI_BCYAN   | 中性提示               |

### 表格
**新增表格列后必须检查宽度对齐**：
- CJK 字符（中文、emoji）显示宽度 = 2，ASCII = 1；`ui_table_header/row` 内部用 `utf8_dwidth()` 计算
- **所有写入单元格的字符串必须提前清洗非打印字节**（NBNS/mDNS 响应可能含控制字符），否则表格线会被终端错位显示
- 新列放不下时优先加宽该列，不要删其他列

### 颜色 / ASCII 回退
- 永远不要裸写 `"\033[..."`；用 `UI_*` 宏，它们会在非 TTY 下自动变空串
- 图标用 `ui_icon_*()` 函数，它们会在非 UTF-8 终端回退到 ASCII `[OK]` / `[X]` 等

---

## 4. 日志/消息约束

- **用户可见消息** → `ui_ok/warn/err/info`，绝不要 `printf("...")` 或 `fprintf(stderr, ...)`
- **调试输出** → 不得进入生产代码。调试期可临时 `fprintf(stderr, ...)`，提交前删除
- **错误消息** → 必须包含上下文：哪个目标、哪个端口、errno（用 `strerror(errno)` 或直接传）
- **敏感内容** → 不要输出密码、token、完整文件路径等可能泄密的内容
- **长输出** → 批量任务用 `ui_progress(done, total, label)` 更新单行，不要逐条刷屏

禁止模式：
```c
// ❌ 不合格
printf("ok\n");
fprintf(stderr, "failed\n");

// ✅ 合格
ui_ok(T(T_CONNECTED_IN), rtt_ms);
ui_err(T(T_E_CONNECT), strerror(errno));
```

---

## 5. 测试约束

### 新增 applet 必须配套
1. **端到端脚本** `tests/test_<applet>.sh`，最少包含：
   - 启动 server → client 连通 → 拿到预期数据
   - 参数错误退出非零
2. **涉及 net/ 的新函数** → 在 `tests/unit_*.c` 增加单测（模仿 `tests/unit_range.c`）
3. 把新脚本加到 `tests/run_all.sh` 的循环自动会捞（文件名 `test_*.sh` 即可）

### 测试规范
- **端口** 必须用 `free_port` 随机分配，禁止硬编码 20000+ 的端口
- **后台进程** 必须用 `lib.sh` 的 `spawn`（自动注册清理），不要裸 `&`
- **等待 server 就绪** 用 `wait_port`，不要 `sleep` 糊
- **server 在多客户端测试中不要加 `-1`**（一个客户端后就退出会把 race 放大）

### 运行
提交前必须：
```bash
make clean && make test
```
**任何一项 FAIL 禁止提交**。

---

## 6. 提交约束

**每完成一个独立模块/能力就提交一次，禁止把多个无关改动攒到一个 commit。**

### Commit message 格式（一行即可）

```
[修改类型] 简短描述
```

示例：
```
[bugfix] ws-client 不传 -m 时立即断开 -> 默认进入交互模式
[feature] bping 支持 IP 范围与 CIDR 展开
[config] 调整默认 UI 语言为 en
```

### 修改类型
| 标记       | 用途                       |
| ---------- | -------------------------- |
| `[feature]`| 新功能、非缺陷增强         |
| `[bugfix]` | 修 bug                     |
| `[config]` | 配置/编译/CI                |

### 提交前检查清单
- [ ] `make clean && make` 零 error，零 warning
- [ ] `make test` 全绿
- [ ] 新增用户可见字符串已走 i18n（en + zh 同时填充）
- [ ] 新 applet 已在 `core/applets.c` 注册
- [ ] 新选项已更新到 `scripts/socketTool.bash-completion`
- [ ] 如涉及用法变化，README.md + README.zh.md **同步更新**

### 推送
用户明确要求时执行 `git push`，不需要再额外确认。

---

## 7. README 约束（双语）

- **README.md 是英文主文档**，README.zh.md 是中文镜像
- 任一方变更必须同步另一方；条目顺序保持一致
- 英文 README 顶部必须保留指向中文版本的链接：`> 📖 **中文文档：[README.zh.md](README.zh.md)**`
- 中文 README 顶部也要链回英文：`> 📖 **English: [README.md](README.md)**`

---

## 8. 常见反模式（禁止）

| 反模式                                      | 正确做法                                        |
| ------------------------------------------- | ----------------------------------------------- |
| `system("ping ...")`                        | 用 `src/net/icmp.c` 原生 ICMP                   |
| 新 applet 直接 `printf` 输出表格            | 用 `ui_table_header/row/sep`                     |
| 新增字符串只填英文                          | en + zh 同时填，漏一个即 bug                    |
| 依赖 OpenSSL / libcurl                      | 禁止新增外部依赖，只允许 libc + pthread         |
| 在 applet 里直接 `getaddrinfo`              | 走 `net_resolve` / `tcp_connect` 等封装         |
| 提交包含 `.o` 或 `socketTool` 二进制        | `.gitignore` 已覆盖；提交前 `make clean`        |
| 改 i18n 枚举时插在中间                      | 只允许在 `T_MAX` 前追加，保 ABI 稳定            |

---

## 9. 使用本 skill 的工作流

用户在本仓库内提需求时：
1. 先判断需求落到哪一层（applets / ui / i18n / net / tests / docs）
2. 按上述约束落地，**每完成一个最小闭环就单独 commit**
3. 提交前跑 `make clean && make test`
4. README 双语同步
5. 结束时向用户总结：改了哪些文件、commit 列表、是否需要推送
