#if defined(EBUS_INTERNAL)
#include "command_manager.hpp"

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
#include <limits>

#include "logger.hpp"
#include "mqtt.hpp"

CommandManager commandManager;

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

bool CommandManager::initFileSystem() { return ensureLittlefsMounted(); }

void CommandManager::setDataUpdatedCallback(DataUpdatedCallback callback) {
  data_updated_callback_ = std::move(callback);
}

void CommandManager::setDataUpdatedLogCallback(
    DataUpdatedLogCallback callback) {
  data_updated_log_callback_ = std::move(callback);
}

void CommandManager::setCommandChangedCallback(
    CommandChangedCallback callback) {
  command_changed_callback_ = std::move(callback);
}

void CommandManager::setCommandRemovedCallback(
    CommandChangedCallback callback) {
  command_removed_callback_ = std::move(callback);
}

void CommandManager::insertCommand(Command command) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (size_t i = 0; i < commands_.size(); i++) {
    if (std::string_view(commands_[i].getKey()) ==
        std::string_view(command.getKey())) {
      uint16_t old_poll_id = commands_[i].getPollId();
      commands_[i] = std::move(command);
      commands_[i].setPollId(old_poll_id);
      if (command_changed_callback_) command_changed_callback_(&commands_[i]);
      return;
    }
  }
  if (commands_.push_back(std::move(command))) {
    if (command_changed_callback_) command_changed_callback_(&commands_.back());
  }
}

void CommandManager::removeCommand(std::string_view key) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (size_t i = 0; i < commands_.size(); i++) {
    if (std::string_view(commands_[i].getKey()) == key) {
      uint8_t removed_key_id = commands_[i].getKeyId();
      if (command_removed_callback_) command_removed_callback_(&commands_[i]);
      commands_.erase(commands_.begin() + i);
      removeFieldOverrides(removed_key_id);
      return;
    }
  }
}

void CommandManager::removeAll() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (size_t i = commands_.size(); i-- > 0;) {
    if (command_removed_callback_) command_removed_callback_(&commands_[i]);
  }
  commands_.clear();
  clearFieldOverrides();
}

Command* CommandManager::findCommand(std::string_view key) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (size_t i = 0; i < commands_.size(); i++) {
    if (std::string_view(commands_[i].getKey()) == key) {
      return &commands_[i];
    }
  }
  return nullptr;
}

Command* CommandManager::findCommand(uint16_t poll_id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (size_t i = 0; i < commands_.size(); i++) {
    Command* cmd = &commands_[i];
    if (cmd->getPollId() == poll_id) {
      return cmd;
    }
  }
  return nullptr;
}

MatchingCommands CommandManager::findPassiveCommands(ebus::ByteView master) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  MatchingCommands result;
  for (size_t i = 0; i < commands_.size(); i++) {
    Command* cmd = &commands_[i];
    if (cmd->getActive()) continue;
    if (cmd->matches(master)) {
      if (!result.push_back(cmd)) break;
    }
  }
  return result;
}

int64_t CommandManager::loadCommands() {
  if (!ensureLittlefsMounted()) return -1;
  std::remove("/littlefs/commands.json.tmp");
  return loadCommandsFrom(commands_file_path);
}

int64_t CommandManager::loadCommandsFrom(const char* path) {
  if (!ensureLittlefsMounted()) return -1;

  FILE* file = std::fopen(path, "rb");
  if (file == nullptr) {
    if (errno == ENOENT) return 0;
    char err_buf[64];
    snprintf(err_buf, sizeof(err_buf),
             "CommandManager: Failed to open commands file %s: %d", path,
             errno);
    logger.error(err_buf);
    return -1;
  }

  // Use small buffer to avoid large heap allocation for FILE* stream
  char file_buf[512];
  std::setvbuf(file, file_buf, _IOFBF, sizeof(file_buf));

  char log_buf[96];
  snprintf(log_buf, sizeof(log_buf), "CommandManager: Loading from %s", path);
  logger.info(log_buf);

  deserializeCommands(file);
  std::fclose(file);
  return static_cast<int64_t>(commandManager.getCommandCount());
}

