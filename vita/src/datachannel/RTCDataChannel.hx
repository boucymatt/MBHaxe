package datachannel;

enum RTCDataChannelReliability {
	Reliable;
	Unreliable(maxRetransmits:Int, maxLifetime:Int);
}

enum RTCDataChannelState {
	Closed;
	Open;
	Error;
}

class RTCDataChannel {
	public var name(default, null):String;
	public var peerConnection(default, null):RTCPeerConnection;
	public var unordered(default, null):Bool;
	public var reliability(default, null):RTCDataChannelReliability;
	public var state(default, null):RTCDataChannelState = Closed;
	public var bufferedAmount(get, never):Int;
	public var bufferedAmountLowThreshold(get, set):Int;
	var lowThreshold = 0;

	public dynamic function onOpen(name:String):Void {}
	public dynamic function onClosed():Void {}
	public dynamic function onError(msg:String):Void {}
	public dynamic function onMessage(msg:haxe.io.Bytes):Void {}
	public dynamic function onBufferedAmountLow():Void {}

	public function sendMessage(message:String):Void {}
	public function sendBytes(bytes:haxe.io.Bytes):Void {}

	function get_bufferedAmount():Int return 0;
	function get_bufferedAmountLowThreshold():Int return lowThreshold;
	function set_bufferedAmountLowThreshold(value:Int):Int return lowThreshold = value;
}
