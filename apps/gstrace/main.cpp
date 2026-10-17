#include "debugger/trace.hpp"
#include <exception>

namespace {
int usage() {
    std::fprintf(stderr,"usage: gstrace [-l labelfile] 6502|65c02|65816 tracefilename.bin\n");
    return 1;
}
}

int main(int argc, char **argv) {
    if (argc < 3) return usage();
    processor_type cputype = PROCESSOR_6502;
    bool have_cpu = false;
    const char *label_filename = nullptr;
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "-l") == 0) {
            if (label_filename || i + 1 >= argc - 1) return usage();
            label_filename = argv[++i];
        } else {
            if (have_cpu) return usage();
            if (std::strcmp(argv[i], "6502") == 0) cputype = PROCESSOR_6502;
            else if (std::strcmp(argv[i], "65c02") == 0) cputype = PROCESSOR_65C02;
            else if (std::strcmp(argv[i], "65816") == 0) cputype = PROCESSOR_65816;
            else return usage();
            have_cpu = true;
        }
    }
    if (!have_cpu) return usage();
    try {
        system_trace_buffer trace_buffer(100000, cputype);
        if (label_filename && !trace_buffer.load_labels_from_file(label_filename))
            throw a2artifact::Error("labels", label_filename, 0);
        trace_buffer.read_from_file(argv[argc - 1]);
        for (size_t i = 0; i < trace_buffer.count; ++i) {
            const char *line = trace_buffer.decode_trace_entry(trace_buffer.get_entry(i));
            if (std::fputs(line, stdout) == EOF || std::fputc('\n', stdout) == EOF)
                throw a2artifact::Error("write", "stdout");
        }
        // This standalone process owns finalizing stdout, including an empty
        // decode. Refuse failed redirected/pipe output instead of a zero exit.
        FILE *output = stdout;
        a2artifact::finish(output, "stdout");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr,"gstrace: status=FAIL %s\n",error.what());
        return 2;
    }
}
