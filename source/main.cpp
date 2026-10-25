/*
 * SafeShutdown NRO  v1.4.4
 * Nintendo Switch Homebrew safe shutdown
 *
 * 用法:
 *   - SafeShutdown.nro 放 SD 卡 /switch/ 目录，相册长按 R 进 hbmenu 运行
 *   - 或用 Sphaira 装前端，从主菜单直接启动
 *   - 3 秒倒计时结束后关机；期间按 B 或 + 取消
 *
 * ★ 关机 API 必须按 applet 类型分派（v1.4 修复的核心问题）
 *   libnx 头文件明确规定:
 *     appletStartShutdownSequence()  —— 仅 SystemApplet，或 [15.0.0+] 的
 *                                       LibraryApplet / OverlayApplet
 *                                       内部走 IGlobalStateController
 *     appletRequestToShutdown()      —— 仅 AppletType_*Application
 *                                       内部走 IApplicationFunctions，成功后无限睡眠不返回
 *
 *   相册启动: hbmenu 是 LibraryApplet -> NRO 也是 LibraryApplet -> 用 StartShutdownSequence
 *   前端启动: 装的是正经 title      -> NRO 是 Application     -> 必须用 RequestToShutdown
 *
 * 其它约束:
 *   1) 界面文字只用 ASCII。libnx console 用系统共享字体（日文/英文），
 *      不含简体中文字形，中文会显示成乱码。
 *   2) 手柄 style set 必须用 HidNpadStyleSet_NpadStandard。
 *   3) 倒计时用 svcGetSystemTick 计时，不靠 consoleUpdate 的帧数。
 *   4) 界面布局全部按 consoleGetDefault() 的实际 windowWidth/windowHeight 居中。
 */

#include <switch.h>
#include <cstdio>
#include <cstring>

#define COUNTDOWN_SEC 3
#define BOX_INNER     50          // 方框内部宽度（字符数）

// 版本号由 Makefile 的 APP_VERSION 通过 -DAPP_VERSION 传入，单一来源
#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

//---------------------------------------------------------------------------------
// 工具
//---------------------------------------------------------------------------------
static void waitMs(u64 ms) {
    svcSleepThread(ms * 1000000ULL);
}

static int conCols(void) {
    PrintConsole* c = consoleGetDefault();
    return (c && c->windowWidth > 0) ? c->windowWidth : 80;
}

static int conRows(void) {
    PrintConsole* c = consoleGetDefault();
    return (c && c->windowHeight > 0) ? c->windowHeight : 45;
}

static const char* appletTypeName(AppletType t) {
    switch (t) {
        case AppletType_Application:       return "Application";
        case AppletType_SystemApplication: return "SystemApplication";
        case AppletType_SystemApplet:      return "SystemApplet";
        case AppletType_LibraryApplet:     return "LibraryApplet";
        case AppletType_OverlayApplet:     return "OverlayApplet";
        case AppletType_None:              return "None";
        default:                           return "Default";
    }
}

static bool isApplication(AppletType t) {
    return t == AppletType_Application || t == AppletType_SystemApplication;
}

// 把 inner 居中填进 BOX_INNER 宽的区域，再加两侧竖线
static void makeBoxLine(char* out, const char* inner) {
    char field[BOX_INNER + 1];
    int len = (int)strlen(inner);
    if (len > BOX_INNER) len = BOX_INNER;
    int pad = (BOX_INNER - len) / 2;
    int i = 0;
    for (; i < pad; i++) field[i] = ' ';
    for (int j = 0; j < len; j++) field[i++] = inner[j];
    while (i < BOX_INNER) field[i++] = ' ';
    field[BOX_INNER] = 0;
    sprintf(out, "|%s|", field);
}

// 整块内容水平 + 垂直居中输出
static void printBlock(char lines[][96], int n) {
    int cols = conCols();
    int rows = conRows();

    consoleClear();

    int top = (rows - n) / 2;
    if (top < 0) top = 0;
    for (int i = 0; i < top; i++) printf("\n");

    for (int i = 0; i < n; i++) {
        int len = (int)strlen(lines[i]);
        int pad = (cols - len) / 2;
        if (pad < 0) pad = 0;
        printf("%*s%s\n", pad, "", lines[i]);
    }
}

