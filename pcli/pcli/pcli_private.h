#include <functional>
#include <string>
#include <sstream>
#include <string.h>

#include "pcli.h"

#if __IS_WIN
#   include <Windows.h>
#endif

#define __NON_EXPORT static

namespace __pcli_private
{
    using namespace pcli;
    /**
     * @brief 利用 RAII + lambda 在作用域结束后进行清理
     */
    class scope_guard
    {
    public:
        explicit scope_guard(std::function<void()> in_on_exit)
            : m_on_exit(in_on_exit), m_flag(true) {}
        ~scope_guard() { if (m_flag) m_on_exit(); }

        scope_guard(const scope_guard&) = delete;
        scope_guard& operator=(const scope_guard&) = delete;
        /**
         * @brief 在结束后不调用目标函数清理
        */
        void close() { m_flag = false; }
    private:
        std::function<void()> m_on_exit;
        bool m_flag;
    };

    class ensure_detail
    {
    public:
        struct __flag_struct { int unused = 0; };
        ensure_detail() {}
        ensure_detail(const char* in_func, int in_line, const char* in_expr)
        {
            std::ostringstream os;
            os << in_func << '(' << in_line << "):ensure_exp_failed:" << in_expr
                << '\n';
            m_what = os.str();
        }
        ensure_detail& operator << (const __flag_struct&) { return *this; }
        template<typename T>
        ensure_detail& operator << (const std::pair<const char*, T>& in_p)
        {
            m_what += (' ' + std::string(in_p.first) + ':' +
                to_string(in_p.second) + '\n');
            return *this;
        }
        std::string what()const { return m_what; }
        operator error_detail()const { return error_detail(m_what); }
    private:
        std::string to_string(void* p)
        {
            char buf[20];
            snprintf(buf, 20, "%p", p);
            return buf;
        }
#if __IS_WIN
        std::string to_string(const SMALL_RECT& src)
        {
            char buf[200];
            snprintf(
                buf, 200, "{T:%d,L:%d,B:%d,R:%d}",
                src.Top, src.Left, src.Bottom, src.Right);
            return buf;
        }
        std::string to_string(const COORD& pos)
        {
            char buf[100];
            snprintf(buf, 100, "{X:%d,Y:%d}", pos.X, pos.Y);
            return buf;
        }
#endif
        template <typename _T> std::string to_string(const _T& x)
        {
            return std::to_string(x);
        }
        std::string m_what;
    };

