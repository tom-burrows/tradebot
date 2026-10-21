#include <gtest/gtest.h>
#include <climits>
#include <cstddef>

// `volatile` stops the compiler from knowing these values at compile time.
// Without it, the compiler can warn about the bug, optimise it away, or fold it
// into a constant before the sanitizer ever runs.

namespace {
    // ---------- ASan ----------

    TEST(SanitizerTest, HeapUseAfterFree) {
        int* p = new int(42);
        delete p;
        volatile int value = *p;  // read after delete
        (void)value;
    }

    TEST(SanitizerTest, HeapBufferOverflow) {
        volatile std::size_t n = 4;
        int* arr = new int[n];
        arr[n] = 1;               // valid indices are 0..3
        delete[] arr;
    }

    TEST(SanitizerTest, StackBufferOverflow) {
        int arr[4] = {};
        volatile int i = 4;
        arr[i] = 1;               // one past the end of a local array
    }

    // ---------- UBSan ----------

    TEST(SanitizerTest, SignedIntegerOverflow) {
        volatile int big = INT_MAX;
        volatile int result = big + 1;  // signed overflow is UB
        (void)result;
    }

    TEST(SanitizerTest, MisalignedAccess) {
        alignas(int) char buf[sizeof(int) * 2] = {};
        volatile std::size_t offset = 1;
        int* p = reinterpret_cast<int*>(buf + offset);  // not 4-byte aligned
        volatile int value = *p;
        (void)value;
    }

    TEST(SanitizerTest, NullDereference) {
        int* volatile p = nullptr;
        volatile int value = *p;
        (void)value;
    }
}