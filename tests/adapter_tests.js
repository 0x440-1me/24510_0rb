/* Executed in an isolated offline browser profile; never loads original JS. */
(async () => {
    const summary = []; let calls = 0;
    const test = (ok, name) => summary.push({name, passed: !!ok});
    const future = new Date(Date.now() + 86400000).toISOString();
    const past = new Date(Date.now() - 86400000).toISOString();
    const quest = (id, status, task = 'WATCH_VIDEO') => ({
        id: `SYNTHETIC_SECRET_${id}`, config: {expires_at: future, task_config_v2: {tasks: {[task]: {target: 100}}}},
        user_status: status, private_metadata: 'SYNTHETIC_SECRET_PRIVATE'
    });
    const accepted = {enrolled_at: past, progress: {WATCH_VIDEO: {value: 30}}};
    const completed = {enrolled_at: past, completed_at: past};
    function stub(response) {
        calls = 0; window.__24510Orb = {phase: 1}; window.stopQuestProgram = () => {};
        window.__questRunnerActive = true; window.__questRunnerRetryAfterMs = 0;
        window.webpackChunkdiscord_app = [];
        window.webpackChunkdiscord_app.push = function(chunk) {
            Array.prototype.push.call(this, chunk);
            chunk[2]({c: {fixture: {exports: {Bo: {
                get: async options => { calls++; test(options.url === '/quests/@me', 'only-read-only-quest-endpoint'); return response; },
                post: () => { throw new Error('Writes forbidden'); }
            }}}}});
            return this.length;
        };
    }
    function clean(result, name) {
        test(!JSON.stringify(result).includes('SYNTHETIC_SECRET'), `${name}-privacy`);
        test(Object.values(result).every(value => typeof value === 'number'), `${name}-numeric-only`);
    }
    async function scan(response, name) { stub(response); const result = await runAdapter(true); clean(result, name); test(calls === 1, `${name}-one-get-no-retry`); return result; }
    stub({status: 200, body: {quests: [quest('a', accepted)]}});
    let result = await runAdapter(false); test(calls === 0 && result.reliable === 0 && result.signal === 1, 'signal-only-no-account-request'); clean(result, 'signal-only');
    result = await scan({status: 200, body: {quests: []}}, 'empty'); test(result.reliable === 1 && result.eligible === 0, 'zero-distinct-from-completion');
    result = await scan({status: 200, body: {quests: [quest('a', completed), quest('b', accepted), quest('c', null)]}}, 'mixed');
    test(result.eligible === 3 && result.completed === 1 && result.unfinished === 1 && result.unaccepted === 1, 'all-eligibility-categories-counted');
    test(result.progress === -1, 'unaccepted-progress-unavailable');
    result = await scan({status: 200, body: {quests: [quest('a', accepted)]}}, 'progress'); test(result.progress === 30 && result.target === 100 && result.activity === 1, 'actual-numeric-progress-only');
    result = await scan({status: 200, body: {quests: [quest('a', completed), quest('b', completed)]}}, 'all'); test(result.reliable === 1 && result.eligible === 2 && result.completed === 2, 'nonempty-all-complete-aggregate');
    result = await scan({status: 200, body: {quests: [quest('unsupported', accepted, 'UNSUPPORTED')]}}, 'unsupported'); test(result.unfinished === 1 && result.completed === 0 && result.progress === -1, 'unsupported-unfinished-not-ignored');
    for (const [name, response] of [
        ['429', {status: 429, body: {private: 'SYNTHETIC_SECRET'}}],
        ['failure', {status: 500}], ['malformed', {status: 200, body: {quests: {}}}],
        ['partial', {status: 200, body: {quests: [], partial: true}}],
        ['pagination', {status: 200, body: {quests: [], next_cursor: 'SYNTHETIC_SECRET'}}],
        ['missing-body', {status: 200}], ['timeout', null],
        ['duplicate', {status: 200, body: {quests: [quest('a', completed), quest('a', completed)]}}],
        ['ambiguous-expiry', {status: 200, body: {quests: [{id: 'fixture'}]}}]
    ]) { result = await scan(response, name); test(result.reliable === 0 && result.error > 0, `${name}-unknown-not-completed`); }
    delete window.stopQuestProgram; result = await runAdapter(false); test(result.hook === 0, 'missing-stop-hook-observed');
    for (const route of [0, 2]) {
        stub({status: 200, body: {quests: [quest('a', completed)]}});
        result = await runAdapter(true, route); test(calls === 0 && result.reliable === 0 && result.route !== 1, 'route-loss-no-account-scan');
    }
    stub({status: 200, body: {quests: [quest('a', completed)]}});
    result = await runAdapter(true, 1, 'expected-run');
    test(calls === 0 && result.signal === 0 && result.route === 0, 'replaced-renderer-no-scan');
    window.__24510Orb.key = 'expected-run'; result = await runAdapter(true, 1, 'expected-run');
    test(calls === 1 && result.route === 1 && result.reliable === 1, 'user-route-recovery-read-only');
    const failed = summary.filter(row => !row.passed).length;
    document.body.textContent = JSON.stringify({total: summary.length, passed: summary.length - failed, failed, summary});
})().catch(error => { document.body.textContent = JSON.stringify({failed: 1, reason: 'Offline fixture error: ' + error.name + ': ' + error.message}); });
