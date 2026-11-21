#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <ctime>
#include <sys/stat.h>
namespace storage_status {
    constexpr const char *StatusPath = "sdmc:/atmosphere/logs/usb_storage_status.txt";
    constexpr const char *RouteCommandPath = "sdmc:/atmosphere/logs/usb_content_route.txt";
    constexpr const char *RouteCommandTempPath = "sdmc:/atmosphere/logs/usb_content_route.txt.tmp";
    constexpr size_t StatusLineSize = 256;

    struct StatusData {
        bool enabled{false};
        bool has_enabled{false};
        bool install_route{false};
        bool has_install_route{false};
        bool has_state{false};
        bool has_ready{false};
        bool ready{false};
        bool writable{false};
        bool running{false};
        bool mounted{false};
        bool registered{false};
        bool has_free_space{false};
        bool has_total_space{false};
        unsigned long long free_space{0};
        unsigned long long total_space{0};
        char state[32]{"Waiting for sysmodule"};
        char filesystem[16]{"unknown"};
        char partition_name[37]{};
        char vendor[9]{};
        char product[17]{};
        char revision[5]{};
        unsigned long long media_generation{0};
        std::time_t modified{};
    };

    void CopyText(char *dst, size_t dst_size, const char *src) {
        if (dst_size == 0) {
            return;
        }
        std::strncpy(dst, src != nullptr ? src : "", dst_size - 1);
        dst[dst_size - 1] = '\0';
    }

    unsigned long long ParseNumber(const char *value) {
        return value != nullptr ? std::strtoull(value, nullptr, 0) : 0;
    }

    void ParseLine(StatusData *status, char *line) {
        char *separator = std::strchr(line, '=');
        if (separator == nullptr) {
            return;
        }
        *separator = '\0';
        char *value = separator + 1;
        value[std::strcspn(value, "\r\n")] = '\0';

        if (std::strcmp(line, "enabled") == 0) {
            status->enabled = ParseNumber(value) != 0;
            status->has_enabled = true;
        } else if (std::strcmp(line, "install_route") == 0) {
            status->install_route = ParseNumber(value) != 0;
            status->has_install_route = true;
        } else if (std::strcmp(line, "state") == 0) {
            CopyText(status->state, sizeof(status->state), value);
            status->has_state = true;
        } else if (std::strcmp(line, "ready") == 0) {
            status->ready = ParseNumber(value) != 0;
            status->has_ready = true;
        } else if (std::strcmp(line, "writable") == 0) {
            status->writable = ParseNumber(value) != 0;
        } else if (std::strcmp(line, "running") == 0) {
            status->running = ParseNumber(value) != 0;
        } else if (std::strcmp(line, "mounted") == 0) {
            status->mounted = ParseNumber(value) != 0;
        } else if (std::strcmp(line, "registered") == 0) {
            status->registered = ParseNumber(value) != 0;
        } else if (std::strcmp(line, "free_space") == 0) {
            status->free_space = ParseNumber(value);
            status->has_free_space = true;
        } else if (std::strcmp(line, "total_space") == 0) {
            status->total_space = ParseNumber(value);
            status->has_total_space = true;
        } else if (std::strcmp(line, "media_generation") == 0) {
            status->media_generation = ParseNumber(value);
        } else if (std::strcmp(line, "filesystem") == 0) {
            CopyText(status->filesystem, sizeof(status->filesystem), value);
        } else if (std::strcmp(line, "partition_name") == 0) {
            CopyText(status->partition_name, sizeof(status->partition_name), value);
        } else if (std::strcmp(line, "vendor") == 0) {
            CopyText(status->vendor, sizeof(status->vendor), value);
        } else if (std::strcmp(line, "product") == 0) {
            CopyText(status->product, sizeof(status->product), value);
        } else if (std::strcmp(line, "revision") == 0) {
            CopyText(status->revision, sizeof(status->revision), value);
        }
    }

    bool ReadStatus(StatusData *out) {
        FILE *file = std::fopen(StatusPath, "rb");
        if (file == nullptr) {
            return false;
        }

        StatusData next{};
        char line[StatusLineSize]{};
        while (std::fgets(line, sizeof(line), file) != nullptr) {
            ParseLine(&next, line);
        }
        const bool read_ok = !std::ferror(file);
        std::fclose(file);

        if (!read_ok || !next.has_state || !next.has_ready) {
            return false;
        }
        struct stat metadata{};
        if (stat(StatusPath, &metadata) == 0) next.modified = metadata.st_mtime;
        *out = next;
        return true;
    }

    void FormatBytes(char *out, size_t out_size, unsigned long long bytes) {
        if (bytes >= 1ULL << 40) {
            std::snprintf(out, out_size, "%.1f TB", double(bytes) / (1ULL << 40));
        } else if (bytes >= 1ULL << 30) {
            std::snprintf(out, out_size, "%.1f GB", double(bytes) / (1ULL << 30));
        } else if (bytes >= 1ULL << 20) {
            std::snprintf(out, out_size, "%.1f MB", double(bytes) / (1ULL << 20));
        } else if (bytes >= 1ULL << 10) {
            std::snprintf(out, out_size, "%.0f KB", double(bytes) / (1ULL << 10));
        } else {
            std::snprintf(out, out_size, "%llu B", bytes);
        }
    }

    bool IsRecent(const StatusData &status) {
        const auto now = std::time(nullptr);
        // This is display freshness only. Eject uses its separate boot-bound
        // protocol and never treats a file timestamp as proof of safe removal.
        return status.modified > 0 && now >= status.modified - 2 && now - status.modified <= 8;
    }

    std::string DeviceName(const StatusData &status) {
        if (status.partition_name[0] != '\0') {
            return status.partition_name;
        }
        if (status.vendor[0] != '\0' || status.product[0] != '\0') {
            return std::string(status.vendor) + " " + status.product;
        }
        return "USB drive";
    }

}
