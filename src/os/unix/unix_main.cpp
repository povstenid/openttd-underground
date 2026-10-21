/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file unix_main.cpp Main entry for Unix. */

#include "../../stdafx.h"
#include "../../openttd.h"
#include "../../crashlog.h"
#include "../../core/random_func.hpp"
#include "../../string_func.h"

#include <time.h>
#include <signal.h>

#ifdef __ANDROID__
/* SDL renames main to SDL_main, which SDL's Java activity calls after loading this library. */
#	include <SDL.h>
#	include <android/log.h>
#	include <pthread.h>
#	include <stdlib.h>
#	include <unistd.h>
#endif

#include "../../safeguards.h"

#ifdef __ANDROID__
/**
 * Pass everything written to stdout and stderr on to logcat, as nothing reads them on Android.
 * @param arg Pointer to the read end of the pipe.
 * @return Nothing; runs until the pipe is closed.
 */
static void *AndroidLogThread(void *arg)
{
	int fd = *static_cast<int *>(arg);
	std::string line;
	char buf[512];
	ssize_t len;
	while ((len = read(fd, buf, sizeof(buf))) > 0) {
		for (ssize_t i = 0; i < len; i++) {
			if (buf[i] != '\n') {
				line += buf[i];
				continue;
			}
			__android_log_write(ANDROID_LOG_INFO, "OpenTTD", line.c_str());
			line.clear();
		}
	}
	return nullptr;
}

/**
 * Prepare the process environment for running on Android.
 * The Java activity extracts the game data to a directory and passes it via OPENTTD_ANDROID_DATA.
 * That directory becomes the working directory (game data) and the base of the
 * personal directory (configuration, saves, downloaded content).
 */
static void SetupAndroidEnvironment()
{
	static int log_pipe[2];
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	if (pipe(log_pipe) == 0) {
		dup2(log_pipe[1], STDOUT_FILENO);
		dup2(log_pipe[1], STDERR_FILENO);
		pthread_t thread;
		if (pthread_create(&thread, nullptr, AndroidLogThread, &log_pipe[0]) == 0) pthread_detach(thread);
	}

	const char *data_dir = getenv("OPENTTD_ANDROID_DATA");
	if (data_dir == nullptr) data_dir = SDL_AndroidGetInternalStoragePath();
	if (data_dir == nullptr) return;

	if (chdir(data_dir) != 0) __android_log_print(ANDROID_LOG_ERROR, "OpenTTD", "Cannot enter data directory %s", data_dir);
	/* Not the data directory: OpenTTD does not scan a working directory that is also the home directory. */
	const char *home_dir = SDL_AndroidGetInternalStoragePath();
	if (home_dir != nullptr) setenv("HOME", home_dir, 1);
	setenv("XDG_DATA_HOME", data_dir, 1);
	setenv("XDG_CONFIG_HOME", data_dir, 1);
}
#endif

int CDECL main(int argc, char *argv[])
{
#ifdef __ANDROID__
	SetupAndroidEnvironment();
#endif

	/* Make sure our arguments contain only valid UTF-8 characters. */
	std::vector<std::string_view> params;
	for (int i = 0; i < argc; ++i) {
		StrMakeValidInPlace(argv[i]);
		params.emplace_back(argv[i]);
	}

	CrashLog::InitialiseCrashLog();

	SetRandomSeed(time(nullptr));

	signal(SIGPIPE, SIG_IGN);

	return openttd_main(params);
}
