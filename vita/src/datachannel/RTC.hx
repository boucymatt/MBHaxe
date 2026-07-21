package datachannel;

/** Offline RTC facade used by the Vita build. */
@:allow(datachannel.RTCPeerConnection)
class RTC {
	static var inited = false;

	public static function processEvents():Void {}

	public static function init():Void {
		inited = true;
	}

	public static function finalize():Void {
		inited = false;
	}
}
