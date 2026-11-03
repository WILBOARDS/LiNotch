#!/usr/bin/env python3
import gi
import sys

gi.require_version('Gtk', '4.0')
gi.require_version('GtkLayerShell', '0.1')

from gi.repository import Gtk, Gio
from notch_window import NotchWindow

class NotchApp(Gtk.Application):
    def __init__(self):
        super().__init__(
            application_id='com.example.notchbar',
            flags=Gio.ApplicationFlags.FLAGS_NONE
        )

    def do_activate(self):
        # Check if already running
        if self.get_windows():
            self.get_windows()[0].present()
            return

        win = NotchWindow(application=self)
        win.present()

def main():
    app = NotchApp()
    return app.run(sys.argv)

if __name__ == '__main__':
    sys.exit(main())
