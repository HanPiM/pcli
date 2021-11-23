#include <stdarg.h>

#include "pcli_private.h"

#if __IS_WIN
#   include <Windows.h>
#else
#   include <unistd.h>
#   include <termios.h>
#   include <sys/ioctl.h>
#endif

using namespace __pcli_private;

std::mutex g_mutex;

struct
{
#if __IS_WIN
    DWORD _win_stdin_oldmode, _win_stdout_oldmode, _win_stderr_oldmode;
    HANDLE _win_hstdin, _win_hstdout, _win_hstderr;
    HANDLE _win_alternate_screen;   /* 备用缓冲区屏幕 */
    HANDLE _win_draw_buffer_screen; /* 绘制缓冲区 */
    HANDLE _win_cur_outscreen;      /* 当前输出屏幕 */
    int _win_flag_use_ter_seq;      /* 使用虚拟终端序列 */
#else
    cc_t _linux_old_vtime;
    cc_t _linux_old_vmin;
#endif
    int flag_old_use_echo;
    int flag_old_use_linemode;
    int flag_use_mouse;
    struct
    {
        int line;
        int column;
    }last_siz, last_mouse_pos;
}g_objs;

static void _lock_g_objs() { g_mutex.lock(); }
static void _unlock_g_objs() { g_mutex.unlock(); }

#define LOCK_AND_INIT_SG()\
_lock_g_objs();\
error_detail __ed;\
scope_guard __sg([&]() {set_last_error(__ed); _unlock_g_objs(); })
#define UNLOCK_AND_CLOSE_SG() __sg.close(),_unlock_g_objs()

/*
* 规定非接口函数操作前不进行加锁, 接口函数会进行加锁
*/

/*
* TODO: 写入 ensure_detail 后检查, 如错误则更新last_error
*/

#if __IS_WIN
/* make sure is true */
#define _ENSURE(expr) __ed=ENSURE(expr)(GetLastError())
/* make sure isn't NULL */
#define _ENSURE_NONUL(x) __ed=ENSURE(NULL!=(x))(GetLastError())
#define _CHECK_ED() if(!__ed.what().empty())return false
#define _S_CHECK_ED() if(!__ed.what().empty())\
    do{set_last_error(__ed);return false;}while(0)
#else
#endif

