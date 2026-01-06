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
 * Performs a simple GET or POST request to an API endpoint and updates the status message.
 * @param {string} path - API endpoint.
 * @param {string} [processingMsg='Processing...'] - Status message while processing.
 */
async function postSimple(path, processingMsg = 'Processing...') {
    setStatus(processingMsg);
    try {
        const res = await fetch(path);
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
        const rows = data.map(item => cols.map(k => item[k] === undefined ? '' : String(item[k])));
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
 */
function renderNestedSections(data, containerId) {
    const container = document.getElementById(containerId);
    container.innerHTML = '';
    function createSection(sectionName, values) {
        const sectionDiv = document.createElement('div');
        sectionDiv.classList.add('section');
        sectionDiv.innerHTML = `<div class="title">${sectionName}</div>`;
        for (const [key, value] of Object.entries(values)) {
            const itemDiv = document.createElement('div');
            itemDiv.classList.add('item');
            if (typeof value === 'object' && value !== null) {
                const nestedSectionDiv = createSection(key, value);
                sectionDiv.appendChild(nestedSectionDiv);
            } else {
                itemDiv.innerHTML = `<span class="key">${key}:</span> ${value}`;
                sectionDiv.appendChild(itemDiv);
            }
        }
        return sectionDiv;
    }
    for (const [section, values] of Object.entries(data)) {
        const sectionDiv = createSection(section, values);
        container.appendChild(sectionDiv);
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