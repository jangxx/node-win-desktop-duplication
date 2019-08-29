const DesktopDuplicationNative = require('../build/Release/desktopduplication').DesktopDuplication;

class DesktopDuplication {
	constructor(screenNum) {
		this._dd = new DesktopDuplicationNative(screenNum);
	}

	initialize() {
		this._dd.initialize();
	}

	getFrame(retryCount = 5) {
		let res = this._dd.getFrame();

		switch (res.result) {
			case "error":
				throw new Error(res.error);
			case "timeout":
				if (retryCount > 0) {
					return this.getFrame(retryCount - 1); // try again
				} else {
					throw new Error("Timeout reached");
				}
			case "accesslost":
				if (retryCount > 0) {
					this.initialize(); // initialize again
					return this.getFrame(retryCount - 1); // try again
				} else {
					throw new Error("Access lost");
				}
			case "success":
				// check if the image is empty or all zeros, but only if we have retries left
				if (retryCount > 0) {
					if (res.data[0] + res.data[1] + res.data[2] + res.data[3] + res.data[4] + res.data[5] + res.data[6] + res.data[7] == 0) { // if the first two pixels are completely empty, we try again
						return this.getFrame(retryCount - 1);
					} else {
						return {
							data: res.data,
							width: res.width,
							height: res.height
						};
					}
				} else {
					return {
						data: res.data,
						width: res.width,
						height: res.height
					};
				}				
		}
	}

	getFrameAsync(retryCount = 5) {
		return new Promise(resolve => {
			this._dd.getFrameAsync(res => resolve(res));
		}).then(res => {
			switch (res.result) {
				case "error":
					throw new Error(res.error);
				case "timeout":
					if (retryCount > 0) {
						return this.getFrameAsync(retryCount - 1); // try again
					} else {
						throw new Error("Timeout reached");
					}
				case "accesslost":
					if (retryCount > 0) {
						this.initialize(); // initialize again
						return this.getFrameAsync(retryCount - 1); // try again
					} else {
						throw new Error("Access lost");
					}
				case "success":
					// check if the image is empty or all zeros, but only if we have retries left
					if (retryCount > 0) {
						if (res.data[0] + res.data[1] + res.data[2] + res.data[3] + res.data[4] + res.data[5] + res.data[6] + res.data[7] == 0) { // if the first two pixels are completely empty, we try again
							return this.getFrameAsync(retryCount - 1);
						} else {
							return {
								data: res.data,
								width: res.width,
								height: res.height
							};
						}
					} else {
						return {
							data: res.data,
							width: res.width,
							height: res.height
						};
					}				
			}
		});
	}
}

module.exports = DesktopDuplication;