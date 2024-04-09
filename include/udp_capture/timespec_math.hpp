#pragma once


#include <time.h>
#include <cmath>

/**
 * @brief Normalize a tv_sec, tv_nsec pair into a timespec such that tv_nsec is in the range [0, 999999999]
 * 
 * Per the timespec specification, tv_nsec should be positive, in the range 0 to 999,999,999. It is always
 * added to tv_sec (even when tv_sec is negative).
 * For this reason, if t is the time in secs, tv_sec is actually floor(t), and nsec is (t - floor(t)) * 1.0e9.
 * 
 * @param tv_sec   (long) time in seconds. Msy be negative.
 * @param tv_nsec  (long) time in nanoseconds to be added to tv_sec. May be negative or >= 1000000000.
 * 
 * @return timespec  Normalized timespec value with tv_nsec in the range [0, 999999999]
 */
inline static struct timespec normalize_timespec(long tv_sec, long tv_nsec) {
    if (tv_nsec >= 1000000000) {
        tv_sec += tv_nsec / 1000000000;
        tv_nsec = tv_nsec % 1000000000;
    } else if (tv_nsec < 0) {
        if (tv_nsec <= -1000000000) {
            tv_sec -= -tv_nsec / 1000000000;
            tv_nsec = -(-tv_nsec % 1000000000);
        }
        if (tv_nsec != 0) {
            tv_sec -= 1;
            tv_nsec += 1000000000;
        }
    }
    return { tv_sec, tv_nsec };
}

/**
 * @brief Subtract two timespec values
 * 
 * @param time1 Ending time
 * @param time0 Starting time
 * @return timespec  (time1 - time0)
 */
inline static struct timespec timespec_subtract(const struct timespec& time1, const struct timespec& time0) {
    return normalize_timespec(time1.tv_sec - time0.tv_sec, time1.tv_nsec - time0.tv_nsec);
}

/**
 * @brief Add two timespec values
 * 
 * @param time1 Ending time
 * @param time0 Starting time
 * @return timespec  (time1 + time0)
 */
inline static struct timespec timespec_add(const struct timespec& time1, const struct timespec& time2) {
    return normalize_timespec(time1.tv_sec + time2.tv_sec, time1.tv_nsec + time2.tv_nsec);
}

/**
 * @brief Convert a timespec to a double in seconds
 * 
 * @param ts 
 * @return double 
 */
inline double timespec_to_secs(const struct timespec& ts) {
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
}

/**
 * @brief Convert a double in seconds to a timespec
 */
inline struct timespec secs_to_timespec(double secs) {
    auto sec = (long)floor(secs);
    auto nsec = (long)round((secs - (double)sec) * 1.0e9);
    return normalize_timespec(sec, nsec);
}

