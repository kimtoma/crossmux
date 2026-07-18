#include "ReadingSyncCredentialStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstdio>

namespace {
constexpr uint8_t CONFIG_SCHEMA_VERSION = 1;
constexpr char CONFIG_DIRECTORY[] = "/.crosspoint/reading_sync";
constexpr char CONFIG_PATH[] = "/.crosspoint/reading_sync/config.json";
constexpr char CONFIG_TEMP_PATH[] = "/.crosspoint/reading_sync/config.json.tmp";
constexpr size_t MAX_CONFIG_BYTES = 8192;
constexpr size_t TOKEN_LENGTH = 47;
constexpr size_t TOKEN_SUFFIX_LENGTH = 43;

bool isValidToken(const std::string& token) {
  if (token.size() != TOKEN_LENGTH || token.compare(0, 4, "rd1_") != 0) {
    return false;
  }
  for (size_t index = token.size() - TOKEN_SUFFIX_LENGTH; index < token.size(); ++index) {
    const char c = token[index];
    const bool alphaNumeric = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (!alphaNumeric && c != '_' && c != '-') {
      return false;
    }
  }
  return true;
}

bool saveTokenAtomic(const std::string& token) {
  const String obfuscated = obfuscation::obfuscateToBase64(token);
  if (obfuscated.isEmpty()) {
    LOG_ERR("RSC", "Could not obfuscate reading sync credential");
    return false;
  }

  StaticJsonDocument<MAX_CONFIG_BYTES> document;
  document["schemaVersion"] = CONFIG_SCHEMA_VERSION;
  document["tokenObfuscated"] = obfuscated.c_str();
  const size_t serializedSize = measureJson(document);
  if (document.overflowed() || serializedSize == 0 || serializedSize > MAX_CONFIG_BYTES) {
    LOG_ERR("RSC", "Credential JSON exceeds persistence limit");
    return false;
  }

  Storage.mkdir(CONFIG_DIRECTORY);
  if (Storage.exists(CONFIG_TEMP_PATH)) {
    Storage.remove(CONFIG_TEMP_PATH);
  }

  bool wroteCompleteFile = false;
  {
    HalFile file;
    if (!Storage.openFileForWrite("RSC", CONFIG_TEMP_PATH, file)) {
      return false;
    }
    const size_t written = serializeJson(document, file);
    file.flush();
    wroteCompleteFile = written == serializedSize;
  }
  if (!wroteCompleteFile) {
    Storage.remove(CONFIG_TEMP_PATH);
    LOG_ERR("RSC", "Credential JSON write was incomplete");
    return false;
  }
  if (Storage.exists(CONFIG_PATH) && !Storage.remove(CONFIG_PATH)) {
    Storage.remove(CONFIG_TEMP_PATH);
    LOG_ERR("RSC", "Could not replace credential JSON");
    return false;
  }
  if (!Storage.rename(CONFIG_TEMP_PATH, CONFIG_PATH)) {
    Storage.remove(CONFIG_TEMP_PATH);
    LOG_ERR("RSC", "Could not activate credential JSON");
    return false;
  }
  return true;
}
}  // namespace

ReadingSyncCredentialStore& ReadingSyncCredentialStore::getInstance() {
  static ReadingSyncCredentialStore instance;
  return instance;
}

bool ReadingSyncCredentialStore::loadFromFile() {
  if (!Storage.exists(CONFIG_PATH)) {
    token_.clear();
    return true;
  }

  StaticJsonDocument<MAX_CONFIG_BYTES> document;
  {
    HalFile file;
    if (!Storage.openFileForRead("RSC", CONFIG_PATH, file)) {
      return false;
    }
    const size_t fileSize = file.fileSize();
    if (fileSize == 0 || fileSize > MAX_CONFIG_BYTES) {
      return false;
    }
    const DeserializationError error = deserializeJson(document, file);
    if (error || document.overflowed()) {
      return false;
    }
  }

  if (!document.is<JsonObjectConst>()) {
    return false;
  }
  const JsonObjectConst root = document.as<JsonObjectConst>();
  if (root.size() != 2 || !root["schemaVersion"].is<uint32_t>() ||
      root["schemaVersion"].as<uint32_t>() != CONFIG_SCHEMA_VERSION || !root["tokenObfuscated"].is<const char*>()) {
    return false;
  }

  bool decodedOk = false;
  const std::string decoded = obfuscation::deobfuscateFromBase64(root["tokenObfuscated"].as<const char*>(), &decodedOk);
  if (!decodedOk || !isValidToken(decoded)) {
    return false;
  }
  token_ = decoded;
  return true;
}

bool ReadingSyncCredentialStore::setToken(const std::string& token) {
  if (!isValidToken(token)) {
    return false;
  }
  if (token_ == token) {
    return true;
  }
  if (!saveTokenAtomic(token)) {
    return false;
  }
  token_ = token;
  return true;
}

bool ReadingSyncCredentialStore::clearToken() {
  if (Storage.exists(CONFIG_TEMP_PATH)) {
    Storage.remove(CONFIG_TEMP_PATH);
  }
  if (Storage.exists(CONFIG_PATH) && !Storage.remove(CONFIG_PATH)) {
    LOG_ERR("RSC", "Could not remove credential JSON");
    return false;
  }
  token_.clear();
  return true;
}

bool ReadingSyncCredentialStore::hasToken() const { return !token_.empty(); }

const std::string& ReadingSyncCredentialStore::tokenForRequest() const { return token_; }

std::string ReadingSyncCredentialStore::maskedStatus() const {
  if (token_.empty()) {
    return tr(STR_NOT_SET);
  }

  char maskedToken[16] = {};
  std::snprintf(maskedToken, sizeof(maskedToken), "rd1_…%.4s", token_.c_str() + token_.size() - 4);
  char status[48] = {};
  std::snprintf(status, sizeof(status), tr(STR_READING_SYNC_TOKEN_SET), maskedToken);
  return status;
}
