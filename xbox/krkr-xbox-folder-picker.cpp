#include <ppltasks.h>
#include <collection.h>
#include <windows.h>
#include <string>

using namespace concurrency;
using namespace Platform;
using namespace Windows::Foundation;
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
            if (folder == nullptr) {
                return nullptr;
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
