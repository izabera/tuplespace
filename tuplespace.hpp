#include <condition_variable>
#include <iostream>
#include <set>
#include <thread>
#include <variant>
#include <vector>

using elem = std::variant<int, double, std::string>;
using tuple = std::vector<elem>;

namespace {
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
}  // namespace

class tuplespace {
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
        ptuple goal{args...};

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

                std::visit(overloaded{
                               [&]<typename t>(t* arg) { *arg = std::get<t>(current[i]); },
                               [&](auto) {},
                           },
                           goal[i]);
            }

            if (remove)
                tuples.erase(it);

            return true;

        nexttuple:;
        }
        return false;
    }

    template <typename... targs>
    tuple copytake(bool remove, targs... args) {
        while (1) {
            std::unique_lock<std::mutex> cv_lock(*cv_mutex);

            if (try_copytake(remove, args...)) {
                tuple ret{};
                ptuple found{args...};
                for (const auto& pt : found) {
                    elem e;

                    std::visit(overloaded{
                                   [&]<typename t>(t* arg) { e = *arg; },
                                   [&](auto arg) { e = arg; },
                               },
                               pt);
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
        {
            std::unique_lock<std::mutex> cv_lock(*cv_mutex);
            std::unique_lock lock(*mutex);
            tuples.insert(ptuple{args...});
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
        std::visit([&](const auto& arg) { os << sep << arg; }, elem);
        sep = ", ";
    }
    return os << " }";
}
