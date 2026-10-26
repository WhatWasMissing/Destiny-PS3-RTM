

  // ── Merged Gemini notification console ────────────────────────────────────
  const notifMsg = document.getElementById('notifMsg');
  const notifPreviewText = document.getElementById('notifPreviewText');
  const notifStatus = document.getElementById('notifStatus');
  const notifApiMode = document.getElementById('notifApiMode');

  if (notifMsg) {
    notifMsg.addEventListener('input', () => {
      notifPreviewText.textContent = notifMsg.value.trim() || 'Waiting for message input...';
    });
  }

  async function sendNotification() {
    const messageText = (notifMsg?.value || '').trim();
    if (!ip) {
      notifStatus.textContent = 'Target: Offline';
      notifStatus.className = 'notif-status err';
      toast('Not connected', 'err');
      return;
    }
    if (!messageText) {
      notifStatus.textContent = 'Message payload cannot be empty';
      notifStatus.className = 'notif-status err';
      toast('Enter a message', 'err');
      return;
    }

    const mode = notifApiMode?.value || 'ps3mapi';
    const title = (document.getElementById('notifTitle')?.value || 'Destiny RTM').trim() || 'Destiny RTM';
    const encoded = encodeURIComponent(messageText);

    notifStatus.textContent = 'Sending...';
    notifStatus.className = 'notif-status';

    try {
      if (mode === 'destiny') {
        // Experimental reproduction of the original soulsAPI toastPrompt sequence.
        // This is NOT the documented webMAN notification API. We only report that
        // the HTTP requests were dispatched; the Destiny UI must be verified in-game.
        await ps3call('750540', '268632064');
        await writeUtf16('10030200', title);
        await writeUtf16('10030400', messageText);
        await ps3call('748714', '28508392', '28507992', '268632064', '7', '0');
        notifStatus.textContent = 'RTM toast request dispatched';
        notifStatus.className = 'notif-status ok';
        log(`Destiny toast sequence dispatched: title="${title}" body="${messageText}"`, 'ok');
        toast('Destiny toast request dispatched', 'ok');
      } else {
        const endpoint = mode === 'webman'
          ? `http://${ip}/popup.ps3/${encoded}`
          : `http://${ip}:8080/notify.ps3mapi?msg=${encoded}`;
        if (mode === 'webman') {
          await fetch(endpoint, { mode: 'no-cors' });
        } else {
          await ps3fetch(endpoint);
        }
        notifStatus.textContent = 'Sent ';
        notifStatus.className = 'notif-status ok';
        log(`Notification sent via ${mode}: "${messageText}"`, 'ok');
        toast('Notification sent ', 'ok');
      }
      setTimeout(() => {
        if (notifStatus) {
          notifStatus.textContent = 'Target: Connected console';
          notifStatus.className = 'notif-status';
        }
      }, 2000);
    } catch (err) {
      console.error('Notification transmission error:', err);
      notifStatus.textContent = 'Send Failed';
      notifStatus.className = 'notif-status err';
      log('Notification failed', 'err');
      toast('Notification failed', 'err');
    }
  }


