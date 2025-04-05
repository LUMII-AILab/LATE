#include <optional>
#include <vector>
#include <algorithm>
#include <tuple>
#include <filesystem>
#include <fstream>

#include <sqlite3.h>

#include "sha256.hpp"
#include "log.hpp"
#include "sqlite/sqlite.hpp"
#include "storage.hpp"


namespace fs = std::filesystem;

bool sqlite_initialized = false;

// revision:
// documents: add created TEXT and modified TEXT fields to documents table
// shared_document_writers(document_id TEXT, timestamp TEXT (when the access was given), hint TEXT (something for the owner to recognize the access token), token TEXT)

class StorageImpl {
    logger log;
    SQLite db;
    SQLite::Statement insertDocumentStmt;
    SQLite::Statement updateDocumentStmt;
    SQLite::Statement selectDocumentDataStmt;
    SQLite::Statement selectDocumentDataAndTypeStmt;
    SQLite::Statement selectDocumentKeyStmt;
    SQLite::Statement checkDocumentWriterStmt;
    SQLite::Statement deleteDocumentStmt;
    SQLite::Statement insertSharedDocumentWriterStmt;
    SQLite::Statement selectSharedDocumentWritersStmt;
    SQLite::Statement deleteSharedDocumentWriterStmt;
    SQLite::Statement updateSharedDocumentWriterHintStmt;
    fs::path file_storage_path;

public:
    StorageImpl(const StorageImpl&) = delete;
    StorageImpl& operator=(const StorageImpl&) = delete;

    StorageImpl(StorageImpl&&) noexcept = default;
    StorageImpl& operator=(StorageImpl&&) noexcept = default;

