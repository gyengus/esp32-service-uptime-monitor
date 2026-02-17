let allEntities = [];
let currentFilter = 'ALL';
let entityToDelete = "";

$(document).ready(function() {
    loadEntities();
    loadSystemStats();
    loadSummary();
    setInterval(loadSummary, 10000);
    setInterval(loadSystemStats, 2000);
    setInterval(loadEntities, 30000); 
});

function loadSummary() {
    $.getJSON('/api/summary', function(stats) {
        if ($('#navTotal').text() != stats.total) {
            $('#navTotal').fadeOut(200, function() { $(this).text(stats.total).fadeIn(200); });
        }
        if ($('#navOnline').text() != stats.online) {
            $('#navOnline').fadeOut(200, function() { $(this).text(stats.online).fadeIn(200); });
        }
        if ($('#navOffline').text() != stats.offline) {
            $('#navOffline').fadeOut(200, function() { $(this).text(stats.offline).fadeIn(200); });
        }
    });
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
        let address = (entity.type === "HEAD")
            ? `<a href="${entity.address}" target="_blank" class="link-light text-truncate" style="max-width:150px">${entity.address}</a>`
            : entity.address;

        const lastChanged = new Date(entity.lastChanged * 1000);
        const formatter = new Intl.DateTimeFormat('hu-HU', {
            year: 'numeric', month: '2-digit', day: '2-digit',
            hour: '2-digit', minute: '2-digit', second: '2-digit'
        });
        const lastChangedText = entity.lastChanged ? '<span class="mdi mdi-clock-outline"></span> ' + formatter.format(lastChanged) : "";

        let cardHtml = `
            <div class="col-12 col-md-6 col-lg-3">
                <div class="card h-100 shadow monitoredEntity ${cardBgClass}">
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
                            <span class="badge bg-light text-dark me-1">${entity.type}</span>
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
    $('#typePing').prop('checked', true);
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
        $(`input[name='type'][value='${entity.type}']`).prop('checked', true);
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
        type: $("input[name='type']:checked").val(),
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
        $("#uptimeVal").text(formatUptime(data.uptime));
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
        $("#heapVal").text((data.freeHeap / 1024).toFixed(0) + " KB");
        $("#tempVal").text(data.temperature ? data.temperature.toFixed(1) + " °C" : "--");
        
        $("#versionVal").text(data.version);
        $("#commitVal").text('#' + data.commit);
        $("#buildDateVal").text(data.buildDate);

        if (data.configEnabled) {
            $('#configButton').show();
        } else {
            $('#configButton').hide();
        }
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

$('#filterAll').click(() => { currentFilter = 'ALL'; renderEntities(); });
$('#filterOnline').click(() => { currentFilter = 'ONLINE'; renderEntities(); });
$('#filterOffline').click(() => { currentFilter = 'OFFLINE'; renderEntities(); });