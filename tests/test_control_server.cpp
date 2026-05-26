// C3a test: exercise ControlServer::handle_message() directly.
//
// The ZMQ REP socket loop is not exercised here — that lands in the C3b
// integration test. This test covers the JSON parse, whitelist validation,
// range checks, and the shadow→seqno write-back path with a synthetic
// link_map of BrokerLinkControl objects.

#include "ocudu_gpu_channel/control_server.h"
#include "ocudu_gpu_channel/runtime_control.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

void require(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

bool contains(const std::string& haystack, const std::string& needle)
{
  return haystack.find(needle) != std::string::npos;
}

bool nearly(float a, float b, float tol = 1e-6F)
{
  return std::fabs(a - b) <= tol;
}

}  // namespace

int main()
{
  // Allocate two link controls — heap-allocated so the link_map can hold
  // stable pointers across re-binds.
  auto ctl_a = std::make_unique<ocg::BrokerLinkControl>();
  auto ctl_b = std::make_unique<ocg::BrokerLinkControl>();

  ocg::ControlServer::LinkMap link_map = {
      {"ue0-gnb0", ctl_a.get()},
      {"ue1-gnb0", ctl_b.get()},
  };

  ocg::ControlServerConfig cfg;
  cfg.endpoint = "inproc://test-c3a-not-bound";
  cfg.recv_timeout_ms = 100;
  ocg::ControlServer server(std::move(cfg), std::move(link_map));

  // ── Case 1: well-formed REQ updates shadow + bumps seqno + returns seqno
  {
    const std::string reply = server.handle_message(
        R"({"link_id":"ue0-gnb0","param":"path_loss_db","value":-12.5})");
    require(contains(reply, "\"ok\":true"), "expected ok:true for valid request");
    require(contains(reply, "\"seqno\":1"), "expected seqno to be 1 after first apply");
    require(nearly(ctl_a->shadow.path_loss_db, -12.5F), "shadow.path_loss_db should be -12.5");
    require(ctl_a->seqno.load() == 1, "seqno on ctl_a should be 1");
    require(ctl_b->seqno.load() == 0, "ctl_b untouched");
  }

  // ── Case 2: subsequent valid REQ on same link advances seqno
  {
    const std::string reply = server.handle_message(
        R"({"link_id":"ue0-gnb0","param":"cfo_hz","value":250.0})");
    require(contains(reply, "\"ok\":true"), "expected ok:true for second valid request");
    require(contains(reply, "\"seqno\":2"), "expected seqno to be 2");
    require(nearly(ctl_a->shadow.cfo_hz, 250.0F), "shadow.cfo_hz should be 250");
    require(ctl_a->seqno.load() == 2, "seqno on ctl_a should be 2");
  }

  // ── Case 3: separate link, independent seqno counter
  {
    const std::string reply = server.handle_message(
        R"({"link_id":"ue1-gnb0","param":"los_k_db","value":9.0})");
    require(contains(reply, "\"ok\":true"), "expected ok:true");
    require(contains(reply, "\"seqno\":1"), "ctl_b's seqno tracks independently");
    require(nearly(ctl_b->shadow.los_k_db, 9.0F), "ctl_b shadow updated");
  }

  // ── Case 4: integer param with non-integer value → rejected
  {
    const std::string reply = server.handle_message(
        R"({"link_id":"ue0-gnb0","param":"tap0_delay_samples","value":3.5})");
    require(contains(reply, "\"ok\":false"), "non-integer for int param should fail");
    require(contains(reply, "requires an integer"), "error should mention integer requirement");
  }

  // ── Case 5: integer param with integer value → accepted
  {
    const std::string reply = server.handle_message(
        R"({"link_id":"ue0-gnb0","param":"tap0_delay_samples","value":7})");
    require(contains(reply, "\"ok\":true"), "integer value for int param should succeed");
    require(ctl_a->shadow.tap0_delay_samples == 7, "shadow.tap0_delay_samples should be 7");
  }

  // ── Case 6: out-of-range → rejected with informative message
  {
    const std::string reply = server.handle_message(
        R"({"link_id":"ue0-gnb0","param":"path_loss_db","value":9999.0})");
    require(contains(reply, "\"ok\":false"), "out-of-range should fail");
    require(contains(reply, "out of range"), "error should say out of range");
    require(contains(reply, "[-200"), "error should include the valid range");
  }

  // ── Case 7: unknown param → rejected
  {
    const std::string reply = server.handle_message(
        R"({"link_id":"ue0-gnb0","param":"reverse_polarity","value":1.0})");
    require(contains(reply, "\"ok\":false"), "unknown param should fail");
    require(contains(reply, "unknown param"), "error should say unknown param");
  }

  // ── Case 8: unknown link_id → rejected
  {
    const std::string reply = server.handle_message(
        R"({"link_id":"nope","param":"path_loss_db","value":-5.0})");
    require(contains(reply, "\"ok\":false"), "unknown link should fail");
    require(contains(reply, "unknown link_id"), "error should say unknown link_id");
  }

  // ── Case 9: missing field → rejected
  {
    const std::string reply = server.handle_message(
        R"({"link_id":"ue0-gnb0","param":"path_loss_db"})");
    require(contains(reply, "\"ok\":false"), "missing field should fail");
    require(contains(reply, "missing required field"), "error should mention missing field");
  }

  // ── Case 10: malformed JSON → rejected
  {
    const std::string reply = server.handle_message("not json at all");
    require(contains(reply, "\"ok\":false"), "garbage input should fail");
    require(contains(reply, "malformed JSON"), "error should mention JSON parse");
  }

  // ── Case 11: type mismatch (value as string) → rejected
  {
    const std::string reply = server.handle_message(
        R"({"link_id":"ue0-gnb0","param":"path_loss_db","value":"oops"})");
    require(contains(reply, "\"ok\":false"), "string value should be rejected");
    require(contains(reply, "type error"), "error should mention type");
  }

  // ── Case 12: counters reflect all attempted REQs
  {
    const auto s = server.stats();
    require(s.msgs_received == 11, "msgs_received should equal total handled messages");
    require(s.updates_applied == 4, "4 successful updates: cases 1, 2, 3, 5");
    require(s.updates_rejected == 7, "7 rejections: cases 4, 6, 7, 8, 9, 10, 11");
  }

  std::cout << "test_control_server OK\n";
  return 0;
}
