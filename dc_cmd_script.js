/*
 * dc_cmd_script.js — Discord quest runner
 *
 * Paste into the Discord desktop client DevTools console.
 *
 * What it does
 *   - loads the Discord webpack module registry and resolves the internal
 *     stores / dispatcher / HTTP client it needs
 *   - enrolls one available (unaccepted, unexpired) quest per cycle
 *   - drives every accepted + active quest using a per-task strategy
 *   - honours HTTP 429 Retry-After and restarts the cycle after >= 60s
 *   - stops on its own once no unaccepted quests remain
 *
 * Public handles
 *   window.stopQuestRunner()       — stop after the current quest finishes
 *   window.stopQuestProgram()      — stop the whole recurring program
 *   window.__questRunnerDiagnose() — report how each internal module resolved
 *
 * Research artifact. For educational use only.
 */

(async () => {
  "use strict";

  // ============================================================
  // configuration
  // ============================================================

  const TAG = "[0x440]";
  const ACCEPT_TAG = "[AUTO-QUEST-ACCEPT]";

  const MIN_CYCLE_DELAY_MS = 60_000;
  const DEFAULT_RETRY_AFTER_MS = 60_000;
  const QUEST_CACHE_TTL_MS = 5_000;

  const QUEST_POLL_INTERVAL_MS = 10_000;
  const BETWEEN_QUESTS_DELAY_MS = 10_000;
  const UNSUPPORTED_SKIP_DELAY_MS = 1_000;

  const VIDEO_STEP_SECONDS = 7;
  const VIDEO_MAX_FUTURE_SECONDS = 10;
  const VIDEO_TICK_MS = 1_000;
  const VIDEO_STALL_LIMIT = 60;

  const ACTIVITY_HEARTBEAT_MS = 20_000;

  /** Task names we know how to drive, in priority order. */
  const SUPPORTED_TASK_NAMES = [
    "WATCH_VIDEO",
    "WATCH_VIDEO_ON_MOBILE",
    "PLAY_ON_DESKTOP",
    "STREAM_ON_DESKTOP",
    "PLAY_ACTIVITY",
  ];

  /** Tasks we know about but cannot drive. Used for clearer skip logging. */
  const KNOWN_UNSUPPORTED_TASK_NAMES = [
    "ACHIEVEMENT_IN_ACTIVITY",
    "PLAY_ON_XBOX",
    "PLAY_ON_PLAYSTATION",
  ];

  /**
   * How to locate each internal module.
   *
   * `fingerprints` are the historical `exports.<minifiedKey>` shapes. Minified
   * keys and helper methods churn between Discord builds, so when every
   * fingerprint misses we fall back to `weights`: a scored scan over every
   * webpack export (and its direct properties) looking for the method names
   * that only that store/API object has.
   */
  const MODULE_SPECS = {
    api: {
      fingerprints: [
        (exports) => exports.Bo?.get && exports.Bo?.post && exports.Bo,
      ],
      weights: { get: 2, post: 2, put: 1, patch: 1, del: 1 },
      // Many exports expose get/post; the verb set identifies the REST client,
      // so use it to break ties.
      tieBreak: { del: 3, put: 2, patch: 2 },
      minScore: 4,
    },

    FluxDispatcher: {
      fingerprints: [(exports) => exports.h?.flushWaitQueue && exports.h],
      weights: {
        flushWaitQueue: 4,
        dispatch: 2,
        subscribe: 2,
        unsubscribe: 1,
      },
      minScore: 4,
    },

    QuestsStore: {
      fingerprints: [
        (exports) => exports.A?.getQuest && exports.A,
        (exports) => exports.AY?.getQuest && exports.AY,
      ],
      weights: {
        getQuest: 4,
        getQuests: 3,
        getClaimedQuests: 2,
        getQuestIDs: 1,
      },
      minScore: 4,
    },

    RunningGameStore: {
      fingerprints: [
        (exports) => exports.Ay?.getRunningGames && exports.Ay,
        (exports) => exports.A?.getRunningGames && exports.A,
        (exports) => exports.AY?.getRunningGames && exports.AY,
      ],
      weights: { getRunningGames: 4, getGameForPID: 2, getRunningGame: 2 },
      minScore: 4,
    },

    ApplicationStreamingStore: {
      fingerprints: [
        (exports) => exports.A?.getStreamerActiveStreamMetadata && exports.A,
        (exports) =>
          exports.Ay?.getStreamerActiveStreamMetadata && exports.Ay,
      ],
      weights: {
        getStreamerActiveStreamMetadata: 5,
        getActiveStreamForUser: 2,
        getCurrentUserActiveStream: 2,
      },
      minScore: 5,
    },

    ChannelStore: {
      fingerprints: [
        (exports) => exports.A?.getSortedPrivateChannels && exports.A,
        (exports) => exports.A?.getAllThreadsForParent && exports.A,
      ],
      weights: {
        getSortedPrivateChannels: 4,
        getAllThreadsForParent: 2,
        getChannel: 1,
      },
      minScore: 4,
    },

    GuildChannelStore: {
      fingerprints: [
        (exports) => exports.Ay?.getSFWDefaultChannel && exports.Ay,
        (exports) => exports.Ay?.getAllGuilds && exports.Ay,
        (exports) => exports.A?.getAllGuilds && exports.A,
      ],
      weights: {
        getAllGuilds: 4,
        getSFWDefaultChannel: 3,
        getChannels: 2,
        getDefaultChannel: 2,
      },
      minScore: 4,
    },
  };

  const HEARTBEAT_EVENT = "QUESTS_SEND_HEARTBEAT_SUCCESS";

  // ============================================================
  // state
  // ============================================================

  const state = {
    /** Set by window.stopQuestProgram(). */
    stopProgram: false,
    /** Set by window.stopQuestRunner(). Consumed by the active quest. */
    stopRunner: false,
    /** True once a cycle found nothing left to enroll. Ends the program. */
    noUnacceptedAvailable: false,
    /** Retry-After hint carried from a rate-limited call to the next cycle. */
    retryAfterMs: 0,
    /** Wakes the inter-cycle delay early when the program is stopped. */
    loopWake: null,
  };

  const isApp = typeof DiscordNative !== "undefined";

  // ============================================================
  // logging
  // ============================================================

  const log = (...args) => console.log(TAG, ...args);
  const warn = (...args) => console.warn(TAG, ...args);
  const errorLog = (...args) => console.error(TAG, ...args);

  // ============================================================
  // primitives
  // ============================================================

  const sleep = (milliseconds) =>
    new Promise((resolve) => setTimeout(resolve, milliseconds));

  /** Sleep that window.stopQuestProgram() can cut short. */
  function interruptibleDelay(milliseconds) {
    return new Promise((resolve) => {
      const finish = () => {
        state.loopWake = null;
        resolve();
      };

      const timer = setTimeout(finish, milliseconds);

      state.loopWake = () => {
        clearTimeout(timer);
        finish();
      };
    });
  }

  /**
   * Normalise a Discord 429 payload into a delay in milliseconds.
   * The extra 500ms keeps us off the exact boundary of the rate-limit window.
   */
  function parseRetryAfterMs(payload, fallbackMs = DEFAULT_RETRY_AFTER_MS) {
    const seconds = Number(
      payload?.body?.retry_after ??
        payload?.retry_after ??
        payload?.response?.body?.retry_after,
    );

    return Number.isFinite(seconds) && seconds > 0
      ? Math.ceil(seconds * 1000) + 500
      : fallbackMs;
  }

  function httpStatusOf(error) {
    return error?.status ?? error?.response?.status;
  }

  // ============================================================
  // discord webpack access
  // ============================================================

  function getWebpackRequire() {
    const webpackRequire = window.webpackChunkdiscord_app.push([
      [Math.random().toString(36)],
      {},
      (require) => require,
    ]);

    window.webpackChunkdiscord_app.pop();

    return webpackRequire;
  }

  function listModules(webpackRequire) {
    try {
      return Object.values(webpackRequire?.c ?? {});
    } catch {
      return [];
    }
  }

  /** Try the historical fingerprints; null when none of them match. */
  function findExport(webpackRequire, matcher) {
    for (const module of listModules(webpackRequire)) {
      const exports = module?.exports;
      if (!exports) continue;

      try {
        const found = matcher(exports);
        if (found) return found;
      } catch {
        // A throwing module is simply not the one we are looking for.
      }
    }

    return null;
  }

  /** Every export plus its direct properties, as scan candidates. */
  function collectCandidates(webpackRequire) {
    const candidates = [];

    for (const module of listModules(webpackRequire)) {
      const exports = module?.exports;
      if (!exports || typeof exports !== "object") continue;

      candidates.push({ target: exports, path: "exports" });

      let keys;
      try {
        keys = Object.keys(exports);
      } catch {
        continue;
      }

      for (const key of keys) {
        let value;
        try {
          value = exports[key];
        } catch {
          continue;
        }

        if (!value) continue;
        if (typeof value !== "object" && typeof value !== "function") continue;

        candidates.push({ target: value, path: `exports.${key}` });
      }
    }

    return candidates;
  }

  function scoreCandidate(target, weights) {
    let score = 0;
    const hits = [];

    for (const [method, weight] of Object.entries(weights)) {
      let hasMethod = false;

      try {
        hasMethod = typeof target?.[method] === "function";
      } catch {
        hasMethod = false;
      }

      if (hasMethod) {
        score += weight;
        hits.push(method);
      }
    }

    return { score, hits };
  }

  /** Report each drift or missing module once, not once per cycle. */
  const reportedOnce = new Set();

  function warnOnce(key, message) {
    if (reportedOnce.has(key)) return;
    reportedOnce.add(key);
    warn(message);
  }

  function resolveModule(name, scanState) {
    const spec = MODULE_SPECS[name];

    if (!spec) throw new Error(`Unknown module: ${name}`);

    for (const fingerprint of spec.fingerprints) {
      const found = findExport(scanState.webpackRequire, fingerprint);
      if (found) {
        return { value: found, via: "fingerprint", path: name, hits: [] };
      }
    }

    if (!scanState.candidates) {
      scanState.candidates = collectCandidates(scanState.webpackRequire);
    }

    let best = null;

    for (const candidate of scanState.candidates) {
      const { score, hits } = scoreCandidate(candidate.target, spec.weights);
      if (score < spec.minScore) continue;

      const tieScore = spec.tieBreak
        ? scoreCandidate(candidate.target, spec.tieBreak).score
        : 0;

      const better =
        !best ||
        score > best.score ||
        (score === best.score && tieScore > best.tieScore);

      if (better) best = { ...candidate, score, tieScore, hits };
    }

    if (!best) {
      throw new Error(
        `Could not find ${name}: no webpack export exposes ` +
          `[${Object.keys(spec.weights).join(", ")}]`,
      );
    }

    // The module still works, only its fingerprint is stale; keep running and
    // say so once so the matcher can be updated later.
    warnOnce(
      name,
      `${name} moved; located by method scan at ${best.path} ` +
        `(${best.hits.join(", ")}).`,
    );

    return {
      value: best.target,
      via: "scan",
      path: best.path,
      hits: best.hits,
    };
  }

  /**
   * Per-cycle module registry. Resolution is lazy on purpose: one stale
   * fingerprint should only disable the quest types that need that module,
   * never the whole runner.
   */
  function createDiscord() {
    const scanState = { webpackRequire: getWebpackRequire(), candidates: null };
    const entries = new Map();

    const entryFor = (name) => {
      if (!entries.has(name)) entries.set(name, resolveModule(name, scanState));
      return entries.get(name);
    };

    return {
      require(name) {
        return entryFor(name).value;
      },

      /** Null instead of throwing, for modules only some quest types need. */
      tryRequire(name) {
        try {
          return entryFor(name).value;
        } catch (error) {
          warnOnce(`missing:${name}`, error.message);
          return null;
        }
      },

      describe(name) {
        try {
          const entry = entryFor(name);
          return {
            ok: true,
            via: entry.via,
            path: entry.path,
            methods: entry.hits,
          };
        } catch (error) {
          return { ok: false, error: error.message };
        }
      },
    };
  }

  /**
   * Resolve the modules a strategy needs. Missing ones are returned instead of
   * thrown, so a single drifted module disables one quest type only.
   */
  function loadModules(discord, names) {
    const modules = {};
    const missing = [];

    for (const name of names) {
      const value = discord.tryRequire(name);
      if (value) modules[name] = value;
      else missing.push(name);
    }

    return { modules, missing };
  }

  // ============================================================
  // quest fetching
  // ============================================================

  let questCache = null;
  let questCacheExpiresAt = 0;
  let questFetchInFlight = null;

  async function fetchQuests(discord, { forceRefresh = false } = {}) {
    if (!forceRefresh && questCache && Date.now() < questCacheExpiresAt) {
      return questCache;
    }

    if (questFetchInFlight) return questFetchInFlight;

    questFetchInFlight = (async () => {
      while (true) {
        try {
          const response = await discord.require("api").get({
            url: "/quests/@me",
            rejectWithError: false,
          });

          if (response?.status === 429) {
            const retryMs = parseRetryAfterMs(response, 5_500);
            warn(`Rate limited. Retrying in ${retryMs}ms.`);
            await sleep(retryMs);
            continue;
          }

          if (response?.status >= 400) {
            throw new Error(`Quest request failed: HTTP ${response.status}`);
          }

          return response?.body?.quests ?? [];
        } catch (error) {
          if (httpStatusOf(error) === 429) {
            const retryMs = parseRetryAfterMs(error, 5_500);
            warn(`Rate limited. Retrying in ${retryMs}ms.`);
            await sleep(retryMs);
            continue;
          }

          throw error;
        }
      }
    })();

    try {
      const quests = await questFetchInFlight;
      questCache = quests;
      questCacheExpiresAt = Date.now() + QUEST_CACHE_TTL_MS;
      return quests;
    } finally {
      questFetchInFlight = null;
    }
  }

  async function getQuestById(discord, questId) {
    const quests = await fetchQuests(discord);
    return quests.find((quest) => quest.id === questId) ?? null;
  }

  async function getAcceptedQuest(discord, skippedQuestIds) {
    const quests = await fetchQuests(discord);

    return (
      quests.find(
        (quest) =>
          !skippedQuestIds.has(quest.id) && isAcceptedActiveQuest(quest),
      ) ?? null
    );
  }

  // ============================================================
  // quest predicates and accessors
  // ============================================================

  /** Unaccepted, not completed, and not expired. */
  function isAvailableQuest(quest) {
    if (!quest?.id) return false;
    if (quest?.user_status?.enrolled_at != null) return false;
    if (quest?.user_status?.completed_at != null) return false;

    const expiry = Date.parse(quest?.config?.expires_at ?? "");

    return !Number.isFinite(expiry) || expiry > Date.now();
  }

  /** Enrolled, not completed, and still inside its expiry window. */
  function isAcceptedActiveQuest(quest) {
    const enrolled = quest?.user_status?.enrolled_at != null;
    const unfinished = quest?.user_status?.completed_at == null;
    const expiry = Date.parse(quest?.config?.expires_at ?? "");

    return (
      enrolled && unfinished && Number.isFinite(expiry) && expiry > Date.now()
    );
  }

  function questDisplayName(quest) {
    return (
      quest?.config?.messages?.quest_name ??
      quest?.config?.application?.name ??
      quest?.id ??
      "unknown quest"
    );
  }

  /**
   * Why an accepted quest is no longer worth polling, or null if it is alive.
   * Behaviour matches the original payload: a quest without a parseable expiry
   * reads as "expired" here.
   */
  function getQuestEndReason(quest) {
    if (!quest) return "removed";
    if (quest?.user_status?.completed_at != null) return "completed";
    if (quest?.user_status?.enrolled_at == null) return "not-enrolled";

    const expiry = Date.parse(quest?.config?.expires_at ?? "");

    if (!Number.isFinite(expiry) || expiry <= Date.now()) return "expired";

    return null;
  }

  function getQuestTaskConfig(quest) {
    const configs = [quest?.config?.task_config_v2, quest?.config?.task_config];

    return (
      configs.find(
        (config) => config?.tasks && Object.keys(config.tasks).length > 0,
      ) ??
      configs.find(Boolean) ??
      null
    );
  }

  function getTaskDetails(quest) {
    const taskConfig = getQuestTaskConfig(quest);

    const taskName = SUPPORTED_TASK_NAMES.find(
      (name) => taskConfig?.tasks?.[name] != null,
    );

    if (!taskName) return null;

    return {
      taskName,
      task: taskConfig.tasks[taskName],
      secondsNeeded: Number(taskConfig.tasks[taskName]?.target ?? 0),
      secondsDone: Number(readQuestProgress(quest, taskName) ?? 0),
    };
  }

  /** Progress is a bare number, a `{ value }` entry, or a legacy field. */
  function readQuestProgress(quest, taskName) {
    const entry = quest?.user_status?.progress?.[taskName];

    if (typeof entry === "number") return entry;
    if (typeof entry?.value === "number") return entry.value;
    if (typeof quest?.user_status?.stream_progress_seconds === "number") {
      return quest.user_status.stream_progress_seconds;
    }

    return 0;
  }

  /** Progress as delivered by a QUESTS_SEND_HEARTBEAT_SUCCESS event. */
  function readHeartbeatProgress(quest, data, taskName) {
    if (quest?.config?.config_version === 1) {
      return Number(data?.userStatus?.streamProgressSeconds ?? 0);
    }

    const value = Number(data?.userStatus?.progress?.[taskName]?.value ?? 0);

    return taskName === "STREAM_ON_DESKTOP" ? Math.floor(value) : value;
  }

  function readTrafficMetadata(quest, QuestsStore) {
    try {
      return (
        QuestsStore?.getQuest?.(quest.id)?.trafficMetadataSealed ??
        quest?.trafficMetadataSealed ??
        quest?.traffic_metadata_sealed ??
        null
      );
    } catch {
      return null;
    }
  }

  /**
   * Poll until the quest ends, the caller's completion predicate flips, or a
   * stop is requested. Returns a reason string, or "completed" on success.
   */
  async function waitForQuestEnd(ctx, isComplete) {
    while (true) {
      if (state.stopRunner) return "stopped";
      if (isComplete?.()) return "completed";

      await sleep(QUEST_POLL_INTERVAL_MS);

      if (state.stopRunner) return "stopped";
      if (isComplete?.()) return "completed";

      const reason = getQuestEndReason(
        await getQuestById(ctx.discord, ctx.quest.id),
      );

      if (reason) return reason;
    }
  }

  // ============================================================
  // strategy: WATCH_VIDEO / WATCH_VIDEO_ON_MOBILE
  // ============================================================

  async function runVideoQuest(ctx) {
    const { discord, quest, details } = ctx;
    const name = questDisplayName(quest);
    const needed = details.secondsNeeded;

    const api = discord.tryRequire("api");

    if (!api) {
      warn(`WATCH_VIDEO unavailable for ${name}: missing api.`);
      return "skipped";
    }

    const enrolledAt = Date.parse(quest?.user_status?.enrolled_at ?? "");
    const startedAt = Number.isFinite(enrolledAt) ? enrolledAt : Date.now();

    let secondsDone = details.secondsDone;
    let completed = false;
    let stalledTicks = 0;

    const postVideoProgress = (timestamp) =>
      api.post({
        url: `/quests/${quest.id}/video-progress`,
        body: { timestamp },
      });

    while (true) {
      if (state.stopRunner) {
        log(`Stopped while processing ${name}.`);
        return "stopped";
      }

      const nextTimestamp = secondsDone + VIDEO_STEP_SECONDS;

      // Discord only accepts progress up to (wall clock since enrollment +
      // a small look-ahead), so we pace updates against that ceiling.
      const maxAllowed =
        Math.floor((Date.now() - startedAt) / 1000) + VIDEO_MAX_FUTURE_SECONDS;

      let posted = false;

      if (maxAllowed - secondsDone >= VIDEO_STEP_SECONDS) {
        const response = await postVideoProgress(
          Math.min(needed, nextTimestamp + Math.random()),
        );

        completed = response?.body?.completed_at != null;
        secondsDone = Math.min(needed, nextTimestamp);
        posted = true;
        stalledTicks = 0;
      }

      if (completed || nextTimestamp >= needed) break;

      if (!posted && ++stalledTicks > VIDEO_STALL_LIMIT) {
        warn(
          `${name}: no progress accepted for ${VIDEO_STALL_LIMIT}s, ` +
            "giving up on this quest.",
        );
        return "stalled";
      }

      await sleep(VIDEO_TICK_MS);
    }

    if (!completed) {
      await postVideoProgress(needed);
    }

    return "completed";
  }

  // ============================================================
  // strategy: PLAY_ON_DESKTOP
  // ============================================================

  async function runDesktopGameQuest(ctx) {
    const { discord, quest, details, pid } = ctx;
    const name = questDisplayName(quest);
    const applicationName =
      quest?.config?.application?.name ?? "Unknown application";

    if (!isApp) {
      warn(
        "This no longer works in browser for non-video quests.",
        `Use Discord desktop for ${name}.`,
      );
      return "skipped";
    }

    const { modules, missing } = loadModules(discord, [
      "api",
      "RunningGameStore",
      "FluxDispatcher",
    ]);

    if (missing.length) {
      warn(
        `PLAY_ON_DESKTOP unavailable for ${name}: ` +
          `missing ${missing.join(", ")}.`,
      );
      return "skipped";
    }

    const { api, RunningGameStore, FluxDispatcher } = modules;

    const applicationId = quest?.config?.application?.id;

    const appResponse = await api.get({
      url: `/applications/public?application_ids=${applicationId}`,
    });

    const appData = appResponse?.body?.[0] ?? {};
    const exeEntry =
      appData.executables?.find((candidate) => candidate.os === "win32") ??
      appData.executables?.[0] ??
      {};

    const exeName =
      String(exeEntry.name ?? appData.name ?? "").replace(">", "") ||
      appData.name ||
      "unknown.exe";

    const installPath = String(appData.name ?? "").toLowerCase();
    const remainingMinutes = Math.ceil(
      (details.secondsNeeded - details.secondsDone) / 60,
    );

    const fakeGame = {
      cmdLine: `C:\\Program Files\\${appData.name}\\${exeName}`,
      exeName,
      exePath: `C:/Program Files/${installPath}/${exeName}`,
      hidden: false,
      isLauncher: false,
      id: applicationId,
      name: appData.name,
      pid,
      pidPath: [pid],
      processName: exeName,
      start: Date.now(),
    };

    const realGetRunningGames = RunningGameStore.getRunningGames;
    const realGetGameForPID = RunningGameStore.getGameForPID;
    const realGames = RunningGameStore.getRunningGames();

    let progress = details.secondsDone;

    const onHeartbeat = (data) => {
      progress = readHeartbeatProgress(quest, data, "PLAY_ON_DESKTOP");
      log(`Quest progress: ${progress}/${details.secondsNeeded}`);
    };

    const install = () => {
      RunningGameStore.getRunningGames = () => [fakeGame];
      RunningGameStore.getGameForPID = (searchPid) =>
        searchPid === fakeGame.pid ? fakeGame : undefined;

      FluxDispatcher.dispatch({
        type: "RUNNING_GAMES_CHANGE",
        removed: realGames,
        added: [fakeGame],
        games: [fakeGame],
      });

      FluxDispatcher.subscribe(HEARTBEAT_EVENT, onHeartbeat);
    };

    const uninstall = () => {
      RunningGameStore.getRunningGames = realGetRunningGames;
      RunningGameStore.getGameForPID = realGetGameForPID;

      FluxDispatcher.dispatch({
        type: "RUNNING_GAMES_CHANGE",
        removed: [fakeGame],
        added: [],
        games: [],
      });

      FluxDispatcher.unsubscribe?.(HEARTBEAT_EVENT, onHeartbeat);
    };

    try {
      install();

      log(
        `Spoofed game to ${applicationName}.`,
        `Wait ${remainingMinutes} more minutes.`,
      );

      return await waitForQuestEnd(
        ctx,
        () => progress >= details.secondsNeeded,
      );
    } finally {
      uninstall();
    }
  }

  // ============================================================
  // strategy: STREAM_ON_DESKTOP
  // ============================================================

  async function runDesktopStreamQuest(ctx) {
    const { discord, quest, details, pid } = ctx;
    const name = questDisplayName(quest);
    const applicationName =
      quest?.config?.application?.name ?? "Unknown application";

    if (!isApp) {
      warn(`Use Discord desktop for ${name}.`);
      return "skipped";
    }

    const { modules, missing } = loadModules(discord, [
      "ApplicationStreamingStore",
      "FluxDispatcher",
    ]);

    if (missing.length) {
      warn(
        `STREAM_ON_DESKTOP unavailable for ${name}: ` +
          `missing ${missing.join(", ")}.`,
      );
      return "skipped";
    }

    const { ApplicationStreamingStore, FluxDispatcher } = modules;

    const realGetStreamerActiveStreamMetadata =
      ApplicationStreamingStore.getStreamerActiveStreamMetadata;

    let progress = details.secondsDone;

    const onHeartbeat = (data) => {
      progress = readHeartbeatProgress(quest, data, "STREAM_ON_DESKTOP");
      log(`Quest progress: ${progress}/${details.secondsNeeded}`);
    };

    const remainingMinutes = Math.ceil(
      (details.secondsNeeded - details.secondsDone) / 60,
    );

    try {
      ApplicationStreamingStore.getStreamerActiveStreamMetadata = () => ({
        id: quest?.config?.application?.id,
        pid,
        sourceName: null,
      });

      FluxDispatcher.subscribe(HEARTBEAT_EVENT, onHeartbeat);

      log(
        `Spoofed stream to ${applicationName}.`,
        `Stream any window in VC for ${remainingMinutes} more minutes.`,
      );
      log("At least one other person needs to be in the VC.");

      return await waitForQuestEnd(
        ctx,
        () => progress >= details.secondsNeeded,
      );
    } finally {
      ApplicationStreamingStore.getStreamerActiveStreamMetadata =
        realGetStreamerActiveStreamMetadata;

      FluxDispatcher.unsubscribe?.(HEARTBEAT_EVENT, onHeartbeat);
    }
  }

  // ============================================================
  // strategy: PLAY_ACTIVITY
  // ============================================================

  /**
   * First private channel, else the first voice channel of any guild. Both
   * stores are optional: whichever resolves is used, so a renamed guild store
   * only costs us the guild fallback instead of the whole quest type.
   */
  function findActivityChannelId(modules) {
    try {
      const privateChannelId =
        modules.ChannelStore?.getSortedPrivateChannels?.()?.[0]?.id;

      if (privateChannelId) return privateChannelId;
    } catch {
      // Store shape changed; fall through to guild channels.
    }

    try {
      const guild = Object.values(
        modules.GuildChannelStore?.getAllGuilds?.() ?? {},
      ).find((candidate) => candidate?.VOCAL?.length > 0);

      return guild?.VOCAL?.[0]?.channel?.id ?? null;
    } catch {
      return null;
    }
  }

  async function runActivityQuest(ctx) {
    const { discord, quest, details } = ctx;
    const name = questDisplayName(quest);

    const { modules, missing } = loadModules(discord, ["api"]);

    if (missing.length) {
      warn(
        `PLAY_ACTIVITY unavailable for ${name}: missing ${missing.join(", ")}.`,
      );
      return "skipped";
    }

    const { api } = modules;
    const channelModules = loadModules(discord, [
      "ChannelStore",
      "GuildChannelStore",
    ]).modules;

    const channelId = findActivityChannelId(channelModules);

    if (!channelId) {
      warn(`No voice or private channel available for ${name}.`);
      return "skipped";
    }

    const streamKey = `call:${channelId}:1`;

    const sendHeartbeat = (terminal) =>
      api.post({
        url: `/quests/${quest.id}/heartbeat`,
        body: { stream_key: streamKey, terminal },
      });

    log("Completing quest:", name);

    while (true) {
      if (state.stopRunner) {
        log(`Stopped while processing ${name}.`);
        return "stopped";
      }

      const response = await sendHeartbeat(false);
      const progress = Number(
        response?.body?.progress?.PLAY_ACTIVITY?.value ?? 0,
      );

      log(`Quest progress: ${progress}/${details.secondsNeeded}`);

      if (progress >= details.secondsNeeded) {
        await sendHeartbeat(true);
        return "completed";
      }

      await sleep(ACTIVITY_HEARTBEAT_MS);
    }
  }

  // ============================================================
  // strategy dispatch
  // ============================================================

  const TASK_STRATEGIES = {
    WATCH_VIDEO: runVideoQuest,
    WATCH_VIDEO_ON_MOBILE: runVideoQuest,
    PLAY_ON_DESKTOP: runDesktopGameQuest,
    STREAM_ON_DESKTOP: runDesktopStreamQuest,
    PLAY_ACTIVITY: runActivityQuest,
  };

  async function processQuest(ctx) {
    const { quest, details } = ctx;
    const name = questDisplayName(quest);
    const strategy = TASK_STRATEGIES[details.taskName];

    if (!strategy) {
      warn(`Unsupported task: ${details.taskName}`);
      return "skipped";
    }

    log(
      `${name}: ${details.secondsDone}/${details.secondsNeeded} ` +
        `(${details.taskName})`,
    );

    let reason;

    try {
      reason = await strategy(ctx);
    } catch (error) {
      errorLog(`Failed to process ${details.taskName} for ${name}:`, error);
      return "failed";
    }

    if (reason === "completed") {
      log(`Quest completed: ${name}`);
    } else if (reason && reason !== "stopped" && reason !== "skipped") {
      log(`${name} ended: ${reason}.`);
    }

    return reason;
  }

  // ============================================================
  // unsupported quest diagnostics
  // ============================================================

  function debugUnsupportedQuest(quest) {
    const taskConfig = getQuestTaskConfig(quest);
    const taskNames = Object.keys(taskConfig?.tasks ?? {});

    if (taskNames.includes("ACHIEVEMENT_IN_ACTIVITY")) {
      log("ACHIEVEMENT_IN_ACTIVITY debug:", {
        questId: quest.id,
        questName: quest?.config?.messages?.quest_name,
        task: taskConfig?.tasks?.ACHIEVEMENT_IN_ACTIVITY,
        progress: quest?.user_status?.progress?.ACHIEVEMENT_IN_ACTIVITY,
        completedAt: quest?.user_status?.completed_at,
        enrolledAt: quest?.user_status?.enrolled_at,
      });
    }

    const knownUnsupported = taskNames.filter((taskName) =>
      KNOWN_UNSUPPORTED_TASK_NAMES.includes(taskName),
    );

    if (knownUnsupported.length) {
      warn(
        "Skipping known unsupported quest:",
        quest.id,
        quest?.config?.messages?.quest_name,
        "tasks:",
        knownUnsupported,
      );
    } else {
      warn(
        "Skipping UNKNOWN quest type:",
        quest.id,
        quest?.config?.messages?.quest_name,
        "tasks:",
        taskNames,
      );
    }
  }

  // ============================================================
  // enrollment
  // ============================================================

  async function acceptAvailableQuest(discord) {
    const acceptLog = (...args) => console.log(ACCEPT_TAG, ...args);
    const api = discord.require("api");
    const QuestsStore = discord.tryRequire("QuestsStore");

    const response = await api.get({
      url: "/quests/@me",
      rejectWithError: false,
    });

    if (response?.status === 429) {
      state.retryAfterMs = parseRetryAfterMs(response);
      acceptLog("Quest fetch rate limited; next cycle will honor Retry-After.");
      return;
    }

    if (response?.status >= 400) {
      throw new Error(`Quest fetch failed: HTTP ${response.status}`);
    }

    const quest = (response?.body?.quests ?? []).find(isAvailableQuest);

    if (!quest) {
      state.noUnacceptedAvailable = true;
      acceptLog("No unaccepted available quests found.");
      return;
    }

    const name = questDisplayName(quest);

    acceptLog(`Accepting: ${name}`);

    const enrollResponse = await api.post({
      url: `/quests/${quest.id}/enroll`,
      body: {
        location: 11,
        is_targeted: false,
        metadata_sealed: null,
        traffic_metadata_sealed: readTrafficMetadata(quest, QuestsStore),
      },
      rejectWithError: false,
    });

    if (enrollResponse?.status === 429) {
      state.retryAfterMs = parseRetryAfterMs(enrollResponse);
      acceptLog("Enrollment rate limited; next cycle will honor Retry-After.");
      return;
    }

    if (enrollResponse?.status >= 200 && enrollResponse?.status < 300) {
      acceptLog(`✓ Accepted: ${name}`);
    } else {
      console.warn(
        ACCEPT_TAG,
        `Failed to accept ${name}`,
        enrollResponse?.status,
        enrollResponse?.body,
      );
    }
  }

  // ============================================================
  // runner
  // ============================================================

  async function runAllAcceptedQuests(discord) {
    if (window.__questRunnerActive) {
      warn("Quest runner is already running.");
      return;
    }

    window.__questRunnerActive = true;
    state.stopRunner = false;

    const skippedQuestIds = new Set();

    log("Quest runner started.");

    try {
      while (!state.stopRunner) {
        const quest = await getAcceptedQuest(discord, skippedQuestIds);

        if (!quest) {
          log("No supported accepted active quests remaining.");
          break;
        }

        const details = getTaskDetails(quest);

        if (!details) {
          debugUnsupportedQuest(quest);
          skippedQuestIds.add(quest.id);
          await sleep(UNSUPPORTED_SKIP_DELAY_MS);
          continue;
        }

        log(
          "Starting quest:",
          questDisplayName(quest),
          `(${details.taskName})`,
        );

        const pid = Math.floor(Math.random() * 30_000) + 1_000;

        const outcome = await processQuest({ quest, details, pid, discord });

        // A quest we cannot drive stays in the accepted list forever, so give
        // up on it for this pass instead of re-picking it every 10 seconds.
        if (outcome === "skipped" || outcome === "stalled") {
          skippedQuestIds.add(quest.id);
          await sleep(UNSUPPORTED_SKIP_DELAY_MS);
          continue;
        }

        await sleep(BETWEEN_QUESTS_DELAY_MS);
      }
    } finally {
      window.__questRunnerActive = false;
    }

    log(
      skippedQuestIds.size
        ? "Quest runner finished. " +
            `Skipped ${skippedQuestIds.size} unsupported quest(s).`
        : "Quest runner finished.",
    );
  }

  // ============================================================
  // public handles
  // ============================================================

  window.stopQuestRunner = () => {
    state.stopRunner = true;
    warn("Stop requested. Runner will stop after the current loop.");
  };

  window.stopQuestProgram = () => {
    state.stopProgram = true;
    window.stopQuestRunner?.();
    state.loopWake?.();
    console.log(`${TAG} Program stop requested.`);
  };

  /**
   * Report how every internal module resolved. Run this if a Discord update
   * breaks the payload: the entry that says `ok: false` (or `via: "scan"`)
   * names the module that moved.
   */
  window.__questRunnerDiagnose = () => {
    let discord;

    try {
      discord = createDiscord();
    } catch (error) {
      errorLog("webpack chunk unavailable:", error);
      return null;
    }

    const report = {};

    for (const name of Object.keys(MODULE_SPECS)) {
      report[name] = discord.describe(name);
    }

    console.log(`${TAG} module diagnosis`, report);
    return report;
  };

  // ============================================================
  // main: enroll -> run -> cool down -> repeat
  // ============================================================

  log("0x440 quest runner loaded ;d");

  while (!state.stopProgram) {
    try {
      const discord = createDiscord();

      await acceptAvailableQuest(discord);

      if (state.stopProgram) break;

      await runAllAcceptedQuests(discord);
    } catch (error) {
      if (httpStatusOf(error) === 429) {
        state.retryAfterMs = parseRetryAfterMs(error);
      }

      errorLog("Quest runner cycle failed:", error);

      if (String(error?.message).includes("Could not find")) {
        warn("Run window.__questRunnerDiagnose() for the module report.");
      }
    }

    if (state.stopProgram || state.noUnacceptedAvailable) break;

    const delayMs = Math.max(MIN_CYCLE_DELAY_MS, state.retryAfterMs || 0);
    state.retryAfterMs = 0;

    log(
      `Cycle stopped; restarting in ${Math.ceil(delayMs / 1000)} seconds. ` +
        "Call window.stopQuestProgram() to stop.",
    );

    await interruptibleDelay(delayMs);
  }
})().catch((error) => {
  console.error("[0x440]", "Quest runner failed:", error);
});