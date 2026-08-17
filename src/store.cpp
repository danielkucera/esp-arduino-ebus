#if defined(EBUS_INTERNAL)
#include "store.hpp"

#include <esp_littlefs.h>
#include <esp_timer.h>
#include <sys/stat.h>

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>
#include <ebus/detail/protocol_limits.hpp>

#include "logger.hpp"
#include "mqtt.hpp"

Store store;

namespace {
constexpr const char* kLittlefsBasePath = "/littlefs";
constexpr const char* kLittlefsPartitionLabel = "littlefs";
constexpr const char* kCommandsFilePath = "/littlefs/commands.json";

bool ensureLittlefsMounted() {
  static bool mounted = false;
  if (mounted) return true;

  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = kLittlefsBasePath;
  conf.partition_label = kLittlefsPartitionLabel;
  conf.partition = nullptr;
  conf.format_if_mount_failed = true;
  conf.read_only = false;
  conf.dont_mount = false;
  conf.grow_on_mount = false;

  esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
    mounted = true;
    return true;
  }

  return false;
}

}  // namespace

bool Store::initFileSystem() { return ensureLittlefsMounted(); }

void Store::setDataUpdatedCallback(DataUpdatedCallback callback) {
  data_updated_callback_ = std::move(callback);
}

void Store::setDataUpdatedLogCallback(DataUpdatedLogCallback callback) {
  data_updated_log_callback_ = std::move(callback);
}

void Store::setCommandChangedCallback(CommandChangedCallback callback) {
  command_changed_callback_ = std::move(callback);
}

void Store::setCommandRemovedCallback(CommandChangedCallback callback) {
  command_removed_callback_ = std::move(callback);
}

void Store::insertCommand(const Command& command) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (size_t i = 0; i < commands_.size(); i++) {
    if (std::string_view(commands_[i].getKey()) ==
        std::string_view(command.getKey())) {
      commands_[i] = command;
      if (command_changed_callback_) command_changed_callback_(&commands_[i]);
      return;
    }
  }
  if (commands_.push_back(command)) {
    if (command_changed_callback_) command_changed_callback_(&commands_.back());
  }
}

void Store::removeCommand(std::string_view key) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (size_t i = 0; i < commands_.size(); i++) {
    if (std::string_view(commands_[i].getKey()) == key) {
      if (command_removed_callback_) command_removed_callback_(&commands_[i]);
      commands_.erase(commands_.begin() + i);
      return;
    }
  }
}

Command* Store::findCommand(std::string_view key) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (size_t i = 0; i < commands_.size(); i++) {
    if (std::string_view(commands_[i].getKey()) == key) {
      return &commands_[i];
    }
  }
  return nullptr;
}

Command* Store::findCommand(uint32_t poll_id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (size_t i = 0; i < commands_.size(); i++) {
    Command* cmd = &commands_[i];
    if (cmd->getPollId() == poll_id) {
      return cmd;
    }
  }
  return nullptr;
}

MatchingCommands Store::findAllMatchingCommands(ebus::ByteView master) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  MatchingCommands result;
  for (size_t i = 0; i < commands_.size(); i++) {
    Command* cmd = &commands_[i];
    if (cmd->matches(master)) {
      if (!result.push_back(cmd)) break;
    }
  }
  return result;
}

int64_t Store::loadCommands() {
  if (!ensureLittlefsMounted()) return -1;
  std::remove("/littlefs/commands.json.tmp");
  return loadCommandsFrom(kCommandsFilePath);
}

int64_t Store::loadCommandsFrom(const char* path) {
  if (!ensureLittlefsMounted()) return -1;

  FILE* file = std::fopen(path, "rb");
  if (file == nullptr) {
    if (errno == ENOENT) return 0;
    char err_buf[64];
    snprintf(err_buf, sizeof(err_buf),
             "Store: Failed to open commands file %s: %d", path, errno);
    logger.error(err_buf);
    return -1;
  }

  // Use small buffer to avoid large heap allocation for FILE* stream
  char file_buf[512];
  std::setvbuf(file, file_buf, _IOFBF, sizeof(file_buf));

  char log_buf[96];
  snprintf(log_buf, sizeof(log_buf), "Store: Loading from %s", path);
  logger.info(log_buf);

  deserializeCommands(file);
  std::fclose(file);
  return static_cast<int64_t>(store.getCommandCount());
}

