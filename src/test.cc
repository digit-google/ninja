// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifdef _WIN32
#include <direct.h>  // Has to be before util.h is included.
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <string>

#include "test.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "build_log.h"
#include "graph.h"
#include "manifest_parser.h"
#include "util.h"

#ifdef _AIX
extern "C" {
        // GCC "helpfully" strips the definition of mkdtemp out on AIX.
        // The function is still present, so if we define it ourselves
        // it will work perfectly fine.
        extern char* mkdtemp(char* name_template);
}
#endif

using namespace std;

namespace {

void CreateWritableTempFile(const std::string& path,
                            const std::string contents) {
  FILE* file = fopen(path.c_str(), "w+b");
  if (!file)
    Fatal("Could not create writable temporary file!");

  if (!contents.empty()) {
    int ret = fwrite(contents.data(), contents.size(), 1, file);
    if (ret != 1)
      Fatal("Could not write writable temporary file!");
  }

  fclose(file);
}

std::string ReadWritableTempFile(const std::string& path) {
  std::string result;
  FILE* file = fopen(path.c_str(), "rb");
  if (!file)
    Fatal("Could not read writable temporary file!");
  if (fseek(file, 0, SEEK_END) != 0)
    Fatal("Could not seek to end of writable temporary file!");
  long file_size = ftell(file);
  if (file_size < 0)
    Fatal("Could not get writable temporary file size!");
  result.resize(static_cast<size_t>(file_size));
  if (file_size > 0) {
    if (fseek(file, 0, SEEK_SET) != 0)
      Fatal("Could not rewind to start of writable temporary file!");
    int ret = fread(const_cast<char*>(result.data()), result.size(), 1, file);
    if (ret != 1)
      Fatal("Could not read writable temporary file!");
  }
  fclose(file);
  return result;
}

void RemoveWritableTempFile(const std::string& path) {
#ifdef _WIN32
  _unlink(path.c_str());
#else   // !_WIN32
  unlink(path.c_str());
#endif  // !_WIN32
}

#ifdef _WIN32
/// Windows has no mkdtemp.  Implement it in terms of _mktemp_s.
char* mkdtemp(char* name_template) {
  int err = _mktemp_s(name_template, strlen(name_template) + 1);
  if (err != 0) {
    perror("_mktemp_s");
    return NULL;
  }

  err = _mkdir(name_template);
  if (err < 0) {
    perror("mkdir");
    return NULL;
  }

  return name_template;
}
#endif  // _WIN32

/// Return system temporary directory, if it exists.
/// the result always has a trailing directory separator,
/// or is empty on failure.
std::string GetSystemTempDir() {
#ifdef _WIN32
  char buf[1024];
  if (!GetTempPath(sizeof(buf), buf))
    return "";
  return buf;
#else
  std::string result = "/tmp/";
  const char* tempdir = getenv("TMPDIR");
  if (tempdir && tempdir[0]) {
    result = tempdir;
    if (result.back() != '/')
      result.push_back('/');
  }
  return result;
#endif
}

std::string GetTemporaryFilePath() {
  std::string temp_path = GetSystemTempDir() + "ninja.test.XXXXXX";
#ifdef _WIN32
  int err =
      _mktemp_s(const_cast<char*>(temp_path.data()), temp_path.size() + 1);
  if (err < 0) {
    perror("_mktemp_s");
    return nullptr;
  }
#else   // !_WIN32
  int ret = mkstemp(const_cast<char*>(temp_path.data()));
  if (ret < 0)
    Fatal("mkstemp");
#endif  // !_WIN32
  return temp_path;
}

#ifdef _WIN32

/// An implementation of fmemopen() that writes the content of the buffer
/// to a temporary file then returns an open handle to it. The file itself
/// is deleted on fclose().
FILE* fmemopen(void* buf, size_t size, const char* mode) {
  std::string temp_path = GetTemporaryFilePath();
  std::wstring wide_path = UTF8ToWin32Unicode(temp_path);
  HANDLE handle =
      CreateFileW(wide_path.c_str(), DELETE | GENERIC_READ | GENERIC_WRITE, 0,
                  nullptr, CREATE_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    errno = EINVAL;
    return nullptr;
  }
  FILE* fp = _fdopen(_open_osfhandle((intptr_t)handle, 0), "w+b");
  if (!fp) {
    ::CloseHandle(handle);
    return nullptr;
  }
  if (buf && size && fwrite(buf, size, 1, fp) != 1) {
    fclose(fp);
    return nullptr;
  }
  rewind(fp);
  return fp;
}

#endif  // _WIN32

}  // anonymous namespace

StateTestWithBuiltinRules::StateTestWithBuiltinRules() {
  AddCatRule(&state_);
}

void StateTestWithBuiltinRules::AddCatRule(State* state) {
  AssertParse(state,
"rule cat\n"
"  command = cat $in > $out\n");
}

Node* StateTestWithBuiltinRules::GetNode(const string& path) {
  EXPECT_FALSE(strpbrk(path.c_str(), "/\\"));
  return state_.GetNode(path, 0);
}

