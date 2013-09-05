/* This is free and unencumbered software released into the public domain. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "descriptor.h"

#include "error.h"
#include "group.h"
#include "mode.h"
#include "user.h"

#include <array>      /* for std::array */
#include <cassert>    /* for assert() */
#include <cerrno>     /* for errno */
#include <cstdint>    /* for std::uint8_t */
#include <cstring>    /* for std::strlen() */
#include <fcntl.h>    /* for F_*, fcntl() */
#include <stdexcept>  /* for std::invalid_argument */
#include <string>     /* for std::string */
#include <sys/stat.h> /* for fchmod() */
#include <unistd.h>   /* for close(), fchown(), fsync(), read(), write() */

using namespace posix;

static const std::string invalid_in_copy_constructor =
  "invalid descriptor passed to copy constructor";

descriptor::descriptor(const descriptor& other) {
  if (other.valid()) {
    const bool cloexec_flag = other.cloexec();
#ifdef F_DUPFD_CLOEXEC
    /* POSIX.1-2008 feature to set FD_CLOEXEC atomically: */
    const int fcntl_cmd = cloexec_flag ? F_DUPFD_CLOEXEC : F_DUPFD;
    _fd = ::fcntl(other._fd, fcntl_cmd, 0);
#else
    _fd = ::fcntl(other._fd, F_DUPFD, 0);
#endif

    if (_fd == -1) {
      switch (errno) {
        case EBADF:  /* Bad file descriptor */
          throw std::invalid_argument(invalid_in_copy_constructor);
        case EMFILE: /* Too many open files */
          throw posix::fatal_error(errno);
        default:
          throw posix::error(errno);
      }
    }

#ifndef F_DUPFD_CLOEXEC
    if (cloexec_flag) {
      cloexec(true);
    }
#endif
  }
}

descriptor::~descriptor() noexcept {
  close();
}

bool
descriptor::readable() const {
  const int status_ = status();
  return status_ & O_RDONLY || status_ & O_RDWR;
}

bool
descriptor::writable() const {
  const int status_ = status();
  return status_ & O_WRONLY || status_ & O_RDWR;
}

int
descriptor::flags() const {
  return fcntl(F_GETFD);
}

int
descriptor::status() const {
  return fcntl(F_GETFL);
}

bool
descriptor::cloexec() const {
  return fcntl(F_GETFD) & FD_CLOEXEC;
}

void
descriptor::cloexec(const bool state) {
  const int flags = fcntl(F_GETFD);
  fcntl(F_SETFD, state ? flags | FD_CLOEXEC : flags & ~FD_CLOEXEC);
}

descriptor
descriptor::dup() const {
  return descriptor(*this); /* rely on copy constructor */
}

int
descriptor::fcntl(const int cmd) const {
  const int result = ::fcntl(_fd, cmd);
  if (result == -1) {
    switch (errno) {
      case EBADF: /* Bad file descriptor */
        throw posix::bad_descriptor();
      default:
        throw posix::error(errno);
    }
  }
  return result;
}

int
descriptor::fcntl(const int cmd,
                  const int arg) {
  const int result = ::fcntl(_fd, cmd, arg);
  if (result == -1) {
    switch (errno) {
      case EBADF: /* Bad file descriptor */
        throw posix::bad_descriptor();
      default:
        throw posix::error(errno);
    }
  }
  return result;
}

int
descriptor::fcntl(const int cmd,
                  void* const arg) {
  const int result = ::fcntl(_fd, cmd, arg);
  if (result == -1) {
    switch (errno) {
      case EBADF: /* Bad file descriptor */
        throw posix::bad_descriptor();
      default:
        throw posix::error(errno);
    }
  }
  return result;
}

void
descriptor::chown(const user& user,
                  const group& group) {
  const uid_t uid = static_cast<uid_t>(user.id());
  const gid_t gid = static_cast<gid_t>(group.id());

  if (fchown(_fd, uid, gid) == -1) {
    switch (errno) {
      case ENOMEM: /* Cannot allocate memory in kernel */
        throw posix::fatal_error(errno);
      case EBADF:  /* Bad file descriptor */
        throw posix::bad_descriptor();
      default:
        throw posix::error(errno);
    }
  }
}

