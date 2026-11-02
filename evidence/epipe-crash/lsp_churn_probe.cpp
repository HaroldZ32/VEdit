// T134: product-level reproduction of the "Broken pipe" crash.
//
// Drives the REAL C ABI LSP session against a language server that exits
// immediately, so the worker's first JSON-RPC write hits a dead stdin pipe.
//   --ignore-sigpipe : what vedit-app does in main() (the fix)
//   default          : the old behaviour (SIGPIPE terminates the process)
#include <vd_ffi.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <unistd.h>

int main(int argc, char **argv)
{
    const bool ignore = argc > 1 && std::strcmp(argv[1], "--ignore-sigpipe") == 0;
    if (ignore) {
        std::signal(SIGPIPE, SIG_IGN);
    }
    const char *ls = "/tmp/t134-exiting-ls.sh";
    const char *root = "/vedit-build/tests/fixtures/verilog-project";
    char *files[] = {const_cast<char *>("/vedit-build/tests/fixtures/verilog-project/rtl/top.sv")};
    VdStringList list{1, files};

    VdService *service = vd_service_create();
    if (service == nullptr) {
        std::printf("LSP-CHURN no-service\n");
        return 2;
    }
    int started = 0;
    int rejected = 0;
    for (int iteration = 0; iteration < 20; ++iteration) {
        const uint64_t session = vd_lsp_session_start(service, ls, root, nullptr, &list);
        if (session == 0) {
            ++rejected;
            continue;
        }
        ++started;
        // The fake language server exits immediately. Give it time to be gone so
        // the request below is a write to a pipe whose reader has closed — the
        // exact "Broken pipe" condition (without this delay the first write may
        // still fit the pipe buffer and the reverse control would be flaky).
        ::usleep(150000);
        const uint64_t request = vd_lsp_session_request(service, session, VD_LSP_REQ_SYMBOLS,
                                                        files[0], 0, 0, nullptr);
        if (request != 0) {
            for (int poll = 0; poll < 10; ++poll) {
                VdLspResponse response{};
                const int32_t status = vd_lsp_request_poll(service, session, request, &response);
                vd_lsp_response_free(&response);
                if (status != VD_STATUS_PENDING) {
                    break;
                }
                ::usleep(2000);
            }
        }
        vd_lsp_session_free(service, session);
        std::printf("LSP-CHURN iteration %d session ok\n", iteration);
        std::fflush(stdout);
    }
    vd_service_destroy(service);
    std::printf("LSP-CHURN done started=%d rejected=%d ignore_sigpipe=%d\n", started, rejected,
                ignore ? 1 : 0);
    return 0;
}
