#include "orb_state.h"
#include <string.h>
void orb_state_init(OrbState *s) {
    memset(s, 0, sizeof(*s)); s->state = DISCONNECTED;
    s->observation.progress = s->observation.target = -1;
}
void orb_connection(OrbState *s, bool connected) {
    s->connected = connected;
    s->state = !connected ? DISCONNECTED : s->start_locked ? UNKNOWN : READY;
}
bool orb_start_confirm(OrbState *s) {
    if (!s->connected || s->start_locked || s->state != READY) return false;
    s->state = START_CONFIRM; return true;
}
void orb_cancel_confirm(OrbState *s) { if (s->state == START_CONFIRM) s->state = READY; }
bool orb_start(OrbState *s) {
    if (!s->connected || s->start_locked || s->state != START_CONFIRM) return false;
    s->ever_started = s->start_locked = true; s->state = DISPATCHING;
    s->observation.progress = s->observation.target = -1; return true;
}
void orb_dispatch_result(OrbState *s, bool acknowledged, int signal) {
    if (s->state != DISPATCHING) return;
    s->state = acknowledged && signal == 1 ? RUNNING : UNKNOWN;
}
void orb_preflight_refused(OrbState *s) {
    if (s->state != DISPATCHING) return;
    s->ever_started = s->start_locked = false; s->state = READY;
    memset(&s->observation, 0, sizeof(s->observation));
    s->observation.route = 2; s->observation.progress = s->observation.target = -1;
}
void orb_request_stop(OrbState *s) { if (s->start_locked) s->state = STOP_REQUESTED; }
void orb_verify(OrbState *s) { s->before_verify = s->state; s->state = VERIFYING; }
bool orb_snapshot_valid(const OrbSnapshot *s) {
    if (!s || s->signal < 0 || s->signal > 3 || s->hook < 0 || s->hook > 1 || s->active < 0 || s->active > 1 ||
        s->reliable < 0 || s->reliable > 1 || s->error < 0 || s->error > 4 || s->activity < 0 || s->activity > 4 || s->route < 0 || s->route > 2 ||
        s->eligible < 0 || s->eligible > 10000 || s->completed < 0 || s->completed > s->eligible ||
        s->unaccepted < 0 || s->unfinished < 0 || s->unaccepted > s->eligible || s->unfinished > s->eligible ||
        s->completed + s->unaccepted + s->unfinished != s->eligible) return false;
    return (s->progress == -1 && s->target == -1) ||
           (s->target > 0 && s->target <= 100000000 && s->progress >= 0 && s->progress <= s->target);
}
void orb_observe(OrbState *s, const OrbSnapshot *snapshot, bool scan) {
    OrbUiState previous = s->state == VERIFYING ? s->before_verify : s->state;
    if (!orb_snapshot_valid(snapshot)) { s->state = UNKNOWN; s->start_locked = true; return; }
    s->observation = *snapshot;
    if (s->ever_started && (snapshot->route != 1 || !snapshot->signal)) {
        s->state = UNKNOWN; s->start_locked = true;
        s->observation.reliable = 0; s->observation.progress = s->observation.target = -1;
        s->observation.activity = 0; return;
    }
    if (snapshot->signal == 1 || snapshot->active) s->start_locked = true;
    if (snapshot->error) { s->state = UNKNOWN; s->start_locked = true; return; }
    if (scan && snapshot->reliable) {
        if (snapshot->eligible && s->ever_started && snapshot->completed == snapshot->eligible &&
            !snapshot->unaccepted && !snapshot->unfinished) {
            s->state = ALL_COMPLETED; s->completed_count = (unsigned)snapshot->completed; return;
        }
        if (!s->ever_started && !snapshot->unaccepted && !snapshot->unfinished) { s->state = NO_ELIGIBLE; return; }
        if (!s->start_locked && (snapshot->unaccepted || snapshot->unfinished)) { s->state = READY; return; }
        if (!snapshot->eligible) { s->state = UNKNOWN; s->start_locked = true; return; }
    }
    if (s->start_locked) s->state = snapshot->signal == 1 ? (previous == STOP_REQUESTED ? STOP_REQUESTED : RUNNING) : UNKNOWN;
    else if (snapshot->hook) { s->start_locked = true; s->state = UNKNOWN; }
    else if (scan && !snapshot->reliable) { s->state = UNKNOWN; s->start_locked = true; }
    else s->state = READY;
}
void orb_restart_verified(OrbState *s) { orb_state_init(s); s->connected = true; s->state = READY; }
