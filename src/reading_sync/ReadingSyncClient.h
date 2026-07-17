#pragma once

#include <string>

#include "ReadingSyncResponseValidation.h"
#include "network/HttpDownloader.h"

struct ReadingCoverJob;
class ReadingSyncQueue;
class ReadingSyncCredentialStore;

class ReadingSyncClient {
 public:
  static ReadingSyncClient& getInstance();
  HttpDownloader::HttpResult validate(const std::string& token, bool* cancelFlag);
  HttpDownloader::HttpResult sync(const ReadingSyncMetadata& metadata, const std::string& token,
                                  ReadingSyncResponse& response, bool* cancelFlag);
  HttpDownloader::HttpResult uploadCover(const ReadingCoverJob& cover, const std::string& token, bool* cancelFlag);
  void performPendingSync(ReadingSyncQueue& queue, ReadingSyncCredentialStore& credentials, bool* cancelFlag);
};

#define READING_SYNC_CLIENT ReadingSyncClient::getInstance()
