/* Minimal X11 input injector for the VEdit keyboard-focus probe.
 *
 * Finds the largest VIEWABLE toplevel window whose WM_NAME contains argv[1]
 * (default "VEdit"), gives it the input focus, types one printable key, then
 * sends Ctrl+S so the app persists whatever actually reached its buffer. The
 * driver script compares the saved file against the original: an inserted
 * character means the key reached the editor, an unchanged file means the view
 * never got keyboard focus.
 *
 * GTK creates unmapped helper windows that can carry the same title, so the
 * selection requires map_state == IsViewable and picks the largest area; a
 * short retry loop tolerates an application that is still mapping its window.
 *
 * Built inside the pinned image with:
 *   gcc xtype.c -o xtype -lX11 -l:libXtst.so.6
 * (the image ships libXtst.so.6 without the -dev symlink, hence the explicit
 *  soname; the two XTest prototypes below are declared locally for the same
 *  reason.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

extern Bool XTestFakeKeyEvent(Display *display, unsigned int keycode,
                              Bool is_press, unsigned long delay);

static void consider(Display *display, Window window, const char *needle,
                     Window *best, unsigned long *best_area) {
    char *name = NULL;
    if (!XFetchName(display, window, &name) || name == NULL) {
        return;
    }
    int matches = strstr(name, needle) != NULL;
    XFree(name);
    if (!matches) {
        return;
    }
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(display, window, &attributes)) {
        return;
    }
    if (attributes.map_state != IsViewable) {
        return;
    }
    unsigned long area =
        (unsigned long)attributes.width * (unsigned long)attributes.height;
    if (area > *best_area) {
        *best_area = area;
        *best = window;
    }
}

static void walk(Display *display, Window window, const char *needle,
                 Window *best, unsigned long *best_area, int depth) {
    if (depth > 8) {
        return;
    }
    consider(display, window, needle, best, best_area);
    Window root = 0, parent = 0, *children = NULL;
    unsigned int count = 0;
    if (!XQueryTree(display, window, &root, &parent, &children, &count)) {
        return;
    }
    for (unsigned int i = 0; i < count; i++) {
        walk(display, children[i], needle, best, best_area, depth + 1);
    }
    if (children != NULL) {
        XFree(children);
    }
}

static Window pick(Display *display, const char *needle) {
    Window best = 0;
    unsigned long best_area = 0;
    walk(display, DefaultRootWindow(display), needle, &best, &best_area, 0);
    return best;
}

static void tap(Display *display, KeySym symbol, int with_ctrl) {
    KeyCode code = XKeysymToKeycode(display, symbol);
    KeyCode ctrl = XKeysymToKeycode(display, XK_Control_L);
    if (with_ctrl) {
        XTestFakeKeyEvent(display, ctrl, True, 20);
    }
    XTestFakeKeyEvent(display, code, True, 20);
    XTestFakeKeyEvent(display, code, False, 20);
    if (with_ctrl) {
        XTestFakeKeyEvent(display, ctrl, False, 20);
    }
    XFlush(display);
}

int main(int argc, char **argv) {
    const char *title = argc > 1 ? argv[1] : "VEditWithFocus";
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "xtype: no display\n");
        return 2;
    }
    Window target = 0;
    for (int attempt = 0; attempt < 25 && target == 0; attempt++) {
        target = pick(display, title);
        if (target == 0) {
            usleep(200000);
        }
    }
    if (target == 0) {
        fprintf(stderr, "xtype: no viewable window matching '%s'\n", title);
        XCloseDisplay(display);
        return 3;
    }
    printf("xtype: window=0x%lx\n", (unsigned long)target);

    XSetInputFocus(display, target, RevertToParent, CurrentTime);
    XFlush(display);
    usleep(400000);

    tap(display, XK_a, 0);
    usleep(400000);
    tap(display, XK_s, 1);
    usleep(900000);

    XCloseDisplay(display);
    return 0;
}
