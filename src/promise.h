#include <memory>
#include <utility>
#include <optional>
#include <type_traits>

namespace prms {
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
class IFuture {
 public:
  virtual ~IFuture() = default;

  virtual void Invoke(Output output) = 0;
};

template <typename Output>
class PromiseState {
 public:
  PromiseState() = default;
  PromiseState(PromiseState const&) = delete;
  PromiseState(PromiseState&&) noexcept = delete;

  void SetValue(Output output) {
    if (future_ != nullptr) {
      future_->Invoke(std::move(output));
      future_ = nullptr;
      return;
    }
    output_ = std::move(output);
  }

  void SetFuture(IFuture<Output>& future) {
    if (output_) {
      future.Invoke(std::move(output_.value()));
      output_.reset();
      return;
    }
    future_ = &future;
  }

 private:
  IFuture<Output>* future_;
  std::optional<Output> output_;
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
  constexpr Future(Future<Output, OutputHandler>&& h, Child&& child)
      : IFuture<Output>{std::move(h)},
        head_{std::move(h)},
        child_{std::move(child)} {
    if (head_.state_) {
      head_.state_->SetFuture(*this);
    }
  }

  constexpr Future(Future&& other) noexcept
      : IFuture<Output>{std::move(other)},
        head_{std::move(other.head_)},
        child_{std::move(other.child_)} {
    if (head_.state_) {
      head_.state_->SetFuture(*this);
    }
  }

  void Invoke(Output output) override {
    decltype(auto) promise = head_.handler_(std::forward<Output>(output));
    promise.state_->SetFuture(child_);
  }

  template <typename Func>
  [[nodiscard]] constexpr auto Then(Func&& func) && {
    using NewChild = decltype(std::move(child_).Then(std::forward<Func>(func)));
    return Future<Output, OutputHandler, NewChild>{
        std::move(head_), std::move(child_).Then(std::forward<Func>(func))};
  }

 private:
  Future<Output, OutputHandler> head_;
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
  using PromiseType = std::invoke_result_t<OutputHandler, Output>;
  using PromiseOut = typename _internal::PromiseOutput<PromiseType>::type;

  constexpr Future(std::shared_ptr<PromiseState<Output>> s, OutputHandler h)
      : state_{s}, handler_{std::move(h)} {
    if (state_) {
      state_->SetFuture(*this);
    }
  }

  constexpr explicit Future(OutputHandler&& h) : handler_{std::move(h)} {}

  constexpr Future(Future&& other) noexcept
      : state_{other.state_},
        handler_{std::move(other.handler_)},
        child_state_{other.child_state_} {
    if (state_) {
      state_->SetFuture(*this);
    }
  }

  void Invoke(Output output) override {
    decltype(auto) p = handler_(std::forward<Output>(output));
    child_state_ = p.state_;
  }

  template <typename Func>
  [[nodiscard]] constexpr auto Then(Func&& func) && {
    using NewChild = Future<PromiseOut, Func>;
    if (child_state_) {
      return Future<Output, OutputHandler, NewChild>{
          std::move(*this), NewChild{child_state_, std::forward<Func>(func)}};
    }
    return Future<Output, OutputHandler, NewChild>{
        std::move(*this), NewChild{std::forward<Func>(func)}};
  }

 private:
  std::shared_ptr<PromiseState<Output>> state_{};
  OutputHandler handler_;
  std::shared_ptr<PromiseState<PromiseOut>> child_state_{};
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
  constexpr Future(std::shared_ptr<PromiseState<Output>> s, OutputHandler&& h)
      : state_{s}, handler_{std::move(h)} {
    state_->SetFuture(*this);
  }

  constexpr explicit Future(OutputHandler&& h) : handler_{std::move(h)} {}

  constexpr Future(Future&& other) noexcept
      : state_{other.state_}, handler_{std::move(other.handler_)} {
    if (state_) {
      state_->SetFuture(*this);
    }
  }

  void Invoke(Output output) override { handler_(std::move(output)); }

 private:
  std::shared_ptr<PromiseState<Output>> state_{};
  OutputHandler handler_;
};

template <typename Output>
class Promise {
 public:
  Promise() : state_{std::make_shared<PromiseState<Output>>()} {}
  explicit Promise(Output value)
      : state_{std::make_shared<PromiseState<Output>>()} {
    state_->SetValue(std::move(value));
  }

  template <typename Func>
  [[nodiscard]] auto Then(Func&& func) {
    return Future<Output, std::decay_t<Func>>{state_, std::forward<Func>(func)};
  }

  void Resolve(Output output) { state_->SetValue(std::move(output)); }

  std::shared_ptr<PromiseState<Output>> state_;
};
}  // namespace prms
