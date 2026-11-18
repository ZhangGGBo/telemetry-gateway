# Telemetry Gateway：设备数据采集与监控服务端

这是模拟工业物联网后端开发场景的 C++17 工程：设备通过 TCP 上报，异步服务端校验并持久化数据，HTTP API 提供设备状态和历史记录，内置网页用于现场演示。个人作品（AI 辅助开发），用于展示 C++ 网络编程、数据库持久化与工程测试。

## Windows 快速启动

需要 Windows、PowerShell、Git、tar 和 MinGW-w64 GCC。脚本优先使用 `C:/msys64/ucrt64/bin`，也支持 PATH 中的 gcc/g++。

```powershell
git clone https://github.com/ZhangGGBo/telemetry-gateway.git
cd telemetry-gateway
./scripts/setup.ps1
./scripts/build.ps1
./scripts/start-server.ps1
```

另开终端，启动设备：

```powershell
./scripts/start-device.ps1 -Device pump-001
```

再开一个终端，用相同脚本传入 `-Device pump-002` 模拟另一台设备。浏览器打开 http://127.0.0.1:8080 ，点击设备查看历史读数。模拟器每秒发送一条随机数据，部分读数会触发告警。停止设备后约 10 秒，页面显示离线。Ctrl+C 停止运行；重启服务端后可查询原有数据。

重新构建：

```powershell
# 在仓库根目录执行
# 新环境首次执行（下载量约 140MB，需要网络和 tar）
./scripts/setup.ps1
./scripts/build.ps1
./scripts/integration.ps1 -SkipBuild
```

构建脚本为本机 MSYS2 UCRT64 工具链设置 PATH；换电脑需安装 MinGW-w64 GCC（或调整脚本路径）。可执行文件需要该工具链运行库，分发时需要一并提供运行库或制作安装包。setup 使用固定版本 Boost 1.85.0 和 SQLite 3.46.0；生产部署前应评估并升级依赖。

## 实际业务流程

1. 设备模拟器建立 TCP 连接，发送换行分隔的 JSON。
2. 服务端按帧读取，校验设备编号、类型和数值范围。
3. 单独的数据库线程执行 SQLite 参数化 INSERT，落盘成功后回复 ACK。
4. 页面每两秒调用 API，显示在线状态、阈值告警、最近 50 条数据。
5. 模拟器遇到连接错误后间隔两秒重连，未确认的数据使用相同序号重试。

## 技术结构

```text
设备模拟器 -- TCP/JSON --> Boost.Asio io_context
                              | 校验、超时、异步读写
                              v
                         数据库工作队列（单线程，最多256个排队/执行任务）
                              | SQLite WAL + 参数化语句
                              v
                          telemetry.db
                              ^
浏览器 -- HTTP --> Boost.Beast | 设备列表 / 历史查询
```

网络由单个事件循环处理，并发连接不需要每台设备分配一个线程。数据库工作线程避免同步磁盘操作阻塞网络事件循环。每条连接逐帧等待写入确认，数据库队列达到上限时拒绝继续接收任务；HTTP 返回 503，设备收到失败响应后重试。

- `include/core.hpp`：配置、数据结构、存储与运行时接口。
- `src/core.cpp`：配置解析、协议校验、SQLite RAII、查询。
- `src/network.cpp`：TCP 会话、超时、异步 HTTP 和内置监控页面。
- `src/server.cpp`：启动、信号处理、退出时等待已提交数据库任务。
- `src/simulator.cpp`：模拟上报、ACK 和重连。
- `tests/core_tests.cpp`：协议、数据库、SQL 注入输入、重启持久化测试。
- `scripts/integration.ps1`：真实 TCP/HTTP 端到端验证。

## TCP 协议

UTF-8 JSON，每条消息以 LF 换行结尾；单帧含缓冲数据上限 4096 字节。连续无完整消息超过 timeout_seconds 会断开。

```json
{"device":"pump-001","temperature":85.5,"pressure":3.2,"sequence":1}
```

设备编号为 1～64 个 ASCII 字母、数字、短横线或下划线；温度范围 -100～1000 °C，压力范围 0～1000 bar，sequence 为非负有符号 64 位整数。入库时间使用服务端 Unix 秒时间戳。temperature >= temperature_limit 或 pressure >= pressure_limit 触发告警。

回复也以换行结尾：

```json
{"ok":true,"message":"stored"}
```

非法消息返回 `ok:false`；超长消息关闭连接。ACK 代表数据库写入已成功。当前为至少一次重试语义，ACK 丢失后重试可能产生重复记录，sequence 仅作追踪字段，尚未实现去重。

## HTTP 接口

| 方法与路径 | 返回 |
|---|---|
| GET / | 内置监控页 |
| GET /health | 进程存活状态，不代表数据库健康 |
| GET /api/devices | 每台设备最新读数，最多1000台，含 online/alarm |
| GET /api/history?device=pump-001&limit=50 | 倒序历史，limit为1～500 |

online 表示最近一次持久化上报距今未超过超时阈值；它不是 TCP 连接是否仍然存在。当前用周期上报兼任心跳，没有独立心跳消息。告警是读数上的标记，未实现短信或告警事件流。SQLite 文件创建在配置指定路径，父目录需预先创建。

## CMake 与 Linux

支持 CMake 3.16+。可使用 setup 下载的本地依赖；若无 third_party，则使用系统安装的 Boost 1.75+ 和 SQLite 开发包。

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake -j2
ctest --test-dir build-cmake --output-on-failure
./build-cmake/telemetry_server config/server.json
```

提供 Dockerfile，可用 `docker build -t telemetry-gateway .` 构建；运行时仅绑定宿主机本地端口：

```sh
docker run --rm -p 127.0.0.1:9000:9000 -p 127.0.0.1:8080:8080 -v telemetry-data:/data telemetry-gateway
```

Docker/Linux 路径尚未在本机实际执行，不能视为已验证交付。

## 工程边界与后续路线

这是可运行的工程 MVP，适合学习企业后端链路、代码讲解和本地部署演示。默认绑定 127.0.0.1，不包含设备认证、TLS、权限管理，不能直接开放到公网。使用单进程、单数据库写线程；未做大规模压测，不宣称支持特定连接数或吞吐。

目前没有设备注册表、连接总数限制、数据保留清理、独立心跳、序号去重或多实例部署。历史数据持续增长；设备状态查询采用每设备最新记录聚合。模拟器采用同步网络调用，在异常网络下退出可能受操作系统超时影响。

下一阶段可围绕实际岗位要求扩展：设备鉴权和TLS；设备注册与会话绑定；幂等上报和断线缓存；告警事件与确认；数据保留与批量写入；Linux 部署和可重复性能压测。

## 依赖来源

- Boost.Asio/Beast： https://www.boost.org/ ，异步模型参考官方示例 https://www.boost.org/latest/libs/beast/doc/html/beast/examples.html 。Boost 源码采用 Boost Software License，下载目录附带许可证。
- SQLite 官方 amalgamation： https://www.sqlite.org/amalgamation.html ，SQLite 为公有领域软件。

## 开发说明

本项目使用 Codex 辅助实现与调试，设备数据均由模拟器生成，不代表企业生产项目或真实设备接入。公开仓库提供源码与本地运行方式，不提供公网设备接入服务。
