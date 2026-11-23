# ChargerProbe

一个很小的探针，用来测量 Windows 上“插拔充电器”事件从三条路径到达的时间：

1. `GUID_ACDC_POWER_SOURCE` 的 `DEVICE_NOTIFY_CALLBACK` 回调（系统线程）
2. 隐藏顶层窗口收到的 `WM_POWERBROADCAST`（`PBT_POWERSETTINGCHANGE` 和 `PBT_APMPOWERSTATUSCHANGE`）
3. 每 250ms 轮询一次 `GetSystemPowerStatus`

它不播放声音，只记录时间戳，控制台和 CSV 各一份。

## 运行

在 `probe` 目录里双击 `run.cmd`，或者：

```powershell
cd "D:\Workspace\OpenCode\WindowsChargingReminder\tools\probe"
.\run.cmd
```

`run.cmd` 发现没有 `probe.exe` 时会自动调用 `build.cmd` 编译。

## 编译

```powershell
cd "D:\Workspace\OpenCode\WindowsChargingReminder\tools\probe"
cmd /c build.cmd
```

用的是 MSYS2 UCRT64 的 `g++`（路径写在 `build.cmd` 里，可以改）。产物是静态链接的单个 `probe.exe`，只依赖系统 DLL。

当前这个 exe 是 x64 的，在 ARM64 平板上通过系统模拟运行。对“事件到达时间”的测量影响在微秒级，可以忽略；正式程序再出 ARM64 原生版本。

## 操作

1. 看到 `Plug/unplug the charger 5 times...` 后，插上充电器，等 2 秒。
2. 拔掉，等 2 秒。
3. 重复 5 次。
4. 按任意键结束（`Ctrl+C` 也可以），会打印汇总表。

也可以先跑固定时长：

```powershell
.\run.cmd --seconds 30
```

## 参数

| 参数 | 说明 |
|---|---|
| `--poll 250` | 轮询间隔，100–2000ms，默认 250 |
| `--seconds 30` | 30 秒后自动结束；不加则一直跑到 Ctrl+C |
| `--no-window` | 不做窗口消息路径，只看回调和轮询 |
| `--no-battery` | 不注册电池百分比回调，减少噪音 |
| `--help` | 帮助 |

## 怎么看输出

每行大概是：

```text
14:32:10.123       2.1ms [CALLBACK     ] ACDC         AC data=0
14:32:10.130       9.3ms [WINDOW       ] ACDC         AC data=0
14:32:10.135      14.1ms [WINDOW-STATUS] statuschg    AC flag=0x03 pct=75
14:32:10.373     252.4ms [POLL         ] ACDC         AC flag=0x03 pct=75
```

- `CALLBACK`：`PowerSettingRegisterNotification` 回调。如果这条每次插拔都有，说明平板的事件源是可靠的。
- `WINDOW`：窗口消息版本的 `GUID_ACDC`。正常情况下和 `CALLBACK` 很接近，差几毫秒。
- `WINDOW-STATUS`：`PBT_APMPOWERSTATUSCHANGE` 广播。它可能因为电量变化也触发，不一定是插拔。
- `POLL`：轮询。它的偏移就是“兜底模式下最坏要等多久”。

启动时会立刻出现一组 `CALLBACK ACDC DC/AC data=...` 和 `WINDOW ACDC ...`，那是系统在报告当前状态，不是插拔，也不会被计入转换。

结束时打印：

```text
Transitions (offset from the first observation):
#   state  time          CALLBACK   WINDOW    WINDOW-STATUS  POLL
1   DC     14:32:12.001  +0.0 ms    +2.1 ms   +2.4 ms        +248.3 ms
2   AC     14:32:14.003  +0.0 ms    +1.8 ms   +2.0 ms        +249.1 ms
Callback missed 0 of 2 transitions.
```

重点看：

- 每次插拔 `CALLBACK` 是否都有；`Callback missed` 是不是 0。
- `CALLBACK` 自己有没有抖动：一次插拔出现 `AC→DC→AC` 这种连续行。
- `POLL` 的偏移是否稳定在轮询间隔以内。
- `WINDOW` 和 `CALLBACK` 的差距；如果窗口路径明显更慢，正式程序就用回调。
- `WINDOW-STATUS` 是否比 `CALLBACK` 晚很多。

## CSV

每次运行会在当前目录生成 `probe-YYYYMMDD-HHmmss.csv`，列是：

```text
seq, local_time, monotonic_ms, source, event, detail, data
```

可以拿 Excel 或 Python 画时间线。

## 注意

- 这个探针不会改注册表、不会装服务、不需要管理员。
- 它只在当前用户会话里运行；如果平板进入 Modern Standby / 睡眠，Win32 程序可能被节流，测到的“延迟”会包含唤醒时间，这是正常的。
- 平板的社区充电驱动本身比较挑线、PD 协商也可能抖，所以建议用你平时用的那根线和充电器测。
- 插拔时如果 `CALLBACK` 和 `WINDOW` 都出现，但状态在几十毫秒内反复跳，说明 PD 协商不稳；正式程序需要一个防抖窗口（例如 300–500ms）。
