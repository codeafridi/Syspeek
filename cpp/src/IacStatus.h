#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Infrastructure-as-code drift (M91): read the Terraform state the user already
// keeps and diff it against the account listing the Cloud page already polls.
// Read-only in both directions - nothing is applied, imported or destroyed.
//
// Grounding (shipped terraform 1.14.4 linux-amd64, strings kept
// /tmp/opencode/m8791/m91/tf.strings - the state schema keys the parser
// reads): `format_version`, `terraform_version`, `root_module`,
// `child_modules`, `"resources"`, `"mode"`, `"type"`, `"name"`,
// `provider_name`, `"instances"`, `"values"`, `schema_version`.
//
// Two shapes are accepted because the CLI emits two:
//   `terraform show -json`  -> values.root_module.resources[] (+ child_modules)
//   the raw `.tfstate`      -> resources[] with instances[].attributes
struct IacResource {
    QString address;   // aws_instance.web / module.db.aws_db_instance.main
    QString type;      // aws_instance
    QString name;      // web
    QString provider;  // registry.terraform.io/hashicorp/aws
    QString id;        // the provider id (i-0abc...), empty when unknown
    QString mode;      // managed / data
};

struct IacDrift {
    bool available = false;
    QString note;
    QString path;
    int stateResources = 0;
    int cloudResources = 0;
    // In the state, missing from the account: something was deleted out of
    // band (or the state is stale).
    QList<IacResource> missing;
    // In the account, not in the state: created out of band, so the next
    // apply would not manage it.
    QList<IacResource> unmanaged;
    int inSync = 0;
};

// --- parsers (pure) ------------------------------------------------------
// Returns managed resources only: a `data` source is a lookup, not an asset.
QList<IacResource> iacParseTerraformState(const QByteArray &json,
                                          QString *error);

// Diffs the state against the account listing. Matching is by id first (the
// provider id both sides carry), then by name for resources whose id the
// state does not expose.
IacDrift iacDiff(const QList<IacResource> &state,
                 const QList<IacResource> &cloud);

// --- text ----------------------------------------------------------------
QString iacSummaryText(const IacDrift &drift);
QString iacDriftText(const IacDrift &drift, int cap = 8);
QString iacGateText(const QString &path);
QString iacParseErrorText(const QString &error);
QString iacSettingsLine(const QString &path, const IacDrift &drift);
