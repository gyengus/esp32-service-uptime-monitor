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
            $('#slackWebhookURL').val(config.slackWebhookURL);
            $('#hostname').val(config.hostname);

            if (config.wifiSSID != "") {
                $('#factoryResetRow').show();
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
            slackWebhookURL: $('#slackWebhookURL').val(),
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

    $('#testSlackButton').on('click', function() {
        const btn = $(this);
        const icon = $("#testSlackButtonIcon");
        const text = $("#testSlackButtonText");
        const spinner = $('#testSlackButtonSpinner');
        const feedback = $("#testSlackFeedback");
        const webhook = $("#slackWebhookURL").val();

        if (!webhook) {
            showFeedback("Please enter a webhook URL first!", "text-danger");
            return;
        }

        btn.prop('disabled', true);
        text.text('Testing');
        spinner.removeClass('d-none');
        feedback.hide();

        $.post('/api/testSlack', { url: webhook })
            .done(function(data) {
                showFeedback("Test message sent! Check your Slack channel.", "text-success");
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