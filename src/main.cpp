#include <string>
#include <iostream>

#include "promise.h"

struct Empty {};

auto foo(prms::Promise<int>& p) {
  return p
      .Then([](int v) {
        std::cout << "v=" << v;
        return prms::Promise<float>(42.12);
      })
      .Then([](float v) {
        std::cout << " v=" << v;
        return prms::Promise<std::string>("Hello");
      })
      .Then([](std::string const& i) {
        std::cout << " i=" << i;
        return prms::Promise<Empty>(Empty{});
      })
      .Then([](Empty) { std::cout << " Done"; });
}

int main() {
  auto p = prms::Promise<int>{};
  auto f = foo(p);
  p.Resolve(12);
  return 0;
}
