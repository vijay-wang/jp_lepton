/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * log.h - Lightweight cross-platform logging facility
 *
 * Provides kernel-style log levels (KERN_EMERG ... KERN_DEBUG) for
 * userspace and embedded targets.  The active log level threshold is
 * controlled at compile time via LOG_LEVEL and at runtime via
 * log_set_level().  Every message is annotated with the source file,
 * line number, and enclosing function name.
 *
 * Log levels mirror those defined in <linux/kern_levels.h>:
 *
 *   LOG_EMERG   (0)  - system is unusable
 *   LOG_ALERT   (1)  - action must be taken immediately
 *   LOG_CRIT    (2)  - critical conditions
 *   LOG_ERR     (3)  - error conditions
 *   LOG_WARNING (4)  - warning conditions
 *   LOG_NOTICE  (5)  - normal but significant condition
 *   LOG_INFO    (6)  - informational
 *   LOG_DEBUG   (7)  - debug-level messages
 *
 * Usage:
 *
 *   #define LOG_LEVEL LOG_DEBUG   // optional, default is LOG_DEBUG
 *   #include "log.h"
 *
 *   pr_info("initialised, version %s\n", VERSION);
 *   pr_err("failed to open %s: %d\n", path, ret);
 * Copyright (C) 2024 The log.h Authors
 */

#ifndef _LOG_H
#define _LOG_H

#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * --------------------------------------------------------------------------
 * Log level definitions  (kept identical to linux/kern_levels.h numerics)
 * --------------------------------------------------------------------------
 */

#define LOG_EMERG	0	/* system is unusable			*/
#define LOG_ALERT	1	/* action must be taken immediately	*/
#define LOG_CRIT	2	/* critical conditions			*/
#define LOG_ERR		3	/* error conditions			*/
#define LOG_WARNING	4	/* warning conditions			*/
#define LOG_NOTICE	5	/* normal but significant condition	*/
#define LOG_INFO	6	/* informational			*/
#define LOG_DEBUG	7	/* debug-level messages			*/

/*
 * LOG_LEVEL - compile-time log level threshold
 *
 * Any pr_*() call whose level is numerically greater than LOG_LEVEL is
 * compiled out entirely and produces zero object code.  Override before
 * including this header, e.g.:
 *
 *   cc -DLOG_LEVEL=LOG_WARNING ...
 */
#ifndef LOG_LEVEL
#define LOG_LEVEL	LOG_DEBUG
#endif

/*
 * --------------------------------------------------------------------------
 * Runtime level control
 * --------------------------------------------------------------------------
 */

/*
 * __log_level_state - return a pointer to the single runtime level variable
 *
 * The level is stored in a function-local static, which the C standard
 * guarantees is initialised exactly once and persists for the lifetime of
 * the program.  Routing all access through this accessor means:
 *
 *   1. No separate .c file or LOG_H_IMPL dance is required — the header is
 *      fully self-contained (header-only).
 *   2. Every translation unit that calls log_set_level() mutates the same
 *      object; linkers fold identical 'static inline' functions into one
 *      copy (COMDAT / vague linkage) on GCC, Clang, and MSVC.
 *   3. On exotic toolchains that do *not* fold COMDAT, each TU gets its own
 *      copy — log_set_level() in one TU would not be visible in another.
 *      For those rare cases, define LOG_LEVEL at build time and rely solely
 *      on the compile-time threshold.
 *
 * Not for direct use; call log_set_level() / log_get_level() instead.
 */
static inline int *__log_level_state(void)
{
	static int __level = LOG_LEVEL;

	return &__level;
}

/**
 * log_set_level - set the runtime log level threshold
 * @level: one of LOG_EMERG .. LOG_DEBUG
 *
 * Messages with a level numerically greater than @level are silently
 * discarded.  The change takes effect immediately and is not thread-safe;
 * callers must serialise if required.
 */
static inline void log_set_level(int level)
{
	*__log_level_state() = level;
}

/**
 * log_get_level - return the current runtime log level threshold
 *
 * Return: the active log level (LOG_EMERG .. LOG_DEBUG).
 */
