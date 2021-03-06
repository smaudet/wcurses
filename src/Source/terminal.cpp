#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <Mmsystem.h>
#include <commctrl.h>

#include "Python.h"

#include "terminal.h"
#include "curses_win32.h"
#include "defines.h"
#include "globals.h"
#include "lines.h"

#define _TRACING
#include "trace.h"

/* void Cls_OnSizing(HWND hwnd, UINT edge, RECT *size) */
//#define HANDLE_WM_SIZING(hwnd, wParam, lParam, fn) \
//    (BOOL)((fn)((hwnd), (UINT)(wParam), (RECT *)(lParam)))

PyTypeObject curses_terminalType = 
{
	PyObject_HEAD_INIT(NULL)
	0,				/*ob_size*/
	const_cast<char*>(TerminalProp),	/*tp_name*/
	sizeof(Terminal),	/*tp_basicsize*/
	0,				/*tp_itemsize*/
	
	/* methods */
	(destructor)Terminal::dealloc, 	/*tp_dealloc*/
	0,						/*tp_print*/
	0, /*tp_getattr*/
	0, /*tp_setattr*/
	0,			/*tp_compare*/
	0,			/*tp_repr*/
	0,			/*tp_as_number*/
	0,			/*tp_as_sequence*/
	0,			/*tp_as_mapping*/
	0,			/*tp_hash*/
	0,			/* tp_call */
	0,			/* tp_str */
	0,			/* tp_getattro */
	0,			/* tp_setattro */
	0,			/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,		/* tp_flags */
	"Terminal Object",		/* tp_doc */
	0,		/* tp_traverse */
	0,		/* tp_clear */
	0,		/* tp_richcompare */
	0,		/* tp_weaklistoffset */
	0,		/* tp_iter */
	0,		/* tp_iternext */
	0, /* tp_methods */
	0, /* tp_members */
    0,                      /*tp_getset*/
    0,                      /*tp_base*/
    0,                      /*tp_dict*/
    0,                      /*tp_descr_get*/
    0,                      /*tp_descr_set*/
    0,                      /*tp_dictoffset*/
    0,                      /*tp_init*/
    0,                      /*tp_alloc*/
    0,                      /*tp_new*/
    0,                      /*tp_free*/
    0,                      /*tp_is_gc*/
};

const char *Terminal::ClassName = "WCurses Terminal";

void Terminal::CursorPos(int x, int y)
{
	TRACE("Terminal_CursorPos");

	this->x = x;
	this->y = y;
}

void Terminal::UpdateCursorPos()
{
	TRACE("Terminal_UpdateCursorPos");

	if (this->has_caret)
	{
		SetCaretPos(
			this->x * this->cell_width,
			(this->y * this->cell_height) + this->caret_yoffs
			);
	}
}

void Terminal::dealloc(Terminal *self)
{
	TRACE("Terminal_dealloc");

	FreeLines(self->buffer, self->height);
	DestroyWindow(self->win);

	Py_DECREF(self->keybuffer);
	PyObject_Del(self);
}

