/* Native application's read-only adapter. Never enrolls, writes progress,
   simulates activity, reads messages/cookies/tokens or logs source records.
   Full scan is explicit opt-in (Refresh/Retry or enabled status setting).
   No account/quest record leaves the renderer: only bounded numeric fields. */
(async () => {
    const state = window.__24510Orb;
    const result = {
        signal: [1, 2, 3].includes(state?.phase) ? state.phase : 0,
        hook: typeof window.stopQuestProgram === 'function' ? 1 : 0,
        active: window.__questRunnerActive === true ? 1 : 0,
        reliable: 0, error: 0, eligible: 0, completed: 0,
        unaccepted: 0, unfinished: 0, progress: -1, target: -1, activity: 0,
        route: 0
    };
    try {
        if (__ORB_KEY__ && state?.key !== __ORB_KEY__) { result.signal = 0; return result; }
        result.route = document.readyState === 'complete' && location.origin === 'https://discord.com' ?
            (location.pathname === '/quest-home' ? 1 : 2) : 0;
    } catch { return result; }
    if (Number(window.__questRunnerRetryAfterMs) > 0) result.error = 2;
    if (!__ORB_SCAN__) return result;
    if (result.route !== 1 || result.error) { if (!result.error) result.error = 1; return result; }
    try {
        const chunk = window.webpackChunkdiscord_app;
        if (!Array.isArray(chunk)) { result.error = 1; return result; }
        let require;
        /* Access the loaded cache; no modules are loaded or patched. */
        chunk.push([[`orb_status_${Date.now()}`], {}, r => { require = r; }]);
        chunk.pop();
        let api;
        for (const module of Object.values(require?.c ?? {})) {
            const candidate = module?.exports?.Bo;
            if (typeof candidate?.get === 'function') { api = candidate; break; }
        }
        if (!api) { result.error = 1; return result; }
        /* One GET per explicit scan. Timeout never retries or writes. The
           underlying request may finish after timeout; no cancellation claim. */
        let timer;
        const response = await Promise.race([
            api.get({url: '/quests/@me', rejectWithError: false}),
            new Promise(resolve => { timer = setTimeout(() => resolve(null), 2500); })
        ]).finally(() => clearTimeout(timer));
        if (response?.status === 429) { result.error = 2; return result; }
        if (!response || response.status < 200 || response.status >= 300) { result.error = 4; return result; }
        const quests = response.body?.quests;
        if (!Array.isArray(quests) || quests.length > 10000 || response.body.has_more === true ||
            response.body.next_cursor != null || response.body.next != null || response.body.partial === true) { result.error = 3; return result; }
        const ids = new Set(); const now = Date.now();
        const types = {WATCH_VIDEO: 1, WATCH_VIDEO_ON_MOBILE: 1, PLAY_ON_DESKTOP: 2, STREAM_ON_DESKTOP: 3, PLAY_ACTIVITY: 4};
        let progress = 0, target = 0, numeric = true, activity = 0, mixed = false;
        for (const quest of quests) {
            if (typeof quest?.id !== 'string' || !quest.id || ids.has(quest.id)) { result.error = 3; return result; }
            ids.add(quest.id);
            const expiry = Date.parse(quest.config?.expires_at ?? '');
            /* Missing expiry is ambiguous: original treats availability
               differently from accepted activity. Never claim completion. */
            if (!Number.isFinite(expiry)) { result.error = 3; return result; }
            if (expiry <= now) continue;
            if (quest.user_status != null && (typeof quest.user_status !== 'object' || Array.isArray(quest.user_status))) { result.error = 3; return result; }
            result.eligible++;
            if (quest.user_status?.completed_at != null) {
                if (!Number.isFinite(Date.parse(quest.user_status.completed_at))) { result.error = 3; return result; }
                result.completed++; continue;
            }
            if (quest.user_status?.enrolled_at == null) { result.unaccepted++; numeric = false; continue; }
            if (!Number.isFinite(Date.parse(quest.user_status.enrolled_at))) { result.error = 3; return result; }
            result.unfinished++;
            const tasks = quest.config.task_config_v2?.tasks ?? quest.config.task_config?.tasks;
            const names = Object.keys(tasks ?? {}); const name = names.length === 1 ? names[0] : null;
            if (!name || !types[name]) { numeric = false; continue; }
            if (activity && activity !== types[name]) mixed = true;
            activity = types[name];
            const entry = quest.user_status.progress?.[name];
            const value = typeof entry === 'number' ? entry : entry?.value;
            const goal = tasks[name]?.target;
            if (!Number.isFinite(value) || !Number.isFinite(goal) || value < 0 || goal <= 0 || value > goal) numeric = false;
            else { progress += value; target += goal; }
        }
        /* A successful full response is required, including both unaccepted
           and accepted entries. Do not derive this from cached store emptiness. */
        result.reliable = 1;
        if (numeric && target > 0 && target <= 100000000) { result.progress = Math.floor(progress); result.target = Math.ceil(target); }
        result.activity = mixed ? 0 : activity;
        return result;
    } catch { result.error = 1; return result; }
})()
