#pragma once

#include "common.h"

#include <stdlib.h> // malloc & pals
#include <string.h> // memcpy

#include <initializer_list>


/*
    Attempt to make larger code non-templated.
*/
struct VoidArray {
    void *pBegin;
    void *pEnd;
    void *pCap;
};

inline void
ArrayRealloc(VoidArray& a, size_t newCapInBytes)
{
    ASSERT(newCapInBytes);

    size_t const origSizeInBytes = static_cast<char *>(a.pEnd) - static_cast<char *>(a.pBegin);

    char *const pBytes = static_cast<char *>(realloc(a.pBegin, newCapInBytes));
    if (!pBytes) {
        perror("realloc");
        exit(7);
    }

    a.pBegin = pBytes;
    a.pEnd   = pBytes + origSizeInBytes;
    a.pCap   = pBytes + newCapInBytes;
}

// may grow more than requested amount, so multiple appends are amortized.
inline void
EnsureAddedRoomGrow(VoidArray& a, uint additionalTs, uint sizeOfT) 
{
    uint const oldCountTs =  uint(static_cast<char *>(a.pEnd) - static_cast<char *>(a.pBegin));
    uint const oldCapBytes = uint(static_cast<char *>(a.pCap) - static_cast<char *>(a.pBegin));
    uint const minCapBytes = oldCountTs + additionalTs * sizeOfT;
    if (oldCapBytes < minCapBytes) {
        ArrayRealloc(a, Max<uint>(minCapBytes, ((oldCountTs * 3) / 2u) * sizeOfT));
    }
}


template<typename T>
class Array {
    static_assert(__is_trivial(T), "T must be trivial");

    VoidArray vArray;

#define BEGIN reinterpret_cast<T *>(vArray.pBegin)
#define END reinterpret_cast<T *>(vArray.pEnd)
#define CAP reinterpret_cast<T *>(vArray.pCap)

public:
    constexpr Array() : vArray{} {
    }
    ~Array() {
        free(vArray.pBegin);
    }

    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;
    
    T *data() const { return BEGIN; }
    T *begin() const { return BEGIN; }
    T *end() const { return END; }
    uint size() const { return uint(END - BEGIN); }
    bool is_empty() const { return BEGIN == END; }

    T& operator[](size_t i) { ASSERT(i < size_t(END - BEGIN)); return BEGIN[i]; }

    // no dtor, so has non-void return:
    T& pop()
    {
        ASSERT(BEGIN < END); 

        T *pBack = END;
        END = --pBack;
        return *pBack;
    }

    T *set_end(size_t n)
    {
        ASSERT(size_t(CAP - BEGIN) >= n);
        END = BEGIN + n;
    }

    void reserve(uint minCapacity)
    {
        uint minCapacityBytes = minCapacity * sizeof(T);
        if (uint(static_cast<char *>(vArray.pCap) - static_cast<char *>(vArray.pBegin)) < minCapacityBytes) {
            ArrayRealloc(vArray, minCapacityBytes);
        }
    }

    T* uninitialized_push_n(uint n)
    {
        EnsureAddedRoomGrow(vArray, n, sizeof(T));
        T *ret = END;
        vArray.pEnd = ret + n;
        return ret;
    }

    /* common case, could make this faster: */
    T* uninitialized_push()
    {
        return uninitialized_push_n(1);
    }

    void push(const T& val)
    {
        *uninitialized_push() = val;
    }

    void push_n(const T *src, uint n)
    {
        memcpy(uninitialized_push_n(n), src, n * sizeof(T));
    }

    void push2(const T& a, const T& b)
    {
        T *p = uninitialized_push_n(2);
        p[0] = a;
        p[1] = b;
    }

    void push3(const T& a, const T& b, const T& c)
    {
        T *p = uninitialized_push_n(3);
        p[0] = a;
        p[1] = b;
        p[2] = c;
    }

    void push4(const T& a, const T& b, const T& c, const T& d)
    {
        T *p = uninitialized_push_n(4);
        p[0] = a;
        p[1] = b;
        p[2] = c;
        p[3] = d;
    }

    void push_initlist(std::initializer_list<T> ilist)
    {
        push_n(ilist.begin(), uint(ilist.size()));
    }

#undef BEGIN
#undef END
#undef CAP
};