    StorageImpl(const std::string& path, const std::string& file_storage_path = "files") : log(new_logger("storage")) {
        try {
            if (!sqlite_initialized) {
                if (!db.isThreadsafe()) {
                    log.error("SQLite not built with threading support");
                    // TODO: throw
                    return;
                }
                auto error = db.configureSerialized();
                if (error) {
                    // throw std::move(*error);
                    return;
                }
                sqlite_initialized = true;
            }

            db.open(path, SQLite::OpenFlags::ReadWrite | SQLite::OpenFlags::Create | SQLite::OpenFlags::FullMutex);

            // create and migrate database schema

            db.exec("CREATE TABLE IF NOT EXISTS documents (id TEXT PRIMARY KEY,"
                " key TEXT, type TEXT, created TEXT DEFAULT CURRENT_TIMESTAMP, modified TEXT DEFAULT CURRENT_TIMESTAMP, data TEXT);");

            if (auto opt = get_columns("documents"); opt) {
                auto& columns = opt.value();

                if (std::find(columns.begin(), columns.end(), "type") == columns.end()) {
                    // type column is missing, add it
                    log.info("upgrading database: documents(type)");

                    db.exec("ALTER TABLE documents ADD COLUMN type TEXT;");
                }

                if (std::find(columns.begin(), columns.end(), "key") == columns.end()) {
                    // key column is missing, add it
                    log.info("upgrading database: documents(key)");

                    db.exec("ALTER TABLE documents ADD COLUMN key TEXT;");
                }

                if (std::find(columns.begin(), columns.end(), "created") == columns.end()) {
                    // key column is missing, add it
                    log.info("upgrading database: documents(created)");

                    db.exec(R"SQLITE(
                            -- Add the new column without default
                            ALTER TABLE documents ADD COLUMN created TEXT;
                            -- ALTER TABLE documents ADD COLUMN created TEXT DEFAULT CURRENT_TIMESTAMP;

                            -- Update existing rows to set the current timestamp
                            UPDATE documents SET created = CURRENT_TIMESTAMP WHERE created IS NULL;

                            -- Create a trigger to automatically set the current timestamp for new rows
                            CREATE TRIGGER set_document_created_timestamp
                            AFTER INSERT ON documents
                            FOR EACH ROW
                            WHEN (NEW.created IS NULL)
                            BEGIN
                                UPDATE documents SET created = CURRENT_TIMESTAMP WHERE rowid = NEW.rowid;
                            END;
                            )SQLITE");
                }

                if (std::find(columns.begin(), columns.end(), "modified") == columns.end()) {
                    // key column is missing, add it
                    log.info("upgrading database: documents(modified)");

                    db.exec(R"SQLITE(
                            -- Add the new column without default
                            ALTER TABLE documents ADD COLUMN modified TEXT;
                            -- ALTER TABLE documents ADD COLUMN modified TEXT DEFAULT CURRENT_TIMESTAMP;

                            -- Update existing rows to set the current timestamp
                            UPDATE documents SET modified = created WHERE modified IS NULL;
                            )SQLITE");
                }
            }

            db.exec("CREATE INDEX IF NOT EXISTS documents_index ON documents (id);");

            db.exec(R"SQLITE(
                CREATE TRIGGER IF NOT EXISTS update_documents_modified
                AFTER UPDATE ON documents
                FOR EACH ROW
                WHEN NEW.modified != OLD.modified
                BEGIN
                    UPDATE documents SET modified = CURRENT_TIMESTAMP WHERE id = OLD.id;
                END;
                )SQLITE");

            db.exec("CREATE TABLE IF NOT EXISTS shared_document_writers (document_id TEXT, token TEXT, timestamp TEXT DEFAULT CURRENT_TIMESTAMP, hint TEXT);");

            db.exec("CREATE INDEX IF NOT EXISTS shared_document_writers_index_document_id ON shared_document_writers (document_id);");

            db.exec("CREATE INDEX IF NOT EXISTS shared_document_writers_index_token ON shared_document_writers (document_id, token);");

            // prepare statements

            insertDocumentStmt = db.prepare("INSERT OR REPLACE INTO documents (id, type, key, data) VALUES (?, ?, ?, ?);", true);

            updateDocumentStmt = db.prepare("UPDATE documents SET data = :data WHERE id = :id;", true);

            selectDocumentDataStmt = db.prepare("SELECT data FROM documents WHERE id = ?;", true);

            selectDocumentDataAndTypeStmt = db.prepare("SELECT data, type FROM documents WHERE id = ?;", true);

            selectDocumentKeyStmt = db.prepare("SELECT key FROM documents WHERE id = ?;", true);

            checkDocumentWriterStmt = db.prepare("SELECT count(*) FROM shared_document_writers WHERE document_id = ? AND token = ?;", true);

            deleteDocumentStmt = db.prepare("DELETE FROM documents WHERE id = ? AND coalesce(key,'') = ?;", true);

            insertSharedDocumentWriterStmt = db.prepare("INSERT OR REPLACE INTO shared_document_writers (document_id, token, hint) VALUES (?, ?, ?);", true);

            selectSharedDocumentWritersStmt = db.prepare("SELECT token, timestamp, hint FROM shared_document_writers WHERE document_id = ?;", true);

            deleteSharedDocumentWriterStmt = db.prepare("DELETE FROM shared_document_writers WHERE document_id = ? AND token = ?;", true);

            updateSharedDocumentWriterHintStmt = db.prepare("UPDATE shared_document_writers SET hint = ? WHERE document_id = ? AND token = ?;", true);

        } catch (const SQLite::SyntaxError& ex) {
            log.error("storage error: {} at position {} in SQL: {}", ex.what(), ex.offset, ex.sql);
        } catch (const SQLite::Error& ex) {
            log.error("storage error: {}", ex.what());
        } catch (const std::exception& ex) {
            log.error("storage error: {}", ex.what());
        }

        // resolve file storage path
        try {
            fs::path base_dir = fs::path(path).parent_path();

            auto& p = file_storage_path;

            // check for absolute or relative path
            if ((p.size() >= 1 && p.compare(0, 1, "/") == 0) ||
                (p.size() >= 2 && p.compare(0, 2, "./") == 0) ||
                (p.size() >= 3 && p.compare(0, 3, "../") == 0))
                this->file_storage_path = file_storage_path;
            else
                this->file_storage_path = base_dir / file_storage_path;

            std::error_code ec; // to avoid exceptions
            fs::create_directories(this->file_storage_path, ec);

            if (ec)
                log.warn("filed to create directory {} for storing files: {}", this->file_storage_path.string(), ec.message());

            fs::path resolved_path = fs::canonical(this->file_storage_path, ec);

            if (ec)
                throw std::invalid_argument(std::string("error resolving path ") + this->file_storage_path.string() + ": " + ec.message());

            if (!fs::is_directory(resolved_path, ec)) {
                if (ec)
                    throw std::invalid_argument(std::string("error checking for directory at path ") + this->file_storage_path.string() + ": " + ec.message());
                throw std::invalid_argument(std::string("error: path ") + this->file_storage_path.string() + " is not a directory");
            }

            log.info("file storage path: {}", this->file_storage_path.string());

        } catch (const std::exception& ex) {
            this->file_storage_path.clear();
            log.error("storage error: error resolving file storage path: {}", ex.what());
        }
    }

