#pragma once

#include <errno.h>
#include <unistd.h>

#include "../common/defer.hpp"
#include "../common/log.hpp"
#include "../common/singleton.hpp"
#include "../common/sockopt.hpp"
#include "../common/timedeal.hpp"
#include "../common/utils.hpp"
#include "coroutinelocal.hpp"
#include "epollctl.hpp"
#include "routeinfo.hpp"
#include "timer.hpp"

#define SYSTEM Common::Singleton<Core::System>::Instance()
extern Core::CoroutineLocal<int> EpollFd;
extern Core::CoroutineLocal<Core::TimeOut> RpcTimeOut;

namespace Core {
// 测试环境： 数据进入 mock 实现，可以控制、拦截、验证。
class SocketIoMock {
public:
    virtual ssize_t read(int fd, void *buf, size_t count) = 0;
    virtual ssize_t write(int fd, const void *buf, size_t count) = 0;
    virtual int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) = 0;
};

//生产环境： 数据写到实际的 socket 或文件。
class System {
public:
    void SetIoMock(SocketIoMock *socketIoMock) { socket_io_mock_ = socketIoMock; }
    ssize_t read(int fd, void *buf, size_t count) {
        if (not socket_io_mock_)
            return ::read(fd, buf, count);
        return socket_io_mock_->read(fd, buf, count);
    }
    ssize_t write(int fd, const void *buf, size_t count) {
        if (not socket_io_mock_)
            return ::write(fd, buf, count);
        return socket_io_mock_->write(fd, buf, count);
    }
    int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
        if (not socket_io_mock_)
            return ::connect(sockfd, addr, addrlen);
        return socket_io_mock_->connect(sockfd, addr, addrlen);
    }
private:
    SocketIoMock *socket_io_mock_{nullptr};
};

typedef struct TimeOutData {
    int cid_;
    bool time_out_{false};
} TimeOutData;

inline void TimeOutCallBack(void *data) {
    TimeOutData *timeOutData = (TimeOutData *)data;
    timeOutData->time_out_ = true;
    MyCoroutine::CoroutineResumeById(SCHEDULE, timeOutData->cid_); // 超时之后，直接唤醒协程
}

inline ssize_t CoRead(int fd, void *buf, size_t size, bool useInnerEventData = true)
{
    EventData eventData(fd, EpollFd.Get(), RPC_CLIENT);
    eventData.cid_ = MyCoroutine::ScheduleGetRunCid(SCHEDULE);
    if (useInnerEventData)
        EpollCtl::AddReadEvent(eventData.epoll_fd_, eventData.fd_, &eventData);
    TimeOutData timeOutData;
    timeOutData.cid_ = MyCoroutine::ScheduleGetRunCid(SCHEDULE);
    int64_t timerId = TIMER.Register(TimeOutCallBack, &timeOutData, RpcTimeOut.Get().read_time_out_ms_);
    Common::Defer defer([&timeOutData, &eventData, useInnerEventData, timerId]() {
        if (useInnerEventData) 
            EpollCtl::ClearEvent(eventData.epoll_fd_, eventData.fd_, false);
        if (not timeOutData.time_out_) {
            TIMER.Cancel(timerId);  // 定时器不超时，则取消定时器
        }
    });

    while (true) {
        ssize_t ret = SYSTEM.read(fd, buf, size);
        if (ret >= 0)
            return ret; // 读成功
        if (EINTR == errno)
            continue; // 调用被中断，则直接重启read调用
        if (EAGAIN == errno or EWOULDBLOCK == errno) { // 暂时不可读
            MyCoroutine::CoroutineYield(SCHEDULE);     // 让出cpu，切换到主协程，等待下一次数据可读
            if (timeOutData.time_out_) { // 读超时了
                errno = EAGAIN;
                return -1; // 读超时，返回-1，并把errno设置为EAGAIN
            }
            continue;
        }
        return ret; // 读失败
    }
}