void
descriptor::chmod(const mode mode) {
  if (fchmod(_fd, static_cast<mode_t>(mode)) == -1) {
    switch (errno) {
      case ENOMEM: /* Cannot allocate memory in kernel */
        throw posix::fatal_error(errno);
      case EBADF:  /* Bad file descriptor */
        throw posix::bad_descriptor();
      default:
        throw posix::error(errno);
    }
  }
}

void
descriptor::write(const std::string& string) {
  return write(string.data(), string.size());
}

void
descriptor::write(const char* const data) {
  assert(data != nullptr);

  return write(data, std::strlen(data));
}

void
descriptor::write(const void* const data,
                  const std::size_t size) {
  assert(data != nullptr);

  std::size_t pos = 0;
  while (pos < size) {
    const ssize_t rc = ::write(fd(), reinterpret_cast<const std::uint8_t*>(data) + pos, size - pos);
    if (rc == -1) {
      switch (errno) {
        case EINTR:  /* Interrupted system call */
          continue;
        case ENOMEM: /* Cannot allocate memory in kernel */
          throw posix::fatal_error(errno);
        case EBADF:  /* Bad file descriptor */
          throw posix::bad_descriptor();
        default:
          throw posix::error(errno);
      }
    }
    pos += rc;
  }
}

std::size_t
descriptor::read_line(std::string& buffer) {
  return read_until('\n', buffer);
}

std::size_t
descriptor::read_until(const char separator,
                       std::string& buffer) {
  std::size_t byte_count = 0;

  char character;
  while (read(character)) {
    byte_count++;

    if (character == separator)
      break; /* all done */

    buffer.push_back(character);
  }

  return byte_count;
}

std::size_t
descriptor::read(char& result) {
retry:
  const ssize_t rc = ::read(fd(), &result, sizeof(result));
  switch (rc) {
    case -1:
      switch (errno) {
        case EINTR: /* Interrupted system call */
          goto retry; /* try again */
        case EBADF: /* Bad file descriptor */
          throw posix::bad_descriptor();
        default:
          assert(errno != EFAULT);
          throw posix::error(errno);
      }

    case 0:
      return rc; /* EOF */

    default:
      assert(rc == 1);
      return rc;
  }
}

std::size_t
descriptor::read(void* const buffer,
                 const std::size_t buffer_size) {
  assert(buffer != nullptr);

  std::size_t byte_count = 0;

  while (byte_count < buffer_size) {
    const ssize_t rc = ::read(fd(),
      reinterpret_cast<std::uint8_t*>(buffer) + byte_count,
      buffer_size - byte_count);
    switch (rc) {
      case -1:
        switch (errno) {
          case EINTR: /* Interrupted system call */
            continue; /* try again */
          case EBADF:  /* Bad file descriptor */
            throw posix::bad_descriptor();
          default:
            throw posix::error(errno);
        }

      case 0:
        goto exit; /* end of file */

      default:
        assert(rc > 0);
        const std::size_t chunk_size = static_cast<std::size_t>(rc);
        byte_count += chunk_size;
    }
  }

exit:
  return byte_count;
}

std::string
descriptor::read() {
  std::string result;
  std::array<char, 4096> buffer;

  for (;;) {
    const ssize_t rc = ::read(_fd, buffer.data(), buffer.size());
    switch (rc) {
      case -1:
        switch (errno) {
          case EINTR: /* Interrupted system call */
            continue; /* try again */
          case EBADF: /* Bad file descriptor */
            throw posix::bad_descriptor();
          default:
            assert(errno != EFAULT);
            throw posix::error(errno);
        }

      case 0:
        return result; /* all done, EOF reached */

      default:
        assert(rc > 0);
        result.append(buffer.data(), static_cast<std::size_t>(rc));
    }
  }
}

void
descriptor::sync() {
  if (fsync(_fd) == -1) {
    switch (errno) {
      case EBADF: /* Bad file descriptor */
        throw posix::bad_descriptor();
      default:
        throw posix::error(errno);
    }
  }
}

void
descriptor::close() noexcept {
  if (valid()) {
    if (::close(_fd) == -1) {
      /* Ignore any errors from close(). */
    }
    _fd = -1;
  }
}