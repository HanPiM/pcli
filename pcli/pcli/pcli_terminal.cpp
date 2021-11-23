#include <stdarg.h>

#include "pcli_private.h"

#if __IS_WIN
#   include <Windows.h>
#   include <VersionHelpers.h>
#else
#   include <unistd.h>
#   include <termios.h>
#   include <sys/ioctl.h>
#endif

using namespace __pcli_private;

#if __IS_WIN
#define _ENSURE(expr) this->last_err=ENSURE(expr)(GetLastError())
#define _ENSURE_NONUL(expr) this->last_err=ENSURE(NULL!=(expr))(GetLastError())
#else
#define _ENSURE(expr) this->last_err=ENSURE(expr)(errno)
#endif

#define _CHECK_ED() if(!this->last_err.what().empty())return false

#define ter_out_seq this->printf

/* 启用所有鼠标输入, 使用 SGR 模式 */
#define ter_seq_enable_mouse() ter_out_seq("\033[?1003h\033[?1006h")
#define ter_seq_disable_mouse() ter_out_seq("\033[?1003l")

#define ter_seq_resize(row,col) ter_out_seq("\033[8;%d;%dt",row,col)

#define ter_seq_active_main_screen() ter_out_seq("\033[?1049l")
#define ter_seq_active_alternate_screen() ter_out_seq("\033[?1049h")

void terminal::clear()
{
#if __IS_WIN
    stdin_oldmode = stdout_oldmode = stderr_oldmode = 0;
    stdin_nowmode = stdout_nowmode = stderr_nowmode = 0;
    hstdin = hstdout = hstderr = NULL;
    alternate_screen = draw_buffer_screen = NULL;
    curout_screen = NULL;
    is_use_seq = false;
#else
#endif
    is_use_mouse = false;
    is_exit = false;
}

terminal::terminal()
{
    clear();
}
terminal::~terminal() { exit(); }

error_detail terminal::last_error()
{
    error_detail e = last_err;
    last_err = error_detail();
    return e;
}
bool terminal::is_success()const { return last_err.what().empty(); }

bool terminal::update_scr_mode()
{
#if __IS_WIN
    _ENSURE(GetConsoleMode(hstdin, &stdin_nowmode)) (hstdin); _CHECK_ED();
    _ENSURE(GetConsoleMode(hstdout, &stdout_nowmode)) (hstdout); _CHECK_ED();
    _ENSURE(GetConsoleMode(hstderr, &stderr_nowmode)) (hstderr); _CHECK_ED();
    stdin_oldmode = stdin_nowmode;
    stdout_oldmode = stdout_nowmode;
    stderr_oldmode = stderr_nowmode;
#endif
    return true;
}
bool terminal::restore_scr_mode()
{
#if __IS_WIN
    _ENSURE(SetConsoleMode(hstdin, stdin_oldmode)) (hstdin); _CHECK_ED();
    _ENSURE(SetConsoleMode(hstdout, stdout_oldmode)) (hstdout); _CHECK_ED();
    _ENSURE(SetConsoleMode(hstderr, stderr_oldmode)) (hstderr); _CHECK_ED();
#endif
    return true;
}

bool terminal::init()
{
#if __IS_WIN
    _ENSURE((hstdin = GetStdHandle(STD_INPUT_HANDLE)) != INVALID_HANDLE_VALUE);
    _CHECK_ED();
    _ENSURE((hstdout = GetStdHandle(STD_OUTPUT_HANDLE)) != INVALID_HANDLE_VALUE);
    _CHECK_ED();
    _ENSURE((hstderr = GetStdHandle(STD_ERROR_HANDLE)) != INVALID_HANDLE_VALUE);
    _CHECK_ED();
    curout_screen = hstdout;
    if (!update_scr_mode())return false;
    _ENSURE_NONUL(alternate_screen = CreateConsoleScreenBuffer(
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, CONSOLE_TEXTMODE_BUFFER, NULL
    )); _CHECK_ED();
    _ENSURE(SetConsoleMode(hstdin, stdin_oldmode & (~ENABLE_PROCESSED_INPUT)))
        (hstdin); _CHECK_ED();
#else
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag &= ~(ISIG);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
#endif
    return true;
}
void terminal::exit()
{
    if (is_exit)return;
}

