#pragma once

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <locale.h>

#ifdef _WIN32
#define STRICT
#define WIN32_LEAN_AND_MEAN
#include <crtdbg.h>
#include <excpt.h>
#include <sdkddkver.h>
#include <windows.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#endif

#include <glib.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <sqlite3.h>
#include <zlib.h>
#include <zip.h>
#ifdef _WIN32
#include <gdk/win32/gdkwin32.h>
#endif

