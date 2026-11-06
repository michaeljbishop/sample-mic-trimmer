#include "projectconfig.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include "profileregistry.h"

namespace smt {

QString projectConfigPath(const QString &destRoot)
{
    return destRoot + QStringLiteral("/project.smt");
}

bool micEnabled(const ProductProfile &profile, const MicStatuses &mics, const MicSignalGroupKey &key)
{
    const MicDef *mic = profile.mic(key.mic);
    if (!mic || !profile.group(key.group))
        return false;
    return mic->locked || mics.value(key.mic).value(key.group, true);
}

bool saveProjectConfig(const QString &destRoot, const ProductProfile &profile, const MicStatuses &mics, QString *error)
{
    QJsonObject j_micSignals;
    for (const auto &mic : profile.mics) {
        QJsonObject j_groups;
        for (const auto &group : profile.groups)
            j_groups[group.key] = micEnabled(profile, mics, {mic.token, group.key});
        j_micSignals[mic.token] = j_groups;
    }

    QSaveFile file(projectConfigPath(destRoot));
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(QJsonObject{{"profile", profile.id}, {"micSignals", j_micSignals}}).toJson(QJsonDocument::Compact)) < 0
        || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

std::optional<MicStatuses> loadProjectConfig(const QString &destRoot, const ProductProfile &profile, QString *error)
{
    QFile file(projectConfigPath(destRoot));
    if (!file.open(QIODevice::ReadOnly)) {
        if (file.error() == QFileDevice::OpenError && !file.exists())
            return MicStatuses{};
        if (error)
            *error = file.errorString();
        return std::nullopt;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject()) {
        if (error)
            *error = parseError.error != QJsonParseError::NoError ? parseError.errorString()
                                                                  : QStringLiteral("not a JSON object");
        return std::nullopt;
    }

    const auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return std::nullopt;
    };
    const QJsonValue j_profile = doc.object().value("profile");
    if (!j_profile.isUndefined() && !j_profile.isString())
        return fail(QStringLiteral("profile is not a string"));
    const QString fileProfile = j_profile.isUndefined() ? kBbcsoProfileId : j_profile.toString(); // old files: BBCSO
    if (fileProfile != profile.id)
        return fail(QStringLiteral("project.smt was made for the profile %1, not %2").arg(fileProfile, profile.id));
    const QJsonValue j_signalsValue = doc.object()["micSignals"];
    if (!j_signalsValue.isUndefined() && !j_signalsValue.isObject())
        return fail(QStringLiteral("micSignals is not an object"));

    MicStatuses result;
    const QJsonObject j_micSignals = j_signalsValue.toObject();
    for (const auto &mic : profile.mics) {
        const QJsonValue j_mic = j_micSignals[mic.token];
        if (j_mic.isUndefined())
            continue;
        if (!j_mic.isObject())
            return fail(QStringLiteral("micSignals.%1 is not an object").arg(mic.token));
        const QJsonObject j_groups = j_mic.toObject();
        for (const auto &group : profile.groups) {
            const QJsonValue v = j_groups[group.key];
            if (v.isUndefined())
                continue;
            if (!v.isBool())
                return fail(QStringLiteral("micSignals.%1.%2 is not a bool").arg(mic.token, group.key));
            result[mic.token][group.key] = v.toBool();
        }
    }
    return result;
}

} // namespace smt