//---------------------------------------------------------------------------------
// 大号数字点阵（只用到 1~3）
//   7 宽 × 10 高，双行厚笔画 = 加粗七段数码管
//   填充字符由 COUNT_STYLE 决定：# (1) / @ (2) / =| 细七段 (3) / + (4)
//   只用 ASCII —— libnx console 只支持 ASCII 字形
//---------------------------------------------------------------------------------
#ifndef COUNT_STYLE
#define COUNT_STYLE 1
#endif

static const char* g_digitMask[4][10] = {
    { "", "", "", "", "", "", "", "", "", "" },                       // 0 占位
    { "   ##  ", "  ###  ", "   ##  ", "   ##  ", "   ##  ",           // 1
      "   ##  ", "   ##  ", "   ##  ", "   ##  ", "  #### " },
    { "#######", "#######", "     ##", "     ##", "#######",           // 2
      "#######", "##     ", "##     ", "#######", "#######" },
    { "#######", "#######", "     ##", "     ##", "#######",           // 3
      "#######", "     ##", "     ##", "#######", "#######" },
};

// COUNT_STYLE 3：细线七段，横段用 =、竖段用 |
#if COUNT_STYLE == 3
static const char* g_digitSeg[4][10] = {
    { "", "", "", "", "", "", "", "", "", "" },
    { "    |  ", "   ||  ", "    |  ", "    |  ", "    |  ",           // 1
      "    |  ", "    |  ", "    |  ", "    |  ", "  ==== " },
    { "=======", "      |", "      |", "=======", "|      ",           // 2
      "|      ", "|      ", "|      ", "=======", "" },
    { "=======", "      |", "      |", "=======", "      |",           // 3
      "      |", "      |", "      |", "=======", "" },
};
#endif

#define DIGIT_ROWS 10

static void digitLine(int d, int row, char* out) {
    int idx = (d >= 1 && d <= 3) ? d : 0;
#if COUNT_STYLE == 3
    strcpy(out, g_digitSeg[idx][row]);
#else
    const char* src = g_digitMask[idx][row];
    char fill = '#';
#if COUNT_STYLE == 2
    fill = '@';
#elif COUNT_STYLE == 4
    fill = '+';
#endif
    int i = 0;
    for (; src[i]; i++) out[i] = (src[i] == '#') ? fill : ' ';
    out[i] = 0;
#endif
}

//---------------------------------------------------------------------------------
// 倒计时界面
//---------------------------------------------------------------------------------
static void drawUI(int remain, AppletType type) {
    char lines[32][96];
    char tmp[128];
    int n = 0;

    // 方框
    char border[BOX_INNER + 3];
    border[0] = '+';
    for (int i = 0; i < BOX_INNER; i++) border[1 + i] = '=';
    border[1 + BOX_INNER] = '+';
    border[2 + BOX_INNER] = 0;

    strcpy(lines[n++], border);
    makeBoxLine(lines[n++], "");
    makeBoxLine(lines[n++], "S A F E   S H U T D O W N");
    makeBoxLine(lines[n++], "");
    strcpy(lines[n++], border);

    strcpy(lines[n++], "");
    strcpy(lines[n++], "Shutting down in");
    strcpy(lines[n++], "");

    for (int i = 0; i < DIGIT_ROWS; i++) digitLine(remain, i, lines[n++]);

    // 单位紧跟数字下方，凑成完整的 "Shutting down in 3 seconds"
    // 注意 1 要用单数
    strcpy(lines[n++], (remain == 1) ? "second" : "seconds");

    strcpy(lines[n++], "");
    strcpy(lines[n++], "Press  [ B ]  to cancel");
    strcpy(lines[n++], "");
    strcpy(lines[n++], "--------------------------------------------------");
    snprintf(tmp, sizeof(tmp), "v%s   2026-10-25      mode: %s",
             APP_VERSION, appletTypeName(type));
    strcpy(lines[n++], tmp);
    strcpy(lines[n++], "--------------------------------------------------");

    printBlock(lines, n);
}

