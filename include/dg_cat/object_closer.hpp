#pragma once

#include <unistd.h>

/**
 * @brief A class that calls close() on an object when it goes out of scope.
 */
template<class _T> class ObjectCloser {
private:
    _T *_obj;

public:
    ObjectCloser() : _obj(nullptr) {}
    ObjectCloser(_T *obj) : _obj(obj) {}
    ObjectCloser(const ObjectCloser &) = delete;
    ObjectCloser &operator=(const ObjectCloser &) = delete;
    ObjectCloser(ObjectCloser &&other) : _obj(other._obj) {
        other._obj = nullptr;
    }
    ObjectCloser &swap(ObjectCloser& other) {
        if (this != &other) {
            _T *tmp = _obj;
            _obj = other._obj;
            other._obj = tmp;
        }
        return *this;
    }
    ObjectCloser &operator=(ObjectCloser &&other) {
        if (this != &other) {
            close();
            _obj = other.detach();
        }
    }
    
    void reset(_T *obj) {
        close();
        _obj = obj;
    }

    void close() {
        if (_obj != nullptr) {
            _obj->close();
            _obj = nullptr;
        }
    }

    _T *detach() {
        _T *obj = _obj;
        _obj = nullptr;
        return obj;
    }

    _T *operator->() const {
        return _obj;
    }

    _T &operator*() const {
        return *_obj;
    }

    _T *get() const {
        return _obj;
    }

    ~ObjectCloser() {
        close();
    }
};

