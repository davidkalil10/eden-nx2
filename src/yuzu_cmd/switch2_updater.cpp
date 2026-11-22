// SPDX-FileCopyrightText: Copyright 2026 Eden-NX2 Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef __SWITCH2__

#include "yuzu_cmd/switch2_updater.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

#include <curl/curl.h>

#define u128 libnx_u128
#define s128 libnx_s128
#include <switch.h>
#undef u128
#undef s128

namespace {

constexpr const char* kLatestReleaseApiUrl =
    "https://api.github.com/repos/davidkalil10/eden-nx2/releases/latest";

struct ScopedSocketInit {
    bool initialized = false;
    ScopedSocketInit() {
        initialized = R_SUCCEEDED(socketInitializeDefault());
    }
    ~ScopedSocketInit() {
        if (initialized) {
            socketExit();
        }
    }
};

struct ScopedCurlGlobal {
    bool ok = false;
    ScopedCurlGlobal() {
        ok = (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    }
    ~ScopedCurlGlobal() {
        if (ok) {
            curl_global_cleanup();
        }
    }
};

std::size_t WriteToString(void* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    const std::size_t bytes = size * nmemb;
    if (out->size() + bytes > 4u * 1024u * 1024u) {
        return 0;
    }
    out->append(static_cast<const char*>(ptr), bytes);
    return bytes;
}

std::size_t WriteToFile(void* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* fp = static_cast<std::FILE*>(userdata);
    return std::fwrite(ptr, size, nmemb, fp);
}

struct DownloadProgressCtx {
    const Switch2UpdateProgressFn* cb = nullptr;
    bool cancelled = false;
};

int CurlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                         curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* ctx = static_cast<DownloadProgressCtx*>(clientp);
    if (ctx && ctx->cb && *ctx->cb) {
        const std::uint64_t now = dlnow > 0 ? static_cast<std::uint64_t>(dlnow) : 0ULL;
        const std::uint64_t tot = dltotal > 0 ? static_cast<std::uint64_t>(dltotal) : 0ULL;
        if (!(*ctx->cb)(now, tot)) {
            ctx->cancelled = true;
            return 1; // abort transfer
        }
    }
    return 0;
}

std::string NormalizeVersion(std::string_view tag) {
    while (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V' || tag.front() == ' ')) {
        tag.remove_prefix(1);
    }
    std::string out;
    for (char c : tag) {
        if ((c >= '0' && c <= '9') || c == '.') {
            out.push_back(c);
        } else {
            break;
        }
    }
    return out;
}

std::array<int, 3> ParseSemver(std::string_view ver) {
    std::array<int, 3> parts{0, 0, 0};
    int idx = 0;
    int cur = 0;
    for (char c : ver) {
        if (c >= '0' && c <= '9') {
            cur = cur * 10 + (c - '0');
        } else if (c == '.' && idx < 2) {
            parts[idx++] = cur;
            cur = 0;
        } else {
            break;
        }
    }
    parts[idx] = cur;
    return parts;
}

bool IsNewerVersion(std::string_view latest, std::string_view current) {
    const auto a = ParseSemver(latest);
    const auto b = ParseSemver(current);
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i]) {
            return a[i] > b[i];
        }
    }
    return false;
}

// Extracts a JSON string value for a given key starting at pos.
bool ExtractJsonString(const std::string& json, const std::string& key, std::size_t start_pos,
                       std::string* out_val, std::size_t* out_next_pos = nullptr) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = json.find(needle, start_pos);
    if (pos == std::string::npos) {
        return false;
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' ||
                                 json[pos] == '\r')) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"') {
        return false;
    }
    ++pos;
    std::string val;
    while (pos < json.size()) {
        const char c = json[pos++];
        if (c == '\\' && pos < json.size()) {
            const char esc = json[pos++];
            switch (esc) {
            case '"':
            case '\\':
            case '/':
                val.push_back(esc);
                break;
            case 'n':
                val.push_back('\n');
                break;
            case 't':
                val.push_back('\t');
                break;
            default:
                val.push_back(esc);
                break;
            }
        } else if (c == '"') {
            if (out_val) *out_val = std::move(val);
            if (out_next_pos) *out_next_pos = pos;
            return true;
        } else {
            val.push_back(c);
        }
    }
    return false;
}