//---------------------------------------------------------------------------------
// 关机：按 applet 类型分派 + 多级兜底
//---------------------------------------------------------------------------------
static void doShutdown(void) {
    char lines[20][96];
    char tmp[128];
    int n = 0;

    consoleClear();
    {
        char one[20][96];
        strcpy(one[0], "");
        strcpy(one[1], "Shutting down, please wait ...");
        strcpy(one[2], "");
        printBlock(one, 3);
    }
    consoleUpdate(NULL);

    AppletType type = appletGetAppletType();
    Result rcPrimary = 0, rcSecondary = 0, rcSpsm = 0;

    if (isApplication(type)) {
        rcPrimary = appletRequestToShutdown();   // 成功则无限睡眠，不返回
        waitMs(1200);
        rcSecondary = appletStartShutdownSequence();
        waitMs(1200);
    } else {
        rcPrimary = appletStartShutdownSequence();
        waitMs(1200);
        rcSecondary = appletRequestToShutdown();
        waitMs(1200);
    }

    // 最后兜底：Atmosphere 的 spsm（原版 Safe_Shutdown 用的就是这条）
    rcSpsm = spsmInitialize();
    if (R_SUCCEEDED(rcSpsm)) {
        rcSpsm = spsmShutdown(false);
        waitMs(1500);
        spsmExit();
    }

    // 全部失败：诊断信息居中显示，等用户按 B
    strcpy(lines[n++], "");
    strcpy(lines[n++], "Shutdown did NOT take effect.");
    strcpy(lines[n++], "");
    strcpy(lines[n++], "--------------------------------------------------");
    snprintf(tmp, sizeof(tmp), "mode                  : %s (%d)",
             appletTypeName(type), (int)type);
    strcpy(lines[n++], tmp);
    if (isApplication(type)) {
        snprintf(tmp, sizeof(tmp), "RequestToShutdown     : 0x%08X", (unsigned)rcPrimary);
        strcpy(lines[n++], tmp);
        snprintf(tmp, sizeof(tmp), "StartShutdownSequence : 0x%08X", (unsigned)rcSecondary);
        strcpy(lines[n++], tmp);
    } else {
        snprintf(tmp, sizeof(tmp), "StartShutdownSequence : 0x%08X", (unsigned)rcPrimary);
        strcpy(lines[n++], tmp);
        snprintf(tmp, sizeof(tmp), "RequestToShutdown     : 0x%08X", (unsigned)rcSecondary);
        strcpy(lines[n++], tmp);
    }
    snprintf(tmp, sizeof(tmp), "spsmInit/Shutdown     : 0x%08X", (unsigned)rcSpsm);
    strcpy(lines[n++], tmp);
    strcpy(lines[n++], "--------------------------------------------------");
    strcpy(lines[n++], "");
    strcpy(lines[n++], "Press  [ B ]  to exit.  (screenshot this)");
    strcpy(lines[n++], "");

    printBlock(lines, n);
}

//---------------------------------------------------------------------------------
// 主循环
//---------------------------------------------------------------------------------
int main(int argc, char** argv) {
    consoleInit(NULL);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    const AppletType type = appletGetAppletType();
    const u64 freq  = armGetSystemTickFreq();
    const u64 start = svcGetSystemTick();
    int lastShown = -1;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 keysDown = padGetButtonsDown(&pad);

        if (keysDown & (HidNpadButton_B | HidNpadButton_Plus)) {
            break;
        }

        u64 elapsed = svcGetSystemTick() - start;
        int remain = COUNTDOWN_SEC - (int)(elapsed / freq);

        if (remain <= 0) {
            doShutdown();
            // 全部失败时会停在诊断屏，等 B 才返回
            while (appletMainLoop()) {
                padUpdate(&pad);
                if (padGetButtonsDown(&pad) & (HidNpadButton_B | HidNpadButton_Plus))
                    break;
                consoleUpdate(NULL);
            }
            break;
        }

        if (remain != lastShown) {
            drawUI(remain, type);
            lastShown = remain;
        }

        consoleUpdate(NULL);
    }

    consoleExit(NULL);
    return 0;
}
