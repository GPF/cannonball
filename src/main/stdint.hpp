/***************************************************************************
    Data Types.

    Enforce data type size at compile time.

    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#pragma once

/** C99 Standard Naming */
#if defined(_MSC_VER)
    typedef signed char int8_t;
    typedef signed short int16_t;
    typedef signed int int32_t;
    typedef signed long long int64_t;

    typedef unsigned char uint8_t;
    typedef unsigned short uint16_t;
    typedef unsigned int uint32_t;
    typedef unsigned long long uint64_t;
#else
    #include <stdint.h>
#endif

/* Report typedef errors */
static_assert(sizeof(int8_t)   == 1, "int8_t is not of the correct size");
static_assert(sizeof(int16_t)  == 2, "int16_t is not of the correct size");
static_assert(sizeof(int32_t)  == 4, "int32_t is not of the correct size");
static_assert(sizeof(int64_t)  == 8, "int64_t is not of the correct size");

static_assert(sizeof(uint8_t)  == 1, "uint8_t is not of the correct size");
static_assert(sizeof(uint16_t) == 2, "uint16_t is not of the correct size");
static_assert(sizeof(uint32_t) == 4, "uint32_t is not of the correct size");
static_assert(sizeof(uint64_t) == 8, "uint64_t is not of the correct size");
