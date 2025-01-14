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

#include "base/base64.h"
#include "base/file.h"
#include "base/foreach.h"
#include "base/string.h"
#include "library/history.h"
#include "sync/manager.h"
#include "taiga/resource.h"
#include "taiga/settings.h"
#include "taiga/stats.h"
#include "taiga/taiga.h"
#include "track/media.h"
#include "track/monitor.h"
#include "ui/dlg/dlg_settings.h"
#include "ui/theme.h"
#include "win/win_taskdialog.h"

namespace ui {

const WCHAR* kSectionTitles[] = {
  L" Services",
  L" Library",
  L" Application",
  L" Recognition",
  L" Sharing",
  L" Torrents"
};

SettingsDialog DlgSettings;

SettingsDialog::SettingsDialog()
    : current_section_(kSettingsSectionServices),
      current_page_(kSettingsPageServicesMain) {
  RegisterDlgClass(L"TaigaSettingsW");

  pages.resize(kSettingsPageCount);
  for (size_t i = 0; i < kSettingsPageCount; i++) {
    pages[i].index = i;
    pages[i].parent = this;
  }
}

void SettingsDialog::SetCurrentSection(SettingsSections section) {
  current_section_ = section;

  if (!IsWindow())
    return;

  SetDlgItemText(IDC_STATIC_TITLE, kSectionTitles[current_section_ - 1]);

  tab_.DeleteAllItems();
  switch (current_section_) {
    case kSettingsSectionServices:
      tab_.InsertItem(0, L"Main", kSettingsPageServicesMain);
      tab_.InsertItem(1, L"MyAnimeList", kSettingsPageServicesMal);
      tab_.InsertItem(2, L"Hummingbird", kSettingsPageServicesHummingbird);
      break;
    case kSettingsSectionLibrary:
      tab_.InsertItem(0, L"Folders", kSettingsPageLibraryFolders);
      tab_.InsertItem(1, L"Cache", kSettingsPageLibraryCache);
      break;
    case kSettingsSectionApplication:
      tab_.InsertItem(0, L"Anime list", kSettingsPageAppList);
      tab_.InsertItem(1, L"Behavior", kSettingsPageAppBehavior);
      tab_.InsertItem(2, L"Connection", kSettingsPageAppConnection);
      tab_.InsertItem(3, L"Interface", kSettingsPageAppInterface);
      break;
    case kSettingsSectionRecognition:
      tab_.InsertItem(0, L"General", kSettingsPageRecognitionGeneral);
      tab_.InsertItem(1, L"Media players", kSettingsPageRecognitionMedia);
      tab_.InsertItem(2, L"Media providers", kSettingsPageRecognitionStream);
      break;
    case kSettingsSectionSharing:
      tab_.InsertItem(0, L"HTTP", kSettingsPageSharingHttp);
      tab_.InsertItem(1, L"mIRC", kSettingsPageSharingMirc);
      tab_.InsertItem(2, L"Skype", kSettingsPageSharingSkype);
      tab_.InsertItem(3, L"Twitter", kSettingsPageSharingTwitter);
      break;
    case kSettingsSectionTorrents:
      tab_.InsertItem(0, L"Discovery", kSettingsPageTorrentsDiscovery);
      tab_.InsertItem(1, L"Downloads", kSettingsPageTorrentsDownloads);
      tab_.InsertItem(2, L"Filters", kSettingsPageTorrentsFilters);
      break;
  }
}

void SettingsDialog::SetCurrentPage(SettingsPages page) {
  pages.at(current_page_).Hide();

  current_page_ = page;

  if (!IsWindow())
    return;

  if (!pages.at(current_page_).IsWindow())
    pages.at(current_page_).Create();
  pages.at(current_page_).Show();

  for (int i = 0; i < tab_.GetItemCount(); i++) {
    if (tab_.GetItemParam(i) == current_page_) {
      tab_.SetCurrentlySelected(i);
      break;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////

BOOL SettingsDialog::OnInitDialog() {
  // Initialize controls
  tree_.Attach(GetDlgItem(IDC_TREE_SECTIONS));
  tree_.SetImageList(ui::Theme.GetImageList24().GetHandle());
  tree_.SetTheme();
  tab_.Attach(GetDlgItem(IDC_TAB_PAGES));

  // Add tree items
  #define TREE_INSERTITEM(s, t, i) \
    tree_.items[s] = tree_.InsertItem(t, i, s, nullptr);
  TREE_INSERTITEM(kSettingsSectionServices, L"Services", ui::kIcon24_Globe);
  TREE_INSERTITEM(kSettingsSectionLibrary, L"Library", ui::kIcon24_Library);
  TREE_INSERTITEM(kSettingsSectionApplication, L"Application", ui::kIcon24_Application);
  TREE_INSERTITEM(kSettingsSectionRecognition, L"Recognition", ui::kIcon24_Recognition);
  TREE_INSERTITEM(kSettingsSectionSharing, L"Sharing", ui::kIcon24_Sharing);
  TREE_INSERTITEM(kSettingsSectionTorrents, L"Torrents", ui::kIcon24_Feed);
  #undef TREE_INSERTITEM

  // Set title font
  SendDlgItemMessage(IDC_STATIC_TITLE, WM_SETFONT,
                     reinterpret_cast<WPARAM>(ui::Theme.GetBoldFont()), TRUE);

  // Select current section and page
  SettingsPages current_page = current_page_;
  tree_.SelectItem(tree_.items[current_section_]);
  SetCurrentSection(current_section_);
  SetCurrentPage(current_page);

  return TRUE;
}

////////////////////////////////////////////////////////////////////////////////

void SettingsDialog::OnOK() {
  win::ListView list;
  SettingsPage* page = nullptr;

  std::wstring previous_service = taiga::GetCurrentService()->canonical_name();
  std::wstring previous_user = taiga::GetCurrentUsername();
  std::wstring previous_theme = Settings[taiga::kApp_Interface_Theme];

  // Services > Main
  page = &pages[kSettingsPageServicesMain];
  if (page->IsWindow()) {
    win::ComboBox combo(page->GetDlgItem(IDC_COMBO_SERVICE));
    auto service_id = static_cast<sync::ServiceId>(combo.GetItemData(combo.GetCurSel()));
    auto service = ServiceManager.service(service_id);
    Settings.Set(taiga::kSync_ActiveService, service->canonical_name());
    Settings.Set(taiga::kSync_AutoOnStart, page->IsDlgButtonChecked(IDC_CHECK_START_LOGIN));
    combo.SetWindowHandle(nullptr);
  }
  // Services > MyAnimeList
  page = &pages[kSettingsPageServicesMal];
  if (page->IsWindow()) {
    Settings.Set(taiga::kSync_Service_Mal_Username, page->GetDlgItemText(IDC_EDIT_USER_MAL));
    Settings.Set(taiga::kSync_Service_Mal_Password, Base64Encode(page->GetDlgItemText(IDC_EDIT_PASS_MAL)));
  }
  // Services > Hummingbird
  page = &pages[kSettingsPageServicesHummingbird];
  if (page->IsWindow()) {
    Settings.Set(taiga::kSync_Service_Hummingbird_Username, page->GetDlgItemText(IDC_EDIT_USER_HUMMINGBIRD));
    Settings.Set(taiga::kSync_Service_Hummingbird_Password, Base64Encode(page->GetDlgItemText(IDC_EDIT_PASS_HUMMINGBIRD)));
  }

  // Library > Folders
  page = &pages[kSettingsPageLibraryFolders];
  if (page->IsWindow()) {
    list.SetWindowHandle(page->GetDlgItem(IDC_LIST_FOLDERS_ROOT));
    Settings.root_folders.clear();
    for (int i = 0; i < list.GetItemCount(); i++) {
      std::wstring folder;
      list.GetItemText(i, 0, folder);
      Settings.root_folders.push_back(folder);
    }
    Settings.Set(taiga::kLibrary_WatchFolders, page->IsDlgButtonChecked(IDC_CHECK_FOLDERS_WATCH));
    list.SetWindowHandle(nullptr);
  }

  // Application > Behavior
  page = &pages[kSettingsPageAppBehavior];
  if (page->IsWindow()) {
    Settings.Set(taiga::kApp_Behavior_Autostart, page->IsDlgButtonChecked(IDC_CHECK_AUTOSTART));
    Settings.Set(taiga::kApp_Behavior_CloseToTray, page->IsDlgButtonChecked(IDC_CHECK_GENERAL_CLOSE));
    Settings.Set(taiga::kApp_Behavior_MinimizeToTray, page->IsDlgButtonChecked(IDC_CHECK_GENERAL_MINIMIZE));
    Settings.Set(taiga::kApp_Behavior_CheckForUpdates, page->IsDlgButtonChecked(IDC_CHECK_START_VERSION));
    Settings.Set(taiga::kApp_Behavior_ScanAvailableEpisodes, page->IsDlgButtonChecked(IDC_CHECK_START_CHECKEPS));
    Settings.Set(taiga::kApp_Behavior_StartMinimized, page->IsDlgButtonChecked(IDC_CHECK_START_MINIMIZE));
  }
  // Application > Connection
  page = &pages[kSettingsPageAppConnection];
  if (page->IsWindow()) {
    Settings.Set(taiga::kApp_Connection_ProxyHost, page->GetDlgItemText(IDC_EDIT_PROXY_HOST));
    Settings.Set(taiga::kApp_Connection_ProxyUsername, page->GetDlgItemText(IDC_EDIT_PROXY_USER));
    Settings.Set(taiga::kApp_Connection_ProxyPassword, page->GetDlgItemText(IDC_EDIT_PROXY_PASS));
  }
  // Application > Interface
  page = &pages[kSettingsPageAppInterface];
  if (page->IsWindow()) {
    Settings.Set(taiga::kApp_Interface_Theme, page->GetDlgItemText(IDC_COMBO_THEME));
    Settings.Set(taiga::kApp_Interface_ExternalLinks, page->GetDlgItemText(IDC_EDIT_EXTERNALLINKS));
  }
  // Application > List
  page = &pages[kSettingsPageAppList];
  if (page->IsWindow()) {
    Settings.Set(taiga::kApp_List_DoubleClickAction, page->GetComboSelection(IDC_COMBO_DBLCLICK));
    Settings.Set(taiga::kApp_List_MiddleClickAction, page->GetComboSelection(IDC_COMBO_MDLCLICK));
    Settings.Set(taiga::kApp_List_DisplayEnglishTitles, page->IsDlgButtonChecked(IDC_CHECK_LIST_ENGLISH));
    Settings.Set(taiga::kApp_List_HighlightNewEpisodes, page->IsDlgButtonChecked(IDC_CHECK_HIGHLIGHT));
    Settings.Set(taiga::kApp_List_ProgressDisplayAired, page->IsDlgButtonChecked(IDC_CHECK_LIST_PROGRESS_AIRED));
    Settings.Set(taiga::kApp_List_ProgressDisplayAvailable, page->IsDlgButtonChecked(IDC_CHECK_LIST_PROGRESS_AVAILABLE));
  }

  // Recognition > General
  page = &pages[kSettingsPageRecognitionGeneral];
  if (page->IsWindow()) {
    Settings.Set(taiga::kSync_Update_AskToConfirm, page->IsDlgButtonChecked(IDC_CHECK_UPDATE_CONFIRM));
    Settings.Set(taiga::kSync_Update_CheckPlayer, page->IsDlgButtonChecked(IDC_CHECK_UPDATE_CHECKMP));
    Settings.Set(taiga::kSync_Update_GoToNowPlaying, page->IsDlgButtonChecked(IDC_CHECK_UPDATE_GOTO));
    Settings.Set(taiga::kSync_Update_OutOfRange, page->IsDlgButtonChecked(IDC_CHECK_UPDATE_RANGE));
    Settings.Set(taiga::kSync_Update_OutOfRoot, page->IsDlgButtonChecked(IDC_CHECK_UPDATE_ROOT));
    Settings.Set(taiga::kSync_Update_WaitPlayer, page->IsDlgButtonChecked(IDC_CHECK_UPDATE_WAITMP));
    Settings.Set(taiga::kSync_Update_Delay, static_cast<int>(page->GetDlgItemInt(IDC_EDIT_DELAY)));
    Settings.Set(taiga::kSync_Notify_Recognized, page->IsDlgButtonChecked(IDC_CHECK_NOTIFY_RECOGNIZED));
    Settings.Set(taiga::kSync_Notify_NotRecognized, page->IsDlgButtonChecked(IDC_CHECK_NOTIFY_NOTRECOGNIZED));
  }
  // Recognition > Media players
  page = &pages[kSettingsPageRecognitionMedia];
  if (page->IsWindow()) {
    list.SetWindowHandle(page->GetDlgItem(IDC_LIST_MEDIA));
    for (size_t i = 0; i < MediaPlayers.items.size(); i++)
      MediaPlayers.items[i].enabled = list.GetCheckState(i);
    list.SetWindowHandle(nullptr);
  }
  // Recognition > Media providers
  page = &pages[kSettingsPageRecognitionStream];
  if (page->IsWindow()) {
    list.SetWindowHandle(page->GetDlgItem(IDC_LIST_STREAM_PROVIDER));
    Settings.Set(taiga::kStream_Animelab, list.GetCheckState(0) == TRUE);
    Settings.Set(taiga::kStream_Ann, list.GetCheckState(1) == TRUE);
    Settings.Set(taiga::kStream_Crunchyroll, list.GetCheckState(2) == TRUE);
    Settings.Set(taiga::kStream_Daisuki, list.GetCheckState(3) == TRUE);
    Settings.Set(taiga::kStream_Veoh, list.GetCheckState(4) == TRUE);
    Settings.Set(taiga::kStream_Viz, list.GetCheckState(5) == TRUE);
    Settings.Set(taiga::kStream_Youtube, list.GetCheckState(6) == TRUE);
    list.SetWindowHandle(nullptr);
  }

  // Sharing > HTTP
  page = &pages[kSettingsPageSharingHttp];
  if (page->IsWindow()) {
    Settings.Set(taiga::kShare_Http_Enabled, page->IsDlgButtonChecked(IDC_CHECK_HTTP));
    Settings.Set(taiga::kShare_Http_Url, page->GetDlgItemText(IDC_EDIT_HTTP_URL));
  }
  // Sharing > mIRC
  page = &pages[kSettingsPageSharingMirc];
  if (page->IsWindow()) {
    Settings.Set(taiga::kShare_Mirc_Enabled, page->IsDlgButtonChecked(IDC_CHECK_MIRC));
    Settings.Set(taiga::kShare_Mirc_Service, page->GetDlgItemText(IDC_EDIT_MIRC_SERVICE));
    Settings.Set(taiga::kShare_Mirc_Mode, page->GetCheckedRadioButton(IDC_RADIO_MIRC_CHANNEL1, IDC_RADIO_MIRC_CHANNEL3) + 1);
    Settings.Set(taiga::kShare_Mirc_MultiServer, page->IsDlgButtonChecked(IDC_CHECK_MIRC_MULTISERVER));
    Settings.Set(taiga::kShare_Mirc_UseMeAction, page->IsDlgButtonChecked(IDC_CHECK_MIRC_ACTION));
    Settings.Set(taiga::kShare_Mirc_Channels, page->GetDlgItemText(IDC_EDIT_MIRC_CHANNELS));
  }
  // Sharing > Skype
  page = &pages[kSettingsPageSharingSkype];
  if (page->IsWindow()) {
    Settings.Set(taiga::kShare_Skype_Enabled, page->IsDlgButtonChecked(IDC_CHECK_SKYPE));
  }
  // Sharing > Twitter
  page = &pages[kSettingsPageSharingTwitter];
  if (page->IsWindow()) {
    Settings.Set(taiga::kShare_Twitter_Enabled, page->IsDlgButtonChecked(IDC_CHECK_TWITTER));
  }

  // Torrents > Discovery
  page = &pages[kSettingsPageTorrentsDiscovery];
  if (page->IsWindow()) {
    Settings.Set(taiga::kTorrent_Discovery_Source, page->GetDlgItemText(IDC_COMBO_TORRENT_SOURCE));
    Settings.Set(taiga::kTorrent_Discovery_SearchUrl, page->GetDlgItemText(IDC_COMBO_TORRENT_SEARCH));
    Settings.Set(taiga::kTorrent_Discovery_AutoCheckEnabled, page->IsDlgButtonChecked(IDC_CHECK_TORRENT_AUTOCHECK));
    Settings.Set(taiga::kTorrent_Discovery_AutoCheckInterval, static_cast<int>(page->GetDlgItemInt(IDC_EDIT_TORRENT_INTERVAL)));
    Settings.Set(taiga::kTorrent_Discovery_NewAction, page->GetCheckedRadioButton(IDC_RADIO_TORRENT_NEW1, IDC_RADIO_TORRENT_NEW2) + 1);
  }
  // Torrents > Downloads
  page = &pages[kSettingsPageTorrentsDownloads];
  if (page->IsWindow()) {
    Settings.Set(taiga::kTorrent_Download_AppMode, page->GetCheckedRadioButton(IDC_RADIO_TORRENT_APP1, IDC_RADIO_TORRENT_APP2) + 1);
    Settings.Set(taiga::kTorrent_Download_AppPath, page->GetDlgItemText(IDC_EDIT_TORRENT_APP));
    Settings.Set(taiga::kTorrent_Download_UseAnimeFolder, page->IsDlgButtonChecked(IDC_CHECK_TORRENT_AUTOSETFOLDER));
    Settings.Set(taiga::kTorrent_Download_FallbackOnFolder, page->IsDlgButtonChecked(IDC_CHECK_TORRENT_AUTOUSEFOLDER));
    Settings.Set(taiga::kTorrent_Download_Location, page->GetDlgItemText(IDC_COMBO_TORRENT_FOLDER));
    Settings.Set(taiga::kTorrent_Download_CreateSubfolder, page->IsDlgButtonChecked(IDC_CHECK_TORRENT_AUTOCREATEFOLDER));
  }
  // Torrents > Filters
  page = &pages[kSettingsPageTorrentsFilters];
  if (page->IsWindow()) {
    Settings.Set(taiga::kTorrent_Filter_Enabled, page->IsDlgButtonChecked(IDC_CHECK_TORRENT_FILTER));
    list.SetWindowHandle(page->GetDlgItem(IDC_LIST_TORRENT_FILTER));
    for (int i = 0; i < list.GetItemCount(); i++) {
      FeedFilter* filter = reinterpret_cast<FeedFilter*>(list.GetItemParam(i));
      if (filter) filter->enabled = list.GetCheckState(i) == TRUE;
    }
    list.SetWindowHandle(nullptr);
    Aggregator.filter_manager.filters.clear();
    for (auto it = feed_filters_.begin(); it != feed_filters_.end(); ++it)
      Aggregator.filter_manager.filters.push_back(*it);
  }

  // Save settings
  Settings.Save();

  // Apply changes
  Settings.ApplyChanges(previous_service, previous_user, previous_theme);

  // End dialog
  EndDialog(IDOK);
}

////////////////////////////////////////////////////////////////////////////////

INT_PTR SettingsDialog::DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    // Set title color
    case WM_CTLCOLORSTATIC: {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      HWND hwnd_static = reinterpret_cast<HWND>(lParam);
      if (hwnd_static == GetDlgItem(IDC_STATIC_TITLE)) {
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, ::GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<INT_PTR>(::GetSysColorBrush(COLOR_APPWORKSPACE));
      }
      break;
    }

    // Taiga, help! Only you can save us!
    case WM_HELP: {
      OnHelp(reinterpret_cast<LPHELPINFO>(lParam));
      return TRUE;
    }

    // Forward mouse wheel messages to the list
    case WM_MOUSEWHEEL:
      switch (current_page_) {
        case kSettingsPageLibraryFolders:
          return pages[kSettingsPageLibraryFolders].SendDlgItemMessage(
              IDC_LIST_FOLDERS_ROOT, uMsg, wParam, lParam);
        case kSettingsPageRecognitionMedia:
          return pages[kSettingsPageRecognitionMedia].SendDlgItemMessage(
              IDC_LIST_MEDIA, uMsg, wParam, lParam);
        case kSettingsPageTorrentsFilters:
          return pages[kSettingsPageTorrentsFilters].SendDlgItemMessage(
              IDC_LIST_TORRENT_FILTER, uMsg, wParam, lParam);
      }
      break;
  }

  return DialogProcDefault(hwnd, uMsg, wParam, lParam);
}

void SettingsDialog::OnHelp(LPHELPINFO lphi) {
  std::wstring message, text;
  pages.at(current_page_).GetDlgItemText(lphi->iCtrlId, text);

  #define SET_HELP_TEXT(i, m) \
    case i: message = m; break;
  switch (lphi->iCtrlId) {
    // Library > Folders
    SET_HELP_TEXT(IDC_LIST_FOLDERS_ROOT,
        L"These folders will be scanned and monitored for new episodes.\n\n"
        L"Suppose that you have an HDD like this:\n\n"
        L"    D:\\\n"
        L"    \u2514 Anime\n"
        L"        \u2514 Bleach\n"
        L"        \u2514 Naruto\n"
        L"        \u2514 One Piece\n"
        L"    \u2514 Games\n"
        L"    \u2514 Music\n\n"
        L"In this case, \"D:\\Anime\" is the root folder you should add.");
    SET_HELP_TEXT(IDC_CHECK_FOLDERS_WATCH,
        L"With this feature on, Taiga instantly detects when a file is added, removed, or renamed under root folders and their subfolders.\n\n"
        L"Enabling this feature is recommended.");
    // Not available
    default:
      message = L"There's no help message associated with this item.";
      break;
  }
  #undef SET_HELP_TEXT

  MessageBox(message.c_str(), L"Help", MB_ICONINFORMATION | MB_OK);
}

LRESULT SettingsDialog::OnNotify(int idCtrl, LPNMHDR pnmh) {
  switch (idCtrl) {
    case IDC_TREE_SECTIONS: {
      switch (pnmh->code) {
        // Select section
        case TVN_SELCHANGED: {
          LPNMTREEVIEW pnmtv = reinterpret_cast<LPNMTREEVIEW>(pnmh);
          auto section_new = static_cast<SettingsSections>(pnmtv->itemNew.lParam);
          auto section_old = static_cast<SettingsSections>(pnmtv->itemOld.lParam);
          if (section_new != section_old) {
            SetCurrentSection(section_new);
            SetCurrentPage(static_cast<SettingsPages>(tab_.GetItemParam(0)));
          }
          break;
        }
      }
      break;
    }

    case IDC_TAB_PAGES: {
      switch (pnmh->code) {
        // Select tab
        case TCN_SELCHANGE: {
          auto page = static_cast<SettingsPages>(tab_.GetItemParam(tab_.GetCurrentlySelected()));
          SetCurrentPage(page);
          break;
        }
      }
      break;
    }

    case IDC_LINK_DEFAULTS: {
      switch (pnmh->code) {
        // Restore default settings
        case NM_CLICK: {
          win::TaskDialog dlg;
          dlg.SetWindowTitle(TAIGA_APP_NAME);
          dlg.SetMainIcon(TD_ICON_WARNING);
          dlg.SetMainInstruction(L"Are you sure you want to restore default settings?");
          dlg.SetContent(L"All your current settings will be lost.");
          dlg.AddButton(L"Yes", IDYES);
          dlg.AddButton(L"No", IDNO);
          dlg.Show(GetWindowHandle());
          if (dlg.GetSelectedButtonID() == IDYES)
            Settings.RestoreDefaults();
          return TRUE;
        }
      }
      break;
    }
  }

  return 0;
}

LRESULT SettingsDialog::TreeView::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    // Forward mouse wheel messages to parent
    case WM_MOUSEWHEEL:
      return ::SendMessage(GetParent(), uMsg, wParam, lParam);
  }