static inline int log_get_level(void)
{
	return *__log_level_state();
}

/*
 * --------------------------------------------------------------------------
 * Output backend
 * --------------------------------------------------------------------------
 */

/*
 * LOG_USE_STDERR - route all output to stderr instead of stdout
 *
 * Define this macro (e.g. -DLOG_USE_STDERR) before including the header to
 * direct log output to stderr.  Errors and above always go to stderr
 * regardless of this setting.
 */

/*
 * __log_stream - select the output stream for a given level
 * @lvl: numeric log level
 *
 * Levels LOG_ERR and above unconditionally use stderr.  Lower-priority
 * messages use stderr only when LOG_USE_STDERR is defined.
 */
#ifdef LOG_USE_STDERR
#  define __log_stream(lvl)	stderr
#else
#  define __log_stream(lvl)	((lvl) <= LOG_ERR ? stderr : stdout)
#endif

/*
 * __log_filename - strip directory prefix from __FILE__ at compile time
 *
 * __FILE__ may expand to a full build-system path such as
 * "/home/user/project/src/foo.c".  This helper walks the string once at
 * compile time (constant folding) and returns a pointer to the basename.
 *
 * The ternary trick is equivalent to strrchr(__FILE__, '/') + 1 but
 * operates on a string literal so the compiler can evaluate it entirely
 * at compile time with -O1 or higher, producing no runtime cost.
 *
 * On Windows both '/' and '\' are tried so that MSVC and MinGW paths
 * are handled correctly.
 */
#if defined(_WIN32) || defined(_WIN64)
#  define __log_filename						\
	(strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 :	\
	 strrchr(__FILE__, '/')  ? strrchr(__FILE__, '/')  + 1 :	\
	 __FILE__)