std::string FindNroAssetUrl(const std::string& json) {
    std::string fallback_nro;
    std::size_t pos = 0;
    std::string url;
    while (ExtractJsonString(json, "browser_download_url", pos, &url, &pos)) {
        std::string lower = url;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.find("eden-switch2.nro") != std::string::npos) {
            return url;
        }
        if (fallback_nro.empty() && lower.size() >= 4 &&
            lower.compare(lower.size() - 4, 4, ".nro") == 0) {
            fallback_nro = url;
        }
    }
    return fallback_nro;
}

bool ValidateSwitch2NroFile(const std::string& path, std::string* out_err) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (out_err) *out_err = "Failed to open downloaded file for verification.";
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long fsize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (fsize < 1024 * 1024) {
        std::fclose(f);
        if (out_err) *out_err = "Downloaded NRO is too small (incomplete download).";
        return false;
    }
    std::array<unsigned char, 0x100> hdr{};
    const std::size_t got = std::fread(hdr.data(), 1, hdr.size(), f);
    std::fclose(f);
    if (got < 0x30) {
        if (out_err) *out_err = "Failed to read NRO header.";
        return false;
    }
    if (std::memcmp(hdr.data() + 0x10, "NRO0", 4) != 0) {
        if (out_err) *out_err = "Invalid NRO header (missing NRO0 magic).";
        return false;
    }
    // Verify OUN0 Switch 2 marker is present in the header/startup area.
    bool has_oun0 = false;
    for (std::size_t i = 0; i + 4 <= got; ++i) {
        if (std::memcmp(hdr.data() + i, "OUN0", 4) == 0) {
            has_oun0 = true;
            break;
        }
    }
    if (!has_oun0) {
        // Also check first 64 KiB in case MOD0/OUN0 sits after early stub.
        std::FILE* f2 = std::fopen(path.c_str(), "rb");
        if (f2) {
            std::string probe(65536, '\0');
            const std::size_t n = std::fread(probe.data(), 1, probe.size(), f2);
            std::fclose(f2);
            has_oun0 = (probe.substr(0, n).find("OUN0") != std::string::npos);
        }
    }
    if (!has_oun0) {
        if (out_err) *out_err = "Downloaded NRO is missing Switch 2 OUN0 header.";
        return false;
    }
    return true;
}

} // namespace

std::string Switch2ResolveSelfNroPath(const char* argv0) {
    if (argv0 != nullptr && argv0[0] != '\0') {
        std::string p(argv0);
        if (p.rfind("sdmc:", 0) == 0 && p.size() > 8) {
            return p;
        }
        if (p.front() == '/' && p.size() > 5) {
            return "sdmc:" + p;
        }
    }
    std::error_code ec;
    static constexpr const char* kCandidates[] = {
        "sdmc:/switch/eden-nx2/eden-switch2.nro",
        "sdmc:/switch/eden-nx2/Eden-NX2.nro",
        "sdmc:/switch/eden-switch2.nro",
    };
    for (const char* cand : kCandidates) {
        if (std::filesystem::exists(cand, ec)) {
            return cand;
        }
    }
    return "sdmc:/switch/eden-nx2/eden-switch2.nro";
}

