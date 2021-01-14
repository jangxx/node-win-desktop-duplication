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

console.log("Got frame!");
console.log(`Width: ${frame.width} Height: ${frame.height}`);

let png = new PNG({
	width: frame.width,
	height: frame.height
});

frame.data.copy(png.data);

png.pack().pipe(fs.createWriteStream("test.png"));