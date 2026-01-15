#include <utility>
#include <optional>
#include <type_traits>

namespace prms {
template <typename T>
class IntrusiveRef;

template <typename T>
class IntrusiveRefObserver {
  friend class IntrusiveRef<T>;

 public:
  IntrusiveRefObserver() : ref_{nullptr} {}
  explicit IntrusiveRefObserver(IntrusiveRef<T>& ref) : ref_{&ref} {
    ref_->observer_ = this;
  }
  ~IntrusiveRefObserver() {
    if (ref_ != nullptr) {
      ref_->observer_ = nullptr;
    }
  }

  IntrusiveRefObserver(IntrusiveRefObserver&& other) noexcept
      : ref_{std::exchange(other.ref_, nullptr)} {
    if (ref_ != nullptr) {
      ref_->observer_ = this;
    }
  }

  IntrusiveRefObserver& operator=(IntrusiveRefObserver&& other) noexcept {
    if (this != &other) {
      ref_ = std::exchange(other.ref_, nullptr);
      if (ref_ != nullptr) {
        ref_->observer_ = this;
      }
    }
    return *this;
  }

  IntrusiveRefObserver(IntrusiveRefObserver const&) = delete;
  IntrusiveRefObserver& operator=(IntrusiveRefObserver const&) = delete;

  explicit operator bool() const { return ref_ != nullptr; }
  T* get() { return (ref_ != nullptr) ? ref_->get() : nullptr; }
  T const* get() const { return (ref_ != nullptr) ? ref_->get() : nullptr; }
  T* operator->() { return get(); }
  T const* operator->() const { return get(); }

 private:
  IntrusiveRef<T>* ref_;
};

template <typename T>
class IntrusiveRef {
  friend class IntrusiveRefObserver<T>;

 public:
  ~IntrusiveRef() {
    if (observer_ != nullptr) {
      observer_->ref_ = nullptr;
    }
  }

  IntrusiveRef() : observer_{nullptr} {}

  IntrusiveRef(IntrusiveRef&& other) noexcept
      : observer_{std::exchange(other.observer_, nullptr)} {
    observer_->ref_ = this;
  }

  IntrusiveRef& operator=(IntrusiveRef&& other) noexcept {
    if (this != other) {
      observer_ = std::exchange(other.observer_, nullptr);
      observer_->ref = this;
    }
    return *this;
  }

  IntrusiveRef(IntrusiveRef const&) = delete;
  IntrusiveRef& operator=(IntrusiveRef const&) = delete;

  T* get() { return static_cast<T*>(this); }
  T const* get() const { return static_cast<T const*>(this); }

 private:
  IntrusiveRefObserver<T>* observer_;
};

template <typename Output>
class Promise;

namespace _internal {
template <typename T>
struct IsPromise : std::false_type {};

template <typename T>
struct IsPromise<Promise<T>> : std::true_type {};

template <typename T>
struct PromiseOutput;

template <typename T>
struct PromiseOutput<Promise<T>> {
  using type = T;
};

template <typename F, typename T>
struct PromiseInvokeRes {
  using type = std::invoke_result_t<F, T>;
};

template <typename F>
struct PromiseInvokeRes<F, void> {
  using type = std::invoke_result_t<F>;
};

template <typename F, typename T>
using PromiseInvokeResT = typename PromiseInvokeRes<F, T>::type;

}  // namespace _internal

template <typename Output>
class IFuture : public IntrusiveRef<IFuture<Output>> {
 public:
  IFuture() = default;
  IFuture(IFuture&& other) : IntrusiveRef<IFuture<Output>>{std::move(other)} {}

  virtual ~IFuture() = default;

