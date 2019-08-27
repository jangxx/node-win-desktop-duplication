const { DesktopDuplication } = require('../');
const { PNG } = require('pngjs');
const fs = require('fs');

let dd = new DesktopDuplication(0);

let frame;
try {
	dd.initialize();

	frame = dd.getFrame();
} catch(err) {
	console.log("An error occured:", err.message);
	process.exit(0);
}

let png = new PNG({
	width: frame.width,
	height: frame.height
});

for(let i = 0; i < frame.data.length; i += 4) {
	png.data[i] = frame.data[i+2];
	png.data[i+1] = frame.data[i+1];
	png.data[i+2] = frame.data[i+0];
	png.data[i+3] = frame.data[i+3];
}

png.pack().pipe(fs.createWriteStream("test.png"));