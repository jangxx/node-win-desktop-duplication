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

dd.startAutoCapture(100);

let frameCount = 0;
let pngs = [];

dd.on("frame", frame => {
	let png = new PNG({
		width: frame.width,
		height: frame.height
	});
	
	frame.data.copy(png.data);
	
	pngs.push(png);

	frameCount++;
});

setTimeout(() => {
	dd.stopAutoCapture();

	console.log("Captured", frameCount, "frames");

	for(let p in pngs) {
		pngs[p].pack().pipe(fs.createWriteStream(`test_frame_${p}.png`));
	}
}, 2000);