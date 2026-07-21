package datachannel;

enum abstract RTCState(Int) from Int to Int {
	var RTC_NEW = 0;
	var RTC_CONNECTING = 1;
	var RTC_CONNECTED = 2;
	var RTC_DISCONNECTED = 3;
	var RTC_FAILED = 4;
	var RTC_CLOSED = 5;
}

enum abstract RTCGatheringState(Int) from Int to Int {
	var RTC_GATHERING_NEW = 0;
	var RTC_GATHERING_INPROGRESS = 1;
	var RTC_GATHERING_COMPLETE = 2;
}

/**
	API-compatible peer connection for the offline Vita build. Multiplayer is
	hidden in the Vita UI, but the shared simulation still references these
	types for its single-player state.
**/
class RTCPeerConnection {
	public var iceServers(default, null):Array<String>;
	public var bindAddress(default, null):String;
	public var portBegin(default, null):Int;
	public var portEnd(default, null):Int;
	public var mtu(default, null):Int;
	public var maxMessageSize(default, null):Int;
	public var state(default, null):RTCState = RTC_CLOSED;
	public var gatheringState(default, null):RTCGatheringState = RTC_GATHERING_NEW;
	public var remoteDescription(default, null):String;
	public var remoteDescriptionType(default, null):String;
	public var localDescription(default, null):String;
	public var localDescriptionType(default, null):String;
	public var dataChannels(default, null):Array<RTCDataChannel> = [];
	public var localAddress(get, never):String;
	public var remoteAddress(get, never):String;

	public dynamic function onLocalDescription(sdp:String, type:String):Void {}
	public dynamic function onLocalCandidate(candidate:String):Void {}
	public dynamic function onStateChange(state:RTCState):Void {}
	public dynamic function onGatheringStateChange(state:RTCGatheringState):Void {}
	public dynamic function onDataChannel(dc:RTCDataChannel):Void {}

	public function new(iceServers:Array<String>, bindAddress:String, portBegin:Int = 0, portEnd:Int = 0, mtu:Int = 0, maxMessageSize:Int = 0) {
		this.iceServers = iceServers;
		this.bindAddress = bindAddress;
		this.portBegin = portBegin;
		this.portEnd = portEnd;
		this.mtu = mtu;
		this.maxMessageSize = maxMessageSize;
	}

	public function close():Void {
		state = RTC_CLOSED;
	}

	public function setRemoteDescription(sdp:String, type:String):Void {
		remoteDescription = sdp;
		remoteDescriptionType = type;
	}

	public function addRemoteCandidate(candidate:String):Void {}

	public function createDatachannel(label:String):RTCDataChannel {
		return null;
	}

	public function createDatachannelWithOptions(label:String, unordered:Bool, maxRetransmits:Null<Int>, maxLifetime:Null<Int>):RTCDataChannel {
		return null;
	}

	function get_localAddress():String return null;
	function get_remoteAddress():String return null;
}
