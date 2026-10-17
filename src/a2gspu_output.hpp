// Fork-owned byte sinks. Explicit bindings only; failures terminate the run.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include "a2gspu_outputs.hpp"
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
struct A2ByteSink;
inline std::vector<A2ByteSink *> a2_byte_sinks;
inline void a2_close_byte_sinks();
#ifdef _WIN32
using A2PipeHandle=HANDLE;
inline bool a2_close_pipe(A2PipeHandle pipe){return CloseHandle(pipe)!=0;}
#else
using A2PipeHandle=int;
inline bool a2_close_pipe(A2PipeHandle pipe){return ::close(pipe)==0;}
#endif
// Native contract-test injection only; production uses these defaults.
struct A2ByteCloseOps {
    a2artifact::WriteOps stdio{};
    bool (*pipe_close)(A2PipeHandle)=a2_close_pipe;
};

struct A2ByteSink {
    A2ByteSink()=default;
    A2ByteSink(const A2ByteSink&)=delete;
    A2ByteSink& operator=(const A2ByteSink&)=delete;
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
        a2_byte_sinks.push_back(this);
        a2outputs::add("byte-captures",a2_close_byte_sinks);
        std::fprintf(stderr, "A2GSPU %s: armed binding=%s resolved=%s\n", name, spec, path.c_str());
    }
    void open(const char *name,const char *spec) {
        try {close();open_checked(name,spec);}
        catch(const a2artifact::Error &error){a2artifact::fatal(error);}
        catch(const std::exception &error){fail(error.what());}
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
    void close(A2ByteCloseOps ops={}) {
        // Consume ownership and reservations before any operation can fail.
        // A failed close is not retryable against an already consumed handle.
        FILE *owned_file=file;file=nullptr;
        const auto owned_pipe=pipe;
        const bool was_enabled=enabled;
        const auto closed_bytes=bytes;
        const std::string closed_binding=binding;
        const std::string closed_label=label;
#ifdef _WIN32
        pipe=INVALID_HANDLE_VALUE;
#else
        pipe=-1;
#endif
        if (reserved) a2_sink_paths.erase(resolved);
        a2_byte_sinks.erase(std::remove(a2_byte_sinks.begin(),a2_byte_sinks.end(),this),a2_byte_sinks.end());
        reserved=false;
        resolved.clear(); binding.clear(); file=nullptr; enabled=false; bytes=0;
        std::string failures;
        try {a2artifact::finish(owned_file,closed_binding.c_str(),ops.stdio);}
        catch(const a2artifact::Error &error){failures=error.what();}
#ifdef _WIN32
        if(owned_pipe!=INVALID_HANDLE_VALUE && !ops.pipe_close(owned_pipe)) {
            if(!failures.empty())failures+="; ";
            failures+="pipe-close win32-error="+std::to_string(GetLastError());
        }
#else
        if(owned_pipe>=0 && !ops.pipe_close(owned_pipe)) {
            if(!failures.empty())failures+="; ";
            failures+=a2artifact::Error("pipe-close",closed_binding.c_str()).what();
        }
#endif
        if(!failures.empty()) {
            std::fprintf(stderr,"A2GSPU %s: FAIL capture-finalization bytes=%llu binding=%s %s\n",closed_label.c_str(),closed_bytes,closed_binding.c_str(),failures.c_str());
            throw a2artifact::Error("capture-finalization",(closed_label+" "+closed_binding+" "+failures).c_str(),0);
        }
        if(was_enabled)std::fprintf(stderr,"A2GSPU %s: closed bytes=%llu binding=%s\n",closed_label.c_str(),closed_bytes,closed_binding.c_str());
    }
    ~A2ByteSink() {try {close();}catch(const a2artifact::Error &error){a2artifact::fatal_on_exit(error);}}
};

inline void a2_close_byte_sinks() {
    std::string failures;
    while(!a2_byte_sinks.empty()) {
        try {a2_byte_sinks.back()->close();}
        catch(const a2artifact::Error &error){if(!failures.empty())failures+="; ";failures+=error.what();}
    }
    if(!failures.empty())throw a2artifact::Error("byte-capture-finalization",failures.c_str(),0);
}
