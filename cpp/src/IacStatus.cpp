#include "IacStatus.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace {

IacResource fromObject(const QJsonObject &obj, const QString &prefix) {
    IacResource row;
    row.address = obj.value(QStringLiteral("address")).toString();
    if (row.address.isEmpty()) {
        const QString name = obj.value(QStringLiteral("name")).toString();
        row.address = prefix.isEmpty() ? name : prefix + QLatin1Char('.') + name;
    }
    row.type = obj.value(QStringLiteral("type")).toString();
    row.name = obj.value(QStringLiteral("name")).toString();
    row.provider = obj.value(QStringLiteral("provider_name")).toString();
    if (row.provider.isEmpty()) {
        row.provider = obj.value(QStringLiteral("provider")).toString();
    }
    row.mode = obj.value(QStringLiteral("mode")).toString();
    if (row.mode.isEmpty()) {
        row.mode = QStringLiteral("managed");
    }

    // `terraform show -json` keeps the attributes under values, the raw state
    // under instances[].attributes; both carry the provider id.
    const QJsonObject values = obj.value(QStringLiteral("values")).toObject();
    row.id = values.value(QStringLiteral("id")).toString();
    if (row.id.isEmpty()) {
        const QJsonArray instances =
            obj.value(QStringLiteral("instances")).toArray();
        if (!instances.isEmpty()) {
            row.id = instances.first()
                         .toObject()
                         .value(QStringLiteral("attributes"))
                         .toObject()
                         .value(QStringLiteral("id"))
                         .toString();
        }
    }
    if (row.name.isEmpty() && !row.address.isEmpty()) {
        row.name = row.address.section(QLatin1Char('.'), -1);
    }
    return row;
}

void walkModule(const QJsonObject &module, QList<IacResource> *out,
                const QString &prefix) {
    const QJsonArray resources = module.value(QStringLiteral("resources")).toArray();
    for (const QJsonValue &value : resources) {
        const IacResource row = fromObject(value.toObject(), prefix);
        if (row.mode.compare(QLatin1String("data"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (row.type.isEmpty() || row.name.isEmpty()) {
            continue;
        }
        out->append(row);
    }
    const QJsonArray children = module.value(QStringLiteral("child_modules")).toArray();
    for (const QJsonValue &value : children) {
        const QJsonObject child = value.toObject();
        walkModule(child, out, child.value(QStringLiteral("address")).toString());
    }
}

} // namespace

QList<IacResource> iacParseTerraformState(const QByteArray &json,
                                          QString *error) {
    if (error != nullptr) {
        error->clear();
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error != nullptr) {
            *error = parseError.error != QJsonParseError::NoError
                         ? parseError.errorString()
                         : QStringLiteral("not a JSON object");
        }
        return {};
    }
    const QJsonObject root = doc.object();
    QList<IacResource> rows;
    const QJsonObject values = root.value(QStringLiteral("values")).toObject();
    if (values.contains(QStringLiteral("root_module"))) {
        walkModule(values.value(QStringLiteral("root_module")).toObject(), &rows,
                   QString());
        return rows;
    }
    // Raw .tfstate: resources live at the top level with no module nesting.
    if (root.contains(QStringLiteral("resources"))) {
        walkModule(root, &rows, QString());
        return rows;
    }
    if (error != nullptr) {
        *error = QStringLiteral("no root_module or resources in the document");
    }
    return {};
}

IacDrift iacDiff(const QList<IacResource> &state,
                 const QList<IacResource> &cloud) {
    IacDrift drift;
    drift.available = true;
    drift.stateResources = state.size();
    drift.cloudResources = cloud.size();

    QSet<QString> cloudIds;
    QSet<QString> cloudNames;
    for (const IacResource &row : cloud) {
        if (!row.id.isEmpty()) {
            cloudIds.insert(row.id);
        }
        if (!row.name.isEmpty()) {
            cloudNames.insert(row.name);
        }
    }
    QSet<QString> stateIds;
    QSet<QString> stateNames;
    for (const IacResource &row : state) {
        if (!row.id.isEmpty()) {
            stateIds.insert(row.id);
        }
        if (!row.name.isEmpty()) {
            stateNames.insert(row.name);
        }
    }

    for (const IacResource &row : state) {
        const bool known = !row.id.isEmpty() ? cloudIds.contains(row.id)
                                             : cloudNames.contains(row.name);
        if (known) {
            ++drift.inSync;
        } else {
            drift.missing.append(row);
        }
    }
    for (const IacResource &row : cloud) {
        const bool known = !row.id.isEmpty()
                               ? stateIds.contains(row.id)
                               : stateNames.contains(row.name);
        if (!known) {
            drift.unmanaged.append(row);
        }
    }
    return drift;
}

QString iacSummaryText(const IacDrift &drift) {
    if (!drift.available) {
        return QStringLiteral("iac: no terraform state read yet");
    }
    return QStringLiteral(
               "%1 state resource(s) vs %2 listed - %3 in sync, %4 missing from "
               "the account, %5 unmanaged")
        .arg(drift.stateResources)
        .arg(drift.cloudResources)
        .arg(drift.inSync)
        .arg(drift.missing.size())
        .arg(drift.unmanaged.size());
}

QString iacDriftText(const IacDrift &drift, int cap) {
    if (!drift.available) {
        return iacGateText(drift.path);
    }
    QStringList lines;
    int shown = 0;
    for (const IacResource &row : drift.missing) {
        if (shown++ >= cap) {
            lines << QStringLiteral("... %1 more missing")
                         .arg(drift.missing.size() - shown + 1);
            break;
        }
        lines << QStringLiteral("MISSING    %1  (%2, id %3)")
                     .arg(row.address, row.type,
                          row.id.isEmpty() ? QStringLiteral("unknown") : row.id);
    }
    shown = 0;
    for (const IacResource &row : drift.unmanaged) {
        if (shown++ >= cap) {
            lines << QStringLiteral("... %1 more unmanaged")
                         .arg(drift.unmanaged.size() - shown + 1);
            break;
        }
        lines << QStringLiteral("UNMANAGED  %1  (%2, id %3)")
                     .arg(row.name.isEmpty() ? row.address : row.name,
                          row.type,
                          row.id.isEmpty() ? QStringLiteral("unknown") : row.id);
    }
    if (lines.isEmpty()) {
        return QStringLiteral(
            "no drift - every state resource exists in the account and nothing "
            "was created outside terraform");
    }
    lines.prepend(QStringLiteral(
        "MISSING = in the state, gone from the account (deleted out of band). "
        "UNMANAGED = in the account, not in the state."));
    return lines.join(QLatin1Char('\n'));
}

QString iacGateText(const QString &path) {
    if (path.isEmpty()) {
        return QStringLiteral(
            "iac: no terraform state configured - set [cloud] terraform_state="
            " to the file `terraform show -json` wrote");
    }
    return QStringLiteral(
               "iac: no readable state at %1 - run `terraform show -json > %1` "
               "or point [cloud] terraform_state= at the file")
        .arg(path);
}

QString iacParseErrorText(const QString &error) {
    return QStringLiteral("iac: the state did not parse (%1)").arg(error);
}

QString iacSettingsLine(const QString &path, const IacDrift &drift) {
    if (path.isEmpty()) {
        return QStringLiteral("iac off (no [cloud] terraform_state)");
    }
    if (!drift.available) {
        return QStringLiteral("iac %1 (not read yet)").arg(path);
    }
    return QStringLiteral("iac %1 - %2 unmanaged, %3 missing")
        .arg(path)
        .arg(drift.unmanaged.size())
        .arg(drift.missing.size());
}
