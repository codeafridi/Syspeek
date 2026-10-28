#pragma once

#include "CloudStatus.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

// Account inventory through the provider's own CLI (M83). The metadata service
// answers "which instance is this box"; the CLI answers "what else is in the
// account, and when did this box launch".
//
// Syspeek never reads or stores credentials: the CLI runs with the profile the
// user already configured, and a refusal is reported with the command that
// fixes it. The command is spawned once per cadence, never per tick, and the
// sampler polls it instead of waiting.
//
// Grounding (each provider's shipped client, kept /tmp/opencode/m83):
//   aws-cli 2.34.53: `aws ec2 describe-instances help` - "Describes the
//     specified instances or all instances... If you do not specify instance
//     IDs or filters, the output includes information for all instances";
//     `--query` "A JMESPath query to use in filtering the response data";
//     `--region` "The region to use. Overrides config/env settings."
//   azure-cli-vm 2.2.23 _params.py `--show-details, -d` "show public ip
//     address, FQDN, and power states. command will run slow"; custom.py
//     list_vm(cmd, resource_group_name=None, show_details=False);
//     azure-cli-core 2.91.0 arm.py `--subscription` "Name or ID of
//     subscription. You can configure the default subscription using `az
//     account set -s NAME_OR_ID`".
//   google-cloud-cli calliope/base.py FORMAT_FLAG "--format" "Sets the format
//     for printing command output resources."; parser_arguments.py lists
//     "--project" among the global flags (base.py: "set ``--project'' flag to
//     PROJECT ID"); surface/compute/instances/list.py "List Compute Engine
//     virtual machine instances."
//   oci-cli 3.95.1 cli_root.py:438 "--region" "The region to make calls
//     against."; compute_cli.py list_instances(..., compartment_id, ...)
//     with "--all" "Fetches all pages of results" and "--output" "The output
//     format. [Default is json]".
struct CloudFleetOptions {
    bool enabled = true;
    int timeoutMs = 25000;
};

QString cloudFleetCli(CloudProvider provider);

QStringList cloudFleetArguments(const CloudIdentity &identity);

QString cloudFleetScope(const CloudIdentity &identity);

QString cloudFleetCommandText(const CloudIdentity &identity);

// Windows installs `az` and `gcloud` as .cmd shims, which CreateProcess cannot
// start directly, so the CLI runs through the command processor:
//   cmd.exe /s /c ""<cli>" <args>"
QString cloudWindowsCmdLine(const QString &program,
                            const QStringList &arguments);

// Synchronous one-shot used by `--dump`. The HUD uses CloudFleetRunner so the
// sampler thread never waits on a CLI.
CloudFleetStatus probeCloudFleet(const CloudIdentity &identity,
                                 const CloudFleetOptions &options,
                                 qint64 nowMs = 0);

class CloudFleetRunner : public QObject {
    Q_OBJECT

public:
    explicit CloudFleetRunner(QObject *parent = nullptr);
    ~CloudFleetRunner() override;

    void setOptions(const CloudFleetOptions &options);
    CloudFleetOptions options() const;

    const CloudFleetStatus &status() const;

    // Starts the listing when the cadence expires, collects it when it
    // finishes and kills it past the deadline. Never blocks.
    void update(const CloudIdentity &identity, qint64 nowMs);

    void stop();

private:
    void begin(const CloudIdentity &identity, qint64 nowMs);
    void finish(qint64 nowMs);
    void fail(const QString &note, qint64 nowMs);
    void cleanup();

    CloudFleetOptions m_options;
    CloudFleetStatus m_status;
    CloudIdentity m_identity;
    QProcess *m_process = nullptr;
    bool m_running = false;
    qint64 m_startedAtMs = 0;
    qint64 m_lastAttemptMs = 0;
};
