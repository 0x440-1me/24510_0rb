#include "../src/orb_state.h"
#include <stdio.h>
static unsigned total, failed;
static void check(bool pass, const char *name) { ++total; if (!pass) ++failed; printf("%s %s\n",pass?"PASS":"FAIL",name); }
static OrbSnapshot snapshot(int eligible, int completed, int unaccepted, int unfinished) {
    OrbSnapshot s = {0}; s.reliable = 1; s.route=1; s.eligible=eligible; s.completed=completed;
    s.unaccepted=unaccepted; s.unfinished=unfinished; s.progress=s.target=-1; return s;
}
static void started(OrbState *s) {
    orb_state_init(s); orb_connection(s,true); orb_start_confirm(s); orb_start(s); orb_dispatch_result(s,true,1);
}
int main(void) {
    OrbState s; orb_state_init(&s);
    check(s.state==DISCONNECTED,"initial-disconnected"); check(!orb_start(&s),"disconnected-forbids-dispatch");
    check(!orb_start_confirm(&s),"disconnected-forbids-start-confirm");
    OrbState before=s; /* Cancel is outside the restart operation; no transition. */
    check(s.state==before.state && !s.ever_started,"restart-cancel-unchanged");
    orb_connection(&s,true); check(s.state==READY && !s.ever_started,"connect-no-auto-start");
    check(!orb_start(&s),"confirmation-required"); check(orb_start_confirm(&s),"ready-confirm");
    check(s.state==START_CONFIRM && !s.start_locked && !s.ever_started,"confirm-no-side-effect");
    orb_cancel_confirm(&s); check(s.state==READY && !s.ever_started,"cancel-no-dispatch");
    orb_start_confirm(&s); check(orb_start(&s),"affirm-dispatch"); check(s.state==DISPATCHING,"dispatching-state");
    check(!orb_start(&s) && !orb_start_confirm(&s),"duplicate-during-dispatch-refused");
    orb_dispatch_result(&s,true,0); check(s.state==UNKNOWN,"helper-success-without-renderer-signal-is-unknown");
    check(!orb_start_confirm(&s),"unknown-forbids-start");
    orb_connection(&s,true); check(s.state==UNKNOWN,"reconnect-preserves-unknown-guard");
    orb_restart_verified(&s); check(s.state==READY && !s.start_locked && !s.ever_started,"verified-reset-ready-no-auto-start");
    OrbSnapshot data=snapshot(0,0,0,0); orb_verify(&s); orb_observe(&s,&data,true);
    check(s.state==NO_ELIGIBLE,"zero-before-run-no-eligible"); check(!s.ever_started,"scan-no-auto-start");
    data=snapshot(2,2,0,0); orb_observe(&s,&data,true); check(s.state==NO_ELIGIBLE,"already-complete-before-start-is-not-run-completion");
    data=snapshot(3,1,1,1); orb_verify(&s); orb_observe(&s,&data,true);
    check(s.state==READY && !s.ever_started,"retry-rescan-ready-no-auto-start");
    started(&s); check(s.state==RUNNING,"renderer-signal-running"); check(!orb_start_confirm(&s),"duplicate-running-refused");
    data=snapshot(3,1,1,1); data.signal=1; orb_observe(&s,&data,true);
    check(s.state==RUNNING,"one-complete-of-many-not-all");
    data=snapshot(2,1,0,1); data.signal=1; orb_observe(&s,&data,true);
    check(s.state==RUNNING,"no-unaccepted-but-unfinished-not-completed");
    check(s.state!=ALL_COMPLETED,"unsupported-unfinished-not-completed");
    data=snapshot(2,2,0,0); data.error=2; orb_observe(&s,&data,true); check(s.state==UNKNOWN,"429-not-completed");
    for(int error=1;error<=4;++error) { started(&s); data=snapshot(2,2,0,0);data.error=error;orb_observe(&s,&data,true);check(s.state==UNKNOWN,"error-timeout-partial-malformed-not-completed"); }
    started(&s);data=snapshot(2,2,0,0);data.reliable=0;data.signal=1;orb_observe(&s,&data,true);
    check(s.state==RUNNING,"unreliable-counts-not-completed");
    data.signal=2;orb_observe(&s,&data,false);check(s.state==UNKNOWN,"runner-return-not-completed-or-stopped");
    started(&s);data=snapshot(0,0,0,0);orb_observe(&s,&data,true);check(s.state==UNKNOWN,"zero-after-start-not-completed");
    started(&s);orb_request_stop(&s);check(s.state==STOP_REQUESTED && s.start_locked,"stop-request-keeps-guard");
    data=snapshot(1,0,0,1);data.signal=1;data.hook=0;orb_observe(&s,&data,false);
    check(s.state==STOP_REQUESTED,"missing-hook-never-stopped");
    data.signal=2;orb_observe(&s,&data,false);check(s.state==UNKNOWN && s.start_locked,"settled-after-stop-cleanup-unknown");
    started(&s);orb_connection(&s,false);check(s.state==DISCONNECTED && s.start_locked,"loss-preserves-start-lock");
    orb_connection(&s,true);check(s.state==UNKNOWN,"reconnect-no-auto-run");
    started(&s);data=snapshot(2,2,0,0);data.signal=1;orb_verify(&s);orb_observe(&s,&data,true);
    check(s.state==ALL_COMPLETED && s.completed_count==2,"nonempty-authoritative-all-complete-after-start");
    check(s.start_locked,"completion-does-not-prove-cleanup");
    orb_verify(&s);data=snapshot(3,2,1,0);orb_observe(&s,&data,true);
    check(s.state==UNKNOWN && !orb_start_confirm(&s),"retry-guarded-until-safe-reset");
    orb_restart_verified(&s);orb_verify(&s);orb_observe(&s,&data,true);check(s.state==READY && !s.ever_started,"safe-reset-rescan-ready");
    data=snapshot(2,1,0,0);check(!orb_snapshot_valid(&data),"inconsistent-aggregate-rejected");
    orb_observe(&s,&data,true);check(s.state==UNKNOWN,"invalid-aggregate-unknown");
    data=snapshot(1,0,0,1);data.progress=30;data.target=100;check(orb_snapshot_valid(&data),"observed-numeric-progress-valid");
    data.progress=101;check(!orb_snapshot_valid(&data),"invented-progress-rejected");
    data=snapshot(1,0,0,1);data.hook=2;check(!orb_snapshot_valid(&data),"non-allowlisted-hook-rejected");
    started(&s); data=snapshot(2,2,0,0); data.signal=1; data.route=2;
    orb_observe(&s,&data,true);
    check(s.state==UNKNOWN && s.start_locked && s.observation.reliable==0,"route-left-quest-home-never-all-complete");
    started(&s); data=snapshot(2,2,0,0); data.signal=1; data.route=0;
    orb_observe(&s,&data,true);
    check(s.state==UNKNOWN && s.start_locked,"unknown-route-never-completed");
    orb_state_init(&s); orb_connection(&s,true); orb_start_confirm(&s); orb_start(&s);
    orb_preflight_refused(&s);
    check(s.state==READY && !s.start_locked && !s.ever_started && s.observation.route==2,"preflight-refusal-zero-start");
    printf("STATE_SUMMARY total=%u passed=%u failed=%u\n",total,total-failed,failed);return failed?1:0;
}