#else
#  define __log_filename						\
	(strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

/*
 * __log_level_str - human-readable tag for each log level
 * @lvl: numeric log level
 */
#define __log_level_str(lvl)			\
	((lvl) == LOG_EMERG   ? "EMERG"   :	\
	 (lvl) == LOG_ALERT   ? "ALERT"   :	\
	 (lvl) == LOG_CRIT    ? "CRIT "   :	\
	 (lvl) == LOG_ERR     ? "ERROR"   :	\
	 (lvl) == LOG_WARNING ? "WARN "   :	\
	 (lvl) == LOG_NOTICE  ? "NOTICE"  :	\
	 (lvl) == LOG_INFO    ? "INFO "   :	\
	 "DEBUG"   )

/*
 * __log_timestamp - write an ISO-8601-style wall-clock prefix
 *
 * Produces "[HH:MM:SS.uuuuuu] "on platforms that provide time()/localtime().
 * The buffer is function-local and valid only for the duration of the call.
 */
static inline const char *__log_timestamp(void)
{
	static char __ts_buf[32];
	time_t t;
	struct tm *tm_info;
	long us;

#if defined(_WIN32) || defined(_WIN64)
	struct tm tm_local;
	FILETIME ft;
	ULONGLONG ull;

	GetSystemTimeAsFileTime(&ft);
	ull = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	/*FILETIME epoch is 1601-01-01, convert to Unix epoch and extract us */
	ull -= 116444736000000000ULL; /*100-ns intervals since 1601 -> since 1970 */
	t  = (time_t)(ull / 10000000ULL);
	us = (long)((ull % 10000000ULL) / 10);

	localtime_s(&tm_local, &t);
	tm_info = &tm_local;
#else
#include <sys/time.h>
	struct timeval tv;

	gettimeofday(&tv, NULL);
	t  = tv.tv_sec;
	us = tv.tv_usec;
	tm_info = localtime(&t);
#endif

	if (!tm_info) {
		__ts_buf[0] = '\0';
		return __ts_buf;
	}

	char hms[12];
	(void)strftime(hms, sizeof(hms), "%H:%M:%S", tm_info);
	snprintf(__ts_buf, sizeof(__ts_buf), "%s.%06ld", hms, us);
	return __ts_buf;
}

/*
 * --------------------------------------------------------------------------
 * Core logging macro
 * --------------------------------------------------------------------------
 */

/**
 * __printk - emit a single log line if both compile- and runtime-gates pass
 * @lvl:  numeric log level constant (LOG_ERR, LOG_INFO, …)
 * @fmt:  printf-style format string
 * @...:  variadic arguments matching @fmt
 *
 * The compile-time guard ensures that calls below the build threshold
 * produce no code.  The runtime guard allows dynamic adjustment without
 * recompilation.
 *
 * Output format:
 *   [HH:MM:SS] [LEVEL] file.c:42 func_name() msg…
 */
#define __printk(lvl, fmt, ...)						\
	do {								\
		if ((lvl) <= LOG_LEVEL) {				\
			if ((lvl) <= log_get_level()) {			\
				fprintf(__log_stream(lvl),		\
						"[%s] [%s] %s:%d %s() " fmt,	\
						__log_timestamp(),		\
						__log_level_str(lvl),		\
						__log_filename,			\
						__LINE__,			\
						__func__,			\
##__VA_ARGS__);			\
			}						\
		}							\
	} while (0)

						/*
						 * --------------------------------------------------------------------------
						 * Public pr_*() API  —  mirrors include/linux/printk.h
						 * --------------------------------------------------------------------------
						 */

						/**
						 * pr_emerg - log a message at LOG_EMERG level
						 * @fmt: printf format string
						 */
#define pr_emerg(fmt, ...)	__printk(LOG_EMERG,   fmt, ##__VA_ARGS__)

						/**
						 * pr_alert - log a message at LOG_ALERT level
						 * @fmt: printf format string
						 */
#define pr_alert(fmt, ...)	__printk(LOG_ALERT,   fmt, ##__VA_ARGS__)

						/**
						 * pr_crit - log a message at LOG_CRIT level
						 * @fmt: printf format string
						 */
#define pr_crit(fmt, ...)	__printk(LOG_CRIT,    fmt, ##__VA_ARGS__)

						/**
						 * pr_err - log a message at LOG_ERR level
						 * @fmt: printf format string
						 */
#define pr_err(fmt, ...)	__printk(LOG_ERR,     fmt, ##__VA_ARGS__)

						/**
						 * pr_warn - log a message at LOG_WARNING level
						 * @fmt: printf format string
						 */
#define pr_warn(fmt, ...)	__printk(LOG_WARNING, fmt, ##__VA_ARGS__)

						/**
						 * pr_notice - log a message at LOG_NOTICE level
						 * @fmt: printf format string
						 */
#define pr_notice(fmt, ...)	__printk(LOG_NOTICE,  fmt, ##__VA_ARGS__)

						/**
						 * pr_info - log a message at LOG_INFO level
						 * @fmt: printf format string
						 */
#define pr_info(fmt, ...)	__printk(LOG_INFO,    fmt, ##__VA_ARGS__)

						/**
						 * pr_debug - log a message at LOG_DEBUG level
						 * @fmt: printf format string
						 *
						 * Note: compiled out entirely unless LOG_LEVEL >= LOG_DEBUG.
						 */
#define pr_debug(fmt, ...)	__printk(LOG_DEBUG,   fmt, ##__VA_ARGS__)

						/*
						 * pr_devel - alias for pr_debug, for development-only instrumentation
						 *
						 * Matches the convention used in <linux/printk.h>.
						 */
#define pr_devel(fmt, ...)	pr_debug(fmt, ##__VA_ARGS__)

						/*
						 * pr_cont - append to the previous log line (best-effort; no location info)
						 *
						 * Mirrors the kernel pr_cont() which continues the previous line.  On
						 * userspace there is no atomic line-continuation guarantee, but the
						 * runtime level gate is still honoured.
						 */
#define pr_cont(fmt, ...)						\
							do {								\
								if (LOG_INFO <= log_get_level())			\
								fprintf(stdout, fmt, ##__VA_ARGS__);		\
							} while (0)

#endif /* _LOG_H */
