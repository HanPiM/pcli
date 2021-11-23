#include "pcli_private.h"
using namespace __pcli_private;

const char* ter_event_info(const terminal::op_event& in_op)
{
    static char buffer[510] = "";
    switch (in_op.type)
    {
    case terminal::op_type::KEY:
        snprintf(
            buffer, sizeof(buffer),
            "{event:key %s pressed}",
            terminal::key_name(in_op._key)
        );
        break;
    case terminal::op_type::MOUSE:
        snprintf(
            buffer, sizeof(buffer),
            "{event:mouse at (%d, %d) with ",
            in_op._mouse.pos.row, in_op._mouse.pos.col
        );
        //if (in_op.data._mouse.tag)
        {
            int flags = in_op._mouse.flags;
            if (flags & terminal::OP_WITH_MOUSE_MOVED)
                strcat(buffer, "{NewPos}");
            if (flags & terminal::OP_WITH_ALT)
                strcat(buffer, "{Key-Alt}");
            if (flags & terminal::OP_WITH_CTRL)
                strcat(buffer, "{Key-Ctrl}");
            if (flags & terminal::OP_WITH_SHIFT)
                strcat(buffer, "{Key-Shift}");
            if (flags & terminal::OP_WITH_MOUSE_WHEELED)
                strcat(buffer, (flags & 3) ? "{ScrollDown}" : "{ScrollUp}");
            else
            {
                switch (flags & 3)
                {
                case terminal::MOUSE_LBTN:
                    strcat(buffer, "{LBtn"); break;
                case terminal::MOUSE_MBTN:
                    strcat(buffer, "{MBtn"); break;
                case terminal::MOUSE_RBTN:
                    strcat(buffer, "{RBtn"); break;
                //default:
                    //strcat(buffer, "{NoBtnDown}"); break;
                }
                if (flags & 3)
                {
                    if (flags & terminal::OP_WITH_MOUSE_DOWN)
                        strcat(buffer, "Down}");
                    else strcat(buffer, "Up}");
                }
            }
        }
        break;
    case terminal::op_type::SIZE:
        snprintf(
            buffer, sizeof(buffer),
            "{event:size change to %dx%d}",
            in_op._size.row, in_op._size.col
        );
        break;
    default:
        return "{event:unknown}";
        break;
    }
    return buffer;
}