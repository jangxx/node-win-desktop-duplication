const dd = require('./build/Release/desktopduplication');

let DeskDup = new dd.DesktopDuplication(0);

try {
	DeskDup.initialize();
} catch(e) {
	console.log(e.message);
	process.exit();
}

let frame = DeskDup.getFrame();

console.log(frame);