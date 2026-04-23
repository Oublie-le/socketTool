# socketTool

一个用于在多台 Linux 设备上测试 TCP / UDP / WebSocket 等网络协议连通性的命令行工具集。

采用 **BusyBox 风格** 的多功能单体二进制架构：所有子命令编译进同一个可执行文件 `socketTool`，既可以通过 `socketTool <applet>` 调用，也可以通过 applet 同名软链接直接调用，便于在嵌入式 / 资源受限的 Linux 设备上部署。

> 项目语言：C (C99 / POSIX)
> 依赖：仅 `pthread`，**零外部依赖**（WebSocket 内置 SHA-1 + Base64）
> 架构参考：BusyBox

---

## 特性

- **多协议支持**：TCP、UDP、WebSocket（RFC 6455，文本帧）
- **双角色**：每个协议都可作为 **客户端** 或 **服务端** 运行
- **批量测试**：`btest` 多线程并发对多个 `host:port[:proto]` 检测
- **批量 ping**：`bping` 支持 TCP-ping（无需 root）与 ICMP 模式
- **BusyBox 风格**：单一二进制 + 多 applet，符号链接即可调用对应子命令
- **美观的 CLI**：彩色输出、表格化结果、`NO_COLOR` 自动适配非 TTY

---

## 目录结构

```
socketTool/
├── Makefile
├── README.md
├── src/
│   ├── main.c        # 入口与 applet 分发
│   ├── applet.[ch]   # applet 注册表 / dispatch
│   ├── applets.c
│   ├── ui.[ch]       # 彩色 / 表格 / 状态行
│   ├── util.[ch]     # 网络辅助、超时、信号、行解析
│   ├── tcp.c         # tcp-client / tcp-server
│   ├── udp.c         # udp-client / udp-server
│   ├── ws.c          # ws-client  / ws-server (RFC 6455)
│   ├── bping.c       # 批量 ping
│   └── btest.c       # 批量协议连通性测试
└── examples/
    ├── hosts.txt     # bping 主机列表示例
    └── targets.txt   # btest 目标列表示例
```

---

## 构建

```bash
make                  # 生成 ./socketTool
make links            # 在当前目录生成所有 applet 软链接
make install PREFIX=/usr/local
make clean
```

构建产物为单一可执行文件 `socketTool`，并在安装/`make links` 时自动创建以下软链接：

```
tcp-client  tcp-server  udp-client  udp-server
ws-client   ws-server   bping       btest
```

---

## 使用方式

与 BusyBox 一致，支持两种调用方式：

```bash
# 方式一：通过主程序 + 子命令
socketTool <applet> [options]

# 方式二：通过软链接的 applet 名直接调用
tcp-client -H 192.168.1.10 -p 8080
```

直接运行 `socketTool` 不带参数会显示 applet 列表；运行 `socketTool <applet> -h` 查看子命令帮助。

### Applet 一览

| Applet        | 说明                                   |
| ------------- | -------------------------------------- |
| `tcp-client`  | TCP 客户端：连接 / 发送 / 交互 / 计数  |
| `tcp-server`  | TCP 服务端：监听 / echo \| discard     |
| `udp-client`  | UDP 客户端：多次发送 + RTT / 丢包统计  |
| `udp-server`  | UDP 服务端：echo \| discard            |
| `ws-client`   | WebSocket 客户端（ws://）              |
| `ws-server`   | WebSocket 服务端                       |
| `bping`       | 批量 ping（TCP / ICMP）                |
| `btest`       | 批量协议连通性测试（TCP / UDP / WS）   |

### 示例

#### TCP

```bash
# 服务端
socketTool tcp-server -p 9000

# 客户端发送一条消息
socketTool tcp-client -H 192.168.1.10 -p 9000 -m "hello"

# 客户端进入交互模式 (stdin <-> socket)
socketTool tcp-client -H 192.168.1.10 -p 9000 -i
```

#### UDP

```bash
# 服务端
socketTool udp-server -p 9001

# 客户端发送 5 次，每次间隔 200ms，等待回包 500ms
socketTool udp-client -H 192.168.1.10 -p 9001 -m ping -c 5 -i 200 -w 500
```

#### WebSocket

```bash
socketTool ws-server -p 9002
socketTool ws-client -H 192.168.1.10 -p 9002 -m '{"hello":"ws"}'
```

#### 批量 ping

```bash
# TCP 模式（默认 80 端口，无需 root）
socketTool bping -f examples/hosts.txt

# 指定端口
socketTool bping -p 22 -t 800 -j 32 host1 host2 host3

# ICMP 模式（依赖系统 ping）
socketTool bping -m icmp -f examples/hosts.txt
```

#### 批量协议测试

```bash
# 命令行直接传 target
socketTool btest 192.168.1.10:80:tcp 192.168.1.10:53:udp 192.168.1.10:8080:ws

# 文件输入
socketTool btest -f examples/targets.txt -P tcp -t 1000 -j 32
```

---

## 输出与配色

- 默认在 TTY 下输出彩色与 Unicode 边框
- 非 TTY（管道、重定向）会自动关闭颜色
- 显式禁用：`NO_COLOR=1 socketTool ...`

---

## 退出码

| 码 | 含义                     |
| -- | ------------------------ |
| 0  | 成功                     |
| 1  | 参数错误 / 用法错误      |
| 2  | 资源初始化失败（连接、监听 ...） |
| 3  | 通信错误                 |
| 4  | 批量任务存在失败项       |

---

## License

TBD
