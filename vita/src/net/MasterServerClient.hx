package net;

import net.Net.ServerInfo;

typedef RemoteServerInfo = {
	name:String,
	players:Int,
	maxPlayers:Int,
	platform:Int,
	version:String
}

/** Disabled online-service adapter for Vita's initial offline port. */
class MasterServerClient {
	public static var instance:MasterServerClient;

	public function new() {}

	public static function process():Void {}

	public static function connectToMasterServer(onConnect:() -> Void, onError:() -> Void = null):Void {
		if (onError != null)
			onError();
	}

	public static function disconnectFromMasterServer():Void {
		instance = null;
	}

	public function heartBeat():Void {}
	public function sendServerInfo(serverInfo:ServerInfo):Void {}
	public function sendConnectToServer(serverName:String, sdp:String, isInvite:Bool = false):Void {}
	public function getServerList(callback:Array<RemoteServerInfo>->Void):Void callback([]);
}
