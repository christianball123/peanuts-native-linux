// Peanuts Native for Linux: getauxval without <elf.h> (glibc's <sys/auxv.h> includes it, Bionic's does not, and the host declares its own Elf64 types)
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
unsigned long getauxval(unsigned long type);
#ifndef AT_HWCAP
#define AT_HWCAP 16
#endif
#ifndef AT_HWCAP2
#define AT_HWCAP2 26
#endif
#ifndef AT_PAGESZ
#define AT_PAGESZ 6
#endif
#ifdef __cplusplus
}
#endif
