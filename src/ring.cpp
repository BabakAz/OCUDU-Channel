#include "ocudu_gpu_channel/ring.h"

namespace ocg {

IqRing::IqRing(std::size_t capacity_samples)
{
  reset(capacity_samples);
}

void IqRing::reset(std::size_t capacity_samples)
{
  buffer_.assign(capacity_samples, {});
  next_sequence_ = 0;
  start_ = 0;
  size_ = 0;
}

bool IqRing::push(std::span<const IqSample> samples)
{
  if (samples.empty()) {
    return true;
  }
  if (samples.size() > buffer_.size() || size_ + samples.size() > buffer_.size()) {
    return false;
  }

  for (const auto& sample : samples) {
    const std::size_t index = (start_ + size_) % buffer_.size();
    buffer_[index] = sample;
    ++size_;
    ++next_sequence_;
  }
  return true;
}

bool IqRing::read(std::uint64_t sequence, std::span<IqSample> out) const
{
  if (out.empty()) {
    return true;
  }
  if (sequence < earliest_sequence() || sequence + out.size() > next_sequence_ || buffer_.empty()) {
    return false;
  }

  const auto offset = static_cast<std::size_t>(sequence - earliest_sequence());
  for (std::size_t i = 0; i != out.size(); ++i) {
    out[i] = buffer_[(start_ + offset + i) % buffer_.size()];
  }
  return true;
}

void IqRing::discard_before(std::uint64_t sequence)
{
  const std::uint64_t earliest = earliest_sequence();
  if (sequence <= earliest || size_ == 0) {
    return;
  }

  if (sequence >= next_sequence_) {
    start_ = 0;
    size_ = 0;
    return;
  }

  const auto discard_count = static_cast<std::size_t>(sequence - earliest);
  start_ = (start_ + discard_count) % buffer_.size();
  size_ -= discard_count;
}

} // namespace ocg
