#ifndef ORB_STATE_H
#define ORB_STATE_H
#include <stdbool.h>
#include <stddef.h>
#ifdef ERROR
#undef ERROR /* Win32 GDI defines ERROR; use the required UI state spelling. */
#endif
/* Single authoritative UI state; helper results are observations only. */
typedef enum { DISCONNECTED, RESTARTING, READY, START_CONFIRM, DISPATCHING,
               RUNNING, STOP_REQUESTED, VERIFYING, ALL_COMPLETED, NO_ELIGIBLE,
               UNKNOWN, ERROR } OrbUiState;
typedef struct {
    int signal, hook, active, reliable, error;
    int eligible, completed, unaccepted, unfinished;
    int progress, target, activity, route; /* 0 unknown, 1 Quest Home ready, 2 other route. */
} OrbSnapshot;
typedef struct {
    OrbUiState state, before_verify;
    bool connected, ever_started, start_locked;
    unsigned completed_count;
    OrbSnapshot observation;
} OrbState;
void orb_state_init(OrbState *);
void orb_connection(OrbState *, bool connected);
bool orb_start_confirm(OrbState *);
void orb_cancel_confirm(OrbState *);
bool orb_start(OrbState *); /* Call only after byte-bound consent. */
void orb_dispatch_result(OrbState *, bool acknowledged, int signal);
void orb_preflight_refused(OrbState *); /* Trusted helper proves ZERO dispatch. */
void orb_request_stop(OrbState *);
void orb_verify(OrbState *);
void orb_observe(OrbState *, const OrbSnapshot *, bool scan_requested);
void orb_restart_verified(OrbState *);
bool orb_snapshot_valid(const OrbSnapshot *);
#endif
