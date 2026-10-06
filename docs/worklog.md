---
Task ID: 2-b
Agent: P0-b subagent
Task: capture-proxy 抓包代理

Work Log:
- 新增 tools/capture-proxy/{main.cpp,CMakeLists.txt}；根 CMakeLists 启用 add_subdirectory(tools/capture-proxy)（仅去注释，未动其他行）
- 实现：CLI(--listen/--upstream/--dir/--parse，默认 25566→127.0.0.1:25565 captures)，单 io_context 单线程 asio 异步；每连接一个 Session（shared_ptr registry，结束自动 prune）；双向独立 pump：async_read_some(64K chunk)→先落盘 .bin 再 async_write 原样转发（串行 read→write 保证零丢失）；一侧 EOF/错误→对侧 socket shutdown(write) 关闭传播，双方向都结束才 finish（关 socket、fflush/fclose、统计日志）
- --parse：每方向独立流式缓冲 vector<uint8_t>，攒够 VarInt+载荷才切帧（绝不假设一次 read=一包）；VarInt 按 LEB128 7bit/字节小端解析，>5 字节带续位或载荷>2^21-1 判协议错误→spdlog warn 并退化为纯转发；帧行格式 `#NNN len=L t=+S.SSSs` + 载荷前 64 字节 hexdump；时戳 steady_clock 秒偏移毫秒精度
- SIGINT/SIGTERM→asio signal_set→close acceptor+全 Session finish（flush 落盘）后自然排空退出
- 修复：Direction 聚合初始化触发 -Wmissing-field-initializers（-Wextra）→改为显式构造函数+optional 成员；2 轮修完零警告
- 测试任务文本中 python payload `bytes([...,'m','c','t','e','s','t'])` 在 Python3 会 TypeError，改用等价 bytes 字面量；字节数实为 4+6+200=210（任务文本 209 系笔误）
- 附加验证：capture .bin 与 client 发送字节逐字节一致；协议错误退化路径（合法帧后跟 7×0xFF）→ 正确记 1 帧后 warn 并继续纯转发

Stage Summary:
- 编译：cmake+ninja Release 零警告零错误（-Wall -Wextra -Wpedantic）
- 测试A：ECHO-MATCH，captures/session001_{c2s,s2c}.bin 各 210 B 与发送字节完全一致
- 测试B：--parse 下 c2s.log 3 行 `len=4/200/5`（hexdump 正确：01 02 03 04 / 58×64 / 68 65 6c 6c 6f），s2c.log 同 3 行，总字节 213
- commit: 1923f22
