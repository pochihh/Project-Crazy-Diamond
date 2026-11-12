export function numberText(value) {
    return Math.abs(value) < 1e-12 ? '0' : String(value);
}

export function referenceNote(controllers, source, period, amplitudes) {
    const lines = [];
    let limited = false;
    const actualPeriod = Math.round(period * 2000) / 2000;
    for (let axis = 0; axis < 2; axis++) {
        const controller = controllers?.[axis];
        if (!controller) continue;
        let line = `Axis ${axis}: ${controller.slew_enabled ? `slew ${controller.slew_rate_mm_s} mm/s` : 'passthrough / no shaping'}`;
        if (source === 'sine' && actualPeriod > 0 && Number.isFinite(amplitudes[axis])) {
            const peak = 2 * Math.PI * Math.abs(amplitudes[axis]) / actualPeriod;
            line += `; sine peak ${peak.toFixed(1)} mm/s`;
            if (controller.slew_enabled && peak > controller.slew_rate_mm_s) limited = true;
        }
        lines.push(line + '.');
    }
    if (limited) lines.push('The active limiter will reshape this sine. For an unshaped reference, disable the axis and select Passthrough / no shaping in Controller Settings.');
    else if (controllers?.slice(0, 2).some(c => c.slew_enabled)) lines.push('Slew limiting also applies to the initial move. Passthrough is available in each axis’s Controller Settings while disabled.');
    return {text: lines.join(' '), limited};
}
