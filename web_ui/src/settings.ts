export type Settings = {
    theme: 'light' | 'dark' | 'system';
    plotFps: number;
    streamHz: number;
    statusSeconds: number;
    showPerformance: boolean;
};
export const defaults: Settings = { theme: 'light', plotFps: 60, streamHz: 50, statusSeconds: 1, showPerformance: false };
export function readSettings(raw: string | null): Settings {
    try {
        const s = JSON.parse(raw ?? '{}');
        return {
            theme: ['light', 'dark', 'system'].includes(s.theme) ? s.theme : defaults.theme,
            plotFps: [15, 30, 60].includes(s.plotFps) ? s.plotFps : defaults.plotFps,
            streamHz: [10, 25, 50].includes(s.streamHz) ? s.streamHz : defaults.streamHz,
            statusSeconds: [1, 2, 5].includes(s.statusSeconds) ? s.statusSeconds : defaults.statusSeconds,
            showPerformance: s.showPerformance === true,
        };
    }
    catch {
        return { ...defaults };
    }
}
