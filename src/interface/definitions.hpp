// SPDX-FileCopyrightText: (c) 2024 Silverlan <opensource@pragma-engine.com>
// SPDX-License-Identifier: MIT

#ifndef __PR_CYCLES_HPP__
#define __PR_CYCLES_HPP__

#ifdef _WIN32
#define PRAGMA_EXPORT __declspec(dllexport)
#define PRAGMA_IMPORT __declspec(dllimport)
#else
#define PRAGMA_EXPORT __attribute__((visibility("default")))
#define PRAGMA_IMPORT
#endif

// #define ENABLE_MOTION_BLUR_TEST

#define ENABLE_TEST_AMBIENT_OCCLUSION

#endif
