#include "pcli_private.h"

using namespace __pcli_private;

static std::mutex g_mutex;
static error_detail g_detail;

void __pcli_private::set_last_error(const error_detail& in_error)
{
    g_mutex.lock();
    g_detail = in_error;
    g_mutex.unlock();
}
void pcli::get_last_error(error_detail& out_error)
{
    g_mutex.lock();
    out_error = g_detail;
    g_mutex.unlock();
}