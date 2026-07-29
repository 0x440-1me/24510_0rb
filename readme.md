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
- auto-retries when Discord rate-limits quest fetches
- exposes `window.stopQuestRunner()` to kill the payload cleanly

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

stop it anytime:

```js
window.stopQuestRunner();
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

## author

- `0x440_1me`

---

This is a research artifact. Keep it in the lab.
