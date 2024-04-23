/**
 * Copyright (c) 2024 Samuel J. McKelvie
 *
 * MIT License - See LICENSE file accompanying this package.
 */
#pragma once

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <vector>
#include <memory>
#include <system_error>
#include <cassert>
#include <stdexcept>

class AddrInfoResultRef {
    /**
     * @brief A class that wraps the result of getaddrinfo() and provides RAII semantics for the addrinfo linked list structure.
     *       The addrinfo structure is freed with freeaddrinfo() when the object is destroyed.
     *       Instances of this class cannot be copied, but can be moved. Use AddrInfoList for a shared abstraction that can be copied.
     */

public:
    AddrInfoResultRef() : _addrinfo(nullptr) {}

    explicit AddrInfoResultRef(struct addrinfo *_addrinfo) : _addrinfo(_addrinfo) {}

    // AddrInfoResultRef cannot be copied due to the single-owner nature of freeaddrinfo(). Use AddrInfoList instead.
    AddrInfoResultRef(const AddrInfoResultRef&) = delete;
    AddrInfoResultRef& operator=(const AddrInfoResultRef&) = delete;

    AddrInfoResultRef(AddrInfoResultRef&& other) :
        _addrinfo(other._addrinfo)
    {
        other._addrinfo = nullptr;
    }

    AddrInfoResultRef& operator=(AddrInfoResultRef&& other) {
        if (this != &other) {
            if (_addrinfo != nullptr) {
                freeaddrinfo(_addrinfo);
            }
            _addrinfo = other._addrinfo;
            other._addrinfo = nullptr;
        }
        return *this;
    }

    void swap(AddrInfoResultRef& other) {
        std::swap(_addrinfo, other._addrinfo);
    }

    ~AddrInfoResultRef() {
        if (_addrinfo != nullptr) {
            freeaddrinfo(_addrinfo);
        }
    }

    const struct addrinfo * get() const {
        return _addrinfo;
    }

    const struct addrinfo& operator*() const {
        if (_addrinfo == nullptr) {
              throw std::out_of_range("Attempt to dereference null addrinfo");
        }
        return *_addrinfo;
    }

    const struct addrinfo *operator->() const {
        if (_addrinfo == nullptr) {
              throw std::out_of_range("Attempt to dereference null addrinfo");
        }
        return _addrinfo;
    }

private:
    struct addrinfo *_addrinfo;
};

class AddrInfoList {
    /**
     * @brief A class that wraps the getaddrinfo() result and provides a range-based interface to the results.
     *        Provides RAII semantics for the addrinfo structure (calls freaddrinfo() on release of last reference). A
     *        shared reference is used so that this class can be copied and assigned.
     */
public:
    class Entry {
        /**
         * @brief A class that wraps a single addrinfo entry in an AddrInfoResultRef list. The AddrInfoResultRef is kept alive by a shared_ptr.
         */
    public:
        Entry() : _addrinfo(nullptr) {}
        Entry(const std::shared_ptr<AddrInfoResultRef>& addrinfo_result_ref, const struct addrinfo *addrinfo) :
            _addrinfo_result_ref(addrinfo_result_ref),
            _addrinfo(addrinfo)
        {
        }
        Entry(const Entry&) = default;
        Entry& operator=(const Entry&) = default;
        Entry(Entry&& other) :
            _addrinfo_result_ref(std::move(other._addrinfo_result_ref)),
            _addrinfo(other._addrinfo)
        {
            other._addrinfo = nullptr;
        }
        Entry& operator=(Entry&& other) {
            if (this != &other) {
                _addrinfo_result_ref = std::move(other._addrinfo_result_ref);
                _addrinfo = other._addrinfo;
                other._addrinfo = nullptr;
            }
            return *this;
        }

        const struct addrinfo * get() const {
            return _addrinfo;
        }

        const struct addrinfo& operator*() const {
            if (_addrinfo == nullptr) {
                  throw std::out_of_range("Attempt to dereference null addrinfo");
            }
            return *_addrinfo;
        }

        const struct addrinfo *operator->() const {
            if (_addrinfo == nullptr) {
                  throw std::out_of_range("Attempt to dereference null addrinfo");
            }
            return _addrinfo;
        }

        /**
         * @brief Return a string representation of the address (e.g., IPv4 or IPv6 address) in the entry
         * 
         * @return std::string String representation of address
         */
        std::string addr_string() const {
            char buf[NI_MAXHOST];
            if (getnameinfo(_addrinfo->ai_addr, _addrinfo->ai_addrlen, buf, sizeof(buf), nullptr, 0, NI_NUMERICHOST) != 0) {
                throw std::system_error(errno, std::system_category(), "getnameinfo() failed");
            }
            return std::string(buf);
        }

