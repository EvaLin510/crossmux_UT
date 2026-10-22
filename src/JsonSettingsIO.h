#pragma once

class ReadingStatsStore;

namespace JsonSettingsIO {

bool saveReadingStats(const ReadingStatsStore& store, const char* path);
bool loadReadingStats(ReadingStatsStore& store, const char* json);
bool loadReadingStatsFromFile(ReadingStatsStore& store, const char* path);


}  // namespace JsonSettingsIO
