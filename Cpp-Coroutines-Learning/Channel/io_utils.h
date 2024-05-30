#pragma once
#include <iostream>
#include <chrono>
#include <ctime>
using namespace std;
void print_time() {
    // ��ȡ��ǰʱ���
    auto now = std::chrono::system_clock::now();
    // ת��Ϊtime_t����
    auto now_c = std::chrono::system_clock::to_time_t(now);
    // ��ȡ����ʱ��
    auto local_time = std::localtime(&now_c);
    // �����ǰ�ķ��Ӻ��루������ꡢ�¡��պ�Сʱ��
    std::cout << "Current time: ";
    std::cout << std::setfill('0') << std::setw(2) << local_time->tm_min << ":";  // ����
    std::cout << std::setfill('0') << std::setw(2) << local_time->tm_sec;        // ��

    // ������벿��
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    auto sec_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(now_c));
    std::cout << "." << std::setfill('0') << std::setw(3) << (now_ms.count() % 1000) << " ";  // ����
}
void debug(const std::string& s) {
    print_time();
    printf(" %d %s\n", std::this_thread::get_id(), s.c_str());
}

void debug(const std::string& s, int x) {
    print_time();
    printf("%d %s %d\n", std::this_thread::get_id(), s.c_str(), x);
}
