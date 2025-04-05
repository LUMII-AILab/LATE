#pragma once

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <memory>
#include <type_traits>


class SQLite {
public:

    // exception classes for SQLite errors

    class Error : public std::runtime_error {
    public:
        explicit Error(const std::string& message, const std::string& sqlite_errmsg, int code, int extended_code)
            : std::runtime_error((message.empty() ? std::string("") : message + ", ")
                    + "SQLite error (" + std::to_string(code) + "," + std::to_string(extended_code) + "): " + sqlite_errmsg),
            message(message), sqlite_errmsg(sqlite_errmsg), code(code), extended_code(extended_code) {}

        const std::string message;
        const std::string sqlite_errmsg;
        const int code;
        const int extended_code;
    };

    class SyntaxError : public Error {
    public:
        explicit SyntaxError(const std::string& message, const std::string& sqlite_errmsg, int code, int extended_code, const std::string& sql, int offset = -1)
            : Error(message, sqlite_errmsg, code, extended_code), sql(sql), offset(offset) {}

        const std::string sql;
        const int offset;
    };

    class BusyError : public Error {
    public:
        using Error::Error;
    };

    class MisuseError : public Error {
    public:
        using Error::Error;
    };

    // TODO: add more custom SQLite errors

    class OtherError : public std::runtime_error {
    public:
        OtherError(const std::string& message) : std::runtime_error(message) {}
    };

    enum class OpenFlags {
        None = 0,
        ReadOnly = 1 << 0,
        ReadWrite = 1 << 1,
        Create = 1 << 2,
        URI = 1 << 3,
        Memory = 1 << 4,
        NoMutex = 1 << 5,
        FullMutex = 1 << 6,
        SharedCache = 1 << 7,
        PrivateCache = 1 << 8,
        NoFollow = 1 << 9
    };

    enum class DataType {
        Integer,
        Float,
        Text,
        Blob,
        Null
    };

    // blob data
    struct Blob {
        const void* data;
        int size;
    };

    static bool isThreadsafe(); // check if SQLite is compiled with thread-safety
    static std::unique_ptr<Error> configureSerialized(); // configure SQLite for serialized threading mode

    SQLite() {}
    SQLite(const std::string& dbName, OpenFlags flags = OpenFlags::None) { open(dbName, flags); }

    ~SQLite();

    SQLite(SQLite&& other) noexcept : db(other.db) { other.db = nullptr; }
    SQLite& operator=(SQLite&& other) noexcept { if (this != &other) { close(); db = other.db; other.db = nullptr; } return *this; }

    // delete copy constructor and copy assignment operator
    SQLite(const SQLite&) = delete;
    SQLite& operator=(const SQLite&) = delete;

    void open(const std::string& dbName, OpenFlags flags = OpenFlags::None);
    void close();

    class Statement;

    // class to encapsulate column operations
    class Column {
    public:
        Column(const Statement& statement, int index) : statement(statement), index(index) {}

        Column(const Column& other) : statement(other.statement), index(other.index) {}
        Column(Column&& other) noexcept : statement(other.statement), index(other.index) {}

        Column& operator=(const Column&) = delete;
        Column& operator=(Column&&) = delete;

        // get column values
        int getInt() const { return statement.getInt(index); }
        int64_t getInt64() const { return statement.getInt64(index); }
        double getDouble() const { return statement.getDouble(index); }
        std::string getString() const { return statement.getString(index); }
        Blob getBlob() const { return statement.getBlob(index); }

        template <typename T> T get() const {
            static_assert(std::is_same_v<T, int> ||
                          std::is_same_v<T, int64_t> ||
                          std::is_same_v<T, double> ||
                          std::is_same_v<T, std::string> ||
                          std::is_same_v<T, Blob>, "Unsupported type");
            return T();
        }
        // moved out of class as required by GCC
        // template<> int get<int>() const { return statement.getInt(index); }
        // template<> int64_t get<int64_t>() const { return statement.getInt64(index); }
        // template<> double get<double>() const { return statement.getDouble(index); }
        // template<> std::string get<std::string>() const { return statement.getString(index); }
        // template<> Blob get<Blob>() const { return statement.getBlob(index); }

        operator int() const { return statement.getInt(index); }
        operator int64_t() const { return statement.getInt64(index); }
        operator double() const { return statement.getDouble(index); }
        operator std::string() const { return statement.getString(index); }
        operator Blob() const { return statement.getBlob(index); }

        DataType type() const { return statement.getColumnType(index); }
        std::string declType() const { return statement.getColumnDeclType(index); }
        std::string name() const { return statement.getColumnName(index); }
        std::string tableName() const { return statement.getColumnTableName(index); }
        std::string databaseName() const { return statement.getColumnDatabaseName(index); }
        std::string originName() const { return statement.getColumnOriginName(index); }

        const int index;

    private:
        const Statement& statement;
    };

    class Statement {
    public:

        Statement() {}
        Statement(struct sqlite3* db, const std::string& sql, bool persistent = false);
        ~Statement();

        Statement(Statement&& other) noexcept;
        Statement& operator=(Statement&& other) noexcept;

        void finalize();

        class Parameter {
            Statement& statement;
        public:
            Parameter(Statement& statement, int index) : statement(statement), index(index) {}
            void operator=(int value) { statement.bind(index, value); }
            void operator=(int64_t value) { statement.bind(index, value); }
            void operator=(double value) { statement.bind(index, value); }
            void operator=(const std::string& value) { statement.bind(index, value); }
            void operator=(const char* value) { statement.bind(index, value); }
            void operator=(const Blob& value) { statement.bind(index, value); }
            std::string name() const { return statement.getParamName(index); }
            const int index;
        };

