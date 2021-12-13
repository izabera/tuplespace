#include <iostream>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <memory>
#include <set>
#include <mutex>
#include <variant>
#include <vector>
using namespace std::chrono_literals;

using elem = std::variant<int, double, std::string>;
using tuple = std::vector<elem>;

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

class simpletuplespace {
    // used by put, try_copy and try_take
    std::unique_ptr<std::mutex> mutex = std::make_unique<std::mutex>();

    // used by put, copy and take
    std::unique_ptr<std::mutex> cv_mutex = std::make_unique<std::mutex>();
    std::unique_ptr<std::condition_variable> cv = std::make_unique<std::condition_variable>();

    using pelem = std::variant<int, int*, double, double*, std::string, std::string*>;
    using ptuple = std::vector<pelem>;

    std::multiset<ptuple> tuples;

    template <typename... targs>
    bool try_copytake(bool remove, targs... args) {
        std::unique_lock lock(*mutex);

        auto goalsize = sizeof...(args);
        ptuple goal{ args... };

        auto begin = tuples.begin();
        auto end = tuples.end();

        for (auto it = begin; it != end; it++) {
            auto current = *it;

            auto size = current.size();
            if (goalsize != size)
                continue;

            for (auto i = 0u; i < size; i++) {
                auto match = [&](const pelem& lhs, const pelem& rhs) {
                    if (lhs.index() / 2 != rhs.index() / 2)
                        return false;

                    // if it's a pointer, accept any value
                    if (lhs.index() % 2 == 1)
                        return true;

                    // if it's a literal value, check that it's the right one
                    return lhs == rhs;
                };

                if (!match(goal[i], current[i]))
                    goto nexttuple;
            }

            // yay we found one!
            for (auto i = 0u; i < size; i++) {
                auto rhs = current[i];

                std::visit(overloaded {
                    [&]<typename t>(t* arg) { *arg = std::get<t>(current[i]); },
                    [&](auto) { },
                }, goal[i]);
            }

            if (remove)
                tuples.erase(it);

            return true;

            nexttuple: ;
        }
        return false;
    }

    template <typename... targs>
    tuple copytake(bool remove, targs... args) {
        while (1) {
            std::unique_lock<std::mutex> cv_lock(*cv_mutex);

            if (try_copytake(remove, args...)) {
                tuple ret{};
                ptuple found{ args... };
                for (const auto& pt : found) {
                    elem e;

                    std::visit(overloaded {
                            [&]<typename t>(t* arg) { e = *arg; },
                            [&](auto arg) { e = arg; },
                            }, pt);
                    ret.push_back(e);
                }
                return ret;
            }
            // wait until more tuples are put in
            cv->wait(cv_lock);
        }
    }

   public:
    template <typename... targs>
    void put(targs... args) {
        std::unique_lock lock(*mutex);
        {
            std::unique_lock<std::mutex> cv_lock(*cv_mutex);
            tuples.insert(ptuple{ args... });
        }
        cv->notify_all();
    }

    template <typename... targs>
    bool try_take(targs... args) {
        return try_copytake(true, args...);
    }

    template <typename... targs>
    bool try_copy(targs... args) {
        return try_copytake(false, args...);
    }

    template <typename... targs>
    tuple take(targs... args) {
        return copytake(true, args...);
    }

    template <typename... targs>
    tuple copy(targs... args) {
        return copytake(false, args...);
    }
};

std::ostream& operator<<(std::ostream& os, const tuple& t) {
    os << "{";
    auto sep = " ";
    for (const auto& elem : t) {
        std::visit([&](const auto& arg){ os << sep << arg; }, elem);
        sep = ", ";
    }
    return os << " }";
}

namespace {
    enum { ok, warning, error, reset };
    class escapes {
        std::string esc[4]{};

       public:
        escapes() {
            if (isatty(1)) {
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

    std::cout << escapes[ret ? ok : error]
              << actualname << " == " << expectedname
              << escapes[reset] << '\n';
    return ret;
}

#define test(x,y) test(x, #x, y, #y)
int main() {
    simpletuplespace t{};
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
    std::cout << t.copy(44, "meow") << '\n';
    test(t.try_take(44, "meow"), true);
    test(t.try_take(44, "meow"), false);

    std::thread thr1([&] {
        std::this_thread::sleep_for(2s);
        t.put("aqq", "zzz");
    });

    std::thread thr2([&] {
        std::this_thread::sleep_for(3s);
        t.put("qqq", "zzz");
    });

    std::string s;
    std::cout << "this should take ~3 seconds" << std::endl;
    std::cout << t.take("qqq", &s) << '\n';
    std::cout << s << '\n';
    thr1.join();
    thr2.join();
}
