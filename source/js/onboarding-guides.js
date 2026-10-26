
  document.addEventListener('DOMContentLoaded', () => {
    const welcome = document.getElementById('welcome-screen');
    const enterButton = document.getElementById('enter-rtm-btn');
    if (!welcome || !enterButton) return;

    enterButton.addEventListener('click', () => {
      welcome.classList.add('hidden');
      setTimeout(() => welcome.remove(), 400);
    });
  });
  // ── Activity Loader Guide + first-run gate ───────────────────────────────
  // ── First-run Server Cheats safety gate ─────────────────────────────────
  const SERVER_SAFETY_STORAGE_KEY = 'destinyRtmServerSafetyReadV1';
  const SERVER_SAFETY_MIN_READ_SECONDS = 10;
  let serverSafetyRequired = false;
  let serverSafetyAtBottom = false;
  let serverSafetyMinimumReadComplete = false;
  let serverSafetyReadUnlockAt = 0;
  let serverSafetyCountdownTimer = null;

  function hasReadServerSafetyGuide() {
    if (typeof isDebugModeEnabled === 'function' && isDebugModeEnabled()) return true;
    try { return localStorage.getItem(SERVER_SAFETY_STORAGE_KEY) === '1'; }
    catch (e) { return false; }
  }

  function setServerSafetyGuideRead() {
    try { localStorage.setItem(SERVER_SAFETY_STORAGE_KEY, '1'); } catch (e) {}
  }

  function setServerCheatsLocked(locked) {
    const section = document.getElementById('sec-server-cheats');
    if (section) section.classList.toggle('server-guide-locked', locked);
  }

  function updateServerSafetyAckButton() {
    if (!serverSafetyRequired) return;
    const ackBtn = document.getElementById('server-safety-ack');
    if (!ackBtn) return;

    const remaining = Math.max(0, Math.ceil((serverSafetyReadUnlockAt - Date.now()) / 1000));
    const timeReady = serverSafetyMinimumReadComplete || remaining <= 0;
    if (remaining <= 0) serverSafetyMinimumReadComplete = true;

    if (!serverSafetyAtBottom) {
      ackBtn.disabled = true;
      ackBtn.textContent = timeReady
        ? 'Scroll to the bottom to continue'
        : `Read the safety guide (${remaining}s) • then scroll to the bottom`;
      return;
    }

    if (!timeReady) {
      ackBtn.disabled = true;
      ackBtn.textContent = `Please keep reading... ${remaining}s`;
      return;
    }

    ackBtn.disabled = false;
    ackBtn.textContent = 'I understand the Server Cheats safety warning';
  }

  function checkServerSafetyScroll() {
    if (!serverSafetyRequired) return;
    const overlay = document.getElementById('server-safety-overlay');
    const modal = overlay ? overlay.querySelector('.server-safety-modal') : null;
    if (!modal) return;
    serverSafetyAtBottom = modal.scrollTop + modal.clientHeight >= modal.scrollHeight - 18;
    updateServerSafetyAckButton();
  }

  function startServerSafetyReadDelay() {
    if (!serverSafetyRequired) return;
    serverSafetyMinimumReadComplete = false;
    serverSafetyAtBottom = false;
    serverSafetyReadUnlockAt = Date.now() + SERVER_SAFETY_MIN_READ_SECONDS * 1000;

    if (serverSafetyCountdownTimer) clearInterval(serverSafetyCountdownTimer);
    updateServerSafetyAckButton();
    serverSafetyCountdownTimer = setInterval(() => {
      updateServerSafetyAckButton();
      if (Date.now() >= serverSafetyReadUnlockAt) {
        serverSafetyMinimumReadComplete = true;
        clearInterval(serverSafetyCountdownTimer);
        serverSafetyCountdownTimer = null;
        updateServerSafetyAckButton();
      }
    }, 250);
  }

  function updateServerSafetyUI() {
    const closeBtn = document.getElementById('server-safety-close-btn');
    const ackBtn = document.getElementById('server-safety-ack');
    if (closeBtn) {
      closeBtn.disabled = serverSafetyRequired;
      closeBtn.style.opacity = serverSafetyRequired ? '0.35' : '';
      closeBtn.style.pointerEvents = serverSafetyRequired ? 'none' : '';
    }
    if (ackBtn) ackBtn.style.display = serverSafetyRequired ? '' : 'none';
  }

  function openServerSafetyGuide() {
    const overlay = document.getElementById('server-safety-overlay');
    if (!overlay) return;
    overlay.classList.add('show');
    updateServerSafetyUI();
    if (serverSafetyRequired) {
      const modal = overlay.querySelector('.server-safety-modal');
      if (modal) modal.scrollTop = 0;
      startServerSafetyReadDelay();
      requestAnimationFrame(checkServerSafetyScroll);
    }
  }

  function closeServerSafetyGuide(event) {
    const overlay = document.getElementById('server-safety-overlay');
    if (!overlay || serverSafetyRequired) return;
    if (!event || event.target === overlay || event.target.closest('.server-safety-close')) {
      overlay.classList.remove('show');
    }
  }

  function acknowledgeServerSafetyGuide() {
    if (!serverSafetyRequired) return;
    const ackBtn = document.getElementById('server-safety-ack');
    if (!ackBtn || ackBtn.disabled) return;

    if (serverSafetyCountdownTimer) {
      clearInterval(serverSafetyCountdownTimer);
      serverSafetyCountdownTimer = null;
    }
    setServerSafetyGuideRead();
    serverSafetyRequired = false;
    setServerCheatsLocked(false);
    updateServerSafetyUI();
    const overlay = document.getElementById('server-safety-overlay');
    if (overlay) overlay.classList.remove('show');
    if (typeof toast === 'function') toast('Server Cheats unlocked', 'ok');

    // Continue to the Activity Loader guide if it is also required.
    const loaderEnabled = typeof secVisible !== 'undefined' && secVisible['activity-loader-panel'] !== false;
    if (loaderEnabled && typeof hasReadActivityGuide === 'function' && !hasReadActivityGuide()) {
      activityGuideRequired = true;
      setActivityLoaderLocked(true);
      setTimeout(openActivityGuide, 120);
    }
  }

  window.openServerSafetyGuide = openServerSafetyGuide;
  window.closeServerSafetyGuide = closeServerSafetyGuide;
  window.acknowledgeServerSafetyGuide = acknowledgeServerSafetyGuide;

  document.addEventListener('keydown', function(e) {
    if (e.key === 'Escape') closeServerSafetyGuide();
  });

  document.addEventListener('DOMContentLoaded', function() {
    serverSafetyRequired = !hasReadServerSafetyGuide() && !(typeof isDebugModeEnabled === 'function' && isDebugModeEnabled());
    setServerCheatsLocked(serverSafetyRequired);
    updateServerSafetyUI();

    const overlay = document.getElementById('server-safety-overlay');
    const modal = overlay ? overlay.querySelector('.server-safety-modal') : null;
    if (modal) modal.addEventListener('scroll', checkServerSafetyScroll, { passive: true });

    // Existing users with Server Cheats enabled must acknowledge the new guide.
    if (isToolSetupComplete() && serverSafetyRequired && secVisible['sec-server-cheats'] !== false) {
      const enterButton = document.getElementById('enter-rtm-btn');
      if (enterButton) {
        enterButton.addEventListener('click', () => setTimeout(openServerSafetyGuide, 300), { once: true });
      } else {
        setTimeout(openServerSafetyGuide, 80);
      }
    }
  });

  const ACTIVITY_GUIDE_STORAGE_KEY = 'destinyRtmActivityGuideReadV1';
  let activityGuideRequired = false;
  let activityGuideMinimumReadComplete = false;
  let activityGuideAtBottom = false;
  let activityGuideCountdownTimer = null;
  const ACTIVITY_GUIDE_MIN_READ_SECONDS = 10;

  function hasReadActivityGuide() {
    if (typeof isDebugModeEnabled === 'function' && isDebugModeEnabled()) return true;
    try {
      return localStorage.getItem(ACTIVITY_GUIDE_STORAGE_KEY) === '1';
    } catch (e) {
      return false;
    }
  }

  function setActivityGuideRead() {
    try {
      localStorage.setItem(ACTIVITY_GUIDE_STORAGE_KEY, '1');
    } catch (e) {
      // If localStorage is unavailable, unlock for this page session only.
    }
  }

  function setActivityLoaderLocked(locked) {
    const panel = document.getElementById('activity-loader-panel');
    if (panel) panel.classList.toggle('guide-locked', locked);
  }

  function updateActivityGuideGateUI() {
    const overlay = document.getElementById('activity-guide-overlay');
    const closeBtn = document.getElementById('activity-guide-close-btn');
    const ackBtn = document.getElementById('activity-guide-ack');
    const modal = overlay ? overlay.querySelector('.activity-guide-modal') : null;

    if (overlay) overlay.classList.toggle('first-run-required', activityGuideRequired);
    if (closeBtn) {
      closeBtn.disabled = activityGuideRequired;
      closeBtn.style.opacity = activityGuideRequired ? '0.35' : '';
      closeBtn.style.pointerEvents = activityGuideRequired ? 'none' : '';
    }

    if (!activityGuideRequired && ackBtn) {
      ackBtn.disabled = false;
      ackBtn.textContent = 'Guide already acknowledged';
      ackBtn.style.display = 'none';
    }

    if (activityGuideRequired && modal && ackBtn) {
      ackBtn.style.display = '';
      ackBtn.disabled = true;
      modal.scrollTop = 0;
      activityGuideAtBottom = false;
      requestAnimationFrame(checkActivityGuideScroll);
    }
  }

  function updateActivityGuideAckButton() {
    if (!activityGuideRequired) return;
    const ackBtn = document.getElementById('activity-guide-ack');
    if (!ackBtn) return;

    const remaining = Math.max(0, Math.ceil((activityGuideReadUnlockAt - Date.now()) / 1000));
    const timeReady = activityGuideMinimumReadComplete || remaining <= 0;
    if (remaining <= 0) activityGuideMinimumReadComplete = true;

    if (!activityGuideAtBottom) {
      ackBtn.disabled = true;
      ackBtn.textContent = timeReady
        ? 'Scroll to the bottom to continue'
        : `Read the guide (${remaining}s) • then scroll to the bottom`;
      return;
    }

    if (!timeReady) {
      ackBtn.disabled = true;
      ackBtn.textContent = `Please keep reading... ${remaining}s`;
      return;
    }

    ackBtn.disabled = false;
    ackBtn.textContent = 'I have read the Activity Loader Guide';
  }

  let activityGuideReadUnlockAt = 0;

  function startActivityGuideReadDelay() {
    if (!activityGuideRequired) return;
    activityGuideMinimumReadComplete = false;
    activityGuideAtBottom = false;
    activityGuideReadUnlockAt = Date.now() + (ACTIVITY_GUIDE_MIN_READ_SECONDS * 1000);

    if (activityGuideCountdownTimer) clearInterval(activityGuideCountdownTimer);
    updateActivityGuideAckButton();
    activityGuideCountdownTimer = setInterval(() => {
      updateActivityGuideAckButton();
      if (Date.now() >= activityGuideReadUnlockAt) {
        activityGuideMinimumReadComplete = true;
        clearInterval(activityGuideCountdownTimer);
        activityGuideCountdownTimer = null;
        updateActivityGuideAckButton();
      }
    }, 250);
  }

  function openActivityGuide() {
    const overlay = document.getElementById('activity-guide-overlay');
    if (!overlay) return;
    overlay.classList.add('show');
    updateActivityGuideGateUI();
    if (activityGuideRequired) startActivityGuideReadDelay();
  }

  function closeActivityGuide(event) {
    const overlay = document.getElementById('activity-guide-overlay');
    if (!overlay || activityGuideRequired) return;
    if (!event || event.target === overlay || event.target.closest('.activity-guide-close')) {
      overlay.classList.remove('show');
    }
  }

  function checkActivityGuideScroll() {
    if (!activityGuideRequired) return;
    const overlay = document.getElementById('activity-guide-overlay');
    const modal = overlay ? overlay.querySelector('.activity-guide-modal') : null;
    if (!modal) return;

    activityGuideAtBottom = modal.scrollTop + modal.clientHeight >= modal.scrollHeight - 18;
    updateActivityGuideAckButton();
  }

  function acknowledgeActivityGuide() {
    if (!activityGuideRequired) return;
    const ackBtn = document.getElementById('activity-guide-ack');
    if (!ackBtn || ackBtn.disabled) return;

    if (activityGuideCountdownTimer) {
      clearInterval(activityGuideCountdownTimer);
      activityGuideCountdownTimer = null;
    }
    setActivityGuideRead();
    activityGuideRequired = false;
    setActivityLoaderLocked(false);
    updateActivityGuideGateUI();

    const overlay = document.getElementById('activity-guide-overlay');
    if (overlay) overlay.classList.remove('show');
    if (typeof toast === 'function') toast('Activity Loader unlocked', 'ok');
  }

  document.addEventListener('keydown', function(e) {
    if (e.key === 'Escape') closeActivityGuide();
  });

  document.addEventListener('DOMContentLoaded', function() {
    activityGuideRequired = !hasReadActivityGuide() && !(typeof isDebugModeEnabled === 'function' && isDebugModeEnabled());
    setActivityLoaderLocked(activityGuideRequired);

    const overlay = document.getElementById('activity-guide-overlay');
    const modal = overlay ? overlay.querySelector('.activity-guide-modal') : null;
    if (modal) modal.addEventListener('scroll', checkActivityGuideScroll, { passive: true });

    updateActivityGuideGateUI();

    const enterButton = document.getElementById('enter-rtm-btn');
    if (!isToolSetupComplete()) {
      // First run: section selection comes first. If Activity Loader is chosen,
      // saveToolSetup() opens the mandatory guide afterwards.
      if (enterButton) enterButton.addEventListener('click', () => setTimeout(showToolSetup, 450), { once: true });
      else setTimeout(showToolSetup, 50);
      return;
    }

    const loaderEnabled = secVisible['activity-loader-panel'] !== false;
    const serverGuideWillRunFirst =
      secVisible['sec-server-cheats'] !== false &&
      typeof hasReadServerSafetyGuide === 'function' &&
      !hasReadServerSafetyGuide();

    if (activityGuideRequired && loaderEnabled && !serverGuideWillRunFirst) {
      if (enterButton) {
        enterButton.addEventListener('click', () => setTimeout(openActivityGuide, 450), { once: true });
      } else {
        setTimeout(openActivityGuide, 50);
      }
    }
  });

