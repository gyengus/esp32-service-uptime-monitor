let allEntities = [];
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

$(document).ready(function() {
    initWebSocket();
    loadEntities();
    loadSystemStats();
    loadSummary();
});

function setWsStatus(connected) {
    const dot = $('#wsStatusDot');
    if (!dot.length) return;
    if (connected) {
        dot.removeClass('bg-secondary bg-danger').addClass('bg-success').attr('title', 'Live (WebSocket connected)');
    } else {
        dot.removeClass('bg-secondary bg-success').addClass('bg-danger').attr('title', 'Disconnected (Reconnecting...)');
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
        console.log('[WS] Connected to ' + wsUrl);
        wsReconnectDelay = 1000;
        setWsStatus(true);
    };

    ws.onmessage = function(event) {
        try {
            const msg = JSON.parse(event.data);
            console.log('[WS Received]', msg);
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
                allEntities = msg.entities;
                renderEntities();
            }
            if (msg.summary) updateSummaryUI(msg.summary);
            if (msg.sysinfo) updateSysinfoUI(msg.sysinfo);
            if (msg.logLevelName) {
                $('#terminalLogLevel').val(msg.logLevelName);
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

function loadEntities() {
    const icon = $("#refreshIcon");
    icon.addClass("spin-animation");
    $("#refreshButton").prop("disabled", true);

    loadSummary();
    $.getJSON('/api/monitoredentities', function(data) {
        allEntities = data;
        renderEntities();
    }).fail(function() {
        showError("Can't fetch monitored entities");
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

    $('#filterAll, #filterOnline, #filterOffline').removeClass('badge-active');
    $('#navStats').removeClass('nav-stats-filtering');

    if (currentFilter === 'ALL') {
        $('#filterAll').addClass('badge-active');
    } else {
        $('#navStats').addClass('nav-stats-filtering');
        if (currentFilter === 'ONLINE') $('#filterOnline').addClass('badge-active');
        if (currentFilter === 'OFFLINE') $('#filterOffline').addClass('badge-active');
    }

    let displayList = [...allEntities].sort((a, b) => 
        a.name.localeCompare(b.name, 'hu', { sensitivity: 'base' })
    );

    if (currentFilter === 'ONLINE') {
        displayList = displayList.filter(e => e.isOnline);
    } else if (currentFilter === 'OFFLINE') {
        displayList = displayList.filter(e => !e.isOnline && e.lastChanged);
    }

    if (displayList.length === 0) {
        grid.append('<div class="col-12 text-center text-secondary py-5"><h5>Nothing to show.</h5></div>');
        return;
    }

    displayList.forEach(entity => {
        let cardBgClass = entity.disabled ? "text-bg-secondary" : entity.lastChanged ? (entity.isOnline ? "text-bg-success" : "text-bg-danger") : "text-bg-light";
        let icon = entity.icon || "mdi-server";
        let notifyIcon = entity.notification ? '<span class="mdi mdi-bell-outline ms-2" title="Notifications ON"></span>' : '';
        let addressUrl = (entity.address.startsWith("http://") || entity.address.startsWith("https://")) 
            ? entity.address 
            : "http://" + entity.address;
        let address = `<a href="${addressUrl}" target="_blank" class="link-light text-truncate" style="max-width:150px">${entity.address}</a>`;

        const lastChanged = new Date(entity.lastChanged * 1000);
        const formatter = new Intl.DateTimeFormat('hu-HU', {
            year: 'numeric', month: '2-digit', day: '2-digit',
            hour: '2-digit', minute: '2-digit', second: '2-digit'
        });
        const lastChangedText = entity.lastChanged ? '<span class="mdi mdi-clock-outline"></span> ' + formatter.format(lastChanged) : "";

        let cardHtml = `
            <div class="col-12 col-md-6 col-lg-3">
                <div class="card h-100 shadow monitoredEntity ${cardBgClass}" data-entity-name="${entity.name}">
                    <div class="card-body position-relative">
                        <div class="position-absolute top-0 end-0 p-2 buttons">
                            <button class="btn btn-sm btn-outline-light text-light-emphasis opacity-75 btn-edit p-1" data-name="${entity.name}" title="Edit">
                                <span class="mdi mdi-pencil"></span>
                            </button>
                            <button class="btn btn-sm btn-outline-light text-light-emphasis opacity-75 btn-delete p-1" data-name="${entity.name}" title="Delete">
                                <span class="mdi mdi-delete"></span>
                            </button>
                        </div>

                        <h5 class="card-title d-flex align-items-center pe-5">
                            <span class="mdi ${icon} me-2 entityIcon"></span>
                            <span class="text-truncate">${entity.name}</span>
                            ${notifyIcon}
                        </h5>
                        <p class="card-text small mb-0">
                            ${entity.disabled ? '<span class="badge bg-secondary me-1 border border-white">Disabled</span>' : ''}
                            <span class="text-white-50">${address}</span>
                        </p>
                        <p class="card-text mt-2 mb-0" style="font-size: 0.75rem; opacity: 0.8;">
                            ${lastChangedText}
                        </p>
                    </div>
                </div>
            </div>`;
        grid.append(cardHtml);
    });
}

$('#entityModal').on('shown.bs.modal', function () {
    $("#name").trigger("focus");
});

$('#entityModal').on('hidden.bs.modal', function () {
    $('#entityForm')[0].reset();
    $('#entityForm').removeClass('was-validated');
    $('#disabledEnabled').prop('checked', false);
    $("#saveButton").prop('disabled', false);
    $('#id').val("");
});

$('#deleteConfirmModal').on('hidden.bs.modal', function () {
    entityToDelete = "";
});

$('#addButton').click(function() {
    $('#entityModal').modal('show');
});

$(document).on('click', '.btn-edit', function() {
    const oldName = $(this).data("name");
    $.getJSON(`/api/monitoredentity/${encodeURIComponent(oldName)}`, function(entity) {
        $('#name').val(entity.name);
        $('#address').val(entity.address);
        $('#icon').val(entity.icon);
        $('#notificationsEnabled').prop('checked', entity.notification);
        $('#disabledEnabled').prop('checked', entity.disabled);
        $('#id').val(entity.name);
        $('#entityModal').modal('show');
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
    if (!entityToDelete) {
        return;
    }

    $.ajax({
        url: `/api/monitoredentity/${encodeURIComponent(entityToDelete)}`,
        type: 'DELETE',
        success: function(result) {
            loadEntities();
            showSuccess(`"${entityToDelete}" has been removed.`);
        },
        error: function(xhr) {
            showError("Error: " + xhr.responseText);
        }
    }).always(function() {
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
    
    if (data.version !== undefined) $("#versionVal").text(data.version);
    if (data.commit !== undefined) $("#commitVal").text('#' + data.commit);
    if (data.buildDate !== undefined) $("#buildDateVal").text(data.buildDate);

    if (data.configEnabled !== undefined) {
        if (data.configEnabled) {
            $('#configButton').show();
        } else {
            $('#configButton').hide();
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
    $('#sysFwVersion').text(fw.version || data.version || '--');
    if (fw.uiVersion) {
        const mismatch = fw.expectedUiVersion && fw.uiVersion !== fw.expectedUiVersion;
        const $ui = $('#sysUiVersion').empty().text(fw.uiVersion);
        if (mismatch) {
            $ui.append(' ').append($('<span>').addClass('badge bg-warning text-dark ms-1').text('Mismatch'));
        }
    } else {
        $('#sysUiVersion').text('Unknown');
    }
    $('#sysCommit').text(fw.commit ? `#${fw.commit}` : '--');
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
    const val = $(this).val();
    sendWsMessage({ type: 'set_log_level', level: val });
});

$('#filterAll').click(() => { currentFilter = 'ALL'; renderEntities(); });
$('#filterOnline').click(() => { currentFilter = 'ONLINE'; renderEntities(); });
$('#filterOffline').click(() => { currentFilter = 'OFFLINE'; renderEntities(); });

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
    window.set_entityToDelete = (v) => { entityToDelete = v; };
}
