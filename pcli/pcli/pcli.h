
#ifndef _INCLUDE_PCLI_
#define _INCLUDE_PCLI_

#include <thread>
#include <mutex>
#include <exception>

#define __IS_DEBUG 1

#if defined _WIN32
#   define __IS_WIN 1
#else
#   define __IS_WIN 0
#endif

namespace pcli
{
    class error_detail
    {
    public:
        error_detail() noexcept {}
        error_detail(const std::string& in_what) :what_info(in_what) {}
        std::string what()const noexcept { return what_info; }
    private:
        std::string what_info;
    };
    void get_last_error(error_detail& out_error);

};

#endif