void AssertParse(State* state, const char* input,
                 ManifestParserOptions opts) {
  ManifestParser parser(state, NULL, opts);
  string err;
  EXPECT_TRUE(parser.ParseTest(input, &err));
  ASSERT_EQ("", err);
  VerifyGraph(*state);
}

void AssertHash(const char* expected, uint64_t actual) {
  ASSERT_EQ(BuildLog::LogEntry::HashCommand(expected), actual);
}

void VerifyGraph(const State& state) {
  for (vector<Edge*>::const_iterator e = state.edges_.begin();
       e != state.edges_.end(); ++e) {
    // All edges need at least one output.
    EXPECT_FALSE((*e)->outputs_.empty());
    // Check that the edge's inputs have the edge as out-edge.
    for (vector<Node*>::const_iterator in_node = (*e)->inputs_.begin();
         in_node != (*e)->inputs_.end(); ++in_node) {
      const vector<Edge*>& out_edges = (*in_node)->out_edges();
      EXPECT_NE(find(out_edges.begin(), out_edges.end(), *e),
                out_edges.end());
    }
    // Check that the edge's outputs have the edge as in-edge.
    for (vector<Node*>::const_iterator out_node = (*e)->outputs_.begin();
         out_node != (*e)->outputs_.end(); ++out_node) {
      EXPECT_EQ((*out_node)->in_edge(), *e);
    }
  }

  // The union of all in- and out-edges of each nodes should be exactly edges_.
  set<const Edge*> node_edge_set;
  for (State::Paths::const_iterator p = state.paths_.begin();
       p != state.paths_.end(); ++p) {
    const Node* n = p->second;
    if (n->in_edge())
      node_edge_set.insert(n->in_edge());
    node_edge_set.insert(n->out_edges().begin(), n->out_edges().end());
  }
  set<const Edge*> edge_set(state.edges_.begin(), state.edges_.end());
  EXPECT_EQ(node_edge_set, edge_set);
}

void VirtualFileSystem::Create(const string& path,
                               const string& contents) {
  auto& entry = files_[path];
  entry.mtime = now_;
  entry.contents = contents;
  files_created_.insert(path);
}

VirtualFileSystem::Entry::~Entry() {
  if (!writable_path.empty())
    RemoveWritableTempFile(writable_path);
}

TimeStamp VirtualFileSystem::Stat(const string& path, string* err) const {
  FileMap::const_iterator i = files_.find(path);
  if (i != files_.end()) {
    *err = i->second.stat_error;
    return i->second.mtime;
  }
  return 0;
}

bool VirtualFileSystem::WriteFile(const string& path, const string& contents) {
  auto it = files_.find(path);
  if (it == files_.end()) {
    // This is a new file, create in-memory content.
    Create(path, contents);
  } else {
    Entry& entry = it->second;
    if (!entry.writable_path.empty()) {
      // Write new contents to temporary writable file.
      CreateWritableTempFile(entry.writable_path, contents);
    } else {
      // Replace in-memory contents.
      entry.contents = contents;
    }
    entry.mtime = now_;
    entry.stat_error.clear();
  }
  return true;
}

bool VirtualFileSystem::MakeDir(const string& path) {
  directories_made_.push_back(path);
  return true;  // success
}

FileReader::Status VirtualFileSystem::ReadFile(const string& path,
                                               string* contents,
                                               string* err) {
  files_read_.push_back(path);
  FileMap::iterator i = files_.find(path);
  if (i != files_.end()) {
    auto& entry = i->second;
    if (!entry.writable_path.empty()) {
      // OpenFile() was previously called with write or append mode,
      // so read the temporary file from disk.
      assert(entry.contents.empty());
      *contents = ReadWritableTempFile(entry.writable_path);
    } else {
      // Get the content from memory.
      *contents = entry.contents;
    }
    return Okay;
  }
  *err = strerror(ENOENT);
  return NotFound;
}

int VirtualFileSystem::RemoveFile(const string& path) {
  auto& dirs = directories_made_;
  auto dir_it = std::find(dirs.begin(), dirs.end(), path);
  if (dir_it != dirs.end()) {
    // Error, because RemoveFile() cannot remove directories,
    // even if they are empty.
    errno = EISDIR;
    return -1;
  }

  auto i = files_.find(path);
  if (i != files_.end()) {
    files_.erase(i);
    files_removed_.insert(path);
    return 0;
  } else {
    return 1;
  }
}