  virtual void Invoke(Output output) = 0;
};

template <typename Output, typename OutputHandler, typename Child = void,
          typename Enabled = void>
class Future;

///////
template <typename Output, typename OutputHandler, typename Child>
class Future<Output, OutputHandler, Child,
             std::enable_if_t<_internal::IsPromise<std::decay_t<
                 _internal::PromiseInvokeResT<OutputHandler, Output>>>::value>>
    final : public IFuture<Output> {
  template <typename T1, typename T2, typename T3, typename T4>
  friend class Future;

 public:
  using PromiseType = std::decay_t<std::invoke_result_t<OutputHandler, Output>>;
  using PromiseOut = _internal::PromiseOutput<PromiseType>;

  constexpr Future(Future<Output, OutputHandler>&& h, Child&& child)
      : IFuture<Output>{std::move(h)},
        promise_{h.promise_},
        handler_{std::move(h.handler_)},
        child_{std::move(child)} {}

  constexpr Future(Future&& other) noexcept
      : IFuture<Output>{std::move(other)},
        promise_{other.promise_},
        handler_{std::move(other.handler_)},
        child_{std::move(other.child_)} {}

  void Invoke(Output output) override {
    decltype(auto) promise = handler_(output);
    promise.SetFuture(&child_);
  }

  template <typename Func>
  [[nodiscard]] constexpr auto Then(Func&& func) && {
    using NewChild = decltype(std::move(child_).Then(std::forward<Func>(func)));
    return Future<Output, OutputHandler, NewChild>{
        Future<Output, OutputHandler>{promise_, std::move(handler_)},
        std::move(child_).Then(std::forward<Func>(func))};
  }

 private:
  Promise<Output>* promise_;
  OutputHandler handler_;
  Child child_;
};

//////////////
template <typename Output, typename OutputHandler>
class Future<Output, OutputHandler, void,
             std::enable_if_t<_internal::IsPromise<std::decay_t<
                 _internal::PromiseInvokeResT<OutputHandler, Output>>>::value>>
    final : public IFuture<Output> {
  template <typename T1, typename T2, typename T3, typename T4>
  friend class Future;

 public:
  using PromiseType = std::decay_t<std::invoke_result_t<OutputHandler, Output>>;
  using PromiseOut = typename _internal::PromiseOutput<PromiseType>::type;

  constexpr Future(Promise<Output>* p, OutputHandler&& h)
      : promise_{p}, handler_{std::move(h)} {
    if (promise_) {
      promise_->SetFuture(this);
    }
  }
  constexpr Future(Future&& other) noexcept
      : IFuture<Output>{std::move(other)},
        promise_{other.promise_},
        handler_{std::move(other.handler_)} {}

  void Invoke(Output output) override {
    decltype(auto) p = handler_(std::forward<Output>(output));
    child_promise_ = &p;
  }

  template <typename Func>
  [[nodiscard]] constexpr auto Then(Func&& func) && {
    using NewChild = Future<PromiseOut, Func>;
    return Future<Output, OutputHandler, NewChild>{
        std::move(*this), NewChild{child_promise_, std::forward<Func>(func)}};
  }

 private:
  Promise<Output>* promise_;
  PromiseType* child_promise_{nullptr};
  OutputHandler handler_;
};

//////////////
template <typename Output, typename OutputHandler>
class Future<Output, OutputHandler, void,
             std::enable_if_t<!_internal::IsPromise<std::decay_t<
                 _internal::PromiseInvokeResT<OutputHandler, Output>>>::value>>
    final : public IFuture<Output> {
  template <typename T1, typename T2, typename T3, typename T4>
  friend class Future;

 public:
  constexpr Future(Promise<Output>* p, OutputHandler&& h)
      : promise_{p}, handler_{std::move(h)} {
    if (promise_) {
      promise_->SetFuture(this);
    }
  }
  constexpr Future(Future&& other) noexcept
      : IFuture<Output>{std::move(other)},
        promise_{other.promise_},
        handler_{std::move(other.handler_)} {}

  void Invoke(Output output) override {
    handler_(std::forward<Output>(output));
  }

 private:
  Promise<Output>* promise_;
  OutputHandler handler_;
};

template <typename Output>
class Promise {
 public:
  Promise() = default;

  template <typename Func>
  [[nodiscard]] auto Then(Func&& func) {
    return Future<Output, std::decay_t<Func>>{this, std::forward<Func>(func)};
  }

  void Resolve(Output output) {
    if (future_) {
      future_->Invoke(std::forward<Output>(output));
      return;
    }
    output_ = std::forward<Output>(output);
  }

  void SetFuture(IFuture<Output>* future) {
    if (output_) {
      future->Invoke(*output_);
      return;
    }
    future_ = IntrusiveRefObserver<IFuture<Output>>{*future};
  }

 private:
  IntrusiveRefObserver<IFuture<Output>> future_;
  std::optional<Output> output_;
};
}  // namespace prms
