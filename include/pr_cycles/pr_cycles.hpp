/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

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

#include <render/session.h>
#include <render/scene.h>

#endif
