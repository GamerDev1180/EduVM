#include "core/Http.hpp"

#include <cstdint>
#include <memory>

#include "core/Strings.hpp"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace fs = std::filesystem;

namespace eduvm {

static const char* kUserAgent = "EduVM/0.1";

#ifdef _WIN32

namespace {

struct HInternetDeleter {
    void operator()(HINTERNET h) const {
        if (h) WinHttpCloseHandle(h);
    }
};
using HInternet = std::unique_ptr<void, HInternetDeleter>;

std::string winHttpError(const char* what) { return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")"; }

// Opens the request, sends it and waits for headers. The caller reads the body.
struct OpenRequest {
    HInternet session, connection, request;
    int status = 0;
    std::uint64_t contentLength = 0;
};

OpenRequest openRequest(const std::string& method, const std::string& url, const HttpHeaders& headers,
                        const std::string& body) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {};
    wchar_t path[4096] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    wchar_t extra[4096] = {};
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) throw Error("Invalid URL: " + url);

    OpenRequest r;
    r.session.reset(WinHttpOpen(widen(kUserAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0));
    if (!r.session) throw Error(winHttpError("WinHttpOpen"));
    // Generous receive timeout: AI answers can take a while.
    WinHttpSetTimeouts(r.session.get(), 10000, 15000, 30000, 600000);

    r.connection.reset(WinHttpConnect(r.session.get(), host, parts.nPort, 0));
    if (!r.connection) throw Error(winHttpError("WinHttpConnect"));

    std::wstring object = std::wstring(path) + extra;
    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    r.request.reset(WinHttpOpenRequest(r.connection.get(), widen(method).c_str(), object.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!r.request) throw Error(winHttpError("WinHttpOpenRequest"));

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(r.request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    std::wstring headerBlock;
    for (auto& [k, v] : headers) headerBlock += widen(k) + L": " + widen(v) + L"\r\n";

    BOOL sent = WinHttpSendRequest(r.request.get(), headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str(),
                                   static_cast<DWORD>(-1L), body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                                   static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!sent) throw Error("Could not reach " + url + " (" + winHttpError("WinHttpSendRequest") + "). Check your internet connection.");
    if (!WinHttpReceiveResponse(r.request.get(), nullptr)) throw Error(winHttpError("WinHttpReceiveResponse"));

    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(r.request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<int>(status);

    wchar_t lengthBuf[32] = {};
    size = sizeof(lengthBuf);
    if (WinHttpQueryHeaders(r.request.get(), WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthBuf, &size,
                            WINHTTP_NO_HEADER_INDEX))
        r.contentLength = std::wcstoull(lengthBuf, nullptr, 10);
    return r;
}

template <typename Sink>
void readBody(HINTERNET request, Sink&& sink) {
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) throw Error(winHttpError("WinHttpQueryDataAvailable"));
        if (available == 0) break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read)) throw Error(winHttpError("WinHttpReadData"));
        chunk.resize(read);
        sink(chunk);
    }
}

}  // namespace

HttpResponse httpRequest(const std::string& method, const std::string& url, const HttpHeaders& headers,
                         const std::string& body) {
    OpenRequest r = openRequest(method, url, headers, body);
    HttpResponse resp;
    resp.status = r.status;
    readBody(r.request.get(), [&](const std::string& chunk) { resp.body += chunk; });
    return resp;
}

#else  // libcurl

namespace {

struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};

size_t appendToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

}  // namespace

HttpResponse httpRequest(const std::string& method, const std::string& url, const HttpHeaders& headers,
                         const std::string& body) {
    static CurlGlobal global;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw Error("curl_easy_init failed");

    curl_slist* list = nullptr;
    for (auto& [k, v] : headers) list = curl_slist_append(list, (k + ": " + v).c_str());

    HttpResponse resp;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, list);
    if (!body.empty()) {
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, appendToString);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &resp.body);

    CURLcode rc = curl_easy_perform(curl.get());
    curl_slist_free_all(list);
    if (rc != CURLE_OK) throw Error("Could not reach " + url + ": " + curl_easy_strerror(rc));
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    resp.status = static_cast<int>(status);
    return resp;
}

#endif

}  // namespace eduvm
