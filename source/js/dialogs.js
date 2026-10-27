(function (root) {
  'use strict';
  function fields({ title, message = '', fields = [], submit = 'Save', secondaryActions = [] }) {
    return new Promise(resolve => {
      const dialog = document.createElement('dialog'); dialog.className = 'rtm-dialog';
      const form = document.createElement('form'); form.method = 'dialog';
      const heading = document.createElement('h2'); heading.textContent = title;
      heading.id = 'rtm-dialog-title'; dialog.setAttribute('aria-labelledby', heading.id);
      form.append(heading);
      if (message) { const paragraph = document.createElement('p'); paragraph.textContent = message; form.append(paragraph); }
      const controls = new Map();
      for (const field of fields) {
        const label = document.createElement('label'); label.textContent = field.label;
        const input = document.createElement(field.options ? 'select' : 'input');
        input.name = field.name;
        if (field.options) for (const option of field.options) {
          const element = document.createElement('option'); element.value = option.value; element.textContent = option.label; input.append(element);
        }
        else { input.type = field.type || 'text'; input.maxLength = field.maxLength || 200; input.step = 'any'; }
        input.value = field.value ?? field.options?.[0]?.value ?? ''; input.required = field.required !== false;
        label.append(input); form.append(label); controls.set(field.name, input);
      }
      const status = document.createElement('p');
      status.className = 'rtm-dialog-status';
      status.setAttribute('role', 'status');
      status.setAttribute('aria-live', 'polite');
      status.hidden = true;
      const actions = document.createElement('div'); actions.className = 'rtm-dialog-actions';
      const cancel = document.createElement('button'); cancel.type = 'button'; cancel.textContent = 'Cancel';
      const accept = document.createElement('button'); accept.type = 'submit'; accept.textContent = submit;
      let actionBusy = false;
      const secondaryButtons = [];
      for (const action of secondaryActions) {
        if (!action || typeof action.run !== 'function') continue;
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'rtm-dialog-secondary-action';
        button.textContent = String(action.label || 'Action');
        button.addEventListener('click', async () => {
          if (actionBusy || !dialog.open) return;
          actionBusy = true;
          for (const control of [cancel, accept, ...secondaryButtons]) control.disabled = true;
          status.hidden = true;
          try {
            await action.run({
              getField(name) { return controls.get(name)?.value ?? ''; },
              setField(name, value) {
                const control = controls.get(name);
                if (!control) throw new Error(`Unknown dialog field: ${name}`);
                control.value = String(value);
                control.setCustomValidity('');
              },
              setStatus(message, state = '') {
                status.textContent = String(message || '');
                status.hidden = !status.textContent;
                if (state) status.dataset.state = state;
                else delete status.dataset.state;
              }
            });
          } catch (error) {
            status.textContent = String(error?.message || error);
            status.dataset.state = 'error';
            status.hidden = false;
          } finally {
            actionBusy = false;
            if (dialog.open) for (const control of [cancel, accept, ...secondaryButtons]) control.disabled = false;
          }
        });
        secondaryButtons.push(button);
        actions.append(button);
      }
      actions.append(cancel, accept);
      if (secondaryButtons.length) form.append(status);
      form.append(actions); dialog.append(form); document.body.append(dialog);
      function finish(result) { dialog.close(); dialog.remove(); resolve(result); }
      cancel.onclick = () => { if (!actionBusy) finish(null); };
      dialog.addEventListener('cancel', event => { event.preventDefault(); if (!actionBusy) finish(null); });
      form.onsubmit = event => {
        event.preventDefault();
        if (actionBusy) return;
        for (const input of controls.values()) {
          input.setCustomValidity(input.required && !input.value.trim() ? 'Enter a value' : '');
        }
        if (form.reportValidity()) finish(Object.fromEntries([...controls].map(([name,input]) => [name,input.value.trim()])));
      };
      dialog.showModal();
      (controls.values().next().value || accept).focus();
    });
  }
  root.RtmDialogs = { fields, confirm: async message => (await fields({ title: 'Confirm action', message, submit: 'Continue' })) !== null };
})(globalThis);