Switch2UpdateInfo Switch2CheckForUpdate() {
    Switch2UpdateInfo info;
    info.current_version = EDEN_NX2_VERSION;

    ScopedSocketInit sock;
    if (!sock.initialized) {
        info.error_msg = "Network/socket init failed (check Wi-Fi).";
        return info;
    }
    ScopedCurlGlobal curl_global;
    if (!curl_global.ok) {
        info.error_msg = "libcurl initialization failed.";
        return info;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        info.error_msg = "curl_easy_init failed.";
        return info;
    }

    std::string body;
    char errbuf[CURL_ERROR_SIZE]{};
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");

    curl_easy_setopt(curl, CURLOPT_URL, kLatestReleaseApiUrl);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Eden-NX2/" EDEN_NX2_VERSION);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

    const CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        info.error_msg = std::string("Network error: ") +
                         (errbuf[0] ? errbuf : curl_easy_strerror(rc));
        return info;
    }
    if (http_code != 200) {
        info.error_msg = "GitHub HTTP " + std::to_string(http_code);
        return info;
    }

    if (!ExtractJsonString(body, "tag_name", 0, &info.tag_name)) {
        info.error_msg = "Invalid GitHub API response (missing tag_name).";
        return info;
    }
    ExtractJsonString(body, "name", 0, &info.release_name);
    info.latest_version = NormalizeVersion(info.tag_name);
    info.download_url = FindNroAssetUrl(body);
    if (info.download_url.empty()) {
        info.error_msg = "Release found (" + info.tag_name + "), but eden-switch2.nro asset missing.";
        return info;
    }

    info.ok = true;
    info.update_available = IsNewerVersion(info.latest_version, info.current_version);
    return info;
}

bool Switch2DownloadAndInstallUpdate(const std::string& download_url,
                                     const std::string& target_nro_path,
                                     const Switch2UpdateProgressFn& progress_cb,
                                     std::string* out_error) {
    if (download_url.empty() || target_nro_path.empty()) {
        if (out_error) *out_error = "Missing download URL or target path.";
        return false;
    }

    ScopedSocketInit sock;
    if (!sock.initialized) {
        if (out_error) *out_error = "Network/socket init failed (check Wi-Fi).";
        return false;
    }
    ScopedCurlGlobal curl_global;
    if (!curl_global.ok) {
        if (out_error) *out_error = "libcurl initialization failed.";
        return false;
    }

    std::error_code ec;
    const auto parent = std::filesystem::path(target_nro_path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }

    const std::string tmp_path = target_nro_path + ".tmp";
    std::remove(tmp_path.c_str());

    std::FILE* fp = std::fopen(tmp_path.c_str(), "wb");
    if (!fp) {
        if (out_error) *out_error = "Cannot write temporary file: " + tmp_path;
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(fp);
        std::remove(tmp_path.c_str());
        if (out_error) *out_error = "curl_easy_init failed.";
        return false;
    }

    char errbuf[CURL_ERROR_SIZE]{};
    DownloadProgressCtx prog_ctx{&progress_cb, false};

    curl_easy_setopt(curl, CURLOPT_URL, download_url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Eden-NX2/" EDEN_NX2_VERSION);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 25L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog_ctx);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

    const CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    std::fflush(fp);
    std::fclose(fp);

    if (prog_ctx.cancelled) {
        std::remove(tmp_path.c_str());
        if (out_error) *out_error = "Download cancelled.";
        return false;
    }
    if (rc != CURLE_OK) {
        std::remove(tmp_path.c_str());
        if (out_error) {
            *out_error = std::string("Download failed: ") +
                         (errbuf[0] ? errbuf : curl_easy_strerror(rc));
        }
        return false;
    }
    if (http_code != 200) {
        std::remove(tmp_path.c_str());
        if (out_error) *out_error = "Download HTTP " + std::to_string(http_code);
        return false;
    }

    if (!ValidateSwitch2NroFile(tmp_path, out_error)) {
        std::remove(tmp_path.c_str());
        return false;
    }

    // Horizon sdmc:/ rename() fails with EEXIST if target exists; remove first.
    std::remove(target_nro_path.c_str());
    if (std::rename(tmp_path.c_str(), target_nro_path.c_str()) != 0) {
        std::filesystem::copy_file(tmp_path, target_nro_path,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        std::remove(tmp_path.c_str());
        if (ec) {
            if (out_error) *out_error = "Failed to replace NRO on SD card.";
            return false;
        }
    }
    return true;
}

#endif // __SWITCH2__