    ~StorageImpl() {
    }

    std::optional<std::vector<std::string>> get_columns(const std::string& table_name) {
        try {
            auto stmt = db.prepare("PRAGMA table_info(" + table_name + ");");

            std::vector<std::string> columns;

            while (stmt.step()) {
                std::string name = stmt["name"].getString();
                columns.emplace_back(name);
            }

            return columns;
        // } catch (const std::exception& ex) {
        } catch (...) {
        }
        return std::nullopt;
    }

    std::optional<bool> update(const std::string& id, const std::string& data, const std::string& accessToken = "") {

        log.debug("updating document with id = {}", id);

        if (auto r = get_document_owner_key(id); r) {
            auto ownerKey = r.value();

            std::string expectedAccessToken = get_token(id, ownerKey);

            if (expectedAccessToken != accessToken) {
                return false;
            }

            try {
                auto& stmt = updateDocumentStmt;

                stmt.reuse();

                stmt.param(":id") = id;
                stmt.param(":data") = data;

                stmt.exec();

                return true;

            } catch (const std::exception& e) {
                log.error("storage error: error updating document with id {}: {}", id, e.what());
                // return false;
            }
        }

        return std::nullopt;  // internal error
    }

    bool put(const std::string& id, const std::string& data, const std::string& key = "", const std::string& type = "json") {

        log.debug("storing document with id = {}", id);

        try {
            auto& stmt = insertDocumentStmt;

            stmt.reuse();

            stmt.bindAll(id, type, key, data);

            stmt.exec();

            return true;

        } catch (const std::exception& e) {
            log.error("storage error: error strogin document: {}", e.what());
        }

        return false;
    }

    std::string get_token(const std::string& id, const std::string& key) {
        SHA256 sha256;
        sha256.update(id + key);
        return sha256.final();
    }

    std::optional<std::vector<std::tuple<std::string/*token*/, std::string/*timestamp*/, std::string/*hint*/>>> get_document_writers(const std::string& id, const std::string& ownerKey) {

        if (auto r = get_document_owner_key(id); r) {
            auto ownerKey = r.value();

            try {
                auto& stmt = selectSharedDocumentWritersStmt;

                stmt.reuse();

                stmt.bindAll(id);

                std::vector<std::tuple<std::string, std::string, std::string>> result;

                while (stmt.step()) {
                    std::string token = stmt["token"];
                    std::string timestamp = stmt["timestamp"];
                    std::string hint = stmt["hint"];

                    result.emplace_back(std::make_tuple(token, timestamp, hint));
                }

                return result;

            } catch (const std::exception& e) {
                log.error("storage error: error getting document writers: {}", e.what());
            }
        }
        return std::nullopt;
    }

    std::optional<bool> remove_document_writer(const std::string& id, const std::string& token, const std::string& ownerKey) {

        if (auto r = get_document_owner_key(id); r) {
            if (ownerKey != r.value()) {
                return false;
            }

            try {
                auto& stmt = deleteSharedDocumentWriterStmt;

                stmt.reuse();

                stmt.bindAll(id, token);

                stmt.step();

                return true;

            } catch (const std::exception& e) {
                log.error("storage error: error removing shared document writer {} for document with id {}: {}", token, id, e.what());
            }
        }

        return std::nullopt;
    }

    std::optional<bool> update_document_writer_hint(const std::string& id, const std::string& token, const std::string& ownerKey, const std::string& hint) {

        if (auto r = get_document_owner_key(id); r) {
            if (ownerKey != r.value()) {
                return false;
            }

            try {
                auto& stmt = updateSharedDocumentWriterHintStmt;

                stmt.reuse();

                stmt.bindAll(hint, id, token);

                stmt.step();

                return true;

            } catch (const std::exception& e) {
                log.error("storage error: error removing shared document writer {} for document with id {}: {}", token, id, e.what());
            }
        }

        return std::nullopt;
    }

