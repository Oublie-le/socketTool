# socketTool

一个用于在多台 Linux 设备上测试 TCP / UDP / WebSocket 等网络协议连通性的命令行工具集。

采用类似 BusyBox 的多功能单体（multi-call binary）架构：所有子命令编译进同一个可执行文件，通过 applet 名称或子命令分发，便于在嵌入式 / 资源受限的 Linux 设备上部署。

> 项目语言：C
> 架构参考：BusyBox

---

## 特性

- **多协议支持**：TCP、UDP、WebSocket 等
- **双角色**：每个协议都可作为 **客户端** 或 **服务端** 运行
- **批量测试**：支持对多个目标地址 / 端口并发执行连通性检测
- **批量 ping**：批量对多台设备执行 ICMP / TCP ping
- **BusyBox 风格**：单一二进制 + 多 applet，符号链接即可调用对应子命令
- **美观的 CLI**：彩色输出、表格化结果、进度展示，注重终端可读性

---

## 适用场景

- 多台 Linux 设备之间的网络连通性自检
- 出厂 / 上线前的批量网络测试
- 嵌入式设备的轻量级网络诊断
- 协议层联调（作为对端服务/客户端配合调试）

---

## 目录结构

```
socketTool/
├── src/            # C 源码（BusyBox 风格 applet 框架）
│   └── main.c      # 入口与 applet 分发
└── README.md
```

---

## 构建

```bash
make
```

构建产物为单一可执行文件 `socketTool`，可通过软链接的方式生成各 applet 的快捷调用：

```bash
ln -s socketTool tcp-client
ln -s socketTool tcp-server
ln -s socketTool udp-client
ln -s socketTool udp-server
ln -s socketTool ws-client
ln -s socketTool ws-server
ln -s socketTool bping
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

### 子命令一览

| 子命令        | 说明                          |
| ------------- | ----------------------------- |
| `tcp-client`  | TCP 客户端，连接并收发数据    |
| `tcp-server`  | TCP 服务端，监听并回显        |
| `udp-client`  | UDP 客户端                    |
| `udp-server`  | UDP 服务端                    |
| `ws-client`   | WebSocket 客户端              |
| `ws-server`   | WebSocket 服务端              |
| `bping`       | 批量 ping（支持文件 / 列表）  |
| `btest`       | 批量协议连通性测试            |

### 示例

```bash
# 启动一个 TCP 服务端
socketTool tcp-server -p 9000

# TCP client sends data
socketTool tcp-client -H 192.168.1.10 -p 9000 -m "hello"

# 批量 ping 一个设备列表
socketTool bping -f hosts.txt

# 批量 TCP 端口连通性测试
socketTool btest -f targets.txt -P tcp
```

---

## 开发计划

- [x] 仓库初始化
- [ ] BusyBox 风格 applet 框架
- [ ] TCP 客户端 / 服务端
- [ ] UDP 客户端 / 服务端
- [ ] WebSocket 客户端 / 服务端
- [ ] 批量 ping
- [ ] 批量协议连通性测试
- [ ] 彩色 / 表格化 CLI 输出

---

## License

TBD
