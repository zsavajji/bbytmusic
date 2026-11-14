#pragma once
#include <string>

// Connects to our own server and runs every command; prints each line sent and received.
bool selftest_run(int port, const std::string &token, double seconds, double gain_db);
