# dc_cmd_script

🛠️  Welcome to the lab.

`dc_cmd_script.js` is a Discord quest recon / automation payload. It crawls the Discord client for hidden webpack exports, hooks into quest state, and attempts to drive accepted quests from the inside.

> **For educational research only.** 

## what it does

This script is the kind of thing you drop into the console when you want to see what Discord is hiding behind the scenes.

- hunts internal Discord modules using webpackChunk logic
- extracts quest state and user progress data
- identifies accepted active quests for the logged-in user
- applies spoofed progress strategies for supported quests
- automatically retries the quest runner after it stops, respecting Discord rate-limit delays
- exposes `window.stopQuestRunner()` to stop the active runner
- exposes `window.stopQuestProgram()` to stop the full recurring program

## supported quest payloads

- `WATCH_VIDEO` / `WATCH_VIDEO_ON_MOBILE`
  - forges video progress updates directly through Discord's quest API.
- `PLAY_ON_DESKTOP`
  - simulates a desktop app session by spoofing running game metadata.
- `STREAM_ON_DESKTOP`
  - fakes stream metadata so Discord thinks you're streaming.
- `PLAY_ACTIVITY`
  - pumps heartbeat updates to emulate activity.

## drop-in usage

1. open Discord.
2. open devtools / console in the Discord client.
3. paste the contents of `dc_cmd_script.js`.
4. watch it scan and process your accepted quests.

stop the active runner anytime:

```js
window.stopQuestRunner();
```
To stop the full recurring program:

```js
window.stopQuestProgram();
```

## why this exists

This repo is a study in Discord client internals and quest flow mechanics. It's about:

- doing recon on Discord's webpack-bundled runtime
- finding the hidden stores and dispatchers that power quest updates
- understanding how quest progress can be observed and manipulated

## notes from the field

- browser-only runs may not fully support desktop-level quest spoofing.
- some quests still need legit client state or real interaction.
- Discord updates can break the detection logic fast.
- use this responsibly — it's an experiment, not a cheat sheet.

## changelog

### 2026-09-26

- Added automatic enrollment for available quests before each runner cycle.
- Added recurring runner cycles with a 60-second minimum pause and Discord Retry-After handling.
- The program exits after no unaccepted available quests remain.
- Added `window.stopQuestProgram()` to stop the full loop; `window.stopQuestRunner()` stops the active runner.

## author

- `0x440_1me`

---

This is a research artifact. Keep it in the lab.