    static constexpr ensure_detail::__flag_struct
        __ENSURE_TRY_GET_PAR_1 = ensure_detail::__flag_struct(),
        __ENSURE_TRY_GET_PAR_2 = ensure_detail::__flag_struct();
#define __ENSURE_ADD(v,next) std::make_pair(#v,v)<<next
#define __ENSURE_TRY_GET_PAR_1(exp) __ENSURE_ADD(exp,__ENSURE_TRY_GET_PAR_2)
#define __ENSURE_TRY_GET_PAR_2(exp) __ENSURE_ADD(exp,__ENSURE_TRY_GET_PAR_1)
    /*
    * 如果表达式为真会返回空的信息
    */
#define ENSURE(expr) (expr)\
    ?ensure_detail()\
    :ensure_detail(__FUNCTION__, __LINE__, #expr) << __ENSURE_TRY_GET_PAR_1

    void set_last_error(const error_detail& in_error);

    typedef unsigned int flag_set;

    /* (调用者应确保其线程安全) */
    class terminal final
    {
    public:

        struct coord
        {
            unsigned short row = 0, col = 0;
            coord() {};
            coord(int r, int c) :row(r), col(c) {}
            bool operator == (const coord& x)
            { return row == x.row && col == x.col; }
        };
        using tersize = coord;
        enum _keyval_basic_tag
        {
            /* ACSII : 0-127 */
            /* \0(^@) : 0 */
            KEY_NUL,
            /* Ctrl+A -> Ctrl+Z : 1->26 */
            KEY_CTRL_A, KEY_CTRL_B, KEY_CTRL_C, KEY_CTRL_D,
            KEY_CTRL_E, KEY_CTRL_F, KEY_CTRL_G,
            KEY_BACKSPACE, /*Ctrl+H*/
            KEY_TAB, /* Ctrl+I */
            KEY_ENTER, /* Ctrl+J */
            KEY_CTRL_K, KEY_CTRL_L, KEY_CTRL_M, KEY_CTRL_N,
            KEY_CTRL_O, KEY_CTRL_P, KEY_CTRL_Q, KEY_CTRL_R,
            KEY_CTRL_S, KEY_CTRL_T, KEY_CTRL_U, KEY_CTRL_V,
            KEY_CTRL_W, KEY_CTRL_X, KEY_CTRL_Y, KEY_CTRL_Z,
            /* Ctrl+... */
            KEY_ESC,/* Ctrl+[ */
            KEY_FS, /* Ctrl+\ */
            KEY_GS, /* Ctrl+] */
            KEY_RS, /* Ctrl+^ */
            KEY_US, /* Ctrl+_ */
            /* ' '->'~' : 32->126 : 可见字符 */
            /* 退格 */
            KEY_CTRL_BACKSPACE = 127,

            /* 功能键 */
            KEY_INSERT = 256,
            KEY_DELETE,
            KEY_PAGE_UP,
            KEY_PAGE_DOWN,
            KEY_F1, KEY_F2, KEY_F3, KEY_F4,
            KEY_F5, KEY_F6, KEY_F7, KEY_F8,
            KEY_F9, KEY_F10, KEY_F11, KEY_F12,
            /* 方向键 */
            KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT,
            KEY_HOME, KEY_END
        };
        typedef unsigned int keyval;
        enum class op_type
        {
            FAILED, MOUSE, KEY, SIZE
        };
        enum _op_flag
        {
            OP_WITH_CTRL = 1 << 20,
            OP_WITH_ALT = 1 << 21,
            OP_WITH_SHIFT = 1 << 22,

            OP_WITH_MOUSE_DOWN = 1 << 23,
            OP_WITH_MOUSE_MOVED = 1 << 24,
            OP_WITH_MOUSE_WHEELED = 1 << 25
        };
        enum _op_mouse_idx
        {
            MOUSE_LBTN = 1,
            MOUSE_MBTN = 2,
            MOUSE_RBTN = 3
        };
        struct op_event
        {
            op_type type = op_type::FAILED;
            union
            {
                struct
                {
                    coord pos;
                    flag_set flags = 0;
                }_mouse;
                keyval _key = 0;
                tersize _size;
            };
            op_event() {};
        };

        enum class update_res
        {
            FAILED, SUCCESS, NOCHANGED
        };

        terminal();
        ~terminal();

        terminal(const terminal&) = delete;
        terminal& operator=(const terminal&) = delete;

        static const char* key_name(keyval val);

        bool is_success()const;
        error_detail last_error();

        void exit();

        void wait_a_event(op_event& e);

        bool enable_mouse(bool flag = true);
        bool is_enable_mouse()const;
        /* 只在 Windows 下有效. 必须在调用其他(除初始化)函数前调用 */
        bool enable_seq(bool flag = true);

        update_res update_size();
        tersize size()const;
        bool resize(tersize dest);

        bool active_main_screen();
        bool active_alternate_screen();

        int printf(const char* format, ...);

        bool init();

    private:

        void clear();
        bool update_scr_mode();
        bool restore_scr_mode();
        bool active_screen(void* scr);

        bool enable_echo(bool flag = true);
        bool enable_linemode(bool flag = true);
        bool enable_mouse_input(bool flag = true);
        bool is_enable_echo();
        bool is_enable_linemode();

        /* 只适用于 Linux 或启用了虚拟终端序列的 Windows */
        int getchar_no_block();
        bool try_get_event_form_seq(op_event& e);

        error_detail last_err;

#if __IS_WIN
        DWORD stdin_oldmode, stdout_oldmode, stderr_oldmode;
        DWORD stdin_nowmode, stdout_nowmode, stderr_nowmode;
        HANDLE hstdin, hstdout, hstderr;
        HANDLE alternate_screen, draw_buffer_screen;
        HANDLE curout_screen;
        bool is_use_seq;
#else

#endif
        bool is_use_mouse;

        bool is_exit;

        char last_mouse_down_idx;
        coord last_mouse_pos;
        tersize last_tersiz;
    };
    
}