int64_t Store::saveCommands() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!ensureLittlefsMounted()) return -1;
  bool commands_empty = commands_.empty();
  if (commands_empty) return 0;

  FILE* file = std::fopen(kCommandsFilePath, "wb");
  if (file == nullptr) return -1;

  char file_buf[512];
  std::setvbuf(file, file_buf, _IOFBF, sizeof(file_buf));

  size_t bytes_written = 0;
  ebus::detail::JsonWriter writer([file, &bytes_written](std::string_view s) {
    bytes_written += std::fwrite(s.data(), 1, s.size(), file);
  });

  {
    auto root_array = writer.arrayScope();

    // Header row for compressed format
    {
      auto header_array = writer.arrayScope();
      static const char* header[] = {
          "key",    "name",     "read_cmd", "write_cmd", "active", "interval",
          "master", "position", "datatype", "divider",   "min",    "max",
          "digits", "unit",     "ha",       "ha_profile"};
      for (const char* h : header) writer.writeValue(h);
    }

    // Data rows in tabular format
    for (const Command& c : commands_) {
      auto row_array = writer.arrayScope();
      writer.writeValue(c.getKey());
      writer.writeValue(c.getName());
      writer.writeHexValue(c.getReadCmd());
      writer.writeHexValue(c.getWriteCmd());
      writer.writeValue(c.getActive());
      writer.writeValue(c.getInterval());
      writer.writeValue(c.getMaster());
      writer.writeValue(c.getPosition());
      writer.writeValue(ebus::dataTypeToString(c.getDatatype()));
      writer.writeValueFloat(c.getDivider());
      writer.writeValueFloat(c.getMin());
      writer.writeValueFloat(c.getMax());
      writer.writeValue(c.getDigits());
      writer.writeValue(c.getUnit());
      writer.writeValue(c.getHA());
      writer.writeValue(c.getHAProfile());

      // Empty key-value map and default key (stored in HA profile registry)
      writer.writeValue("");
      writer.writeValue(0);
    }
  }
  writer.flush();
  std::fclose(file);

  return static_cast<int64_t>(bytes_written);
}

int64_t Store::wipeCommands() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  commands_.clear();
  if (!ensureLittlefsMounted()) return -1;

  struct stat fileStat{};
  if (stat(kCommandsFilePath, &fileStat) != 0) {
    if (errno == ENOENT) return 0;
    return -1;
  }

  if (std::remove(kCommandsFilePath) != 0) {
    if (errno == ENOENT) return 0;
    return -1;
  }

  if (fileStat.st_size <= 0) {
    return 0;
  }

  return static_cast<int64_t>(fileStat.st_size);
}

void Store::fetchCommands(const ebus::JsonChunkVisitor& visitor) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ebus::detail::JsonWriter writer(visitor);
  auto array_scope = writer.arrayScope();

  size_t n = commands_.size();
  std::array<const Command*, 64> ordered{};
  for (size_t i = 0; i < n; i++) {
    ordered[i] = &commands_[i];
  }
  std::sort(ordered.begin(), ordered.begin() + n,
            [](const Command* a, const Command* b) {
              return std::string_view(a->getKey()) <
                     std::string_view(b->getKey());
            });

  for (size_t i = 0; i < n; i++) {
    writer.writeValue(*ordered[i]);
  }
}

const std::vector<Command*> Store::getCommands() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::vector<Command*> result;
  result.reserve(commands_.size());
  for (size_t i = 0; i < commands_.size(); i++) {
    result.push_back(&commands_[i]);
  }
  return result;
}

size_t Store::getActiveCommands() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  size_t count = 0;
  for (const Command& c : commands_) {
    if (c.getActive()) count++;
  }
  return count;
}

size_t Store::getPassiveCommands() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  size_t count = 0;
  for (const Command& c : commands_) {
    if (!c.getActive()) count++;
  }
  return count;
}

size_t Store::getCommandCount() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return commands_.size();
}

bool Store::active() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (const Command& c : commands_) {
    if (c.getActive()) return true;
  }
  return false;
}

Command* Store::nextActiveCommand() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  Command* next = nullptr;
  bool init = false;
  for (size_t i = 0; i < commands_.size(); i++) {
    Command* cmd = &commands_[i];
    if (!cmd->getActive()) continue;
    if (cmd->getLast() == 0) {
      next = cmd;
      init = true;
      break;
    }
    if (next == nullptr || (cmd->getLast() + cmd->getInterval() * 1000 <
                            next->getLast() + next->getInterval() * 1000))
      next = cmd;
  }

  if (!init && next &&
      (uint32_t)(esp_timer_get_time() / 1000ULL) <
          next->getLast() + next->getInterval() * 1000)
    next = nullptr;

  return next;
}

MatchingCommands Store::findPassiveCommands(ebus::ByteView master) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  MatchingCommands result;
  for (size_t i = 0; i < commands_.size(); i++) {
    Command* cmd = &commands_[i];
    // Skip active commands
    if (cmd->getActive()) continue;
    if (cmd->matches(master)) {
      if (!result.push_back(cmd)) break;
    }
  }
  return result;
}

