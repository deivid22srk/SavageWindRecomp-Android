// Small services: parental control, add-on content, and other mostly-stubbed interfaces.
#include <string.h>

#include "../hle.h"
#include "../ipc.h"

static void ok_logger(Service *s, IpcReq *req, IpcResp *resp) {
    LOG("[svc] %s cmd %u (ok)\n", s->name, req->cmd);
    resp_size(resp, 0x10);
}

static uint32_t new_event_handle(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

// ------------------------------------------------------------------ pctl
static void pctl_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 1031: case 1035: case 1018: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;  // restrictions off
    case 1032: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;
    default: LOG("[pctl] IParentalControlService cmd %u (ok)\n", req->cmd); resp_size(resp, 0x10);
    }
}

static void pctl_root(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 0 || req->cmd == 1)
        ipc_return_object(resp, service_new("IParentalControlService", pctl_service, NULL));
}
SERVICE("pctl", pctl) { pctl_root(s, req, resp); }
SERVICE("pctl:s", pctl_s) { pctl_root(s, req, resp); }
SERVICE("pctl:a", pctl_a) { pctl_root(s, req, resp); }
SERVICE("pctl:r", pctl_r) { pctl_root(s, req, resp); }

// ------------------------------------------------------------------ aoc:u (no DLC)
SERVICE("aoc:u", aoc_u) {
    (void)s;
    switch (req->cmd) {
    case 2: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;   // CountAddOnContent
    case 3: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;   // ListAddOnContent
    case 5: OUT(uint64_t, 0) = (g_program_id & ~0xFFFull) + 0x1000; resp_size(resp, 8); break;  // GetAddOnContentBaseId
    case 8: resp->copy_handles[resp->ncopy++] = new_event_handle(); break;
    default: ok_logger(s, req, resp);
    }
}

// ------------------------------------------------------------------ nfp:user (amiibo: no NFC reader)
// IUserManager hands out an IUser; the IUser reports no devices, so games see amiibo as unavailable instead of
// crashing on a missing object (BOTW 1.8.2 calls nn::nfp::Initialize at start-up).
static void nfp_user(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: case 1: break;                                                // Initialize / Finalize
    case 2: OUT(int32_t, 0) = 0; resp_size(resp, 4); break;               // ListDevices: none
    case 17: case 18: case 23:                                            // Attach{Activate,Deactivate,Availability}Event
        resp->copy_handles[resp->ncopy++] = new_event_handle();
        break;
    case 19: OUT(uint32_t, 0) = 1; resp_size(resp, 4); break;            // GetState: initialized
    case 20: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;            // GetDeviceState: none
    default: LOG("[nfp] IUser cmd %u (ok)\n", req->cmd); resp_size(resp, 0x10);
    }
}

SERVICE("nfp:user", nfp_user_manager) {
    (void)s;
    if (req->cmd == 0) ipc_return_object(resp, service_new("IUser(nfp)", nfp_user, NULL));  // CreateUserInterface
    else LOG("[nfp] IUserManager cmd %u (ok)\n", req->cmd);
}
