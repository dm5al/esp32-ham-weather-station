/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Copyright (C) 2026 Dmitriy Aleksandrov, DM5AL. See LICENSE. */
/*
 * On-target checks for the pure-maths libraries.
 *
 * These would normally be host unit tests, but no host compiler is available in
 * this toolchain — only the Xtensa cross-compiler. Running them on the device
 * is no worse and arguably better: it exercises the real code on the real FPU,
 * where a float-vs-double mistake would actually show up.
 *
 * Run from the serial console with `selftest`.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Run all checks, logging each. @return number of failures. */
int selftest_run(void);

#ifdef __cplusplus
}
#endif
