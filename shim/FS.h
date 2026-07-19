// Host-backed FS/File emulation (Teensy FS.h API subset).
// Files live in a real directory on the host; each FS instance is rooted at
// a subdirectory of the emulator state dir (see shim_drivers.cpp).
#pragma once

#include <Arduino.h>
#include <Stream.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#define FILE_READ 0
#define FILE_WRITE 1
#define FILE_WRITE_BEGIN 2

class File;

// Shared state so File copies behave like Teensy's refcounted File.
struct EmuFileImpl {
  FILE* fp = nullptr;
  std::string path;       // host path
  std::string name;       // basename
  bool is_dir = false;
  std::vector<std::string> entries;  // for directories
  size_t dir_pos = 0;
  ~EmuFileImpl() {
    if (fp) fclose(fp);
  }
};

class File : public Stream {
 public:
  File() {}
  explicit File(std::shared_ptr<EmuFileImpl> impl) : impl_(impl) {}

  operator bool() const { return impl_ != nullptr; }

  const char* name() const { return impl_ ? impl_->name.c_str() : ""; }
  bool isDirectory() const { return impl_ && impl_->is_dir; }

  uint64_t size() const {
    if (!impl_ || !impl_->fp) return 0;
    long cur = ftell(impl_->fp);
    fseek(impl_->fp, 0, SEEK_END);
    long sz = ftell(impl_->fp);
    fseek(impl_->fp, cur, SEEK_SET);
    return (uint64_t)sz;
  }

  bool seek(uint64_t pos) {
    return impl_ && impl_->fp && fseek(impl_->fp, (long)pos, SEEK_SET) == 0;
  }
  uint64_t position() const {
    return (impl_ && impl_->fp) ? (uint64_t)ftell(impl_->fp) : 0;
  }

  int available() override {
    if (!impl_ || !impl_->fp) return 0;
    long cur = ftell(impl_->fp);
    fseek(impl_->fp, 0, SEEK_END);
    long end = ftell(impl_->fp);
    fseek(impl_->fp, cur, SEEK_SET);
    long left = end - cur;
    return left > 0x7fffffffL ? 0x7fffffff : (int)left;
  }

  int read() override {
    if (!impl_ || !impl_->fp) return -1;
    return fgetc(impl_->fp);
  }

  int peek() override {
    if (!impl_ || !impl_->fp) return -1;
    int c = fgetc(impl_->fp);
    if (c >= 0) ungetc(c, impl_->fp);
    return c;
  }

  size_t read(void* buf, size_t nbyte) {
    if (!impl_ || !impl_->fp) return 0;
    return fread(buf, 1, nbyte, impl_->fp);
  }

  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t sz) override {
    if (!impl_ || !impl_->fp) return 0;
    return fwrite(buf, 1, sz, impl_->fp);
  }
  using Print::write;

  void flush() override {
    if (impl_ && impl_->fp) fflush(impl_->fp);
  }

  void close() {
    if (impl_ && impl_->fp) {
      fclose(impl_->fp);
      impl_->fp = nullptr;
    }
    impl_.reset();
  }

  File openNextFile(uint8_t mode = FILE_READ);

 private:
  std::shared_ptr<EmuFileImpl> impl_;
};

class FS {
 public:
  FS() {}
  explicit FS(const char* label) : label_(label) {}
  virtual ~FS() {}

  // Root host directory for this filesystem (set up lazily under the emu
  // state dir).
  const std::string& rootdir();

  File open(const char* filepath, uint8_t mode = FILE_READ);
  bool exists(const char* filepath);
  bool remove(const char* filepath);
  bool mkdir(const char* filepath);
  bool rmdir(const char* filepath);
  bool rename(const char* oldpath, const char* newpath);

  uint64_t totalSize() { return 1024 * 1024; }
  uint64_t usedSize() { return 0; }
  bool mediaPresent() { return true; }
  bool format(int = 0, char = '.', Print& = Serial) { return true; }

 protected:
  std::string label_ = "fs";
  std::string root_;

  std::string hostpath(const char* filepath);
};
