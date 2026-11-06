// T11 evidence tool: drive the shipped project-wide lint ABI (engine=1,
// verible-verilog-lint) on ONE file and print every diagnostic it collected, in
// a shape that can sit next to the real CLI's text output.
//
// This is evidence tooling, not product code and not a test: it links the same
// `libvd_ffi.a` the product does and is the detail-level companion of the
// `PROJECT-PASS` line in w5-probe.
//
//   t11_batch_dump <verible-verilog-lint> <file> [rules-file|-]
//
// Columns are printed in the ABI's own units (0-based line, 0-based UTF-16
// column); the CLI prints 1-based byte columns. The comparison log states which
// unit each number is in instead of pretending they are the same field.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "vd_ffi.h"

namespace {

std::string owned(const char *text) { return text != nullptr ? std::string(text) : std::string(); }

const char *severity_name(int32_t severity) {
    switch (severity) {
        case 0:
            return "error";
        case 1:
            return "warning";
        case 2:
            return "note";
        case 3:
            return "ignored";
        default:
            return "unknown";
    }
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::printf("usage: t11_batch_dump <verible-verilog-lint> <file> [rules-file|-]\n");
        return 2;
    }
    const char *verible = argv[1];
    const char *file = argv[2];
    const char *rules = (argc > 3 && std::strcmp(argv[3], "-") != 0) ? argv[3] : nullptr;

    struct VdService *service = vd_service_create();
    if (service == nullptr) {
        std::printf("BATCH-ERROR service-create\n");
        return 2;
    }

    char *files[1] = {const_cast<char *>(file)};
    struct VdStringList list {};
    list.count = 1;
    list.items = files;
    struct VdLintBatchOptions options {};
    options.engine = 1;
    options.verible_binary = verible;
    options.rules_path = rules;

    const uint64_t job = vd_deeplint_batch_start(service, &list, &options);
    if (job == 0) {
        std::printf("BATCH-ERROR start rejected (engine/argv/empty file list)\n");
        vd_service_destroy(service);
        return 2;
    }

    std::string tool;
    std::string message;
    std::string stage;
    int32_t status = VD_STATUS_PENDING;
    for (int attempt = 0; attempt < 3000; ++attempt) {
        struct VdLintProgress progress {};
        status = vd_deeplint_batch_progress(service, job, &progress);
        tool = owned(progress.tool);
        message = owned(progress.message);
        stage = owned(progress.stage);
        const int32_t phase = progress.phase;
        vd_lint_progress_free(&progress);
        if (status != VD_STATUS_PENDING || phase >= 2) {
            break;
        }
        usleep(10 * 1000);
    }
    std::printf("BATCH status=%d phase-terminal tool='%s' stage='%s' message='%s'\n", status,
                tool.c_str(), stage.c_str(), message.c_str());

    struct VdDiagnostics diagnostics {};
    const int32_t polled = vd_deeplint_batch_poll(service, job, &diagnostics);
    std::printf("BATCH-POLL status=%d count=%zu\n", polled, diagnostics.count);
    for (size_t index = 0; index < diagnostics.count; ++index) {
        const struct VdDiagnostic &item = diagnostics.items[index];
        std::printf(
            "BATCH-DIAG file='%s' line=%u column=%u end_line=%u end_column=%u severity=%s "
            "code='%s' message='%s'\n",
            owned(item.file).c_str(), item.line, item.column, item.end_line, item.end_column,
            severity_name(item.severity), owned(item.code).c_str(), owned(item.message).c_str());
    }
    vd_diagnostics_free(&diagnostics);
    vd_deeplint_batch_release(service, job);
    vd_service_destroy(service);
    return status == VD_STATUS_FAILED ? 1 : 0;
}
