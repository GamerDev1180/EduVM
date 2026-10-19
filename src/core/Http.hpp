#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

namespace eduvm {

struct HttpResponse {
    int status = 0;
    std::string body;
};

using HttpHeaders = std::map<std::string, std::string>;
// Called with (bytesSoFar, totalBytes or 0 if unknown).
using ProgressFn = std::function<void(std::uint64_t, std::uint64_t)>;

// Thin HTTPS client: WinHTTP on Windows (no extra DLLs to ship to students),
// libcurl elsewhere.
HttpResponse httpRequest(const std::string& method, const std::string& url, const HttpHeaders& headers,
                         const std::string& body = {});

inline HttpResponse httpGet(const std::string& url, const HttpHeaders& headers = {}) {
    return httpRequest("GET", url, headers);
}
inline HttpResponse httpPost(const std::string& url, const HttpHeaders& headers, const std::string& body) {
    return httpRequest("POST", url, headers, body);
}

// Streams a (possibly multi-GB) file to disk; used for lab base images.
void httpDownload(const std::string& url, const std::filesystem::path& dest, const ProgressFn& progress = {});

}  // namespace eduvm
