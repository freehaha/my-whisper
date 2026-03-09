#pragma once

#include "types.h"

#include <QString>

class Config {
public:
    static QString configPath();
    static AppConfig defaultConfig();
    static AppConfig load();
    static bool save(const AppConfig &config, QString *errorMessage = nullptr);

    static bool hasValidDeepgramKey(const AppConfig &config);
    static bool hasUsableRefiner(const AppConfig &config);
};
