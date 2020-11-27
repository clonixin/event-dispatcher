#include <criterion/criterion.h>

#include <iostream>
#include "../srcs/SyncDispatcher.hpp"

template <typename T>
struct TestEvent {
    T val;
};

template <typename T>
bool operator==(TestEvent<T> const &lhs, TestEvent<T> const &rhs) {
    return lhs.val == rhs.val;
}

template <typename T>
class CompareCallback {
    TestEvent<T> _stored;
    int _num_call;
    public:
        CompareCallback(T val): _stored{val}, _num_call(0) {}
        void operator()(TestEvent<T> val) {
            _num_call++;
            cr_assert_eq(val, _stored);
        };

        void assert_eq(TestEvent<T> val) {
            _num_call++;
            cr_assert_eq(val, _stored);
        }

        int getNumCall() const { return _num_call; }
};

static void callbackFunPtr(TestEvent<float>) {}

namespace ce = clonixin::event;

TestSuite(SyncDispatcherTestSuite, .description = "Testing SyncDispatcher features.");

Test(SyncDispatcherTestSuite, registerCallbacksTest,
        .description = "Register 4 callbacks (1 lambda, 1 function pointer, an object with operator() and a std::function that wrap a bound member function pointer) and ensure it does not throws.") {
    ce::SyncDispatcher dis;

    cr_assert_none_throw(
        ce::handle hndl = dis.registerCallback<TestEvent<int>>([](TestEvent<int> te){});
        cr_assert_neq(hndl, ce::nil_handle, "%s: nil handle returned.", "lambda");
    , "registering lambda caused an exception to be thrown.");

    cr_assert_none_throw(
        ce::handle hndl = dis.registerCallback<TestEvent<float>>(callbackFunPtr);
        cr_assert_neq(hndl, ce::nil_handle, "%s: nil handle returned.", "fnptr");
    , "registering function pointer caused an exception to be thrown.");

    CompareCallback<std::string> callback("hello");

    cr_assert_none_throw(
        ce::handle hndl = dis.registerCallback<TestEvent<std::string>>(callback);
        cr_assert_neq(hndl, ce::nil_handle, "%s: nil handle returned.", "functor");
    , "registering functor caused an exception to be thrown.");

    cr_assert_none_throw(
        auto fun = std::bind(&CompareCallback<std::string>::assert_eq, &callback, std::placeholders::_1);
        ce::handle hndl = dis.registerCallback<TestEvent<std::string>>(fun);
        cr_assert_neq(hndl, ce::nil_handle, "%s: nil handle returned.", "bound function");
    , "registering bound member function caused an exception to be thrown.");

}

Test(SyncDispatcherTestSuite, unregisterCallbackTest, .description = "Register a simple callback, then unregister it. then dispatch an event to ensure it's not received.") {
    ce::SyncDispatcher dis;
    CompareCallback<std::string> cb("hello");
    auto fun = std::bind(&CompareCallback<std::string>::assert_eq, &cb, std::placeholders::_1);

    ce::handle hndl = dis.registerCallback<TestEvent<std::string>>(fun);

    dis.dispatch(TestEvent<std::string>{"hello"});

    cr_assert_none_throw(dis.unregisterCallback(hndl));

    dis.dispatch(TestEvent<std::string>{"hello"});
    cr_assert_eq(cb.getNumCall(), 1, "Callback was reached after being unregistered.");
}

Test(SyncDispatcherTestSuite, oneEventNoCallback, .description = "Dispatch an event without registering a callback first. Should not throw") {
    ce::SyncDispatcher dis;

    cr_assert_none_throw(dis.dispatch(TestEvent<std::string> { "hello" }));
}

Test(SyncDispatcherTest, oneEventOneCallback, .description = "Register a callback, then dispatch a single event.") {
    ce::SyncDispatcher dis;

    std::string value = "hello";
    TestEvent<std::string> ev { value };
    CompareCallback<std::string> cb(value);

    ce::handle hndl = dis.registerCallback<TestEvent<std::string>>(std::bind(&CompareCallback<std::string>::assert_eq, &cb, std::placeholders::_1));

    dis.dispatch(ev);

    cr_assert_eq(cb.getNumCall(), 1, "Callback was called more than once (called %d times)", cb.getNumCall());
}

