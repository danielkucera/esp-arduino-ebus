#if defined(EBUS_INTERNAL)
#include "store.hpp"

#include <esp_littlefs.h>
#include <esp_timer.h>
#include <sys/stat.h>

#include <algorithm>
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
constexpr const char* littlefs_base_path = "/littlefs";
constexpr const char* littlefs_partition_label = "littlefs";
constexpr const char* commands_file_path = "/littlefs/commands.json";

bool ensureLittlefsMounted() {
  static bool mounted = false;
  if (mounted) return true;

  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = littlefs_base_path;
  conf.partition_label = littlefs_partition_label;
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

void Store::insertCommand(Command command) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (size_t i = 0; i < commands_.size(); i++) {
    if (std::string_view(commands_[i].getKey()) ==
        std::string_view(command.getKey())) {
      // Move write_cmd to separate storage before overwriting
      if (!command.getWriteCmdTemp().empty()) {
        command.setWriteCmd(std::move(command.getWriteCmdTemp()), *this);
      } else if (command.hasWriteCmd()) {
        PollSequence ps;
        ps.assign(command.getWriteCmd(*this));
        command.setWriteCmd(std::move(ps), *this);
      }
      commands_[i] = std::move(command);
      if (command_changed_callback_) command_changed_callback_(&commands_[i]);
      return;
    }
  }
  // Move write_cmd to separate storage before inserting
  if (!command.getWriteCmdTemp().empty()) {
    command.setWriteCmd(std::move(command.getWriteCmdTemp()), *this);
  } else if (command.hasWriteCmd()) {
    PollSequence ps;
    ps.assign(command.getWriteCmd(*this));
    command.setWriteCmd(std::move(ps), *this);
  }
  if (commands_.push_back(std::move(command))) {
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

ebus::ByteView Store::getWriteCmd(size_t idx) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (idx < write_cmds_.size()) {
    return ebus::ByteView(write_cmds_[idx].data(), write_cmds_[idx].size());
  }
  return {};
}

bool Store::addWriteCmd(PollSequence&& cmd) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (write_cmds_.size() >= write_cmd_capacity) {
    return false;
  }
  write_cmds_.push_back(std::move(cmd));
  return true;
}

size_t Store::getWriteCmdCount() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return write_cmds_.size();
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

Command* Store::findCommand(uint16_t poll_id) {
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
  return loadCommandsFrom(commands_file_path);
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

  FILE* file = std::fopen(commands_file_path, "wb");
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
      static const char* header[] = {"key",       "name",   "read_cmd",
                                     "write_cmd", "active", "interval",
                                     "fields"};
      for (const char* h : header) writer.writeValue(h);
    }

    // Data rows in tabular format
    for (const Command& c : commands_) {
      auto row_array = writer.arrayScope();
      writer.writeValue(c.getKey());
      writer.writeValue(c.getName());
      writer.writeHexValue(c.getReadCmd());
      // Serialize write_cmd from separate storage
      if (c.hasWriteCmd()) {
        writer.writeHexValue(c.getWriteCmd(*this));
      } else {
        writer.writeValue("");
      }
      writer.writeValue(c.getActive());
      writer.writeValue(c.getInterval());

      // Serialize fields as JSON (with per-field HA)
      {
        auto fields_arr = writer.arrayScope();
        for (size_t i = 0; i < c.getFieldCount(); i++) {
          auto field_obj = writer.objectScope();
          writer.writeField("name", c.getFieldName(i));
          writer.writeField("profile", c.getFieldProfile(i)
                                           ? c.getFieldProfile(i)->name
                                           : "");
          writer.writeField("position",
                            static_cast<uint32_t>(c.getFieldPosition(i)));
          writer.writeField("master", c.getFieldMaster(i));
          writer.writeField("ha", c.getFieldHA(i));
          writer.writeField("ha_profile", c.getFieldHAProfileName(i));
        }
      }
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
  if (stat(commands_file_path, &fileStat) != 0) {
    if (errno == ENOENT) return 0;
    return -1;
  }

  if (std::remove(commands_file_path) != 0) {
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
    const Command* c = ordered[i];
    auto scope = writer.objectScope();
    writer.writeField("key", c->getKey());
    writer.writeField("name", c->getName());
    writer.writeHexField("read_cmd", c->getReadCmd());
    if (c->hasWriteCmd()) {
      writer.writeHexField("write_cmd", c->getWriteCmd(*this));
    } else {
      writer.writeField("write_cmd", "");
    }
    writer.writeField("active", c->getActive());
    writer.writeField("interval", c->getInterval());

    auto arr = writer.arrayScope("fields");
    for (size_t j = 0; j < c->getFieldCount(); j++) {
      auto field_obj = writer.objectScope();
      writer.writeField("name", c->getFieldName(j));
      const DataProfile* p = c->getFieldProfile(j);
      writer.writeField("profile", p ? p->name : "");
      writer.writeField("position",
                        static_cast<uint32_t>(c->getFieldPosition(j)));
      writer.writeField("master", c->getFieldMaster(j));
      writer.writeField("ha", c->getFieldHA(j));
      writer.writeField("ha_profile", c->getFieldHAProfileName(j));
    }
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
  return std::count_if(commands_.begin(), commands_.end(),
                       [](const Command& c) { return c.getActive(); });
}

size_t Store::getPassiveCommands() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return std::count_if(commands_.begin(), commands_.end(),
                       [](const Command& c) { return !c.getActive(); });
}

size_t Store::getCommandCount() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return commands_.size();
}

bool Store::active() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return std::any_of(commands_.begin(), commands_.end(),
                     [](const Command& c) { return c.getActive(); });
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

    if (cmd->getFieldCount() == 0) return;

    size_t min_pos = SIZE_MAX;
    size_t max_end = 0;
    bool is_master = cmd->getFieldMaster(0);

    for (size_t i = 0; i < cmd->getFieldCount(); i++) {
      auto* profile = cmd->getFieldProfile(i);
      if (!profile) continue;
      size_t field_len = ebus::sizeOfDataType(cmd->getFieldDatatype(i));
      size_t end = cmd->getFieldPosition(i) + field_len;
      min_pos = std::min(min_pos, cmd->getFieldPosition(i));
      max_end = std::max(max_end, end);
    }

    if (min_pos > max_end) return;

    size_t data_len = max_end - min_pos;
    if (is_master) {
      cmd->setData(ebus::range(master_view, 4 + min_pos, data_len));
    } else {
      cmd->setData(ebus::range(slave_view, min_pos, data_len));
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

    std::string unit;
    if (cmd->getFieldCount() > 0) {
      unit = std::string(cmd->getFieldUnit(0));
    }
    writer.writeField("unit", unit);
    writer.writeField("age",
                      (cmd->getLast() > 0) ? (now - cmd->getLast()) / 1000 : 0);
    writer.writeField("write", cmd->hasWriteCmd());
    writer.writeField("active", cmd->getActive());
  }
}

void Store::deserializeCommands(FILE* file) {
  constexpr size_t reader_buf_size = 1536;
  constexpr size_t row_buf_size = 1024;
  constexpr size_t chunk_size = 512;

  static char reader_buf[reader_buf_size];
  static char row_buf[row_buf_size];
  char chunk_buf[chunk_size];

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
        row_sv.size() < row_buf_size ? row_sv.size() : row_buf_size - 1;
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