        // input

        int getParamIndex(const std::string& paramName) const;
        std::string getParamName(int index) const;

        Parameter param(int index) { return Parameter(*this, index); }
        Parameter param(const std::string& paramName) { return Parameter(*this, getParamIndex(paramName)); }
        Parameter param(const char* paramName) { return Parameter(*this, getParamIndex(paramName)); }

        void bind(int index, int value);
        void bind(int index, int64_t value);
        void bind(int index, double value);
        void bind(int index, const std::string& value);
        void bind(int index, const Blob& value);

        void bind(const std::string& name, int value) { bind(getParamIndex(name), value); }
        void bind(const std::string& name, int64_t value) { bind(getParamIndex(name), value); }
        void bind(const std::string& name, double value) { bind(getParamIndex(name), value); }
        void bind(const std::string& name, const std::string& value) { bind(getParamIndex(name), value); }
        void bind(const std::string& name, const Blob& value) { bind(getParamIndex(name), value); }

        // bind all arguments by position
        template<typename... Args>
        void bindAll(Args... args) { bindHelper(1, args...); }

        // get results (columns) for current row

        Column operator[](int index) const { return Column(*this, index); }
        Column operator[](const std::string& columnName) const { return Column(*this, getColumnIndex(columnName)); }
        Column operator[](const char* columnName) const { return Column(*this, getColumnIndex(columnName)); }

        int columnCount() const;
        int count() const { return columnCount(); }

        int getColumnIndex(const std::string& columnName) const; // get column index by name

        DataType getColumnType(int index) const;
        std::string getColumnDeclType(int index) const;

        DataType getColumnType(const std::string& name) const { return getColumnType(getColumnIndex(name)); }
        std::string getColumnDeclType(const std::string& name) const { return getColumnDeclType(getColumnIndex(name)); }

        std::string getColumnName(int index) const;

        std::string getColumnOriginName(int index) const;
        std::string getColumnTableName(int index) const;
        std::string getColumnDatabaseName(int index) const;

        std::string getColumnOriginName(const std::string& name) const { return getColumnOriginName(getColumnIndex(name)); }
        std::string getColumnTableName(const std::string& name) const { return getColumnTableName(getColumnIndex(name)); }
        std::string getColumnDatabaseName(const std::string& name) const { return getColumnDatabaseName(getColumnIndex(name)); }

        int getInt(int columnIndex) const;
        int64_t getInt64(int columnIndex) const;
        double getDouble(int columnIndex) const;
        std::string getString(int columnIndex) const;
        Blob getBlob(int columnIndex) const;

        int getInt(const std::string& name) const { return getInt(getColumnIndex(name)); }
        int64_t getInt64(const std::string& name) const { return getInt64(getColumnIndex(name)); }
        double getDouble(const std::string& name) const { return getDouble(getColumnIndex(name)); }
        std::string getString(const std::string& name) const { return getString(getColumnIndex(name)); }
        Blob getBlob(const std::string& name) const { return getBlob(getColumnIndex(name)); }


        bool step();

        bool exec() { return step(); }
        bool execute() { return step(); }

        void reset();
        void clearBindings();

        void reuse() { reset(); clearBindings(); }

        // statement prepared/not empty
        operator bool() const { return stmt != nullptr; }

    private:
        struct sqlite3_stmt* stmt = nullptr;

        std::unordered_map<std::string, int> columnIndices;

        // disable copying
        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;

        void initializeColumnIndices();

        // helper function to bind arguments
        template<typename T, typename... Args>
        void bindHelper(int index, T value, Args... args) { bind(index, value); bindHelper(index + 1, args...); }
        void bindHelper(int index) {} // final bind call for recursion termination

        void ensure() const { if (stmt == nullptr) throw OtherError("SQLite statement not initialized"); }
    };

    Statement prepare(const std::string& sql, bool persistent = false) { ensure(); return Statement(db, sql, persistent); }

    void exec(const std::string& sql);
    void execute(const std::string& sql) { exec(sql); }

    operator bool() const { return db != nullptr; }

private:
    struct sqlite3* db = nullptr;

    static int toSQLiteOpenFlags(OpenFlags flags);

    void ensure() const { if (db == nullptr) throw OtherError("SQLite database connection not initialized"); }
};

// overload bitwise OR operator for OpenFlags
inline SQLite::OpenFlags operator|(SQLite::OpenFlags lhs, SQLite::OpenFlags rhs) {
    using T = std::underlying_type_t<SQLite::OpenFlags>;
    return static_cast<SQLite::OpenFlags>(static_cast<T>(lhs) | static_cast<T>(rhs));
}

// overload bitwise AND operator for OpenFlags
inline bool operator&(SQLite::OpenFlags lhs, SQLite::OpenFlags rhs) {
    using T = std::underlying_type_t<SQLite::OpenFlags>;
    return static_cast<bool>(static_cast<T>(lhs) & static_cast<T>(rhs));
}

std::string to_string(SQLite::DataType type);


// specializations for the template SQLite::Column::get() function
template<> inline int SQLite::Column::get<int>() const {
    return statement.getInt(index);
}

template<> inline int64_t SQLite::Column::get<int64_t>() const {
    return statement.getInt64(index);
}

template<> inline double SQLite::Column::get<double>() const {
    return statement.getDouble(index);
}

template<> inline std::string SQLite::Column::get<std::string>() const {
    return statement.getString(index);
}

template<> inline SQLite::Blob SQLite::Column::get<SQLite::Blob>() const {
    return statement.getBlob(index);
}
