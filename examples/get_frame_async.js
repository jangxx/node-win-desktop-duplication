const { DesktopDuplication } = require('../');
const { PNG } = require('pngjs');
const fs = require('fs');

let dd = new DesktopDuplication(0);

try {
	dd.initialize();
} catch(err) {
	console.log("An error occured:", err.message);
	process.exit(0);
}

dd.getFrameAsync().then(frame => {
	let png = new PNG({
		width: frame.width,
		height: frame.height
	});
	
	frame.data.copy(png.data);
	
	png.pack().pipe(fs.createWriteStream("test_async.png"));
}).catch(err => console.log("An error occured:", err.message));