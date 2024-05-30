#pragma once
#include <iostream>
#include <chrono>
#include <ctime>
using namespace std;
void print_time() {
    // 获取当前时间点
    auto now = std::chrono::system_clock::now();
    // 转换为time_t类型
    auto now_c = std::chrono::system_clock::to_time_t(now);
    // 获取本地时间
    auto local_time = std::localtime(&now_c);
    // 输出当前的分钟和秒（不输出年、月、日和小时）
    std::cout << "Current time: ";
    std::cout << std::setfill('0') << std::setw(2) << local_time->tm_min << ":";  // 分钟
    std::cout << std::setfill('0') << std::setw(2) << local_time->tm_sec;        // 秒

    // 处理毫秒部分
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    auto sec_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(now_c));
    std::cout << "." << std::setfill('0') << std::setw(3) << (now_ms.count() % 1000) << " ";  // 毫秒
}
void debug(const std::string& s) {
    print_time();
    printf(" %d %s\n", std::this_thread::get_id(), s.c_str());
}

void debug(const std::string& s, int x) {
    print_time();
    printf("%d %s %d\n", std::this_thread::get_id(), s.c_str(), x);
}
