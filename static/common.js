// common.js - Shared JavaScript utilities for esp-arduino-ebus web UI

/**
 * Navigates to the home page ('/').
 */
function handleHome() {
    window.location.href = '/';
}

/**
 * Sets the status message in the element with id 'status'.
 * @param {string} message - The status message to display.
 */
function setStatus(message) {
    document.getElementById('status').textContent = message;
}

/**
 * Fetches JSON data from a given API endpoint and calls a callback with the parsed data.
 * Updates the status message during the process.
 * @param {string} path - API endpoint.
 * @param {function} onData - Callback to handle parsed JSON.
 * @param {string} [fetchingMsg='Fetching...'] - Status message while fetching.
 */
async function fetchJson(path, onData, fetchingMsg = 'Fetching...') {
    setStatus(fetchingMsg);
    try {
        const res = await fetch(path);
        if (!res.ok) throw new Error('Fetch failed');
        const jsonData = await res.json();
        onData(jsonData);
        setStatus('Fetched');
    } catch (err) {
        console.error(err);
        setStatus('Error fetching');
    }
}

/**
 * Performs a simple POST request to an API endpoint and updates the status message.
 * @param {string} path - API endpoint.
 * @param {string} [processingMsg='Processing...'] - Status message while processing.
 */
async function postSimple(path, processingMsg = 'Processing...') {
    setStatus(processingMsg);
    try {
        const res = await fetch(path, { method: 'POST' });
        const text = await res.text();
        setStatus(text || (res.ok ? 'OK' : 'Error'));
    } catch (err) {
        console.error(err);
        setStatus('Error');
    }
}

/**
 * Fetches data from the specified API endpoint and renders it into an HTML table.
 * This function dynamically constructs the table's headers and rows based on the
 * structure of the retrieved data.
 * @param {string} apiEndpoint - The API endpoint to retrieve the data.
 * @param {string} theadId - ID of the <thead> element where table headers will be rendered.
 * @param {string} tbodyId - ID of the <tbody> element where table rows will be rendered.
 */
function handleSimpleTable(apiEndpoint, theadId, tbodyId) {
    fetchJson(apiEndpoint, data => {
        const keys = new Set();
        data.forEach(item => Object.keys(item || {}).forEach(k => keys.add(k)));
        const cols = Array.from(keys);
        const rows = data.map(item => cols.map(k => {
            const val = item[k];
            if (val === undefined) return '';
            if (typeof val === 'object' && val !== null) return JSON.stringify(val);
            return String(val);
        }));
        renderSimpleTable(theadId, tbodyId, cols, rows);
    });
}

/**
 * Renders a simple table given headers and rows.
 * @param {string} theadId - ID of the <thead> element.
 * @param {string} tbodyId - ID of the <tbody> element.
 * @param {Array<string>} headers - Table headers.
 * @param {Array<Array>} rows - Table rows (arrays of cell values).
 */
function renderSimpleTable(theadId, tbodyId, headers, rows) {
    const thead = document.getElementById(theadId);
    const tbody = document.getElementById(tbodyId);
    thead.innerHTML = '';
    tbody.innerHTML = '';

    // Header
    const headerRow = document.createElement('tr');
    headers.forEach(header => {
        const th = document.createElement('th');
        th.textContent = header;
        headerRow.appendChild(th);
    });
    thead.appendChild(headerRow);

    // Rows
    rows.forEach(row => {
        const tr = document.createElement('tr');
        row.forEach(cell => {
            const td = document.createElement('td');
            td.textContent = cell;
            tr.appendChild(td);
        });
        tbody.appendChild(tr);
    });
}

/**
 * Renders nested JSON data as expandable sections in a container.
 * @param {object} data - The JSON object to render.
 * @param {string} containerId - The ID of the container element.
 * @param {boolean} sortNumbers - Whether to sort the number fields or not.
 * @param {number} radix - The radix (base) to use for parsing keys.
 */