  return WindowProcDefault(hwnd, uMsg, wParam, lParam);
}

////////////////////////////////////////////////////////////////////////////////

int SettingsDialog::AddTorrentFilterToList(HWND hwnd_list, const FeedFilter& filter) {
  win::ListView list = hwnd_list;
  int index = list.GetItemCount();
  int group = filter.anime_ids.empty() ? 0 : 1;

  int icon = ui::kIcon16_Funnel;
  switch (filter.action) {
    case kFeedFilterActionDiscard: icon = ui::kIcon16_FunnelCross; break;
    case kFeedFilterActionSelect:  icon = ui::kIcon16_FunnelTick;  break;
    case kFeedFilterActionPrefer:  icon = ui::kIcon16_FunnelPlus;  break;
  }

  // Insert item
  index = list.InsertItem(index, group, icon, 0, nullptr, filter.name.c_str(),
                          reinterpret_cast<LPARAM>(&filter));
  list.SetCheckState(index, filter.enabled);
  list.SetWindowHandle(nullptr);

  return index;
}

void SettingsDialog::RefreshCache() {
  std::wstring text;
  Stats.CalculateLocalData();
  SettingsPage& page = pages[kSettingsPageLibraryCache];

  // History
  text = ToWstr(static_cast<int>(History.items.size())) + L" item(s)";
  page.SetDlgItemText(IDC_STATIC_CACHE1, text.c_str());

  // Image files
  text = ToWstr(Stats.image_count) + L" item(s), " + ToSizeString(Stats.image_size);
  page.SetDlgItemText(IDC_STATIC_CACHE2, text.c_str());

  // Torrent files
  text = ToWstr(Stats.torrent_count) + L" item(s), " + ToSizeString(Stats.torrent_size);
  page.SetDlgItemText(IDC_STATIC_CACHE3, text.c_str());
}

void SettingsDialog::RefreshTorrentFilterList(HWND hwnd_list) {
  win::ListView list = hwnd_list;
  list.DeleteAllItems();

  for (auto it = feed_filters_.begin(); it != feed_filters_.end(); ++it)
    AddTorrentFilterToList(hwnd_list, *it);

  list.SetColumnWidth(0, LVSCW_AUTOSIZE_USEHEADER);
  list.SetWindowHandle(nullptr);
}

void SettingsDialog::RefreshTwitterLink() {
  std::wstring text;

  if (Settings[taiga::kShare_Twitter_Username].empty()) {
    text = L"Taiga is not authorized to post to your Twitter account yet.";
  } else {
    text = L"Taiga is authorized to post to this Twitter account: ";
    text += L"<a href=\"URL(http://twitter.com/" + Settings[taiga::kShare_Twitter_Username];
    text += L")\">" + Settings[taiga::kShare_Twitter_Username] + L"</a>";
  }

  pages[kSettingsPageSharingTwitter].SetDlgItemText(IDC_LINK_TWITTER, text.c_str());
}

}  // namespace ui