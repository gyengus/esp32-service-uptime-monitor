let allEntities = [];
let currentFilter = 'ALL';
let entityToDelete = "";
let ws = null;
let wsReconnectTimer = null;
let wsReconnectDelay = 1000;

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
    if (data.temperature !== undefined) {
        $("#tempVal").text(data.temperature ? data.temperature.toFixed(1) + " °C" : "--");
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

$('#filterAll').click(() => { currentFilter = 'ALL'; renderEntities(); });
$('#filterOnline').click(() => { currentFilter = 'ONLINE'; renderEntities(); });
$('#filterOffline').click(() => { currentFilter = 'OFFLINE'; renderEntities(); });

if (typeof window !== 'undefined') {
    window.formatUptime = formatUptime;
    window.loadSummary = loadSummary;
    window.updateSummaryUI = updateSummaryUI;
    window.loadEntities = loadEntities;
    window.renderEntities = renderEntities;
    window.loadSystemStats = loadSystemStats;
    window.updateSysinfoUI = updateSysinfoUI;
    window.showSuccess = showSuccess;
    window.showError = showError;
    window.initWebSocket = initWebSocket;
    window.handleWsMessage = handleWsMessage;
    window.handleEntityUpdate = handleEntityUpdate;
    window.handleEntityDelete = handleEntityDelete;
    window.triggerCardPulse = triggerCardPulse;
    window.setWsStatus = setWsStatus;
    window.get_ws = () => ws;
    window.set_ws = (v) => { ws = v; };
    window.get_allEntities = () => allEntities;
    window.set_allEntities = (v) => { allEntities = v; };
    window.get_currentFilter = () => currentFilter;
    window.set_currentFilter = (v) => { currentFilter = v; };
    window.get_entityToDelete = () => entityToDelete;
    window.set_entityToDelete = (v) => { entityToDelete = v; };
}
