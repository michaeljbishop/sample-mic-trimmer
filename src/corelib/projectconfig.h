#pragma once

#include <optional>
#include <QString>
#include "smt.h"

namespace smt {

QString projectConfigPath(const QString &destRoot); // <destRoot>/project.smt
// Writes every mic x group of the profile (keys it does not know are dropped); loading ignores unknown keys.
bool saveProjectConfig(const QString &destRoot, const ProductProfile &profile, const MicStatuses &mics, QString *error);
std::optional<MicStatuses> loadProjectConfig(const QString &destRoot, const ProductProfile &profile, QString *error);
// False for a mic or group the profile does not know; true for a locked mic; else the stored value (default on).
bool micEnabled(const ProductProfile &profile, const MicStatuses &mics, const MicSignalGroupKey &key);

} // namespace smt
