package src;

import haxe.CallStack;
import haxe.Exception;
import sys.FileSystem;
import sys.io.File;

/** Persistent startup diagnostics for failures that happen before the UI. */
class VitaDiagnostics {
	static inline var LOG_DIRECTORY = "ux0:data/MBHaxe-MBU";
	static inline var LOG_PATH = "ux0:data/MBHaxe-MBU/startup.log";
	static var reportedFailure = false;
	static var startTime:Float = 0;

	static function ensureDirectory():Void {
		if (!FileSystem.exists(LOG_DIRECTORY))
			FileSystem.createDirectory(LOG_DIRECTORY);
	}

	static function append(message:String):Void {
		try {
			ensureDirectory();
			var output = File.append(LOG_PATH, false);
			output.writeString(message + "\n");
			output.flush();
			output.close();
		} catch (_:Dynamic) {}
	}

	public static function reset():Void {
		reportedFailure = false;
		startTime = haxe.Timer.stamp();
		try {
			ensureDirectory();
			// Native main resets this file before HashLink starts. Preserve its
			// pre-Haxe stages and uncaught-static-initializer diagnostics.
			if (!FileSystem.exists(LOG_PATH))
				File.saveContent(LOG_PATH, "MBHaxe Vita startup\n");
		} catch (_:Dynamic) {}
		stage("Main.main: entry");
	}

	public static function stage(message:String):Void {
		var now = haxe.Timer.stamp();
		if (startTime == 0)
			startTime = now;
		append('[${Std.int((now - startTime) * 1000)} ms] ${message}');
	}

	public static function reportError(error:Dynamic):Void {
		if (reportedFailure)
			return;
		reportedFailure = true;

		var message = try Std.string(error) catch (_:Dynamic) "Unknown exception";
		var stack = "";
		try {
			var exception = Std.downcast(error, Exception);
			stack = CallStack.toString(exception == null ? CallStack.exceptionStack() : exception.stack);
		} catch (_:Dynamic) {}
		append("FATAL: " + message + (stack == "" ? "" : "\n" + stack));
	}
}
