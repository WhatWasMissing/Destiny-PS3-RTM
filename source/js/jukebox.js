(function () {
  'use strict';

  // Ported from the source RTM's PS3 Jukebox data. Each pair is [tone, pause ms].
  const sequences = Object.freeze({
    fanfare: {
      name: 'Victory Fanfare',
      notes: [[1,180],[1,180],[1,180],[2,400],[1,180],[2,120],[1,400],[1,180],[1,180],[1,180],[2,400],[1,180],[2,120],[1,600]]
    },
    imperial: {
      name: 'Imperial March',
      notes: [[2,350],[2,350],[2,350],[3,500],[1,200],[2,500],[2,350],[2,350],[2,350],[3,500],[1,200],[2,600]]
    },
    mario: {
      name: 'Super Mario',
      notes: [[1,150],[1,300],[1,150],[1,150],[1,150],[2,150],[1,150],[2,300],[1,150],[1,300],[1,150],[1,150],[1,150],[2,150],[1,150],[2,400]]
    },
    nokia: {
      name: 'Nokia Tune',
      notes: [[1,150],[2,150],[1,150],[2,300],[1,150],[2,150],[1,300],[2,150],[1,150],[2,150],[1,300],[2,150],[1,150],[2,150],[1,400]]
    },
    sos: {
      name: 'SOS',
      notes: [[1,120],[1,120],[1,300],[3,300],[3,300],[3,500],[1,120],[1,120],[1,400]]
    },
    shave: {
      name: 'Shave & Haircut',
      notes: [[1,180],[2,180],[2,180],[2,180],[1,350],[2,180],[1,500]]
    }
  });

  let activeRun = null;

  function setStatus(message, state = '') {
    const status = document.getElementById('jukebox-status');
    if (status) {
      status.textContent = message;
      status.dataset.state = state;
    }
  }

  function setCount(message) {
    const count = document.getElementById('jukebox-count');
    if (count) count.textContent = message;
  }

  function setProgress(value, max) {
    const progress = document.getElementById('jukebox-progress');
    if (!progress) return;
    progress.max = Math.max(1, max || 1);
    progress.value = Math.max(0, Math.min(value, progress.max));
  }

  function setControls(playing, stopping = false) {
    document.querySelectorAll('[data-jukebox-song]').forEach(button => {
      button.disabled = playing;
    });
    const stop = document.getElementById('jukebox-stop');
    if (stop) {
      stop.disabled = !playing || stopping;
      stop.textContent = stopping ? 'Stopping…' : 'Stop sequence';
    }
  }

  function sessionChanged(run) {
    return run.generation !== connectionGeneration || run.target !== ip;
  }

  function waitForPause(ms, run) {
    return new Promise(resolve => {
      const timer = setTimeout(() => {
        run.releasePause = null;
        resolve();
      }, ms);
      run.releasePause = () => {
        clearTimeout(timer);
        run.releasePause = null;
        resolve();
      };
    });
  }

  async function playJukeboxSong(key) {
    const sequence = sequences[key];
    if (!sequence || activeRun) return;
    const target = String(ip || '').trim();
    if (!target) {
      toast('Connect to a PS3 before playing a sequence', 'err');
      return;
    }

    const run = {
      target,
      generation: connectionGeneration,
      stopRequested: false,
      releasePause: null,
      completed: 0,
      sequence
    };
    activeRun = run;
    setControls(true);
    setProgress(0, sequence.notes.length);
    setCount(`0 / ${sequence.notes.length}`);
    setStatus(`Playing ${sequence.name}`, 'playing');
    log(`Playing PS3 tone sequence: ${sequence.name}`, '');

    let failure = null;
    let connectionWasChanged = false;
    try {
      for (let index = 0; index < sequence.notes.length; index += 1) {
        if (run.stopRequested) break;
        if (sessionChanged(run)) {
          connectionWasChanged = true;
          break;
        }

        const [tone, pause] = sequence.notes[index];
        setStatus(`Playing ${sequence.name}`, 'playing');
        setCount(`${index + 1} / ${sequence.notes.length}`);
        try {
          await consoleTransport.request(`http://${target}/beep.ps3?${tone}`, 8000);
        } catch (error) {
          if (sessionChanged(run)) connectionWasChanged = true;
          else failure = error;
          break;
        }

        run.completed = index + 1;
        setProgress(run.completed, sequence.notes.length);
        if (run.stopRequested || sessionChanged(run)) {
          connectionWasChanged = !run.stopRequested;
          break;
        }

        // Match the source sequence timing, including the pause after the final tone.
        await waitForPause(pause, run);
      }
    } finally {
      if (activeRun === run) activeRun = null;
      setControls(false);
    }

    if (connectionWasChanged) {
      setStatus('Connection changed; sequence stopped.', 'error');
      setCount(`${run.completed} / ${sequence.notes.length}`);
      log(`PS3 tone sequence stopped after the console connection changed: ${sequence.name}`, 'err');
    } else if (failure) {
      setStatus('Tone request failed; sequence stopped.', 'error');
      setCount(`${run.completed} / ${sequence.notes.length}`);
      log(`PS3 tone sequence failed: ${failure.message}`, 'err');
      toast('Could not play the tone sequence', 'err');
    } else if (run.stopRequested) {
      setStatus(`${sequence.name} stopped.`, '');
      setCount(`${run.completed} / ${sequence.notes.length}`);
      log(`PS3 tone sequence stopped: ${sequence.name}`, '');
    } else {
      setStatus(`${sequence.name} complete.`, 'success');
      setCount(`${sequence.notes.length} / ${sequence.notes.length}`);
      log(`PS3 tone sequence complete: ${sequence.name}`, '');
    }
  }

  function stopJukeboxSong() {
    if (!activeRun || activeRun.stopRequested) return;
    activeRun.stopRequested = true;
    if (activeRun.releasePause) activeRun.releasePause();
    setStatus('Stopping after the current system beep request…', 'playing');
    setControls(true, true);
  }

  window.playJukeboxSong = playJukeboxSong;
  window.stopJukeboxSong = stopJukeboxSong;
})();
