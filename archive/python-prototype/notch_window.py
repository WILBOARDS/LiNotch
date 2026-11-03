import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkLayerShell', '0.1')
gi.require_version('Adw', '1')

from gi.repository import Gtk, GtkLayerShell, GLib, Gdk
import datetime

class NotchWindow(Gtk.ApplicationWindow):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.set_title("NotchBar")

        # State management
        self.expanded = False
        self.collapsed_width = 240
        self.collapsed_height = 42
        self.expanded_width = 420
        self.expanded_height = 320
        self.auto_collapse_timer = None

        self.setup_window()
        self.setup_ui()
        self.setup_css()
        self.start_clock()

        # Click to toggle
        click_gesture = Gtk.GestureClick()
        click_gesture.connect("pressed", self.on_click)
        self.add_controller(click_gesture)

    def setup_window(self):
        """Configure window for notch behavior"""
        # Layer shell for Wayland (Pop!_OS default on Intel/AMD)
        try:
            GtkLayerShell.init_for_window(self)
            GtkLayerShell.set_layer(self, GtkLayerShell.Layer.TOP)
            GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.TOP, True)
            GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.LEFT, False)
            GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.RIGHT, False)
            GtkLayerShell.set_margin(self, GtkLayerShell.Edge.TOP, 8)
            GtkLayerShell.set_exclusive_zone(self, 0)  # Don't push windows down
            GtkLayerShell.set_keyboard_mode(self, GtkLayerShell.KeyboardMode.NONE)
        except:
            # X11 fallback (NVIDIA on Pop!_OS)
            self.set_decorated(False)
            self.set_keep_above(True)
            self.set_type_hint(Gdk.WindowTypeHint.DOCK)

        self.set_size_request(self.collapsed_width, self.collapsed_height)
        self.set_resizable(False)
        self.set_visible(True)

    def setup_ui(self):
        """Build the UI with collapsed and expanded views"""
        self.main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.set_child(self.main_box)

        # === COLLAPSED VIEW (Always visible) ===
        self.collapsed_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        self.collapsed_box.set_margin_top(10)
        self.collapsed_box.set_margin_bottom(10)
        self.collapsed_box.set_margin_start(20)
        self.collapsed_box.set_margin_end(20)
        self.collapsed_box.set_halign(Gtk.Align.CENTER)

        # Time
        self.time_label = Gtk.Label()
        self.time_label.add_css_class("time")

        # Battery icon + percentage
        self.battery_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        self.battery_icon = Gtk.Image.new_from_icon_name("battery-full-symbolic")
        self.battery_label = Gtk.Label(label="100%")
        self.battery_box.append(self.battery_icon)
        self.battery_box.append(self.battery_label)

        # Status dot (notification indicator)
        self.status_dot = Gtk.Box()
        self.status_dot.set_size_request(8, 8)
        self.status_dot.add_css_class("status-dot")

        self.collapsed_box.append(self.time_label)
        self.collapsed_box.append(Gtk.Separator(orientation=Gtk.Orientation.VERTICAL))
        self.collapsed_box.append(self.battery_box)
        self.collapsed_box.append(self.status_dot)

        self.main_box.append(self.collapsed_box)

        # === EXPANDED VIEW (Hidden by default) ===
        self.expanded_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.expanded_box.set_margin_top(10)
        self.expanded_box.set_margin_bottom(20)
        self.expanded_box.set_margin_start(20)
        self.expanded_box.set_margin_end(20)
        self.expanded_box.set_visible(False)

        # Header
        header = Gtk.Label(label="Quick Controls")
        header.add_css_class("header")
        self.expanded_box.append(header)

        # Placeholder content
        self.notification_label = Gtk.Label(label="No new notifications")
        self.notification_label.add_css_class("dim")
        self.expanded_box.append(self.notification_label)

        # Media controls placeholder
        media_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        media
