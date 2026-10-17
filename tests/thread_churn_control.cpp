// Standalone allocator/sanitizer control: deliberately does not link the library.
#include <barrier>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
    using Clock = std::chrono::steady_clock;

    const int seconds = argc == 2 ? std::stoi(argv[1]) : 300;

    if (seconds <= 0)
        return 1;

    const auto begin = Clock::now();
    uint64_t waves = 0;

    do
    {
        std::barrier gate(32);
        std::vector<std::jthread> threads;

        for (int i = 0; i < 32; ++i)
            threads.emplace_back(
                [&]
                {
                    gate.arrive_and_wait();
                });

        threads.clear();
        ++waves;
        std::this_thread::sleep_until(begin + std::chrono::milliseconds(waves * 100));

        if (waves % 100 == 0)
            std::cout << "CONTROL waves=" << waves << " thread_lifetimes=" << waves * 32
                      << " elapsed_ms="
                      << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin)
                             .count()
                      << std::endl;
    } while (Clock::now() - begin < std::chrono::seconds(seconds));

    std::cout << "PASS thread_churn_control waves=" << waves << " thread_lifetimes=" << waves * 32
              << std::endl;
}
