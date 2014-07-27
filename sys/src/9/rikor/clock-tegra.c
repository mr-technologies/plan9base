/*
 * tegra 2 SoC clocks; excludes cortex-a timers.
 *
 * SoC provides these shared clocks:
 * 4 29-bit count-down `timers' @ 1MHz,
 * 1 32-bit count-up time-stamp counter @ 1MHz,
 * and a real-time clock @ 32KHz.
 * the tegra watchdog (tegra 2 ref man §5.4.1) is tied to timers, not rtc.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "arm.h"

