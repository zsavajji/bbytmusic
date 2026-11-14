#pragma once
#include <string>

#include "store/db.h"

namespace bbm {

std::string get_setting(Db &db, const std::string &key, const std::string &dflt);  // dflt if absent or on error
bool set_setting(Db &db, const std::string &key, const std::string &value, std::string &err);

}  // namespace bbm
