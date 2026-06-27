#ifndef SCP_MEMORY_GUARD_H_
#define SCP_MEMORY_GUARD_H_

#include <cstdlib>
#include <cstring>
#include <utility>
#include <new>

namespace scp {

// RAII wrapper for C-style malloc/free buffers.
template <typename T>
class ScopedBuffer {
 public:
  ScopedBuffer() : data_(nullptr), size_(0) {}
  explicit ScopedBuffer(size_t count) : data_(nullptr), size_(0) {
    Alloc(count);
  }
  ~ScopedBuffer() { Free(); }

  ScopedBuffer(const ScopedBuffer&) = delete;
  ScopedBuffer& operator=(const ScopedBuffer&) = delete;

  ScopedBuffer(ScopedBuffer&& other) noexcept
      : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
  }

  ScopedBuffer& operator=(ScopedBuffer&& other) noexcept {
    if (this != &other) {
      Free();
      data_ = other.data_;
      size_ = other.size_;
      other.data_ = nullptr;
      other.size_ = 0;
    }
    return *this;
  }

  void Alloc(size_t count) {
    Free();
    if (count > 0) {
      data_ = static_cast<T*>(std::calloc(count, sizeof(T)));
      if (!data_) throw std::bad_alloc();
      size_ = count;
    }
  }

  void Free() {
    std::free(data_);
    data_ = nullptr;
    size_ = 0;
  }

  T* get() { return data_; }
  const T* get() const { return data_; }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  explicit operator bool() const { return data_ != nullptr; }

  T& operator[](size_t i) { return data_[i]; }
  const T& operator[](size_t i) const { return data_[i]; }

 private:
  T* data_;
  size_t size_;
};

// RAII wrapper for libssh2-managed resources using a deleter functor.
template <typename T, typename Deleter>
class UniqueResource {
 public:
  UniqueResource() : obj_(nullptr), deleter_() {}
  explicit UniqueResource(T obj, Deleter d = Deleter())
      : obj_(obj), deleter_(std::move(d)) {}
  ~UniqueResource() { Reset(); }

  UniqueResource(const UniqueResource&) = delete;
  UniqueResource& operator=(const UniqueResource&) = delete;

  UniqueResource(UniqueResource&& other) noexcept
      : obj_(other.obj_), deleter_(std::move(other.deleter_)) {
    other.obj_ = nullptr;
  }

  UniqueResource& operator=(UniqueResource&& other) noexcept {
    if (this != &other) {
      Reset();
      obj_ = other.obj_;
      deleter_ = std::move(other.deleter_);
      other.obj_ = nullptr;
    }
    return *this;
  }

  void Reset(T new_obj = nullptr) {
    if (obj_) deleter_(obj_);
    obj_ = new_obj;
  }

  T Release() {
    T tmp = obj_;
    obj_ = nullptr;
    return tmp;
  }

  T get() const { return obj_; }
  explicit operator bool() const { return obj_ != nullptr; }

 private:
  T obj_;
  Deleter deleter_;
};

// Helper to check and return C-style error codes from scp_error_t returns.
inline scp_error_t ToScpError(int libssh2_error) {
  if (libssh2_error == 0) return SCP_OK;
  // Map common libssh2 errors to our error codes.
  // Exact mapping depends on context; callers may override.
  if (libssh2_error < 0) return SCP_ERROR_GENERIC;
  return SCP_OK;
}

}  // namespace scp

#endif  // SCP_MEMORY_GUARD_H_