Terminal *Terminal::New()
{
	TRACE("Terminal_New");

	Terminal *newterm = PyObject_NEW(Terminal, &curses_terminalType);
	if (newterm == NULL)
		return NULL;

	newterm->height = DEFAULT_HEIGHT;
	newterm->width = DEFAULT_WIDTH;

	newterm->keybuffer = PyList_New(0);
	newterm->ungetch = -1;
	newterm->waiting_for_key = 0;

	newterm->buffer = AllocLines(newterm->height, newterm->width, SPACE);

	newterm->has_caret = 0;

	// Cursor to top left
	newterm->x = 0;
	newterm->y = 0;

	// IO options
	// What should these default to? The defaults should probably be set via corresponding functions.
	newterm->_cbreak = 0;
	newterm->_echo = 1;
	newterm->_raw = 0;
	newterm->_qiflush = 0;
	newterm->allow8bitInput = 1;
	newterm->_keypad = 0;
	newterm->_halfdelay = 0;

	// Create window
	newterm->win = CreateWindow (
		Terminal::ClassName,         // window class name
		Terminal::ClassName,     // window caption
	    WS_OVERLAPPEDWINDOW,     // window style
	    CW_USEDEFAULT,           // initial x position
	    CW_USEDEFAULT,           // initial y position
	    CW_USEDEFAULT,           // initial x size
	    CW_USEDEFAULT,           // initial y size
	    NULL,                    // parent window handle
	    NULL,                    // window menu handle
	    g_dll_instance,               // program instance handle
		NULL) ;		             // creation parameters

	// Hook up the Win32 window for this terminal to the associated Terimnal structure
	SetProp(newterm->win, TerminalProp, newterm);

	// Create statusbar
	newterm->status_bar = CreateStatusWindow(
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | CCS_BOTTOM | SBARS_SIZEGRIP,
		Version,
		newterm->win,
		CID_STATUSBAR
		);

	// Add some items to the system menu
	HMENU sysmenu = GetSystemMenu(newterm->win, FALSE);

	MENUITEMINFO mitem;
	mitem.cbSize = sizeof(MENUITEMINFO);
	mitem.fMask = MIIM_TYPE;
	mitem.fType = MFT_SEPARATOR;
	mitem.dwItemData = 0;

	InsertMenuItem(sysmenu, -1, TRUE, &mitem);

	HFONT font = CreateFontIndirect(&g_default_font);
	newterm->SetFont(font);
	newterm->SetCursorVisibility(1);

	return newterm;
}

void Terminal::RedrawText()
{
	TRACE("Terminal_RedrawText");

	RECT r;
	r.left = 0;
	r.top = 0;
	r.right = this->pixel_width - 1;
	// Don't invalidate the status bar
	r.bottom = this->pixel_height - 1 - this->status_bar_height;

	InvalidateRect(this->win,&r,0);
	UpdateWindow(this->win);
}

void Terminal::Show()
{
	TRACE("Terminal_Show");
	ShowWindow(this->win, SW_SHOWNORMAL);
	UpdateWindow(this->win);
}

// Must be done AFTER a font is selected.
int Terminal::SetCursorVisibility(int visibility)
{
	TRACE("Terminal_SetCursorVisibility");
	
	int old_visibility = this->cursor_visibility;
	this->cursor_visibility = visibility;

	switch(this->cursor_visibility)
	{
	case 0:
		this->caret_yoffs = 0;
		if (this->has_caret) HideCaret(this->win);
		break;

	case 1:
		this->caret_height = 3;
		this->caret_yoffs = this->cell_height - this->caret_height;
		break;

	case 2:
		this->caret_height = this->cell_height;
		this->caret_yoffs = 0;
		break;
	}

	return old_visibility;
}

void Terminal::SetFont(HFONT newfont)
{
	TEXTMETRIC text_metrics;
	RECT rect,cli_rect;

	RECT rect_sbar;

	TRACE("Terminal_SetFont");

	HDC DC = GetDC(this->win);
	SelectFont(DC, newfont);
	GetTextMetrics(DC, &text_metrics);
	ReleaseDC(this->win, DC);

	this->font = newfont;

	this->cell_width = text_metrics.tmAveCharWidth;
	this->cell_height = text_metrics.tmHeight + text_metrics.tmExternalLeading;
	
	// find the minimum window size
	GetWindowRect(this->win,&rect);
	GetClientRect(this->win,&cli_rect);

	GetWindowRect(this->status_bar, &rect_sbar);
	this->status_bar_height = rect_sbar.bottom - rect_sbar.top + 1;

	int xw = rect.right-rect.left+1-cli_rect.right;
	int xh = rect.bottom-rect.top+1-cli_rect.bottom;
	
	this->pixel_width=xw + (this->cell_width * this->width);
	this->pixel_height=xh + (this->cell_height * this->height) + this->status_bar_height;

	MoveWindow(this->win, rect.left, rect.top, this->pixel_width, this->pixel_height, 1);
}

