// The store's schema, as numbered migrations.
#pragma once
#include <string>

#include "store/db.h"

namespace bbm {

constexpr int kSchemaVersion = 1;

// Opens the database and brings it to kSchemaVersion. Never deletes and starts again; a
// database newer than this build is refused.
bool open_store(Db &db, const std::string &path, std::string &err);

}  // namespace bbm
