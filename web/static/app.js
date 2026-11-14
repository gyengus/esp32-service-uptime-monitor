(function() {
let allEntities = [];
let hasLoadedEntities = false;
let currentFilter = 'ALL';
let entityToDelete = "";
let ws = null;
let wsReconnectTimer = null;
let wsReconnectDelay = 1000;
let terminalOpen = false;
let terminalAutoScroll = true;
let terminalLogsBuffer = [];
const MAX_TERMINAL_LOGS = 300;
let lastDetailedSysinfo = null;
let sysinfoRefreshTimer = null;
const SYSINFO_DETAILS_REFRESH_MS = 30000;
const DASHBOARD_ICONS_CDN = 'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons';
const DASHBOARD_ICONS_TREE_URL = `${DASHBOARD_ICONS_CDN}/tree.json`;
const MDI_CSS_URL = 'https://cdn.jsdelivr.net/npm/@mdi/font@7.4.47/css/materialdesignicons.min.css';
const ICON_SUGGESTIONS_LIMIT = 48;
const GITHUB_REPO_URL = 'https://github.com/gyengus/esp32-service-uptime-monitor';
let dashboardIconsCatalog = null;
let dashboardIconsLoading = false;
let mdiIconsCatalog = null;
let mdiIconsLoading = false;
let configCountdownInterval = null;
let configTimeoutRemaining = 0;

function formatConfigCountdown(totalSeconds) {
    if (isNaN(totalSeconds) || totalSeconds <= 0) return '00:00';
    const mins = Math.floor(totalSeconds / 60);
    const secs = totalSeconds % 60;
    return `${String(mins).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;
}

function updateConfigCountdownUI() {
    if (configTimeoutRemaining <= 0) {
        $('#configCountdownBadge').addClass('d-none').removeClass('d-inline-flex');
        $('#configButton').hide();
        $('#headerConfigBadge').addClass('d-none');
        if (configCountdownInterval) {
            clearInterval(configCountdownInterval);
            configCountdownInterval = null;
        }
        return;
    }

    const formatted = formatConfigCountdown(configTimeoutRemaining);
    $('#configCountdownTime').text(formatted);
    $('#configMenuCountdown').text(formatted);

    if (configTimeoutRemaining <= 60) {
        $('#configCountdownBadge').removeClass('bg-warning text-dark').addClass('bg-danger text-white');
        $('#configMenuCountdown').removeClass('bg-warning text-dark').addClass('bg-danger text-white');
    } else {
        $('#configCountdownBadge').removeClass('bg-danger text-white').addClass('bg-warning text-dark');
        $('#configMenuCountdown').removeClass('bg-danger text-white').addClass('bg-warning text-dark');
    }
}

function setConfigCountdown(seconds, enabled) {
    if (!enabled || seconds <= 0) {
        configTimeoutRemaining = 0;
        updateConfigCountdownUI();
        return;
    }

    configTimeoutRemaining = seconds;
    $('#configCountdownBadge').removeClass('d-none').addClass('d-inline-flex');
    $('#configButton').show();
    $('#headerConfigBadge').removeClass('d-none');
    updateConfigCountdownUI();

    if (!configCountdownInterval) {
        configCountdownInterval = setInterval(() => {
            if (configTimeoutRemaining > 0) {
                configTimeoutRemaining--;
                updateConfigCountdownUI();
            } else {
                clearInterval(configCountdownInterval);
                configCountdownInterval = null;
                updateConfigCountdownUI();
            }
        }, 1000);
    }
}

function getGithubReleaseUrl(version) {
    if (!version || version === '--') {
        return `${GITHUB_REPO_URL}/releases`;
    }
    const cleanVer = String(version).trim();
    if (!cleanVer || cleanVer === '--') {
        return `${GITHUB_REPO_URL}/releases`;
    }
    const tag = cleanVer.startsWith('v') ? cleanVer : `v${cleanVer}`;
    return `${GITHUB_REPO_URL}/releases/tag/${encodeURIComponent(tag)}`;
}

function getGithubCommitUrl(commit) {
    if (!commit || commit === '--') {
        return GITHUB_REPO_URL;
    }
    const cleanCommit = String(commit).replace(/^#/, '').trim();
    if (!cleanCommit || cleanCommit === '--') {
        return GITHUB_REPO_URL;
    }
    return `${GITHUB_REPO_URL}/commit/${encodeURIComponent(cleanCommit)}`;
}

function escapeHtml(str) {
    if (str === null || str === undefined) return '';
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

function renderUiSvg(symbolId, extraClass = '') {
    const cls = extraClass ? `ui-icon ${extraClass}` : 'ui-icon';
    return `<svg class="${escapeHtml(cls)}" aria-hidden="true"><use href="#${escapeHtml(symbolId)}"></use></svg>`;
}

function resolveIconConfig(rawIcon) {
    const trimmed = (rawIcon || '').trim();
    if (!trimmed) {
        return { type: 'mdi', className: 'mdi-server' };
    }

    if (trimmed.toLowerCase().startsWith('di:')) {
        const slugPart = trimmed.slice(3).replace(/^\/+/, '');
        if (!/^[a-zA-Z0-9._-]+$/.test(slugPart)) {
            return { type: 'mdi', className: 'mdi-server' };
        }

        const extMatch = slugPart.match(/\.(svg|webp|png)$/i);
        if (extMatch) {
            const ext = extMatch[1].toLowerCase();
            return {
                type: 'image',
                url: `${DASHBOARD_ICONS_CDN}/${ext}/${slugPart}`,
                fallbackUrls: []
            };
        }

        return {
            type: 'image',
            url: `${DASHBOARD_ICONS_CDN}/svg/${slugPart}.svg`,
            fallbackUrls: [
                `${DASHBOARD_ICONS_CDN}/webp/${slugPart}.webp`,
                `${DASHBOARD_ICONS_CDN}/png/${slugPart}.png`
            ]
        };
    }

    if (/^https?:\/\//i.test(trimmed)) {
        return {
            type: 'image',
            url: trimmed,
            fallbackUrls: []
        };
    }

    const safeClass = /^[a-zA-Z0-9_-]+$/.test(trimmed) ? trimmed : 'mdi-server';
    return { type: 'mdi', className: safeClass };
}

function renderIconHtml(rawIcon, extraClass = 'me-2 entityIcon') {
    const resolved = resolveIconConfig(rawIcon);
    const clsSuffix = extraClass ? ` ${extraClass}` : '';
    if (resolved.type === 'image') {
        const fallbacks = resolved.fallbackUrls.length
            ? ` data-fallbacks="${escapeHtml(resolved.fallbackUrls.join('|'))}"`
            : '';
        return `<img src="${escapeHtml(resolved.url)}" alt="" class="entity-img-icon${clsSuffix}" loading="lazy"${fallbacks} />`;
    }
    return `<span class="mdi ${escapeHtml(resolved.className)}${clsSuffix}"></span>`;
}

function handleEntityIconError(event) {
    const img = event.target;
    if (!img || img.tagName !== 'IMG' || !img.classList || !img.classList.contains('entity-img-icon')) {
        return;
    }

    const rawFallbacks = img.dataset ? (img.dataset.fallbacks || '') : '';
    const queue = rawFallbacks.split('|').filter(Boolean);
    if (queue.length > 0) {
        const nextUrl = queue.shift();
        img.dataset.fallbacks = queue.join('|');
        img.src = nextUrl;
        return;
    }

    // Replace broken image with default MDI server icon while keeping spacing/animation classes
    const fallbackSpan = document.createElement('span');
    fallbackSpan.className = 'mdi mdi-server';
    if (img.classList.contains('me-2')) fallbackSpan.classList.add('me-2');
    if (img.classList.contains('entityIcon')) fallbackSpan.classList.add('entityIcon');
    if (img.parentNode) {
        img.parentNode.replaceChild(fallbackSpan, img);
    }
}

if (typeof document !== 'undefined') {
    document.addEventListener('error', handleEntityIconError, true);
}

$(document).ready(function() {
    initWebSocket();
    loadEntities();
    loadSystemStats();
    loadSummary();
    updateIconPreview();
});

function setWsStatus(connected) {
    const dot = $('#wsStatusDot');
    if (!dot.length) return;
    if (connected) {
        const msg = 'Live (WebSocket connected)';
        dot.removeClass('bg-secondary bg-danger').addClass('bg-success').attr({ title: msg, 'aria-label': msg });
    } else {
        const msg = 'Disconnected (Reconnecting...)';
        dot.removeClass('bg-secondary bg-success').addClass('bg-danger').attr({ title: msg, 'aria-label': msg });
    }
}

function initWebSocket() {
    if (typeof WebSocket === 'undefined') return;
    const loc = window.location;
    if (!loc || !loc.host) return;

    const protocol = loc.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${loc.host}/ws`;

    try {
        ws = new WebSocket(wsUrl);
        if (typeof window !== 'undefined') {
            window.ws = ws;
        }
    } catch (e) {
        setWsStatus(false);
        scheduleReconnect();
        return;
    }

    ws.onopen = function() {
        wsReconnectDelay = 1000;
        setWsStatus(true);
    };

    ws.onmessage = function(event) {
        try {
            const msg = JSON.parse(event.data);
            handleWsMessage(msg);
        } catch (e) {
            console.error('[WS] Invalid message:', e, event.data);
        }
    };

    ws.onclose = function(e) {
        console.warn('[WS] Closed:', e.code, e.reason);
        setWsStatus(false);
        scheduleReconnect();
    };

    ws.onerror = function(err) {
        console.error('[WS] Error:', err);
        setWsStatus(false);
    };
}

function scheduleReconnect() {
    if (wsReconnectTimer) clearTimeout(wsReconnectTimer);
    wsReconnectTimer = setTimeout(() => {
        initWebSocket();
        wsReconnectDelay = Math.min(wsReconnectDelay * 1.5, 10000);
    }, wsReconnectDelay);
}

function handleWsMessage(msg) {
    if (!msg || !msg.type) return;

    switch (msg.type) {
        case 'init':
            if (Array.isArray(msg.entities)) {
                hasLoadedEntities = true;
                allEntities = msg.entities;
                renderEntities();
            }
            if (msg.summary) updateSummaryUI(msg.summary);
            if (msg.sysinfo) updateSysinfoUI(msg.sysinfo);
            if (msg.logLevelName) {
                $('#terminalLogLevel').val(msg.logLevelName);
            }
            if (msg.canChangeLogLevel === false) {
                $('#terminalLogLevel').prop('disabled', true).attr('title', 'Log level is fixed in release builds');
            } else {
                $('#terminalLogLevel').prop('disabled', false).removeAttr('title');
            }
            if (terminalOpen) {
                sendWsMessage({ type: 'subscribe_logs', enabled: true });
            }
            break;

        case 'log':
            if (msg.line) {
                appendLogLine(msg.line);
            }
            break;

        case 'log_level':
            if (msg.name) {
                $('#terminalLogLevel').val(msg.name);
            }
            break;

        case 'entity_update':
            if (msg.entity) {
                handleEntityUpdate(msg.entity);
            }
            break;

        case 'entity_delete':
            if (msg.name) {
                handleEntityDelete(msg.name);
            }
            break;

        case 'summary':
            if (msg.data) updateSummaryUI(msg.data);
            break;

        case 'sysinfo':
            if (msg.data) updateSysinfoUI(msg.data);
            break;

        default:
            break;
    }
}

function handleEntityUpdate(updatedEntity) {
    const index = allEntities.findIndex(e => e.name === updatedEntity.name);
    let statusChanged = false;

    if (index >= 0) {
        const prev = allEntities[index];
        statusChanged = (prev.isOnline !== updatedEntity.isOnline) || (prev.disabled !== updatedEntity.disabled);
        allEntities[index] = { ...prev, ...updatedEntity };
    } else {
        allEntities.push(updatedEntity);
        statusChanged = true;
    }

    renderEntities();

    if (statusChanged) {
        triggerCardPulse(updatedEntity.name);
    }
}

function handleEntityDelete(name) {
    allEntities = allEntities.filter(e => e.name !== name);
    renderEntities();
}

function triggerCardPulse(entityName) {
    const card = $(`.monitoredEntity[data-entity-name="${CSS.escape(entityName)}"]`);
    if (card.length) {
        card.removeClass('card-status-pulse');
        void card[0].offsetWidth; // trigger reflow
        card.addClass('card-status-pulse');
        setTimeout(() => {
            card.removeClass('card-status-pulse');
        }, 850);
    }
}

function loadSummary() {
    $.getJSON('/api/summary', function(stats) {
        updateSummaryUI(stats);
    });
}

function updateSummaryUI(stats) {
    if (!stats) return;
    if ($('#navTotal').text() != stats.total) {
        $('#navTotal').fadeOut(200, function() { $(this).text(stats.total).fadeIn(200); });
    }
    if ($('#navOnline').text() != stats.online) {
        $('#navOnline').fadeOut(200, function() { $(this).text(stats.online).fadeIn(200); });
    }
    if ($('#navOffline').text() != stats.offline) {
        $('#navOffline').fadeOut(200, function() { $(this).text(stats.offline).fadeIn(200); });
    }
}

$('#refreshButton').click(function() {
    loadEntities();
});

function renderEntitiesLoading() {
    const grid = $("#entityGrid");
    grid.html(`
        <div class="col-12 text-center text-secondary py-5" role="status" aria-live="polite">
            <div class="spinner-border text-primary mb-2" role="status" aria-hidden="true"></div>
            <div class="small">Loading services...</div>
        </div>
    `);
}

function loadEntities() {
    const icon = $("#refreshIcon");
    icon.addClass("spin-animation");
    $("#refreshButton").prop("disabled", true);
    if (!hasLoadedEntities) {
        renderEntitiesLoading();
    }

    loadSummary();
    $.getJSON('/api/monitoredentities', function(data) {
        hasLoadedEntities = true;
        allEntities = data;
        renderEntities();
    }).fail(function() {
        hasLoadedEntities = true;
        showError("Can't fetch monitored entities");
        renderEntities();
    }).always(function() {
        setTimeout(() => {
            icon.removeClass("spin-animation");
            $("#refreshButton").prop("disabled", false);
        }, 500);
    });
}

function renderEntities() {
    let grid = $("#entityGrid");
    grid.empty();

    $('#filterAll, #filterOnline, #filterOffline')
        .removeClass('badge-active')
        .attr('aria-pressed', 'false');
    $('#navStats').removeClass('nav-stats-filtering');

    if (currentFilter === 'ALL') {
        $('#filterAll').addClass('badge-active').attr('aria-pressed', 'true');
    } else {
        $('#navStats').addClass('nav-stats-filtering');
        if (currentFilter === 'ONLINE') $('#filterOnline').addClass('badge-active').attr('aria-pressed', 'true');
        if (currentFilter === 'OFFLINE') $('#filterOffline').addClass('badge-active').attr('aria-pressed', 'true');
    }

    const userLocale = (typeof navigator !== 'undefined' && navigator.language) ? navigator.language : 'en-US';
    let displayList = [...allEntities].sort((a, b) => 
        a.name.localeCompare(b.name, userLocale, { sensitivity: 'base' })
    );

    if (currentFilter === 'ONLINE') {
        displayList = displayList.filter(e => e.isOnline);
    } else if (currentFilter === 'OFFLINE') {
        displayList = displayList.filter(e => !e.isOnline && e.lastChanged);
    }

    if (displayList.length === 0) {
        if (allEntities.length === 0) {
            grid.append(`
                <div class="col-12 text-center text-secondary py-5">
                    <h5>No monitored services yet</h5>
                    <p class="small mb-3">Add your first service to start monitoring its uptime.</p>
                    <button type="button" class="btn btn-primary d-inline-flex align-items-center gap-1" id="emptyAddButton">
                        ${renderUiSvg('ui-plus')}
                        <span>Add your first service</span>
                    </button>
                </div>
            `);
        } else {
            grid.append(`
                <div class="col-12 text-center text-secondary py-5">
                    <h5>No services match the current filter</h5>
                    <button type="button" class="btn btn-outline-secondary mt-2" id="emptyShowAllButton">Show all</button>
                </div>
            `);
        }
        return;
    }

    const formatter = new Intl.DateTimeFormat(userLocale, {
        year: 'numeric', month: '2-digit', day: '2-digit',
        hour: '2-digit', minute: '2-digit', second: '2-digit'
    });

    displayList.forEach(entity => {
        const isLightCard = !entity.disabled && !entity.lastChanged;
        const cardBgClass = entity.disabled ? "text-bg-secondary" : entity.lastChanged ? (entity.isOnline ? "text-bg-success" : "text-bg-danger") : "text-bg-light";
        const linkClass = isLightCard ? 'link-dark' : 'link-light';
        const subTextClass = isLightCard ? 'text-body-secondary' : 'text-white text-opacity-75';
        const actionBtnClass = isLightCard ? 'btn-outline-dark text-dark' : 'btn-outline-light text-white';

        let statusBadge = '';
        if (entity.disabled) {
            statusBadge = `<span class="badge text-bg-dark border border-secondary-subtle d-inline-flex align-items-center gap-1">${renderUiSvg('ui-pause-circle')}<span>Disabled</span></span>`;
        } else if (!entity.lastChanged) {
            statusBadge = `<span class="badge text-bg-secondary d-inline-flex align-items-center gap-1">${renderUiSvg('ui-clock')}<span>Pending</span></span>`;
        } else if (entity.isOnline) {
            statusBadge = `<span class="badge bg-dark bg-opacity-25 text-white border border-white border-opacity-25 d-inline-flex align-items-center gap-1">${renderUiSvg('ui-check-circle')}<span>Online</span></span>`;
        } else {
            statusBadge = `<span class="badge bg-dark bg-opacity-25 text-white border border-white border-opacity-25 d-inline-flex align-items-center gap-1">${renderUiSvg('ui-alert-circle')}<span>Offline</span></span>`;
        }

        let iconHtml = renderIconHtml(entity.icon || "mdi-server");
        let notifyIcon = entity.notification ? `<span class="ms-2 d-inline-flex align-items-center" title="Notifications ON">${renderUiSvg('ui-bell')}</span>` : '';
        let addressUrl = (entity.address.startsWith("http://") || entity.address.startsWith("https://")) 
            ? entity.address 
            : "http://" + entity.address;
        const safeName = escapeHtml(entity.name);
        const safeAddress = escapeHtml(entity.address);
        const safeAddressUrl = escapeHtml(addressUrl);
        let address = `<a href="${safeAddressUrl}" target="_blank" rel="noopener noreferrer" class="${linkClass} d-block text-truncate">${safeAddress}</a>`;

        const lastChanged = new Date(entity.lastChanged * 1000);
        const lastChangedText = entity.lastChanged ? `${renderUiSvg('ui-clock', 'me-1')}${formatter.format(lastChanged)}` : "";

        let cardHtml = `
            <div class="col-12 col-md-6 col-lg-3">
                <div class="card h-100 shadow monitoredEntity ${cardBgClass}" data-entity-name="${safeName}">
                    <div class="card-body position-relative">
                        <div class="position-absolute top-0 end-0 p-2 buttons d-flex gap-1">
                            <button type="button" class="btn btn-sm ${actionBtnClass} opacity-75 btn-edit" data-name="${safeName}" title="Edit" aria-label="Edit ${safeName}">
                                ${renderUiSvg('ui-pencil')}
                            </button>
                            <button type="button" class="btn btn-sm ${actionBtnClass} opacity-75 btn-delete" data-name="${safeName}" title="Delete" aria-label="Delete ${safeName}">
                                ${renderUiSvg('ui-delete')}
                            </button>
                        </div>

                        <h5 class="card-title d-flex align-items-center pe-5">
                            ${iconHtml}
                            <span class="text-truncate">${safeName}</span>
                            ${notifyIcon}
                        </h5>
                        <div class="mb-2">
                            ${statusBadge}
                        </div>
                        <p class="card-text small mb-0 ${subTextClass}">
                            ${address}
                        </p>
                        <p class="card-text mt-2 mb-0 ${subTextClass}" style="font-size: 0.75rem;">
                            ${lastChangedText}
                        </p>
                    </div>
                </div>
            </div>`;
        grid.append(cardHtml);
    });
}

function updateIconPreview() {
    const preview = $('#iconPreview');
    if (!preview.length) return;
    preview.html(renderIconHtml($('#icon').val() || 'mdi-server', ''));
}

function buildDashboardIconsCatalog(treeData) {
    if (!treeData || typeof treeData !== 'object') return [];
    const formats = ['svg', 'webp', 'png'];
    const bySlug = new Map();

    formats.forEach((fmt) => {
        const list = Array.isArray(treeData[fmt]) ? treeData[fmt] : [];
        list.forEach((filename) => {
            if (typeof filename !== 'string') return;
            const slug = filename.replace(/\.(svg|webp|png)$/i, '');
            if (!slug || bySlug.has(slug)) return;
            const iconValue = fmt === 'svg' ? `di:${slug}` : `di:${slug}.${fmt}`;
            bySlug.set(slug, {
                source: 'DI',
                slug,
                iconValue,
                previewHtml: `<img src="${escapeHtml(`${DASHBOARD_ICONS_CDN}/${fmt}/${filename}`)}" alt="" loading="lazy" />`
            });
        });
    });

    return Array.from(bySlug.values()).sort((a, b) => a.slug.localeCompare(b.slug));
}

function parseMdiCssText(cssText) {
    if (typeof cssText !== 'string' || !cssText) return [];
    const seen = new Set();
    const regex = /\.mdi-([a-z0-9-]+)(?:::?before)/g;
    let match;
    while ((match = regex.exec(cssText)) !== null) {
        const slug = match[1];
        if (slug && !seen.has(slug)) {
            seen.add(slug);
        }
    }
    return Array.from(seen).sort().map((slug) => ({
        source: 'MDI',
        slug,
        iconValue: `mdi-${slug}`,
        previewHtml: `<span class="mdi mdi-${escapeHtml(slug)}"></span>`
    }));
}

function extractMdiIconsFromStylesheets() {
    if (typeof document === 'undefined' || !document.styleSheets) return [];
    const seen = new Set();
    for (const sheet of Array.from(document.styleSheets)) {
        try {
            if (sheet.href && !sheet.href.includes('materialdesignicons')) continue;
            const rules = sheet.cssRules || [];
            for (const rule of Array.from(rules)) {
                const sel = rule.selectorText;
                if (!sel || !sel.includes('.mdi-')) continue;
                const matches = sel.matchAll(/\.mdi-([a-z0-9-]+)(?:::?before)/g);
                for (const m of matches) {
                    if (m[1]) seen.add(m[1]);
                }
            }
        } catch (e) {
            // Ignore cross-origin stylesheets if CORS headers are absent
        }
    }
    return Array.from(seen).sort().map((slug) => ({
        source: 'MDI',
        slug,
        iconValue: `mdi-${slug}`,
        previewHtml: `<span class="mdi mdi-${escapeHtml(slug)}"></span>`
    }));
}

function filterCatalogItems(catalog, query) {
    if (!Array.isArray(catalog) || catalog.length === 0) return [];
    if (!query) return catalog;

    const exact = [];
    const prefix = [];
    const sub = [];
    for (const item of catalog) {
        const lower = item.slug.toLowerCase();
        if (lower === query) {
            exact.push(item);
        } else if (lower.startsWith(query)) {
            prefix.push(item);
        } else if (lower.includes(query)) {
            sub.push(item);
        }
    }
    return exact.concat(prefix, sub);
}

function searchIconCatalogs(rawInput) {
    const trimmed = (rawInput || '').trim().toLowerCase();
    if (/^https?:\/\//i.test(trimmed)) {
        return { items: [], totalMatches: 0, isUrl: true };
    }

    let searchDi = true;
    let searchMdi = true;
    let query = trimmed;

    if (trimmed.startsWith('di:')) {
        searchMdi = false;
        query = trimmed.slice(3).replace(/^\/+/, '').replace(/\.(svg|webp|png)$/i, '');
    } else if (trimmed.startsWith('mdi-')) {
        searchDi = false;
        query = trimmed.slice(4);
    } else if (trimmed.startsWith('mdi:')) {
        searchDi = false;
        query = trimmed.slice(4);
    }

    const diMatches = searchDi ? filterCatalogItems(dashboardIconsCatalog, query) : [];
    const mdiMatches = searchMdi ? filterCatalogItems(mdiIconsCatalog, query) : [];
    const totalMatches = diMatches.length + mdiMatches.length;

    let combined;
    if (searchDi && searchMdi) {
        // Interleave DI and MDI matches so both icon sets appear near the top
        combined = [];
        const maxLen = Math.max(diMatches.length, mdiMatches.length);
        for (let i = 0; i < maxLen && combined.length < ICON_SUGGESTIONS_LIMIT; i++) {
            if (i < diMatches.length) combined.push(diMatches[i]);
            if (combined.length < ICON_SUGGESTIONS_LIMIT && i < mdiMatches.length) combined.push(mdiMatches[i]);
        }
    } else {
        combined = (searchDi ? diMatches : mdiMatches).slice(0, ICON_SUGGESTIONS_LIMIT);
    }

    return { items: combined, totalMatches, isUrl: false };
}

function renderIconSuggestions(rawInput) {
    const panel = $('#iconSuggestions');
    const grid = $('#iconSuggestionsGrid');
    const status = $('#iconSuggestionsStatus');
    if (!panel.length || !grid.length) return;

    const val = rawInput !== undefined ? rawInput : $('#icon').val();
    const { items, totalMatches, isUrl } = searchIconCatalogs(val);

    if (isUrl) {
        panel.addClass('d-none');
        grid.empty();
        return;
    }

    if (dashboardIconsLoading || mdiIconsLoading) {
        if (items.length === 0) {
            status.text('Loading icon catalogs...');
            grid.empty();
            return;
        }
    }

    if (!Array.isArray(dashboardIconsCatalog) && !Array.isArray(mdiIconsCatalog)) {
        status.text('Icon catalogs unavailable (offline or CDN unreachable). You can still type mdi-* or di:* manually.');
        grid.empty();
        return;
    }

    if (items.length === 0) {
        status.text('No matching icons found.');
        grid.empty();
        return;
    }

    status.text(
        totalMatches > items.length
            ? `Showing ${items.length} of ${totalMatches} icons — keep typing to filter (use mdi- or di: to narrow).`
            : `${totalMatches} icon${totalMatches === 1 ? '' : 's'} found.`
    );

    const html = items.map((item) => `
        <button type="button" class="icon-suggestion-item" data-icon-value="${escapeHtml(item.iconValue)}" title="${escapeHtml(item.iconValue)}">
            <span class="icon-source-badge">${escapeHtml(item.source)}</span>
            ${item.previewHtml}
            <span class="icon-suggestion-label">${escapeHtml(item.slug)}</span>
        </button>`).join('');
    grid.html(html);
}

async function loadDashboardIconsCatalog() {
    if (Array.isArray(dashboardIconsCatalog) || dashboardIconsLoading) return;
    dashboardIconsLoading = true;
    try {
        const response = await fetch(DASHBOARD_ICONS_TREE_URL);
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        const treeData = await response.json();
        dashboardIconsCatalog = buildDashboardIconsCatalog(treeData);
    } catch (err) {
        console.warn('[DashboardIcons] Failed to load catalog:', err);
    } finally {
        dashboardIconsLoading = false;
    }
}

async function loadMdiIconsCatalog() {
    if (Array.isArray(mdiIconsCatalog) || mdiIconsLoading) return;
    const fromSheets = extractMdiIconsFromStylesheets();
    if (fromSheets.length > 0) {
        mdiIconsCatalog = fromSheets;
        return;
    }

    mdiIconsLoading = true;
    try {
        const response = await fetch(MDI_CSS_URL);
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        const cssText = await response.text();
        mdiIconsCatalog = parseMdiCssText(cssText);
    } catch (err) {
        console.warn('[MDI] Failed to load catalog:', err);
    } finally {
        mdiIconsLoading = false;
    }
}

async function loadIconCatalogs() {
    await Promise.all([loadDashboardIconsCatalog(), loadMdiIconsCatalog()]);
    if (!$('#iconSuggestions').hasClass('d-none')) {
        renderIconSuggestions($('#icon').val());
    }
}

function openIconSuggestions() {
    const val = $('#icon').val();
    if (/^https?:\/\//i.test((val || '').trim())) {
        $('#iconSuggestions').addClass('d-none');
        return;
    }
    $('#iconSuggestions').removeClass('d-none');
    loadIconCatalogs();
    renderIconSuggestions(val);
}

$('#icon').on('focus', function() {
    openIconSuggestions();
});

$('#icon').on('input', function() {
    updateIconPreview();
    openIconSuggestions();
});

$('#icon').on('keydown', function(e) {
    if (e.key === 'Escape') {
        $('#iconSuggestions').addClass('d-none');
    }
});

$(document).on('click', '.icon-suggestion-item', function() {
    const iconVal = $(this).data('icon-value');
    $('#icon').val(iconVal);
    updateIconPreview();
    $('#iconSuggestions').addClass('d-none');
});

$(document).on('click', function(e) {
    const wrapper = $('#iconFieldWrapper');
    if (wrapper.length && !wrapper[0].contains(e.target)) {
        $('#iconSuggestions').addClass('d-none');
    }
});

$('#entityModal').on('shown.bs.modal', function () {
    updateIconPreview();
    $("#name").trigger("focus");
});

$('#entityModal').on('hidden.bs.modal', function () {
    $('#entityForm')[0].reset();
    $('#entityForm').removeClass('was-validated');
    $('#disabledEnabled').prop('checked', false);
    $("#saveButton").prop('disabled', false);
    $('#id').val("");
    $('#iconSuggestions').addClass('d-none');
    updateIconPreview();
});

$('#deleteConfirmModal').on('hidden.bs.modal', function () {
    entityToDelete = "";
});

$('#addButton').click(function() {
    updateIconPreview();
    $('#entityModal').modal('show');
});

$(document).on('click', '.btn-edit', function() {
    const oldName = $(this).data("name");
    $.getJSON(`/api/monitoredentity/${encodeURIComponent(oldName)}`, function(entity) {
        $('#name').val(entity.name);
        $('#address').val(entity.address);
        $('#icon').val(entity.icon);
        updateIconPreview();
        $('#notificationsEnabled').prop('checked', entity.notification);
        $('#disabledEnabled').prop('checked', entity.disabled);
        $('#id').val(entity.name);
        $('#entityModal').modal('show');
    }).fail(function() {
        showError("Failed to load entity details.");
    });
});

$('#saveButton').click(function(event) {
    $(this).prop('disabled', true);
    event.preventDefault();
    $('#entityForm').addClass('was-validated');
    if (!$('#entityForm')[0].checkValidity()) {
        $(this).prop('disabled', false);
        return;
    }

    const entity = {
        name: $('#name').val(),
        address: $('#address').val(),
        icon: $('#icon').val() || "mdi-server",
        notification: $('#notificationsEnabled').is(':checked'),
        disabled: $('#disabledEnabled').is(':checked')
    };

    let url = '/api/monitoredentity';
    let method = 'POST';
    if ($('#id').val()) {
        url += '/' + encodeURIComponent($('#id').val());
        method = 'PUT';
    }

    let payloadJSON = JSON.stringify(entity);
    $.ajax({
        url: url,
        type: method,
        contentType: 'application/json',
        data: payloadJSON,
        success: function(response) {
            $('#entityModal').modal('hide');
            $('#entityForm')[0].reset();
            loadEntities();
            showSuccess("Entity saved successfully");
        },
        error: function(xhr) {
            showError("Error saving entity: " + xhr.responseText);
        }
    }).always(function() {
        $('#saveButton').prop('disabled', false);
    });
});

$(document).on('click', '.btn-delete', function() {
    entityToDelete = $(this).data("name");
    $('#deleteEntityNameDisplay').text(entityToDelete);
    $('#deleteConfirmModal').modal('show');
});

$('#confirmDeleteButton').click(function() {
    const $btn = $(this);
    if (!entityToDelete || $btn.prop('disabled')) {
        return;
    }

    $btn.prop('disabled', true).text('Deleting...');
    const targetName = entityToDelete;

    $.ajax({
        url: `/api/monitoredentity/${encodeURIComponent(targetName)}`,
        type: 'DELETE',
        success: function(result) {
            loadEntities();
            showSuccess(`"${targetName}" has been removed.`);
        },
        error: function(xhr) {
            showError("Error: " + xhr.responseText);
        }
    }).always(function() {
        $btn.prop('disabled', false).text('Delete');
        $('#deleteConfirmModal').modal('hide');
        $('#deleteEntityNameDisplay').text('');
        entityToDelete = "";
    });
});

function showSuccess(msg) {
    showToast('#successToast', msg);
}

function showError(msg) {
    showToast('#errorToast', msg);  
}

function showToast(toastId, msg) {
    const toastObj = $(toastId);
    toastObj.find('.toast-body').text(msg);
    const toast = new bootstrap.Toast(toastObj[0], { delay: 3000 });
    toast.show();    
}

function loadSystemStats() {
    $.getJSON("/api/sysinfo", function(data) {
        updateSysinfoUI(data);
    });
}

function updateSysinfoUI(data) {
    if (!data) return;
    if (data.uptime !== undefined) $("#uptimeVal").text(formatUptime(data.uptime));
    if (data.rssi !== undefined) {
        $("#rssiVal").text(data.rssi + " dBm");
        $("#rssiIcon").removeClass("text-success text-warning text-danger");
        $("#rssiVal").removeClass("text-success text-warning text-danger");
        if (data.rssi > -60) {
            $("#rssiIcon").addClass("text-success");
            $("#rssiVal").addClass("text-success");
        } else if (data.rssi > -80) {
            $("#rssiIcon").addClass("text-warning");
            $("#rssiVal").addClass("text-warning");
        } else {
            $("#rssiIcon").addClass("text-danger");
            $("#rssiVal").addClass("text-danger");
        }
    }
    if (data.freeHeap !== undefined) {
        $("#heapVal").text((data.freeHeap / 1024).toFixed(0) + " KB");
    }
    if (data.temperature !== undefined && data.temperature !== null) {
        $("#tempVal").text(data.temperature.toFixed(1) + " °C");
        $("#tempContainer").show();
    } else {
        $("#tempContainer").hide();
    }
    
    if (data.version !== undefined) {
        $("#versionVal").text(data.version);
        $("#menuVersionVal").text(data.version);
        $("#sysFwVersion").text(data.version);
        const releaseUrl = getGithubReleaseUrl(data.version);
        $("#versionLink").attr('href', releaseUrl);
        $("#menuVersionLink").attr('href', releaseUrl);
        $("#sysFwVersionLink").attr('href', releaseUrl);
    }
    if (data.commit !== undefined) {
        const commitText = '#' + data.commit;
        $("#commitVal").text(commitText);
        $("#menuCommitVal").text(commitText);
        $("#sysCommit").text(commitText);
        const commitUrl = getGithubCommitUrl(data.commit);
        $("#commitLink").attr('href', commitUrl);
        $("#menuCommitLink").attr('href', commitUrl);
        $("#sysCommitLink").attr('href', commitUrl);
    }
    if (data.buildDate !== undefined) $("#buildDateVal").text(data.buildDate);

    if (data.configEnabled !== undefined) {
        if (data.configEnabled) {
            $('#configButton').show();
            $('#headerConfigBadge').removeClass('d-none');
            const remaining = (data.configTimeoutRemaining !== undefined && data.configTimeoutRemaining > 0)
                ? data.configTimeoutRemaining : (configTimeoutRemaining > 0 ? configTimeoutRemaining : 600);
            setConfigCountdown(remaining, true);
        } else {
            $('#configButton').hide();
            $('#headerConfigBadge').addClass('d-none');
            setConfigCountdown(0, false);
        }
    }

    updateLiveSysinfoModalFields(data);
}

function formatBytes(bytes) {
    if (bytes === undefined || bytes === null || isNaN(bytes) || bytes < 0) return '--';
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
    return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
}

function getUsageBarClass(percent) {
    if (percent >= 85) return 'bg-danger';
    if (percent >= 70) return 'bg-warning';
    return 'bg-success';
}

function setProgressBar($bar, used, total) {
    if (!total || total <= 0) {
        $bar.css('width', '0%').removeClass('bg-success bg-warning bg-danger bg-info').addClass('bg-secondary');
        return;
    }
    const pct = Math.min(100, Math.max(0, Math.round((used / total) * 100)));
    $bar.css('width', `${pct}%`)
        .removeClass('bg-success bg-warning bg-danger bg-secondary bg-info')
        .addClass(getUsageBarClass(pct));
}

function formatRssiHtml(rssi) {
    if (rssi === undefined || rssi === null || isNaN(rssi)) return '--';
    let label = 'Weak';
    let cls = 'bg-danger';
    if (rssi > -55) {
        label = 'Excellent';
        cls = 'bg-success';
    } else if (rssi > -70) {
        label = 'Good';
        cls = 'bg-success';
    } else if (rssi > -80) {
        label = 'Fair';
        cls = 'bg-warning text-dark';
    }
    return `${rssi} dBm <span class="badge ${cls} ms-1">${label}</span>`;
}

function updateLiveSysinfoModalFields(data) {
    if (!data) return;

    if (data.uptime !== undefined) {
        $('#sysModalUptime').text(formatUptime(data.uptime));
    }

    if (data.temperature !== undefined && data.temperature !== null) {
        $('#sysTemp').html(`${data.temperature.toFixed(1)} °C <span class="text-muted">(internal)</span>`);
    } else if (lastDetailedSysinfo && lastDetailedSysinfo.device && !lastDetailedSysinfo.device.hasTempSensor) {
        $('#sysTemp').text('N/A');
    }

    if (data.heapSize !== undefined && data.freeHeap !== undefined) {
        const usedHeap = Math.max(0, data.heapSize - data.freeHeap);
        $('#sysHeapText').text(`${formatBytes(usedHeap)} / ${formatBytes(data.heapSize)}`);
        setProgressBar($('#sysHeapBar'), usedHeap, data.heapSize);
    } else if (data.freeHeap !== undefined) {
        $('#sysHeapText').text(`${formatBytes(data.freeHeap)} free`);
    }

    if (data.minFreeHeap !== undefined) {
        $('#sysMinFreeHeap').text(formatBytes(data.minFreeHeap));
    }
    if (data.maxAllocHeap !== undefined) {
        $('#sysMaxAllocHeap').text(formatBytes(data.maxAllocHeap));
    }
    if (data.maxFreeBlock !== undefined) {
        $('#sysMaxFreeBlock').text(formatBytes(data.maxFreeBlock));
    }

    if (data.psramSize !== undefined) {
        if (data.psramSize > 0 && data.freePsram !== undefined) {
            $('#sysPsramRow').show();
            const usedPsram = Math.max(0, data.psramSize - data.freePsram);
            $('#sysPsramText').text(`${formatBytes(usedPsram)} / ${formatBytes(data.psramSize)}`);
            setProgressBar($('#sysPsramBar'), usedPsram, data.psramSize);
        } else {
            $('#sysPsramRow').hide();
        }
    }
    if (data.minFreePsram !== undefined) {
        $('#sysMinFreePsram').text(formatBytes(data.minFreePsram));
    }
    if (data.maxAllocPsram !== undefined) {
        $('#sysMaxAllocPsram').text(formatBytes(data.maxAllocPsram));
    }

    if (data.rssi !== undefined) {
        $('#sysNetRssi').html(formatRssiHtml(data.rssi));
    }
    if (data.wsClients !== undefined) {
        $('#sysNetWsClients').text(String(data.wsClients));
    }
    if (data.lastCheckDurationMs !== undefined) {
        const when = data.lastCheckTime ? ` @ ${data.lastCheckTime.split(' ')[1] || data.lastCheckTime}` : '';
        $('#sysLastCheck').text(`${data.lastCheckDurationMs} ms${when}`);
    }
    if (data.stackHighWaterBytes !== undefined) {
        $('#sysMonStack').text(data.stackHighWaterBytes ? formatBytes(data.stackHighWaterBytes) : 'N/A');
    }

    if (data.timeSynced !== undefined || data.localTime !== undefined) {
        if (data.timeSynced && data.localTime) {
            $('#sysLocalTime').html(`${data.localTime} <span class="badge bg-success ms-1">Synced</span>`);
        } else {
            $('#sysLocalTime').html('<span class="badge bg-warning text-dark">Not synced</span>');
        }
    }

    if (data.configEnabled !== undefined) {
        $('#sysConfigMode').html(data.configEnabled
            ? '<span class="badge bg-warning text-dark">Unlocked</span>'
            : '<span class="badge bg-secondary">Locked</span>');
    }

    if (lastDetailedSysinfo) {
        ['temperature', 'heapSize', 'freeHeap', 'minFreeHeap', 'maxAllocHeap', 'maxFreeBlock',
         'psramSize', 'freePsram', 'minFreePsram', 'maxAllocPsram', 'rssi', 'wsClients',
         'uptime', 'localTime', 'timeSynced', 'lastCheckDurationMs', 'lastCheckTime',
         'stackHighWaterBytes', 'configEnabled'].forEach(key => {
            if (data[key] !== undefined) {
                lastDetailedSysinfo[key] = data[key];
            }
        });
        if (lastDetailedSysinfo.memory) {
            if (data.freeHeap !== undefined) lastDetailedSysinfo.memory.freeHeap = data.freeHeap;
            if (data.minFreeHeap !== undefined) lastDetailedSysinfo.memory.minFreeHeap = data.minFreeHeap;
            if (data.maxAllocHeap !== undefined) lastDetailedSysinfo.memory.maxAllocHeap = data.maxAllocHeap;
            if (data.maxFreeBlock !== undefined) lastDetailedSysinfo.memory.maxFreeBlock = data.maxFreeBlock;
            if (data.freePsram !== undefined) lastDetailedSysinfo.memory.freePsram = data.freePsram;
            if (data.minFreePsram !== undefined) lastDetailedSysinfo.memory.minFreePsram = data.minFreePsram;
            if (data.maxAllocPsram !== undefined) lastDetailedSysinfo.memory.maxAllocPsram = data.maxAllocPsram;
        }
        if (lastDetailedSysinfo.network) {
            if (data.rssi !== undefined) lastDetailedSysinfo.network.rssi = data.rssi;
            if (data.wsClients !== undefined) lastDetailedSysinfo.network.wsClients = data.wsClients;
        }
        if (lastDetailedSysinfo.monitoring) {
            if (data.lastCheckDurationMs !== undefined) lastDetailedSysinfo.monitoring.lastCheckDurationMs = data.lastCheckDurationMs;
            if (data.lastCheckTime !== undefined) lastDetailedSysinfo.monitoring.lastCheckTime = data.lastCheckTime;
            if (data.stackHighWaterBytes !== undefined) lastDetailedSysinfo.monitoring.stackHighWaterBytes = data.stackHighWaterBytes;
        }
        if (lastDetailedSysinfo.time) {
            if (data.uptime !== undefined) lastDetailedSysinfo.time.uptime = data.uptime;
            if (data.localTime !== undefined) lastDetailedSysinfo.time.localTime = data.localTime;
            if (data.timeSynced !== undefined) lastDetailedSysinfo.time.timeSynced = data.timeSynced;
        }
    }

    const nowStr = new Date().toLocaleTimeString();
    $('#sysinfoLastUpdated').text(`Updated: ${nowStr}`);
}

function renderDetailedSysinfoModal(data) {
    if (!data) return;
    lastDetailedSysinfo = data;

    // Device & CPU
    const dev = data.device || {};
    $('#sysBoard').text(dev.board || '--');
    $('#sysChip').text(dev.chipModel ? `${dev.chipModel} (rev ${dev.chipRevision ?? 0})` : '--');
    $('#sysCpu').text(dev.chipCores ? `${dev.chipCores} core${dev.chipCores > 1 ? 's' : ''} @ ${dev.cpuFreqMHz} MHz` : '--');
    $('#sysResetReason').text(dev.resetReason || '--');
    $('#sysEfuseMac').text(dev.efuseMac || '--');
    if (!dev.hasTempSensor && data.temperature === undefined) {
        $('#sysTemp').text('N/A');
    }

    // Memory & Flash
    const mem = data.memory || {};
    const heapSize = mem.heapSize ?? data.heapSize;
    const freeHeap = mem.freeHeap ?? data.freeHeap;
    if (heapSize !== undefined && freeHeap !== undefined) {
        const usedHeap = Math.max(0, heapSize - freeHeap);
        $('#sysHeapText').text(`${formatBytes(usedHeap)} / ${formatBytes(heapSize)}`);
        setProgressBar($('#sysHeapBar'), usedHeap, heapSize);
    }
    if (mem.minFreeHeap !== undefined) $('#sysMinFreeHeap').text(formatBytes(mem.minFreeHeap));
    if (mem.maxAllocHeap !== undefined) $('#sysMaxAllocHeap').text(formatBytes(mem.maxAllocHeap));
    if (mem.maxFreeBlock !== undefined) $('#sysMaxFreeBlock').text(formatBytes(mem.maxFreeBlock));

    if (mem.psramSize > 0) {
        $('#sysPsramRow').show();
        const usedPsram = Math.max(0, mem.psramSize - (mem.freePsram || 0));
        $('#sysPsramText').text(`${formatBytes(usedPsram)} / ${formatBytes(mem.psramSize)}`);
        setProgressBar($('#sysPsramBar'), usedPsram, mem.psramSize);
        $('#sysMinFreePsram').text(formatBytes(mem.minFreePsram));
        $('#sysMaxAllocPsram').text(formatBytes(mem.maxAllocPsram));
    } else {
        $('#sysPsramRow').hide();
    }
    $('#sysFlash').text(mem.flashSize ? `${formatBytes(mem.flashSize)} (${Math.round((mem.flashSpeed || 0) / 1000000)} MHz)` : '--');
    $('#sysSketch').text(mem.sketchSize ? `${formatBytes(mem.sketchSize)} / ${formatBytes(mem.freeSketchSpace)} free` : '--');

    // Storage
    const st = data.storage || {};
    $('#sysStorageBackend').text(st.backend || '--');
    $('#sysStorageIcon')
        .removeClass('mdi-sd mdi-harddisk')
        .addClass(st.backend === 'SD_MMC' ? 'mdi-sd' : 'mdi-harddisk');
    if (st.available) {
        $('#sysStorageStatus').text('Mounted').removeClass('bg-secondary bg-danger').addClass('bg-success');
        $('#sysStorageUsageRow').show();
        $('#sysStorageText').text(`${formatBytes(st.usedBytes)} / ${formatBytes(st.totalBytes)}`);
        $('#sysStorageFree').text(formatBytes(st.freeBytes));
        setProgressBar($('#sysStorageBar'), st.usedBytes, st.totalBytes);
    } else {
        $('#sysStorageStatus').text('Unavailable').removeClass('bg-secondary bg-success').addClass('bg-danger');
        $('#sysStorageUsageRow').hide();
    }
    if (st.cardType) {
        $('.sysinfo-sd-field').removeClass('d-none');
        $('#sysSdCardInfo').text(st.cardSizeBytes ? `${st.cardType} (${formatBytes(st.cardSizeBytes)})` : st.cardType);
    } else {
        $('.sysinfo-sd-field').addClass('d-none');
    }

    // Network & Wi-Fi
    const net = data.network || {};
    const modeLabel = net.mode || '--';
    $('#sysNetMode')
        .text(net.connected ? `${modeLabel} · Connected` : `${modeLabel} · Disconnected`)
        .removeClass('bg-secondary bg-success bg-danger')
        .addClass(net.connected ? 'bg-success' : 'bg-danger');
    $('#sysNetSsid').text(net.ssid ? `${net.ssid} (ch ${net.channel ?? '--'})` : '--');
    $('#sysNetBssid').text(net.bssid || 'N/A');
    $('#sysNetIp').text(net.ip || '--');
    $('#sysNetGateway').text(net.gateway ? `${net.gateway} / ${net.subnetMask || '--'}` : 'N/A');
    $('#sysNetDns').text(net.dns || 'N/A');
    $('#sysNetMac').text(net.mac || '--');
    if (net.hostname) {
        const mdnsHost = `${net.hostname}.local`;
        const $link = $('<a>')
            .attr('href', `http://${mdnsHost}`)
            .attr('target', '_blank')
            .attr('rel', 'noopener noreferrer')
            .addClass('text-decoration-none')
            .text(mdnsHost);
        $('#sysNetHostname').empty().append($link);
    } else {
        $('#sysNetHostname').text('--');
    }
    $('#sysNetWsClients').text(net.wsClients !== undefined ? String(net.wsClients) : '--');

    // Time & Monitoring
    const tm = data.time || {};
    const tzOffset = tm.timezoneOffset !== undefined ? `UTC${tm.timezoneOffset >= 0 ? '+' : ''}${tm.timezoneOffset}` : '';
    $('#sysSntp').text(tm.sntpServer ? `${tm.sntpServer}${tzOffset ? ' (' + tzOffset + ')' : ''}` : '--');

    const mon = data.monitoring || {};
    $('#sysCheckInterval').text(mon.checkInterval !== undefined ? `${mon.checkInterval} s` : '--');
    if (mon.lastCheckDurationMs !== undefined) {
        const when = mon.lastCheckTime ? ` @ ${mon.lastCheckTime.split(' ')[1] || mon.lastCheckTime}` : '';
        $('#sysLastCheck').text(`${mon.lastCheckDurationMs} ms${when}`);
    } else {
        $('#sysLastCheck').text('--');
    }
    $('#sysMonStack').text(mon.stackHighWaterBytes ? `${formatBytes(mon.stackHighWaterBytes)}` : 'N/A');

    // Firmware & Build
    const fw = data.firmware || {};
    const fwVer = fw.version || data.version;
    $('#sysFwVersion').text(fwVer || '--');
    $('#sysFwVersionLink').attr('href', getGithubReleaseUrl(fwVer));
    if (fw.uiVersion) {
        const mismatch = fw.expectedUiVersion && fw.uiVersion !== fw.expectedUiVersion;
        const $ui = $('#sysUiVersion').empty().text(fw.uiVersion);
        if (mismatch) {
            $ui.append(' ').append($('<span>').addClass('badge bg-warning text-dark ms-1').text('Mismatch'));
        }
    } else {
        $('#sysUiVersion').text('Unknown');
    }
    const commitHash = fw.commit || (data.commit ? data.commit : null);
    $('#sysCommit').text(commitHash ? `#${commitHash}` : '--');
    $('#sysCommitLink').attr('href', getGithubCommitUrl(commitHash));
    $('#sysBuildDate').text(fw.buildDate || '--');
    $('#sysOtaPartition').text(fw.otaPartition || '--');
    $('#sysSdk').text(fw.coreVersion ? `${fw.coreVersion} / ${fw.sdkVersion || ''}` : (fw.sdkVersion || '--'));

    updateLiveSysinfoModalFields(data);
}

function loadDetailedSysinfo() {
    const $icon = $('#sysinfoRefreshIcon');
    const $btn = $('#btnRefreshSysinfo');
    $icon.addClass('spin-animation');
    $btn.prop('disabled', true);
    $('#sysinfoError').addClass('d-none').text('');
    if (!lastDetailedSysinfo) {
        $('#sysinfoLoading').removeClass('d-none');
    }

    return $.getJSON('/api/sysinfo/details', function(data) {
        renderDetailedSysinfoModal(data);
    }).fail(function() {
        $('#sysinfoError').text('Failed to load detailed system metrics.').removeClass('d-none');
    }).always(function() {
        $('#sysinfoLoading').addClass('d-none');
        $icon.removeClass('spin-animation');
        $btn.prop('disabled', false);
    });
}

function formatUptime(seconds) {
    if (isNaN(seconds) || seconds < 0) return "--";

    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);

    let result = "";
    if (days > 0) result += days + "d ";
    if (hours > 0 || days > 0) result += hours + "h ";
    result += minutes + "m";

    return result;
}

function sendWsMessage(obj) {
    if (ws && ws.readyState === 1) { // 1 = WebSocket.OPEN
        ws.send(JSON.stringify(obj));
    }
}

function toggleTerminal(force) {
    terminalOpen = (typeof force === 'boolean') ? force : !terminalOpen;
    const $drawer = $('#terminalDrawer');
    const $btn = $('#btnToggleTerminal');
    if (terminalOpen) {
        $drawer.addClass('open');
        $btn.addClass('active btn-secondary').removeClass('btn-outline-secondary');
        sendWsMessage({ type: 'subscribe_logs', enabled: true });
        scrollToTerminalBottom();
    } else {
        $drawer.removeClass('open');
        $btn.removeClass('active btn-secondary').addClass('btn-outline-secondary');
        sendWsMessage({ type: 'subscribe_logs', enabled: false });
    }
}

function appendLogLine(line) {
    if (!line) return;
    terminalLogsBuffer.push(line);
    if (terminalLogsBuffer.length > MAX_TERMINAL_LOGS) {
        terminalLogsBuffer.shift();
    }
    $('#terminalLineCount').text(`${terminalLogsBuffer.length} lines`);

    let levelClass = '';
    if (/\[DEBUG\]|\[TRACE\]|\[VERBOSE\]/i.test(line)) {
        levelClass = 'terminal-log-level-debug';
    } else if (/\[INFO\]|\[NOTICE\]/i.test(line)) {
        levelClass = 'terminal-log-level-info';
    } else if (/\[WARN|WARNING\]/i.test(line)) {
        levelClass = 'terminal-log-level-warning';
    } else if (/\[ERROR|CRITICAL|ALERT|EMERGENCY\]/i.test(line)) {
        levelClass = 'terminal-log-level-error';
    }

    const $entry = $('<div>')
        .addClass('terminal-log-entry ' + levelClass)
        .text(line);

    const $container = $('#terminalLogs');
    $container.append($entry);

    while ($container.children().length > MAX_TERMINAL_LOGS) {
        $container.children().first().remove();
    }

    if (terminalAutoScroll) {
        scrollToTerminalBottom();
    }
}

function scrollToTerminalBottom() {
    const body = document.getElementById('terminalBody');
    if (body) {
        body.scrollTop = body.scrollHeight;
    }
}

function toggleHeaderMenu(force) {
    const $menu = $('#headerMenuDropdown');
    const $btn = $('#headerMenuButton');
    if (!$menu.length) return;
    const isOpen = (typeof force === 'boolean') ? force : !$menu.hasClass('show');
    $menu.toggleClass('show', isOpen);
    $btn.attr('aria-expanded', isOpen ? 'true' : 'false');
}

$('#headerMenuButton').on('click', function(e) {
    e.stopPropagation();
    toggleHeaderMenu();
});

$('#headerMenuDropdown').on('click', '.dropdown-item', function() {
    toggleHeaderMenu(false);
});

$(document).on('click', function(e) {
    const $wrapper = $('#headerMenuWrapper');
    if ($wrapper.length && !$wrapper[0].contains(e.target)) {
        toggleHeaderMenu(false);
    }
});

$(document).on('keydown', function(e) {
    if (e.key === 'Escape') {
        toggleHeaderMenu(false);
    }
});

$('#btnToggleTerminal').on('click', () => toggleTerminal());
$('#btnTerminalClose').on('click', () => toggleTerminal(false));
$('#btnTerminalClear').on('click', () => {
    terminalLogsBuffer = [];
    $('#terminalLogs').empty();
    $('#terminalLineCount').text('0 lines');
});
$('#terminalAutoScroll').on('change', function() {
    terminalAutoScroll = $(this).is(':checked');
    if (terminalAutoScroll) scrollToTerminalBottom();
});
$('#terminalLogLevel').on('change', function() {
    if ($(this).is(':disabled')) return;
    const val = $(this).val();
    sendWsMessage({ type: 'set_log_level', level: val });
});

$('#filterAll').click(() => { currentFilter = 'ALL'; renderEntities(); });
$('#filterOnline').click(() => { currentFilter = 'ONLINE'; renderEntities(); });
$('#filterOffline').click(() => { currentFilter = 'OFFLINE'; renderEntities(); });

$(document).on('click', '#emptyAddButton', () => {
    updateIconPreview();
    $('#entityModal').modal('show');
});

$(document).on('click', '#emptyShowAllButton', () => {
    currentFilter = 'ALL';
    renderEntities();
});

$('#sysinfoModal').on('show.bs.modal', function() {
    loadDetailedSysinfo();
    if (sysinfoRefreshTimer) clearInterval(sysinfoRefreshTimer);
    sysinfoRefreshTimer = setInterval(() => {
        loadDetailedSysinfo();
    }, SYSINFO_DETAILS_REFRESH_MS);
});

$('#sysinfoModal').on('hidden.bs.modal', function() {
    if (sysinfoRefreshTimer) {
        clearInterval(sysinfoRefreshTimer);
        sysinfoRefreshTimer = null;
    }
});

$('#btnRefreshSysinfo').on('click', function() {
    loadDetailedSysinfo();
});

$('#btnCopySysinfo').on('click', function() {
    if (!lastDetailedSysinfo) return;
    const payload = JSON.stringify(lastDetailedSysinfo, null, 2);
    if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(payload).then(() => {
            showSuccess('System diagnostics copied to clipboard');
        }).catch(() => {
            showError('Failed to copy diagnostics');
        });
    } else {
        showError('Clipboard API unavailable');
    }
});

