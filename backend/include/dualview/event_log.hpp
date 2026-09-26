#pragma once
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace dualview {
// A single processing worker owns this logger. Rotation bounds local disk usage.
class EventLog {
 public:
  explicit EventLog(std::string path, std::uintmax_t limit = 2000000)
      : path_(std::move(path)), limit_(limit) {}
  bool write(const nlohmann::json& event) noexcept {
    try {
      const auto line = event.dump() + "\n";
      if (path_ == "-") {
        std::clog << line;
        return bool(std::clog);
      }
      const std::filesystem::path path(path_);
      if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
      if (std::filesystem::exists(path) &&
          std::filesystem::file_size(path) + line.size() > limit_) {
        std::filesystem::remove(path_ + ".3");
        for (int index = 2; index >= 0; --index) {
          const auto source = index == 0 ? path_ : path_ + "." + std::to_string(index);
          if (std::filesystem::exists(source))
            std::filesystem::rename(source, path_ + "." + std::to_string(index + 1));
        }
      }
      std::ofstream output(path, std::ios::app);
      output << line;
      output.flush();
      if (!output) throw std::runtime_error("Could not write diagnostic log");
      return true;
    } catch (const std::exception& error) {
      if (!warned_) {
        std::cerr << "Diagnostic log unavailable: " << error.what() << '\n';
        warned_ = true;
      }
      return false;
    }
  }

 private:
  std::string path_;
  std::uintmax_t limit_;
  bool warned_ = false;
};
}  // namespace dualview
