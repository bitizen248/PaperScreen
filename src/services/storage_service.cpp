#include "services/storage_service.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* kRoot = "/paperscreen";

constexpr const char* kRequiredDirectories[] = {
    "/paperscreen",
    "/paperscreen/apps",
    "/paperscreen/cache",
    "/paperscreen/cache/trmnl",
    "/paperscreen/import",
    "/paperscreen/import/tasks",
    "/paperscreen/import/reader",
    "/paperscreen/import/settings",
    "/paperscreen/export",
    "/paperscreen/export/tasks",
    "/paperscreen/export/reader",
    "/paperscreen/export/diagnostics",
    "/paperscreen/logs",
    "/paperscreen/sync",
    "/paperscreen/sync/inbox",
    "/paperscreen/sync/outbox",
    "/paperscreen/sync/archive",
};

}  // namespace

namespace paper_screen {

StorageInfo StorageService::begin(BoardStorage& board_storage)
{
    Serial.println("[storage] begin");
    board_storage_ = &board_storage;
    const BoardStorageStatus board_status = board_storage_->begin();
    info_.card_type = board_status.card_type;
    info_.total_bytes = board_status.total_bytes;
    info_.used_bytes = board_status.used_bytes;

    switch (board_status.state) {
    case BoardStorageState::Mounted:
        info_.status = StorageStatus::Mounted;
        info_.status = ensure_layout() ? StorageStatus::Mounted : StorageStatus::IoError;
        break;
    case BoardStorageState::CardMissing:
        info_.status = StorageStatus::CardMissing;
        break;
    case BoardStorageState::MountFailed:
        info_.status = StorageStatus::MountFailed;
        break;
    case BoardStorageState::NotStarted:
        info_.status = StorageStatus::NotStarted;
        break;
    }

    Serial.printf("[storage] status=%s\n", storage_status_label(info_.status));
    return info_;
}

StorageInfo StorageService::info() const
{
    return info_;
}

bool StorageService::available() const
{
    return board_storage_ != nullptr && info_.status == StorageStatus::Mounted;
}

bool StorageService::ensure_app_directory(const char* app_id)
{
    if (app_id == nullptr || strchr(app_id, '/') != nullptr) {
        return false;
    }

    char path[96] = {};
    const int written = snprintf(path, sizeof(path), "%s/apps/%s", kRoot, app_id);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(path)) {
        return false;
    }
    return ensure_directory(path);
}

bool StorageService::write_text_atomic(const char* path, const char* text)
{
    if (text == nullptr) {
        return false;
    }
    return write_file_atomic(path, reinterpret_cast<const uint8_t*>(text), strlen(text));
}

bool StorageService::write_file_atomic(const char* path, const uint8_t* data, size_t size)
{
    if (!available() || !path_allowed(path) || data == nullptr) {
        return false;
    }

    char tmp_path[160] = {};
    const int written = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(tmp_path)) {
        return false;
    }

    if (board_storage_->exists(tmp_path)) {
        board_storage_->remove(tmp_path);
    }
    fs::File file = board_storage_->open(tmp_path, FILE_WRITE);
    if (!file) {
        return set_io_error();
    }

    const size_t bytes_written = size > 0 ? file.write(data, size) : 0;
    file.flush();
    file.close();
    if (bytes_written != size) {
        if (board_storage_->exists(tmp_path)) {
            board_storage_->remove(tmp_path);
        }
        return set_io_error();
    }

    if (board_storage_->exists(path)) {
        board_storage_->remove(path);
    }
    if (!board_storage_->rename(tmp_path, path)) {
        if (board_storage_->exists(tmp_path)) {
            board_storage_->remove(tmp_path);
        }
        return set_io_error();
    }

    return true;
}

bool StorageService::append_text(const char* path, const char* text)
{
    if (!available() || !path_allowed(path) || text == nullptr) {
        return false;
    }

    fs::File file = board_storage_->open(path, FILE_APPEND);
    if (!file) {
        return set_io_error();
    }

    const size_t text_len = strlen(text);
    const size_t bytes_written = file.write(reinterpret_cast<const uint8_t*>(text), text_len);
    file.flush();
    file.close();
    return bytes_written == text_len || set_io_error();
}

bool StorageService::file_size(const char* path, size_t* out_size) const
{
    if (out_size == nullptr) {
        return false;
    }
    *out_size = 0;
    if (!available() || !path_allowed(path)) {
        return false;
    }

    fs::File file = board_storage_->open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        return false;
    }

    *out_size = static_cast<size_t>(file.size());
    file.close();
    return true;
}

bool StorageService::read_file(const char* path, uint8_t* out, size_t capacity, size_t* out_size) const
{
    if (out_size != nullptr) {
        *out_size = 0;
    }
    if (!available() || !path_allowed(path) || out == nullptr) {
        return false;
    }

    fs::File file = board_storage_->open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        return false;
    }

    const size_t size = static_cast<size_t>(file.size());
    if (size > capacity) {
        file.close();
        return false;
    }

    const size_t bytes_read = file.read(out, size);
    file.close();
    if (out_size != nullptr) {
        *out_size = bytes_read;
    }
    return bytes_read == size;
}

bool StorageService::remove_file(const char* path)
{
    if (!available() || !path_allowed(path)) {
        return false;
    }
    if (!board_storage_->exists(path)) {
        return true;
    }
    return board_storage_->remove(path) || set_io_error();
}

bool StorageService::exists(const char* path) const
{
    if (!available() || !path_allowed(path)) {
        return false;
    }
    return board_storage_->exists(path);
}

bool StorageService::app_path(const char* app_id, const char* leaf, char* out, size_t out_size) const
{
    if (app_id == nullptr || leaf == nullptr || out == nullptr || out_size == 0) {
        return false;
    }
    if (strchr(app_id, '/') != nullptr || strstr(leaf, "..") != nullptr || leaf[0] == '/') {
        return false;
    }

    const int written = snprintf(out, out_size, "%s/apps/%s/%s", kRoot, app_id, leaf);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

bool StorageService::ensure_layout()
{
    for (const char* path : kRequiredDirectories) {
        if (!ensure_directory(path)) {
            return false;
        }
    }

    ensure_app_directory("trmnl");
    ensure_app_directory("tasks");
    ensure_app_directory("reader");
    ensure_app_directory("focus");
    ensure_app_directory("sync");
    ensure_app_directory("settings");
    return true;
}

bool StorageService::ensure_directory(const char* path)
{
    if (!available() || !path_allowed(path)) {
        return false;
    }
    if (!board_storage_->mkdir(path)) {
        return set_io_error();
    }
    return true;
}

bool StorageService::path_allowed(const char* path) const
{
    if (path == nullptr) {
        return false;
    }
    if (strncmp(path, kRoot, strlen(kRoot)) != 0) {
        return false;
    }
    if (path[strlen(kRoot)] != '\0' && path[strlen(kRoot)] != '/') {
        return false;
    }
    return strstr(path, "..") == nullptr;
}

bool StorageService::set_io_error()
{
    info_.status = StorageStatus::IoError;
    return false;
}

const char* storage_status_label(StorageStatus status)
{
    switch (status) {
    case StorageStatus::NotStarted:
        return "not started";
    case StorageStatus::Mounted:
        return "mounted";
    case StorageStatus::CardMissing:
        return "card missing";
    case StorageStatus::MountFailed:
        return "mount failed";
    case StorageStatus::IoError:
        return "io error";
    }
    return "unknown";
}

}  // namespace paper_screen
