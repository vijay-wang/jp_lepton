/*
 * perf_tick.h - Cross-platform high-resolution elapsed-time measurement
 *
 * Usage:
 *   perf_tick_t t0, t1;
 *   perf_tick_now(&t0);
 *   ... code to measure ...
 *   perf_tick_now(&t1);
 *   printf("elapsed: %lld us\n", (long long)perf_tick_diff_us(&t0, &t1));
 *   printf("elapsed: %lld ns\n", (long long)perf_tick_diff_ns(&t0, &t1));
 *
 * Platforms:
 *   Windows : QueryPerformanceCounter  (hardware tick, ~100 ns resolution)
 *   POSIX   : clock_gettime(CLOCK_MONOTONIC) (nanosecond resolution)
 *
 * Both sources are monotonic and unaffected by wall-clock adjustments.
 */

#ifndef PERF_TICK_H
#define PERF_TICK_H

#include <stdint.h>

#if defined(_WIN32) || defined(_WIN64)
#  include <windows.h>
typedef LARGE_INTEGER perf_tick_t;
#else
#  include <time.h>
typedef struct timespec perf_tick_t;
#endif

/* -------------------------------------------------------------------------
 * perf_tick_now
 *   Capture the current high-resolution counter into *t.
 * ---------------------------------------------------------------------- */
static inline void perf_tick_now(perf_tick_t *t)
{
#if defined(_WIN32) || defined(_WIN64)
	QueryPerformanceCounter(t);
#else
	clock_gettime(CLOCK_MONOTONIC, t);
#endif
}

/* -------------------------------------------------------------------------
 * perf_tick_freq
 *   Windows only: cache QueryPerformanceFrequency to avoid repeated syscalls.
 * ---------------------------------------------------------------------- */
#if defined(_WIN32) || defined(_WIN64)
static inline int64_t __perf_tick_freq(void)
{
	static int64_t freq = 0;
	if (freq == 0) {
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		freq = (int64_t)f.QuadPart;
	}
	return freq;
}
#endif

/* -------------------------------------------------------------------------
 * perf_tick_diff_ns
 *   Elapsed nanoseconds from *start to *end.
 * ---------------------------------------------------------------------- */
static inline int64_t perf_tick_diff_ns(const perf_tick_t *start,
		const perf_tick_t *end)
{
#if defined(_WIN32) || defined(_WIN64)
	return (int64_t)((end->QuadPart - start->QuadPart) * 1000000000LL
			/ __perf_tick_freq());
#else
	return (int64_t)(end->tv_sec  - start->tv_sec)  * 1000000000LL
		+ (int64_t)(end->tv_nsec - start->tv_nsec);
#endif
}

/* -------------------------------------------------------------------------
 * perf_tick_diff_us
 *   Elapsed microseconds from *start to *end.
 * ---------------------------------------------------------------------- */
static inline int64_t perf_tick_diff_us(const perf_tick_t *start,
		const perf_tick_t *end)
{
	return perf_tick_diff_ns(start, end) / 1000LL;
}

/* -------------------------------------------------------------------------
 * perf_tick_diff_ms
 *   Elapsed milliseconds from *start to *end.
 * ---------------------------------------------------------------------- */
static inline int64_t perf_tick_diff_ms(const perf_tick_t *start,
		const perf_tick_t *end)
{
	return perf_tick_diff_ns(start, end) / 1000000LL;
}

/* -------------------------------------------------------------------------
 * perf_tick_diff_s
 *   Elapsed seconds (double) from *start to *end.
 * ---------------------------------------------------------------------- */
static inline double perf_tick_diff_s(const perf_tick_t *start,
		const perf_tick_t *end)
{
	return (double)perf_tick_diff_ns(start, end) / 1e9;
}

/* -------------------------------------------------------------------------
 * PERF_MEASURE_US(code, result_us)
 *   Convenience macro: run `code`, store elapsed microseconds in `result_us`.
 * ---------------------------------------------------------------------- */

#define PERF_MEASURE_US(result_us, ...)         \
	do {						\
			perf_tick_t _t0, _t1;           \
			perf_tick_now(&_t0);            \
			__VA_ARGS__;                    \
			perf_tick_now(&_t1);            \
			(result_us) = perf_tick_diff_us(&_t0, &_t1); \
	} while (0)

#endif /* PERF_TICK_H */
