package;

import datachannel.RTC;
import gui.VersionGui;
import gui.PresentsGui;
import src.Debug;
import src.Marbleland;
import src.Console;
import hxd.Key;
import src.Util;
import src.ResourceLoader;
#if js
import fs.ManifestFileSystem;
import fs.ManifestBuilder;
#end
import hxd.Timer;
import hxd.Window;
import src.AudioManager;
import src.Settings;
import src.MarbleGame;
import gui.MainMenuGui;
import hxd.res.DefaultFont;
import h2d.Text;
import h3d.Vector;
import src.ProfilerUI;
import src.Gamepad;
import src.Http;
import src.Renderer;
import src.MissionList;
import src.MPCustoms;
#if vita
import src.VitaDiagnostics;
#end

class Main extends hxd.App {
	var marbleGame:MarbleGame;

	var loaded:Bool = false;
	#if vita
	var firstUpdateLogged:Bool = false;
	var firstRenderLogged:Bool = false;
	#end

	var frameByFrame:Bool = false;

	// var timeAccumulator:Float = 0.0;

	override function init() {
		super.init();
		#if vita
		VitaDiagnostics.stage("Main.init: engine ready");
		#end

		s3d.renderer = new Renderer();
		// s3d.checkPasses = false;

		#if (hl && !android && !ios && !vita)
		hl.UI.closeConsole();
		#end

		#if js
		Window.getInstance().useScreenPixels = false;
		var zoomRatio = Util.isTouchDevice() ? js.Browser.window.screen.height * js.Browser.window.devicePixelRatio / 768 : js.Browser.window.devicePixelRatio; // js.Browser.window.devicePixelRatio;
		s2d.scaleMode = Zoom(zoomRatio);
		#end
		#if (android || ios || vita)
		var zoomRatio = Math.min(Window.getInstance().height, Window.getInstance().width) / 700;
		#if (ios || vita)
		var sw = Window.getInstance().width;
		var sh = Window.getInstance().height;
		Settings.optionsSettings.screenWidth = Std.int(sw / zoomRatio);
		Settings.optionsSettings.screenHeight = Std.int(sh / zoomRatio);
		#end
		s2d.scaleMode = Zoom(zoomRatio);

		trace("Initial Window resized to "
			+ Window.getInstance().width
			+ "x"
			+ Window.getInstance().height
			+ " (Zoom "
			+ zoomRatio
			+ ")");
		#end

		#if (android || ios)
		Window.getInstance().addEventTarget(ev -> {
			if (ev.kind == EPush || ev.kind == ERelease || ev.kind == EMove) {
				@:privateAccess s2d.window.curMouseX = cast ev.relX;
				@:privateAccess s2d.window.curMouseY = cast ev.relY;
				ev.propagate = true;
			}

			if (ev.kind == EPush) {
				@:privateAccess Key.keyPressed[Key.MOUSE_LEFT] = Key.getFrame();
			}

			if (ev.kind == ERelease) {
				@:privateAccess Key.keyPressed[Key.MOUSE_LEFT] = -Key.getFrame();
			}
		});
		#end

		new Console(); // Singleton
		Console.log("Initializing MBHaxe");
		#if vita
		VitaDiagnostics.stage("Main.init: console ready");
		#end
		#if hl
		Console.log("System: " + Sys.systemName());
		#end

		// try {
		#if !vita
		RTC.init();
		Http.init();
		haxe.MainLoop.add(() -> Http.loop());
		#end
		Settings.init();
		#if vita
		VitaDiagnostics.stage("Main.init: settings ready");
		#end
		Gamepad.init();
		#if vita
		VitaDiagnostics.stage("Main.init: gamepad ready");
		#end
		#if !vita
		MPCustoms.loadMissionList();
		#end
		ResourceLoader.init(s2d, () -> {
			#if vita
			VitaDiagnostics.stage("Main.init: filesystem ready");
			#end
			AudioManager.init();
			AudioManager.playShell();
			#if vita
			VitaDiagnostics.stage("Main.init: audio ready");
			#end
			#if !vita
			Marbleland.init();
			#end
			marbleGame = new MarbleGame(s2d, s3d);
			MarbleGame.canvas.setContent(new PresentsGui());
			#if vita
			VitaDiagnostics.stage("Main.init: first UI ready");
			#end
			haxe.Timer.delay(() -> {
				#if vita
				VitaDiagnostics.stage("Main.init: mission list begin");
				#end
				MissionList.buildMissionList(); // Yeah pls
				#if vita
				// Mission parsing creates many temporary strings and parser nodes. Free
				// them before the preview world begins its own allocation burst.
				hl.Gc.major();
				#end
				#if vita
				VitaDiagnostics.stage("Main.init: mission list ready");
				VitaDiagnostics.stage("Main.init: preview world begin");
				#end
				marbleGame.startPreviewWorld(() -> {
					#if vita
					VitaDiagnostics.stage("Main.init: preview world ready");
					VitaDiagnostics.stage("Main.init: preview mission begin");
					#end
					marbleGame.setPreviewMission('urban', () -> {
						MarbleGame.canvas.setContent(new MainMenuGui());
						#if vita
						VitaDiagnostics.stage("Main.init: main menu ready");
						#end
						#if !vita
						VersionGui.checkVersion();
						#end
					});
				});
			}, 100);

			new ProfilerUI(s2d);

			loaded = true;
			#if vita
			VitaDiagnostics.stage("Main.init: startup complete");
			#end
		});
		// } catch (e) {
		// 	Console.error(e.message);
		// 	Console.error(e.stack.toString());
		// 	throw e;
		// }

		// ResourceLoader.init(s2d, () -> {
		// 	Settings.init();
		// 	AudioManager.init();
		// 	AudioManager.playShell();
		// 	marbleGame = new MarbleGame(s2d, s3d);
		// 	MarbleGame.canvas.setContent(new MainMenuGui());
		// 	// world = new MarbleWorld(s3d, s2d, mission);

		// 	// world.init();
		// 	// world.start();
		// 	// debugProfiler = new h3d.impl.Benchmark(s2d);
		// 	// debugProfiler.y = 40;

		// 	// fpsCounter = new Text(DefaultFont.get(), s2d);
		// 	// fpsCounter.y = 40;
		// 	// fpsCounter.color = new Vector(1, 1, 1, 1);

		// 	loaded = true;
		// });
	}

