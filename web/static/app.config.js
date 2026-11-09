$.when($.ready).then(function() {
    var wifiScanInProgress = false;

    // Get current configuration from the server
    $.ajax({
        type: 'GET',
        url: '/api/config',
        dataType: "json",
        success: function(config) {
            $('#wifiSSID').val(config.wifiSSID);
            $('#wifiPassword').val(config.wifiPassword);
            $('#sntpServer').val(config.sntpServer);
            $('#offsetHours').val(config.offsetHours);
            $('#checkInterval').val(config.checkInterval);
            $('#discordWebhookURL').val(config.discordWebhookURL);
            $('#hostname').val(config.hostname);

            if (config.wifiSSID != "") {
                $('#factoryResetRow').show();
                $('#backupRow').show();
            }
        },
        error: function(xhr, status, error) {
            showError(xhr, "Can't fetch configuration");
        }
    });

    // Handle save button click
    $('#saveButton').on('click', function(event) {
        $(this).prop('disabled', true);
        event.preventDefault();
        $('#configForm').addClass('was-validated');
        if (!$('#configForm')[0].checkValidity()) {
            $(this).prop('disabled', false);
            return;
        }
        var data = JSON.stringify({
            wifiSSID: $('#wifiSSID').val(),
            wifiPassword: $('#wifiPassword').val(),
            sntpServer: $('#sntpServer').val(),
            offsetHours: parseInt($('#offsetHours').val()),
            checkInterval: parseInt($('#checkInterval').val()),
            discordWebhookURL: $('#discordWebhookURL').val(),
            hostname: $('#hostname').val()
        });
        $.ajax({
            type: 'POST',
            url: '/api/config',
            data: data,
            contentType: 'application/json',
            success: function(response) {
                $('html, body').animate({ scrollTop: 0 }, 100);
                showSuccess('Configuration saved successfully.' + (response.restarting ? ' The device will restart.' : ''));
                $('#saveButton').prop('disabled', false);
            },
            error: function(xhr, status, error) {
                showError(xhr, "Can't save configuration");
                $('#saveButton').prop('disabled', false);
            }
        });
    });

    $('#scanWifiButton').on('click', function() {
        if (wifiScanInProgress) {
            return;
        }
        wifiScanInProgress = true;
        let $btn = $(this);
        let $spinner = $('#btnSpinner');
        let $text = $('#btnText');

        $btn.prop('disabled', true);
        $spinner.removeClass('d-none');
        $text.text('Scanning');

        let container = $('#wifiNetworksContainer');
        container.fadeOut();
        container.empty();
        let tries = 0;
        const pollScan = () => {
            tries++;
            if (tries > 20) {
                finalizeScan();
                return;
            }
            $.getJSON('/api/wifiscan', function(data) {
                if (data.status === 'scanning' || data.status === 'in progress') {
                    setTimeout(pollScan, 1000);
                    return;
                } else if (data.status === 'done') {
                    let networks = data.networks || [];
                    let uniqueNetworks = {};
                    $.each(networks, function(i, net) {
                        if (!uniqueNetworks[net.ssid] || net.rssi > uniqueNetworks[net.ssid].rssi) {
                            uniqueNetworks[net.ssid] = net;
                        }
                    });
                    let sortedNetworks = Object.values(uniqueNetworks).sort((a, b) => b.rssi - a.rssi);
                    console.log("WiFi scan results:", JSON.stringify(sortedNetworks));
                    if (sortedNetworks.length === 0) {
                        container.append('<div>No networks found</div>');
                    } else {
                        $.each(sortedNetworks, function(i, net) {
                            let signalPercent = Math.min(Math.max(2 * (net.rssi + 100), 0), 100);
                            let networkDiv = $('<div></div>').addClass('wifi-network')
                                .text(net.ssid + ' (Signal: ' + net.rssi + ' dBm, ' + signalPercent + '%)');
                            networkDiv.on('click', function() {
                                $('#wifiSSID').val(net.ssid);
                                $('#wifiPassword').focus();
                            });
                            container.append(networkDiv);
                        });
                    }
                    container.fadeIn();
                    finalizeScan();
                } else {
                    console.log("Unknown status, data: " + JSON.stringify(data));
                    finalizeScan();
                }
            }).fail(function() {
                setTimeout(pollScan, 2000);
            });
        };

        const finalizeScan = () => {
            wifiScanInProgress = false;
            $btn.prop('disabled', false);
            $spinner.addClass('d-none');
            $text.text('Scan');
        }

        pollScan();
    });

    $('#testDiscordButton').on('click', function() {
        const btn = $(this);
        const icon = $("#testDiscordButtonIcon");
        const text = $("#testDiscordButtonText");
        const spinner = $('#testDiscordButtonSpinner');
        const feedback = $("#testDiscordFeedback");
        const webhook = $("#discordWebhookURL").val();

        if (!webhook) {
            showFeedback("Please enter a webhook URL first!", "text-danger");
            return;
        }

        btn.prop('disabled', true);
        text.text('Testing');
        spinner.removeClass('d-none');
        feedback.hide();

        $.post('/api/testDiscord', { url: webhook })
            .done(function(data) {
                showFeedback("Test message sent! Check your Discord channel.", "text-success");
            })
            .fail(function(xhr, status, error) {
                showFeedback(xhr.responseText, "text-danger");
            })
            .always(function() {
                btn.prop('disabled', false);
                text.text('Test');
                spinner.addClass('d-none');
            });

        function showFeedback(msg, cls) {
            feedback.text(msg).attr('class', 'form-text mt-2 ' + cls).fadeIn();
        }
    });

    $('#factoryResetButton').on('click', function() {
        $('#factoryResetConfirmModal').modal('show');
    });

    $('#confirmFactoryResetButton').on('click', function() {
        $.ajax({
            type: 'POST',
            url: '/api/config/factoryReset',
            success: function(response) {
                $('#factoryResetConfirmModal').modal('hide');
                showSuccess(response.message);
            },
            error: function(xhr, status, error) {
                $('#factoryResetConfirmModal').modal('hide');
                showError(xhr, "Factory reset failed");
            }
        });
    });
});

