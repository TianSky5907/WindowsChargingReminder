#include "strings.h"

#include <windows.h>

#include <unordered_map>

namespace strings {
namespace {

std::wstring g_language = L"zh-CN";

const std::unordered_map<std::wstring, std::wstring>& English() {
    static const std::unordered_map<std::wstring, std::wstring> map = {
        // Navigation and pages
        {L"Windows 充电提示", L"Windows Charging Reminder"},
        {L"充电提示", L"Charging Reminder"},
        {L"Windows 充电提示 - 设置", L"Windows Charging Reminder - Settings"},
        {L"常规", L"General"},
        {L"声音", L"Sound"},
        {L"通知", L"Notifications"},
        {L"悬浮窗", L"Overlay"},
        {L"事件源", L"Sources"},
        {L"关于", L"About"},

        {L"外观、启动、提示与静音", L"Appearance, startup, alerts and mute"},
        {L"每个事件的触发阈值、声音文件和音量",
         L"Thresholds, sound files and volume for each event"},
        {L"Windows 通知的内容和通知声音", L"Notification content and sound"},
        {L"悬浮窗的样式、位置和预览", L"Overlay style, position and preview"},
        {L"监听方式和运行诊断", L"Detection methods and diagnostics"},
        {L"版本、日志和重置", L"Version, logs and reset"},

        // General
        {L"外观", L"Appearance"},
        {L"主题", L"Theme"},
        {L"跟随系统", L"Follow system"},
        {L"浅色", L"Light"},
        {L"深色", L"Dark"},
        {L"启动与行为", L"Startup & behavior"},
        {L"开机启动", L"Start with Windows"},
        {L"跟随系统勿扰", L"Follow Do Not Disturb"},
        {L"静音、全屏或演示时不提示",
         L"Do not alert while muted, fullscreen or presenting"},
        {L"提示方式", L"Alerts"},
        {L"播放声音", L"Play sound"},
        {L"Windows 通知", L"Windows notifications"},
        {L"暂时静音", L"Temporary mute"},
        {L"当前未静音", L"Not muted"},
        {L"已静音，剩余 ", L"Muted, "},
        {L"取消静音", L"Unmute"},
        {L"未静音", L"Not muted"},
        {L"静音时长", L"Mute duration"},
        {L"1–1440 分钟", L"1–1440 minutes"},
        {L"最大 99 小时 59 分钟", L"Up to 99 h 59 min"},
        {L"小时", L"h"},
        {L"分钟", L"min"},
        {L"立即静音", L"Mute now"},
        {L"语言", L"Language"},
        {L"简体中文", L"简体中文"},
        {L"English", L"English"},

        // Sound
        {L"总体", L"General"},
        {L"启用声音", L"Enable sound"},
        {L"关闭后仍可使用通知和悬浮窗",
         L"Notifications and overlay still work when off"},
        {L"主音量", L"Master volume"},
        {L"会与每个事件的音量相乘",
         L"Multiplied with the volume of each event"},
        {L"暂只支持 WAV。", L"WAV only for now."},
        {L"插入电源", L"Plugged in"},
        {L"接通电源时提示。", L"Alert when power is connected."},
        {L"拔出电源", L"Unplugged"},
        {L"断开电源时提示。", L"Alert when power is disconnected."},
        {L"电量充满", L"Fully charged"},
        {L"达到阈值时提示。", L"Alert when the threshold is reached."},
        {L"电量较低", L"Battery low"},
        {L"低于阈值时提示一次。",
         L"Alert once when the battery falls below the threshold."},
        {L"电量不足", L"Battery critically low"},
        {L"低于第二阈值时提示一次。",
         L"Alert once below the second threshold."},
        {L"充电异常", L"Charging issue"},
        {L"插电后电量异常下降时提示。",
         L"Alert if the battery drops unexpectedly while charging."},
        {L"事件", L"Event"},
        {L"触发阈值", L"Trigger threshold"},
        {L"声音文件", L"Sound file"},
        {L"使用 Windows 默认声音", L"Use Windows default sound"},
        {L"选择...", L"Browse..."},
        {L"默认", L"Default"},
        {L"预览", L"Preview"},
        {L"音量", L"Volume"},
        {L"时间窗口", L"Time window"},
        {L"下降", L"Drop"},

        // Notifications
        {L"启用通知", L"Enable notifications"},
        {L"显示当前电量", L"Show battery percentage"},
        {L"显示阈值文案", L"Show threshold text"},
        {L"充满时显示“电量已充至 N%”",
         L"Show “Charged to N%” when full"},
        {L"通知声音", L"Notification sound"},
        {L"自动：应用声音开启时不响系统通知声",
         L"Auto: silent while app sound is on"},
        {L"自动", L"Auto"},
        {L"始终开启", L"Always on"},
        {L"始终关闭", L"Always off"},
        {L"通知内容使用当前阈值和电量显示设置生成。",
         L"Uses the current thresholds and percentage settings."},
        {L"显示通知预览", L"Show preview"},
        {L" 秒", L" s"},
        {L" 分钟", L" min"},

        // Overlay
        {L"启用悬浮窗", L"Enable overlay"},
        {L"事件发生时显示一张圆角卡片",
         L"Show a rounded card when an event fires"},
        {L"外观与位置", L"Appearance & position"},
        {L"位置", L"Position"},
        {L"不会占用任务栏区域", L"Does not cover the taskbar"},
        {L"左上", L"Top left"},
        {L"上中", L"Top center"},
        {L"右上", L"Top right"},
        {L"左中", L"Middle left"},
        {L"右中", L"Middle right"},
        {L"左下", L"Bottom left"},
        {L"下中", L"Bottom center"},
        {L"右下", L"Bottom right"},
        {L"显示时长", L"Duration"},
        {L"0.5–10.0 秒，按 0.1 秒调整", L"0.5–10.0 s, 0.1 s steps"},
        {L"不透明度", L"Opacity"},
        {L"圆角", L"Corner radius"},
        {L"背景", L"Background"},
        {L"跟随系统时浅色主题用白色，深色主题用黑色",
         L"System: white in light mode, black in dark mode"},
        {L"黑色", L"Black"},
        {L"白色", L"White"},
        {L"图标框颜色", L"Icon color"},
        {L"第一个为跟随系统强调色",
         L"First swatch follows the system accent"},
        {L"已插入电源", L"Plugged in"},
        {L"当前电量 80%", L"Battery 80%"},
        {L"设置会立即应用。", L"Changes apply immediately."},
        {L"显示预览", L"Show preview"},

        // Sources
        {L"监听方式", L"Detection"},
        {L"默认使用 GUID_ACDC。修改后会立即重新注册监听。",
         L"Uses GUID_ACDC by default. Changes re-register immediately."},
        {L"GUID_ACDC 回调", L"GUID_ACDC callback"},
        {L"PBT_APMPOWERSTATUSCHANGE 广播",
         L"PBT_APMPOWERSTATUSCHANGE broadcast"},
        {L"回调失败时使用窗口消息", L"Window message fallback"},
        {L"电池百分比回调", L"Battery percentage callback"},
        {L"轮询（兜底）", L"Polling (fallback)"},
        {L"轮询间隔", L"Poll interval"},
        {L"全部启用（测试）", L"Enable all (test)"},
        {L"采用最佳", L"Use best"},
        {L"恢复默认", L"Reset defaults"},
        {L"诊断", L"Diagnostics"},
        {L"最近事件", L"Last event"},
        {L"暂无事件，插拔一次充电器试试",
         L"No events yet. Plug or unplug the charger."},
        {L"事件计数", L"Event counts"},
        {L"回调", L"Callback"},
        {L"窗口", L"Window"},
        {L"广播", L"Broadcast"},
        {L"轮询", L"Poll"},
        {L"电池", L"Battery"},
        {L"重置统计", L"Reset counts"},

        // About
        {L"版本 0.1.0", L"Version 0.1.0"},
        {L"架构：", L"Architecture: "},
        {L"ARM64 原生", L"Native ARM64"},
        {L"无管理员权限、无服务、无第三方运行时。",
         L"No admin rights, services or third-party runtime."},
        {L"文件位置", L"Files"},
        {L"配置文件", L"Config file"},
        {L"打开配置文件", L"Open config"},
        {L"日志文件", L"Log file"},
        {L"打开日志文件夹", L"Open log folder"},
        {L"维护", L"Maintenance"},
        {L"导出会把当前日志和配置复制到桌面上的一个新文件夹。",
         L"Copies the current log and config to a new folder on the Desktop."},
        {L"导出日志", L"Export logs"},
        {L"恢复默认会重置所有设置，包括阈值、声音和悬浮窗样式。",
         L"Resets all settings, including thresholds, sound and overlay style."},
        {L"恢复默认设置", L"Reset settings"},
        {L"确定要恢复所有默认设置吗？\n当前配置会被覆盖。",
         L"Reset all settings to defaults?\nThe current configuration will be overwritten."},

        // Tray
        {L"设置", L"Settings"},
        {L"静音", L"Mute"},
        {L"1 小时", L"1 hour"},
        {L"2 小时", L"2 hours"},
        {L"4 小时", L"4 hours"},
        {L"8 小时", L"8 hours"},
        {L"自定义静音...", L"Custom..."},
        {L"退出", L"Exit"},
        {L"[静音]", L"[Muted]"},
        {L"已静音 ", L"Muted for "},
        {L"已取消静音", L"Unmuted"},
        {L"已启用开机启动", L"Start with Windows enabled"},
        {L"已关闭开机启动", L"Start with Windows disabled"},

        // Notifications / balloons
        {L"已拔出电源", L"Unplugged"},
        {L"电池已充满", L"Fully charged"},
        {L"电池电量较低", L"Battery low"},
        {L"电池电量不足", L"Battery critically low"},
        {L"回调 ", L"Callback "},
        {L"窗口 ", L"Window "},
        {L"广播 ", L"Broadcast "},
        {L"轮询 ", L"Poll "},
        {L"电池 ", L"Battery "},
        {L"当前电量 ", L"Battery "},
        {L"电量已充至 ", L"Charged to "},
        {L"请检查充电器连接状态", L"Check the charger connection"},
        {L"未插入电源", L"Not plugged in"},
        {L"已静音", L"Muted"},
        {L"无法创建主窗口。", L"Failed to create the main window."},
        {L"无法创建托盘图标。请确认 Windows 资源管理器正在运行。",
         L"Failed to create the tray icon. Check that Explorer is running."},
        {L"无法创建设置窗口。", L"Failed to create the settings window."},

        // Dialogs
        {L"WAV 音频", L"WAV audio"},
        {L"所有文件", L"All files"},
        {L"选择提示音", L"Choose a sound"},
        {L"当前版本只支持 WAV。\nMP3 / M4A / FLAC 的自动转换会在后续版本加入。",
         L"Only WAV is supported for now.\nMP3 / M4A / FLAC import will be added later."},
        {L"当前版本只支持 WAV。", L"Only WAV is supported for now."},
    };
    return map;
}

}  // namespace

void SetLanguage(const std::wstring& setting) {
    if (setting == L"zh-CN") {
        g_language = L"zh-CN";
        return;
    }
    if (setting == L"en-US") {
        g_language = L"en-US";
        return;
    }

    const LANGID language = GetUserDefaultUILanguage();
    g_language = PRIMARYLANGID(language) == LANG_CHINESE ? L"zh-CN" : L"en-US";
}

std::wstring CurrentLanguage() {
    return g_language;
}

bool IsEnglish() {
    return g_language == L"en-US";
}

std::wstring Tr(const std::wstring& chinese) {
    if (!IsEnglish() || chinese.empty())
        return chinese;

    const auto& map = English();
    const auto found = map.find(chinese);
    return found == map.end() ? chinese : found->second;
}

std::wstring Duration(int64_t seconds) {
    if (seconds < 0)
        seconds = 0;

    if (IsEnglish()) {
        if (seconds < 60)
            return std::to_wstring(seconds) + L" s";
        if (seconds < 3600)
            return std::to_wstring(seconds / 60) + L" min";
        const int64_t hours = seconds / 3600;
        const int64_t minutes = (seconds % 3600) / 60;
        std::wstring result = std::to_wstring(hours) + L" h";
        if (minutes > 0)
            result += L" " + std::to_wstring(minutes) + L" min";
        return result;
    }

    if (seconds < 60)
        return std::to_wstring(seconds) + L" 秒";
    if (seconds < 3600)
        return std::to_wstring(seconds / 60) + L" 分钟";
    const int64_t hours = seconds / 3600;
    const int64_t minutes = (seconds % 3600) / 60;
    std::wstring result = std::to_wstring(hours) + L" 小时";
    if (minutes > 0)
        result += L" " + std::to_wstring(minutes) + L" 分钟";
    return result;
}

std::wstring Ago(int64_t seconds) {
    if (IsEnglish())
        return std::to_wstring(seconds) + L" s ago";
    return std::to_wstring(seconds) + L" 秒前";
}

}  // namespace strings
