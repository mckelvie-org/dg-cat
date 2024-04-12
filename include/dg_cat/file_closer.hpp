#pragma once

#include <unistd.h>

/**
 * @brief A class that closes a file descriptor when it goes out of scope.
 */
class FileCloser {
public:
    int fd;

public:
    FileCloser(int _fd) : fd(_fd) {}

    void close() {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    ~FileCloser() {
        close();
    }
};

