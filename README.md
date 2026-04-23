# socketTool

一个用于在多台 Linux 设备上测试 TCP / UDP / WebSocket 等网络协议连通性的命令行工具集。

采用 **BusyBox 风格** 的多功能单体二进制架构：所有子命令编译进同一个可执行文件 `socketTool`，既可以通过 `socketTool <applet>` 调用，也可以通过 applet 同名软链接直接调用，便于在嵌入式 / 资源受限的 Linux 设备上部署。

> 项目语言：C (C99 / POSIX)
> 依赖：仅 `pthread`，**零外部依赖**（WebSocket 内置 SHA-1 + Base64）
> 架构参考：BusyBox

---

## 特性

- **多协议**：TCP / UDP / WebSocket（RFC 6455 文本帧）
- **双角色**：每个协议都可作为 **客户端** 或 **服务端** 运行
- **批量 ping**：支持 **单 IP / 范围 (`192.168.1.10-50`) / 完整范围 (`a-b`) / CIDR (`10.0.0.0/24`) / 主机列表文件**，TCP-ping 无需 root，亦可 ICMP
- **批量协议测试**：`btest` 多线程并发对多个 `host:port[:proto]` 检测
- **中英文双语**：编译期 `make LANG=zh|en`，运行期 `--lang zh|en` 或 `ST_LANG` 环境变量
- **美观 CLI**：Unicode 图标 (✔ ✘ ⚠ ℹ ◀ ▶ ⏱ 🚀 🌐)、亮色调色板、CJK 宽度对齐
- **Tab 补全**：随安装一同部署 bash completion，支持子命令、选项、模式枚举、文件名
- **完整测试套件**：`make test` 一键回归 (range 单测 + 各 applet 端到端)

---

## 目录结构 (按层组织)

```
socketTool/
├── Makefile
├── README.md
├── src/
│   ├── core/        # 入口与 applet 分发
│   │   ├── main.c
│   │   ├── applet.h
│   │   └── applets.c
│   ├── ui/          # 终端 UI：颜色、图标、表格、进度条
│   │   ├── ui.h
│   │   └── ui.c
│   ├── i18n/        # 多语言字符串表 (中/英)
│   │   ├── i18n.h
│   │   └── i18n.c
│   ├── net/         # 网络/socket 通用辅助、IP 范围/CIDR 展开
│   │   ├── net.h
│   │   └── net.c
│   └── applets/     # 各 applet 实现
│       ├── tcp.c    udp.c    ws.c    bping.c    btest.c
├── tests/           # 测试模块
│   ├── lib.sh
│   ├── run_all.sh
│   ├── unit_range.c
│   └── test_*.sh
├── scripts/
│   └── socketTool.bash-completion
└── examples/
    ├── hosts.txt
    └── targets.txt
```

---

## 构建

```bash
make                       # 默认英文 UI
make LANG=zh               # 默认中文 UI
make links                 # 生成所有 applet 软链接
make install PREFIX=/usr/local
make test                  # 跑全套测试
make clean
```

安装时会同时把 bash completion 脚本部署到 `$PREFIX/share/bash-completion/completions/socketTool`。

---

## 使用

```bash
# 主程序 + 子命令
socketTool <applet> [options]

# 软链接直接调用
tcp-client -H 192.168.1.10 -p 8080
```

直接运行 `socketTool` 不带参数会显示带颜色的 applet 列表；`socketTool <applet> -h` 查看子命令帮助。

### 全局选项 (在 applet 之前/之后均可)

| 选项                | 说明                            |
| ------------------- | ------------------------------- |
| `--lang en\|zh`     | 切换输出语言                    |
| `--no-color`        | 关闭颜色 (亦可 `NO_COLOR=1`)    |
| `-V, --version`     | 显示版本                        |

### Applet 一览

