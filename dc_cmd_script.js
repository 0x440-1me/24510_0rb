(async () => {

	// hax by 0x440-1me ;d

	delete window.$;

	// get discord webpack require

	let wpRequire = window.webpackChunkdiscord_app.push([
		[Math.random().toString(36)],
		{},
		require => require
	]);

	window.webpackChunkdiscord_app.pop();

	//find webpack export

	const findExport = (name, matcher) => {
		for (const module of Object.values(wpRequire.c)) {
			const exports = module?.exports;

			if (!exports) continue;

			const found = matcher(exports);

			if (found) {
				return found;
			}
		}

		throw new Error(`Could not find ${name}`);
	};

	// loads modules

	let ApplicationStreamingStore = findExport(
		"ApplicationStreamingStore",
		exports =>
			exports.A?.getStreamerActiveStreamMetadata &&
			exports.A
	);

	let RunningGameStore = findExport(
		"RunningGameStore",
		exports =>
			exports.Ay?.getRunningGames &&
			exports.Ay
	);

	let QuestsStore = findExport(
		"QuestsStore",
		exports =>
			exports.A?.getQuest &&
			exports.A
	);

	let ChannelStore = findExport(
		"ChannelStore",
		exports =>
			exports.A?.getAllThreadsForParent &&
			exports.A
	);

	let GuildChannelStore = findExport(
		"GuildChannelStore",
		exports =>
			exports.Ay?.getSFWDefaultChannel &&
			exports.Ay
	);

	let FluxDispatcher = findExport(
		"FluxDispatcher",
		exports =>
			exports.h?.flushWaitQueue &&
			exports.h
	);

	let api = findExport(
		"api",
		exports =>
			exports.Bo?.get &&
			exports.Bo?.post &&
			exports.Bo
	);

	// galubal helpers

	const sleep = milliseconds =>
		new Promise(resolve =>
			setTimeout(resolve, milliseconds)
		);

	const isApp =
		typeof DiscordNative !== "undefined";

	let stopRequested = false;
	let questCache = null;
	let questCacheExpiresAt = 0;
	let questFetchInFlight = null;
	const QUEST_CACHE_TTL = 5000;

	window.stopQuestRunner = () => {
		stopRequested = true;

		console.log(
			"Stop requested. The runner will stop after the current loop."
		);
	};


	// get quests

	async function fetchRawQuests(forceRefresh = false) {
		if (!forceRefresh && Date.now() < questCacheExpiresAt && questCache) {
			return questCache;
		}

		if (questFetchInFlight) {
			return questFetchInFlight;
		}

		questFetchInFlight = (async () => {
			while (true) {
				try {
					const response = await api.get({
						url: "/quests/@me",
						rejectWithError: false
					});

					if (response?.status === 429) {
						const retrySeconds =
							Number(response?.body?.retry_after ?? 5);

						const retryMs =
							Math.ceil(retrySeconds * 1000) + 500;

						console.warn(
							`Rate limited. Retrying in ${retryMs}ms.`
						);

						await sleep(retryMs);
						continue;
					}

					if (response?.status >= 400) {
						throw new Error(
							`Quest request failed: HTTP ${response.status}`
						);
					}

					return response?.body?.quests ?? [];

				} catch (error) {
					const status =
						error?.status ??
						error?.response?.status;

					if (status === 429) {
						const retrySeconds =
							Number(
								error?.body?.retry_after ??
								error?.response?.body?.retry_after ??
								5
							);

						const retryMs =
							Math.ceil(retrySeconds * 1000) + 500;

						console.warn(
							`Rate limited. Retrying in ${retryMs}ms.`
						);

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
			questCacheExpiresAt = Date.now() + QUEST_CACHE_TTL;
			return quests;
		} finally {
			questFetchInFlight = null;
		}
	}
	// filter quests

	function isAcceptedActiveQuest(quest) {
		const enrolled =
			quest?.user_status?.enrolled_at != null;

		const unfinished =
			quest?.user_status?.completed_at == null;

		const expiry =
			Date.parse(
				quest?.config?.expires_at ?? ""
			);

		const active =
			Number.isFinite(expiry) &&
			expiry > Date.now();

		return enrolled && unfinished && active;
	}

	async function getAcceptedQuest(skippedQuestIds = new Set()) {
		const quests = await fetchRawQuests();

		return (
			quests.find(quest =>
				!skippedQuestIds.has(quest.id) &&
				isAcceptedActiveQuest(quest)
			) ?? null
		);
	}

	async function getQuestById(questId) {
		const quests = await fetchRawQuests();

		return (
			quests.find(
				quest => quest.id === questId
			) ?? null
		);
	}

	// get task details

	function getTaskDetails(quest) {
		const taskConfig =
			quest?.config?.task_config ??
			quest?.config?.task_config_v2;

		const supportedTaskNames = [
			"WATCH_VIDEO",
			"WATCH_VIDEO_ON_MOBILE",
			"PLAY_ON_DESKTOP",
			"STREAM_ON_DESKTOP",
			"PLAY_ACTIVITY"
		];
 
		const unsupportedTasks = [ // unused const
			"ACHIEVEMENT_IN_ACTIVITY",
			"PLAY_ON_XBOX", 
			"PLAY_ON_PLAYSTATION" 
		]

		const taskName =
			supportedTaskNames.find(name =>
				taskConfig?.tasks?.[name] != null
			);

		if (!taskName) {
			return null;
		}

		const task =
			taskConfig.tasks[taskName];

		const progressEntry =
			quest?.user_status?.progress?.[taskName];

		let progressValue = 0;

		if (typeof progressEntry === "number") {
			progressValue = progressEntry;
		} else if (
			typeof progressEntry?.value === "number"
		) {
			progressValue = progressEntry.value;
		} else if (
			typeof quest?.user_status
				?.stream_progress_seconds === "number"
		) {
			progressValue =
				quest.user_status
					.stream_progress_seconds;
		}

		return {
			taskName,
			task,
			secondsNeeded: Number(
				task?.target ?? 0
			),
			secondsDone: Number(
				progressValue ?? 0
			)
		};
	}

	// finish check

	function getQuestEndReason(quest) {
		if (!quest) {
			return "removed";
		}

		if (
			quest.user_status?.completed_at != null
		) {
			return "completed";
		}

		if (
			quest.user_status?.enrolled_at == null
		) {
			return "not-enrolled";
		}

		const expiry =
			Date.parse(
				quest.config?.expires_at ?? ""
			);

		if (
			!Number.isFinite(expiry) ||
			expiry <= Date.now()
		) {
			return "expired";
		}

		return null;
	}


	async function processQuest(quest, initialDetails, pid) {

		const questId = quest.id;

		const questName =
			quest.config?.messages?.quest_name ??
			questId;

		const applicationId = quest.config?.application?.id;
		const applicationName =
			quest.config?.application?.name ?? "Unknown application";

		const {
			taskName,
			secondsNeeded,
			secondsDone: initialSecondsDone
		} = initialDetails;

		let secondsDone = initialSecondsDone;

		let fn = async () => {
			while (true) {
				if (stopRequested) {
					console.log(
						`Stopped while processing ${questName}.`
					);

					break;
				}

				const currentQuest =
					await getQuestById(questId);

				const endReason =
					getQuestEndReason(currentQuest);

				if (endReason) {
					console.log(
						`${questName} ended: ${endReason}.`
					);

					break;
				}

				const currentDetails =
					getTaskDetails(currentQuest);

				if (!currentDetails) {
					console.warn(
						`${questName} no longer has a supported task.`
					);

					break;
				}

				console.log(
					`${questName}: ` +
					`${currentDetails.secondsDone}/` +
					`${currentDetails.secondsNeeded} ` +
					`(${currentDetails.taskName})`
				);


				if (taskName === "WATCH_VIDEO" || taskName === "WATCH_VIDEO_ON_MOBILE") {
					const maxFuture = 10, speed = 7, interval = 1
					const enrolledAt = new Date(quest.user_status.enrolled_at).getTime()
					let completed = false
					let fn = async () => {
						while (true) {
							const maxAllowed = Math.floor((Date.now() - enrolledAt) / 1000) + maxFuture
							const diff = maxAllowed - secondsDone
							const timestamp = secondsDone + speed
							if (diff >= speed) {
								const res = await api.post({ url: `/quests/${quest.id}/video-progress`, body: { timestamp: Math.min(secondsNeeded, timestamp + Math.random()) } })
								completed = res.body.completed_at != null
								secondsDone = Math.min(secondsNeeded, timestamp)
							}

							if (timestamp >= secondsNeeded) {
								break
							}
							await new Promise(resolve => setTimeout(resolve, interval * 1000))
						}
						if (!completed) {
							await api.post({ url: `/quests/${quest.id}/video-progress`, body: { timestamp: secondsNeeded } })
						}
						console.log("Quest completed!")
					}
					await fn();
					console.log(`Spoofing video for ${questName}.`)
					return;
				}

				else if (
					taskName === "PLAY_ON_DESKTOP"
				) {
					if (!isApp) {
						console.log("This no longer works in browser for non-video quests. Use the discord desktop app to complete the", questName, "quest!")
						return;
					} else {
						try {
							const res = await api.get({ url: `/applications/public?application_ids=${applicationId}` });
							const appData = res.body[0] ?? {};
							const exeEntry = appData.executables?.find(x => x.os === "win32") || appData.executables?.[0] || {};
							const exeName = String(exeEntry.name ?? appData.name ?? "").replace(">", "") || appData.name || "unknown.exe";

							const fakeGame = {
								cmdLine: `C:\\Program Files\\${appData.name}\\${exeName}`,
								exeName,
								exePath: `C:/Program Files/${String(appData.name ?? "").toLowerCase()}/${exeName}`,
								hidden: false,
								isLauncher: false,
								id: applicationId,
								name: appData.name,
								pid,
								pidPath: [pid],
								processName: exeName,
								start: Date.now(),
							};

							const realGames = RunningGameStore.getRunningGames();
							const fakeGames = [fakeGame];
							const realGetRunningGames = RunningGameStore.getRunningGames;
							const realGetGameForPID = RunningGameStore.getGameForPID;

							RunningGameStore.getRunningGames = () => fakeGames;
							RunningGameStore.getGameForPID = (searchPid) => fakeGames.find(x => x.pid === searchPid);
							FluxDispatcher.dispatch({ type: "RUNNING_GAMES_CHANGE", removed: realGames, added: [fakeGame], games: fakeGames });

							const fn = data => {
								const progress = quest.config.config_version === 1
									? Number(data?.userStatus?.streamProgressSeconds ?? 0)
									: Number(data?.userStatus?.progress?.PLAY_ON_DESKTOP?.value ?? 0);

								console.log(`Quest progress: ${progress}/${secondsNeeded}`);

								if (progress >= secondsNeeded) {
									console.log("Quest completed!");

									RunningGameStore.getRunningGames = realGetRunningGames;
									RunningGameStore.getGameForPID = realGetGameForPID;
									FluxDispatcher.dispatch({ type: "RUNNING_GAMES_CHANGE", removed: [fakeGame], added: [], games: [] });
									FluxDispatcher.unsubscribe?.("QUESTS_SEND_HEARTBEAT_SUCCESS", fn);
								}
							};

							FluxDispatcher.subscribe("QUESTS_SEND_HEARTBEAT_SUCCESS", fn);

							console.log(`Spoofed your game to ${applicationName}. Wait for ${Math.ceil((secondsNeeded - secondsDone) / 60)} more minutes.`);
							return;
						} catch (error) {
							console.error("Failed to spoof PLAY_ON_DESKTOP game:", error);
							return;
						}
						if (!isApp) {
							console.log("This no longer works in browser for non-video quests. Use the discord desktop app to complete the", questName, "quest!")
						} else {
							let realFunc = ApplicationStreamingStore.getStreamerActiveStreamMetadata
							ApplicationStreamingStore.getStreamerActiveStreamMetadata = () => ({
								id: applicationId,
								pid,
								sourceName: null
							})

							let fn = data => {
								let progress = quest.config.config_version === 1 ? data.userStatus.streamProgressSeconds : Math.floor(data.userStatus.progress.STREAM_ON_DESKTOP.value)
								console.log(`Quest progress: ${progress}/${secondsNeeded}`)

								if (progress >= secondsNeeded) {
									console.log("Quest completed!")

									ApplicationStreamingStore.getStreamerActiveStreamMetadata = realFunc
									FluxDispatcher.unsubscribe("QUESTS_SEND_HEARTBEAT_SUCCESS", fn)
								}
							}
							FluxDispatcher.subscribe("QUESTS_SEND_HEARTBEAT_SUCCESS", fn)

							console.log(`Spoofed your stream to ${applicationName}. Stream any window in vc for ${Math.ceil((secondsNeeded - secondsDone) / 60)} more minutes.`)
							console.log("Remember that you need at least 1 other person to be in the vc!")
						}
						return;
					}
				}

				else if (taskName === "PLAY_ACTIVITY") {
					const channelId = ChannelStore.getSortedPrivateChannels()[0]?.id ?? Object.values(GuildChannelStore.getAllGuilds()).find(x => x != null && x.VOCAL.length > 0).VOCAL[0].channel.id
					const streamKey = `call:${channelId}:1`

					let fn = async () => {
						console.log("Completing quest", questName, "-", quest.config.messages.quest_name)

						while (true) {
							const res = await api.post({ url: `/quests/${quest.id}/heartbeat`, body: { stream_key: streamKey, terminal: false } })
							const progress = res.body.progress.PLAY_ACTIVITY.value
							console.log(`Quest progress: ${progress}/${secondsNeeded}`)

							await new Promise(resolve => setTimeout(resolve, 20 * 1000))

							if (progress >= secondsNeeded) {
								await api.post({ url: `/quests/${quest.id}/heartbeat`, body: { stream_key: streamKey, terminal: true } })
								break
							}
						}

						console.log("Quest completed!")
					}
					await fn();
					return;

				}

				else {
					console.warn(`Unsupported task: ${taskName}`);
					break;
				}

				await sleep(5000);
			}
		};

		await fn();
	}

	async function runAllAcceptedQuests() {
		if (window.__questRunnerActive) {
			console.warn("Quest runner is already running.");
			return;
		}

		window.__questRunnerActive = true;
		stopRequested = false;

		try {
			while (!stopRequested) {
				const quest = await getAcceptedQuest();

				if (!quest) {
					console.log(
						"No accepted active quests remaining."
					);
					break;
				}

				const details = getTaskDetails(quest);

				if (!details) {
					console.warn("Unsupported quest:", quest.id);
					break;
				}

				const pid = Math.floor(Math.random() * 30000) + 1000;

				await processQuest(quest, details, pid);

				await sleep(10000);
			}
		} finally {
			window.__questRunnerActive = false;
		}

		console.log("Quest runner finished.");
	}

	await runAllAcceptedQuests();

})().catch(error => {
	console.error(
		"Quest runner failed:",
		error
	);
});
