#include "ocudu_gpu_channel/ring.h"
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

} // namespace

int main()
{
  ocg::IqRing ring(4);
  ocg::IqBuffer first = {{1.0F, 0.0F}, {2.0F, 0.0F}};
  ocg::IqBuffer second = {{3.0F, 0.0F}, {4.0F, 0.0F}};
  ocg::IqBuffer overflow = {{5.0F, 0.0F}};
  ocg::IqBuffer out(2);

  require(ring.push(first), "first push");
  require(ring.push(second), "second push");
  require(!ring.push(overflow), "bounded ring rejects overflow");
  require(ring.read(0, out), "first cursor read");
  require(out[0].i == 1.0F && out[1].i == 2.0F, "first cursor data");
  require(ring.read(2, out), "second cursor read");
  require(out[0].i == 3.0F && out[1].i == 4.0F, "second cursor data");
  require(!ring.read(3, out), "partial future read rejected");
  ring.discard_before(2);
  require(ring.size() == 2, "discard releases consumed samples");
  require(ring.push(overflow), "push after discard");
  require(ring.next_sequence() == 5, "sequence remains monotonic");
  return 0;
}
