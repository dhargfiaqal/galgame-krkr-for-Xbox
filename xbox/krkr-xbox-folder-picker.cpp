#include <ppltasks.h>
#include <collection.h>
#include <windows.h>
#include <string>
#include <algorithm>
#include <cctype>
#include <cwctype>

#include "miniz.h"

using namespace concurrency;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;
using namespace Windows::Storage::Pickers;
using namespace Windows::UI::Core;

namespace {

StorageFolder^ WaitForFolder(IAsyncOperation<StorageFolder^>^ operation) {
    StorageFolder^ result = nullptr;
    bool completed = false;
    create_task(operation).then([&](StorageFolder^ folder) {
        result = folder;
        completed = true;
    }, task_continuation_context::use_current());

    CoreWindow^ window = CoreWindow::GetForCurrentThread();
    if (window == nullptr) {
        try {
            return create_task(operation).get();
        } catch (...) {
            return nullptr;
        }
    }
    while (!completed) {
        window->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    }
    return result;
}

StorageFolder^ GetSavedFolder() {
    auto values = ApplicationData::Current->LocalSettings->Values;
    const wchar_t *key = L"lastGameFolderToken";
    if (!values->HasKey(key)) {
        return nullptr;
    }

    auto token = safe_cast<String^>(values->Lookup(key));
    if (token == nullptr || token->IsEmpty() ||
        !StorageApplicationPermissions::FutureAccessList->ContainsItem(token)) {
        return nullptr;
    }
    return WaitForFolder(StorageApplicationPermissions::FutureAccessList->GetFolderAsync(token));
}

StorageFolder^ PickFolder() {
    FolderPicker^ picker = ref new FolderPicker();
    picker->SuggestedStartLocation = PickerLocationId::Desktop;
    picker->FileTypeFilter->Append(L"*");
    return WaitForFolder(picker->PickSingleFolderAsync());
}

StorageFile^ WaitForFile(IAsyncOperation<StorageFile^>^ operation) {
    StorageFile^ result = nullptr;
    bool completed = false;
    create_task(operation).then([&](StorageFile^ file) {
        result = file;
        completed = true;
    }, task_continuation_context::use_current());
    CoreWindow^ window = CoreWindow::GetForCurrentThread();
    if (window == nullptr) {
        try { return create_task(operation).get(); } catch (...) { return nullptr; }
    }
    while (!completed) {
        window->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    }
    return result;
}

IVectorView<StorageFile^>^ WaitForFiles(IAsyncOperation<IVectorView<StorageFile^>^>^ operation) {
    IVectorView<StorageFile^>^ result = nullptr;
    bool completed = false;
    create_task(operation).then([&](IVectorView<StorageFile^>^ files) {
        result = files;
        completed = true;
    }, task_continuation_context::use_current());
    CoreWindow^ window = CoreWindow::GetForCurrentThread();
    if (window == nullptr) {
        try { return create_task(operation).get(); } catch (...) { return nullptr; }
    }
    while (!completed) window->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    return result;
}

IVectorView<StorageFolder^>^ WaitForFolders(IAsyncOperation<IVectorView<StorageFolder^>^>^ operation) {
    IVectorView<StorageFolder^>^ result = nullptr;
    bool completed = false;
    create_task(operation).then([&](IVectorView<StorageFolder^>^ folders) {
        result = folders;
        completed = true;
    }, task_continuation_context::use_current());
    CoreWindow^ window = CoreWindow::GetForCurrentThread();
    if (window == nullptr) {
        try { return create_task(operation).get(); } catch (...) { return nullptr; }
    }
    while (!completed) window->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    return result;
}

StorageFolder^ GetOrCreateFolder(StorageFolder^ parent, const wchar_t *name) {
    return WaitForFolder(parent->CreateFolderAsync(ref new String(name), CreationCollisionOption::OpenIfExists));
}

std::string ToUtf8(String^ value);

StorageFile^ FindZip(StorageFolder^ folder, int depth) {
    auto files = WaitForFiles(folder->GetFilesAsync());
    if (files != nullptr) {
        for (unsigned int i = 0; i < files->Size; ++i) {
            auto name = files->GetAt(i)->Name;
            std::wstring lower(name->Data());
            std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
            if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, L".zip") == 0) {
                return files->GetAt(i);
            }
        }
    }
    if (depth <= 0) return nullptr;
    auto folders = WaitForFolders(folder->GetFoldersAsync());
    if (folders != nullptr) {
        for (unsigned int i = 0; i < folders->Size; ++i) {
            auto found = FindZip(folders->GetAt(i), depth - 1);
            if (found != nullptr) return found;
        }
    }
    return nullptr;
}

