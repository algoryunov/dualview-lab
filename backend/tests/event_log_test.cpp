#include "dualview/event_log.hpp"

#include <cassert>
#include <chrono>

int main() {
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("dualview-log-test-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  const auto path = directory / "events.jsonl";
  dualview::EventLog logger(path.string(), 160);
  for (int i = 0; i < 20; ++i)
    logger.write({{"event", "tracking_sample"}, {"sequence", i}, {"reason", "stale_frames"}});
  assert(std::filesystem::exists(path.string() + ".3"));
  int count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    ++count;
    assert(entry.file_size() <= 160);
    std::ifstream input(entry.path());
    std::string line;
    while (std::getline(input, line)) {
      const auto value = nlohmann::json::parse(line);
      assert(value["event"] == "tracking_sample");
    }
  }
  assert(count == 4);
  // A broken log destination must not crash inference.
  dualview::EventLog blocked((path / "not-a-directory").string());
  assert(!blocked.write({{"event", "test"}}));
  std::filesystem::remove_all(directory);
}
