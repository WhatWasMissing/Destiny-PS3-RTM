
  try {
    if ((localStorage.getItem('rtm_appearance') || 'dark') === 'light') {
      document.body.classList.add('light-mode');
    }
  } catch (e) {}
