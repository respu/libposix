/* This is free and unencumbered software released into the public domain. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pathname.h"

#include "error.h"

#include <cassert>      /* for assert() */
#include <cerrno>       /* for errno */
#include <cstring>      /* for std::strcpy(), GNU basename() */
#include <libgen.h>     /* for basename(), dirname() */

#undef basename         /* get rid of the glibc macro */

using namespace posix;

pathname
pathname::dirname() const {
  char buffer[_string.size() + 1];
  std::strcpy(buffer, _string.c_str());
  return pathname(::dirname(buffer));
}

pathname
pathname::basename() const {
  char buffer[_string.size() + 1];
  std::strcpy(buffer, _string.c_str());
  return pathname(::basename(buffer));
}