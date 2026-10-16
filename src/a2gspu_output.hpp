// Fork-owned byte sinks. Explicit bindings only; failures terminate the run.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <filesystem>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#endif

inline std::set<std::string> a2_sink_paths;

struct A2ByteSink {
    FILE *file = nullptr;
#ifdef _WIN32
    HANDLE pipe = INVALID_HANDLE_VALUE;
#else
    int pipe = -1;
#endif
    std::string label, binding;
    std::string resolved;
    unsigned long long bytes = 0;
    bool enabled = false;
    bool reserved = false;
    [[noreturn]] void fail(const char *why) const {
        std::fprintf(stderr, "A2GSPU %s: FAIL %s binding=%s\n", label.c_str(), why, binding.c_str());
        std::exit(2);
    }
    void open_checked(const char *name, const char *spec) {
        if(enabled) throw std::runtime_error("capture already armed; turn it off before rebinding");
        label = name;
        if (!spec) return;
        binding = spec;
        bool is_file = !std::strncmp(spec, "file:", 5);
        bool is_pipe = !std::strncmp(spec, "pipe:", 5);
        if ((!is_file && !is_pipe) || !spec[5]) throw std::runtime_error("expected file:<path> or pipe:<existing-endpoint>");
        std::string path = spec + 5;
        const char *out = std::getenv("A2GSPU_OUT_DIR");
        if (is_file && out && *out && path[0] != '/' && path[0] != '\\' &&
            !(path.size() > 1 && path[1] == ':')) path = std::string(out) + "/" + path;
        std::error_code error;
        // Resolve the absolute path before weak canonicalisation: for a file
        // that does not exist yet, weakly_canonical alone can stay relative.
        auto absolute = is_file ? std::filesystem::absolute(path,error) : std::filesystem::path(path);
        if (error) throw std::runtime_error("cannot resolve capture path");
        resolved = is_file ? std::filesystem::weakly_canonical(absolute,error).generic_string() : path;
        if (error) throw std::runtime_error("cannot resolve capture path");
#ifdef _WIN32
        // Windows paths are case-insensitive; aliases must not truncate one
        // another's captures. Pipe names share the same restriction.
        for (char &c : resolved) if (c >= 'A' && c <= 'Z') c += 'a'-'A';
#endif
        if (a2_sink_paths.count(resolved)) throw std::runtime_error("capture endpoint already bound");
        if (is_file) {
            if(std::filesystem::exists(path,error) && !std::filesystem::is_regular_file(path,error))
                throw std::runtime_error("file capture requires an ordinary file");
            if(error)throw std::runtime_error("cannot inspect capture endpoint");
            for(const auto &bound:a2_sink_paths) {
                std::error_code same_error;
                if(std::filesystem::equivalent(path,bound,same_error))throw std::runtime_error("capture endpoint alias already bound");
            }
            file = std::fopen(path.c_str(), "wb");
            if (!file) throw std::runtime_error("cannot open capture file");
        } else {
#ifdef _WIN32
            pipe = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe == INVALID_HANDLE_VALUE || GetFileType(pipe) != FILE_TYPE_PIPE) {close();throw std::runtime_error("cannot open existing named pipe");}
#else
            pipe = ::open(path.c_str(), O_WRONLY | O_NONBLOCK);
            struct stat st{};
            if (pipe < 0 || fstat(pipe, &st) || !S_ISFIFO(st.st_mode)) {close();throw std::runtime_error("cannot open existing FIFO with reader");}
            ::signal(SIGPIPE,SIG_IGN); // report disconnect as a failing write
#endif
        }
        a2_sink_paths.insert(resolved);reserved=true;enabled = true;
        std::fprintf(stderr, "A2GSPU %s: armed binding=%s resolved=%s\n", name, spec, path.c_str());
    }
    void open(const char *name,const char *spec) {
        close();try {open_checked(name,spec);}catch(const std::exception &e){fail(e.what());}
    }
    void write(const unsigned char *data, size_t n) {
        if (!enabled) return;
        if (file) {
            if (std::fwrite(data, 1, n, file) != n || std::fflush(file)) fail("capture write failed");
        } else {
#ifdef _WIN32
            OVERLAPPED ov{};
            ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
            if (!ov.hEvent) fail("pipe event failed");
            DWORD done = 0;
            BOOL ok = WriteFile(pipe, data, (DWORD)n, &done, &ov);
            if (!ok && GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(ov.hEvent, 100) == WAIT_OBJECT_0)
                    ok = GetOverlappedResult(pipe, &ov, &done, FALSE);
                else {
                    CancelIoEx(pipe, &ov);
                    GetOverlappedResult(pipe, &ov, &done, TRUE);
                    CloseHandle(ov.hEvent);
                    fail("pipe backpressure (100 ms deadline)");
                }
            }
            CloseHandle(ov.hEvent);
            if (!ok || done != n) fail("pipe disconnected or short write");
#else
            if (::write(pipe, data, n) != (ssize_t)n) fail("pipe disconnected or backpressure");
#endif
        }
        bytes += n;
    }
    void byte(unsigned char b) { write(&b, 1); }
    void close() {
        if (file) std::fclose(file);
#ifdef _WIN32
        if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
#else
        if (pipe >= 0) ::close(pipe);
#endif
        if (enabled) std::fprintf(stderr, "A2GSPU %s: closed bytes=%llu binding=%s\n", label.c_str(), bytes, binding.c_str());
        if (reserved) a2_sink_paths.erase(resolved);
        reserved=false;
        resolved.clear(); binding.clear(); file=nullptr; enabled=false; bytes=0;
#ifdef _WIN32
        pipe=INVALID_HANDLE_VALUE;
#else
        pipe=-1;
#endif
    }
    ~A2ByteSink() { close(); }
};
