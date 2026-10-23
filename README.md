# Grok Bot ESP32 SDK

Grok Bot 桌面协议的非官方 C++ 移植，适用于 ESP-IDF 5.5.2 及以上。协议源自 [grok-bot-sdk](https://github.com/RongleCat/grok-bot-sdk) 0.70.2。MIT 许可。

把此目录复制到 ESP-IDF 工程 `components/grok_bot`，并在调用方声明 `REQUIRES grok_bot`。

```cpp
#include "grok_bot.hpp"

grok::EspTransport transport;
grok::Credentials credentials;
// 从自己的安全配置存储读取，不要在源码中硬编码。
credentials.access_token = load_access_token();
grok::Client client(transport, credentials);
std::vector<grok::Bot> bots;
bool truncated = false;
auto result = client.list_bots(bots, truncated);
```

组件提供浏览器 PKCE 登录、账号读取、Bot 列表、原始发送协议、Connect JSON 分段解码、作用域隔离的 uint64 游标、回复事件及头像参数。头像参数包含全部 25 个形状名、11 组颜色，并保持 JavaScript UTF-16 哈希一致；图像渲染不在此组件中。

登录：`create_login()` 返回 `login_url` 给用户浏览器。由应用保留 `LoginFlow`，周期调用 `poll_login()`；`AUTH_PENDING` 可继续轮询，其它失败应显示错误。授权返回的凭据不写日志，保存在设备自身配置存储中。

收取回复：先 `tail(cursor)`，建立 `watch_request(cursor)`，将 transport 的字节逐次交给 `ConnectDecoder::feed()`，再把每个 JSON frame 交给 `apply_watch_frame()`。确认 `connected` 后发送，避免丢失快速回复。游标应随 Bot 和 session 一起保存，重连从最新游标恢复。`RESYNC_REQUIRED` 需要重新同步。

`send_message()` 要求调用者生成稳定 `message_id` 并传入当前毫秒时间。`SEND_UNCONFIRMED` 表示投递结果不确定，**不能自动重新发一条**。成功只表示接收投递，不表示 Bot 已执行或已回复。

`EspTransport` 使用 ESP-IDF 证书包验证 HTTPS；启用 `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`，联网后同步系统时间。HTTP 重定向不跟随。SDK 不自动刷新或撤销账号 token；也不会唤醒云电脑、上传本地附件或执行工具。

SDK 的普通 HTTP 响应默认限制 512KB，Connect 单帧默认限制 256KB。`entry.unavailable` 表示服务端未内联正文，不能把它当空回答。流重连、超时、任务取消和 UI 应由应用层管理。

已在真实 M5Stack StackChan（ESP32-S3、8MB PSRAM）验证账号读取、41 个 Bot 列表，以及先建立 Connect 回复流再发送消息并收到云端 Bot 回答。主机测试覆盖头像向量、投递结果、分段帧、uint64 游标、重放与会话隔离。

网络任务应预留足够的 FreeRTOS 栈空间；本项目实机任务使用 16KB。传输读缓冲在堆中分配，避免嵌套请求占用大块任务栈。回调应尽量短，耗时操作可交给应用工作队列。

独立编译检查：

```bash
. "$IDF_PATH/export.sh"
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
idf.py -C examples/minimal -B "$PWD/build-esp32s3" set-target esp32s3 build
```

`examples/minimal` 仅检查编译与链接，不包含 Wi-Fi 或账号凭据，不能当作机器人应用烧录。
