#pragma once

#include <functional>

/**
// do step 1
step1();
scope_guard guard1 = [&]() {
    // revert step 1
    revert1();
};

// step 2
step2();
guard1.dismiss();
*/

class scope_guard {
public: 
    template<class Callable> 
    scope_guard(Callable && undo_func) try : f(std::forward<Callable>(undo_func)) {
    } catch(...) {
        undo_func();
        throw;
    }

    scope_guard(scope_guard && other) : f(std::move(other.f)) {
        other.f = nullptr;
    }

    ~scope_guard() {
        if(f) f(); // must not throw
    }

    void dismiss() noexcept {
        f = nullptr;
    }

    scope_guard(const scope_guard&) = delete;
    void operator = (const scope_guard&) = delete;

private:
    std::function<void()> f;
};