#if __IS_WIN
__NON_EXPORT bool update_old_scr_mode()
{
    error_detail __ed;
    scope_guard se([&]() {set_last_error(__ed); });
    _ENSURE(GetConsoleMode(g_objs._win_hstdin, &g_objs._win_stdin_oldmode))
        (g_objs._win_hstdin); _CHECK_ED();
    _ENSURE(GetConsoleMode(g_objs._win_hstdout, &g_objs._win_stdout_oldmode))
        (g_objs._win_hstdout); _CHECK_ED();
    _ENSURE(GetConsoleMode(g_objs._win_hstderr, &g_objs._win_stderr_oldmode))
        (g_objs._win_hstderr); _CHECK_ED();
    se.close();
    return true;
}
__NON_EXPORT bool restore_old_scr_mode()
{
    error_detail __ed;
    scope_guard se([&]() {set_last_error(__ed); });
    _ENSURE(SetConsoleMode(g_objs._win_hstdin, g_objs._win_stdin_oldmode))
        (g_objs._win_hstdin); _CHECK_ED();
    _ENSURE(SetConsoleMode(g_objs._win_hstdout, g_objs._win_stdout_oldmode))
        (g_objs._win_hstdout); _CHECK_ED();
    _ENSURE(SetConsoleMode(g_objs._win_hstderr, g_objs._win_stderr_oldmode))
        (g_objs._win_hstderr); _CHECK_ED();
    se.close();
    return true;
}
#endif
bool __pcli_private::ter::init()
{
    LOCK_AND_INIT_SG();
#if __IS_WIN
    DWORD inmode;

    _ENSURE_NONUL(g_objs._win_hstdin = GetStdHandle(STD_INPUT_HANDLE));
    _CHECK_ED();
    _ENSURE_NONUL(g_objs._win_hstdout = GetStdHandle(STD_OUTPUT_HANDLE));
    _CHECK_ED();
    _ENSURE_NONUL(g_objs._win_hstderr = GetStdHandle(STD_ERROR_HANDLE));
    _CHECK_ED();

    g_objs._win_cur_outscreen = g_objs._win_hstdout;

    if (!update_old_scr_mode()) { UNLOCK_AND_CLOSE_SG(); return false; }

    _ENSURE_NONUL(g_objs._win_alternate_screen = CreateConsoleScreenBuffer(
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, CONSOLE_TEXTMODE_BUFFER, NULL
    )); _CHECK_ED();

    inmode = g_objs._win_stdin_oldmode & (~ENABLE_PROCESSED_INPUT);
    /*
    * 如果表达式为假会写入详细信息, 一定会返回, 执行不到这里,
    * 但 VS 编译器识别不出来, 会报 Warning
    */
    _ENSURE(SetConsoleMode(g_objs._win_hstdin, inmode))
        (g_objs._win_hstdin);
    _CHECK_ED();
#endif
    UNLOCK_AND_CLOSE_SG();
    if (!set_flags(0))return false;
    return true;
}
void __pcli_private::ter::clean_up()
{
#if __IS_WIN
    _lock_g_objs();
    restore_old_scr_mode();
    CloseHandle(g_objs._win_alternate_screen);
    CloseHandle(g_objs._win_draw_buffer_screen);
    _unlock_g_objs();
#endif
}
/* Windows 下需要使用 WriteConsole 输出到特定缓冲区 */
#if __IS_WIN
__NON_EXPORT int ter_vhprintf(HANDLE in_hscreen, const char* in_format, va_list ap)
{
    va_list t;
    size_t need;
    int res;
    char* buf;
    char sbuf[1024];
    va_copy(t, ap);
    need = vsnprintf(NULL, 0, in_format, t);
    if (need < 1023)buf = sbuf;
    else buf = (char*)malloc(need + 1);
    res = vsnprintf(buf, need + 1, in_format, ap);

    WriteConsoleA(in_hscreen, buf, (DWORD)need, NULL, NULL);
    if (buf != NULL && buf != sbuf)
        free(buf);
    va_end(t);
    return res;
}
__NON_EXPORT int ter_hprintf(HANDLE in_hscreen, const char* in_format, ...)
{
    int res;
    va_list ap;
    va_start(ap, in_format);
    res = ter_vhprintf(in_hscreen, in_format, ap);
    va_end(ap);
    return res;
}
int __pcli_private::ter::printf(const char* in_format, ...)
{
    va_list ap;
    int res;
    va_start(ap, in_format);
    _lock_g_objs();
    res = ter_vhprintf(g_objs._win_cur_outscreen, in_format, ap);
    _unlock_g_objs();
    va_end(ap);
    return res;
}
#else
int __pcli_private::ter::printf(const char* in_format, ...)
{
    va_list ap;
    int res;
    va_start(ap, in_format);
    _lock_g_objs();
    res = vprintf(in_format, ap);
    _unlock_g_objs();
    va_end(ap);
    return res;
}
#endif

