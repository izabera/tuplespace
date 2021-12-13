#include <iostream>
#include <chrono>
#include <thread>
#include <unistd.h>

#include "tuplespace.hpp"

using namespace std::chrono_literals;

namespace {
    enum { ok, warning, error, reset };
    class escapes {
        std::string esc[4]{};

       public:
        escapes() {
            if (::isatty(1)) {
                esc[ok] = "\x1b[32m";
                esc[warning] = "\x1b[33m";
                esc[error] = "\x1b[31m";
                esc[reset] = "\x1b[m";
            }
        }

        auto operator[](int i) { return esc[i]; }
    };
    escapes escapes;
}

template <typename t>
bool test(t actual, std::string actualname, t expected, std::string expectedname) {
    bool ret = actual == expected;

    auto slen = actualname.size() + 4 + expectedname.size() - 6;
    std::string dots(slen < 80 ? 80 - slen : 0, '.');
    std::cout << escapes[ret ? ok : error]
              << actualname << " == " << expectedname
              << dots << (ret ? "[PASS]" : "[FAIL]")
              << escapes[reset] << '\n';
    return ret;
}

#define test(x,y) test(x, #x, y, #y)
int main() {
    tuplespace t{};
    t.put(3, 1.2, "meow", 4);
    t.put(3, 1.2, "meow", 4);
    t.put(44, "meow");

    test(t.try_copy(3, 1.2, "meow", 4), true);
    test(t.try_copy(3, 1.2, "thiswillfail", 4), false);

    int p;
    test(t.try_copy(&p, 1.2, "meow", 4), true);
    test(p, 3);

    test(t.try_copy(44, "meow"), true);
    test(t.try_copy(44, "meow"), true);
    test(t.try_copy(44, "meow"), true);
    test(t.copy(44, "meow"), (tuple{44, "meow"}));
    test(t.try_take(44, "meow"), true);
    test(t.try_take(44, "meow"), false);

    // basic concurrency test
    std::thread thr1([&] {
        std::this_thread::sleep_for(2s);
        t.put("aqq", "zzz");
    });

    std::thread thr2([&] {
        std::this_thread::sleep_for(3s);
        t.put("qqq", 777);
    });

    int v;
    std::cerr << "this should take ~3 seconds" << std::endl;
    test((t.take("qqq", &v), v), 777);
    thr1.join();
    thr2.join();

    // lots of concurrent putters
    constexpr int limit = 1000;
    int nthreads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    for (auto i = 0; i < nthreads; i++) {
        threads.push_back(std::thread( [&] {
            std::this_thread::sleep_for(1s);
            for (auto j = 0; j < limit; j++) {
                t.put("threadtest", j);
            }
            }));
    }

    for (auto& thr : threads)
        thr.join();

    // lots of concurrent takers
    threads.clear();
    std::vector<int> sums(nthreads);
    for (auto i = 0; i < nthreads; i++) {
        threads.push_back(std::thread( [&, i] {
            std::this_thread::sleep_for(1s);
            while (1) {
                int val;
                if (!t.try_take("threadtest", &val))
                    break;
                sums[i] += val;
            }
            }));
    }

    for (auto& thr : threads)
        thr.join();


    auto total = 0;
    for (auto s : sums)
        total += s;

    std::cerr << "{";
    auto sep = " ";
    for (auto s : sums) {
        std::cerr << sep << s;
        sep = ", ";
    }
    std::cerr << " } (total=" << total << ")\n";
    test(total, nthreads * (limit -1) * limit / 2);
}