$('#backupButton').on('click', function() {
    $.ajax({
        type: 'GET',
        url: '/api/config/backup',
        success: function(data) {
            const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
            const url = URL.createObjectURL(blob);

            const a = document.createElement('a');
            a.href = url;
            a.download = 'backup.json';
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);

            URL.revokeObjectURL(url);

            showSuccess('Backup downloaded successfully');
        },
        error: function(xhr, status, error) {
            showError(xhr, "Backup failed");
        }
    });
});

$('#restoreButton').on('click', function() {
    $('#restoreFile').click();
});

$('#restoreFile').on('change', function(event) {
    const file = event.target.files[0];
    if (!file) return;

    const formData = new FormData();
    formData.append('file', file);

    $.ajax({
        type: 'POST',
        url: '/api/config/restore',
        data: formData,
        processData: false,
        contentType: false,
        success: function(response) {
            showSuccess('Restore completed. The device will restart.');
        },
        error: function(xhr, status, error) {
            showError(xhr, "Restore failed");
            $('#restoreFile').val('');
        }
    });
});

$('#updateWebButton').on('click', function() {
    $('#webArchiveFile').click();
});

$('#webArchiveFile').on('change', function(event) {
    const file = event.target.files[0];
    if (!file) return;

    let $btn = $('#updateWebButton');
    let $icon = $('#updateWebIcon');
    let $text = $('#updateWebText');
    let $spinner = $('#updateWebSpinner');

    $btn.prop('disabled', true);
    $icon.addClass('d-none');
    $spinner.removeClass('d-none');
    $text.text('Updating Web UI...');

    const formData = new FormData();
    formData.append('archive', file);

    $.ajax({
        type: 'POST',
        url: '/api/web/upload-archive',
        data: formData,
        processData: false,
        contentType: false,
        success: function(response) {
            $btn.prop('disabled', false);
            $spinner.addClass('d-none');
            $icon.removeClass('d-none');
            $text.text('Update Web UI (.zip)');
            $('#webArchiveFile').val('');
            showSuccess('Web UI updated successfully! Reloading page in 2s...');
            setTimeout(function() {
                window.location.reload();
            }, 2000);
        },
        error: function(xhr, status, error) {
            $btn.prop('disabled', false);
            $spinner.addClass('d-none');
            $icon.removeClass('d-none');
            $text.text('Update Web UI (.zip)');
            $('#webArchiveFile').val('');
            showError(xhr, "Web UI update failed");
        }
    });
});