function renderNestedSections(data, containerId, sortNumbers = false, radix = 10) {
    const container = document.getElementById(containerId);
    container.innerHTML = '';

    function renderEntry(parent, key, value, forceSection = false) {
        const isObj = typeof value === 'object' && value !== null;
        const isArr = Array.isArray(value);

        if (forceSection || (isObj && !isArr)) {
            const sectionDiv = document.createElement('div');
            sectionDiv.classList.add('section');
            if (key) sectionDiv.innerHTML = `<div class="title">${key}</div>`;

            if (isObj && !isArr) {
                const entries = Object.entries(value);
                if (sortNumbers) {
                    entries.sort(([k1, v1], [k2, v2]) => {
                        if (typeof v1 === 'number' && typeof v2 === 'number') {
                            return parseInt(k1, radix) - parseInt(k2, radix);
                        }
                        return 0;
                    });
                }
                for (const [k, v] of entries) {
                    renderEntry(sectionDiv, k, v);
                }
            } else if (isArr) {
                if (value.length === 0) {
                    sectionDiv.insertAdjacentHTML('beforeend', '<div class="item"><i>(empty)</i></div>');
                } else {
                    value.forEach((itemVal, index) => {
                        renderEntry(sectionDiv, `[${index}]`, itemVal);
                    });
                }
            } else {
                sectionDiv.insertAdjacentHTML('beforeend', `<div class="item">${value}</div>`);
            }
            parent.appendChild(sectionDiv);
        } else {
            const itemDiv = document.createElement('div');
            itemDiv.classList.add('item');
            itemDiv.innerHTML = `<span class="key">${key}:</span> ${isArr ? JSON.stringify(value) : value}`;
            parent.appendChild(itemDiv);
        }
    }

    for (const [key, value] of Object.entries(data)) {
        // Force root level entries into sections for visual consistency
        renderEntry(container, key, value, true);
    }
}

/**
 * Downloads a text content as a file with the given filename and MIME type.
 * @param {string} text - The content to download.
 * @param {string} filename - The filename for the download.
 * @param {string} [mime='text/plain'] - The MIME type.
 */
function downloadTextFile(text, filename, mime = 'text/plain') {
    try {
        const now = new Date();
        const timestamp = now.toISOString().split('T')[0] + '-' + now.toTimeString().split(' ')[0].replace(/:/g, '-'); // Format: YYYY-MM-DD-HH-MM-SS
        const newFilename = `${timestamp}-${filename}`;

        const blob = new Blob([text], { type: mime });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = newFilename;
        document.body.appendChild(a);
        a.click();
        a.remove();
        URL.revokeObjectURL(url);
        setStatus('Downloaded');
    } catch (err) {
        setStatus('Download failed');
        console.error('Download error:', err);
    }
}

/**
 * Clears the value of a textarea and updates the size/status.
 * @param {string} textareaId - The ID of the textarea element.
 * @param {string} sizeId - The ID of the size element (optional, for updateSize).
 */
function clearTextarea(textareaId, sizeId) {
    document.getElementById(textareaId).value = '';
    if (typeof updateSize === 'function') updateSize();
    setStatus('Editor cleared');
}

/**
 * Formats the JSON content in a textarea, pretty-printing it.
 * @param {string} textareaId - The ID of the textarea element.
 */
function formatTextareaJson(textareaId) {
    try {
        const area = document.getElementById(textareaId);
        const o = JSON.parse(area.value);
        area.value = JSON.stringify(o, null, 2);
        setStatus('Formatted');
        if (typeof updateSize === 'function') updateSize();
    } catch (e) {
        setStatus('Invalid JSON');
    }
}

/**
 * Handles uploading a JSON file and loading its content into a textarea.
 * @param {Event} ev - The file input change event.
 * @param {string} textareaId - The ID of the textarea element.
 * @param {function} updateSizeCb - Optional callback to update size/status.
 */
function handleJsonFileUpload(ev, textareaId, updateSizeCb) {
    const f = ev.target.files[0];
    if (!f) return;
    const r = new FileReader();
    r.onload = (e) => {
        try {
            const obj = JSON.parse(e.target.result);
            document.getElementById(textareaId).value = JSON.stringify(obj, null, 2);
            setStatus('Loaded file');
            if (typeof updateSizeCb === 'function') updateSizeCb();
        } catch (err) {
            setStatus('Invalid JSON in file');
        }
    };
    r.readAsText(f);
}

/**
 * Updates the text content of the specified label element to display the poll interval.
 * @param {string} labelId - The ID of the label element to update.
 * @param {number} interval - The current poll interval to display.
 */
function updatePollLabel(labelId, interval) {
    document.getElementById(labelId).textContent = `${interval}s`;
}

/**
 * Sets a timeout for polling the data.
 * This function clears any existing timeout and sets a new one that calls the specified function 
 * after the designated interval.
 * @param {number} interval - The time in seconds after which to poll.
 * @param {function} handleFunction - The function to call when the timeout expires.
 */
