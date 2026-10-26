#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the actual desktop binary on a private X server and private bus.
Coordinates intentionally use a controlled 1280x800 GTK fixture. No live desktop
or home directory is used. Failure artifacts survive under test-results/x11.
"""
import configparser
import os
from pathlib import Path
import subprocess
import sys
import time

import gi
gi.require_version("Gdk", "3.0")
# Import Gdk only after selecting the private DISPLAY.

root, binary = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
assert os.environ.get("DESKTOP_GROUPS_TEST_SANDBOX"), "Use test-x11.sh"
results = Path(os.environ.get("DG_TEST_RESULTS", str(root / "test-results" / "x11")))
results.mkdir(parents=True, exist_ok=True)
os.environ["DG_TEST_RESULTS"]=str(results)
log = (results / "session.log").open("w")
processes = []
checks = []


def run(*args):
    return subprocess.check_output(args, stderr=subprocess.STDOUT, text=True)


def xdo(*args):
    run("xdotool", *map(str, args))
    time.sleep(0.15)


def group_action_from_end(steps):
    # Root tail: Applications, Desktop Groups. Inside the submenu the common
    # tail is quick creation, arrangement, settings, then context-specific actions.
    xdo("key", "End", "Up", "Right", "End", *(["Up"] * (steps - 1)), "Return")


def wait_for(predicate, description, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.1)
    raise AssertionError(description)


def check(condition, description):
    assert condition, description
    checks.append(description)
    print("PASS:", description, flush=True)


def config():
    c = configparser.ConfigParser(interpolation=None)
    c.read(Path(os.environ["XDG_CONFIG_HOME"]) / "xfce4/desktop-groups.ini")
    return c


def groups():
    c = config()
    return [c[s] for s in c.sections() if s.startswith("Group ")]


def capture(name):
    from gi.repository import Gdk
    w = Gdk.get_default_root_window()
    Gdk.pixbuf_get_from_window(w, 0, 0, w.get_width(), w.get_height()).savev(str(results / name), "png", [], [])


def start_desktop():
    p = subprocess.Popen([str(binary), "--disable-wm-check"], stdout=log, stderr=log)
    processes.append(p)
    def ready():
        found = subprocess.run(["xdotool", "search", "--class", "xfdesktop"], stdout=subprocess.DEVNULL)
        return p.poll() is not None or found.returncode == 0
    wait_for(ready, "desktop startup")
    assert p.poll() is None
    time.sleep(1)
    return p


try:
    read_fd, write_fd = os.pipe()
    server = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "1280x800x24", "-nolisten", "tcp", "-noreset"], pass_fds=[write_fd], stdout=log, stderr=log)
    processes.append(server)
    os.close(write_fd)
    display = os.read(read_fd, 32).decode().strip()
    os.close(read_fd)
    assert display.isdigit()
    os.environ["DISPLAY"] = ":" + display
    run("dbus-update-activation-environment", "DISPLAY", "HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "NO_AT_BRIDGE")
    from gi.repository import Gdk
    for setting in ("show-home", "show-trash", "show-filesystem", "show-removable"):
        run("xfconf-query", "-c", "xfce4-desktop", "-p", "/desktop-icons/file-icons/" + setting, "-n", "-t", "bool", "-s", "false")
    alpha = Path.home() / "Desktop/alpha.txt"
    alpha.write_text("never moved or deleted\n")
    # Xvfb has no usable CUDA device; choose software VP8 for a deterministic fixture.
    os.environ.setdefault("GST_PLUGIN_FEATURE_RANK", "vp8dec:512,nvvp8dec:0")
    video = Path.home() / "test-wallpaper.webm"
    run("ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=640x400:rate=12", "-t", "3", "-c:v", "libvpx", "-deadline", "realtime", "-b:v", "500k", "-threads", "2", str(video))
    connector = next(line.split()[0] for line in run("xrandr", "--query").splitlines() if " connected" in line)
    backdrop = "/backdrop/screen0/monitor" + connector + "/workspace0/"
    for prop, kind, value in ((backdrop + "last-image", "string", str(video)), (backdrop + "image-style", "int", "3"), ("/video-wallpaper/muted", "bool", "true"), ("/video-wallpaper/paused", "bool", "false")):
        run("xfconf-query", "-c", "xfce4-desktop", "-p", prop, "-n", "-t", kind, "-s", value)
    desktop = start_desktop()
    def video_frame():
        return bytes(Gdk.pixbuf_get_from_window(Gdk.get_default_root_window(), 900, 500, 200, 200).get_pixels())
    def video_menu(action):
        xdo("mousemove", 1200, 700, "click", 3)
        downs = {"Play": 0, "Pause": 1, "Mute": 2}[action]
        xdo("key", "Home", "Right", "Home", *(["Down"] * downs), "Return")
        time.sleep(.3)
    first = video_frame()
    time.sleep(.4)
    wait_for(lambda: first != video_frame(), "video wallpaper initialization", timeout=15)
    check(True, "video wallpaper renders changing frames in the combined build")
    time.sleep(3.2)
    first = video_frame()
    time.sleep(.4)
    check(first != video_frame(), "video continues playing after end-of-stream looping")
    video_menu("Pause")
    first = video_frame()
    time.sleep(.4)
    check(first == video_frame(), "native video context menu pauses rendering")
    def display_control(name):
        keys=run("xfconf-query","-c","xfce4-desktop","-l").splitlines()
        return next(k for k in keys if k.startswith('/video-wallpaper/displays/') and k.endswith('/'+name))
    paused_key=display_control('paused')
    check(run("xfconf-query", "-c", "xfce4-desktop", "-p", paused_key).strip() == "true", "video pause state persists")
    video_menu("Play")
    first = video_frame()
    time.sleep(.4)
    check(first != video_frame(), "native video context menu resumes rendering")
    video_menu("Mute")
    check(run("xfconf-query", "-c", "xfce4-desktop", "-p", display_control("muted")).strip() == "false", "native video mute control remains connected")
    video_menu("Mute")
    video_menu("Pause")
    import hashlib
    scaling_key='/video-wallpaper/scaling/'+hashlib.sha256(backdrop.rstrip('/').encode()).hexdigest()
    xdo("mousemove",1200,700,"click",3,"key","Home","Right","Home","Down","Down","Down","Right","End","Return")
    wait_for(lambda:run("xfconf-query","-c","xfce4-desktop","-p",scaling_key).strip()=="fill","video fill selection")
    check(True,"native video Scaling submenu selects per-display Fill")
    check(run("xfconf-query","-c","xfce4-desktop","-p",paused_key).strip()=="true","changing video scaling preserves pause state")
    run("xfconf-query","-c","xfce4-desktop","-p",scaling_key,"-s","stretch")
    # Only alpha is visible: native grid first slot, select then native context menu.
    xdo("mousemove", 60, 40, "click", 3)
    capture("context-menu.png")
    xdo("key", "End", "Up", "Right")
    capture("groups-submenu.png")
    xdo("key", "Left")
    # Enter Desktop Groups, then choose the selection action.
    group_action_from_end(4)
    wait_for(lambda: len(groups()) == 1, "selected-icon group creation")
    xdo("type", "--clearmodifiers", "Documents")
    xdo("key", "Return")
    wait_for(lambda: groups()[0]["title"] == "Documents", "inline title commit")
    check(str(alpha) in groups()[0]["members"], "create group from native selected file icon")
    check(alpha.read_text() == "never moved or deleted\n", "grouping preserves file path and contents")
    if os.environ.get('XFCE_TWEAKS_TEST_FOCUS')=='visibility':
        run('xfconf-query','-c','xfce4-desktop','-p','/xfce-tweaks/enabled/desktop-groups','-n','-t','bool','-s','true')
        page_files=[alpha]
        for i in range(5):
            f=Path.home()/('Desktop/page-%d.txt'%i);f.write_text('visibility fixture\n');page_files.append(f)
        wm=subprocess.Popen(['xfwm4','--compositor=off','--sm-client-disable'],stdout=log,stderr=log);processes.append(wm);time.sleep(1)
    else:
        # First group starts at x=0,y=0. Move its title to a known free location.
        time.sleep(0.4)
        xdo("mousemove", 80, 12, "mousedown", 1, "sleep", ".2", "mousemove", 400, 212, "sleep", ".2", "mouseup", 1)
        wait_for(lambda: float(groups()[0]["x"]) > 300, "title drag")
        saved = (groups()[0]["x"], groups()[0]["y"], groups()[0]["monitor"])
        check(saved[:2] == ("320", "200"), "freeform group movement preserves exact logical coordinates")
        capture("group.png")
        # Inspect actual rendered glyph bounds, excluding the icon row.
        header = Gdk.pixbuf_get_from_window(Gdk.get_default_root_window(), 320, 200, int(float(groups()[0]["width"])), 40)
        data, stride, channels = header.get_pixels(), header.get_rowstride(), header.get_n_channels()
        text_pixels = [(x,y) for y in range(header.get_height()) for x in range(header.get_width())
            if min(data[y*stride+x*channels:y*stride+x*channels+3]) > 220]
        check(bool(text_pixels) and abs((min(x for x,y in text_pixels)+max(x for x,y in text_pixels))/2 - header.get_width()/2) < 4,
            "visible group titles render centered")
        video_menu("Play")
        first = video_frame()
        time.sleep(.4)
        check(first != video_frame(), "video keeps rendering while native desktop groups exist")
        capture("video-with-groups.png")
        video_menu("Pause")

        desktop.terminate()
        desktop.wait(timeout=5)
        desktop = start_desktop()
        check((groups()[0]["x"], groups()[0]["y"], groups()[0]["monitor"]) == saved, "restart retains group position and stable monitor identity")
        capture("restored.png")
        video_menu("Play")
        first = video_frame()
        time.sleep(.4)
        check(first != video_frame(), "video and groups both restore after desktop restart")
        video_menu("Pause")

        # Native XFCE icon sizing changes both the icon renderer and local group cells.
        for size in (32,64,48):
            run("xfconf-query", "-c", "xfce4-desktop", "-p", "/desktop-icons/icon-size", "-n", "-t", "int", "-s", str(size))
            time.sleep(.5)
            xdo("mousemove", 400, 212, "click", 1)  # commit current automatic dimensions
            if size == 32:
                small_height = float(groups()[0]["height"])
            elif size == 64:
                check(float(groups()[0]["height"]) > small_height + 40,
                    "group cells and automatic sizing follow XFCE desktop icon-size changes")
        check((groups()[0]["x"],groups()[0]["y"]) == saved[:2], "icon size changes retain the group upper-left position")
        # Open customization through the native desktop menu and edit live controls.
        xdo("mousemove", 400, 212, "click", 3)
        group_action_from_end(9)
        dialog_id = run("xdotool", "search", "--name", "^Customize Desktop Group$").splitlines()[0]
        xdo("windowfocus", dialog_id)
        xdo("key", "ctrl+a")
        xdo("type", "--clearmodifiers", "Documents")
        # Browse opens GTK's real file chooser and returns the chosen path to the live preview.
        from gi.repository import GdkPixbuf
        background = Path.home() / "group-background.png"
        pix = GdkPixbuf.Pixbuf.new(GdkPixbuf.Colorspace.RGB, True, 8, 16, 16)
        pix.fill(0x446688ff)
        pix.savev(str(background), "png", [], [])
        xdo("key", "Tab", "Tab", "space")
        wait_for(lambda: subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "^Choose Group Background$"], stdout=subprocess.DEVNULL).returncode == 0, "background chooser maps", timeout=15)
        chooser = run("xdotool", "search", "--onlyvisible", "--name", "^Choose Group Background$").splitlines()[0]
        xdo("windowfocus", chooser)
        xdo("key", "ctrl+l")
        xdo("type", "--clearmodifiers", str(background))
        xdo("key", "Return")
        time.sleep(.4)
        if subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "^Choose Group Background$"], stdout=subprocess.DEVNULL).returncode == 0:
            xdo("key", "alt+o")
        wait_for(lambda: subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "^Choose Group Background$"], stdout=subprocess.DEVNULL).returncode != 0, "background image chooser acceptance")
        xdo("windowfocus", dialog_id)
        xdo("key", "Tab", "Tab", "ctrl+a")
        xdo("type", "--clearmodifiers", "0.25")
        xdo("key", "Tab")
        capture("customization.png")
        xdo("key", "Escape")
        wait_for(lambda: abs(float(groups()[0]["opacity"]) - .25) < .001, "customization opacity save")
        check(True, "GTK customization edits and persists background opacity")
        check(groups()[0]["image"] == str(background), "native Browse chooser selects and persists the group background image")
        time.sleep(.4)


        # Smooth hover affects only the background; sample away from native icons.
        def pixel(x, y):
            return bytes(Gdk.pixbuf_get_from_window(Gdk.get_default_root_window(), x, y, 1, 1).get_pixels())
        xdo("mousemove", 1250, 700)
        time.sleep(.25)
        exited = pixel(340, 300)
        xdo("mousemove", 340, 300)
        time.sleep(.25)
        entered = pixel(340, 300)
        check(exited != entered, "hover opacity changes the group background")
        xdo("mousemove", 400, 212, "click", 3)
        group_action_from_end(8)
        wait_for(lambda: groups()[0]["locked"] == "true", "lock group")
        xdo("mousemove", 400, 212, "mousedown", 1, "sleep", ".2", "mousemove", 450, 250, "sleep", ".2", "mouseup", 1)
        check((groups()[0]["x"], groups()[0]["y"]) == saved[:2], "position lock prevents group movement")
        xdo("mousemove", 400, 212, "click", 3)
        group_action_from_end(8)
        wait_for(lambda: groups()[0]["locked"] == "false", "unlock group")
        # Native resize grip switches the group to fixed sizing.
        old_width, old_height = float(groups()[0]["width"]), float(groups()[0]["height"])
        grip_x, grip_y = int(320+old_width-6), int(200+old_height-6)
        xdo("mousemove", grip_x, grip_y, "mousedown", 1, "sleep", ".2", "mousemove", grip_x+120, grip_y+100, "sleep", ".2", "mouseup", 1)
        wait_for(lambda: groups()[0]["auto_size"] == "false", "resize switches sizing mode")
        check(float(groups()[0]["width"]) > old_width+100 and float(groups()[0]["height"]) > old_height+80, "resize grip persists fixed dimensions")
        # Empty background well below the title now drags the entire group.
        xdo("mousemove", 340, 350, "mousedown", 1, "sleep", ".2", "mousemove", 360, 370, "sleep", ".2", "mouseup", 1)
        wait_for(lambda: groups()[0]["x"] == "340", "group background drag")
        check(groups()[0]["y"] == "220", "empty group background moves the group without grabbing its title")
        xdo("mousemove", 360, 370, "mousedown", 1, "sleep", ".2", "mousemove", 340, 350, "sleep", ".2", "mouseup", 1)
        wait_for(lambda: groups()[0]["x"] == "320", "restore group position")
        # Empty Ctrl rubber-band -> group creation menu, first item.
        xdo("mousemove", 700, 100, "keydown", "ctrl", "mousedown", 1, "mousemove", 1100, 430, "sleep", ".2", "mouseup", 1, "keyup", "ctrl")
        xdo("key", "Home", "Return")
        wait_for(lambda: len(groups()) == 2, "empty rectangle group")
        xdo("key", "Return")
        check(any(g["title"] == "" and g["auto_size"] == "false" for g in groups()), "empty group accepts blank title and fixed rectangle dimensions")
        time.sleep(0.4)
        # Drag alpha from native slot inside first group to empty second group surface.
        # First group occupies x320..546: its only column is centered at x480.
        xdo("mousemove", 380, 272, "mousedown", 1, "sleep", ".2", "mousemove", 405, 290, "sleep", ".2", "mousemove", 850, 180, "sleep", ".4", "mouseup", 1)
        wait_for(lambda: str(alpha) not in next(g for g in groups() if g["title"] == "Documents")["members"], "icon drag between groups")
        check(str(alpha) in next(g for g in groups() if g["title"] == "")["members"], "native icon drag moves logical membership between groups")
        alpha.rename(alpha.with_name("renamed.txt"))
        alpha = alpha.with_name("renamed.txt")
        wait_for(lambda: str(alpha) in next(g for g in groups() if g["title"] == "")["members"], "rename follows file monitor")
        check(True, "native file rename retains group membership")
        xdo("mousemove", 760, 172, "mousedown", 1, "sleep", ".2", "mousemove", 785, 190, "sleep", ".2", "mousemove", 600, 650, "sleep", ".4", "mouseup", 1)
        wait_for(lambda: all(str(alpha) not in g["members"] for g in groups()), "drag icon out of group")
        check(alpha.exists(), "dragging an icon out releases logical membership without moving its file")
        # Native destination at (600,650) snaps to column 5, row 5 (icon at 585,600).
        xdo("mousemove", 585, 600, "mousedown", 1, "sleep", ".2", "mousemove", 610, 620, "sleep", ".2", "mousemove", 850, 180, "sleep", ".4", "mouseup", 1)
        wait_for(lambda: str(alpha) in next(g for g in groups() if g["title"] == "")["members"], "drag icon back into group")
        check(True, "dragging an ungrouped native icon into a group assigns membership")
        # Delete the destination group through its native context menu.
        xdo("mousemove", 800, 110, "click", 3)
        # Tail: Applications, quick, auto, defaults, delete.
        group_action_from_end(4)
        wait_for(lambda: len(groups()) == 1, "delete group")
        check(alpha.read_text() == "never moved or deleted\n", "deleting group releases icons and preserves underlying file")
        capture("ungrouped.png")
        xdo("mousemove", 400, 212, "click", 3)
        group_action_from_end(2)
        wait_for(lambda: config()["Desktop Groups"]["auto_arrange"] == "true", "automatic arrangement")
        check(True, "global automatic group arrangement can be enabled")
        xdo("mousemove", 900, 600, "click", 3)
        group_action_from_end(2)
        wait_for(lambda: config()["Desktop Groups"]["auto_arrange"] == "false", "freeform arrangement")
        check(True, "automatic arrangement can switch back to freeform")

        check(desktop.poll() is None, "desktop remains running after interaction sequence")
        # Restore a known overflowing fixture, then exercise real native icon paging.
        desktop.terminate()
        desktop.wait(timeout=5)
        fixture = config()
        section = next(s for s in fixture.sections() if s.startswith("Group "))
        fixture[section].update({"x":"320", "y":"200", "width":"240", "height":"220", "auto_size":"false"})
        page_files = [alpha]
        for i in range(5):
            file = Path.home() / ("Desktop/page-%d.txt" % i)
            file.write_text("paging fixture\n")
            page_files.append(file)
        fixture[section]["members"] = ";".join(map(str, page_files)) + ";"
        with (Path(os.environ["XDG_CONFIG_HOME"]) / "xfce4/desktop-groups.ini").open("w") as f:
            fixture.write(f, space_around_delimiters=False)
        desktop = start_desktop()
        xdo("mousemove", 340, 300)
        time.sleep(.3)
        page1 = bytes(Gdk.pixbuf_get_from_window(Gdk.get_default_root_window(), 320, 200, 240, 220).get_pixels())
        capture("overflow-page1.png")
        xdo("click", 5)
        time.sleep(.3)
        page2 = bytes(Gdk.pixbuf_get_from_window(Gdk.get_default_root_window(), 320, 200, 240, 220).get_pixels())
        capture("overflow-page2.png")
        check(page1 != page2 and all(f.exists() for f in page_files), "fixed group overflow pages native icons without moving files")
        xdo("mousemove", 1200, 700, "click", 1, "key", "ctrl+a")
        xdo("mousemove", 380, 272, "click", 3)
        group_action_from_end(4)
        wait_for(lambda: len(groups()) == 2, "multiple-selection group")
        xdo("key", "Return")
        time.sleep(.4)
        # xfdesktop 4.21 selects only placed icons; off-page members are intentionally excluded.
        selected_page = sorted(page_files, key=str)[2:4]
        check(any(all(str(f) in g["members"] for f in selected_page) and len([m for m in g["members"].split(";") if m]) == 2 for g in groups()), "native multiple-icon selection groups every visible selected icon")
        # EWMH verification under the actual XFCE window manager in this private X server.
        wm = subprocess.Popen(["xfwm4", "--compositor=off", "--sm-client-disable"], stdout=log, stderr=log)
        processes.append(wm)
        time.sleep(1)
        normal = subprocess.Popen(["python3", "-c", "import gi; gi.require_version('Gtk','3.0'); from gi.repository import Gtk; w=Gtk.Window(title='Desktop Groups stacking test'); w.set_default_size(400,300); w.show_all(); Gtk.main()"], stdout=log, stderr=log)
        processes.append(normal)
        time.sleep(1)
        normal_id = int(run("xdotool", "search", "--name", "^Desktop Groups stacking test$").splitlines()[0])
        stack = run("xprop", "-root", "_NET_CLIENT_LIST_STACKING")
        ids = [int(x.strip(), 16) for x in stack.split("#",1)[1].split(",")]
        desktop_ids = [i for i in ids if "_NET_WM_WINDOW_TYPE_DESKTOP" in run("xprop", "-id", str(i), "_NET_WM_WINDOW_TYPE")]
        check(bool(desktop_ids) and all(ids.index(i) < ids.index(normal_id) for i in desktop_ids), "normal application stacks above native desktop groups under xfwm4")
        check(len(ids) == len(desktop_ids) + 1, "groups create no separate managed taskbar or Alt+Tab windows")
        capture("stacking.png")

        # Run actual GTK switches and the import chooser against this same private host.
        normal.terminate();normal.wait(timeout=5)
        saved_config=(Path(os.environ["XDG_CONFIG_HOME"])/"xfce4/desktop-groups.ini").read_bytes()
        saved_pid=desktop.pid
        subprocess.run(["python3",str(root/"tests/gui-driver.py")],check=True,stdout=log,stderr=log,timeout=30)
        import json
        gui=json.loads((results/"gui-results.json").read_text())
        for message in gui["checks"]: check(True,message)
        check(desktop.poll() is None and desktop.pid==saved_pid,"module toggles and import retain the same desktop process")
        check((Path(os.environ["XDG_CONFIG_HOME"])/"xfce4/desktop-groups.ini").read_bytes()==saved_config,
              "module disable/re-enable preserves group configuration exactly")
        check(all(f.exists() for f in page_files),"module toggles preserve desktop files")
        def visible_window(pattern):
            found=subprocess.run(["xdotool","search","--onlyvisible","--name",pattern],capture_output=True)
            return found.returncode==0
        # Disabling while nested GTK dialogs are running must safely unwind them.
        xdo("mousemove",1200,700,"click",3)
        group_action_from_end(3)
        wait_for(lambda:visible_window("New Group Defaults"),"group defaults dialog")
        run("xfconf-query","-c","xfce4-desktop","-p","/xfce-tweaks/enabled/desktop-groups","-s","false")
        wait_for(lambda:not visible_window("New Group Defaults"),"group dialog closes on disable")
        check(desktop.poll() is None,"disabling Desktop Groups safely closes an open customization dialog")
        run("xfconf-query","-c","xfce4-desktop","-p","/xfce-tweaks/enabled/desktop-groups","-s","true")
        time.sleep(.4)
        xdo("mousemove",1200,700,"click",3,"key","Home","Right","End","Return")
        wait_for(lambda:visible_window("Choose Video Wallpaper"),"video chooser")
        run("xfconf-query","-c","xfce4-desktop","-p","/xfce-tweaks/enabled/video-wallpaper","-s","false")
        wait_for(lambda:not visible_window("Choose Video Wallpaper"),"video chooser closes on disable")
        check(desktop.poll() is None,"disabling Video Wallpaper safely closes its open file chooser")
        run("xfconf-query","-c","xfce4-desktop","-p","/xfce-tweaks/enabled/video-wallpaper","-s","true")
        for unused in range(4):
            for enabled in ("false","true"):
                for identifier in ("desktop-groups","video-wallpaper"):
                    run("xfconf-query","-c","xfce4-desktop","-p","/xfce-tweaks/enabled/"+identifier,"-s",enabled)
                time.sleep(.1)
        check(desktop.poll() is None,"repeated enable/disable cycles leave the desktop running")
        # A second host start must discover the imported user module and saved switch states.
        desktop.terminate();desktop.wait(timeout=5);desktop=start_desktop()
        check(run("xfconf-query","-c","xfce4-desktop","-p","/xfce-tweaks/status/desktop-groups").strip()=="active",
              "restart restores enabled modules")
        check(run("xfconf-query","-c","xfce4-desktop","-p","/xfce-tweaks/enabled/example-tweak").strip()=="false",
              "restart preserves the imported module's disabled setting")

    # Group opacity must affect actual native icons, not only their backdrop.
    run("xfconf-query","-c","xfce4-desktop","-p",paused_key,"-s","true")
    time.sleep(.8)
    path=Path(os.environ["XDG_CONFIG_HOME"])/"xfce4/desktop-groups.ini"
    controlled=config();original_group=next(dict(controlled[s]) for s in controlled.sections() if s.startswith("Group "))
    monitor=original_group['monitor']
    for section in list(controlled.sections()):
        if section.startswith("Group "):controlled.remove_section(section)
    controlled['Group 00000000-0000-4000-8000-000000000001']=dict(original_group)
    g=controlled['Group 00000000-0000-4000-8000-000000000001']
    g.update({'monitor':monitor,'x':'300','y':'250','width':'400','height':'240',
              'auto_size':'false','auto_icons':'true','title_visible':'false','locked':'true',
              'border':'0','radius':'0','color':'rgba(0,0,0,0)','image':'',
              'members':str(page_files[0])+';'})
    def group_opacity(value,enter=None,exit=None):
        g['opacity']=str(value);g['hover_enter']=str(value if enter is None else enter);g['hover_exit']=str(value if exit is None else exit)
        with path.open('w') as output:controlled.write(output,space_around_delimiters=False)
        for enabled in ['false','true']:run('xfconf-query','-c','xfce4-desktop','-p','/xfce-tweaks/enabled/desktop-groups','-s',enabled)
        time.sleep(.6)
    def pixels(x,y,w,h):return bytes(Gdk.pixbuf_get_from_window(Gdk.get_default_root_window(),x,y,w,h).get_pixels())
    xdo('mousemove',1200,700,'click',1)
    group_opacity(1);opaque=pixels(300,250,400,240);ungrouped=pixels(0,0,150,700)
    group_opacity(0);transparent=pixels(300,250,400,240)
    check(opaque!=transparent,'group opacity zero hides assigned native icons and labels')
    group_opacity(.4);partial=pixels(300,250,400,240)
    errors=[abs(c-(b+.4*(a-b))) for a,b,c in zip(opaque,transparent,partial)]
    print('Opacity pixel error:',sum(errors)/len(errors),'partial equals transparent:',partial==transparent,'partial equals opaque:',partial==opaque,flush=True)
    capture('opacity-partial.png')
    check(sum(errors)/len(errors)<1.5 and partial!=transparent and partial!=opaque,
          'assigned icon pixels follow group opacity while its background is transparent')
    check(pixels(0,0,150,700)==ungrouped,'group opacity leaves unassigned desktop icons unchanged')
    group_opacity(.2,.8,.2);xdo('mousemove',600,460);time.sleep(.35);hovered=pixels(300,250,400,240)
    xdo('mousemove',1200,700);time.sleep(.35);exited=pixels(300,250,400,240)
    check(hovered!=exited,'group hover opacity also fades its assigned native icons and labels')
    group_opacity(.4)

    # The visibility Tweak is enabled after legacy menu-order tests finish.
    for identifier in ['victus-control','desktop-peek']:
        run('xfconf-query','-c','xfce4-desktop','-p','/xfce-tweaks/enabled/'+identifier,'-n','-t','bool','-s','true')
    wait_for(lambda:run('xfconf-query','-c','xfce4-desktop','-p','/xfce-tweaks/status/desktop-peek').strip()=='active','visibility module')
    time.sleep(.5)
    def hidden():return run('xfconf-query','-c','xfce4-desktop','-p','/xfce-tweaks/desktop-peek/hidden').strip()=='true'
    def double(x,y):
        time.sleep(.5);xdo('mousemove',x,y,'click','--repeat',2,'--delay',100,1);time.sleep(.25)
    double(600,460);check(not hidden(),'double-click inside a locked group preserves group interaction')
    double(1100,65);check(not hidden(),'double-click on a widget does not hide desktop items')
    wait_for(lambda:subprocess.run(['xdotool','search','--name','^HP Victus/Omen Control$'],stdout=subprocess.DEVNULL).returncode==0,'widget controls open')
    panel=run('xdotool','search','--name','^HP Victus/Omen Control$').splitlines()[0]
    xdo('windowactivate','--sync',panel,'key','alt+F4')
    xdo('keydown','ctrl');double(1200,700);xdo('keyup','ctrl');check(not hidden(),'modified desktop double-click retains native behavior')
    icon_before=pixels(0,0,150,700);group_before=pixels(300,250,400,240);widget_before=pixels(952,24,304,250);wallpaper_before=pixels(1150,740,80,40)
    double(1200,700);wait_for(hidden,'desktop hide')
    check(pixels(0,0,150,700)!=icon_before and pixels(300,250,400,240)!=group_before and pixels(952,24,304,250)!=widget_before,
          'empty desktop double-click hides icons, groups and the native widget together')
    check(pixels(1150,740,80,40)==wallpaper_before and run('xfconf-query','-c','xfce4-desktop','-p',paused_key).strip()=='true',
          'hiding desktop items preserves wallpaper and video pause state')
    double(1100,65);wait_for(lambda:not hidden(),'desktop restore')
    check(pixels(0,0,150,700)==icon_before,'double-clicking hidden widget area restores desktop icons')
    double(1200,700);wait_for(hidden,'second desktop hide')
    run('xfconf-query','-c','xfce4-desktop','-p','/xfce-tweaks/enabled/desktop-peek','-s','false')
    wait_for(lambda:not hidden(),'disable restores desktop')
    wait_for(lambda:pixels(0,0,150,700)==icon_before,'restored desktop repaints after disable')
    check(True,'disabling visibility while hidden restores desktop items')
    check(all(f.exists() for f in page_files),'visibility and opacity changes preserve desktop files')

    (results / "results.txt").write_text("\n".join("PASS: " + c for c in checks) + "\n")
except BaseException:
    try:
        capture("failure.png")
    except Exception:
        pass
    raise
finally:
    for p in reversed(processes):
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
    log.close()