/*
CoWrite 协程写操作的流程说明：
1. 创建 epoll 事件对象（EventData），记录当前协程 ID。
2. 如果启用了内部事件监听（useInnerEventData，则将写事件加入到 epoll，等待 fd 可写。
3. 注册一个写操作的超时定时器（写超时配置来自 RpcTimeOut）。
4. 使用 Defer 机制确保协程结束时做清理操作： - 清除 epoll 写事件（如果启用）- 如果没有超时，则取消定时器
5. 进入写循环：
   a. 尝试直接 write，如果成功，直接返回写入字节数。
   b. 如果被中断（EINTR），重试。
   c. 如果暂时不可写（EAGAIN/EWOULDBLOCK）：
      - 调用 CoroutineYield 让出 CPU，挂起当前协程
      - 等待 epoll 通知或者定时器触发
      - 如果是超时唤醒，则设置 errno 为 EAGAIN 并返回 -1。
   d. 其他错误，直接返回 write 的错误码。
*/
inline ssize_t CoWrite(int fd, const void *buf, size_t size, bool useInnerEventData = true) {
    // 准备事件监听数据
    EventData eventData(fd, EpollFd.Get(), RPC_CLIENT);
    eventData.cid_ = MyCoroutine::ScheduleGetRunCid(SCHEDULE);
    if (useInnerEventData)
        EpollCtl::AddWriteEvent(eventData.epoll_fd_, eventData.fd_, &eventData);
        
    TimeOutData timeOutData;
    timeOutData.cid_ = MyCoroutine::ScheduleGetRunCid(SCHEDULE);
    int64_t timerId = TIMER.Register(
        TimeOutCallBack, &timeOutData, RpcTimeOut.Get().write_time_out_ms_);
    
    Common::Defer defer([&timeOutData, &eventData, useInnerEventData, timerId]() {
        if (useInnerEventData) 
            EpollCtl::ClearEvent(eventData.epoll_fd_, eventData.fd_, false);
        if (not timeOutData.time_out_) 
            TIMER.Cancel(timerId);  // 定时器不超时，则取消定时器 
    });
    while (true) {
        ssize_t ret = SYSTEM.write(fd, buf, size);
        if (ret >= 0)
            return ret; // 写成功
        if (EINTR == errno)
            continue; // 调用被中断，则直接重启write调用
        if (EAGAIN == errno or EWOULDBLOCK == errno) { // 暂时不可写
            MyCoroutine::CoroutineYield(SCHEDULE);     // 让出cpu，切换到主协程，等待下一次数据可写
            if (timeOutData.time_out_) { // 写超时了
                errno = EAGAIN;
                return -1; // 写超时，返回-1，并把errno设置为EAGAIN
            }
            continue;
        }
        return ret; // 写失败
    }
}

inline int CoConnect(int fd, const struct sockaddr *addr, socklen_t size)
{
    EventData eventData(fd, EpollFd.Get(), RPC_CLIENT);
    eventData.cid_ = MyCoroutine::ScheduleGetRunCid(SCHEDULE);
    EpollCtl::AddWriteEvent(eventData.epoll_fd_, eventData.fd_, &eventData); // 监听可写事件
    TimeOutData timeOutData;
    timeOutData.cid_ = MyCoroutine::ScheduleGetRunCid(SCHEDULE);
    int64_t timerId = TIMER.Register(TimeOutCallBack, &timeOutData, RpcTimeOut.Get().connect_time_out_ms_);
    Common::Defer defer([&timeOutData, &eventData, timerId]()
                        {
EpollCtl::ClearEvent(eventData.epoll_fd_, eventData.fd_, false);
if (not timeOutData.time_out_) {
    TIMER.Cancel(timerId);  // 定时器不超时，则取消定时器
} });
    while (true)
    {
        int ret = SYSTEM.connect(fd, addr, size);
        if (0 == ret)
            return 0; // connect成功
        if (errno == EINTR)
            continue; // 调用被中断，则直接重启connect调用
        if (errno == EINPROGRESS)
        {                                          // 三次握手进行中
            MyCoroutine::CoroutineYield(SCHEDULE); // 让出cpu，切换到主协程，等待下一次数据可写
            if (timeOutData.time_out_)
            { // connect超时了
                TRACE("connect_time_out, connect_time_out_ms[%d]", RpcTimeOut.Get().connect_time_out_ms_);
                errno = EAGAIN;
                return -1; // connect超时，返回-1，并把errno设置为EAGAIN
            }
            return Common::SockOpt::GetSocketError(fd); // 检查sockFd上是否有错误发生
        }
        return ret;
    }
}

} // namespace Core
