#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class WiFiClient {
 public:
  WiFiClient();
  explicit WiFiClient(int socketFd);
  WiFiClient(WiFiClient&& other) noexcept;
  WiFiClient& operator=(WiFiClient&& other) noexcept;
  WiFiClient(const WiFiClient&) = delete;
  WiFiClient& operator=(const WiFiClient&) = delete;
  ~WiFiClient();

  explicit operator bool() const;
  bool connected() const;
  void stop();

  int available() const;
  int availableForWrite() const;

  int read();
  int peek();
  int read(uint8_t* buffer, size_t size);
  size_t write(uint8_t byte);
  size_t write(const uint8_t* buffer, size_t size);
  size_t write(const char* buffer);

  void flush();
  void setNoDelay(bool enable);

  size_t print(const char* value);
  size_t print(const std::string& value);
  size_t println(const char* value);

 private:
  void closeSocket();

  int socketFd_ = -1;
};
