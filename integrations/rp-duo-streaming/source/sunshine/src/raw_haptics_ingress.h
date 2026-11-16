/**
 * @file raw_haptics_ingress.h
 * @brief Owner-only local datagram ingress for raw game haptic snapshots.
 */
#pragma once
#include "raw_haptics.h"

#include <array>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace rpduo {
  /** @brief One nonblocking ingress, protected from other users and duplicate servers. */
  class haptics_ingress {
    int fd_ = -1;  ///< Bound datagram socket.
    int lock_ = -1;  ///< Exclusive lock retained until destruction.
    std::string path_;  ///< Bound path, empty when initialization failed.

  public:
    /** @brief Find/create a private per-user directory; reject symlinks or broad permissions. */
    static std::string default_path(bool direct = false) {
      std::string dir = "/private/tmp/rpduo-stream-" + std::to_string(getuid());
      mkdir(dir.c_str(), 0700);
      struct stat st {};
      if (lstat(dir.c_str(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 0777) != 0700) {
        return {};
      }
      return dir + (direct ? "/direct-haptics.sock" : "/haptics.sock");
    }

    /** @brief Bind a user-owned socket; an existing live server is never replaced. */
    explicit haptics_ingress(const std::string &path) {
      if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) {
        return;
      }
      std::string lock_path = path + ".lock";
      lock_ = open(lock_path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
      struct stat st {};
      if (lock_ < 0 || fstat(lock_, &st) || st.st_uid != getuid() || !S_ISREG(st.st_mode) || flock(lock_, LOCK_EX | LOCK_NB)) {
        return;
      }
      if (!lstat(path.c_str(), &st)) {
        if (st.st_uid != getuid() || !S_ISSOCK(st.st_mode)) {
          return;
        }
        if (unlink(path.c_str())) {
          return;
        }
      }
      fd_ = socket(AF_UNIX, SOCK_DGRAM, 0);
      if (fd_ < 0) {
        return;
      }
      fcntl(fd_, F_SETFD, FD_CLOEXEC);
      fcntl(fd_, F_SETFL, O_NONBLOCK);
      sockaddr_un addr {};
      addr.sun_family = AF_UNIX;
      memcpy(addr.sun_path, path.c_str(), path.size() + 1);
      if (bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))) {
        close(fd_);
        fd_ = -1;
        return;
      }
      path_ = path;
      if (chmod(path_.c_str(), 0600)) {
        close(fd_);
        fd_ = -1;
        unlink(path_.c_str());
        path_.clear();
      }
    }

    /** @brief Close the listener and remove only the socket this object bound. */
    ~haptics_ingress() {
      if (fd_ >= 0) {
        close(fd_);
      }
      if (!path_.empty()) {
        unlink(path_.c_str());
      }
      if (lock_ >= 0) {
        close(lock_);
      }
    }

    haptics_ingress(const haptics_ingress &) = delete;
    haptics_ingress &operator=(const haptics_ingress &) = delete;

    /** @brief Report whether this instance owns a listening socket. */
    bool ready() const {
      return fd_ >= 0;
    }

    /** @brief Drain a bounded burst; preserve distinct transitions and reject malformed frames. */
    std::vector<std::array<unsigned char, RP_HAPTICS_SIZE>> poll(bool expire_host_queue = false) {
      std::vector<std::array<unsigned char, RP_HAPTICS_SIZE>> frames;
      if (fd_ < 0) {
        return frames;
      }
      for (int i = 0; i < 64; i++) {
        std::array<unsigned char, RP_HAPTICS_SIZE + 1> bytes {};
        auto n = recv(fd_, bytes.data(), bytes.size(), 0);
        if (n < 0) {
          break;
        }
        if (rp_haptics_valid(bytes.data(), static_cast<unsigned>(n)) &&
            (!expire_host_queue || rp_haptics_apply_host_age(bytes.data(), clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000))) {
          std::array<unsigned char, RP_HAPTICS_SIZE> frame {};
          memcpy(frame.data(), bytes.data(), frame.size());
          frames.push_back(frame);
        }
      }
      return frames;
    }
  };
}  // namespace rpduo
