#include <sqlite3.h>

#include "sqlite.hpp"


std::string to_string(SQLite::DataType type) {
    switch (type) {
        case SQLite::DataType::Integer:
            return "Integer";
        case SQLite::DataType::Float:
            return "Float";
        case SQLite::DataType::Text:
            return "Text";
        case SQLite::DataType::Blob:
            return "Blob";
        case SQLite::DataType::Null:
            return "Null";
        default:
            return "Unknown";
    }
}

void throwSQLiteError(sqlite3* db, const std::string& message, const std::string& sql = "") {
    int code = sqlite3_errcode(db);
    int extended_code = sqlite3_extended_errcode(db);
    std::string errmsg = sqlite3_errmsg(db);
    int offset = sql.empty() ? -1 : sqlite3_error_offset(db);
    if (offset > -1) {
        throw SQLite::SyntaxError(message, errmsg, code, extended_code, sql, offset);
    }
    switch (code) {
        case SQLITE_BUSY:
            throw SQLite::BusyError(message, errmsg, code, extended_code);
        case SQLITE_MISUSE:
            throw SQLite::MisuseError(message, errmsg, code, extended_code);
        default:
            throw SQLite::Error(message, errmsg, code, extended_code);
    }
}


bool SQLite::isThreadsafe() {
    return sqlite3_threadsafe() != 0;
}

std::unique_ptr<SQLite::Error> SQLite::configureSerialized() {
    int result = sqlite3_config(SQLITE_CONFIG_SERIALIZED);
    if (result != SQLITE_OK) {
        return std::make_unique<SQLite::Error>(
            "failed to configure SQLite for serialized threading mode",
            sqlite3_errstr(result),
            result,
            result
        );
    }
    return nullptr;
}

void SQLite::open(const std::string& dbName, OpenFlags flags) {
    int sqliteFlags = toSQLiteOpenFlags(flags);
    if (sqlite3_open_v2(dbName.c_str(), &db, sqliteFlags, nullptr) != SQLITE_OK) {
        throwSQLiteError(db, "failed to open database");
    }
}

void SQLite::close() {
    if (db) {
        if (sqlite3_close(db) != SQLITE_OK) {
            throwSQLiteError(db, "failed to close connection");
        }
        db = nullptr;
    }
}

SQLite::~SQLite() {
    close();
}

int SQLite::toSQLiteOpenFlags(OpenFlags flags) {
    int sqliteFlags = 0;

    if (static_cast<int>(flags) & static_cast<int>(OpenFlags::ReadOnly)) sqliteFlags |= SQLITE_OPEN_READONLY;
    if (static_cast<int>(flags) & static_cast<int>(OpenFlags::ReadWrite)) sqliteFlags |= SQLITE_OPEN_READWRITE;
    if (static_cast<int>(flags) & static_cast<int>(OpenFlags::Create)) sqliteFlags |= SQLITE_OPEN_CREATE;
    if (static_cast<int>(flags) & static_cast<int>(OpenFlags::URI)) sqliteFlags |= SQLITE_OPEN_URI;
    if (static_cast<int>(flags) & static_cast<int>(OpenFlags::Memory)) sqliteFlags |= SQLITE_OPEN_MEMORY;
    if (static_cast<int>(flags) & static_cast<int>(OpenFlags::NoMutex)) sqliteFlags |= SQLITE_OPEN_NOMUTEX;
    if (static_cast<int>(flags) & static_cast<int>(OpenFlags::FullMutex)) sqliteFlags |= SQLITE_OPEN_FULLMUTEX;
    if (static_cast<int>(flags) & static_cast<int>(OpenFlags::SharedCache)) sqliteFlags |= SQLITE_OPEN_SHAREDCACHE;
    if (static_cast<int>(flags) & static_cast<int>(OpenFlags::PrivateCache)) sqliteFlags |= SQLITE_OPEN_PRIVATECACHE;
    if (static_cast<int>(flags) & static_cast<int>(OpenFlags::NoFollow)) sqliteFlags |= SQLITE_OPEN_NOFOLLOW;

    return sqliteFlags;
}