int64_t CommandManager::saveCommands() const {
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
      static const char* header[] = {"key",       "name",     "read_cmd",
                                     "write_cmd", "interval", "master",
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
      writer.writeValue(c.getInterval());
      writer.writeValue(c.getMaster());

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
          writer.writeField("ha_profile", c.getFieldHAProfileName(i));
          float min_ov = getFieldMinOverride(c.getKeyId(), i);
          float max_ov = getFieldMaxOverride(c.getKeyId(), i);
          if (!std::isnan(min_ov)) {
            writer.writeField("min", min_ov);
          }
          if (!std::isnan(max_ov)) {
            writer.writeField("max", max_ov);
          }
        }
      }
    }
  }
  writer.flush();
  std::fclose(file);

  return static_cast<int64_t>(bytes_written);
}

int64_t CommandManager::wipeCommands() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  commands_.clear();
  clearFieldOverrides();
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

void CommandManager::fetchCommands(
    const ebus::JsonChunkVisitor& visitor) const {
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
    writer.writeField("interval", c->getInterval());
    writer.writeField("master", c->getMaster());

    auto arr = writer.arrayScope("fields");
    for (size_t j = 0; j < c->getFieldCount(); j++) {
      auto field_obj = writer.objectScope();
      writer.writeField("name", c->getFieldName(j));
      const DataProfile* p = c->getFieldProfile(j);
      writer.writeField("profile", p ? p->name : "");
      writer.writeField("position",
                        static_cast<uint32_t>(c->getFieldPosition(j)));
      writer.writeField("ha_profile", c->getFieldHAProfileName(j));
      float min_ov = getFieldMinOverride(c->getKeyId(), j);
      float max_ov = getFieldMaxOverride(c->getKeyId(), j);
      if (!std::isnan(min_ov)) {
        writer.writeField("min", min_ov);
      }
      if (!std::isnan(max_ov)) {
        writer.writeField("max", max_ov);
      }
    }
  }
}

size_t CommandManager::getActiveCommands() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return std::count_if(commands_.begin(), commands_.end(),
                       [](const Command& c) { return c.getActive(); });
}

size_t CommandManager::getPassiveCommands() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return std::count_if(commands_.begin(), commands_.end(),
                       [](const Command& c) { return !c.getActive(); });
}

size_t CommandManager::getCommandCount() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return commands_.size();
}

Command* CommandManager::nextActiveCommand() {
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
void CommandManager::updateData(const ebus::ProtocolInfo& info) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);

  Command* command = nullptr;
  if (info.poll_id != 0) {
    command = findCommand(info.poll_id);
  }

  auto update = [this](Command* cmd, const ebus::ProtocolInfo& info) {
    if (cmd->getFieldCount() == 0) return;

    bool is_master = cmd->getMaster();
    size_t data_len = 0;
    if (is_master) {
      if (info.master_view.size() >= 5) data_len = info.master_view[4];
    } else {
      if (info.slave_view.size() >= 1) data_len = info.slave_view[0];
    }

    if (data_len == 0) return;

    cmd->setLast((uint32_t)(esp_timer_get_time() / 1000ULL));
    cmd->setSessionId(info.session_id);

    if (is_master) {
      cmd->setData(ebus::range(info.master_view, 5, data_len));
    } else {
      cmd->setData(ebus::range(info.slave_view, 1, data_len));
    }

    if (data_updated_callback_) {
      data_updated_callback_(cmd->getKey());
    }

    if (data_updated_log_callback_) {
      data_updated_log_callback_(cmd->getKey());
    }
  };

  if (command) {
    update(command, info);
    return;
  }

  MatchingCommands matchingCommands = findPassiveCommands(info.master_view);
  for (Command* cmd : matchingCommands) update(cmd, info);
}

void CommandManager::fetchValues(const ebus::JsonChunkVisitor& visitor) const {
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
  }
}

ebus::ByteView CommandManager::getWriteCmd(size_t idx) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (idx < write_cmds_.size()) {
    return ebus::ByteView(write_cmds_[idx].data(), write_cmds_[idx].size());
  }
  return {};
}

bool CommandManager::addWriteCmd(PollSequence&& cmd) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (write_cmds_.size() >= write_cmd_capacity) {
    return false;
  }
  write_cmds_.push_back(std::move(cmd));
  return true;
}

size_t CommandManager::getWriteCmdCount() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return write_cmds_.size();
}

