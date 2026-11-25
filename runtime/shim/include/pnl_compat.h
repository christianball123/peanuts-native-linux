// Peanuts Native for Linux: what Bionic's headers declare and glibc's (before 2.42) do not, included ahead of every host source.
#pragma once
#include <stdbool.h>      // Bionic and Apple headers bring bool in
#include <sys/types.h>
#include <pthread.h>
pid_t pthread_gettid_np(pthread_t t);   // glibc 2.42+ has it; pnl_misc.c otherwise