SQLite::Statement::Statement(sqlite3* db, const std::string& sql, bool persistent) {
    int prepareFlags = persistent ? SQLITE_PREPARE_PERSISTENT : 0;
    if (sqlite3_prepare_v3(db, sql.c_str(), -1, prepareFlags, &stmt, nullptr) != SQLITE_OK) {
        throwSQLiteError(db, "failed to prepare statement", sql);
    }
    initializeColumnIndices();
}

SQLite::Statement::~Statement() {
    finalize();
}

SQLite::Statement::Statement(Statement&& other) noexcept : stmt(other.stmt), columnIndices(other.columnIndices) {
    other.stmt = nullptr;
}

SQLite::Statement& SQLite::Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        finalize();
        stmt = other.stmt;
        other.stmt = nullptr;

        columnIndices = std::move(other.columnIndices);
    }
    return *this;
}

void SQLite::Statement::finalize() {
    if (stmt) {
        if (sqlite3_finalize(stmt) != SQLITE_OK) {
            throwSQLiteError(sqlite3_db_handle(stmt), "failed to finalize statement");
        }
        stmt = nullptr;
    }
}

void SQLite::Statement::initializeColumnIndices() {
    int columnCount = sqlite3_column_count(stmt);
    for (int i = 0; i < columnCount; ++i) {
        const char* columnName = sqlite3_column_name(stmt, i);
        if (columnName) {
            columnIndices[columnName] = i;
        }
    }
}

int SQLite::Statement::getColumnIndex(const std::string& columnName) const {
    auto it = columnIndices.find(columnName);
    if (it != columnIndices.end()) {
        return it->second;
    }
    throw SQLite::OtherError("column not found: " + columnName);
}

int SQLite::Statement::getParamIndex(const std::string& paramName) const {
    ensure();
    int index = sqlite3_bind_parameter_index(stmt, paramName.c_str());
    if (index == 0) {
        throw SQLite::OtherError("parameter not found: " + paramName);
    }
    return index;
}

std::string SQLite::Statement::getParamName(int index) const {
    ensure();
    const char* name = sqlite3_bind_parameter_name(stmt, index);
    if (name) {
        return name;
    }
    return "";
}

void SQLite::Statement::bind(int index, int value) {
    ensure();
    if (sqlite3_bind_int(stmt, index, value) != SQLITE_OK) {
        throwSQLiteError(sqlite3_db_handle(stmt), "failed to bind int");
    }
}

void SQLite::Statement::bind(int index, int64_t value) {
    ensure();
    if (sqlite3_bind_int64(stmt, index, value) != SQLITE_OK) {
        throwSQLiteError(sqlite3_db_handle(stmt), "failed to bind int64");
    }
}

void SQLite::Statement::bind(int index, double value) {
    ensure();
    if (sqlite3_bind_double(stmt, index, value) != SQLITE_OK) {
        throwSQLiteError(sqlite3_db_handle(stmt), "failed to bind double");
    }
}

void SQLite::Statement::bind(int index, const std::string& value) {
    ensure();
    if (sqlite3_bind_text(stmt, index, value.c_str(), value.size(), SQLITE_TRANSIENT) != SQLITE_OK) {
        throwSQLiteError(sqlite3_db_handle(stmt), "failed to bind text");
    }
}

void SQLite::Statement::bind(int index, const Blob& blob) {
    ensure();
    if (sqlite3_bind_blob(stmt, index, blob.data, blob.size, SQLITE_TRANSIENT) != SQLITE_OK) {
        throwSQLiteError(sqlite3_db_handle(stmt), "failed to bind blob");
    }
}

int SQLite::Statement::columnCount() const {
    ensure();
    return sqlite3_column_count(stmt);
}

