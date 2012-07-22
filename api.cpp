/*
** Taiga, a lightweight client for MyAnimeList
** Copyright (C) 2010-2012, Eren Okka
** 
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "std.h"

#include "api.h"

#include "anime_episode.h"
#include "common.h"
#include "debug.h"
#include "string.h"

class Api TaigaApi;

// =============================================================================

Api::Api() {
  // Register window messages
  wm_attach = ::RegisterWindowMessage(L"TaigaApiAttach");
  wm_detach = ::RegisterWindowMessage(L"TaigaApiDetach");
  wm_ready = ::RegisterWindowMessage(L"TaigaApiReady");
  wm_quit = ::RegisterWindowMessage(L"TaigaApiQuit");
}

Api::~Api() {
  // Destroy API window
  TaigaApi.BroadcastMessage(TaigaApi.wm_quit);
  window.Destroy();
}

void Api::Announce(anime::Episode& episode) {
  for (auto it = handles.begin(); it != handles.end(); ++it) {
    // Validate window
    if (!::IsWindow(it->first)) {
      it = handles.erase(it);
      continue;
    }
    // Validate format
    if (it->second.empty()) {
      continue;
    }

    const char* format = ToANSI(ReplaceVariables(it->second, episode));
    
    COPYDATASTRUCT cds;
    cds.dwData = 0;
    cds.lpData = (void*)format;
    cds.cbData = strlen(format) + 1;

    SendMessage(it->first, WM_COPYDATA, 
                reinterpret_cast<WPARAM>(window.GetWindowHandle()), 
                reinterpret_cast<LPARAM>(&cds));
  }
}

void Api::BroadcastMessage(UINT message) {
  PDWORD_PTR result = nullptr;
  SendMessageTimeout(HWND_BROADCAST, message, 
                     reinterpret_cast<WPARAM>(window.GetWindowHandle()), 
                     0, SMTO_NORMAL, 1000, result);
}

void Api::Create() {
  // Create API window
  window.Create();
  TaigaApi.BroadcastMessage(TaigaApi.wm_ready);
}

// =============================================================================

void Api::Window::PreRegisterClass(WNDCLASSEX& wc) {
  wc.lpszClassName = L"TaigaApiW";
}

void Api::Window::PreCreate(CREATESTRUCT& cs) {
  cs.lpszName = L"Taiga API";
  cs.style = WS_OVERLAPPEDWINDOW;
}

LRESULT Api::Window::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  HWND hwnd_app = reinterpret_cast<HWND>(wParam);

  // Attach a handle
  if (uMsg == TaigaApi.wm_attach) {
    TaigaApi.handles[hwnd_app] = L"";
    debug::Print(L"API - Attached handle: " + 
      ToWstr(reinterpret_cast<int>(hwnd_app)) + L"\n");
    return TRUE;
  }
  // Detach a handle
  if (uMsg == TaigaApi.wm_detach) {
    auto it = TaigaApi.handles.find(hwnd_app);
    if (it != TaigaApi.handles.end()) {
      TaigaApi.handles.erase(it);
      debug::Print(L"API - Detached handle: " + 
        ToWstr(reinterpret_cast<int>(hwnd_app)) + L"\n");
      return TRUE;
    } else {
      return FALSE;
    }
  }

  // Set announcement format
  if (uMsg == WM_COPYDATA) {
    auto cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
    string format = reinterpret_cast<char*>(cds->lpData);
    TaigaApi.handles[hwnd_app] = ToUTF8(format);
    debug::Print(L"API - New format for " + 
      ToWstr(reinterpret_cast<int>(hwnd_app)) + L": \"" + 
      TaigaApi.handles[hwnd_app] + L"\"\n");
    return TRUE;
  }
  
  return WindowProcDefault(hwnd, uMsg, wParam, lParam);
}