std::string BaseName(const std::string &path) {
    auto slash = path.find_last_of("/\\");
    auto name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    auto dot = name.find_last_of('.');
    if (dot != std::string::npos) name.resize(dot);
    for (auto &ch : name) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') ch = '_';
    }
    return name.empty() ? "ImportedGame" : name;
}

std::wstring ToWide(const std::string &value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    std::wstring result(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &result[0], size);
    return result;
}

void EnsureDirectory(const std::string &path) {
    auto wide = ToWide(path);
    for (size_t i = 3; i < wide.size(); ++i) {
        if (wide[i] == L'\\') {
            CreateDirectoryW(wide.substr(0, i).c_str(), nullptr);
        }
    }
    CreateDirectoryW(wide.c_str(), nullptr);
}

std::string FindGameFolder(StorageFolder^ folder, int depth) {
    auto files = WaitForFiles(folder->GetFilesAsync());
    if (files != nullptr) {
        for (unsigned int i = 0; i < files->Size; ++i) {
            std::wstring name(files->GetAt(i)->Name->Data());
            std::transform(name.begin(), name.end(), name.begin(), towlower);
            if (name == L"startup.tjs" || (name.size() >= 4 && name.compare(name.size() - 4, 4, L".xp3") == 0)) {
                return ToUtf8(folder->Path);
            }
        }
    }
    if (depth <= 0) return {};
    auto folders = WaitForFolders(folder->GetFoldersAsync());
    if (folders != nullptr) {
        for (unsigned int i = 0; i < folders->Size; ++i) {
            auto found = FindGameFolder(folders->GetAt(i), depth - 1);
            if (!found.empty()) return found;
        }
    }
    return {};
}

std::string ExtractZip(StorageFile^ zipFile) {
    auto local = ApplicationData::Current->LocalFolder;
    auto imports = GetOrCreateFolder(local, L"Imports");
    auto games = GetOrCreateFolder(local, L"Games");
    auto copied = WaitForFile(zipFile->CopyAsync(imports, zipFile->Name, NameCollisionOption::ReplaceExisting));
    if (copied == nullptr) return {};

    std::string source = ToUtf8(copied->Path);
    std::string destination = ToUtf8(games->Path) + "\\" + BaseName(ToUtf8(zipFile->Name));
    EnsureDirectory(destination);

    mz_zip_archive archive{};
    if (!mz_zip_reader_init_file(&archive, source.c_str(), 0)) return {};
    mz_uint count = mz_zip_reader_get_num_files(&archive);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&archive, i, &stat)) continue;
        std::string name = stat.m_filename;
        std::replace(name.begin(), name.end(), '/', '\\');
        if (name.find("..\\") != std::string::npos || (!name.empty() && name[0] == '\\')) continue;
        std::string output = destination + "\\" + name;
        auto slash = output.find_last_of('\\');
        if (slash != std::string::npos) {
            std::string directory = output.substr(0, slash);
            EnsureDirectory(directory);
        }
        if (stat.m_is_directory) {
            EnsureDirectory(output);
        } else if (!mz_zip_reader_extract_to_file(&archive, i, output.c_str(), 0)) {
            mz_zip_reader_end(&archive);
            return {};
        }
    }
    mz_zip_reader_end(&archive);
    auto extracted = WaitForFolder(StorageFolder::GetFolderFromPathAsync(ref new String(ToWide(destination).c_str())));
    auto gameFolder = extracted == nullptr ? std::string{} : FindGameFolder(extracted, 3);
    return gameFolder.empty() ? destination : gameFolder;
}

std::string ToUtf8(String^ value) {
    if (value == nullptr || value->IsEmpty()) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, value->Data(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value->Data(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

} // namespace

extern "C" const char *krkr_xbox_pick_game_folder() {
    static std::string selectedPath;
    try {
        StorageFolder^ folder = GetSavedFolder();
        if (folder == nullptr) {
            folder = PickFolder();
            if (folder == nullptr) return nullptr;
            auto zip = FindZip(folder, 2);
            if (zip != nullptr) {
                auto imported = ExtractZip(zip);
                if (!imported.empty()) {
                    selectedPath = imported;
                    return selectedPath.c_str();
                }
            }
            auto token = StorageApplicationPermissions::FutureAccessList->Add(folder);
            ApplicationData::Current->LocalSettings->Values->Insert(
                L"lastGameFolderToken", token);
        }
        selectedPath = ToUtf8(folder->Path);
        return selectedPath.empty() ? nullptr : selectedPath.c_str();
    } catch (Exception^) {
        return nullptr;
    }
}