bool __pcli_private::ter::get_size(int& out_rows, int& out_columns)
{
    LOCK_AND_INIT_SG();
#if __IS_WIN
    CONSOLE_SCREEN_BUFFER_INFO info;
    _ENSURE(GetConsoleScreenBufferInfo(g_objs._win_cur_outscreen, &info))
        (g_objs._win_cur_outscreen); _CHECK_ED();
    out_columns = info.srWindow.Right - info.srWindow.Left + 1;
    out_rows = info.srWindow.Bottom - info.srWindow.Top + 1;
#else
    struct winsize w;
    _ENSURE(ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0); _CHECK_ED();
    out_columns = w.ws_col;
    out_rows = w.ws_row;
#endif
    UNLOCK_AND_CLOSE_SG();
    return true;
}
bool __pcli_private::ter::set_size(int in_rows, int in_columns)
{
#if __IS_WIN
    LOCK_AND_INIT_SG();
    CONSOLE_SCREEN_BUFFER_INFO info;
    SMALL_RECT rc;
    COORD siz;

    rc.Left = rc.Top = 0;
    rc.Right = (SHORT)in_columns - 1;
    rc.Bottom = (SHORT)in_rows - 1;
    _ENSURE(GetConsoleScreenBufferInfo(g_objs._win_cur_outscreen, &info))
        (g_objs._win_cur_outscreen); _CHECK_ED();
    siz.X = info.dwSize.X, siz.Y = info.dwSize.Y;
    /* 调整窗口大小需确保窗口缓冲区不小于目标大小 */
    if (siz.Y < in_columns || siz.X < in_rows)
    {
        if (siz.X < in_columns)siz.X = (SHORT)in_columns;
        if (siz.Y < in_rows)siz.Y = (SHORT)in_rows;
        _ENSURE(SetConsoleScreenBufferSize(g_objs._win_cur_outscreen, siz))
            (g_objs._win_cur_outscreen); _CHECK_ED();
    }
    _ENSURE(SetConsoleWindowInfo(g_objs._win_cur_outscreen, TRUE, &rc))
        (g_objs._win_cur_outscreen); _CHECK_ED();
    /* 调整缓冲区大小指合适大小以隐藏滚动条 */
    siz.X = (SHORT)in_columns;
    siz.Y = (SHORT)in_rows;
    _ENSURE(SetConsoleScreenBufferSize(g_objs._win_cur_outscreen, siz))
        (g_objs._win_cur_outscreen); _CHECK_ED();
    
    UNLOCK_AND_CLOSE_SG();
#else
    /*
    struct winsize w;
    memset(&w, 0, sizeof(w));
    w.ws_col = in_columns;
    w.ws_row = in_lines;
    if (ioctl(STDOUT_FILENO, TIOCSWINSZ, &w) != 0)_ER(errno);
    */
    ter_seq_setsize(in_columns, in_rows);
#endif
    return true;
}

__NON_EXPORT bool set_mouse_input(bool flag_enable)
{
#if __IS_WIN
    DWORD mode = 0;
    error_detail __ed;
    _ENSURE(GetConsoleMode(g_objs._win_hstdin, &mode));
    _S_CHECK_ED();
    if (flag_enable)
    {
        mode |= ENABLE_MOUSE_INPUT;
        mode &= (~ENABLE_QUICK_EDIT_MODE);
    }
    else
    {
        mode &= (~ENABLE_MOUSE_INPUT);
        if (g_objs._win_stdin_oldmode & ENABLE_QUICK_EDIT_MODE)
            mode |= ENABLE_QUICK_EDIT_MODE;
    }
    _ENSURE(SetConsoleMode(g_objs._win_hstdin, mode));
    _S_CHECK_ED();
#else
    if (flag_enable) ter_seq_enable_mouse();
    else ter_seq_disable_mouse();
#endif
    return true;
}
__NON_EXPORT bool set_echo(bool flag_enable)
{
#if __IS_WIN
    DWORD mode = 0;
    error_detail __ed;
    _ENSURE(GetConsoleMode(g_objs._win_hstdin, &mode));
    _S_CHECK_ED();
    if (flag_enable) mode |= ENABLE_ECHO_INPUT;
    else mode &= (~ENABLE_ECHO_INPUT);
    _ENSURE(SetConsoleMode(g_objs._win_hstdin, mode));
    _S_CHECK_ED();
#else

#endif
    return true;
}
__NON_EXPORT bool set_line_mode(bool flag_enable)
{
#if __IS_WIN
    DWORD mode = 0;
    error_detail __ed;
    _ENSURE(GetConsoleMode(g_objs._win_hstdin, &mode));
    _S_CHECK_ED();
    if (flag_enable) mode |= ENABLE_LINE_INPUT;
    else mode &= (~ENABLE_LINE_INPUT);
    _ENSURE(SetConsoleMode(g_objs._win_hstdin, mode));
    _S_CHECK_ED();
#else

#endif
    return true;
}

bool __pcli_private::ter::set_flags(int in_flags)
{
    LOCK_AND_INIT_SG();
    bool b = true;
    do
    {
#define MAKE(flag,func) if(!(b=func(in_flags&(flag))))break
        MAKE(USE_MOUSE_INPUT, set_mouse_input);
        MAKE(USE_ECHO, set_echo);
        MAKE(USE_LINE_MODE, set_line_mode);
    } while (0);
    UNLOCK_AND_CLOSE_SG();
    return b;
}