Test(SyncDispatcherTestSuite, oneEventFiredTwiceOneCallback, .description = "Register a callback, then dispatch a single event two time.") {
    ce::SyncDispatcher dis;

    std::string value = "hello";
    TestEvent<std::string> ev { value };
    CompareCallback<std::string> cb(value);

    ce::handle hndl = dis.registerCallback<TestEvent<std::string>>(std::bind(&CompareCallback<std::string>::assert_eq, &cb, std::placeholders::_1));

    dis.dispatch(ev);
    dis.dispatch(ev);

    cr_assert_eq(cb.getNumCall(), 2, "Callback was not called a correct amount of time (called %d times)", cb.getNumCall());
}

Test(SyncDispatcherTestSuite, oneEventOneMatchingCallback, .description = "Register two callback, then dispatch a single event. Only one should be called.") {
    ce::SyncDispatcher dis;

    std::string value = "hello";
    TestEvent<std::string> ev { value };
    CompareCallback<std::string> cb(value);
    CompareCallback<float> cbf(0.4);

    ce::handle hndl = dis.registerCallback<TestEvent<std::string>>(std::bind(&CompareCallback<std::string>::assert_eq, &cb, std::placeholders::_1));
    ce::handle hndlf = dis.registerCallback<TestEvent<float>>(std::bind(&CompareCallback<float>::assert_eq, &cbf, std::placeholders::_1));

    dis.dispatch(ev);

    cr_assert_eq(cb.getNumCall(), 1, "Callback was called more than once (called %d times)", cb.getNumCall());
    cr_assert_eq(cbf.getNumCall(), 0, "Callback was called more than once (called %d times)", cb.getNumCall());
}

Test(SyncDispatcherTestSuite, oneEventTwoMatchingCallback, .description = "Register two callbacks, then dispatch a single event.") {
    ce::SyncDispatcher dis;

    std::string value = "hello";
    TestEvent<std::string> ev { value };
    CompareCallback<std::string> cb(value);
    CompareCallback<std::string> cb2(value);

    ce::handle hndl = dis.registerCallback<TestEvent<std::string>>(std::bind(&CompareCallback<std::string>::assert_eq, &cb, std::placeholders::_1));
    ce::handle hndl2 = dis.registerCallback<TestEvent<std::string>>(std::bind(&CompareCallback<std::string>::assert_eq, &cb2, std::placeholders::_1));

    dis.dispatch(ev);

    cr_assert_eq(cb.getNumCall(), 1, "Callback was not called the correct number of time (called %d times)", cb.getNumCall());
    cr_assert_eq(cb2.getNumCall(), 1, "Callback was not called the correct number of time (called %d times)", cb2.getNumCall());
}

Test(SyncDispatcherTestSuite, severalEventSeveralCallback, .description = "Register several callbacks, for several events, then fire events.") {
    ce::SyncDispatcher dis;

    std::string str_val = "hello";
    int int_val = 42;
    float float_val = 4.2;

    CompareCallback<std::string> cbs1{str_val};
    CompareCallback<std::string> cbs2{str_val};
    CompareCallback<std::string> cbs3{str_val};

    CompareCallback<int> cbi1{int_val};
    CompareCallback<int> cbi2{int_val};
    CompareCallback<int> cbi3{int_val};

    CompareCallback<float> cbf1{float_val};
    CompareCallback<float> cbf2{float_val};
    CompareCallback<float> cbf3{float_val};

    (void)dis.registerCallback<TestEvent<std::string>>(std::bind(&CompareCallback<std::string>::assert_eq, &cbs1, std::placeholders::_1));
    (void)dis.registerCallback<TestEvent<int>>(std::bind(&CompareCallback<int>::assert_eq, &cbi1, std::placeholders::_1));
    (void)dis.registerCallback<TestEvent<float>>(std::bind(&CompareCallback<float>::assert_eq, &cbf1, std::placeholders::_1));

    dis.dispatch(TestEvent<std::string>{str_val});
    dis.dispatch(TestEvent<int>{int_val});
    dis.dispatch(TestEvent<float>{float_val});

    (void)dis.registerCallback<TestEvent<std::string>>(std::bind(&CompareCallback<std::string>::assert_eq, &cbs2, std::placeholders::_1));
    (void)dis.registerCallback<TestEvent<int>>(std::bind(&CompareCallback<int>::assert_eq, &cbi2, std::placeholders::_1));
    (void)dis.registerCallback<TestEvent<float>>(std::bind(&CompareCallback<float>::assert_eq, &cbf2, std::placeholders::_1));

    dis.dispatch(TestEvent<std::string>{str_val});
    dis.dispatch(TestEvent<int>{int_val});
    dis.dispatch(TestEvent<float>{float_val});

    (void)dis.registerCallback<TestEvent<std::string>>(std::bind(&CompareCallback<std::string>::assert_eq, &cbs3, std::placeholders::_1));
    (void)dis.registerCallback<TestEvent<int>>(std::bind(&CompareCallback<int>::assert_eq, &cbi3, std::placeholders::_1));
    (void)dis.registerCallback<TestEvent<float>>(std::bind(&CompareCallback<float>::assert_eq, &cbf3, std::placeholders::_1));

    dis.dispatch(TestEvent<std::string>{str_val});
    dis.dispatch(TestEvent<int>{int_val});
    dis.dispatch(TestEvent<float>{float_val});

    cr_assert_eq(cbs1.getNumCall(), 3, "Callback was not called the correct number of time (called %d times)", cbs1.getNumCall());
    cr_assert_eq(cbi1.getNumCall(), 3, "Callback was not called the correct number of time (called %d times)", cbi1.getNumCall());
    cr_assert_eq(cbf1.getNumCall(), 3, "Callback was not called the correct number of time (called %d times)", cbf1.getNumCall());

    cr_assert_eq(cbs2.getNumCall(), 2, "Callback was not called the correct number of time (called %d times)", cbs2.getNumCall());
    cr_assert_eq(cbi2.getNumCall(), 2, "Callback was not called the correct number of time (called %d times)", cbi2.getNumCall());
    cr_assert_eq(cbf2.getNumCall(), 2, "Callback was not called the correct number of time (called %d times)", cbf2.getNumCall());

    cr_assert_eq(cbs3.getNumCall(), 1, "Callback was not called the correct number of time (called %d times)", cbs3.getNumCall());
    cr_assert_eq(cbi3.getNumCall(), 1, "Callback was not called the correct number of time (called %d times)", cbi3.getNumCall());
    cr_assert_eq(cbf3.getNumCall(), 1, "Callback was not called the correct number of time (called %d times)", cbf3.getNumCall());
}

