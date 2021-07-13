#ifndef __COMMON_H__
#define __COMMON_H__

#include <stddef.h>
#include <stdint.h>

typedef size_t usize;
typedef uint8_t u8;
typedef int32_t i32;

void __attribute__((format(printf, 2, 3))) assert(int e, const char *msg, ...);
void die(const char *fmt, ...) __attribute__((format(printf, 1, 2)))
__attribute__((noreturn));

#endif // !__COMMON_H__
