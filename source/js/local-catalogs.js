(function () {
  'use strict';

  const MAX_FILES = 8;
  const MAX_FILE_BYTES = 4 * 1024 * 1024;
  const MAX_TOTAL_BYTES = 8 * 1024 * 1024;
  const MAX_ROWS_PER_FILE = 8000;
  const MAX_TOTAL_ROWS = 10000;
  const MAX_COLUMNS = 24;
  const MAX_CELL_CHARS = 200;
  const MAX_RENDER_ROWS = 100;
  const MAX_RENDER_COLUMNS = 10;
  // The verified BLES 01.24 item array contains indexes 0 through 6556.
  const MAX_BLES_0124_ITEM_INDEX = 6556;

  const catalogs = [];
  let loadedBytes = 0;
  let loadedRows = 0;

  function byId(id) { return document.getElementById(id); }

  function formatBytes(bytes) {
    return bytes < 1024 * 1024
      ? `${Math.max(1, Math.round(bytes / 1024))} KB`
      : `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
  }

  function formatCell(value) {
    if (value === null || value === undefined) return '';
    let text;
    if (typeof value === 'string') text = value;
    else if (typeof value === 'number' || typeof value === 'boolean') text = String(value);
    else {
      try { text = JSON.stringify(value); }
      catch (_) { text = '[unavailable]'; }
    }
    return text.length > MAX_CELL_CHARS ? `${text.slice(0, MAX_CELL_CHARS)}…` : text;
  }

  function parseDelimited(text, delimiter, rowLimit) {
    const rows = [];
    let row = [];
    let cell = '';
    let quoted = false;
    let fieldStarted = false;
    let truncated = false;
    if (text.charCodeAt(0) === 0xFEFF) text = text.slice(1);

    const pushCell = () => {
      if (row.length < MAX_COLUMNS) row.push(cell);
      cell = '';
      fieldStarted = false;
    };
    const pushRow = (position) => {
      pushCell();
      if (row.some(value => value.length) || row.length > 1) rows.push(row);
      row = [];
      if (rows.length >= rowLimit && text.slice(position + 1).trim().length) {
        truncated = true;
        return true;
      }
      return false;
    };

    for (let i = 0; i < text.length; i++) {
      const char = text[i];
      if (quoted) {
        if (char === '"') {
          if (text[i + 1] === '"') {
            if (cell.length < MAX_CELL_CHARS) cell += '"';
            i++;
          } else quoted = false;
        } else if (cell.length < MAX_CELL_CHARS) cell += char;
        continue;
      }
      if (char === '"' && !fieldStarted && cell.length === 0) {
        quoted = true;
        fieldStarted = true;
      } else if (char === delimiter) {
        pushCell();
      } else if (char === '\n' || char === '\r') {
        if (char === '\r' && text[i + 1] === '\n') i++;
        if (pushRow(i)) break;
      } else {
        fieldStarted = true;
        if (cell.length < MAX_CELL_CHARS) cell += char;
      }
    }

    if (quoted) throw new Error('The delimited file has an unfinished quoted field.');
    if (!truncated && (cell.length || row.length)) {
      pushCell();
      if (row.some(value => value.length) || row.length > 1) rows.push(row);
      if (rows.length > rowLimit) {
        rows.length = rowLimit;
        truncated = true;
      }
    }
    if (!rows.length) throw new Error('No rows were found in the file.');
    return { rows, truncated };
  }

  function uniqueHeaders(values) {
    const used = new Set();
    return values.slice(0, MAX_COLUMNS).map((value, index) => {
      const base = String(value || '').trim().slice(0, 80) || `Column ${index + 1}`;
      let header = base;
      let suffix = 2;
      while (used.has(header.toLocaleLowerCase())) header = `${base} (${suffix++})`;
      used.add(header.toLocaleLowerCase());
      return header;
    });
  }

  function skipLeadingCommentLines(text) {
    if (text.charCodeAt(0) === 0xFEFF) text = text.slice(1);
    const lines = text.split(/\r\n|\n|\r/);
    while (lines.length && (!lines[0].trim() || lines[0].trimStart().startsWith('#'))) lines.shift();
    return lines.join('\n');
  }

  function parseTable(text, delimiter, rowLimit) {
    const parsed = parseDelimited(skipLeadingCommentLines(text), delimiter, rowLimit + 1);
    const headers = uniqueHeaders(parsed.rows[0]);
    if (!headers.length) throw new Error('The file needs a header row.');
    const sourceRows = parsed.rows.slice(1);
    if (!sourceRows.length) throw new Error('The file has headers but no catalog records.');
    const rows = sourceRows.slice(0, rowLimit).map(values => {
      const record = new Map();
      headers.forEach((header, index) => record.set(header, values[index] || ''));
      return record;
    });
    return { columns: headers, rows, truncated: parsed.truncated || sourceRows.length > rowLimit };
  }

  function parseJson(text, rowLimit) {
    if (text.charCodeAt(0) === 0xFEFF) text = text.slice(1);
    let parsed;
    try { parsed = JSON.parse(text); }
    catch (_) { throw new Error('The JSON file could not be parsed.'); }

    let sourceRows;
    if (Array.isArray(parsed)) sourceRows = parsed;
    else if (parsed && typeof parsed === 'object') {
      const listKey = ['rows', 'items', 'data', 'entries', 'records', 'catalog']
        .find(key => Array.isArray(parsed[key]));
      sourceRows = listKey ? parsed[listKey] : [parsed];
    } else throw new Error('JSON must contain an array of catalog rows or one record object.');
    if (!sourceRows.length) throw new Error('The JSON catalog contains no rows.');

    const columns = [];
    const columnNames = new Map();
    const records = [];
    const limit = Math.min(rowLimit, sourceRows.length);
    const getColumn = (rawName, ordinal) => {
      const raw = String(rawName || '').slice(0, 160);
      if (columnNames.has(raw)) return columnNames.get(raw);
      if (columns.length >= MAX_COLUMNS) return null;
      const base = raw.trim().slice(0, 80) || `Column ${ordinal + 1}`;
      let name = base;
      let suffix = 2;
      while (columns.some(existing => existing.toLocaleLowerCase() === name.toLocaleLowerCase())) {
        name = `${base} (${suffix++})`;
      }
      columnNames.set(raw, name);
      columns.push(name);
      return name;
    };

    for (let index = 0; index < limit; index++) {
      const source = sourceRows[index];
      const record = new Map();
      if (Array.isArray(source)) {
        source.slice(0, MAX_COLUMNS).forEach((value, column) => {
          const name = getColumn(`Column ${column + 1}`, column);
          if (name) record.set(name, formatCell(value));
        });
      } else if (source && typeof source === 'object') {
        Object.entries(source).slice(0, MAX_COLUMNS).forEach(([key, value], column) => {
          const name = getColumn(key, column);
          if (name) record.set(name, formatCell(value));
        });
      } else {
        const name = getColumn('Value', 0);
        if (name) record.set(name, formatCell(source));
      }
      records.push(record);
    }
    if (!columns.length) throw new Error('No displayable fields were found in the JSON catalog.');
    return { columns, rows: records, truncated: sourceRows.length > limit };
  }

  function parseTextCatalog(text, rowLimit) {
    if (text.charCodeAt(0) === 0xFEFF) text = text.slice(1);
    const lines = text.split(/\r\n|\n|\r/).map(line => line.trim()).filter(Boolean);
    if (!lines.length) throw new Error('No catalog entries were found in the text file.');
    const limit = Math.min(rowLimit, lines.length);
    const rows = lines.slice(0, limit).map(line => new Map([['Entry', formatCell(line)]]));
    return { columns: ['Entry'], rows, truncated: lines.length > limit };
  }

  function categoryFor(fileName, selected) {
    if (selected !== 'auto') return selected;
    const name = fileName.toLocaleLowerCase();
    if (name.includes('activity') || name.includes('mission')) return 'activity';
    if (name.includes('spawn') || name.includes('hop') || name.includes('encounter') || name.includes('squad')) return 'spawn-hopon';
    if (name.includes('item') || name.includes('loot') || name.includes('weapon') || name.includes('perk')) return 'item-loot';
    if (name.includes('world') || name.includes('global') || name.includes('pool')) return 'world-data';
    return 'general';
  }

  function normalizedHeader(value) {
    return String(value || '').toLocaleLowerCase().replace(/[^a-z0-9]/g, '');
  }

  function buildItemNameMap(category, columns, rows) {
    if (category !== 'item-loot') return null;
    const indexColumn = columns.find(column => ['itemindex', 'blesitemindex'].includes(normalizedHeader(column)));
    const nameColumn = columns.find(column => ['name', 'itemname', 'displayname'].includes(normalizedHeader(column)));
    if (!indexColumn || !nameColumn) return null;

    const names = new Map();
    for (const row of rows) {
      const rawIndex = (row.get(indexColumn) || '').trim();
      if (!/^\d{1,5}$/.test(rawIndex)) continue;
      const unsignedIndex = Number(rawIndex);
      if (!Number.isInteger(unsignedIndex) || unsignedIndex < 0 || unsignedIndex > MAX_BLES_0124_ITEM_INDEX) continue;
      const name = (row.get(nameColumn) || '').trim();
      if (!name) continue;
      if (!names.has(unsignedIndex)) names.set(unsignedIndex, name);
    }
    return names;
  }

  function localItemName(itemIndex) {
    if (!Number.isInteger(itemIndex)) return '';
    for (let index = catalogs.length - 1; index >= 0; index--) {
      const names = catalogs[index].itemNames;
      if (names && names.has(itemIndex)) return names.get(itemIndex);
    }
    return '';
  }

  function extractSpawnCandidates(sourceCatalogs) {
    const result = [];
    const tagNames = new Set(['tag', 'contenttag', 'entitytag', 'spawntag']);
    const labelNames = ['candidates', 'family', 'name', 'entity', 'label', 'displayname'];
    for (const catalog of Array.isArray(sourceCatalogs) ? sourceCatalogs : []) {
      if (!catalog || catalog.category !== 'spawn-hopon' || !Array.isArray(catalog.columns) || !Array.isArray(catalog.rows)) continue;
      const tagColumn = catalog.columns.find(column => tagNames.has(normalizedHeader(column)));
      if (!tagColumn) continue;
      const labelColumns = labelNames.map(name => catalog.columns.find(column => normalizedHeader(column) === name)).filter(Boolean);
      catalog.rows.forEach((row, rowIndex) => {
        if (!row || typeof row.get !== 'function') return;
        const rawTag = String(row.get(tagColumn) || '').trim().replace(/^0x/i, '');
        if (!/^[0-9a-f]{1,8}$/i.test(rawTag)) return;
        const label = labelColumns.map(column => String(row.get(column) || '').trim()).find(Boolean) || `Catalog row ${rowIndex + 1}`;
        result.push({
          tag: rawTag.toUpperCase().padStart(8, '0'),
          label: label.slice(0, 120),
          source: String(catalog.fileName || 'Imported catalog').slice(0, 120),
          row: rowIndex + 1
        });
      });
    }
    return result;
  }

  function categoryLabel(value) {
    return ({
      activity: 'Activity',
      'spawn-hopon': 'Spawn / hop-on',
      'item-loot': 'Item / loot',
      'world-data': 'World data',
      general: 'General'
    })[value] || 'General';
  }

  function setStatus(message, state = '') {
    const status = byId('local-catalog-status');
    status.textContent = message;
    if (state) status.dataset.state = state;
    else delete status.dataset.state;
  }

  function populateCatalogSelect() {
    const select = byId('local-catalog-select');
    select.replaceChildren();
    catalogs.forEach((catalog, index) => {
      const option = document.createElement('option');
      option.value = String(index);
      option.textContent = `${catalog.fileName} · ${categoryLabel(catalog.category)}`;
      select.append(option);
    });
    select.value = String(Math.max(0, catalogs.length - 1));
  }

  function activeCatalog() {
    const index = Number(byId('local-catalog-select').value);
    return Number.isInteger(index) ? catalogs[index] || null : null;
  }

  function populateFieldSelect(catalog) {
    const select = byId('local-catalog-field');
    select.replaceChildren();
    const all = document.createElement('option');
    all.value = '';
    all.textContent = 'All fields';
    select.append(all);
    catalog.columns.forEach((column, index) => {
      const option = document.createElement('option');
      option.value = String(index);
      option.textContent = column;
      select.append(option);
    });
  }

  function addMetadata(label, value) {
    const item = document.createElement('span');
    const strong = document.createElement('strong');
    strong.textContent = `${label}: `;
    item.append(strong, document.createTextNode(value));
    byId('local-catalog-metadata').append(item);
  }

  function makeCell(tag, value) {
    const cell = document.createElement(tag);
    cell.textContent = value;
    if (value.length > 120) cell.title = value;
    return cell;
  }

  function renderRows(catalog) {
    const query = byId('local-catalog-search').value.trim().toLocaleLowerCase();
    const selectedField = byId('local-catalog-field').value;
    const selectedColumn = selectedField === '' ? null : catalog.columns[Number(selectedField)];
    const matches = [];
    for (const row of catalog.rows) {
      const values = selectedColumn ? [row.get(selectedColumn) || ''] : catalog.columns.map(column => row.get(column) || '');
      if (!query || values.some(value => value.toLocaleLowerCase().includes(query))) matches.push(row);
    }

    const visibleColumns = catalog.columns.slice(0, MAX_RENDER_COLUMNS);
    const head = byId('local-catalog-head');
    head.replaceChildren();
    const headerRow = document.createElement('tr');
    visibleColumns.forEach(column => headerRow.append(makeCell('th', column)));
    head.append(headerRow);

    const body = byId('local-catalog-rows');
    body.replaceChildren();
    const visibleRows = matches.slice(0, MAX_RENDER_ROWS);
    visibleRows.forEach(row => {
      const tr = document.createElement('tr');
      visibleColumns.forEach(column => tr.append(makeCell('td', row.get(column) || '')));
      body.append(tr);
    });
    if (!visibleRows.length) {
      const tr = document.createElement('tr');
      const td = makeCell('td', 'No matching rows.');
      td.colSpan = Math.max(1, visibleColumns.length);
      tr.append(td);
      body.append(tr);
    }

    const extraColumns = catalog.columns.length > visibleColumns.length
      ? ` · first ${visibleColumns.length} of ${catalog.columns.length} fields shown` : '';
    const truncated = catalog.truncated ? ' · import capped; remaining source rows were not loaded' : '';
    byId('local-catalog-preview-note').textContent =
      `Showing ${visibleRows.length} of ${matches.length} matching rows${extraColumns}${truncated}.`;
  }

  function renderCatalog() {
    const catalog = activeCatalog();
    const library = byId('local-catalog-library');
    if (!catalog) {
      library.hidden = true;
      return;
    }
    library.hidden = false;
    byId('local-catalog-metadata').replaceChildren();
    addMetadata('Type', categoryLabel(catalog.category));
    addMetadata('Format', catalog.format);
    addMetadata('Rows', `${catalog.rows.length}${catalog.truncated ? '+' : ''}`);
    addMetadata('Size', formatBytes(catalog.bytes));
    byId('local-catalog-schema').replaceChildren();
    const schemaTitle = document.createElement('strong');
    schemaTitle.textContent = `Fields (${catalog.columns.length}): `;
    byId('local-catalog-schema').append(schemaTitle, document.createTextNode(catalog.columns.join(' · ')));
    renderRows(catalog);
  }

  function parseFile(file, rowLimit, selectedType) {
    const extension = file.name.toLocaleLowerCase().split('.').pop();
    return file.text().then(text => {
      let parsed;
      let format;
      if (extension === 'json') {
        parsed = parseJson(text, rowLimit);
        format = 'JSON';
      } else if (extension === 'csv' || extension === 'tsv') {
        parsed = parseTable(text, extension === 'tsv' ? '\t' : ',', rowLimit);
        format = extension.toUpperCase();
      } else if (extension === 'txt') {
        parsed = parseTextCatalog(text, rowLimit);
        format = 'Text';
      } else throw new Error('Choose a .csv, .tsv, .json, or .txt file.');
      const category = categoryFor(file.name, selectedType);
      return {
        fileName: file.name.slice(0, 180),
        format,
        category,
        bytes: file.size,
        columns: parsed.columns,
        rows: parsed.rows,
        itemNames: buildItemNameMap(category, parsed.columns, parsed.rows),
        truncated: parsed.truncated
      };
    });
  }

  async function importSelected() {
    const input = byId('local-catalog-files');
    const files = Array.from(input.files || []);
    if (!files.length) {
      setStatus('Choose one or more CSV, TSV, JSON or text files first.', 'error');
      return;
    }
    if (catalogs.length + files.length > MAX_FILES) {
      setStatus(`The page can hold up to ${MAX_FILES} catalogs. Clear some before importing more.`, 'error');
      return;
    }

    const importButton = byId('local-catalog-import');
    importButton.disabled = true;
    const messages = [];
    let importedCount = 0;
    const selectedType = byId('local-catalog-type').value;
    try {
      for (const file of files) {
        if (file.size <= 0) {
          messages.push(`${file.name.slice(0, 80)}: empty file skipped`);
          continue;
        }
        if (file.size > MAX_FILE_BYTES) {
          messages.push(`${file.name.slice(0, 80)}: over the 4 MB limit`);
          continue;
        }
        if (loadedBytes + file.size > MAX_TOTAL_BYTES) {
          messages.push(`${file.name.slice(0, 80)}: would exceed the 8 MB total limit`);
          continue;
        }
        const rowLimit = Math.min(MAX_ROWS_PER_FILE, MAX_TOTAL_ROWS - loadedRows);
        if (rowLimit <= 0) {
          messages.push(`${file.name.slice(0, 80)}: total row limit reached`);
          break;
        }
        try {
          const catalog = await parseFile(file, rowLimit, selectedType);
          catalogs.push(catalog);
          loadedBytes += file.size;
          loadedRows += catalog.rows.length;
          importedCount++;
          messages.push(`${catalog.fileName}: ${catalog.rows.length}${catalog.truncated ? '+' : ''} rows loaded`);
        } catch (error) {
          messages.push(`${file.name.slice(0, 80)}: ${String(error?.message || error).slice(0, 180)}`);
        }
      }
    } finally {
      importButton.disabled = false;
      input.value = '';
    }

    if (catalogs.length) {
      populateCatalogSelect();
      populateFieldSelect(activeCatalog());
      byId('local-catalog-search').value = '';
      byId('local-catalog-clear').disabled = false;
      renderCatalog();
      window.dispatchEvent(new Event('rtm-local-item-catalogs-changed'));
      const hasSkipped = messages.length > importedCount;
      setStatus(messages.join(' · '), importedCount && !hasSkipped ? 'ok' : (importedCount ? 'warning' : 'error'));
    } else setStatus(messages.join(' · ') || 'No catalogs were imported.', 'error');
  }

  function clearCatalogs() {
    catalogs.length = 0;
    loadedBytes = 0;
    loadedRows = 0;
    byId('local-catalog-files').value = '';
    byId('local-catalog-search').value = '';
    byId('local-catalog-select').replaceChildren();
    byId('local-catalog-field').replaceChildren();
    byId('local-catalog-library').hidden = true;
    byId('local-catalog-metadata').replaceChildren();
    byId('local-catalog-schema').replaceChildren();
    byId('local-catalog-head').replaceChildren();
    byId('local-catalog-rows').replaceChildren();
    byId('local-catalog-preview-note').textContent = '';
    byId('local-catalog-clear').disabled = true;
    setStatus('Catalogs cleared from this page.', 'ok');
    window.dispatchEvent(new Event('rtm-local-item-catalogs-changed'));
  }

  function initialize() {
    if (!byId('local-catalog-browser')) return;
    byId('local-catalog-import').addEventListener('click', importSelected);
    byId('local-catalog-clear').addEventListener('click', clearCatalogs);
    byId('local-catalog-select').addEventListener('change', () => {
      const catalog = activeCatalog();
      if (catalog) populateFieldSelect(catalog);
      renderCatalog();
    });
    byId('local-catalog-search').addEventListener('input', () => {
      const catalog = activeCatalog();
      if (catalog) renderRows(catalog);
    });
    byId('local-catalog-field').addEventListener('change', () => {
      const catalog = activeCatalog();
      if (catalog) renderRows(catalog);
    });
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', initialize, { once: true });
  else initialize();
  window.localInventoryItemName = localItemName;
  window.rtmLocalSpawnCatalogs = Object.freeze({
    extractCandidates: extractSpawnCandidates,
    listCandidates: () => extractSpawnCandidates(catalogs)
  });
})();