function setPollingTimeout(interval, handleFunction) {
    if (isPaused) return;
    clearTimeout(pollingTimeout);
    pollingTimeout = setTimeout(() => {
        handleFunction();
        setPollingTimeout(interval, handleFunction);
    }, interval * 1000);
}

/**
 * Changes the poll interval and updates the label.
 * This function increases or decreases the interval based on the delta value 
 * and ensures it remains within defined min and max limits.
 * @param {number} delta - The amount to change the poll interval by (positive or negative).
 * @param {number} min - The minimum allowed value for the poll interval.
 * @param {number} max - The maximum allowed value for the poll interval.
 * @param {string} labelId - The ID of the label element to update.
 * @param {function} handleFunction - The function to call when the interval changes.
 */
function changePollInterval(delta, min, max, labelId, handleFunction) {
    if ((delta > 0 && pollInterval < max) || (delta < 0 && pollInterval > min)) {
        pollInterval += delta;
        updatePollLabel(labelId, pollInterval);
        setPollingTimeout(pollInterval, handleFunction);
    }
}

/**
 * Toggles the pause state for the poll updates.
 * @param {string} labelId - The ID of the button element to update.
 */
function togglePause(labelId) {
    isPaused = !isPaused;
    document.getElementById(labelId).textContent = isPaused ? 'Resume' : 'Pause';

    if (isPaused)
        clearTimeout(pollTimeout);
    else
        setPollingTimeout(pollInterval, handleValues);
}

/**
 * Renders a flat key-value object as a 2-column summary table
 * (label | value). Used for the status page "alive?" landing view.
 * @param {object} data - Flat key-value object to render.
 * @param {string} containerId - ID of the container element.
 */
function renderSummaryTable(data, containerId) {
    const container = document.getElementById(containerId);
    container.innerHTML = '';

    const table = document.createElement('table');
    table.classList.add('kv');
    const thead = document.createElement('thead');
    const headerRow = document.createElement('tr');
    ['metric', 'value'].forEach(h => {
        const th = document.createElement('th');
        th.textContent = h;
        headerRow.appendChild(th);
    });
    thead.appendChild(headerRow);
    table.appendChild(thead);
    const tbody = document.createElement('tbody');

    for (const [key, value] of Object.entries(data)) {
        // One level of nesting expands to parent.child rows so small
        // objects (health.heap, health.logger) stay readable. Deeper
        // structures still render as JSON — they don't belong here.
        if (typeof value === 'object' && value !== null &&
            Object.values(value).every(
                v => v === null || typeof v !== 'object')) {
            for (const [sub, subValue] of Object.entries(value)) {
                addRow(`${key}.${sub}`, subValue);
            }
            continue;
        }
        addRow(key, value);
    }

    function addRow(label, val) {
        const row = document.createElement('tr');

        const labelCell = document.createElement('td');
        labelCell.textContent = label;
        labelCell.style.fontWeight = 'bold';

        const valueCell = document.createElement('td');
        if (typeof val === 'boolean') {
            valueCell.textContent = val ? 'yes' : 'no';
            if (!val) valueCell.style.color = '#888';
        } else if (typeof val === 'number') {
            valueCell.textContent = String(val);
        } else if (typeof val === 'object' && val !== null) {
            valueCell.textContent = JSON.stringify(val);
        } else {
            valueCell.textContent = String(val);
        }

        row.appendChild(labelCell);
        row.appendChild(valueCell);
        tbody.appendChild(row);
    }

    table.appendChild(tbody);
    container.appendChild(table);
}

/**
 * Renders nested JSON data as collapsible <details> sections.
 * Each object/array becomes a <details> with a <summary> showing the
 * key and a preview; primitives render as plain items.
 * @param {object} data - The JSON object to render.
 * @param {string} containerId - The ID of the container element.
 */
