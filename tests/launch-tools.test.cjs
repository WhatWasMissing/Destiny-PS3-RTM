const { test } = require('node:test');
const assert = require('node:assert/strict');
const { defaultPath, validateISOPath } = require('../source/js/launch-tools.js');

test('launch path accepts one ISO directly under webMAN PS3ISO', () => {
  assert.equal(defaultPath, '/dev_hdd0/PS3ISO/Destiny.iso');
  assert.equal(validateISOPath('/dev_hdd0/PS3ISO/Destiny One (EU).iso'), '/dev_hdd0/PS3ISO/Destiny One (EU).iso');
});

test('launch path rejects traversal, route injection and alternate directories', () => {
  for (const path of [
    '/dev_hdd0/PS3ISO/../play.ps3',
    '/dev_hdd0/PS3ISO/Destiny.iso&mode=fast',
    '/dev_hdd0/PS3ISO/Destiny.iso?next=/play.ps3',
    '/dev_hdd0/PS3ISO/subdir/Destiny.iso',
    '/dev_hdd0/GAMES/Destiny.iso'
  ]) assert.throws(() => validateISOPath(path));
});
