#pragma once
#include <assert.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <queue>
#include <unordered_set>
#include "../common/singleton.hpp"

#define TIMER Common::Singleton<Core::Timer>::Instance()

namespace Core {
typedef void (*TimerCallBack)(void *data);
typedef struct TimerData {
    friend bool operator<(const TimerData &left, const TimerData &right){
        return left.abs_time_ms_ > right.abs_time_ms_;
    }
    uint64_t id_;
    void *data_{nullptr};
    int64_t abs_time_ms_{0};
    TimerCallBack call_back_{nullptr};
} TimerData;

class Timer {
public:
    uint64_t Register(TimerCallBack callBack, void *data, int64_t timeOutMs) {
        alloc_id_++;
        TimerData timerData;
        timerData.id_ = alloc_id_;
        timerData.data_ = data;
        timerData.abs_time_ms_ = GetCurrentTimeMs() + timeOutMs;
        timerData.call_back_ = callBack;
        timers_.push(timerData);
        timer_ids_.insert(timerData.id_);
        return alloc_id_;
    }
    void Cancel(uint64_t id) {
        assert(timer_ids_.find(id) != timer_ids_.end()); // 取消的定时器，必须是存在的
        cancel_ids_.insert(id);                          // 这里只是做一下记录
    }

    //获取下一个即将超时的定时器
    bool GetLastTimer(TimerData &timerData) {
        while (not timers_.empty()) {
            timerData = timers_.top();
            timers_.pop();
            if (cancel_ids_.find(timerData.id_) != cancel_ids_.end())
            { // 被取消的定时器不执行，直接删除
                cancel_ids_.erase(timerData.id_);
                timer_ids_.erase(timerData.id_);
                continue;
            }
            return true;
        }
        return false;
    }

    //计算一个定时器超时还剩余的时间
    int64_t TimeOutMs(TimerData &timerData) {
        // 多1ms，确保后续的定时器必定能超时
        int64_t temp = timerData.abs_time_ms_ + 1 - GetCurrentTimeMs(); 
        if (temp > 0)
            return temp;
        return 0;
    }
    void Run(TimerData &timerData) {
        if (not isExpire(timerData)) { // 没有过期则重新塞入队列中去重新排队
            timers_.push(timerData);
            return;
        }
        
        int64_t timerId = timerData.id_;
        assert(timer_ids_.find(timerId) != timer_ids_.end()); // 要运行的定时器，必须是存在的
        timer_ids_.erase(timerId);
        if (cancel_ids_.find(timerId) != cancel_ids_.end()) { // 被取消的定时器不执行，直接删除
            cancel_ids_.erase(timerId);
            return;
        }
        timerData.call_back_(timerData.data_);
    }
    int64_t GetCurrentTimeMs(){
        struct timeval current;
        gettimeofday(&current, NULL);
        return current.tv_sec * 1000 + current.tv_usec / 1000;
    }

private:
    bool isExpire(TimerData &timerData) { 
        return GetCurrentTimeMs() >= timerData.abs_time_ms_;
    }

private:
    uint64_t alloc_id_{0};
    std::unordered_set<uint64_t> timer_ids_;
    std::unordered_set<uint64_t> cancel_ids_;
    std::priority_queue<TimerData> timers_;
};
} // namespace Core