void Store::updateData(Command* command, ebus::ByteView master_view,
                       ebus::ByteView slave_view) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto update = [this](Command* cmd, ebus::ByteView master_view,
                       ebus::ByteView slave_view) {
    cmd->setLast((uint32_t)(esp_timer_get_time() / 1000ULL));
    if (cmd->getMaster()) {
      cmd->setData(
          ebus::range(master_view, 4 + cmd->getPosition(), cmd->getLength()));
    } else {
      cmd->setData(
          ebus::range(slave_view, cmd->getPosition(), cmd->getLength()));
    }

    // Offload heavy JSON and string work to background task via key-only
    // callback
    if (data_updated_callback_) {
      data_updated_callback_(cmd->getKey());
    }

    if (data_updated_log_callback_) {
      data_updated_log_callback_(cmd->getKey());
    }
  };

  if (command) {
    update(command, master_view, slave_view);
    return;
  }

  // Find all matching commands (both active and passive)
  MatchingCommands matchingCommands = findAllMatchingCommands(master_view);
  for (Command* cmd : matchingCommands) update(cmd, master_view, slave_view);
}

void Store::fetchValues(const ebus::JsonChunkVisitor& visitor) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ebus::detail::JsonWriter writer(visitor);
  auto array_scope = writer.arrayScope();

  size_t n = commands_.size();
  std::array<const Command*, 64> ordered{};
  for (size_t i = 0; i < n; i++) {
    ordered[i] = &commands_[i];
  }
  std::sort(ordered.begin(), ordered.begin() + n,
            [](const Command* a, const Command* b) {
              return std::string_view(a->getKey()) <
                     std::string_view(b->getKey());
            });

  uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  for (size_t i = 0; i < n; i++) {
    const Command* cmd = ordered[i];
    auto scope = writer.objectScope();
    writer.writeField("key", cmd->getKey());
    writer.writeField("name", cmd->getName());

    writer.appendKey("value");
    cmd->getValueJson(writer);

    writer.writeField("unit", cmd->getUnit());
    writer.writeField("age",
                      (cmd->getLast() > 0) ? (now - cmd->getLast()) / 1000 : 0);
    writer.writeField("write", !cmd->getWriteCmd().empty());
    writer.writeField("active", cmd->getActive());
  }
}

void Store::deserializeCommands(FILE* file) {
  constexpr size_t kReaderBufSize = 1536;
  constexpr size_t kRowBufSize = 1024;
  constexpr size_t kChunkSize = 512;

  static char reader_buf[kReaderBufSize];
  static char row_buf[kRowBufSize];
  char chunk_buf[kChunkSize];

  ebus::detail::JsonReader reader(reader_buf, sizeof(reader_buf));

  bool eof = false;
  size_t loaded_count = 0;
  bool header_seen = false;

  auto feedFile = [&]() -> bool {
    if (eof) return false;
    size_t n = std::fread(chunk_buf, 1, sizeof(chunk_buf), file);
    if (n > 0) {
      reader.feed(std::string_view(chunk_buf, n));
      return true;
    }
    eof = true;
    reader.endOfInput();
    return false;
  };

  feedFile();

  // Expect root array
  while (true) {
    auto t = reader.next();
    if (t == ebus::detail::JsonReader::Token::need_more_data) {
      if (!feedFile()) {
        t = reader.next();
        if (t == ebus::detail::JsonReader::Token::need_more_data) return;
      }
      continue;
    }
    if (t == ebus::detail::JsonReader::Token::array_start) break;
    if (t == ebus::detail::JsonReader::Token::end ||
        t == ebus::detail::JsonReader::Token::error)
      return;
  }

  // Read each element
  while (true) {
    std::string_view row_sv = reader.rawValue();
    if (row_sv.empty()) {
      if (reader.needsMoreData()) {
        if (eof) {
          reader.endOfInput();
        }
        if (!feedFile()) {
          if (reader.needsMoreData()) {
            logger.warn("Store: Command element exceeds reader buffer size");
            return;
          }
          continue;
        }
        continue;
      }
      break;
    }

    size_t copy_len =
        row_sv.size() < kRowBufSize ? row_sv.size() : kRowBufSize - 1;
    std::memcpy(row_buf, row_sv.data(), copy_len);
    row_buf[copy_len] = '\0';

    ebus::detail::JsonReader row_reader(std::string_view(row_buf, copy_len));
    auto token = row_reader.next();

    if (token == ebus::detail::JsonReader::Token::array_start) {
      if (!header_seen) {
        header_seen = true;
        continue;
      }
      row_reader.reset();
      insertCommand(Command::fromTabular(row_reader));
      loaded_count++;
    } else if (token == ebus::detail::JsonReader::Token::object_start) {
      row_reader.reset();
      std::string evalError = Command::evaluate(row_reader);
      if (evalError.empty()) {
        row_reader.reset();
        insertCommand(Command::fromJson(row_reader));
        loaded_count++;
      } else {
        char err_buf[128];
        snprintf(err_buf, sizeof(err_buf),
                 "Store: Command validation failed: %s", evalError.c_str());
        logger.error(err_buf);
      }
    }
  }

  char res_buf[64];
  snprintf(res_buf, sizeof(res_buf), "Store: Deserialized %u commands.",
           static_cast<unsigned>(loaded_count));
  logger.info(res_buf);
}

#endif
