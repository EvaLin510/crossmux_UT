#pragma once

#include <string>

// Clears the reading cache for a book file if its extension is recognised
// (EPUB, XTC, TXT, or Markdown). Does nothing for other file types.
bool clearBookCache(const std::string& path);

// Same as clearBookCache, but restores progress.bin afterwards. For a book file
// replaced in place (re-upload to the same path): layout caches go stale, the
// reading position does not. Readers validate the restored position on load.
bool clearBookCacheKeepProgress(const std::string& path);

// Returns the path-keyed cache directory, or an empty string for non-book files.
std::string bookCachePath(const std::string& path);

// Move cache/bookmark files after the backing book path changed.
bool relocateBookArtifacts(const std::string& oldPath, const std::string& newPath);

// Update path-keyed stores after one backing book path changed.
bool relocateBookReferences(const std::string& oldPath, const std::string& newPath);

// Returns true if the directory name matches a book cache entry.
bool isBookCacheDirectoryName(const char* name);