    // NOTE: because this will be called not only by the owner
    std::optional<bool> add_writer_key(const std::string& id, const std::string& accessToken, const std::string& key, const std::string& hint = "") {
        // access_token = get_token(id, owner_key)

        // 1. get document owner
        // 2. calc access_token
        // 3. compare access_token
        // 4. if valid, add key, possibly return writer_token

        if (auto r = get_document_owner_key(id); r) {
            auto ownerKey = r.value();

            std::string expectedAccessToken = get_token(id, ownerKey);

            if (accessToken != expectedAccessToken) {
                return false;
            }

            try {
                auto& stmt = insertSharedDocumentWriterStmt;

                stmt.reuse();

                auto token = get_token(id, key);

                stmt.bindAll(id, token, hint);

                stmt.step();

                return true;

            } catch (const std::exception& e) {
                log.error("error inserting shared document writer for document with id {}: {}", id, e.what());
                return std::nullopt;
            }
        }

        return std::nullopt;
    }

    std::optional<bool> check_writer_key(const std::string& id, const std::string& key) {
        log.debug("checking writer key for item with id = {}", id);

        auto token = get_token(id, key);

        int result = 0;

        try {
            auto& stmt = checkDocumentWriterStmt;

            stmt.reuse();

            stmt.bindAll(id, token);

            if (stmt.step()) {
                result = stmt.getInt(0);
            } else {
                return std::nullopt;
            }

        } catch (...) {
            return std::nullopt;
        }

        return result > 0;
    }

    std::optional<bool> check_owner_key(const std::string& id, const std::string& key) {
        return check_key(id, key);
    }

    // verifies owner
    std::optional<bool> check_key(const std::string& id, const std::string& key) {
        log.debug("checking owner key for item with id = {}", id);

        if (auto r = get_document_owner_key(id); r) {
            auto expectedKey = r.value();
            return expectedKey == key;
        }

        return false;
    }

    std::optional<std::pair<std::string, std::string>> get(const std::string& id) {
        log.debug("getting document with id = {}", id);

        std::string type, data;

        try {
            auto& stmt = selectDocumentDataAndTypeStmt;

            stmt.reuse();

            stmt.bindAll(id);

            if (stmt.step()) {
                type = static_cast<std::string>(stmt["type"]);
                data = static_cast<std::string>(stmt["data"]);
            } else {
                return std::nullopt;
            }

        } catch (const std::exception& e) {
            log.error("error retrieving document with id {}: {}", id, e.what());
            return std::nullopt;
        } catch (...) {
            return std::nullopt;
        }

        return std::make_pair(type, data);
    }

    std::optional<bool> remove(const std::string& id, const std::string& key) {
        log.debug("removing document with id = {}", id);

        try {
            auto& stmt = deleteDocumentStmt;

            stmt.reuse();

            stmt.bindAll(id, key);

            if (auto r = stmt.step(); !r)
                return false;

            return remove_files(id);

        } catch (const std::exception& e) {
            log.error("error retrieving document with id {}: {}", id, e.what());
        } catch (...) {
        }

        return std::nullopt;
    }

    bool put_file(const std::string& id, const void* data, size_t size, const std::string& extension = ".wav") {
        if (file_storage_path.empty())
            return false;

        std::string path = file_storage_path / (id + extension);

        std::ofstream file(path, std::ios::binary);
        if (!file) {
            log.error("filed to open file: {}", path);
            return false;
        }

        file.write(reinterpret_cast<const char*>(data), size);
        return file.good();
    }

    std::optional<SharedBuffer<void>> get_file(const std::string& id, const std::string& extension = ".wav") {
        if (file_storage_path.empty())
            return std::nullopt;

        std::string path = file_storage_path / (id + extension);

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            log.error("filed to open file: {}", path);
            return std::nullopt;
        }

        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        auto deleter = [](void* ptr) { std::free(ptr); };
        std::shared_ptr<void> buffer(std::malloc(size), deleter);

        if (!buffer) {
            log.error("filed to allocate memory for file: {}", path);
            return std::nullopt;
        }

        file.read(static_cast<char*>(buffer.get()), size);

        if (!file) {
            log.error("error reading file: {}", path);
            return std::nullopt;
        }

