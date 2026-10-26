# Desktop Groups appearance

Each group title now has **Title shadow color** and **Title shadow strength** controls in Customize Group. The color includes alpha; strength ranges from 0 (off) to 1. New groups inherit these values from the Desktop Groups defaults editor. Existing groups load with a subtle black shadow until customized.

The Clock, Weather, and HP Victus/Omen widgets expose the same text shadow color and strength controls in their Appearance settings. Their defaults are subtle black shadows at 0.30 strength. Set strength to zero to turn a shadow off. Changes redraw immediately and persist with the existing Xfconf appearance settings.

Only text receives the shadow. Card backgrounds, group backgrounds, desktop icons, and video wallpaper are unchanged. Text continues to use the active GTK theme or the widget's configured text color.

The group overflow hint also now reflects native icons actually placed inside the group: it is hidden when every assigned icon is visible, uses a page count only when more than one page is needed, and retains a resize hint when some assigned icons do not fit but the remaining items fit on a single page.
# Restore defaults

Each group's **Customize Group…** dialog has a **Restore default appearance** button. It restores the default background, opacity, hover opacity, border, corners, title placement and shadow, and automatic icon sizing. It preserves the group's name, membership, monitor, position, and position lock.
