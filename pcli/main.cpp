// pcli.cpp: 定义应用程序的入口点。
//


#include "pcli/pcli.h"
#include "pcli/pcli_private.h"
#include <iostream>
#include <thread>

#   include <unistd.h>
#   include <termios.h>
#   include <sys/ioctl.h>
#include <signal.h>

using namespace std;
using namespace __pcli_private;

static void out_error()
{
    pcli::error_detail ed;
    pcli::get_last_error(ed);
    printf("%s\n_", ed.what().c_str());
}

const char* ter_event_info(const terminal::op_event& in_op);

int main()
{
    terminal t;
    t.init();
    if (!t.enable_seq())
    {
        t.printf("%s", t.last_error().what().c_str());
    }
    if (!t.active_alternate_screen())
    {
        printf("%s", t.last_error().what().c_str());
    }
    t.update_size();
    auto s = t.size();
    std::cout << s.row << " " << s.col << std::endl;
    t.enable_mouse();

    //signal(SIGINT, SIG_IGN);

    while (1)
    {
        terminal::op_event e;
        t.wait_a_event(e);
        if (e.type == terminal::op_type::KEY)
            if (e._key == terminal::KEY_CTRL_G)
                break;
            else t.printf("%s", t.key_name(e._key));
    }
    if (!t.active_main_screen())
    {
        printf("%s", t.last_error().what().c_str());
    }
    return 0;
}