bool VirtualFileSystem::RenameFile(const std::string& from,
                                   const std::string& to) {
  auto& dirs = directories_made_;
  auto dir_from_it = std::find(dirs.begin(), dirs.end(), from);
  if (dir_from_it != dirs.end()) {
    // Renaming an existing directory.

    // Verify that destination is not an existing file. If so, remove it.
    auto to_it = files_.find(to);
    if (to_it != files_.end()) {
      files_.erase(to_it);
    }

    // Check that an existing destination directory is empty.
    std::string to_prefix = to + "/";
    auto dir_to_it = std::find(dirs.begin(), dirs.end(), to);
    if (dir_to_it != dirs.end()) {
      // destination directory exists. Verify that it is empty.
      for (const auto& pair : files_) {
        const std::string& path = pair.first;
        if (path.substr(0, to_prefix.size()) == to_prefix) {
          errno = ENOTEMPTY;
          return -1;
        }
      }
    }

    // Remove source directory from list.
    dirs.erase(dir_from_it);

    // Now rename any files belonging to the source directory.
    // First remove any file entry from the map that starts with |from_prefix|,
    // saving its renamed file path and entry content to |to_rename|.
    std::string from_prefix = from + "/";
    using FileEntry = FileMap::value_type;
    std::vector<FileEntry> to_rename;
    for (auto it = files_.begin(), it_last = files_.end(); it != it_last;) {
      const auto& path = it->first;
      if (path.substr(0, from_prefix.size()) == from_prefix) {
        std::string to_path = to_prefix + path.substr(from_prefix.size());
        to_rename.emplace_back(std::make_pair<std::string, Entry>(
            std::move(to_path), std::move(it->second)));
        it = files_.erase(it);
      } else {
        ++it;
      }
    }
    // Now add the new renamed file entries to the map.
    for (auto& pair : to_rename) {
      files_.emplace(pair.first, std::move(pair.second));
    }
    // And done!
    return true;
  }

  auto file_it = files_.find(from);
  if (file_it == files_.end()) {
    errno = ENOENT;
    return false;
  }

  // The source is a file, check that the destination is not a directory.
  if (std::find(dirs.begin(), dirs.end(), to) != dirs.end()) {
    errno = EISDIR;
    return -1;
  }

  // Overwrite destination file in map.
  files_[to] = std::move(file_it->second);
  files_.erase(file_it);
  return true;
}

FILE* VirtualFileSystem::OpenFile(const std::string& path, const char* mode) {
  // Is write/append support needed?
  bool needs_writable_path = strpbrk(mode, "aw") != nullptr;

  auto it = files_.find(path);
  if (it == files_.end()) {
    // Cannot read missing file.
    if (!needs_writable_path) {
      errno = ENOENT;
      return nullptr;
    }
  }

  Entry& entry = files_[path];

  if (needs_writable_path && entry.writable_path.empty()) {
    // Create a new temporary file to back the content of this file.
    entry.writable_path = GetTemporaryFilePath();
    if (!entry.contents.empty()) {
      CreateWritableTempFile(entry.writable_path, entry.contents);
      entry.contents.clear();
    }
  }

  if (!entry.writable_path.empty()) {
    return fopen(entry.writable_path.c_str(), mode);
  } else {
    // Use fmemopen() to read the data from memory directly.
    const std::string& data = it->second.contents;
    return fmemopen(const_cast<char*>(data.data()), data.size(), mode);
  }
}

void ScopedTempDir::CreateAndEnter(const string& name) {
  // First change into the system temp dir and save it for cleanup.
  start_dir_ = GetSystemTempDir();
  if (start_dir_.empty())
    Fatal("couldn't get system temp dir");
  if (chdir(start_dir_.c_str()) < 0)
    Fatal("chdir: %s", strerror(errno));

  // Create a temporary subdirectory of that.
  char name_template[1024];
  strcpy(name_template, name.c_str());
  strcat(name_template, "-XXXXXX");
  char* tempname = mkdtemp(name_template);
  if (!tempname)
    Fatal("mkdtemp: %s", strerror(errno));
  temp_dir_name_ = tempname;

  // chdir into the new temporary directory.
  if (chdir(temp_dir_name_.c_str()) < 0)
    Fatal("chdir: %s", strerror(errno));
}

void ScopedTempDir::Cleanup() {
  if (temp_dir_name_.empty())
    return;  // Something went wrong earlier.

  // Move out of the directory we're about to clobber.
  if (chdir(start_dir_.c_str()) < 0)
    Fatal("chdir: %s", strerror(errno));

#ifdef _WIN32
  string command = "rmdir /s /q " + temp_dir_name_;
#else
  string command = "rm -rf " + temp_dir_name_;
#endif
  if (system(command.c_str()) < 0)
    Fatal("system: %s", strerror(errno));

  temp_dir_name_.clear();
}

ScopedFilePath::ScopedFilePath(ScopedFilePath&& other) noexcept
    : path_(std::move(other.path_)), released_(other.released_) {
  other.released_ = true;
}

/// It would be nice to use '= default' here instead but some old compilers
/// such as GCC from Ubuntu 16.06 will not compile it with "noexcept", so just
/// write it manually.
ScopedFilePath& ScopedFilePath::operator=(ScopedFilePath&& other) noexcept {
  if (this != &other) {
    this->~ScopedFilePath();
    new (this) ScopedFilePath(std::move(other));
  }
  return *this;
}

ScopedFilePath::~ScopedFilePath() {
  if (!released_) {
    unlink(path_.c_str());
  }
}

void ScopedFilePath::Release() {
  released_ = true;
}