$('#updateFirmwareButton').on('click', function() {
    $('#firmwareBinaryFile').click();
});

$('#firmwareBinaryFile').on('change', function(event) {
    const file = event.target.files[0];
    if (!file) return;

    let $btn = $('#updateFirmwareButton');
    let $icon = $('#updateFirmwareIcon');
    let $text = $('#updateFirmwareText');
    let $spinner = $('#updateFirmwareSpinner');
    let $progressContainer = $('#firmwareProgressContainer');
    let $progressBar = $('#firmwareProgressBar');
    let $progressText = $('#firmwareProgressText');

    $btn.prop('disabled', true);
    $icon.addClass('d-none');
    $spinner.removeClass('d-none');
    $text.text('Starting OTA...');
    $progressContainer.removeClass('d-none');
    $progressBar.removeClass('bg-success bg-danger').addClass('bg-warning');
    $progressBar.css('width', '0%');
    $progressText.text('0%');

    function resetFirmwareUI(errorMsg) {
        $btn.prop('disabled', false);
        $spinner.addClass('d-none');
        $icon.removeClass('d-none');
        $text.text('Update Firmware (.bin)');
        $('#firmwareBinaryFile').val('');
        if (errorMsg) {
            $progressBar.removeClass('bg-warning').addClass('bg-danger');
            showToast('#errorToast', errorMsg);
        } else {
            $progressContainer.addClass('d-none');
        }
    }

    $.ajax({
        type: 'GET',
        url: '/ota/start?mode=fr',
        success: function() {
            $text.text('Uploading firmware...');

            const formData = new FormData();
            formData.append('file', file, file.name);

            const xhr = new XMLHttpRequest();
            xhr.open('POST', '/ota/upload', true);

            if (xhr.upload) {
                xhr.upload.onprogress = function(e) {
                    if (e.lengthComputable && e.total > 0) {
                        const percent = Math.round((e.loaded / e.total) * 100);
                        $progressBar.css('width', percent + '%');
                        $progressText.text(percent + '% (' + Math.round(e.loaded / 1024) + ' / ' + Math.round(e.total / 1024) + ' KB)');
                    }
                };
            }

            xhr.onload = function() {
                if (xhr.status === 200) {
                    $progressBar.removeClass('bg-warning bg-danger').addClass('bg-success');
                    $progressBar.css('width', '100%');
                    $progressText.text('100% - Firmware update successful!');
                    $text.text('Rebooting...');
                    showSuccess('Firmware update successful! The device is restarting...');

                    let remaining = 12;
                    const timer = setInterval(function() {
                        remaining--;
                        if (remaining > 0) {
                            $text.text('Reconnecting in ' + remaining + 's...');
                        } else {
                            clearInterval(timer);
                            window.location.reload();
                        }
                    }, 1000);
                } else {
                    let errMsg = 'Firmware upload failed (status ' + xhr.status + ')';
                    if (xhr.responseText) errMsg += ': ' + xhr.responseText;
                    resetFirmwareUI(errMsg);
                }
            };

            xhr.onerror = function() {
                resetFirmwareUI('Network error occurred during firmware upload');
            };

            xhr.send(formData);
        },
        error: function(xhr) {
            let msg = 'Failed to initiate OTA session';
            if (xhr.responseText) msg += ': ' + xhr.responseText;
            resetFirmwareUI(msg);
        }
    });
});

function showError(xhr, defaultMsg) {
    let response = defaultMsg;
    try {
        response = JSON.parse(xhr.responseText).message;
    } catch (e) {
        console.error(e);
    }
    console.error(response);
    showToast('#errorToast', response);
}

function showSuccess(msg) {
    showToast('#successToast', msg);
}

function showToast(toastId, msg) {
    const toastObj = $(toastId);
    toastObj.find('.toast-body').text(msg);
    const toast = new bootstrap.Toast(toastObj[0], { delay: 3000 });
    toast.show();
}

if (typeof window !== 'undefined') {
    window.showError = showError;
    window.showSuccess = showSuccess;
    window.showToast = showToast;
}
