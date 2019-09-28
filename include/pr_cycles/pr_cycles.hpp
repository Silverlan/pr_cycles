#ifndef __PR_CYCLES_HPP__
#define __PR_CYCLES_HPP__

#ifdef _WIN32
#define PRAGMA_EXPORT __declspec(dllexport)
#define PRAGMA_IMPORT __declspec(dllimport)
#else
#define PRAGMA_EXPORT __attribute__((visibility("default")))
#define PRAGMA_IMPORT
#endif

#include <render/session.h>
#include <render/scene.h>

#endif