Test(SyncDispatcherTestSuite, testMoveConstructor, .description = "Construct a dispatcher by moving an initialized one.") {
    ce::SyncDispatcher dis_base;

    std::string value = "hello";
    TestEvent<std::string> ev { value };
    CompareCallback<std::string> cb(value);
    CompareCallback<std::string> cb2(value);

    ce::handle hndl = dis_base.registerCallback<TestEvent<std::string>>(std::bind(&CompareCallback<std::string>::assert_eq, &cb, std::placeholders::_1));
    ce::handle hndl2 = dis_base.registerCallback<TestEvent<std::string>>(std::bind(&CompareCallback<std::string>::assert_eq, &cb2, std::placeholders::_1));

    dis_base.dispatch(ev);

    cr_expect_eq(cb.getNumCall(), 1, "Callback was not called the correct number of time (called %d times)", cb.getNumCall());
    cr_expect_eq(cb2.getNumCall(), 1, "Callback was not called the correct number of time (called %d times)", cb2.getNumCall());

    ce::SyncDispatcher dis(std::move(dis_base));

    dis.dispatch(ev);

    cr_expect_eq(cb.getNumCall(), 2, "Callback was not called the correct number of time (called %d times)", cb.getNumCall());
    cr_expect_eq(cb2.getNumCall(), 2, "Callback was not called the correct number of time (called %d times)", cb2.getNumCall());
}

Test(SyncDispatcherTestSuite, testMoveOperator, .description = "Construct a dispatcher by moving an initialized one.") {
    ce::SyncDispatcher dis_base;
    ce::SyncDispatcher dis;

    std::string value = "hello";
    TestEvent<std::string> ev { value };
    CompareCallback<std::string> cb(value);
    CompareCallback<std::string> cb2(value);

    ce::handle hndl = dis_base.registerCallback<TestEvent<std::string>>(std::bind(&CompareCallback<std::string>::assert_eq, &cb, std::placeholders::_1));
    ce::handle hndl2 = dis_base.registerCallback<TestEvent<std::string>>(std::bind(&CompareCallback<std::string>::assert_eq, &cb2, std::placeholders::_1));

    dis_base.dispatch(ev);

    cr_expect_eq(cb.getNumCall(), 1, "Callback was not called the correct number of time (called %d times)", cb.getNumCall());
    cr_expect_eq(cb2.getNumCall(), 1, "Callback was not called the correct number of time (called %d times)", cb2.getNumCall());


    dis = std::move(dis_base);

    dis.dispatch(ev);

    cr_expect_eq(cb.getNumCall(), 2, "Callback was not called the correct number of time (called %d times)", cb.getNumCall());
    cr_expect_eq(cb2.getNumCall(), 2, "Callback was not called the correct number of time (called %d times)", cb2.getNumCall());
}