SQLite::DataType SQLite::Statement::getColumnType(int index) const {
    ensure();
    switch (sqlite3_column_type(stmt, index)) {
        case SQLITE_INTEGER: return DataType::Integer;
        case SQLITE_FLOAT: return DataType::Float;
        case SQLITE_TEXT: return DataType::Text;
        case SQLITE_BLOB: return DataType::Blob;
        case SQLITE_NULL: return DataType::Null;
        default: throw SQLite::OtherError("unknown column type");
    }
}

std::string SQLite::Statement::getColumnDeclType(int index) const {
    ensure();
    const char* declType = sqlite3_column_decltype(stmt, index);
    return declType ? declType : "";
}

std::string SQLite::Statement::getColumnName(int index) const {
    ensure();
    const char* name = sqlite3_column_name(stmt, index);
    return name ? name : "";
}

std::string SQLite::Statement::getColumnOriginName(int index) const {
#ifdef SQLITE_ENABLE_COLUMN_METADATA
    ensure();
    const char* originName = sqlite3_column_origin_name(stmt, index);
    return originName ? originName : "";
#else
    throw SQLite::OtherError("column metadata not enabled, to enable, define SQLITE_ENABLE_COLUMN_METADATA");
#endif
}

std::string SQLite::Statement::getColumnTableName(int index) const {
#ifdef SQLITE_ENABLE_COLUMN_METADATA
    ensure();
    const char* tableName = sqlite3_column_table_name(stmt, index);
    return tableName ? tableName : "";
#else
    throw SQLite::OtherError("column metadata not enabled, to enable, define SQLITE_ENABLE_COLUMN_METADATA");
#endif
}

std::string SQLite::Statement::getColumnDatabaseName(int index) const {
#ifdef SQLITE_ENABLE_COLUMN_METADATA
    ensure();
    const char* dbName = sqlite3_column_database_name(stmt, index);
    return dbName ? dbName : "";
#else
    throw SQLite::OtherError("column metadata not enabled, to enable, define SQLITE_ENABLE_COLUMN_METADATA");
#endif
}


int SQLite::Statement::getInt(int index) const {
    ensure();
    return sqlite3_column_int(stmt, index);
}

int64_t SQLite::Statement::getInt64(int index) const {
    ensure();
    return sqlite3_column_int64(stmt, index);
}

double SQLite::Statement::getDouble(int index) const {
    ensure();
    return sqlite3_column_double(stmt, index);
}

std::string SQLite::Statement::getString(int index) const {
    ensure();
    const unsigned char* text = sqlite3_column_text(stmt, index);
    int size = sqlite3_column_bytes(stmt, index);
    return text ? std::string(reinterpret_cast<const char*>(text), size) : std::string();
}

SQLite::Blob SQLite::Statement::getBlob(int index) const {
    ensure();
    Blob blob;
    blob.data = sqlite3_column_blob(stmt, index);
    blob.size = sqlite3_column_bytes(stmt, index);
    return blob;
}


bool SQLite::Statement::step() {
    ensure();
    int result = sqlite3_step(stmt);
    if (result == SQLITE_ROW) {
        return true;
    } else if (result == SQLITE_DONE) {
        return false;
    } else {
        throwSQLiteError(sqlite3_db_handle(stmt), "failed to step statement");
    }
    return false; // silence compiler warning
}

void SQLite::Statement::reset() {
    ensure();
    if (sqlite3_reset(stmt) != SQLITE_OK) {
        throwSQLiteError(sqlite3_db_handle(stmt), "failed to reset statement");
    }
}

void SQLite::Statement::clearBindings() {
    ensure();
    if (sqlite3_clear_bindings(stmt) != SQLITE_OK) {
        throwSQLiteError(sqlite3_db_handle(stmt), "failed to clear bindings");
    }
}

void SQLite::exec(const std::string& sql) {
    ensure();
    int result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if (result != SQLITE_OK) {
        throwSQLiteError(db, "failed to execute SQL query", sql);
    }
}