function renderDetailsSections(data, containerId) {
    const container = document.getElementById(containerId);
    container.innerHTML = '';

    function formatValue(value) {
        if (typeof value === 'boolean') return value ? 'yes' : 'no';
        if (typeof value === 'number') return String(value);
        if (typeof value === 'string') return value;
        if (value === null) return 'null';
        return JSON.stringify(value);
    }

    function entrySummary(key, value) {
        const isObj = typeof value === 'object' && value !== null;
        if (isObj) {
            const preview = Array.isArray(value)
                ? `[${value.length} items]`
                : `{${Object.keys(value).length} fields}`;
            return key ? `${key}: ${preview}` : preview;
        }
        return key ? `${key}: ${formatValue(value)}` : formatValue(value);
    }

    function renderEntry(parent, key, value) {
        const isObj = typeof value === 'object' && value !== null;

        if (isObj) {
            const details = document.createElement('details');
            const summary = document.createElement('summary');
            summary.textContent = entrySummary(key, value);
            details.appendChild(summary);

            const childDiv = document.createElement('div');
            for (const [k, v] of Object.entries(value)) {
                renderEntry(childDiv, k, v);
            }
            details.appendChild(childDiv);
            parent.appendChild(details);
        } else {
            const itemDiv = document.createElement('div');
            itemDiv.classList.add('item');
            itemDiv.innerHTML = key
                ? `<span class="key">${key}:</span> ${formatValue(value)}`
                : formatValue(value);
            parent.appendChild(itemDiv);
        }
    }

    for (const [key, value] of Object.entries(data)) {
        renderEntry(container, key, value);
    }
}

/**
 * Renders nested JSON data as one <h2> table per top-level object.
 * Arrays of objects become column tables, plain objects become
 * key/value tables, primitives become single rows. Tables, not
 * collapsed sections: every value visible without clicking.
 * @param {object} data - The JSON object to render.
 * @param {string} containerId - ID of the container element.
 * @param {boolean} [alignFirst=false] - Align first columns page-wide
 * (adds the "kv" class); use for stacked tables sharing a column.
 */
function renderTables(data, containerId, alignFirst = false) {
    const container = document.getElementById(containerId);
    container.innerHTML = '';
    function formatCell(v) {
        if (v === undefined || v === null) return '';
        if (typeof v === 'object') return JSON.stringify(v);
        return String(v);
    }
    function addTable(title, cols, rows) {
        const h = document.createElement('h2');
        h.textContent = title;
        container.appendChild(h);
        const table = document.createElement('table');
        if (cols.length === 2) table.classList.add('kv');
        const thead = document.createElement('thead');
        const headerRow = document.createElement('tr');
        cols.forEach(c => {
            const th = document.createElement('th');
            th.textContent = c;
            headerRow.appendChild(th);
        });
        thead.appendChild(headerRow);
        table.appendChild(thead);
        const tbody = document.createElement('tbody');
        rows.forEach(r => {
            const tr = document.createElement('tr');
            r.forEach(cell => {
                const td = document.createElement('td');
                td.textContent = cell;
                tr.appendChild(td);
            });
            tbody.appendChild(tr);
        });
        table.appendChild(tbody);
        container.appendChild(table);
    }
    for (const [key, value] of Object.entries(data || {})) {
        if (Array.isArray(value)) {
            const cols = [];
            value.forEach(item => {
                if (item && typeof item === 'object') {
                    Object.keys(item).forEach(k => {
                        if (!cols.includes(k)) cols.push(k);
                    });
                }
            });
            addTable(key, cols, value.map(item =>
                cols.map(c => formatCell(item ? item[c] : ''))));
        } else if (value && typeof value === 'object') {
            addTable(key, ['key', 'value'], Object.entries(value).map(
                ([k, v]) => [k, formatCell(v)]));
        } else {
            addTable(key, ['value'], [[formatCell(value)]]);
        }
    }
}

/**
 * Renders array-of-object rows into an existing <thead>/<tbody> pair
 * using an explicit column list. Used for heap trend, thread stacks
 * and CPU shares tables.
 * @param {string} theadId - ID of the <thead> element.
 * @param {string} tbodyId - ID of the <tbody> element.
 * @param {Array<string>} cols - Column keys, in order.
 * @param {Array<object>} items - Row objects.
 * @param {boolean} [alignFirst=false] - Align first column page-wide
 * (adds the "kv" class); use for stacked tables sharing a column.
 */
function renderRows(theadId, tbodyId, cols, items, alignFirst = false) {
    const thead = document.getElementById(theadId);
    const tbody = document.getElementById(tbodyId);
    const table = thead.closest('table');
    if (table && (alignFirst || cols.length === 2)) table.classList.add('kv');
    thead.innerHTML = '';
    tbody.innerHTML = '';
    const headerRow = document.createElement('tr');
    cols.forEach(c => {
        const th = document.createElement('th');
        th.textContent = c;
        headerRow.appendChild(th);
    });
    thead.appendChild(headerRow);
    items.forEach(item => {
        const tr = document.createElement('tr');
        cols.forEach(c => {
            const td = document.createElement('td');
            const v = item[c];
            td.textContent = (v === undefined || v === null) ? '' : String(v);
            tr.appendChild(td);
        });
        tbody.appendChild(tr);
    });
}
