
#include<X11/X.h>
#include<X11/Xlib.h>
#include<X11/Xutil.h>
#include<stdio.h>
#include<ctype.h>

int main ()
{
    Display* d = XOpenDisplay(NULL);
    Window root = DefaultRootWindow(d);
    Window curFocus;
    char buf[17];
    KeySym ks;
    XComposeStatus comp;
    int len;
    int revert;

    XGetInputFocus (d, &curFocus, &revert);
    XSelectInput(d, curFocus, );

    while (1)
    {
        XEvent ev;
        XNextEvent(d, &ev);
        switch (ev.type)
        {
            case UnmapNotify:
                ev.xunmap.window
                printf ("%s unmapped\n", wname);
                break;

            case KeyPress:
                printf ("Got key!\n");
                len = XLookupString(&ev.xkey, buf, 16, &ks, &comp);
                if (len > 0 && isprint(buf[0]))
                {
                    buf[len]=0;
                    printf("String is: %s\n", buf);
                }
                else
                {
                    printf ("Key is: %d\n", (int)ks);
                }
        }

    }
}
