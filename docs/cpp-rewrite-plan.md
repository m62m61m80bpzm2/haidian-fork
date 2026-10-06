# Minecraft 26.2 → C++ 重写计划

## 0. 基线事实（勿重新考证）

| 项 | 值 |
|---|---|
| 目标版本 | Minecraft Java 26.2 (release, 2026-06-16) |
| 协议版本 | **776** (version.json: protocol_version) |
| 世界版本 | 4903 (world_version) |
| 客户端/服务端 class 版 | v69 = Java 25 |
| 反编译源码 | `/home/z/my-project/mcsrc/`（Vineflower 1.12.0，4830 java / ~53.7万行，官方 Mojang 名，未混淆） |
| server.jar | `/home/z/my-project/download/mc-26.2-server.jar`（59M，bundler 格式，可跑 vanilla 对照） |
| 内核 jar | `mc-cpp-restore/bundler/META-INF/versions/26.2/server-26.2.jar` |
| 备份 | GitHub `m62m61m80bpzm2/haidian-fork` 孤儿分支 `mc262-src`（重置后可恢复） |

## 1. 技术选型（已定，勿更改）

- **C++20**，gcc 14.2（Debian trixie）
- **CMake 3.31 + Ninja**（deb 解包至 `tools/cpptools/`，`source /home/z/my-project/env.sh` 后 cmake/ninja 直接可用）
- **依赖仅三个**：asio 1.30（header-only，standalone 非 boost）、spdlog 1.15、zlib 1.3。头文件已解包至 `tools/cpptools/usr/include/`，编译时 `-isystem` 指过去即可，**禁止 vcpkg/conan/网络拉包**
- Netty → asio（`asio::ip::tcp` + 异步）；log4j2 → spdlog；Java 集合 → std 结构

## 2. 翻译规则（宪法，所有子代理必须遵守）

1. **逻辑 1:1**：控制流、算法、字节序、异常语义逐条对照 java 源。翻译时在类/函数头注释写上 java 源文件相对路径（如 `// from net/minecraft/network/VarInt.java`）
2. **内存重新设计**（不 1:1 模拟 GC）：
   - 所有权用 `unique_ptr`/值语义；观察者一律裸指针（不拥有）
   - Entity 一律由 Level 持有（`vector<unique_ptr<Entity>>`），别处只存裸指针或 int id
   - BlockState 用 intern 池：全唯一实例，指针即身份，`==` 比较指针
3. **命名空间映射**：包 `net.minecraft.network` → 命名空间 `mc::network`；目录 `src/mc/network/`（与 java 包逐级镜像）。类名保持 PascalCase 与 java 相同；方法名 camelCase → snake_case；java getter 链 `getFoo()` → `foo()`
4. **确定性**：RandomSource 必须与 java 同算法（LCG 64 位同种子同序列），任何随机数禁止用 std::mt19937 顶替
5. **可跳过项**（不做）：datagen（~2.5 万行）、datafixerUpper（延后 P7）、client 渲染、flightrecorder/jfr
6. 禁引入 asio/spdlog/zlib 之外的第三方库；禁后台守护进程；产物一律放 `/home/z/my-project/` 下

## 3. 目录结构

```
mc-cpp/
  CMakeLists.txt            # 根：全局设置 + add_subdirectory(src) + tools
  docs/cpp-rewrite-plan.md  # 本文档
  src/mc/                   # 与 net/minecraft/ 逐包镜像
    nbt/                    # P1
    network/                # P1 (VarInt/VarLong/压缩/Cipher 占位)
    protocol/               # P1 (包定义与状态机)
    server/network/         # P1 (三个 Listener 的 C++ 版)
    util/                   # P0 就位 (Mth 等按需)
  src/mcserver_main.cpp     # P0: 空服务器入口
  tools/capture-proxy/      # P0-b: 抓包代理 (独立可执行)
  tests/                    # P1 起逐步补
  build/                    # 构建产物 (git 忽略)
```

## 4. 阶段计划

### P0 — 工程骨架 + 抓包代理（预算 0.3M token）
- [x] 工具链就位（cmake/ninja/asio/spdlog 解包完成，重建脚本 scripts/rebuild_mc_env.sh）
- [ ] P0-a `src/mc/` 目录骨架 + 根 CMake + 空服务器：spdlog 打横幅 → asio 监听 :25565 → 接受连接仅记日志
- [ ] P0-b 抓包代理 `tools/capture-proxy`：TCP 中间人 (listen 25566 → upstream 25565)，全双工转发零改动，按方向落盘原始字节；`--parse` 模式按 MC 帧格式切包（VarInt 长度前缀）逐包 hexdump
- **验收**：① `cmake -G Ninja` + `ninja` 零错误（-Wall -Wextra -Wpedantic 无警告）② mc-server 启动至 "listening" 日志且 `nc` 能连上 25565 ③ 代理转发 vanilla server（或 nc echo）字节不丢不改，日志可回放

