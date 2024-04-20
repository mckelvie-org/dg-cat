// (c) 2008, Timo Bingmann from http://idlebox.net/
// published under the WTFPL v2.0
// Modified 2024 by Sam McKelvie to include line numbers and filenames

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
#include <cxxabi.h>


/** Print a demangled stack backtrace of the caller function to FILE* out. */
void print_stacktrace(unsigned int max_frames = 63)
{
    fprintf(stderr, "Stack trace:\n");

    // storage array for stack trace address data
    void* addrlist[max_frames+1];

    // retrieve current stack addresses
    int addrlen = backtrace(addrlist, sizeof(addrlist) / sizeof(void*));

    if (addrlen == 0) {
        fprintf(stderr, "  <empty, possibly corrupt>\n");
        return;
    }

    // resolve addresses into strings containing "filename(function+address)",
    // this array must be free()-ed
    char** symbollist = backtrace_symbols(addrlist, addrlen);

    // allocate string which will be filled with the demangled function name
    size_t funcnamesize = 256;
    char* funcname = (char*)malloc(funcnamesize);

    // iterate over the returned symbol lines. skip the first, it is the
    // address of this function.
    for (int i = 1; i < addrlen; i++)
    {
        //fprintf(stderr, "\n[addr]: [%p] [symbols:] %s\n", addrlist[i], symbollist[i]);

        char *begin_filename = symbollist[i];
        char *end_filename = nullptr;
        char *begin_name = nullptr;
        char *end_name = nullptr;
        char *begin_offset = nullptr;
        char *end_offset = nullptr;
        char *end_symbol_text = symbollist[i] + strlen(symbollist[i]);

        // find parentheses and +address offset surrounding the mangled name:
        // ./module(function+0x15c) [0x8048a6d]
        for (char *p = symbollist[i]; *p; ++p) {
            if (*p == '(') {
                begin_name = p + 1;
                if (end_filename == nullptr) {
                    end_filename = p;
                }
            } else if (*p == ' ') {
                if (end_filename == nullptr) {
                    end_filename = p;
                }
            } else if (*p == '+') {
                begin_offset = p;
                if (begin_name) {
                    end_name = p;
                }
            } else if (*p == ')') {
                if (begin_offset) {
                    end_offset = p;
                }
                if (begin_name) {
                    end_name = p;
                }
            }
        }

        if (begin_filename != nullptr && end_filename == nullptr) {
            end_filename = end_symbol_text;
        }

        if (begin_name != nullptr && end_name == nullptr) {
            end_name = end_symbol_text;
        }

        if (begin_offset != nullptr && end_offset == nullptr) {
            end_offset = end_symbol_text;
        }

        if (begin_filename && begin_name && begin_offset) {
            *end_name = '\0';
            *end_offset = '\0';
            *end_filename = '\0';

            // mangled name is now in [begin_name, end_name) and caller
            // offset in [begin_offset, end_offset). now apply
            // __cxa_demangle():

            int status;
            char* demangled = abi::__cxa_demangle(begin_name,
                            funcname, &funcnamesize, &status);
            if (status == 0) {
                funcname = demangled; // use possibly realloc()-ed string
                fprintf(stderr, "  %s : %s+%s  ",
                    symbollist[i], funcname, begin_offset);
            } else {
                // demangling failed. Output function name as a C function with
                // no arguments.
                fprintf(stderr, "  %s : %s()+%s  ",
                    symbollist[i], begin_name, begin_offset);
            }
            char syscom[1200];
            sprintf(syscom,"addr2line %s -e %s >/dev/stderr", begin_offset, begin_filename);
            // fprintf(stderr, "\n[running syscom]: %s\n", syscom);
            int ret = system(syscom);
            if (ret != 0) {
                fprintf(stderr, "\n");
            }
        } else {
            // couldn't parse the line? print the whole line.
            fprintf(stderr, "  %s\n", symbollist[i]);
        }
    }

    free(funcname);
    free(symbollist);
}
