{
	"targets": [
		{
			"target_name": "desktopduplication",
			"sources": [
				"src/desktopduplication.cpp"
			],
			"include_dirs": [
				"<!@(node -p \"require('node-addon-api').include\")"
			],
			"dependencies": [
				"<!(node -p \"require('node-addon-api').gyp\")"
			],
			"defines": [
				"NAPI_DISABLE_CPP_EXCEPTIONS"
			],
			"libraries": [
				"d3d11.lib"
			]
		}
	]
}