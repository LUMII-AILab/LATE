#pragma once

#include <string>
#include <optional>
#include <memory>
#include <utility>

#include "util.hpp"

class StorageImpl;

class Storage {
public:
    Storage(const std::string& path, const std::string& file_storage_path = "files");

    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    Storage(Storage&&) noexcept = default;
    Storage& operator=(Storage&&) noexcept = default;

    ~Storage();

    bool put(const std::string& id, const std::string& data, const std::string& key = "");
    std::optional<std::pair<std::string, std::string>> get(const std::string& id);

    bool put_file(const std::string& id, const void* data, size_t size, const std::string& extension = ".wav");
    std::optional<SharedBuffer<void>> get_file(const std::string& id, const std::string& extension = ".wav");
    bool remove_file(const std::string& id, const std::string& extension = ".wav");
    bool remove_files(const std::string& id);

    std::optional<bool> update(const std::string& id, const std::string& data, const std::string& accessToken = "");

    std::optional<bool> remove(const std::string& id, const std::string& key);
    std::optional<bool> check_key(const std::string& id, const std::string& key);
    std::optional<bool> check_owner_key(const std::string& id, const std::string& key);

    std::optional<std::vector<std::tuple<std::string/*token*/, std::string/*timestamp*/, std::string/*hint*/>>> get_document_writers(const std::string& id, const std::string& ownerKey);
    std::optional<bool> remove_document_writer(const std::string& id, const std::string& token, const std::string& ownerKey);
    std::optional<bool> update_document_writer_hint(const std::string& id, const std::string& token, const std::string& ownerKey, const std::string& hint);
    std::optional<bool> add_writer_key(const std::string& id, const std::string& accessToken, const std::string& key, const std::string& hint = "");
    std::optional<bool> check_writer_key(const std::string& id, const std::string& key);

private:
    std::unique_ptr<StorageImpl> impl;
};
