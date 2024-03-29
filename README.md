# Windows Desktop Duplication for Node

A native module for Node.js to use the [Desktop Duplication API](https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api), which is part of Windows 8 and higher.

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

More examples, including ones on how to write the data into png-files, can be found in the _examples/_ directory.
You need to install pngjs before you can use them however (`npm i pngjs`).

# Methods

## Data format

All of the methods returning images return an object with the format:

```javascript
{
	data: Buffer,
	width: Number,
	height: Number
}
```

The data in the buffer are the raw pixel values in RGBA order with one byte per channel.
This object format is referred to as the "default format" in the rest of this documentation.

## DesktopDuplication

_static_ **getMonitorCount**()  
Static method to get the number of available monitors.

**constructor**(screenNum)  
Creates a new instance for the screen `screenNum`.
Use the `getMonitorCount()` method to get the number of available screens.

**initialize**()  
Set up the required DirectX objects.
Use a try/catch block to catch errors in the initialization process.

**getFrame**(?retryCount)  
Synchronously gets a single frame in the default format.
If the procedure fails, retry up to `retryCount` times (default: 5).
If there was no image captured after all retries are used up, this method throws an error.

**getFrameAsync**(?retryCount)  
Like `getFrame`, but returning a promise instead which resolves to image data.
The capture and image processing is also run in a separate thread for better performance.

**startAutoCapture**(delay, ?allowSkips)  
Starts a new thread, which tries to capture the screen every `delay` milliseconds.
Image data is then emitted as a **frame** event.
This method functions similar to `setInterval(() => dd.getFrameAsync().then(frame => emit("frame", frame)), delay)`, but with the added bonus of all the timing stuff happening in native code and a separate thread for better performance. 
**Note**: You can only have one of these threads running at any time, so subsequent calls to `startAutoCapture` without stopping the auto capture in between have no effect.
The optional parameter `allowSkips` controls how the thread queues up the **frame** events.
If the event did not have a chance to fire before the next image is captured, it can either be queued up (`allowSkips = false`) or just be thrown away (`allowSkips = true`, default).

**stopAutoCapture**(?clearBacklog)  
Stops the auto capture thread.
By default, no futher **frame** events will be emitted after this method has been called, since `clearBacklog` is `true` by default.
If you want to process every captured frame set `clearBacklog` to `false`.

# Events

Event **'frame'**  
Emitted in an interval determined by the delay parameter in the `startAutoCapture` method.
The event handler will be called with an object in the default image format.

# Troubleshooting

### Error: *Failed to aquire next frame: The application made a call that is invalid. Either the parameters of the call or the state of some object was incorrect.*

This error is thrown if multiple simultaneous requests to capture an image are attempted.
Different from how one might expect the DesktopDuplication to work, it does not actually simply return an image of the current desktop content.
Instead, the user has to *request* an image and Windows will only return one if the contents have changed since the last request.
The request is given a timeout (the maximum time it will block) in case no image is returned.
This behavior does not mesh well with having multiple threads running however, since this library only uses a single DesktopDuplication instance for all methods as well as a timeout of 1 second.
It is therefore very easy to request two images at the same time if `getFrameAsync` or `startAutoCapture` are used.
To avoid the error you can either only use the blocking `getFrame`, or make sure that no calls to `getFrame` and `getFrameAsync` and no running auto capture threads happen at the same time.
