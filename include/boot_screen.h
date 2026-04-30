// Pisces Moon OS — PocketMind Edition
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// fluidfortune.com

#ifndef BOOT_SCREEN_H
#define BOOT_SCREEN_H

#include <Arduino.h>

/**
 * Run the full PocketMind Edition boot sequence.
 * Call from setup() after display init and hardware checks.
 *
 * @param sd_ok    true if SD card mounted successfully
 * @param touch_ok true if GT911 touch initialized
 * @param pmu_ok   true if AXP2101 PMU initialized
 * @param rtc_ok   true if PCF85063 RTC initialized
 */
void run_boot_sequence(bool sd_ok, bool touch_ok, bool pmu_ok, bool rtc_ok);

#endif
