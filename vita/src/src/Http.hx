package src;

typedef HttpRequest = {
	var url:String;
	var callback:haxe.io.Bytes->Void;
	var errCallback:String->Void;
	var cancelled:Bool;
	var fulfilled:Bool;
	var post:Bool;
	var postData:String;
	var ?file:haxe.io.Bytes;
};

/** Offline HTTP adapter. Network-backed menus are removed from the Vita UI. */
class Http {
	public static function init():Void {}
	public static function loop():Void {}

	public static function get(url:String, callback:haxe.io.Bytes->Void, errCallback:String->Void):HttpRequest {
		return request(url, callback, errCallback, false, null);
	}

	public static function post(url:String, postData:String, callback:haxe.io.Bytes->Void, errCallback:String->Void):HttpRequest {
		return request(url, callback, errCallback, true, postData);
	}

	public static function uploadFile(url:String, data:haxe.io.Bytes, callback:haxe.io.Bytes->Void, errCallback:String->Void):HttpRequest {
		var req = request(url, callback, errCallback, true, null);
		req.file = data;
		return req;
	}

	static function request(url:String, callback:haxe.io.Bytes->Void, errCallback:String->Void, post:Bool, postData:String):HttpRequest {
		return {
			url: url,
			callback: callback,
			errCallback: errCallback,
			cancelled: false,
			fulfilled: true,
			post: post,
			postData: postData
		};
	}

	public static function cancel(req:HttpRequest):Void {
		req.cancelled = true;
	}
}