if (typeof window !== 'undefined') {
    window.formatConfigCountdown = formatConfigCountdown;
    window.setConfigCountdown = setConfigCountdown;
    window.get_configTimeoutRemaining = () => configTimeoutRemaining;
    window.set_configTimeoutRemaining = (v) => { configTimeoutRemaining = v; };
    window.formatUptime = formatUptime;
    window.formatBytes = formatBytes;
    window.loadSummary = loadSummary;
    window.updateSummaryUI = updateSummaryUI;
    window.loadEntities = loadEntities;
    window.renderEntities = renderEntities;
    window.loadSystemStats = loadSystemStats;
    window.updateSysinfoUI = updateSysinfoUI;
    window.loadDetailedSysinfo = loadDetailedSysinfo;
    window.renderDetailedSysinfoModal = renderDetailedSysinfoModal;
    window.get_lastDetailedSysinfo = () => lastDetailedSysinfo;
    window.set_lastDetailedSysinfo = (v) => { lastDetailedSysinfo = v; };
    window.showSuccess = showSuccess;
    window.showError = showError;
    window.initWebSocket = initWebSocket;
    window.handleWsMessage = handleWsMessage;
    window.handleEntityUpdate = handleEntityUpdate;
    window.handleEntityDelete = handleEntityDelete;
    window.triggerCardPulse = triggerCardPulse;
    window.setWsStatus = setWsStatus;
    window.toggleTerminal = toggleTerminal;
    window.toggleHeaderMenu = toggleHeaderMenu;
    window.appendLogLine = appendLogLine;
    window.sendWsMessage = sendWsMessage;
    window.get_terminalOpen = () => terminalOpen;
    window.set_terminalOpen = (v) => { terminalOpen = v; };
    window.get_terminalLogsBuffer = () => terminalLogsBuffer;
    window.clearTerminal = () => { terminalLogsBuffer = []; $('#terminalLogs').empty(); };
    window.get_ws = () => ws;
    window.set_ws = (v) => { ws = v; };
    window.get_allEntities = () => allEntities;
    window.set_allEntities = (v) => { allEntities = v; };
    window.get_currentFilter = () => currentFilter;
    window.set_currentFilter = (v) => { currentFilter = v; };
    window.get_entityToDelete = () => entityToDelete;
    window.GITHUB_REPO_URL = GITHUB_REPO_URL;
    window.getGithubReleaseUrl = getGithubReleaseUrl;
    window.getGithubCommitUrl = getGithubCommitUrl;
    window.set_entityToDelete = (v) => { entityToDelete = v; };
    window.resolveIconConfig = resolveIconConfig;
    window.renderIconHtml = renderIconHtml;
    window.updateIconPreview = updateIconPreview;
    window.buildDashboardIconsCatalog = buildDashboardIconsCatalog;
    window.parseMdiCssText = parseMdiCssText;
    window.searchIconCatalogs = searchIconCatalogs;
    window.renderIconSuggestions = renderIconSuggestions;
    window.loadDashboardIconsCatalog = loadDashboardIconsCatalog;
    window.loadMdiIconsCatalog = loadMdiIconsCatalog;
    window.loadIconCatalogs = loadIconCatalogs;
    window.get_dashboardIconsCatalog = () => dashboardIconsCatalog;
    window.set_dashboardIconsCatalog = (v) => { dashboardIconsCatalog = v; };
    window.get_mdiIconsCatalog = () => mdiIconsCatalog;
    window.set_mdiIconsCatalog = (v) => { mdiIconsCatalog = v; };
    window.getUsageBarClass = getUsageBarClass;
    window.setProgressBar = setProgressBar;
    window.formatRssiHtml = formatRssiHtml;
}
})();
