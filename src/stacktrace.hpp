# pragma once

#include <cstdio>

/** Print a demangled stack backtrace of the caller function to FILE* out. */
extern void print_stacktrace(unsigned int max_frames=63);
