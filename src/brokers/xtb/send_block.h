#pragma once
#ifndef SRC_BROKERS_XTB_SEND_BLOCK_H_
#define SRC_BROKERS_XTB_SEND_BLOCK_H_
#include <chrono>
#include <thread>


template<typename Dur>
class XTBSendBlock {
public:


    XTBSendBlock(Dur dur):_dur(dur) {}
    void operator()() {
        auto x = _expiration;
        _expiration = std::chrono::system_clock::now()+_dur;
        std::this_thread::sleep_until(x);
    }


protected:
    Dur _dur;
    std::chrono::system_clock::time_point _expiration;
};

template<typename Dur>
XTBSendBlock(Dur) -> XTBSendBlock<Dur>;



#endif /* SRC_BROKERS_XTB_SEND_BLOCK_H_ */
