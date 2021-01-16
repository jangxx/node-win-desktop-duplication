import { EventEmitter } from 'events';

/** Represents the image captured from screen. */
export declare interface Frame {
    /** Buffer with the raw pixel values in RGBA order. */
    data: Buffer,
    /** Width of the captured frame. */
    width: number,
    /** Height of the captured frame. */
    height: number
}

/** A native addon to use the Windows Desktop Duplication API. */
export declare class DesktopDuplication extends EventEmitter {
    /** Static method to get the number of available monitors. */
    static getMonitorCount(): number;

    /** 
     * Creates a new instance for the screen `screenNum`.  
     * Use the `getMonitorCount()` method to get the number of available screens.
     */
    constructor(screenNum: number);

    /**
     * Setup the required DirectX objects.  
     * Use a try/catch block to catch errors in the initialization process.
     */
    initialize(): void;

    /**
     * Synchronously gets a single frame in the default format.  
     * If the procedure fails, retry up to `retryCount` times (default: 5).  
     * If there was no image captured after all retries are used up, this method throws an error.
     */
    getFrame(retryCount?: number): Frame;

    /**
     * Like `getFrame`, but returning a promise instead, which resolves to image data.  
     * The capture and image processing is also run in a separate thread for better performance.
     */
    getFrameAsync(retryCount?: number): Promise<Frame>;

    /**
     * Starts a new thread, which tries to capture the screen every `delay` milliseconds.  
     * Image data is then emitted as an **frame** event.  
     * This method functions similar to `setInterval(() => dd.getFrameAsync().then(frame => emit("frame", frame)), delay)`, but with the added bonus of all the timing stuff happening in native code and a separate thread, which improves the performance.  
     * **Note**: You can only have one of these threads running at any time, so subsequent calls to `startAutoCapture` without stopping the auto capture in between have no effect.  
     * The optional parameter `allowSkips` controls how the thread queues up the **frame** events.  
     * If the event did not have a chance to fire before the next image is captured, it can either be queued up (`allowSkips = false`) or just be thrown away (`allowSkips = true`, default).
     */
    startAutoCapture(delay: number, allowSkips?: boolean): void;

    /**
     * Stops the auto capture thread.  
     * By default, no futher **frame** events will be emitted after this method has been called, since `clearBacklog` is `true` by default.  
     * If you want to process every captured frame however, set `clearBacklog` to `false`.
     */
    stopAutoCapture(clearBacklog?: boolean): void;

    addListener(event: "frame", listener: (frame: Frame) => void): this;
    addListener(event: string | symbol, listener: (...args: any[]) => void): this;

    on(event: "frame", listener: (frame: Frame) => void): this;
    on(event: string | symbol, listener: (...args: any[]) => void): this;

    once(event: "frame", listener: (frame: Frame) => void): this;
    once(event: string | symbol, listener: (...args: any[]) => void): this;

    removeListener(event: "frame", listener: (frame: Frame) => void): this;
    removeListener(event: string | symbol, listener: (...args: any[]) => void): this;

    off(event: "frame", listener: (frame: Frame) => void): this;
    off(event: string | symbol, listener: (...args: any[]) => void): this;
}