    private:
        std::shared_ptr<AddrInfoResultRef> _addrinfo_result_ref;
        const struct addrinfo *_addrinfo;
    };

    class iterator {
        /**
         * @brief A class that provides an iterator iterator for AddrInfoList. Lifetime is scoped to the AddrInfoList.
         */
    public:
        iterator(const AddrInfoList *addrinfo_list, size_t index) :
            _addrinfo_list(addrinfo_list),
            _index(index)
        {
        }
        iterator(const iterator&) = default;
        iterator& operator=(const iterator&) = default;

        const Entry& operator*() const {
            if (_index >= _addrinfo_list->size()) {
                  throw std::out_of_range("Attempt to dereference addrinfo at end()");
            }
            return (*_addrinfo_list)[_index];
        }

        const Entry *operator->() const {
            if (_index >= _addrinfo_list->size()) {
                return nullptr;
            }
            return &((*_addrinfo_list)[_index]);
        }

        iterator& operator++() {
            if (_index >= _addrinfo_list->size()) {
                  throw std::out_of_range("Attempt to advance addrinfo iterator past end()");
            }
            ++_index;
            return *this;
        }

        bool operator==(const iterator& other) const {
            return _addrinfo_list == other._addrinfo_list && _index == other._index;
        }

        bool operator!=(const iterator& other) const {
            return !(*this == other);
        }

    private:
        const AddrInfoList * _addrinfo_list;
        size_t _index;
    };

public:
    AddrInfoList() {}

    explicit AddrInfoList(struct addrinfo *addrinfo) : 
        _addrinfo(std::make_shared<AddrInfoResultRef>(addrinfo))
    {
        updateResults();
    }

    AddrInfoList(
        const char *__restrict name,
        const char *__restrict service,
        const struct addrinfo *__restrict req
    )
    {
        struct addrinfo *raw_addrinfo = nullptr;
        if (getaddrinfo(name, service, req, &raw_addrinfo) != 0) {
            throw std::system_error(errno, std::system_category(), "getaddrinfo() failed");
        }
        _addrinfo = std::make_shared<AddrInfoResultRef>(raw_addrinfo);
        updateResults();
    }

    explicit AddrInfoList(
        const char *__restrict name,  // DNS name or IP address
        const char *__restrict service = nullptr,  // nullptr means "any service"
        int ai_flags = AI_CANONNAME, // AI_PASSIVE, AI_CANONNAME, AI_NUMERICHOST, AI_NUMERICSERV
        int ai_family = PF_UNSPEC,  // AF_INET, AF_INET6
        int ai_socktype = 0  // SOCK_DGRAM, SOCK_STREAM

    )
    {
        struct addrinfo req;
        memset(&req, 0, sizeof(req));
        req.ai_flags = ai_flags;
        req.ai_family = ai_family;
        req.ai_socktype = ai_socktype;
        struct addrinfo *raw_addrinfo = nullptr;
        if (getaddrinfo(name, service, &req, &raw_addrinfo) != 0) {
            throw std::system_error(errno, std::system_category(), "getaddrinfo() failed");
        }
        _addrinfo = std::make_shared<AddrInfoResultRef>(raw_addrinfo);
        updateResults();
    }

    // AddrInfoList cannot be copied

    AddrInfoList(const AddrInfoList&) = default;
    AddrInfoList& operator=(const AddrInfoList&) = default;

    AddrInfoList(AddrInfoList&& other) :
        _addrinfo(std::move(other._addrinfo)),
        _results(std::move(other._results))
    {
    }

    AddrInfoList& operator=(AddrInfoList&& other) {
        _addrinfo = std::move(other._addrinfo);
        _results = std::move(other._results);
        return *this;
    }

    size_t size() const {
        return _results.size();
    }

    const Entry& operator[](size_t i) const {
        return _results[i];
    }

    const iterator begin() const {
        return iterator(this, 0);
    }

    const iterator end() const {
        return iterator(this, size());
    }

    const struct addrinfo * get_raw() const {
        return _addrinfo->get();
    }

private:
    void updateResults() {
        const struct addrinfo *pai = _addrinfo->get();
        while (pai != nullptr) {
            _results.push_back(Entry(_addrinfo, pai));
            pai = pai->ai_next;
        }
    }

private:
    std::shared_ptr<AddrInfoResultRef> _addrinfo;
    std::vector<Entry> _results;
};