	override function update(dt:Float) {
		#if vita
		var logFirstUpdate = !firstUpdateLogged;
		if (logFirstUpdate)
			VitaDiagnostics.stage("Main.loop: first update begin");
		#end
		super.update(dt);
		ProfilerUI.begin(1);
		ProfilerUI.measure("updateBegin", 1);
		if (loaded) {
			// try {
			// timeAccumulator += dt;
			// while (timeAccumulator > 1 / 60) {
			// 	marbleGame.update(1 / 60);
			// 	timeAccumulator -= 1 / 60;
			// }
			#if !vita
			RTC.processEvents();
			#end
			marbleGame.update(dt);
			// } catch (e) {
			// Console.error(e.message);
			// Console.error(e.stack.toString());
			// 	throw e;
			// }
			// world.update(dt);
			ProfilerUI.update(this.engine.fps);
		}
		ProfilerUI.end(1);
		#if vita
		if (logFirstUpdate) {
			firstUpdateLogged = true;
			VitaDiagnostics.stage("Main.loop: first update complete");
		}
		#end
	}

	override function render(e:h3d.Engine) {
		#if vita
		var logFirstRender = !firstRenderLogged;
		if (logFirstRender)
			VitaDiagnostics.stage("Main.loop: first render begin");
		#end
		// this.world.render(e);
		ProfilerUI.begin(0);
		if (loaded) {
			ProfilerUI.measure("renderBegin", 0);
			marbleGame.render(e);
		}
		super.render(e);
		ProfilerUI.end(0);
		#if hl
		e.driver.present();
		#end
		#if vita
		if (logFirstRender) {
			firstRenderLogged = true;
			VitaDiagnostics.stage("Main.loop: first render complete");
		}
		#end
	}

	static function main() {
		// h3d.mat.PbrMaterialSetup.set();
		#if vita
		VitaDiagnostics.reset();
		hxd.System.reportError = VitaDiagnostics.reportError;
		sdl.Sdl.onGlContextError = () -> {
			VitaDiagnostics.reportError("Failed to create the VitaGL shader context; check ur0:data/libshacccg.suprx");
			Sys.exit(-1);
		};
		try {
			new Main();
		} catch (error:Dynamic) {
			VitaDiagnostics.reportError(error);
			throw error;
		}
		#else
		new Main();
		#end
	}
}