#if __IS_WIN
static int ter_vhprintf(HANDLE scr, const char* format, va_list args)
{
    va_list t;
    size_t need;
    int res;
    char* buf;
    char sbuf[1024];
    va_copy(t, args);
    need = vsnprintf(NULL, 0, format, t);
    if (need < 1023)buf = sbuf;
    else buf = (char*)malloc(need + 1);
    res = vsnprintf(buf, need + 1, format, args);

    WriteConsoleA(scr, buf, (DWORD)need, NULL, NULL);
    if (buf != NULL && buf != sbuf)
        free(buf);
    va_end(t);
    return res;
}
static int ter_hprintf(HANDLE scr, const char* format, ...)
{
    int res;
    va_list ap;
    va_start(ap, format);
    res = ter_vhprintf(scr, format, ap);
    va_end(ap);
    return res;
}
int terminal::printf(const char* format, ...)
{
    va_list ap;
    int res;
    va_start(ap, format);
    res = ter_vhprintf(curout_screen, format, ap);
    va_end(ap);
    return res;
}
#else
int terminal::printf(const char* format, ...)
{
    va_list ap;
    int res;
    va_start(ap, format);
    res = vprintf(format, ap);
    va_end(ap);
    return res;
}
#endif

terminal::tersize terminal::size() const { return last_tersiz; }
terminal::update_res terminal::update_size()
{
    int r, c;
#if __IS_WIN
    CONSOLE_SCREEN_BUFFER_INFO info;
    _ENSURE(GetConsoleScreenBufferInfo(curout_screen, &info)) (curout_screen);
    if (!is_success())return update_res::FAILED;
    r = info.srWindow.Bottom - info.srWindow.Top + 1;
    c = info.srWindow.Right - info.srWindow.Left + 1;
#else
    struct winsize w;
    _ENSURE(ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0);
    if (!is_success())return update_res::FAILED;
    r = w.ws_row;
    c = w.ws_col;
#endif
    if (r == last_tersiz.row && c == last_tersiz.col)
        return update_res::NOCHANGED;
    last_tersiz = tersize(r, c);
    return update_res::SUCCESS;
}
bool terminal::resize(tersize dest)
{
#if __IS_WIN
    CONSOLE_SCREEN_BUFFER_INFO info;
    SMALL_RECT rc;
    COORD oldsize;

    rc.Left = rc.Top = 0;
    rc.Right = dest.col - 1, rc.Bottom = dest.row - 1;
    _ENSURE(GetConsoleScreenBufferInfo(curout_screen, &info))(curout_screen);
    _CHECK_ED();
    oldsize = info.dwSize;
    /* 调整窗口大小需确保窗口缓冲区不小于目标大小 */
    if (oldsize.Y < dest.col || oldsize.X < dest.row)
    {
        if (oldsize.X < dest.col)oldsize.X = dest.col;
        if (oldsize.Y < dest.row)oldsize.Y = dest.row;
        _ENSURE(SetConsoleScreenBufferSize(curout_screen, oldsize))
            (curout_screen); _CHECK_ED();
    }
    _ENSURE(SetConsoleWindowInfo(curout_screen, TRUE, &rc))(curout_screen)(rc);
    _CHECK_ED();
    /* 调整缓冲区大小指合适大小以隐藏滚动条 */
    oldsize.X = dest.col, oldsize.Y = dest.row;
    _ENSURE(SetConsoleScreenBufferSize(curout_screen, oldsize))
        (curout_screen)(oldsize); _CHECK_ED();
#else
    /* struct winsize w;
    memset(&w, 0, sizeof(w));
    w.ws_col = , w.ws_row = ;
    ioctl(STDOUT_FILENO, TIOCSWINSZ, &w); */
    ter_seq_resize(dest.row, dest.col);
#endif
    return true;
}

