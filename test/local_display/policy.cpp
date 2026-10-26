#include <services/local_live_policy.h>
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <fstream>
#include <iterator>

int main(int argc, char **argv) {
  local_live::PowerCycles power;
  for (int n = 0; n < 20; ++n) assert(power.begin(true));
  assert(power.begin(false)); // final battery poll after unplugging
  assert(!power.begin(false)); // no busy loop while on battery
  assert(!power.begin(false));
  assert(power.begin(true)); // plugging in while awake resumes live mode
  assert(power.begin(false));
  assert(!power.begin(false));
  local_live::PowerCycles batteryBoot;
  assert(batteryBoot.begin(false)); // periodic wake fetches once
  assert(!batteryBoot.begin(false));
  assert(local_live::BATTERY_REFRESH_SECONDS == 300);
  local_live::RefreshPolicy policy;
  assert(policy.full(100)); // first frame must clear, regardless of RTC state
  policy.displayed(100, true);
  for (int i = 0; i < 3; ++i) {
    assert(!policy.full(200 + i));
    policy.displayed(200 + i, false);
  }
  assert(policy.full(210));
  policy.displayed(210, true);
  assert(!policy.full(211));
  assert(policy.full(211, true)); // manual full refresh
  assert(policy.full(210 + 10UL * 60 * 1000));
  policy.failed();
  assert(policy.full(212)); // render errors invalidate the previous-image assumption
  policy.displayed(0xfffffff0, true);
  assert(!policy.full(32)); // millis() wraparound
  assert(local_live::retryDelay(1) == 4000);
  assert(local_live::retryDelay(5) == 60000);
  assert(local_live::retryDelay(200) == 60000);
  assert(local_live::validId("0123456789abcdef0123456789abcdef"));
  assert(!local_live::validId("../../credentials"));
  assert(!local_live::validId("0123456789abcdef0123456789abcdeG"));
  assert(!local_live::validPng(nullptr, 0, 1872, 1404));
  assert(local_live::validPartialPasses(6));
  assert(local_live::validPartialPasses(8));
  assert(local_live::validPartialPasses(10));
  assert(!local_live::validPartialPasses(0));
  assert(!local_live::validPartialPasses(3));
  assert(!local_live::validPartialPasses(11));
  uint8_t previous[12] = {}, current[12] = {};
  auto rows = local_live::changedRows(current, previous, 3, 4);
  assert(rows.last == -1);
  current[4] = 1; current[8] = 2;
  rows = local_live::changedRows(current, previous, 3, 4);
  assert(rows.first == 1 && rows.last == 2);
  assert(local_live::PARTIAL_DRIVE_PASSES == 8);
  assert(local_live::validEndpoint("http://display.example:8080/display/next"));
  assert(local_live::validEndpoint("https://display.example/next"));
  assert(local_live::validEndpoint("http://[::1]:8080/next"));
  for (auto bad : {"", "ftp://display.example/next", "http://display.example", "http:///next",
                   "http://user:secret@display.example/next", "http://display.example/next?token=secret",
                   "http://display.example/next#fragment", "http://display.example/has space",
                   "http://display.example/next\r\nX-Test:yes", "http://display.example\\evil/next"})
    assert(!local_live::validEndpoint(bad));
  assert(local_live::validPath("/images/frame.png?version=2"));
  assert(local_live::validPath("/ack?id=0123456789abcdef0123456789abcdef"));
  for (auto bad : {"", "relative", "//evil.example/image", "https://evil.example/image", "/x#fragment", "/x\\evil", "/x\nHeader:yes"})
    assert(!local_live::validPath(bad));
  assert(local_live::validToken(""));
  assert(local_live::validToken("a-server-issued-token"));
  assert(!local_live::validToken("token with spaces"));
  assert(!local_live::validToken("token\r\nX-Header:yes"));
  assert(!local_live::validToken(std::string(257, 'x').c_str()));
  assert(!local_live::validPath(("/" + std::string(512, 'x')).c_str()));
  assert(argc == 2);
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> png((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  assert(local_live::validPng(png.data(), png.size(), 1872, 1404));
  assert(!local_live::validPng(png.data(), png.size(), 800, 480));
  assert(!local_live::validPng(png.data(), 32, 1872, 1404));
  png[24] = 8;
  assert(!local_live::validPng(png.data(), png.size(), 1872, 1404));
  png[24] = 1; png[25] = 6;
  assert(!local_live::validPng(png.data(), png.size(), 1872, 1404));
  puts("Local display policy and PNG checks passed.");
}
