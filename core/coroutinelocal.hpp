#pragma once
// 协程本地变量模板类（CoroutineLocal），用于在协程中存储特定于协程的局部数据。
// 每个协程都可以有独立的本地数据，而这些数据是与协程的生命周期相关的。
// 该类的功能是为每个协程提供独立的局部变量，类似于线程局部存储（TLS）。
// 每个协程都有自己的数据副本，避免了多个协程之间的冲突。

#include "coroutine.h"
namespace Core { // 协程本地变量模版类
template <class Type>
class CoroutineLocal {
public:
    static void FreeLocal(void *data) {
        if (data)
            delete (Type *)data;
    }
    void Set(Type value) {
        Type *temp = new Type;
        *temp = value;
        MyCoroutine::LocalData localData {
            .data = temp,
            .freeEntry = FreeLocal,
        };
        MyCoroutine::CoroutineLocalSet(SCHEDULE, this, localData);
    }
    Type &Get() {
        MyCoroutine::LocalData localData;
        bool result = MyCoroutine::CoroutineLocalGet(SCHEDULE, this, localData);
        assert(result == true);
        return *(Type *)localData.data;
    }
};
} // namespace Core