        return SharedBuffer<void>(buffer, size);

    }

    bool remove_file(const std::string& id, const std::string& extension = ".wav") {
        if (file_storage_path.empty())
            return false;

        std::string path = file_storage_path / (id + extension);

        std::error_code ec;
        if (fs::remove(path, ec))
            return true;
        else if (ec)
            log.error("error removing file {}: {}", path, ec.message());
        return false;
    }

    bool remove_files(const std::string& id) {
        std::error_code ec;

        std::string basename = id;

        fs::path base_dir = fs::canonical(file_storage_path, ec);
        if (ec) {
            log.error("unable to resolve file storage path {}: {}", file_storage_path.string(), ec.message());
            return false;
        }

        if (!fs::exists(base_dir, ec) || !fs::is_directory(base_dir, ec)) {
            log.error("error: file storage path is not a valid directory {}: {}", base_dir.string(), ec.message());
            return false;
        }

        bool ok = false;

        for (const auto& entry : fs::directory_iterator(base_dir, ec)) {
            if (ec) {
                log.error("error removing files for {}: error accessing file storage path: {}", id, ec.message());
                return false;
            }

            if (fs::is_regular_file(entry.path(), ec) && entry.path().stem() == basename) {
                log.debug("removing file {} for {}", entry.path().string(), id);
                fs::remove(entry.path(), ec);
                if (ec) {
                    log.error("error removing file {}: {}", entry.path().string(), ec.message());
                    ok = false;
                }
            }
        }

        return ok;
    }

private:
    std::optional<std::string> get_document_owner_key(const std::string& id) {

        try {
            auto& stmt = selectDocumentKeyStmt;

            stmt.reuse();

            stmt.bindAll(id);

            if (stmt.step()) {
                return static_cast<std::string>(stmt[0]);
            }

        } catch (const std::exception& e) {
            log.error("error retrieving owner key for document with id {}: {}", id, e.what());
        }

        return std::nullopt;
    }
};


Storage::Storage(const std::string& path, const std::string& file_storage_path) : impl(std::make_unique<StorageImpl>(path, file_storage_path)) {
}

Storage::~Storage() {
}

bool Storage::put(const std::string& id, const std::string& data, const std::string& key) {
    return impl->put(id, data, key);
}

bool Storage::put_file(const std::string& id, const void* data, size_t size, const std::string& extension) {
    return impl->put_file(id, data, size, extension);
}

std::optional<SharedBuffer<void>> Storage::get_file(const std::string& id, const std::string& extension) {
    return impl->get_file(id, extension);
}

bool Storage::remove_file(const std::string& id, const std::string& extension) {
    return impl->remove_file(id, extension);
}

bool Storage::remove_files(const std::string& id) {
    return impl->remove_files(id);
}

std::optional<std::pair<std::string, std::string>> Storage::get(const std::string& id) {
    return impl->get(id);
}

std::optional<bool> Storage::remove(const std::string& id, const std::string& key) {
    return impl->remove(id, key);
}

std::optional<bool> Storage::check_key(const std::string& id, const std::string& key) {
    return impl->check_key(id, key);
}

std::optional<bool> Storage::check_owner_key(const std::string& id, const std::string& key) {
    return impl->check_owner_key(id, key);
}

std::optional<std::vector<std::tuple<std::string/*token*/, std::string/*timestamp*/, std::string/*hint*/>>>
    Storage::get_document_writers(const std::string& id, const std::string& ownerKey) {
    return impl->get_document_writers(id, ownerKey);
}

std::optional<bool> Storage::remove_document_writer(const std::string& id, const std::string& token, const std::string& ownerKey) {
    return impl->remove_document_writer(id, token, ownerKey);
}

std::optional<bool> Storage::update_document_writer_hint(const std::string& id, const std::string& token, const std::string& ownerKey, const std::string& hint) {
    return impl->update_document_writer_hint(id, token, ownerKey, hint);
}

std::optional<bool> Storage::add_writer_key(const std::string& id, const std::string& accessToken, const std::string& key, const std::string& hint) {
    return impl->add_writer_key(id, accessToken, key, hint);
}

std::optional<bool> Storage::check_writer_key(const std::string& id, const std::string& key) {
    return impl->check_writer_key(id, key);
}
