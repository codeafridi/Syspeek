function publish(pid) {
    callDBus("org.crowsnest.Focus", "/Focus",
             "org.crowsnest.Focus", "setFocusedPid",
             (pid > 0 ? pid : 0));
}

function onActivated(window) {
    publish(window && window.pid ? window.pid : 0);
}

try {
    workspace.windowActivated.connect(onActivated);
} catch (e) {
}

try {
    workspace.clientActivated.connect(onActivated);
} catch (e) {
}

var active = workspace.activeWindow ? workspace.activeWindow : workspace.activeClient;
if (active && active.pid) {
    publish(active.pid);
}
