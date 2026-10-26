(function (root) {
  'use strict';
  function fields({ title, message = '', fields = [], submit = 'Save' }) {
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
      const actions = document.createElement('div'); actions.className = 'rtm-dialog-actions';
      const cancel = document.createElement('button'); cancel.type = 'button'; cancel.textContent = 'Cancel';
      const accept = document.createElement('button'); accept.type = 'submit'; accept.textContent = submit;
      actions.append(cancel, accept); form.append(actions); dialog.append(form); document.body.append(dialog);
      function finish(result) { dialog.close(); dialog.remove(); resolve(result); }
      cancel.onclick = () => finish(null);
      dialog.addEventListener('cancel', event => { event.preventDefault(); finish(null); });
      form.onsubmit = event => {
        event.preventDefault();
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
