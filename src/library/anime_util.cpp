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

#include "base/file.h"
#include "base/foreach.h"
#include "base/log.h"
#include "base/process.h"
#include "base/string.h"
#include "library/anime.h"
#include "library/anime_db.h"
#include "library/anime_episode.h"
#include "library/anime_util.h"
#include "library/history.h"
#include "sync/hummingbird_util.h"
#include "sync/sync.h"
#include "taiga/announce.h"
#include "taiga/path.h"
#include "taiga/settings.h"
#include "taiga/taiga.h"
#include "taiga/timer.h"
#include "track/feed.h"
#include "track/media.h"
#include "track/recognition.h"
#include "track/search.h"
#include "ui/ui.h"

namespace anime {

bool IsAiredYet(const Item& item) {
  if (item.GetAiringStatus(false) != kNotYetAired)
    return true;

  if (!IsValidDate(item.GetDateStart()))
    return false;

  Date date_japan = GetDateJapan();
  Date date_start = item.GetDateStart();

  // Assume the worst case
  if (!date_start.month)
    date_start.month = 12;
  if (!date_start.day)
    date_start.day = 31;

  return date_japan >= date_start;
}

bool IsFinishedAiring(const Item& item) {
  if (item.GetAiringStatus(false) == kFinishedAiring)
    return true;

  if (!IsValidDate(item.GetDateEnd()))
    return false;

  if (!IsAiredYet(item))
    return false;

  return GetDateJapan() > item.GetDateEnd();
}

int EstimateLastAiredEpisodeNumber(const Item& item) {
  // Can't estimate for other types of anime
  if (item.GetType() != kTv)
    return 0;

  // TV series air weekly, so the number of weeks that has passed since the day
  // the series started airing gives us the last aired episode. Note that
  // irregularities such as broadcasts being postponed due to sports events make
  // this method unreliable.
  const Date& date_start = item.GetDateStart();
  if (date_start.year && date_start.month && date_start.day) {
    // To compensate for the fact that we don't know the airing hour,
    // we substract one more day.
    int date_diff = GetDateJapan() - date_start - 1;
    if (date_diff > -1) {
      int number_of_weeks = date_diff / 7;
      if (number_of_weeks < item.GetEpisodeCount()) {
        return number_of_weeks + 1;
      } else {
        return item.GetEpisodeCount();
      }
    }
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////

// An item's series information will only be updated only if its last modified
// value is significantly older than the new one's. This helps us lower
// the number of requests we send to a service.

bool IsItemOldEnough(const Item& item) {
  if (!item.GetLastModified())
    return true;

  time_t time_diff = time(nullptr) - item.GetLastModified();

  if (item.GetAiringStatus() == kFinishedAiring) {
    return time_diff >= 60 * 60 * 24 * 7;  // 1 week
  } else {
    return time_diff >= 60 * 60;  // 1 hour
  }
}

bool MetadataNeedsRefresh(const Item& item) {
  if (IsItemOldEnough(item))
    return true;

  if (item.GetSynopsis().empty())
    return true;
  if (item.GetGenres().empty())
    return true;
  if (item.GetScore().empty() &&
      taiga::GetCurrentServiceId() == sync::kMyAnimeList)
    return true;

  return false;
}

////////////////////////////////////////////////////////////////////////////////

bool IsNsfw(const Item& item) {
  if (item.GetAgeRating() == anime::kAgeRatingR18)
    return true;

  if (item.GetAgeRating() == anime::kUnknownAgeRating) {
    auto& genres = item.GetGenres();
    if (std::find(genres.begin(), genres.end(), L"Hentai") != genres.end())
      return true;
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////

bool PlayEpisode(int anime_id, int number) {
  auto anime_item = AnimeDatabase.FindItem(anime_id);

  if (!anime_item)
    return false;

  if (number > anime_item->GetEpisodeCount() &&
      anime_item->GetEpisodeCount() != 0)
    return false;

  if (number == 0)
    number = 1;

  std::wstring file_path;

  // Check saved episode path
  if (number == anime_item->GetMyLastWatchedEpisode() + 1) {
    const std::wstring& next_episode_path = anime_item->GetNextEpisodePath();
    if (!next_episode_path.empty()) {
      if (FileExists(next_episode_path)) {
        file_path = next_episode_path;
      } else {
        LOG(LevelWarning, L"File doesn't exist anymore.");
        LOG(LevelWarning, L"Path: " + next_episode_path);
        anime_item->SetEpisodeAvailability(number, false, L"");
      }
    }
  }

  // Scan available episodes
  if (file_path.empty()) {
    ScanAvailableEpisodes(false, anime_item->GetId(), number);
    if (anime_item->IsEpisodeAvailable(number)) {
      file_path = file_search_helper.path_found();
    }
  }

  if (file_path.empty()) {
    ui::ChangeStatusText(L"Could not find episode #" + ToWstr(number) +
                         L" (" + anime_item->GetTitle() + L").");
  } else {
    Execute(file_path);
  }

  return !file_path.empty();
}

bool PlayLastEpisode(int anime_id) {
  auto anime_item = AnimeDatabase.FindItem(anime_id);

  if (!anime_item)
    return false;

  return PlayEpisode(anime_id, anime_item->GetMyLastWatchedEpisode());
}

bool PlayNextEpisode(int anime_id) {
  auto anime_item = AnimeDatabase.FindItem(anime_id);

  if (!anime_item)
    return false;

  int number = 1;
  if (anime_item->GetEpisodeCount() != 1)
    number = anime_item->GetMyLastWatchedEpisode() + 1;

  return PlayEpisode(anime_id, number);
}

bool PlayRandomAnime() {
  static time_t time_last_checked = 0;
  time_t time_now = time(nullptr);
  if (time_now > time_last_checked + (60 * 2)) {  // 2 minutes
    ScanAvailableEpisodesQuick();
    time_last_checked = time_now;
  }

  std::vector<int> valid_ids;

  foreach_(it, AnimeDatabase.items) {
    anime::Item& anime_item = it->second;
    if (!anime_item.IsInList())
      continue;
    if (!anime_item.IsNewEpisodeAvailable())
      continue;
    switch (anime_item.GetMyStatus()) {
      case anime::kNotInList:
      case anime::kCompleted:
      case anime::kDropped:
        continue;
    }
    valid_ids.push_back(anime_item.GetId());
  }

  foreach_ (id, valid_ids) {
    srand(static_cast<unsigned int>(GetTickCount()));
    size_t max_value = valid_ids.size();
    size_t index = rand() % max_value + 1;
    int anime_id = valid_ids.at(index);
    if (PlayNextEpisode(anime_id))
      return true;
  }

  ui::OnAnimeEpisodeNotFound();
  return false;
}

bool PlayRandomEpisode(Item& item) {
  const int total = item.GetMyStatus() == kCompleted ?
      item.GetEpisodeCount() : item.GetMyLastWatchedEpisode() + 1;
  const int max_tries = item.GetFolder().empty() ? 3 : 10;

  srand(static_cast<unsigned int>(GetTickCount()));

  for (int i = 0; i < min(total, max_tries); i++) {
    int episode_number = rand() % total + 1;
    if (PlayEpisode(item.GetId(), episode_number))
      return true;
  }

  ui::OnAnimeEpisodeNotFound();
  return false;
}

bool LinkEpisodeToAnime(Episode& episode, int anime_id) {
  auto anime_item = AnimeDatabase.FindItem(anime_id);

  if (!anime_item)
    return false;

  episode.anime_id = anime_id;
  anime_item->AddtoUserList();

  auto synonyms = anime_item->GetUserSynonyms();
  synonyms.push_back(CurrentEpisode.title);
  anime_item->SetUserSynonyms(synonyms);
  Meow.UpdateCleanTitles(anime_item->GetId());
  Settings.Save();

  StartWatching(*anime_item, episode);
  ui::ClearStatusText();

  return true;
}

void StartWatching(Item& item, Episode& episode) {
  // Make sure item is in list
  if (!item.IsInList())
    item.AddtoUserList();

  // Change status
  Taiga.play_status = taiga::kPlayStatusPlaying;
  item.SetPlaying(true);

  ui::OnAnimeWatchingStart(item, episode);

  // Check folder
  if (item.GetFolder().empty()) {
    if (episode.folder.empty()) {
      HWND hwnd = MediaPlayers.GetCurrentWindowHandle();
      episode.folder = GetPathOnly(MediaPlayers.GetTitleFromProcessHandle(hwnd));
    }
    if (IsInsideRootFolders(episode.folder)) {
      // Set the folder if only it is under a root folder
      item.SetFolder(episode.folder);
      Settings.Save();
    }
  }

  // Get additional information
  if (item.GetScore().empty() || item.GetSynopsis().empty())
    sync::GetMetadataById(item.GetId());

  // Update list
  if (Settings.GetInt(taiga::kSync_Update_Delay) == 0 &&
      !Settings.GetBool(taiga::kSync_Update_WaitPlayer))
    UpdateList(item, episode);
}

void EndWatching(Item& item, Episode episode) {
  // Change status
  Taiga.play_status = taiga::kPlayStatusStopped;
  item.SetPlaying(false);

  // Announce
  episode.anime_id = item.GetId();
  Announcer.Do(taiga::kAnnounceToHttp, &episode);
  Announcer.Clear(taiga::kAnnounceToSkype);

  episode.anime_id = anime::ID_UNKNOWN;

  ui::OnAnimeWatchingEnd(item, episode);
}

////////////////////////////////////////////////////////////////////////////////

bool IsDeletedFromList(Item& item) {
  foreach_(it, History.queue.items)
    if (it->anime_id == item.GetId())
      if (it->mode == taiga::kHttpServiceDeleteLibraryEntry)
        return true;

  return false;
}

bool IsUpdateAllowed(Item& item, const Episode& episode, bool ignore_update_time) {
  if (episode.processed)
    return false;

  if (!ignore_update_time) {
    auto delay = Settings.GetInt(taiga::kSync_Update_Delay);
    auto ticks = taiga::timers.timer(taiga::kTimerMedia)->ticks();
    if (delay > 0 && ticks > 0)
      return false;
  }

  if (item.GetMyStatus() == kCompleted && item.GetMyRewatching() == 0)
    return false;

  int number = GetEpisodeHigh(episode.number);
  int number_low = GetEpisodeLow(episode.number);
  int last_watched = item.GetMyLastWatchedEpisode();

  if (Settings.GetBool(taiga::kSync_Update_OutOfRange))
    if (number_low > last_watched + 1 || number < last_watched + 1)
      return false;

  if (!IsValidEpisode(number, item.GetEpisodeCount(), last_watched))
    return false;

  return true;
}

void UpdateList(Item& item, Episode& episode) {
  if (!IsUpdateAllowed(item, episode, false))
    return;

  episode.processed = true;

  if (Settings.GetBool(taiga::kSync_Update_AskToConfirm)) {
    ConfirmationQueue.Add(episode);
    ConfirmationQueue.Process();
  } else {
    AddToQueue(item, episode, true);
  }
}

void AddToQueue(Item& item, const Episode& episode, bool change_status) {
  // Create history item
  HistoryItem history_item;
  history_item.anime_id = item.GetId();

  // Set episode number
  history_item.episode = GetEpisodeHigh(episode.number);

  // Set start/finish date
  if (*history_item.episode == 1 && !IsValidDate(item.GetMyDateStart()))
    history_item.date_start = GetDate();
  if (*history_item.episode == item.GetEpisodeCount() && !IsValidDate(item.GetMyDateEnd()))
    history_item.date_finish = GetDate();

  // Set update mode
  if (item.GetMyStatus() == kNotInList) {
    history_item.mode = taiga::kHttpServiceAddLibraryEntry;
    change_status = true;
  } else {
    history_item.mode = taiga::kHttpServiceUpdateLibraryEntry;
  }

  if (change_status) {
    // Move to completed
    if (item.GetEpisodeCount() == *history_item.episode) {
      history_item.status = kCompleted;
      if (item.GetMyRewatching()) {
        history_item.enable_rewatching = FALSE;
        //history_item.times_rewatched++; // TODO: Enable when MAL adds to API
      }
    // Move to watching
    } else if (item.GetMyStatus() != kWatching || *history_item.episode == 1) {
      if (!item.GetMyRewatching()) {
        history_item.status = kWatching;
      }
    }
  }

  // Add to queue
  History.queue.Add(history_item);
}

////////////////////////////////////////////////////////////////////////////////

bool GetFansubFilter(int anime_id, std::vector<std::wstring>& groups) {
  bool found = false;

  foreach_(i, Aggregator.filter_manager.filters) {
    if (found) break;
    foreach_(j, i->anime_ids) {
      if (*j != anime_id) continue;
      if (found) break;
      foreach_(k, i->conditions) {
        if (k->element == kFeedFilterElement_Episode_Group) {
          groups.push_back(k->value);
          found = true;
        }
      }
    }
  }

  return found;
}

bool SetFansubFilter(int anime_id, const std::wstring& group_name) {
  // Check existing filters
  foreach_(i, Aggregator.filter_manager.filters) {
    foreach_(j, i->anime_ids) {
      if (*j != anime_id) continue;
      foreach_(k, i->conditions) {
        if (k->element == kFeedFilterElement_Episode_Group) {
          if (group_name.empty()) {
            Aggregator.filter_manager.filters.erase(i);
          } else {
            k->value = group_name;
          }
          return true;
        }
      }
    }
  }

  if (group_name.empty())
    return false;

  // Create new filter
  auto anime_item = AnimeDatabase.FindItem(anime_id);
  Aggregator.filter_manager.AddFilter(
      kFeedFilterActionPrefer, kFeedFilterMatchAll, kFeedFilterOptionDefault,
      true, L"[Fansub] " + anime_item->GetTitle());
  Aggregator.filter_manager.filters.back().AddCondition(
      kFeedFilterElement_Episode_Group, kFeedFilterOperator_Equals,
      group_name);
  Aggregator.filter_manager.filters.back().anime_ids.push_back(anime_id);
  return true;
}

std::wstring GetImagePath(int anime_id) {
  std::wstring path = taiga::GetPath(taiga::kPathDatabaseImage);
  if (anime_id > 0) path += ToWstr(anime_id) + L".jpg";
  return path;
}

void GetUpcomingTitles(std::vector<int>& anime_ids) {
  foreach_c_(item, AnimeDatabase.items) {
    const anime::Item& anime_item = item->second;

    const Date& date_start = anime_item.GetDateStart();
    const Date& date_now = GetDateJapan();

    if (!date_start.year || !date_start.month || !date_start.day)
      continue;

    if (date_start > date_now &&
        ToDayCount(date_start) < ToDayCount(date_now) + 7) { // Same week
      anime_ids.push_back(anime_item.GetId());
    }
  }
}

////////////////////////////////////////////////////////////////////////////////

bool IsInsideRootFolders(const std::wstring& path) {
  foreach_c_(root_folder, Settings.root_folders)
    if (StartsWith(path, *root_folder))
      return true;

  return false;
}

bool ValidateFolder(Item& item) {
  if (item.GetFolder().empty())
    return false;

  if (FolderExists(item.GetFolder()))
    return true;

  LOG(LevelWarning, L"Folder doesn't exist anymore.\n"
                    L"Path: " + item.GetFolder());

  item.SetFolder(L"");

  for (int i = 1; i <= item.GetAvailableEpisodeCount(); i++)
    item.SetEpisodeAvailability(i, false, L"");

  return false;
}

////////////////////////////////////////////////////////////////////////////////

int GetEpisodeHigh(const std::wstring& episode_number) {
  int value = 1;
  int pos = InStrRev(episode_number, L"-", episode_number.length());

  if (pos == episode_number.length() - 1) {
    value = ToInt(episode_number.substr(0, pos));
  } else if (pos > -1) {
    value = ToInt(episode_number.substr(pos + 1));
  } else {
    value = ToInt(episode_number);
  }

  return value;
}

int GetEpisodeLow(const std::wstring& episode_number) {
  return ToInt(episode_number);  // ToInt() stops at "-"
}

bool IsAllEpisodesAvailable(const Item& item) {
  if (item.GetEpisodeCount() == 0)
    return false;

  int available_episode_count = item.GetAvailableEpisodeCount();
  bool all_episodes_available = available_episode_count > 0;

  for (int i = 1; i <= available_episode_count; i++) {
    if (!item.IsEpisodeAvailable(i)) {
      all_episodes_available = false;
      break;
    }
  }

  return all_episodes_available;
}

bool IsEpisodeRange(const std::wstring& episode_number) {
  return GetEpisodeLow(episode_number) != GetEpisodeHigh(episode_number);
}

bool IsValidEpisode(int episode, int total) {
  if ((episode < 0) ||
      (episode > total && total != 0))
    return false;

  return true;
}

bool IsValidEpisode(int episode, int total, int watched) {
  if (!IsValidEpisode(episode, total) ||
      (episode < watched) ||
      (episode == watched && total != 1))
    return false;

  return true;
}

std::wstring JoinEpisodeNumbers(const std::vector<int>& input) {
  std::wstring output;

  foreach_(it, input) {
    if (!output.empty())
      output += L"-";
    output += ToWstr(*it);
  }

  return output;
}

void SplitEpisodeNumbers(const std::wstring& input, std::vector<int>& output) {
  if (input.empty())
    return;

  std::vector<std::wstring> numbers;
  Split(input, L"-", numbers);

  foreach_(it, numbers)
    output.push_back(ToInt(*it));
}

int EstimateEpisodeCount(const Item& item) {
  // If we already know the number, we don't need to estimate
  if (item.GetEpisodeCount() > 0)
    return item.GetEpisodeCount();

  int number = 0;

  // Estimate using user information
  if (item.IsInList())
    number = max(item.GetMyLastWatchedEpisode(),
                 item.GetAvailableEpisodeCount());

  // Estimate using local information
  number = max(number, const_cast<Item&>(item).GetLastAiredEpisodeNumber());

  // Estimate using airing dates of TV series
  if (item.GetType() == kTv) {
    Date date_start = item.GetDateStart();
    if (IsValidDate(date_start)) {
      Date date_end = item.GetDateEnd();
      // Use current date in Japan if ending date is unknown
      if (!IsValidDate(date_end)) date_end = GetDateJapan();
      // Assuming the series is aired weekly
      number = max(number, (date_end - date_start) / 7);
    }
  }

  // Given all TV series aired since 2000, most them have their episodes
  // spanning one or two seasons. Following is a table of top ten values:
  //
  //   Episodes    Seasons    Percent
  //   ------------------------------
  //         12          1      23.6%
  //         13          1      20.2%
  //         26          2      15.4%
  //         24          2       6.4%
  //         25          2       5.0%
  //         52          4       4.4%
  //         51          4       3.1%
  //         11          1       2.6%
  //         50          4       2.3%
  //         39          3       1.4%
  //   ------------------------------
  //   Total:                   84.6%
  //
  // With that in mind, we can normalize our output at several points.
  if (number < 12) return 13;
  if (number < 24) return 26;
  if (number < 50) return 52;

  // This is a series that has aired for more than a year, which means we cannot
  // estimate for how long it is going to continue.
  return 0;
}

void ChangeEpisode(int anime_id, int value) {
  auto anime_item = AnimeDatabase.FindItem(anime_id);

  if (!anime_item)
    return;

  if (IsValidEpisode(value, anime_item->GetEpisodeCount())) {
    Episode episode;
    episode.number = ToWstr(value);
    AddToQueue(*anime_item, episode, true);
  }
}

void DecrementEpisode(int anime_id) {
  auto anime_item = AnimeDatabase.FindItem(anime_id);

  if (!anime_item)
    return;

  int watched = anime_item->GetMyLastWatchedEpisode();
  auto history_item = History.queue.FindItem(anime_item->GetId(),
                                             kQueueSearchEpisode);

  if (history_item && *history_item->episode == watched &&
      watched > anime_item->GetMyLastWatchedEpisode(false)) {
    history_item->enabled = false;
    History.queue.RemoveDisabled();
  } else {
    ChangeEpisode(anime_id, watched - 1);
  }
}

void IncrementEpisode(int anime_id) {
  auto anime_item = AnimeDatabase.FindItem(anime_id);

  if (!anime_item)
    return;

  int watched = anime_item->GetMyLastWatchedEpisode();

  ChangeEpisode(anime_id, watched + 1);
}

////////////////////////////////////////////////////////////////////////////////

std::wstring TranslateMyStatus(int value, bool add_count) {
  #define ADD_COUNT() (add_count ? L" (" + ToWstr(AnimeDatabase.GetItemCount(value)) + L")" : L"")
  switch (value) {
    case kNotInList: return L"Not in list";
    case kWatching: return L"Currently watching" + ADD_COUNT();
    case kCompleted: return L"Completed" + ADD_COUNT();
    case kOnHold: return L"On hold" + ADD_COUNT();
    case kDropped: return L"Dropped" + ADD_COUNT();
    case kPlanToWatch: return L"Plan to watch" + ADD_COUNT();
    default: return L"";
  }
  #undef ADD_COUNT
}

std::wstring TranslateNumber(int value, const std::wstring& default_char) {
  return value > 0 ? ToWstr(value) : default_char;
}

std::wstring TranslateScore(int value, const std::wstring& default_char) {
  switch (taiga::GetCurrentServiceId()) {
    default:
    case sync::kMyAnimeList:
      return value > 0 ? ToWstr(value) : default_char;

    case sync::kHummingbird:
      return value > 0 ? 
          sync::hummingbird::TranslateMyRatingTo(value) : default_char;
  }
}

std::wstring TranslateScoreFull(int value) {
  switch (taiga::GetCurrentServiceId()) {
    default:
    case sync::kMyAnimeList:
      switch (value) {
        default:
        case 0: return L"(0) No Score";
        case 1: return L"(1) Unwatchable";
        case 2: return L"(2) Horrible";
        case 3: return L"(3) Very Bad";
        case 4: return L"(4) Bad";
        case 5: return L"(5) Average";
        case 6: return L"(6) Fine";
        case 7: return L"(7) Good";
        case 8: return L"(8) Very Good";
        case 9: return L"(9) Great";
        case 10: return L"(10) Masterpiece";
      }
      break;

    case sync::kHummingbird:
      switch (value) {
        default:
        case 0: return L"\u2605 0.0";
        case 1: return L"\u2605 0.5";
        case 2: return L"\u2605 1.0";
        case 3: return L"\u2605 1.5";
        case 4: return L"\u2605 2.0";
        case 5: return L"\u2605 2.5";
        case 6: return L"\u2605 3.0";
        case 7: return L"\u2605 3.5";
        case 8: return L"\u2605 4.0";
        case 9: return L"\u2605 4.5";
        case 10: return L"\u2605 5.0";
      }
      break;
  }
}

std::wstring TranslateStatus(int value) {
  switch (value) {
    case kAiring: return L"Currently airing";
    case kFinishedAiring: return L"Finished airing";
    case kNotYetAired: return L"Not yet aired";
    default: return ToWstr(value);
  }
}

std::wstring TranslateType(int value) {
  switch (value) {
    case kTv: return L"TV";
    case kOva: return L"OVA";
    case kMovie: return L"Movie";
    case kSpecial: return L"Special";
    case kOna: return L"ONA";
    case kMusic: return L"Music";
    default: return L"";
  }
}

int TranslateResolution(const std::wstring& str, bool return_validity) {
  // *###x###*
  if (str.length() > 6) {
    int pos = InStr(str, L"x", 0);
    if (pos > -1) {
      for (unsigned int i = 0; i < str.length(); i++)
        if (i != pos && !IsNumeric(str.at(i)))
          return 0;
      return return_validity ? TRUE : ToInt(str.substr(pos + 1));
    }

  // *###p
  } else if (str.length() > 3) {
    if (str.at(str.length() - 1) == 'p') {
      for (unsigned int i = 0; i < str.length() - 1; i++)
        if (!IsNumeric(str.at(i)))
          return 0;
      return return_validity ? TRUE : ToInt(str.substr(0, str.length() - 1));
    }
  }

  return 0;
}

}  // namespace anime