| Applet        | 说明                                                |
| ------------- | --------------------------------------------------- |
| `tcp-client`  | TCP 客户端：连接 / 发送 / 交互 / 计数               |
| `tcp-server`  | TCP 服务端：监听 / echo \| discard                  |
| `udp-client`  | UDP 客户端：多次发送 + RTT / 丢包                   |
| `udp-server`  | UDP 服务端：echo \| discard                         |
| `ws-client`   | WebSocket 客户端 (ws://)                            |
| `ws-server`   | WebSocket 服务端                                    |
| `bping`       | 批量 ping（**单点 / 范围 / CIDR / 文件**，TCP/ICMP）|
| `btest`       | 批量协议连通性测试（TCP / UDP / WS）                |

### 示例

#### TCP

```bash
socketTool tcp-server -p 9000
socketTool tcp-client -H 192.168.1.10 -p 9000 -m "hello"
socketTool tcp-client -H 192.168.1.10 -p 9000 -i        # 交互
```

#### UDP

```bash
socketTool udp-server -p 9001
socketTool udp-client -H 192.168.1.10 -p 9001 -m ping -c 5 -i 200 -w 500
```

#### WebSocket

```bash
socketTool ws-server -p 9002
socketTool ws-client -H 192.168.1.10 -p 9002 -m '{"hello":"ws"}'
```

#### 批量 ping (range / CIDR)

```bash
# 单点
socketTool bping 192.168.1.10

# 末段范围
socketTool bping 192.168.1.10-50

# 完整范围
socketTool bping 192.168.1.10-192.168.2.20

# CIDR
socketTool bping 10.0.0.0/24

# 混合 + 文件
socketTool bping -p 22 -j 64 -t 800 \
    -f examples/hosts.txt 192.168.1.0/29 router.local

# ICMP 模式 (依赖系统 ping)
socketTool bping -m icmp 8.8.8.8 1.1.1.1
```

#### 批量协议测试

```bash
socketTool btest 192.168.1.10:80:tcp 192.168.1.10:53:udp 192.168.1.10:8080:ws
socketTool btest -f examples/targets.txt -P tcp -j 32
```

---

## 中英文切换

```bash
# 编译期默认
make LANG=zh

# 运行期切换 (优先于编译默认)
socketTool --lang zh bping 10.0.0.0/30
ST_LANG=zh tcp-client -H 127.0.0.1 -p 9000
```

---

## Tab 补全

```bash
# 临时启用
. scripts/socketTool.bash-completion

# 安装后会自动 source（如果系统启用了 bash-completion）
make install
```

支持的补全：
- `socketTool <Tab>` → 列出所有 applet
- `tcp-client -<Tab>` → 列出选项
- `bping -m <Tab>` → `tcp icmp`
- `btest -P <Tab>` → `tcp udp ws`
- `--lang <Tab>` → `en zh`
- `-H <Tab>` → `/etc/hosts` + `localhost / 127.0.0.1`
- `-f <Tab>` → 文件名

---

## 测试

```bash
make test
```

输出示例：

```
━━━ unit tests ━━━
  ✔ single host
  ✔ last-octet range 1-3
  ...
━━━ test_tcp.sh ━━━
  ✔ tcp client connects & echoes
  ...
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  ALL TESTS PASSED
```

测试覆盖：
- `tests/unit_range.c` ：`host_range_expand` 单元测试 (单点/范围/CIDR/边界)
- `tests/test_dispatch.sh` ：主程序与 i18n 切换
- `tests/test_tcp.sh` `test_udp.sh` `test_ws.sh` ：协议端到端
- `tests/test_bping.sh` `test_btest.sh` ：批量 applet

---

## 退出码

| 码 | 含义                     |
| -- | ------------------------ |
| 0  | 成功                     |
| 1  | 参数错误 / 用法错误      |
| 2  | 资源初始化失败           |
| 3  | 通信错误                 |
| 4  | 批量任务存在失败项       |

---

## License

TBD
