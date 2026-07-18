#pragma once

#include <atomic>
#include <string>

#include "ReadingSyncResponseValidation.h"
#include "ReadingSyncUiPolicy.h"
#include "network/HttpDownloader.h"

struct ReadingCoverJob;
class ReadingSyncQueue;
class ReadingSyncCredentialStore;

class ReadingSyncClient {
 public:
  static ReadingSyncClient& getInstance();
  HttpDownloader::HttpResult validate(const std::string& token, const std::atomic_bool* cancelFlag);
  HttpDownloader::HttpResult sync(const ReadingSyncMetadata& metadata, const std::string& token,
                                  ReadingSyncResponse& response, const std::atomic_bool* cancelFlag);
  HttpDownloader::HttpResult uploadCover(const ReadingCoverJob& cover, const std::string& token,
                                         const std::atomic_bool* cancelFlag);
  KimtomaConnectionTestState performValidation(ReadingSyncCredentialStore& credentials,
                                               const std::atomic_bool* cancelFlag);
  void performPendingSync(ReadingSyncQueue& queue, ReadingSyncCredentialStore& credentials,
                          const std::atomic_bool* cancelFlag);
};

#define READING_SYNC_CLIENT ReadingSyncClient::getInstance()
