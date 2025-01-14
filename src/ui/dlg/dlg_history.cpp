/*
** Taiga
** Copyright (C) 2010-2014, Eren Okka
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

#include "base/foreach.h"
#include "base/gfx.h"
#include "base/string.h"
#include "library/anime_db.h"
#include "library/anime_util.h"
#include "library/history.h"
#include "taiga/http.h"
#include "taiga/resource.h"
#include "taiga/taiga.h"
#include "ui/dlg/dlg_history.h"
#include "ui/dlg/dlg_main.h"
#include "ui/dialog.h"
#include "ui/menu.h"
#include "ui/theme.h"
#include "ui/ui.h"

namespace ui {

HistoryDialog DlgHistory;

BOOL HistoryDialog::OnInitDialog() {
  // Create list
  list_.Attach(GetDlgItem(IDC_LIST_EVENT));
  list_.EnableGroupView(true);
  list_.SetExtendedStyle(LVS_EX_AUTOSIZECOLUMNS | LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
  list_.SetImageList(ui::Theme.GetImageList16().GetHandle());
  list_.SetTheme();

  // Insert list columns
  list_.InsertColumn(0, GetSystemMetrics(SM_CXSCREEN), 250, LVCFMT_LEFT, L"Anime title");
  list_.InsertColumn(1, 400, 400, LVCFMT_LEFT, L"Details");
  list_.InsertColumn(2, 120, 120, LVCFMT_LEFT, L"Last modified");
  list_.SetColumnWidth(2, LVSCW_AUTOSIZE_USEHEADER);

  // Insert list groups
  list_.InsertGroup(0, L"Queued for update");
  list_.InsertGroup(1, L"Recently watched");

  // Refresh list
  RefreshList();
  return TRUE;
}

INT_PTR HistoryDialog::DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    // Forward mouse wheel messages to the list
    case WM_MOUSEWHEEL: {
      return list_.SendMessage(uMsg, wParam, lParam);
    }
  }

  return DialogProcDefault(hwnd, uMsg, wParam, lParam);
}


LRESULT HistoryDialog::OnNotify(int idCtrl, LPNMHDR pnmh) {
  if (pnmh->hwndFrom == list_.GetWindowHandle()) {
    switch (pnmh->code) {
      // Custom draw
      case NM_CUSTOMDRAW: {
        LPNMLVCUSTOMDRAW pCD = reinterpret_cast<LPNMLVCUSTOMDRAW>(pnmh);
        switch (pCD->nmcd.dwDrawStage) {
          case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
          case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYSUBITEMDRAW;
          case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
            // Alternate background color
            if (pCD->nmcd.dwItemSpec % 2)
              pCD->clrTextBk = ChangeColorBrightness(GetSysColor(COLOR_WINDOW), -0.03f);
            return CDRF_DODEFAULT;
        }
        break;
      }
      // Double click
      case NM_DBLCLK: {
        if (list_.GetSelectedCount() > 0) {
          auto lpnmitem = reinterpret_cast<LPNMITEMACTIVATE>(pnmh);
          int item_index = list_.GetNextItem(-1, LVNI_SELECTED);
          int anime_id = list_.GetItemParam(item_index);
          ShowDlgAnimeInfo(anime_id);
        }
        break;
      }
      // Right click
      case NM_RCLICK: {
        std::wstring action = ui::Menus.Show(DlgMain.GetWindowHandle(), 0, 0, L"HistoryList");
        if (action == L"Delete()") {
          RemoveItems();
        } else if (action == L"ClearHistory()") {
          History.Clear();
        }
        break;
      }
    }
  }

  return 0;
}

void HistoryDialog::OnSize(UINT uMsg, UINT nType, SIZE size) {
  switch (uMsg) {
    case WM_SIZE: {
      win::Rect rcWindow(0, 0, size.cx, size.cy);
      list_.SetPosition(nullptr, rcWindow, 0);
      break;
    }
  }
}

BOOL HistoryDialog::PreTranslateMessage(MSG* pMsg) {
  switch (pMsg->message) {
    case WM_KEYDOWN: {
      if (::GetFocus() == list_.GetWindowHandle()) {
        switch (pMsg->wParam) {
          // Select all items
          case 'A': {
            if (::GetKeyState(VK_CONTROL) & 0xFF80) {
              list_.SetSelectedItem(-1);
              return TRUE;
            }
            break;
          }
          // Delete selected items
          case VK_DELETE: {
            if (RemoveItems())
              return TRUE;
          }
          // Move selected items
          case VK_UP:
          case VK_DOWN: {
            if (::GetKeyState(VK_CONTROL) & 0xFF80)
              if (MoveItems(pMsg->wParam == VK_UP ? -1 : 1))
                return TRUE;
            break;
          }
        }
      }
      break;
    }
  }

  return FALSE;
}

////////////////////////////////////////////////////////////////////////////////

void HistoryDialog::RefreshList() {
  if (!IsWindow())
    return;

  // Disable drawing
  list_.SetRedraw(FALSE);

  // Clear list
  list_.DeleteAllItems();

  // Add queued items
  foreach_cr_(it, History.queue.items) {
    int i = list_.GetItemCount();

    int icon = ui::kIcon16_ArrowUp;
    switch (it->mode) {
      case taiga::kHttpServiceAddLibraryEntry:
        icon = ui::kIcon16_Plus;
        break;
      case taiga::kHttpServiceDeleteLibraryEntry:
        icon = ui::kIcon16_Cross;
        break;
    }

    std::wstring details;
    if (it->mode == taiga::kHttpServiceAddLibraryEntry)
      AppendString(details, L"Add to list");
    if (it->mode == taiga::kHttpServiceDeleteLibraryEntry)
      AppendString(details, L"Remove from list");
    if (it->episode)
      AppendString(details, L"Episode: " + anime::TranslateNumber(*it->episode));
    if (it->score)
      AppendString(details, L"Score: " + anime::TranslateScore(*it->score));
    if (it->status)
      AppendString(details, !it->enable_rewatching || *it->enable_rewatching != TRUE ?
                   L"Status: " + anime::TranslateMyStatus(*it->status, false) : L"Rewatching");
    if (it->tags)
      AppendString(details, L"Tags: \"" + *it->tags + L"\"");
    if (it->date_start)
      AppendString(details, L"Start date: " + std::wstring(*it->date_start));
    if (it->date_finish)
      AppendString(details, L"Finish date: " + std::wstring(*it->date_finish));

    list_.InsertItem(i, 0, icon, 0, nullptr,
                     AnimeDatabase.FindItem(it->anime_id)->GetTitle().c_str(),
                     static_cast<LPARAM>(it->anime_id));
    list_.SetItem(i, 1, details.c_str());
    list_.SetItem(i, 2, it->time.c_str());
  }

  // Add recently watched
  foreach_cr_(it, History.items) {
    int i = list_.GetItemCount();
    auto anime_item = AnimeDatabase.FindItem(it->anime_id);
    int icon = StatusToIcon(anime_item->GetAiringStatus());
    std::wstring details;
    AppendString(details, L"Episode: " + anime::TranslateNumber(*it->episode));

    list_.InsertItem(i, 1, icon, 0, nullptr,
                     anime_item->GetTitle().c_str(),
                     static_cast<LPARAM>(it->anime_id));
    list_.SetItem(i, 1, details.c_str());
    list_.SetItem(i, 2, it->time.c_str());
  }

  // Redraw
  list_.SetRedraw(TRUE);
  list_.RedrawWindow(nullptr, nullptr,
                     RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

bool HistoryDialog::MoveItems(int pos) {
  // TODO: Re-enable
  return false;

  if (History.queue.updating) {
    MessageBox(L"History cannot be modified while an update is in progress.",
               L"Error", MB_ICONERROR);
    return false;
  }

  int index = -1;
  std::vector<bool> item_selected(list_.GetItemCount());
  std::vector<bool> item_selected_new(list_.GetItemCount());
  while ((index = list_.GetNextItem(index, LVNI_SELECTED)) > -1) {
    item_selected.at(index) = true;
  }

  for (size_t i = 0; i < item_selected.size(); i++) {
    size_t j = (pos < 0 ? i : item_selected.size() - 1 - i);
    if (!item_selected.at(j))
      continue;
    if (j == (pos < 0 ? 0 : item_selected.size() - 1)) {
      item_selected_new.at(j) = true;
      continue;
    }
    if (item_selected_new.at(j + pos)) {
      item_selected_new.at(j) = true;
      continue;
    }
    std::iter_swap(History.queue.items.begin() + j,
                   History.queue.items.begin() + j + pos);
    item_selected_new.at(j + pos) = true;
  }

  RefreshList();
  for (size_t i = 0; i < item_selected_new.size(); i++)
    if (item_selected_new.at(i)) list_.SetSelectedItem(i);

  return true;
}

bool HistoryDialog::RemoveItems() {
  if (History.queue.updating) {
    MessageBox(L"History cannot be modified while an update is in progress.",
               L"Error", MB_ICONERROR);
    return false;
  }

  if (list_.GetSelectedCount() > 0) {
    while (list_.GetSelectedCount() > 0) {
      int item_index = list_.GetNextItem(-1, LVNI_SELECTED);
      list_.DeleteItem(item_index);
      if (item_index < static_cast<int>(History.queue.items.size())) {
        item_index = History.queue.items.size() - item_index - 1;
        History.queue.Remove(item_index, false, false, false);
      } else {
        item_index -= History.queue.items.size();
        item_index = History.items.size() - item_index - 1;
        History.items.erase(History.items.begin() + item_index);
      }
    }
    History.Save();
  } else {
    History.queue.Clear();
  }

  ui::OnHistoryChange();

  return true;
}

}  // namespace ui
