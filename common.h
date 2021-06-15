#pragma once

#include <stdint.h>
#include <stddef.h>

typedef unsigned int uint;
typedef unsigned char ubyte;

#include <assert.h>

#define ASSERT assert

#ifdef _MSC_VER
#define unreachable __assume(0)
#define attrib_noreturn __declspec(noreturn)
// avoid including <intrin[0].h> for better compile times (?)
// or move this to a "semi-common" header
extern "C" unsigned char _BitScanForward(unsigned long * _Index, unsigned long _Mask);
#pragma intrinsic(_BitScanForward)
__forceinline static unsigned long bsf(unsigned long v)
{
    unsigned long i;
    _BitScanForward(&i, v);
    return i;
}
#else
#define unreachable __builtin_unreachable()
#define attrib_noreturn __attribute__((noreturn))
#define bsf(v) unsigned(__builtin_ctz(v))
#endif

template<class T>
struct array_span {
	T *ptr;
	uint length;

	array_span() = default;
	template<uint N> constexpr array_span(T(&a)[N]) : ptr(a), length(N) { }
	constexpr array_span(T *a, uint n) : ptr(a), length(n) { }

	// template<class C> constexpr array_span(const C& c) : ptr(c.begin()), length(c.size()) { }

    uint size() const { return length; }

    T& operator[](size_t i) const  { return ptr[i]; }

	// ranged for:
	T* begin() const { return ptr; }
	T* end() const { return ptr+length; }
};

template<class T>
struct array_interval {
	T *first;
	T *past;

	array_interval() = default;
	template<uint N> constexpr array_interval(T(&a)[N]) : first(a), past(a+N) { }
	constexpr array_interval(T *b, T *e) : first(b), past(e) { }
	// template<class C> constexpr array_interval(const C& c) : first(c.begin()), past(c.end()) { }

	uint size() const { return uint(past - first); }
	// ranged for:
	T* begin() const { return first; }
	T* end() const { return past; }
};

typedef array_span<const char> char_view;

// The count on input does not include the terminator.
inline constexpr char_view operator""_view(const char *strlit, size_t n) noexcept
{
    return { strlit, uint(n) };
}

template<class T> constexpr T Max(T a, T b) { return a < b ? b : a; }

template<class T, uint N> constexpr uint lengthof(T(&)[N]) { return N; }

