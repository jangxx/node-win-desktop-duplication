# Windows Desktop Duplication for Node

A native addon to use the [Windows Duplication API](https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api), which is part of Windows 8 and higher.

# Installation

	npm install windows-desktop-duplication

# Usage

```javascript
const { DesktopDuplication } = require('windows-desktop-duplication');

let dd = new DesktopDuplication(0); // register for screen 0

let frame;

try {
	dd.initialize();
	frame = dd.getFrame();
} catch(err) {
	console.log("An error occured:", err.message);
	process.exit(0);
}

// work with the data in frame
```

An example of how to write the data into a png-file can be found in the _examples/_ directory.
You need to install pngjs before you can use it, however (`npm i pngjs`).

# Methods

**coming soon**