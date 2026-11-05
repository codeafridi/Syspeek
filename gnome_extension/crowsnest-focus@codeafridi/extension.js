import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

export default class CrowsnestFocusExtension extends Extension {
    enable() {
        this._pidFile = Gio.File.new_for_path(
            GLib.build_filenamev([GLib.get_user_runtime_dir(), 'crowsnest-active-pid'])
        );
        this._focusChangedId = global.display.connect(
            'notify::focus-window',
            () => this._writeFocusedPid()
        );
        this._writeFocusedPid();
    }

    disable() {
        if (this._focusChangedId) {
            global.display.disconnect(this._focusChangedId);
            this._focusChangedId = null;
        }

        try {
            this._pidFile.delete(null);
        } catch (_) {
            // The file may not exist when no app window was focused.
        }
        this._pidFile = null;
    }

    _writeFocusedPid() {
        const focusedWindow = global.display.get_focus_window();
        const pid = focusedWindow ? focusedWindow.get_pid() : 0;

        try {
            if (pid > 0) {
                this._pidFile.replace_contents(
                    `${pid}\n`,
                    null,
                    false,
                    Gio.FileCreateFlags.REPLACE_DESTINATION,
                    null
                );
            } else {
                this._pidFile.delete(null);
            }
        } catch (error) {
            logError(error, 'Crowsnest Focus Bridge could not update the focused PID');
        }
    }
}
