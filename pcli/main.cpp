// pcli.cpp: 定义应用程序的入口点。
//


#include "pcli/pcli.h"
#include "pcli/pcli_private.h"
#include <iostream>
#include <thread>

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

    while (1)
    {
        terminal::op_event e;
        t.wait_a_event(e);
        if (e.type == terminal::op_type::KEY)
            if (e._key == terminal::KEY_CTRL_K)
                break;
        switch (e.type)
        {
        case terminal::op_type::KEY:
        {
            t.printf("%s", t.key_name(e._key));
            break;
        }
        case terminal::op_type::MOUSE:
        {
            t.printf("%s\n", ter_event_info(e));
            break;
        }
        default:
            break;
        }
    }
    if (!t.active_main_screen())
    {
        printf("%s", t.last_error().what().c_str());
    }
    return 0;
}
