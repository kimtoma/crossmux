#pragma once

#include <string>

class ReadingSyncCredentialStore {
 public:
  static ReadingSyncCredentialStore& getInstance();
  bool loadFromFile();
  bool setToken(const std::string& token);
  bool clearToken();
  bool hasToken() const;
  const std::string& tokenForRequest() const;
  std::string maskedStatus() const;

 private:
  std::string token_;
};

#define READING_SYNC_CREDENTIALS ReadingSyncCredentialStore::getInstance()
