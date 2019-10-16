const { DesktopDuplication } = require('../');

let monitors = DesktopDuplication.getMonitorCount();

console.log(`This PC has ${monitors} monitor${(monitors > 1) ? "s" : ""} connected to it.`);