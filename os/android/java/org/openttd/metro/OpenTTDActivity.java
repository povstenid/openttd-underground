/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

package org.openttd.metro;

import android.content.pm.PackageInfo;
import android.graphics.Point;
import android.system.Os;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * Starts OpenTTD through SDL. Before the game runs, the data files bundled in the APK
 * (base graphics, languages, script libraries, the metro set) are copied to the app's storage, as
 * OpenTTD reads its data from regular files.
 */
public class OpenTTDActivity extends SDLActivity {
	private static final String TAG = "OpenTTD";

	/** Asset directories that are copied to the data directory. */
	private static final String[] DATA_DIRS = { "baseset", "lang", "ai", "game", "scripts", "newgrf" };

	@Override
	protected String[] getLibraries() {
		return new String[] { "SDL2", "openttd" };
	}

	/**
	 * SDL calls this on its own thread right before it starts the game,
	 * so the data is prepared there instead of on the UI thread.
	 */
	@Override
	protected String[] getArguments() {
		File dir = getExternalFilesDir(null);
		if (dir == null) dir = getFilesDir();
		try {
			extractData(dir);
			writeDefaultConfig(dir);
			Os.setenv("OPENTTD_ANDROID_DATA", dir.getAbsolutePath(), true);
			/* OpenTTD picks its initial language from the locale environment, which Android does not set. */
			Locale locale = Locale.getDefault();
			String lang = locale.getLanguage() + (locale.getCountry().isEmpty() ? "" : "_" + locale.getCountry());
			Os.setenv("LANG", lang + ".UTF-8", true);
		} catch (Exception e) {
			Log.e(TAG, "Preparing the game data failed", e);
		}

		/* Optional command line options for OpenTTD, e.g. "-d misc=3" to debug; there is no other way to pass them on Android. */
		String cmdline = readFile(new File(dir, "openttd/cmdline.txt"));
		if (cmdline == null || cmdline.trim().isEmpty()) return new String[0];
		Log.i(TAG, "Command line: " + cmdline.trim());
		return cmdline.trim().split("\\s+");
	}

	@Override
	protected void onDestroy() {
		super.onDestroy();
		/* OpenTTD keeps its state in globals that are only set up once per process; start fresh next time. */
		System.exit(0);
	}

	/** Copy the bundled data when it is missing or belongs to another build of the app. */
	private void extractData(File dir) throws Exception {
		PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
		String version = info.versionName + "/" + info.lastUpdateTime;
		File stamp = new File(dir, ".data-version");
		if (version.equals(readFile(stamp))) return;

		Log.i(TAG, "Extracting game data to " + dir);
		for (String name : DATA_DIRS) {
			File target = new File(dir, name);
			deleteRecursive(target);
			copyAsset(name, target);
		}
		writeFile(stamp, version);
	}

	/** On first start pick an interface size that is readable from the couch, relative to the screen height. */
	private void writeDefaultConfig(File dir) throws IOException {
		File config = new File(dir, "openttd/openttd.cfg");
		if (config.exists()) return;

		Point size = new Point();
		getWindowManager().getDefaultDisplay().getRealSize(size);
		int height = Math.min(size.x, size.y);
		int scale = Math.round(height * 150f / 1080f / 25f) * 25;
		scale = Math.max(100, Math.min(500, scale));

		config.getParentFile().mkdirs();
		writeFile(config, "[misc]\ngui_scale = " + scale + "\n");
		Log.i(TAG, "Created " + config + " with interface scale " + scale + "% for a screen height of " + height);
	}

	private void copyAsset(String path, File target) throws IOException {
		String[] children = getAssets().list(path);
		if (children == null || children.length == 0) {
			target.getParentFile().mkdirs();
			try (InputStream in = getAssets().open(path); OutputStream out = new FileOutputStream(target)) {
				byte[] buf = new byte[65536];
				int len;
				while ((len = in.read(buf)) > 0) out.write(buf, 0, len);
			}
			return;
		}
		target.mkdirs();
		for (String child : children) copyAsset(path + "/" + child, new File(target, child));
	}

	private static void deleteRecursive(File file) {
		File[] children = file.listFiles();
		if (children != null) {
			for (File child : children) deleteRecursive(child);
		}
		file.delete();
	}

	private static String readFile(File file) {
		if (!file.exists()) return null;
		try (InputStream in = new FileInputStream(file)) {
			byte[] buf = new byte[(int)file.length()];
			int len = in.read(buf);
			return new String(buf, 0, Math.max(len, 0), StandardCharsets.UTF_8);
		} catch (IOException e) {
			return null;
		}
	}

	private static void writeFile(File file, String content) throws IOException {
		try (OutputStream out = new FileOutputStream(file)) {
			out.write(content.getBytes(StandardCharsets.UTF_8));
		}
	}
}
