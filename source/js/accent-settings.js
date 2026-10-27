// Keep the chosen hue consistent while tuning it for readable text and controls
// against the dark and light surfaces.
(function() {
  const root = document.documentElement;
  const DEFAULT_ACCENT = '#c4b784';

  function luminance(rgb) {
    const linear = rgb.map(value => {
      const channel = value / 255;
      return channel <= 0.04045 ? channel / 12.92 : ((channel + 0.055) / 1.055) ** 2.4;
    });
    return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2];
  }

  function tuneAccent(rgb, target, lighten) {
    let amount = 0;
    let result = rgb;
    while (amount <= 1) {
      result = rgb.map(channel => Math.round(lighten
        ? channel + (255 - channel) * amount
        : channel * (1 - amount)));
      if (lighten ? luminance(result) >= target : luminance(result) <= target) break;
      amount += 0.01;
    }
    return result;
  }

  function toHex(rgb) {
    return `#${rgb.map(channel => channel.toString(16).padStart(2, '0')).join('')}`;
  }

  function withAlpha(hex, alpha) {
    const rgb = hex.match(/[0-9a-f]{2}/gi).map(value => parseInt(value, 16));
    return `rgba(${rgb[0]}, ${rgb[1]}, ${rgb[2]}, ${alpha})`;
  }

  function setAccent(color) {
    if (!/^#[0-9a-fA-F]{6}$/.test(color)) return;
    const rgb = color.slice(1).match(/.{2}/g).map(value => parseInt(value, 16));
    const darkAccent = toHex(tuneAccent(rgb, 0.22, true));
    const lightAccent = toHex(tuneAccent(rgb, 0.16, false));

    root.style.setProperty('--accent-choice', color.toLowerCase());
    root.style.setProperty('--accent', darkAccent);
    root.style.setProperty('--accent-soft', withAlpha(darkAccent, 0.09));
    root.style.setProperty('--accent-line', withAlpha(darkAccent, 0.28));
    root.style.setProperty('--accent-light', lightAccent);
    root.style.setProperty('--accent-soft-light', withAlpha(lightAccent, 0.08));
    root.style.setProperty('--accent-line-light', withAlpha(lightAccent, 0.28));
    try { localStorage.setItem('rtm_accent_color', color.toLowerCase()); } catch (e) {}
    const picker = document.getElementById('settings-accent');
    if (picker) picker.value = color;
  }

  window.setAccent = setAccent;
  let saved = null;
  try { saved = localStorage.getItem('rtm_accent_color'); } catch (e) {}
  setAccent(saved || DEFAULT_ACCENT);
})();
