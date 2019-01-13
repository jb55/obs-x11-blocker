
#include<X11/X.h>
#include<X11/Xlib.h>
#include<X11/Xutil.h>
#include<stdio.h>
#include<ctype.h>

int main ()
{
	Display* d = XOpenDisplay(NULL);
	/* Window root = DefaultRootWindow(d); */
	XClassHint chint;
	Window curFocus;
	int revert;

	XGetInputFocus (d, &curFocus, &revert);
	XSelectInput(d, curFocus, SubstructureNotifyMask);

	while (1)
	{
		XEvent ev;
		XNextEvent(d, &ev);
		switch (ev.type)
		{
		case UnmapNotify:
			XGetClassHint(d, ev.xunmap.window, &chint);
			/* ev.xunmap.window */
			printf ("%s unmapped\n", chint.res_name);
			break;
		}

	}
}