bool terminal::is_enable_echo()
{
#if __IS_WIN
    return stdin_nowmode & ENABLE_ECHO_INPUT;
#else
    struct termios t;
    _ENSURE(tcgetattr(STDIN_FILENO, &t) != 0); _CHECK_ED();
    return t.c_lflag & ECHO;
#endif
}
bool terminal::enable_echo(bool flag)
{
#if __IS_WIN
    DWORD mode = stdin_nowmode;
    if (flag) mode |= ENABLE_ECHO_INPUT;
    else mode &= (~ENABLE_ECHO_INPUT);
    _ENSURE(SetConsoleMode(hstdin, mode))(hstdin); _CHECK_ED();
    /* 确保成功后在更新 */
    stdin_nowmode = mode;
#else
    struct termios t;
    _ENSURE(tcgetattr(STDIN_FILENO, &t) == 0); _CHECK_ED();
    if (flag) t.c_lflag |= ECHO;
    else t.c_lflag &= ~ECHO;
    _ENSURE(tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0); _CHECK_ED();
#endif
    return true;
}
bool terminal::is_enable_linemode()
{
#if __IS_WIN
    return stdin_nowmode & ENABLE_LINE_INPUT;
#else
    struct termios t;
    _ENSURE(tcgetattr(STDIN_FILENO, &t) == 0);
    return t.c_lflag & ICANON;
#endif
}
bool terminal::enable_linemode(bool flag)
{
#if __IS_WIN
    DWORD mode = stdin_nowmode;
    if (flag) mode |= ENABLE_LINE_INPUT;
    else mode &= (~ENABLE_LINE_INPUT);
    _ENSURE(SetConsoleMode(hstdin, mode))(hstdin); _CHECK_ED();
    stdin_nowmode = mode;
#else
    struct termios t;
    _ENSURE(tcgetattr(STDIN_FILENO, &t) == 0); _CHECK_ED();
    if (flag) t.c_lflag |= ICANON;
    else t.c_lflag &= ~ICANON;
    _ENSURE(tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0); _CHECK_ED();

#endif
    return true;
}
bool terminal::enable_mouse_input(bool flag)
{
#if __IS_WIN
    if (is_use_seq)
    {
        flag ? ter_seq_enable_mouse() : ter_seq_disable_mouse();
        return true;
    }
    DWORD mode = stdin_nowmode;
    if (flag)
    {
        mode |= ENABLE_MOUSE_INPUT;
        mode &= (~ENABLE_QUICK_EDIT_MODE);
    }
    else
    {
        mode &= (~ENABLE_MOUSE_INPUT);
        if (stdin_oldmode & ENABLE_QUICK_EDIT_MODE)
            mode |= ENABLE_QUICK_EDIT_MODE;
    }
    _ENSURE(SetConsoleMode(hstdin, mode))(hstdin); _CHECK_ED();
    stdin_nowmode = mode;
#else
    if (flag) ter_seq_enable_mouse();
    else ter_seq_disable_mouse();
#endif
    return true;
}
bool terminal::enable_mouse(bool flag)
{
    if (!enable_mouse_input(flag))return false;
    is_use_mouse = flag;
    return true;
}
bool terminal::is_enable_mouse() const { return is_use_mouse; }
bool terminal::enable_seq(bool flag)
{
#if __IS_WIN
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#pragma message("ENABLE_VIRTUAL_TERMINAL_PROCESSING not defined!")
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#pragma message("ENABLE_VIRTUAL_TERMINAL_INPUT not defined!")
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
    
    DWORD inmode = stdin_oldmode;
    DWORD outmode = stdout_oldmode;
    DWORD errmode = stderr_oldmode;
    if (flag)
    {
       inmode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
       //inmode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
       outmode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
       errmode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    }
    else
    {
        inmode &= (~ENABLE_VIRTUAL_TERMINAL_INPUT);
        outmode &= (~ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        errmode &= (~ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    _ENSURE(SetConsoleMode(hstdin, inmode))(hstdin); _CHECK_ED();
    stdin_oldmode = inmode;
    _ENSURE(SetConsoleMode(hstdout, outmode))(hstdout); _CHECK_ED();
    stdout_oldmode = outmode;
    _ENSURE(SetConsoleMode(hstderr, errmode))(hstderr); _CHECK_ED();
    stderr_oldmode = errmode;
    is_use_seq = flag;
#endif
    return true;
}

bool terminal::active_screen(void* scr)
{
#if __IS_WIN
    HANDLE h = (HANDLE)scr;
    _ENSURE(SetConsoleActiveScreenBuffer(h))(h); _CHECK_ED();
    curout_screen = h;
    return update_size() != update_res::FAILED;
#endif
    return true;
}
bool terminal::active_main_screen()
{
#if __IS_WIN
    if (!restore_scr_mode())return false;
    if (!active_screen(hstdout))return false;
#else
    ter_seq_active_main_screen();
    if (!enable_echo(true))return false;
    if (!enable_linemode(true))return false;
    if (is_use_mouse)return enable_mouse_input(false);
#endif
    return true;
}
bool terminal::active_alternate_screen()
{
#if __IS_WIN
    if (!update_scr_mode())return false;
    if (!active_screen(alternate_screen))return false;
    _ENSURE(FlushConsoleInputBuffer(hstdin))(hstdin); _CHECK_ED();
#else
    ter_seq_active_alternate_screen();
#endif
    if (!enable_echo(false))return false;
    if (!enable_linemode(false))return false;
    if (is_use_mouse)return enable_mouse_input(true);
    return true;
}

static const char* simple_key_name(terminal::keyval val)
{
#define _CASE(_s) case terminal::KEY_##_s: return #_s
    switch (val)
    {
        _CASE(F1); _CASE(F2); _CASE(F3); _CASE(F4);
        _CASE(F5); _CASE(F6); _CASE(F7); _CASE(F8);
        _CASE(F9); _CASE(F10); _CASE(F11); _CASE(F12);

        _CASE(CTRL_A); _CASE(CTRL_B); _CASE(CTRL_C); _CASE(CTRL_D);
        _CASE(CTRL_E); _CASE(CTRL_F); _CASE(CTRL_G); _CASE(BACKSPACE);
        _CASE(TAB); _CASE(ENTER); _CASE(CTRL_K); _CASE(CTRL_L);
        _CASE(CTRL_M); _CASE(CTRL_N); _CASE(CTRL_O); _CASE(CTRL_P);
        _CASE(CTRL_Q); _CASE(CTRL_R); _CASE(CTRL_S); _CASE(CTRL_T);
        _CASE(CTRL_U); _CASE(CTRL_V); _CASE(CTRL_W); _CASE(CTRL_X);
        _CASE(CTRL_Y); _CASE(CTRL_Z);

        _CASE(ESC);

    case terminal::KEY_FS:return "CTRL_\\";
    case terminal::KEY_GS:return "CTRL_]";
    case terminal::KEY_RS:return "CTRL_^";
    case terminal::KEY_US:return "CTRL__";

        _CASE(CTRL_BACKSPACE);
        _CASE(INSERT);
        _CASE(DELETE);
        _CASE(PAGE_UP); _CASE(PAGE_DOWN);

        _CASE(UP); _CASE(DOWN); _CASE(RIGHT); _CASE(LEFT);

        _CASE(HOME); _CASE(END);

    default:
        break;
    }
    if (32 <= val && val <= 126)
    {
        static char buf[10] = "";
        buf[0] = (char)val;
        buf[1] = 0;
        return buf;
    }
    return "<?>";
#undef _CASE
}
const char* terminal::key_name(keyval val)
{
    static char buffer[500];
    buffer[0] = 0;
#if __IS_WIN
#define strcat strcat_s
#endif
    if (val & OP_WITH_CTRL)
        strcat(buffer, "CTRL_"), val &= ~OP_WITH_CTRL;
    if (val & OP_WITH_ALT)
        strcat(buffer, "ALT_"), val &= ~OP_WITH_ALT;
    if (val & OP_WITH_SHIFT)
        strcat(buffer, "SHIFT_"), val &= ~OP_WITH_SHIFT;
    strcat(buffer, simple_key_name(val));
#if __IS_WIN
#undef strcat
#endif
    return buffer;
}


int terminal::getchar_no_block()
{
#if __IS_WIN
    if (!is_use_seq)return -2;
    INPUT_RECORD inr;
    DWORD n;
    if (!PeekConsoleInputA(hstdin, &inr, 1, &n))return -1;
    if (!n)return -1;
    if (inr.EventType != KEY_EVENT)return -1;
    if (!ReadConsoleInputA(hstdin, &inr, 1, &n))return -1;
    if (!n)return -1;
    if (!inr.Event.KeyEvent.bKeyDown)return -1;
    return inr.Event.KeyEvent.uChar.AsciiChar;
#else
#endif
    return -1;
}

bool terminal::try_get_event_form_seq(op_event& e)
{
    //int ch = getchar_no_block();

    return false;// true;
}

#if __IS_WIN
static int vk_to_idx(int vk)
{
    if (
        vk == VK_CONTROL
        || vk == VK_SHIFT
        || vk == VK_MENU
        || vk == VK_CAPITAL
        )return -1;
    if (VK_F1 <= vk && vk <= VK_F12)return terminal::KEY_F1 + vk - VK_F1;
    if ('A' <= vk && vk <= 'Z')return vk;
#define _CASE(key) case VK_##key:return terminal::KEY_##key
    switch (vk)
    {
    _CASE(TAB);
    _CASE(END);
    _CASE(HOME);
    _CASE(LEFT);
    _CASE(UP);
    _CASE(RIGHT);
    _CASE(DOWN);
    _CASE(INSERT);
    _CASE(DELETE);
    case VK_BACK:return terminal::KEY_BACKSPACE;
    case VK_PRIOR:return terminal::KEY_PAGE_UP;
    case VK_NEXT:return terminal::KEY_PAGE_DOWN;
    }
#undef _CASE
    return -1;
}
void terminal::wait_a_event(op_event& e)
{
    DWORD n;
    INPUT_RECORD inr;
    DWORD tmp;
    while(1)
    {
        //e.type = op_type::FAILED;
        if (update_size() == update_res::SUCCESS)
        {
            e.type = op_type::SIZE;
            e._size = size();
            COORD tmp;
            tmp.X = e._size.col, tmp.Y = e._size.row;
            SetConsoleScreenBufferSize(curout_screen, tmp);
            return;
        }

        Sleep(1);

        if (!GetNumberOfConsoleInputEvents(hstdin, &n))continue;
        if (n == 0)continue;
        if (!ReadConsoleInputA(hstdin, &inr, 1, &n))continue;
        if (n == 0)continue;

        switch (inr.EventType)
        {
        case MOUSE_EVENT:
        {
            CONSOLE_SCREEN_BUFFER_INFO info;
            flag_set flags;
            if (!GetConsoleScreenBufferInfo(curout_screen, &info))continue;
            e.type = op_type::MOUSE;
            e._mouse.pos = coord(
                inr.Event.MouseEvent.dwMousePosition.Y - info.srWindow.Top + 1,
                inr.Event.MouseEvent.dwMousePosition.X - info.srWindow.Left + 1
            );
            tmp = inr.Event.MouseEvent.dwButtonState;
            flags = 0;
            if (tmp & FROM_LEFT_1ST_BUTTON_PRESSED)
                flags = MOUSE_LBTN | OP_WITH_MOUSE_DOWN;
            else if (tmp & FROM_LEFT_2ND_BUTTON_PRESSED)
                flags = MOUSE_MBTN | OP_WITH_MOUSE_DOWN;
            else if (tmp & RIGHTMOST_BUTTON_PRESSED)
                flags = MOUSE_RBTN | OP_WITH_MOUSE_DOWN;
            else if (last_mouse_down_idx)
            {
                /* 之前按下了但现在没有按下, 即松开按键 */
                flags = last_mouse_down_idx;
                last_mouse_down_idx = 0;
            }
            if (flags & OP_WITH_MOUSE_DOWN)last_mouse_down_idx = flags & 3;

            tmp = inr.Event.MouseEvent.dwEventFlags;
            if (tmp & MOUSE_MOVED)
            {
                if (last_mouse_pos == e._mouse.pos)continue;
                last_mouse_pos = e._mouse.pos;
                flags |= OP_WITH_MOUSE_MOVED;
            }
            if (tmp & MOUSE_WHEELED)
            {
                flags |= (((signed long)inr.Event.MouseEvent.dwButtonState) < 0);
                flags |= OP_WITH_MOUSE_WHEELED;
            }
#if defined MOUSE_HWHEELED
            /* 不响应横向滚动 */
            if (tmp & MOUSE_HWHEELED)continue;
#endif
            tmp = inr.Event.MouseEvent.dwControlKeyState;
            if (tmp & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
                flags |= OP_WITH_ALT;
            if (tmp & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
                flags |= OP_WITH_CTRL;
            if (tmp & SHIFT_PRESSED)flags |= OP_WITH_SHIFT;
            e._mouse.flags = flags;
            return;
        }
        case KEY_EVENT:
        {
            if (!inr.Event.KeyEvent.bKeyDown)continue;
            WORD vk = inr.Event.KeyEvent.wVirtualKeyCode;
            int ch = inr.Event.KeyEvent.uChar.UnicodeChar;
            tmp = inr.Event.KeyEvent.dwControlKeyState;
            if (ch == 0)
            {
                ch = vk_to_idx(vk);
                if (ch == -1)continue;
                if (tmp & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
                    ch |= OP_WITH_CTRL;
                if (tmp & SHIFT_PRESSED)
                    if (vk < 'A' || vk>'Z') ch |= OP_WITH_SHIFT;
            }
            else
            {
                if (ch == KEY_ESC)
                {
                    if (try_get_event_form_seq(e))return;
                }
            }
            if (tmp & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
                ch |= OP_WITH_ALT;
            e._key = ch;
            e.type = op_type::KEY;
            return;
        }
        default:
            continue;
        }
    }
}
#else
void terminal::wait_a_event(op_event& e)
{
    e.type = op_type::KEY;
    e._key = getchar();
}
#endif