namespace {
template <size_t Capacity>
FieldOverride* findOverride(ebus::StaticVector<FieldOverride, Capacity>& pool,
                            uint8_t key_id, size_t field_idx) {
  for (size_t i = 0; i < pool.size(); ++i) {
    if (pool[i].key_id == key_id && pool[i].field_idx == field_idx) {
      return &pool[i];
    }
  }
  return nullptr;
}

template <size_t Capacity>
const FieldOverride* findOverride(
    const ebus::StaticVector<FieldOverride, Capacity>& pool, uint8_t key_id,
    size_t field_idx) {
  for (size_t i = 0; i < pool.size(); ++i) {
    if (pool[i].key_id == key_id && pool[i].field_idx == field_idx) {
      return &pool[i];
    }
  }
  return nullptr;
}
}  // namespace

float CommandManager::getFieldMinOverride(uint8_t key_id,
                                          size_t field_idx) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto* o =
      findOverride(field_overrides_, key_id, static_cast<uint8_t>(field_idx));
  return o ? o->min_override : std::numeric_limits<float>::quiet_NaN();
}

float CommandManager::getFieldMaxOverride(uint8_t key_id,
                                          size_t field_idx) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto* o =
      findOverride(field_overrides_, key_id, static_cast<uint8_t>(field_idx));
  return o ? o->max_override : std::numeric_limits<float>::quiet_NaN();
}

void CommandManager::setFieldMinOverride(uint8_t key_id, size_t field_idx,
                                         float min_val) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto* o =
      findOverride(field_overrides_, key_id, static_cast<uint8_t>(field_idx));
  if (o) {
    o->min_override = min_val;
    return;
  }
  if (field_overrides_.size() >= field_overrides_.capacity()) return;
  FieldOverride entry;
  entry.key_id = key_id;
  entry.field_idx = static_cast<uint8_t>(field_idx);
  entry.min_override = min_val;
  field_overrides_.push_back(entry);
}

void CommandManager::setFieldMaxOverride(uint8_t key_id, size_t field_idx,
                                         float max_val) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto* o =
      findOverride(field_overrides_, key_id, static_cast<uint8_t>(field_idx));
  if (o) {
    o->max_override = max_val;
    return;
  }
  if (field_overrides_.size() >= field_overrides_.capacity()) return;
  FieldOverride entry;
  entry.key_id = key_id;
  entry.field_idx = static_cast<uint8_t>(field_idx);
  entry.max_override = max_val;
  field_overrides_.push_back(entry);
}

void CommandManager::removeFieldOverrides(uint8_t key_id) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (size_t i = field_overrides_.size(); i-- > 0;) {
    if (field_overrides_[i].key_id == key_id) {
      field_overrides_.erase(field_overrides_.begin() + i);
    }
  }
}

void CommandManager::clearFieldOverrides() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  field_overrides_.clear();
}

void CommandManager::deserializeCommands(FILE* file) {
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
            logger.warn(
                "CommandManager: Command element exceeds reader buffer size");
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
      // Pre-extract key to drop any stale field overrides for that key
      // before fromTabular() repopulates the pool.
      row_reader.reset();
      if (row_reader.next() == ebus::detail::JsonReader::Token::array_start) {
        if (row_reader.next() == ebus::detail::JsonReader::Token::string) {
          uint8_t kid = StringPool::instance().intern(row_reader.value());
          if (kid != 0) removeFieldOverrides(kid);
        }
      }
      row_reader.reset();
      insertCommand(Command::fromTabular(row_reader));
      loaded_count++;
    } else if (token == ebus::detail::JsonReader::Token::object_start) {
      row_reader.reset();
      std::string_view evalError = Command::evaluate(row_reader);
      if (evalError.empty()) {
        // Pre-extract key to drop stale overrides before fromJson()
        // repopulates.
        row_reader.reset();
        if (row_reader.next() ==
            ebus::detail::JsonReader::Token::object_start) {
          row_reader.findKey("key");
          if (row_reader.next() == ebus::detail::JsonReader::Token::string) {
            uint8_t kid = StringPool::instance().intern(row_reader.value());
            if (kid != 0) removeFieldOverrides(kid);
          }
        }
        row_reader.reset();
        insertCommand(Command::fromJson(row_reader));
        loaded_count++;
      } else {
        char err_buf[128];
        snprintf(err_buf, sizeof(err_buf),
                 "CommandManager: Command validation failed: %.*s",
                 static_cast<int>(evalError.length()), evalError.data());
        logger.error(err_buf);
      }
    }
  }

  char res_buf[64];
  snprintf(res_buf, sizeof(res_buf),
           "CommandManager: Deserialized %u commands.",
           static_cast<unsigned>(loaded_count));
  logger.info(res_buf);
}

#endif