void Terminal::OnPaint()
{
	int r,g,b;

	PAINTSTRUCT ps;
	HDC	hdc = BeginPaint(this->win, &ps);
	SelectFont(hdc,this->font);
	
	for (int y=0; y<this->height; y++)
	{
		int x = 0;
		while (x < this->width)
		{
			char_cell ccch = this->buffer[y][x];
			int colors = g_color_pairs[ (ccch & ATTR_COLOR) >> 8 ];
			int fg = colors & 0xf;
			int bg = (colors & 0xf0) >> 4;

			crack_color(fg, &r, &g, &b);

			if (ccch & ATTR_BOLD)
			{
				r = r?255:0;
				g = g?255:0;
				b = b?255:0;
			}

			int fg_rgb = RGB(r, g, b);
			crack_color(bg, &r, &g, &b);
			int bg_rgb = RGB(r, g, b);

			if (! (ccch & ATTR_REVERSE))
			{
				SetTextColor(hdc,fg_rgb);
				SetBkColor(hdc,bg_rgb);
			}
			else
			{
				SetTextColor(hdc,bg_rgb);
				SetBkColor(hdc,fg_rgb);
			}

			char ch = this->buffer[y][x] & ATTR_CHAR;
			TextOut(hdc, x * this->cell_width, y * this->cell_height, &ch, 1);
			x++;
		}
	}

	EndPaint(this->win, &ps);
	this->UpdateCursorPos();
}

void Terminal::OnSetFocus(HWND oldFocus)
{
	if (this->CursorVisibility())
	{
		CreateCaret(this->win, NULL, this->cell_width, this->caret_height);
		this->UpdateCursorPos();
		ShowCaret(this->win);
		this->has_caret = 1;
	}
}

void Terminal::OnKillFocus(HWND newFocus)
{
	if (this->has_caret)
	{
		DestroyCaret();
		this->has_caret = 0;
	}
}

void Terminal::OnChar(TCHAR ch, int cRepeat)
{
	for (int i=0; i < cRepeat; i++)
	{
		PyList_Append(this->keybuffer, PyInt_FromLong(ch));
	}
}

void Terminal::OnKey(UINT vkey, BOOL fDown, int cRepeat, UINT flags)
{
	// Don't do anything for KEYUP messages.
	if (!fDown)
		return;

	PyObject *mappedkey = PyDict_GetItem(g_keymap_dict, PyInt_FromLong(vkey));
	if (!mappedkey)
		return;

	for (int i=0; i < cRepeat; i++)
	{
		Py_INCREF(mappedkey);
		PyList_Append(this->keybuffer, mappedkey);
	}
}

void Terminal::OnSize(UINT state, int cx, int cy)
{
	MoveWindow(this->StatusBar(), 0, 0, this->pixel_width, this->StatusBarHeight(), TRUE);
}

BOOL Terminal::OnSizing(UINT edge, RECT *size)
{
	if (size->right-size->left+1 < this->pixel_width)
		size->right = size->left+this->pixel_width;

	if (size->bottom-size->top+1 < this->pixel_height)
		size->bottom=size->top+this->pixel_height;

	return TRUE;
}

LRESULT CALLBACK Terminal::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	Terminal *term = (Terminal *)GetProp(hwnd, TerminalProp);

	switch (uMsg)
	{
	case WM_PAINT:
		return term->OnPaint(), 0L;

	case WM_SETFOCUS:
		return term->OnSetFocus((HWND)wParam), 0L;

	case WM_KILLFOCUS:
		return term->OnKillFocus((HWND)wParam), 0L;

	case WM_CHAR:
		return term->OnChar((TCHAR)(wParam), (int)(short)LOWORD(lParam)), 0L;

	case WM_KEYDOWN:
		return term->OnKey((UINT)(wParam), TRUE, (int)(short)LOWORD(lParam), (UINT)HIWORD(lParam)), 0L;

	case WM_SIZE:
		return term->OnSize((UINT)(wParam), (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)), 0L;

	case WM_SIZING:
		return term->OnSizing((UINT)(wParam), (RECT *)(lParam));

	case WM_NCDESTROY:
		RemoveProp(hwnd, TerminalProp);
		return 0;

	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
}
