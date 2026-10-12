# HOI4 Globe Map Fix

为 Globe Mod 修补 HOI4 的地图图标锚点投影。

（使用gpt-6.1-sol，harness为codex）

## 安装

1. 已安装并启用 hoi4 mod injector：https://github.com/MyresMkG/HOI4-Mod-Injector/releases ；在启动器的播放集中启用 Globe Mod：https://steamcommunity.com/sharedfiles/filedetails/?id=3816496697 。
2. 把成品的 **Globe Map Fix 整个目录**放到游戏的 `injected_mods` 下，最终路径是：

```text
Hearts of Iron IV/
  injected_mods/
    Globe Map Fix/
      Globe Map Fix.dll
      Globe Map Fix.ini
```

3. 重启游戏。此 DLL 通过 LoadLibrary 自行延迟初始化，不需要 injector 调用自定义初始化导出。
4. 检查同目录生成的 `Globe Map Fix.log`。成功时包含 `Installed: one 5-byte call replaced`。injector 日志中的 `loaded` 仅表示 DLL 已载入，不能代替此安装成功记录。

成品只交付在工作目录，本次没有安装到实际游戏目录。

## 行为和范围

- 只替换通用地图图标路径中的一条 5 字节 CALL；原投影函数本体不改。近地址 RX 跳板转入修补函数，再调用原投影函数。
- 地形变形所用的 GlobePosition 算法同步到图标的**临时**世界锚点。普通二维 GUI 继续完成大小、对齐、整数取整、叠放和点击区域设置。
- 通用图标入口包含兵牌、聚合兵牌及其他走同一投影路径的地图图标；不是全局拦截所有投影函数调用。
- 普通缩放调用原投影，球面背面及越界锚点置到屏幕外，避免显示和普通 GUI 命中。没有将球面坐标写回省份、单位或持久锚点。
- 本补丁的范围是图标锚点；点击地图省份、右键地图下令等地块反向拾取沿用游戏原逻辑，尚未修正。

## 兼容性与拒绝条件

不使用固定函数 RVA、固定相机成员偏移或固定地图宽高。启动时扫描 CALL 上下文，同时核对原投影函数的独立指令特征；必须恰好有一个有效候选。相机位置和前向方向由原投影矩阵恢复，地图尺寸从有效 provinces.bmp 头部读取。

已核对本机 HOI4 **1.19.3.0** 的指令，并在地址不同的测试宿主验证定位。相同指令形态的更新版本可以自行定位；编译器改变寄存器/指令形态、函数 ABI 改变或其他原生模组先修改该调用时，会拒绝修补。不能保证所有未来游戏版本都兼容。

只支持与当前 Globe Mod **0.2.0** 一致的球面变换。读取 shader 的数学部分，忽略空白和注释，再检查变换契约；数学变更会拒绝修补，需要重新审阅 CPU 实现。WATER_HEIGHT 则读取有效 constants.fxh。

默认读取游戏正在使用的 `dlc_load.json`，按启用顺序解析目录式模组，并处理相关 `replace_path` 遮蔽。没有启用球面 shader 时不修补；数学契约不符、地形 shader 不使用 GlobeWarp、模组元数据缺失、压缩包模组或定位不唯一时不修补。这样会牺牲部分模组组合的可用性，避免在无法确认实际资源时错误启用。没有每帧重新扫描磁盘；更改播放集、shader 或配置后应重启。

用户目录依次从 INI 自定义值、`-userdir` 参数、游戏 `userdir.txt`、Windows 实际 Documents 已知文件夹确定。自定义 `user_data_dir` 必须与游戏实际目录一致，不要指向另一播放集的数据目录。带非 ASCII 自定义路径的 INI 请保存为 UTF-16 LE（带 BOM），符合 Win32 INI API 的读取方式。

## 配置与禁用

`Globe Map Fix.ini`：

```ini
[fix]
enabled=1
probe=0
user_data_dir=
```

- `enabled=0`：下次启动时不修补。
- `probe=1`：检查模组和特征，完全不写进程代码。
- injector 的 `probe=1` 生成的 `diplo_action_hook_probe_only.txt` 同样生效。
- 在 DLL 旁放置空文件 `Globe Map Fix.dllnoinject`，可通过已有 injector 禁止加载。
- 删除整个安装目录后重启即可卸载。DLL 为异步回调和跳板存活期被 pin 到进程结束，不支持运行中 FreeLibrary 卸载。

导出 `GlobeMapFix_GetStatus()` 仅用于诊断：0 等待初始化、1 球面未启用/宿主非 hoi4.exe、2 拒绝修补、3 探测完成、4 已安装、5 INI 禁用。

## 构建和测试

MinGW-w64 x64、C++17。使用系统 Windows DLL 和 Universal CRT；libgcc/libstdc++ 静态链接，不需要另外复制 MinGW 运行库。不是一个 dxgi 代理，不替换 injector。

```powershell
.\build.bat
py .\tools\run_tests.py
```

`MINGW_BIN` 可指定编译器 bin 目录，`OUTDIR` 可指定构建输出目录。默认输出到工作区的 `full_releases\Globe Map Fix`。测试目录是源码的 `build`；GPU 测试从本机 Workshop 读取 shader，没有把原 shader 或游戏文件打包到成品。测试脚本内的 EXE/MOD 路径可按安装位置调整。

测试内容包括：17,887 项数值/JSON/定位检查；实际 Globe shader 的 D3D11 WARP 计算着色器对照；四线程调用期间安装；平面输出逐字节保持、球面锚点与原投影调用 ABI、背面和无效坐标、模块 pin、重复加载、自定义用户目录、元数据与资源冲突拒绝；以及工作区已有真实 injector 的加载和 probe 模式。

GPU 对照共 221,184 个锚点，涵盖 800～2400 高度、起始过渡、完整球面和三个倾斜方向。最大世界坐标差 0.075684；可见屏幕内最大位置差 **0.106201 像素**（1920×1080）。CPU 与 GPU 的 trig/rsqrt 舍入不同，不声明位级完全一致。

测试宿主的球面调用约 139～141 ns/图标，原测试投影约 6 ns；普通缩放约 22～24 ns。球面每 10,000 次投影的新增耗时约 1.3 ms。相机数据按矩阵变化缓存；常量、文件和特征只在初始化读取，热路径没有日志、文件访问或全局锁。TLS 缓存每线程首次使用有运行库初始化开销。该结果是合成基准，不能代替实际 HOI4 帧率测量。

完整报告在成品 `验证报告.json`。**尚未执行真实 HOI4 战局画面与鼠标交互测试**，因此没有保证绝对无 bug，也没有把测试宿主的结果写成实机验证。成品已完成上述自动测试，之后若做实机验证，应重点检查缩放过渡、地图接缝、聚合兵牌、悬停和框选。
