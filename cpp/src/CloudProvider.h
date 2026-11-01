#pragma once

#include <QString>

// Which cloud provider this host reports, shared by the metadata model
// (CloudStatus.h), the account inventory (CloudFleetSource.h) and the billed
// spend query (CloudSpendStatus.h) so none of them has to include another.
enum class CloudProvider {
    None,
    Aws,
    Azure,
    Gcp,
    Oci,
};

QString cloudProviderName(CloudProvider provider);
// "aws"/"amazon"/"ec2", "azure", "gcp"/"google", "oci"/"oracle" -> the enum;
// anything else is None. Used by the [cloud] identity= fallback parser.
CloudProvider cloudProviderFromName(const QString &name);
// "aws"/"amazon"/"ec2", "azure"/"az", "gcp"/"google", "oci"/"oracle" -> the
// enum; anything else is None.
CloudProvider cloudProviderFromName(const QString &name);
QString cloudProviderLabel(CloudProvider provider);