### P1 — 网络协议层（预算 3~5M token）
- [ ] P1-a 字节原语 + NBT：
  - `mc::network::ByteBuffer`（= FriendlyByteBuf 的 C++ 化：readVarInt/VarLong/writeVarInt/VarLong/String(UTF, VarInt 长度前缀)/UUID/ByteArray/Optional）
  - `mc::nbt` 全套 13 Tag + 读写器（NbtIo/TagTypes 对齐；**注意 1.20.5+ 的 NBT 在网络上是 VarInt 长度前缀的改良格式**，以 `net/minecraft/nbt/NbtIo.java`、`network/codec/` 下 stream codec 为准）
  - 单元测试：对拍 mcsrc 里 CompoundTag/IntTag 等的读写行为
- [ ] P1-b 状态机 + 压缩 + offline：
  - handshake(0x00 C→S, intent 1=status 2=login) → status: request(0x00)/response(JSON)/ping(0x01)/pong → login: start → (offline 模式) success（UUID = md5("OfflinePlayer:"+name) v3，参照 `UUIDUtil.createOfflinePlayerIdentifier`）→ acknowledge → configuration: client info/known packs/registry 数据（最小集，够客户端进主菜单）→ finish → (play 边界即停)
  - zlib 压缩：login success 后发 Set Compression(阈值 256)，之后 CompressionDecoder/Encoder 逻辑（阈值内不压缩仍写长度前缀 0）
  - JSON status 响应生成（version.name="26.2" protocol=776, players, description, favicon 可空）
  - 参照：`server/network/ServerHandshakePacketListenerImpl.java`、`ServerLoginPacketListenerImpl.java`、`ServerConfigurationPacketListenerImpl.java`、`network/Connection.java`、`network/CompressionDecoder.java`、`network/protocol/ProtocolInfo.java`
- **验收**：vanilla 26.2 server 与 mc-cpp 同机启动；capture-proxy 分别记录两边对同一 client 输入的字节序列；status 响应 JSON 字段级等价、login/configuration 流程帧序列同构（长度可差，语义一致）；客户端对 C++ 服务器握手→进主菜单成功作加分项

### P2 — 注册表与资源（概要）
core/Registry, ResourceLocation(→mc::resources::Identifier), Tag 集, Block/Item 静态表, datapack 加载骨架（json 解析器自写）

### P3 — 世界与存档（概要）
Level/LevelChunk/Section(PalettedContainer), NBT 存档读写(anvil 区域文件), DimensionType, 生成器接口（噪声生成可延 P5）

### P4 — 实体与方块行为（概要）
Entity 层次(Level 持有), BlockState 硬编码表(intern 池), tick 调度, 碰撞(VoxelShape 简化 AABB 优先)

### P5 — 玩法层（概要）
Player/Inventory/ItemStack, 交互, 伤害/生命, 噪声地形生成

### P6 — 命令与进度（概要）
brigadier C++ 化(commands/tree), 命令解析执行, advancement 触发

### P7 — datafix 与兼容（概要）
DataFixerUpper 子集, 旧存档升级路径

### P8 — 打磨（概要）
RCON, 查询协议, 性能, 与 vanilla 的全量协议回归

## 5. 环境备忘

- `source /home/z/my-project/env.sh` 后 cmake/ninja 可用（LD_LIBRARY_PATH 已含解包 lib）
- **重置后恢复**：`bash /home/z/my-project/scripts/rebuild_mc_env.sh`（幂等，3 分钟）；mcsrc 也可从 GitHub `haidian-fork` 的 `mc262-src` 分支恢复：`git clone -b mc262-src https://github.com/m62m61m80bpzm2/haidian-fork.git mcsrc`
- 编译 include：`-isystem /home/z/my-project/tools/cpptools/usr/include`（asio+spdlog 头）；链接：`-L$MC_CPP_TOOLS/usr/lib/x86_64-linux-gnu`（spdlog 库 + fmt），zlib 用系统 `-lz`
- spdlog 推荐 header-only 模式（`SPDLOG_HEADER_ONLY`）避免链接解包库的 ABI 问题——由 P0-a 决定并在 CMake 固化
- 跑 vanilla：`java -jar download/mc-26.2-server.jar --nogui`（首次需 `echo "eula=true" > eula.txt`），offline 用 `online-mode=false`
- **禁后台守护进程**：长命令前台 + 轮询产物；Bash 工具单次 ≤10 分钟
- 无 root：装软件用 `apt-get download 包名 && dpkg-deb -x 包 tools/` 方案
