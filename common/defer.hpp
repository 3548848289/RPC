#pragma once
//Defer 主要用于确保某些操作在函数退出时始终执行（例如释放资源、解锁互斥锁、打印日志等），
//即使在函数执行过程中发生了异常，也能保证操作被执行。
#include <functional>
namespace Common {
    class Defer {
    public:
        Defer(std::function<void(void)> func) : func_(func) {}
        ~Defer() { func_(); }

    private:
        std::function<void(void)> func_;
    };
} // namespace Common